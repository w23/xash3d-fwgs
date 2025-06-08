#include "VCombuf.h"
#include "VCommandPool.h"
#include "VPerfQuery.h"
#include "vk_logs.h"

#include "std/arrays.h"
#include "std/profiler.h"

#define LOG_MODULE combuf

#define MAX_GPU_SCOPES 64
#define MAX_COMMANDBUFFERS 6
#define MAX_TIMESTAMP_QUERIES 128
#define MAX_PERFORMANCE_QUERIES 64
#define MAX_PERFORMANCE_QUERY_COUNTERS 16

// Rough theoretical max is (((MAX_TIMESTAMP_QUERIES) + (MAX_PERFORMANCE_QUERIES) * (MAX_PERFORMANCE_QUERY_COUNTERS))
#define MAX_PROF_EVENTS 1024

typedef struct {
	vk_combuf_t public;
	int used;
	struct {
		// Offset into timestamp query results array
		int timestamps_offset;
		int timestamp_queries;

		aprof_event_t events[MAX_PROF_EVENTS];
		int events_count;

		int active_perf_query;

		// Performance query object that was used for the last submission.
		// If it is not the *current* performance query object from `g_combuf`, then the counters are ignored.
		//const VPerfQuery *perf_query_used;
	} profiler;
} vk_combuf_impl_t;

static struct {
	vk_command_pool_t pool;

	vk_combuf_impl_t combufs[MAX_COMMANDBUFFERS];
	struct {
		VkQueryPool pool;
		uint64_t values[MAX_TIMESTAMP_QUERIES * MAX_COMMANDBUFFERS];
	} timestamp;

	aprof_scope_t scopes[MAX_GPU_SCOPES];
	int scopes_count;

	int entire_combuf_scope_id;

	struct {
		VPerfQuery *query;
		ARRAY_DYNAMIC_DECLARE(uint32_t, counters);
	} perf;
} g_combuf;


qboolean R_VkCombuf_Init( void ) {
	g_combuf.pool = R_VkCommandPoolCreate(MAX_COMMANDBUFFERS);
	if (!g_combuf.pool.pool)
		return false;

	const VkQueryPoolCreateInfo qpci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.pNext = NULL,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = COUNTOF(g_combuf.timestamp.values),
		.flags = 0,
	};

	XVK_CHECK(vkCreateQueryPool(vk_core.device, &qpci, NULL, &g_combuf.timestamp.pool));

	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		vk_combuf_impl_t *const cb = g_combuf.combufs + i;

		cb->public.cmdbuf = g_combuf.pool.buffers[i];
		SET_DEBUG_NAMEF(cb->public.cmdbuf, VK_OBJECT_TYPE_COMMAND_BUFFER, "cmdbuf[%d]", i);

		cb->profiler.timestamps_offset = i * MAX_TIMESTAMP_QUERIES;
	}

	g_combuf.entire_combuf_scope_id = R_VkGpuScope_Register("GPU");

	arrayDynamicInitT(&g_combuf.perf.counters);

	// FIXME test-only! This should be a command!
	const uint32_t counters[] = {0, 1, 2, 10, 11};
	R_VkCombufPerfQueryEnable(counters, COUNTOF(counters));

	return true;
}

static void perfQueryCleanup(void) {
	if (!g_combuf.perf.query)
		return;

	// FIXME reset all combufs query refs

	vkReleaseProfilingLockKHR(v_device);

	vPerfQueryDestroy(g_combuf.perf.query);
	g_combuf.perf.query = NULL;

	arrayDynamicDestroyT(&g_combuf.perf.counters);
}

void R_VkCombuf_Destroy( void ) {
	perfQueryCleanup();

	vkDestroyQueryPool(vk_core.device, g_combuf.timestamp.pool, NULL);
	R_VkCommandPoolDestroy(&g_combuf.pool);

	for (int i = 0; i < g_combuf.scopes_count; ++i) {
		Mem_Free((char*)g_combuf.scopes[i].name);
	}
}

