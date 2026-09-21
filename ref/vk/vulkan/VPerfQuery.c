#include "VPerfQuery.h"
#include "VCombuf.h"

#include "vk_logs.h"

#define LOG_MODULE combuf

typedef enum {
	QueryState_Available,
	QueryState_Began,
	QueryState_Ended,
} QueryState;

struct VPerfQuery {
	VkQueryPool pool;

	VkPerformanceCounterResultKHR *results;
	uint32_t counters;

	struct {
		QueryState *states;
		uint32_t max;
	} queries;
};

VPerfQuery *vPerfQueryCreate(const uint32_t *counters, uint32_t counters_count, uint32_t max_queries) {
	VPerfQuery pq = {0};

	VkQueryPoolPerformanceCreateInfoKHR qppci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR,
		.pNext = NULL,
		.counterIndexCount = counters_count,
		.pCounterIndices = counters,
		.queueFamilyIndex = v_device_info.queue_index,
	};

	uint32_t passes_count = 0;
	vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(v_device_info.physical_device, &qppci, &passes_count);
	if (passes_count != 1) {
		ERR("Performance query with %d counters needs %d passes. Only a single pass is supported.",
			counters_count, passes_count);
		return NULL;
	}

	VkQueryPoolCreateInfo qpci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.pNext = &qppci,
		.queryType = VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR,
		.queryCount = max_queries,
	};

	XVK_CHECK(vkCreateQueryPool(v_device, &qpci, NULL, &pq.pool));
	pq.queries.max = max_queries;

	vkResetQueryPool(v_device, pq.pool, 0, max_queries);

	const size_t results_size = sizeof(VkPerformanceCounterResultKHR) * counters_count;
	const size_t queries_size = sizeof(QueryState) * max_queries;
	const size_t total_size = sizeof(VPerfQuery) + queries_size + results_size;

	VPerfQuery *const ret = Mem_Malloc(vk_core.pool, total_size);
	*ret = pq;
	ret->queries.states = (QueryState*)((char*)ret + sizeof(VPerfQuery));
	ret->results = (VkPerformanceCounterResultKHR*)((char*)ret->queries.states + queries_size);
	ret->counters = counters_count;
	for (uint32_t i = 0; i < ret->queries.max; ++i) {
		ret->queries.states[i] = QueryState_Available;
	}
	return ret;
}

void vPerfQueryDestroy(VPerfQuery *pq) {
	vkDestroyQueryPool(v_device, pq->pool, NULL);
}

int vPerfQueryBegin(VPerfQuery *pq, struct vk_combuf_s *cb) {
	for (uint32_t i = 0; i < pq->queries.max; ++i) {
		if (pq->queries.states[i] != QueryState_Available)
			continue;

		vkCmdBeginQuery(cb->cmdbuf, pq->pool, i, 0);
		pq->queries.states[i] = QueryState_Began;
		return i;
	}

	return -1;
}

void vPerfQueryEnd(VPerfQuery *pq, struct vk_combuf_s *cb, uint32_t query_index) {
	if (query_index >= pq->queries.max)
		return;

	ASSERT(pq->queries.states[query_index] == QueryState_Began);

	vkCmdPipelineBarrier(cb->cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, NULL, 0, NULL, 0, NULL);
	vkCmdEndQuery(cb->cmdbuf, pq->pool, query_index);

	pq->queries.states[query_index] = QueryState_Ended;
}

const VkPerformanceCounterResultKHR* vPerfQueryRead(VPerfQuery *pq, struct vk_combuf_s *cb, uint32_t query_index) {
	if (query_index >= pq->queries.max)
		return NULL;

	ASSERT(pq->queries.states[query_index] == QueryState_Ended);

	const uint32_t firstQuery = query_index;
	const uint32_t queryCount = 1;
	const size_t dataSize = pq->counters * sizeof(VkPerformanceCounterResultKHR);
	const VkDeviceSize stride = pq->counters * sizeof(VkPerformanceCounterResultKHR);
	XVK_CHECK(vkGetQueryPoolResults(v_device, pq->pool,
		firstQuery, queryCount, dataSize,
		pq->results, stride,
		VK_QUERY_RESULT_WAIT_BIT));
	vkResetQueryPool(v_device, pq->pool, firstQuery, queryCount);

	pq->queries.states[query_index] = QueryState_Available;
	return pq->results;
}