vk_combuf_t* R_VkCombufOpen( void ) {
	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		vk_combuf_impl_t *const cb = g_combuf.combufs + i;
		if (!cb->used) {
			cb->used = 1;
			cb->profiler.active_perf_query = -1;
			return &cb->public;
		}
	}

	return NULL;
}

void R_VkCombufClose( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;
	cb->used = 0;

	// TODO synchronize?
	// For now, external synchronization expected
}

void R_VkCombufBegin( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	cb->profiler.events_count = 0;
	cb->profiler.timestamp_queries = 0;
	cb->profiler.active_perf_query = -1;

	const VkCommandBufferBeginInfo beginfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	XVK_CHECK(vkBeginCommandBuffer(cb->public.cmdbuf, &beginfo));

	vkCmdResetQueryPool(cb->public.cmdbuf, g_combuf.timestamp.pool, cb->profiler.timestamps_offset, MAX_TIMESTAMP_QUERIES);
	R_VkCombufScopeBegin(pub, g_combuf.entire_combuf_scope_id, VCombufScopeFlag_None);
}

void R_VkCombufEnd( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	R_VkCombufScopeEnd(pub, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	XVK_CHECK(vkEndCommandBuffer(cb->public.cmdbuf));
}

static const char* myStrdup(const char *src) {
	const int len = strlen(src);
	char *ret = Mem_Malloc(vk_core.pool, len + 1);
	memcpy(ret, src, len);
	ret[len] = '\0';
	return ret;
}

int R_VkGpuScope_Register(const char *name) {
	// Find existing scope with the same name
	for (int i = 0; i < g_combuf.scopes_count; ++i) {
		if (Q_strcmp(name, g_combuf.scopes[i].name) == 0)
			return i;
	}

	if (g_combuf.scopes_count == MAX_GPU_SCOPES) {
		gEngine.Con_Printf(S_ERROR "Cannot register GPU profiler scope \"%s\": max number of scope %d reached\n", name, MAX_GPU_SCOPES);
		return -1;
	}

	g_combuf.scopes[g_combuf.scopes_count] = (aprof_scope_t) {
		.name = myStrdup(name),
		.flags = 0,
		.source_file = __FILE__, // TODO
		.source_line = __LINE__, // TODO
	};

	return g_combuf.scopes_count++;
}

static int combufAppendPerfEvent(vk_combuf_impl_t *cb, aprof_event_t event) {
	if (cb->profiler.events_count >= COUNTOF(cb->profiler.events)) {
		ERROR_THROTTLED(10, "Command buffer %p ran out of profiler event slots (max %d) trying to write event %#08llx",
			cb, (int)COUNTOF(cb->profiler.events), (unsigned long long)event);
		return -1;
	}

	const int event_index = cb->profiler.events_count++;
	cb->profiler.events[event_index] = event;
	return event_index;
}

static void scopePerfQueryBegin(vk_combuf_impl_t *cb, uint32_t flags) {
	// There should be no active query
	ASSERT(cb->profiler.active_perf_query == -1);

	if ((flags & VCombufScopeFlag_PerfQuery) == 0)
		return;

	if (!g_combuf.perf.query)
		return;

	const int perf_query_index = vPerfQueryBegin(g_combuf.perf.query, &cb->public);

	if (LOG_VERBOSE)
		DEBUG("Begin perf_query id=%d", perf_query_index);

	if (perf_query_index < 0)
		return;

	cb->profiler.active_perf_query = perf_query_index;
}

static void scopePerfQueryEnd(vk_combuf_impl_t *cb) {
	if (cb->profiler.active_perf_query < 0)
		return;

	ASSERT(g_combuf.perf.query);

	if (LOG_VERBOSE)
		DEBUG("End perf_query id=%d", cb->profiler.active_perf_query);

	vPerfQueryEnd(g_combuf.perf.query, &cb->public, cb->profiler.active_perf_query);

	for (size_t i = 0; i < g_combuf.perf.counters.count; ++i) {
		combufAppendPerfEvent(cb, COMBUF_EVENT_MAKE_COUNTER(i, cb->profiler.active_perf_query));
	}

	cb->profiler.active_perf_query = -1;
}

static int writeTimestamp(vk_combuf_impl_t *cb, int scope_id, int event_type, VkPipelineStageFlagBits pipeline_stage) {
	if (cb->profiler.timestamp_queries >= MAX_TIMESTAMP_QUERIES) {
		ERROR_THROTTLED(10, "Command buffer %p ran out of max timestamp query slots (%d) with scope \"%s\" (%d)",
			cb, MAX_TIMESTAMP_QUERIES, g_combuf.scopes[scope_id].name, scope_id);
		return -1;
	}

	const uint32_t timestamp_index = cb->profiler.timestamp_queries++;
	const uint32_t timestamp_query_index = cb->profiler.timestamps_offset + timestamp_index;
	vkCmdWriteTimestamp(cb->public.cmdbuf, pipeline_stage, g_combuf.timestamp.pool, timestamp_query_index);
	return combufAppendPerfEvent(cb, APROF_EVENT_MAKE(event_type, scope_id, timestamp_index));
}

int R_VkCombufScopeBegin(vk_combuf_t* cumbuf, int scope_id, uint32_t flags) {
	if (scope_id < 0)
		return -1;

	ASSERT(scope_id < g_combuf.scopes_count);

	if (LOG_VERBOSE) {
		DEBUG("Begin scope id=%d (%s) flags=%#x", scope_id, g_combuf.scopes[scope_id].name, flags);
	}

	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)cumbuf;
	const int event_index = writeTimestamp(cb, scope_id, APROF_EVENT_SCOPE_BEGIN, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

	scopePerfQueryBegin(cb, flags);

	return event_index;
}

void R_VkCombufScopeEnd(vk_combuf_t* combuf, int begin_index, VkPipelineStageFlagBits pipeline_stage) {
	if (begin_index < 0)
		return;

	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)combuf;
	// TODO: ASSERT that this is the right scope
	const int scope_id = APROF_EVENT_SCOPE_ID(cb->profiler.events[begin_index]);

	if (LOG_VERBOSE) {
		DEBUG("End scope id=%d (%s)", scope_id, g_combuf.scopes[scope_id].name);
	}

	scopePerfQueryEnd(cb);
	writeTimestamp(cb, scope_id, APROF_EVENT_SCOPE_END, pipeline_stage);
}

int R_VkCombufPerfQueryEnable(const uint32_t *counters, uint32_t counters_count) {
	if (!v_device_info.perf_query) {
		ERR("Cannot enable perf query counters, as VK_KHR_performance_query is not available");
		return 0;
	}

	// Validate counters
	for (uint32_t i = 0; i < counters_count; ++i) {
		const uint32_t counter = counters[i];
		if (counter > v_device_info.perf_counters.count) {
			ERR("Counter %u is invalid, max %u counters are available", counter, v_device_info.perf_counters.count);
			return 0;
		}

		for (uint32_t j = 0; j < i; ++j) {
			if (counters[j] == counter) {
				ERR("Duplicate counter %u", counter);
				return 0;
			}
		}
	}

	VPerfQuery *const new_query = vPerfQueryCreate(counters, counters_count, MAX_COMMANDBUFFERS * MAX_TIMESTAMP_QUERIES);
	if (!new_query) {
		ERR("Couldn't create performance query with %u counters", counters_count);
		return 0;
	}

	// Further operations affect the entire device, so make sure everything is stopped
	vkDeviceWaitIdle(v_device);

	perfQueryCleanup();

	arrayDynamicResizeT(&g_combuf.perf.counters, counters_count);
	for (uint32_t i = 0; i < counters_count; ++i) {
		g_combuf.perf.counters.items[i] = counters[i];
	}

	g_combuf.perf.query = new_query;

	const VkAcquireProfilingLockInfoKHR apli = {
		.sType = VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR,
		.timeout = UINT64_MAX,
	};
	XVK_CHECK(vkAcquireProfilingLockKHR(v_device, &apli));

	return 1;
}

static uint64_t getGpuTimestampOffsetNs( uint64_t latest_gpu_timestamp, uint64_t latest_cpu_timestamp_ns ) {
	// FIXME this is an incorrect check, we need to carry per-device extensions availability somehow. vk_core-vs-device refactoring pending
	if (!vkGetCalibratedTimestampsEXT) {
		// Estimate based on supposed submission time, assuming that we submit, and it starts computing right after cmdbuffer closure
		// which may not be true. But it's all we got
		// TODO alternative approach: estimate based on end timestamp
		const uint64_t gpu_begin_ns = (double) latest_gpu_timestamp * v_device_info.properties.limits.timestampPeriod;
		return latest_cpu_timestamp_ns - gpu_begin_ns;
	}

	const VkCalibratedTimestampInfoEXT cti[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
			.pNext = NULL,
			.timeDomain = VK_TIME_DOMAIN_DEVICE_EXT,
		},
		{
			.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
			.pNext = NULL,
#if defined(_WIN32)
			.timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT,
#else
			.timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
#endif
		},
	};

	uint64_t timestamps[2] = {0};
	uint64_t max_deviation[2] = {0};
	vkGetCalibratedTimestampsEXT(vk_core.device, 2, cti, timestamps, max_deviation);

	const uint64_t cpu = aprof_time_platform_to_ns(timestamps[1]);
	const uint64_t gpu = (double)timestamps[0] * v_device_info.properties.limits.timestampPeriod;
	return cpu - gpu;
}

static void patchTimestampQueryEvents(vk_combuf_impl_t *cb) {
	const int timestamps_count = cb->profiler.timestamp_queries;
	if (timestamps_count <= 0)
		return;

	ASSERT(timestamps_count <= MAX_TIMESTAMP_QUERIES);
	uint64_t timestamps[MAX_TIMESTAMP_QUERIES];

	XVK_CHECK(vkGetQueryPoolResults(vk_core.device, g_combuf.timestamp.pool, cb->profiler.timestamps_offset,
		timestamps_count, timestamps_count * sizeof(uint64_t),
		timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

	const uint64_t timestamp_offset_ns = getGpuTimestampOffsetNs(timestamps[1], aprof_time_now_ns());
	// `double` is necessary here for 64 bit precision
	const double timestamp_period = v_device_info.properties.limits.timestampPeriod;

	// Patch timestamp events with timestamp indexes with real timestamp values
	for (int i = 0; i < cb->profiler.events_count; ++i) {
		aprof_event_t *const event = &cb->profiler.events[i];
		const int event_type = APROF_EVENT_TYPE(*event);
		switch (event_type) {
			case APROF_EVENT_SCOPE_BEGIN:
			case APROF_EVENT_SCOPE_END:
				{
					const uint64_t scope_id = APROF_EVENT_SCOPE_ID(*event);
					const uint64_t timestamp_index = APROF_EVENT_TIMESTAMP(*event);
					ASSERT(timestamp_index < timestamps_count);
					const uint64_t timestamp = (uint64_t)(timestamps[timestamp_index] * timestamp_period) + timestamp_offset_ns;
					const aprof_event_t new_event = APROF_EVENT_MAKE(event_type, scope_id, timestamp);
					*event = new_event;
					break;
				}
		}
	}
}

static uint64_t scaledCounter(VkPerformanceCounterStorageKHR storage, VkPerformanceCounterResultKHR result, int scale) {
	switch (storage) {
		case VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR: return result.int32 * scale; break;
		case VK_PERFORMANCE_COUNTER_STORAGE_INT64_KHR: return result.int64 * scale; break;
		case VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR: return result.uint32 * scale; break;
		case VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR: return result.uint64 * scale; break;
		case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR: return result.float32 * scale; break;
		case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR: return result.float64 * scale; break;
		default:
			ERR("Invalud performance counter storage %08x", storage);
			return 0;
	}
}

static uint64_t computeCounterValue(uint32_t counter_index, VkPerformanceCounterResultKHR result) {
	const VkPerformanceCounterKHR *const cnt = &v_device_info.perf_counters.counters[counter_index];
	switch (cnt->unit) {
		case VK_PERFORMANCE_COUNTER_UNIT_PERCENTAGE_KHR:
			// Percentage is always in permyriad, i.e. hundredth-percent: 10000 is 100%, 2317 is 23.17%
			return scaledCounter(cnt->storage, result, 100);
			break;
		default:
			return scaledCounter(cnt->storage, result, 1);
			break;
	}
}

static void patchPeformanceQueryEvents(vk_combuf_impl_t *cb) {
	ASSERT(cb->profiler.active_perf_query < 0);

	if (!g_combuf.perf.query)
		return;

	for (int i = 0; i < cb->profiler.events_count; ++i) {
		aprof_event_t *const event = &cb->profiler.events[i];
		const int event_type = APROF_EVENT_TYPE(*event);
		if (event_type != COMBUF_PROF_EVENT_PERF_COUNTER)
			continue;

		const uint64_t query_index = COMBUF_EVENT_COUNTER_VALUE(*event);
		const VkPerformanceCounterResultKHR* const results = vPerfQueryRead(g_combuf.perf.query, &cb->public, query_index);

		for (uint32_t j = 0; j < g_combuf.perf.counters.count; ++j) {
			aprof_event_t *const event = &cb->profiler.events[i + j];

			// Make sure that the right slot is reserved
			const int event_type = APROF_EVENT_TYPE(*event);
			ASSERT(event_type == COMBUF_PROF_EVENT_PERF_COUNTER);

			// Make sure we're writing into the correct slot
			const uint64_t counter_index = COMBUF_EVENT_COUNTER_INDEX(*event);
			ASSERT(counter_index == j);

			*event = COMBUF_EVENT_MAKE_COUNTER(j, computeCounterValue(j, results[j]));
		} // for events in counters reserved block

		// Skip the entire reserved block
		i += g_combuf.perf.counters.count;

		// After the perf counters block there should always be a scope_end event by design
		{
			ASSERT(i < cb->profiler.events_count);
			const aprof_event_t next_event = cb->profiler.events[i];
			const int next_event_type = APROF_EVENT_TYPE(next_event);
			ASSERT(next_event_type == APROF_EVENT_SCOPE_END);

			const uint64_t scope_id = APROF_EVENT_SCOPE_ID(next_event);
			ASSERT(scope_id < g_combuf.scopes_count);

			INFO("Scope [%s] perf counters:", g_combuf.scopes[scope_id].name);
			for (uint32_t i = 0; i < g_combuf.perf.counters.count; ++i) {
				const uint32_t counter = g_combuf.perf.counters.items[i];
				INFO("\t%s (%d) = %f", v_device_info.perf_counters.desc[counter].name, i, results[i].float64);
			}
		} // next_event block
	} // for all events
} // patchTimestampQueryEvents()

VCombufProfilingResult R_VkCombufProfilingGetResult(vk_combuf_t *pub) {
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	patchTimestampQueryEvents(cb);
	patchPeformanceQueryEvents(cb);

	uint64_t begin_ns = 0, end_ns = 0;
	if (cb->profiler.events_count > 1) {
		begin_ns = APROF_EVENT_TIMESTAMP(cb->profiler.events[0]);
		end_ns = APROF_EVENT_TIMESTAMP(cb->profiler.events[cb->profiler.events_count-1]);
	}

	APROF_SCOPE_END(function);

	return (VCombufProfilingResult) {
		.begin_ns = begin_ns,
		.end_ns = end_ns,
		.scopes = {
			.items = g_combuf.scopes,
			.count = g_combuf.scopes_count,
		},
		.enabled_perf_counters = {
			.items = g_combuf.perf.counters.items,
			.count = g_combuf.perf.counters.count,
		},
		.events = {
			.items = cb->profiler.events,
			.count = cb->profiler.events_count,
		},
	};
}
