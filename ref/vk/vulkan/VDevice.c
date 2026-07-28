#include "VDevice.h"

#include "vk_common.h"
#include "std/arrays.h"
#include "vk_logs.h"

#define LOG_MODULE core

VDeviceInfo v_device_info = {0};
VkDevice v_device = VK_NULL_HANDLE;

#define X(f) PFN_##f f = NULL;
	DEVICE_FUNCS(X)
	DEVICE_FUNCS_RTX(X)
#undef X

static dllfunc_t device_funcs[] = {
#define X(f) {#f, (void**)&f},
	DEVICE_FUNCS(X)
#undef X
};

static dllfunc_t device_funcs_rtx[] = {
#define X(f) {#f, (void**)&f},
	DEVICE_FUNCS_RTX(X)
#undef X
};

static const char* device_extensions_req[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const char* device_extensions_rt[] = {
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,

	// TODO optional under -vkvalidate
	VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
};

static const char* device_extensions_nv_checkpoint[] = {
	VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,
	VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME,
};

static const char* device_extensions_extra[] = {
	VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
};

static const char* device_extensions_perf_query[] = {
	VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME,
};

static const VkExtensionProperties *findExtension( const VkExtensionProperties *exts, uint32_t num_exts, const char *extension ) {
	for (uint32_t i = 0; i < num_exts; ++i) {
		if (strncmp(exts[i].extensionName, extension, sizeof(exts[i].extensionName)) == 0)
			return exts + i;
	}
	return NULL;
}

static qboolean deviceSupportsExtensions(const VkExtensionProperties *exts, uint32_t num_exts, const char *check_extensions[], int check_extensions_count) {
	qboolean result = true;
	for (int i = 0; i < check_extensions_count; ++i) {
		if (!findExtension(exts, num_exts, check_extensions[i])) {
			WARN("Extension %s is not supported", check_extensions[i]);
			result = false;
		}
	}
	return result;
}

static void devicePrintExtensionsFromList(const VkExtensionProperties *exts, uint32_t num_exts, const char *print_extensions[], int print_extensions_count) {
	for (int i = 0; i < print_extensions_count; ++i) {
		const VkExtensionProperties *const ext_prop = findExtension(exts, num_exts, print_extensions[i]);
		if (!ext_prop) {
			INFO("\t\t\t%s: N/A", print_extensions[i]);
		} else {
			INFO("\t\t\t%s: %u.%u.%u", ext_prop->extensionName, XVK_PARSE_VERSION(ext_prop->specVersion));
		}
	}
}

#define MAX_DEVICE_EXTENSIONS 16
static int appendDeviceExtensions(const char** out, int out_count, const char *in_extensions[], int in_extensions_count) {
	for (int i = 0; i < in_extensions_count; ++i) {
		ASSERT(out_count < MAX_DEVICE_EXTENSIONS);
		out[out_count++] = in_extensions[i];
	}
	return out_count;
}

#define GET_VULKAN_ARRAY(TYPE, NAME, FUNC) \
	struct { \
		TYPE *items; \
		uint32_t count; \
	} NAME = {0}; \
	FUNC(&NAME.count, NULL); \
	NAME.items = Mem_Malloc(vk_core.pool, sizeof(TYPE) * NAME.count); \
	FUNC(&NAME.count, NAME.items)

static uint32_t findUsableQueueFamilyIndex(VkPhysicalDevice physdev) {
#define FUNC(COUNT, ITEMS) vkGetPhysicalDeviceQueueFamilyProperties(physdev, COUNT, ITEMS)
	GET_VULKAN_ARRAY(VkQueueFamilyProperties, queue_family_props, FUNC);
#undef FUNC

	// Find queue family that supports needed properties
	uint32_t usable_queue_index = VK_QUEUE_FAMILY_IGNORED;
	for (uint32_t i = 0; i < queue_family_props.count; ++i) {
		VkBool32 supports_present = 0;
		const qboolean supports_graphics = !!(queue_family_props.items[i].queueFlags & VK_QUEUE_GRAPHICS_BIT);
		const qboolean supports_compute = !!(queue_family_props.items[i].queueFlags & VK_QUEUE_COMPUTE_BIT);
		vkGetPhysicalDeviceSurfaceSupportKHR(physdev, i, vk_core.surface.surface, &supports_present);

		INFO("\t\tQueue %d/%d present: %d graphics: %d compute: %d", i, queue_family_props.count,
			supports_present, supports_graphics, supports_compute);

		if (!supports_present)
			continue;

		// ray tracing needs compute
		// also, by vk spec graphics queue must support compute
		if (!supports_graphics || !supports_compute)
			continue;

		usable_queue_index = i;
		break;
	}

	Mem_Free(queue_family_props.items);
	return usable_queue_index;
}

#if 0 // TODO
static void addRefDeviceT(void) {
		// Store devices list in vk_core.devices for pfnGetRenderDevices
		vk_core.devices[i].vendorID = props.vendorID;
		vk_core.devices[i].deviceID = props.deviceID;
		switch( props.deviceType )
		{
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_INTERGRATED_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_DISCRETE_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_VIRTUAL_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_CPU;
			break;
		default:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_OTHER;
			break;
		}
		Q_strncpy( vk_core.devices[i].deviceName, props.deviceName, sizeof( vk_core.devices[i].deviceName ));
}
#endif

static void devicePrintMemoryInfo(const VkPhysicalDeviceMemoryProperties *props, const VkPhysicalDeviceMemoryBudgetPropertiesEXT *budget) {
	INFO("Memory heaps: %d", props->memoryHeapCount);
	for (int i = 0; i < (int)props->memoryHeapCount; ++i) {
		const VkMemoryHeap* const heap = props->memoryHeaps + i;
		INFO("  %d: size=%dMb used=%dMb avail=%dMb device_local=%d", i,
			(int)(heap->size / (1024 * 1024)),
			(int)(budget->heapUsage[i] / (1024 * 1024)),
			(int)(budget->heapBudget[i] / (1024 * 1024)),
			!!(heap->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT));
	}

	INFO("Memory types: %d", props->memoryTypeCount);
	for (int i = 0; i < (int)props->memoryTypeCount; ++i) {
		const VkMemoryType* const type = props->memoryTypes + i;
		INFO("  %d: bit=0x%x heap=%d flags=%c%c%c%c%c", i,
			(1 << i),
			type->heapIndex,
			type->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? 'D' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? 'V' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? 'C' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? '$' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? 'L' : '.'
		);
	}
}

static const char* perfCounterUnitName(VkPerformanceCounterUnitKHR unit) {
	switch (unit) {
		case VK_PERFORMANCE_COUNTER_UNIT_GENERIC_KHR: return "generic";
		case VK_PERFORMANCE_COUNTER_UNIT_PERCENTAGE_KHR: return "%";
		case VK_PERFORMANCE_COUNTER_UNIT_NANOSECONDS_KHR: return "ns";
		case VK_PERFORMANCE_COUNTER_UNIT_BYTES_KHR: return "b";
		case VK_PERFORMANCE_COUNTER_UNIT_BYTES_PER_SECOND_KHR: return "bps";
		case VK_PERFORMANCE_COUNTER_UNIT_KELVIN_KHR: return "K";
		case VK_PERFORMANCE_COUNTER_UNIT_WATTS_KHR: return "W";
		case VK_PERFORMANCE_COUNTER_UNIT_VOLTS_KHR: return "V";
		case VK_PERFORMANCE_COUNTER_UNIT_AMPS_KHR: return "A";
		case VK_PERFORMANCE_COUNTER_UNIT_HERTZ_KHR: return "Hz";
		case VK_PERFORMANCE_COUNTER_UNIT_CYCLES_KHR: return "C";
		default: return "?";
	}
}

static const char *perfCounterScopeName(VkPerformanceCounterScopeKHR scope) {
	switch(scope) {
		case VK_PERFORMANCE_COUNTER_SCOPE_COMMAND_BUFFER_KHR: return "cmdbuf";
		case VK_PERFORMANCE_COUNTER_SCOPE_RENDER_PASS_KHR: return "renderpass";
		case VK_PERFORMANCE_COUNTER_SCOPE_COMMAND_KHR: return "command";
		default: return "unknown";
	}
}

static const char *perfCounterStorageName(VkPerformanceCounterStorageKHR storage) {
	switch (storage) {
		case VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR: return "i32";
		case VK_PERFORMANCE_COUNTER_STORAGE_INT64_KHR: return "i64";
		case VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR: return "u32";
		case VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR: return "u64";
		case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR: return "f32";
		case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR: return "f64";
		default: return "unknown";
	}
}

void vDevicePrintPerformanceCounters(const VDeviceInfo *info) {
	INFO("Got %d counters:", info->perf_counters.count);
	for (uint32_t i = 0; i < info->perf_counters.count; ++i) {
		const VkPerformanceCounterKHR *const cnt = info->perf_counters.counters + i;
		const VkPerformanceCounterDescriptionKHR *const desc = info->perf_counters.desc + i;
		INFO("  %d: %s %s/%s, %s@%s (%s)",
			i, perfCounterScopeName(cnt->scope),
			desc->category, desc->name,
			perfCounterUnitName(cnt->unit), perfCounterStorageName(cnt->storage),
			desc->description);
	}
}

static void queryPerformanceCounters(VDeviceInfo *info) {
	uint32_t counters_count = 0;
	XVK_CHECK(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(info->physical_device, info->queue_index, &counters_count, NULL, NULL));

	VkPerformanceCounterKHR *const counters = Mem_Malloc(vk_core.pool, counters_count * sizeof(*counters));
	VkPerformanceCounterDescriptionKHR *const counters_desc = Mem_Malloc(vk_core.pool, counters_count * sizeof(*counters_desc));

	for (uint32_t i = 0; i < counters_count; ++i) {
		counters[i] = (VkPerformanceCounterKHR) { .sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR, };
		counters_desc[i] = (VkPerformanceCounterDescriptionKHR) { .sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR, };
	}

	XVK_CHECK(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(info->physical_device, info->queue_index, &counters_count, counters, counters_desc));

	info->perf_counters.count = counters_count;
	info->perf_counters.counters = counters;
	info->perf_counters.desc = counters_desc;

	vDevicePrintPerformanceCounters(info);
}

static void readPhysicalDeviceInfo(VDeviceInfo *info) {
#define FUNC(COUNT, ITEMS) XVK_CHECK(vkEnumerateDeviceExtensionProperties(info->physical_device, NULL, COUNT, ITEMS))
		GET_VULKAN_ARRAY(VkExtensionProperties, extensions, FUNC);
#undef FUNC
	{
		INFO( "\t\tSupported device extensions: %u", extensions.count);
		devicePrintExtensionsFromList(extensions.items, extensions.count, device_extensions_req, COUNTOF(device_extensions_req));
		devicePrintExtensionsFromList(extensions.items, extensions.count, device_extensions_rt, COUNTOF(device_extensions_rt));
		devicePrintExtensionsFromList(extensions.items, extensions.count, device_extensions_nv_checkpoint, COUNTOF(device_extensions_nv_checkpoint));
		devicePrintExtensionsFromList(extensions.items, extensions.count, device_extensions_extra, COUNTOF(device_extensions_extra));
		devicePrintExtensionsFromList(extensions.items, extensions.count, device_extensions_perf_query, COUNTOF(device_extensions_perf_query));
	}

	void *features_head = NULL;
	VkPhysicalDevicePerformanceQueryFeaturesKHR perf_query_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR,
		.pNext = features_head,
	};

	const qboolean perf_query_extension_supported = deviceSupportsExtensions(extensions.items, extensions.count, device_extensions_perf_query, COUNTOF(device_extensions_perf_query));
	if (perf_query_extension_supported)
		features_head = &perf_query_features;

	info->features = (VkPhysicalDeviceFeatures2) {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = features_head,
	};
	vkGetPhysicalDeviceFeatures2(info->physical_device, &info->features);

	{
		info->anisotropy = info->features.features.samplerAnisotropy;
		INFO("\t\tAnistoropy supported: %d", info->anisotropy);

		info->ray_tracing = deviceSupportsExtensions(extensions.items, extensions.count, device_extensions_rt, COUNTOF(device_extensions_rt));
		INFO("\t\tRay tracing supported: %d", info->ray_tracing);

		info->nv_checkpoint = vk_core.debug && deviceSupportsExtensions(extensions.items, extensions.count, device_extensions_nv_checkpoint, COUNTOF(device_extensions_nv_checkpoint));
		INFO("\t\tNV checkpoints supported: %d", info->nv_checkpoint);

		info->calibrated_timestamps = deviceSupportsExtensions(extensions.items, extensions.count, device_extensions_extra, COUNTOF(device_extensions_extra));
		INFO("\t\tCalibrated timestamps supported: %d", info->calibrated_timestamps);

		info->perf_query = perf_query_extension_supported && perf_query_features.performanceCounterQueryPools;
		INFO("\t\tPerformance query supported: %d", info->perf_query);
	}

	if (perf_query_extension_supported)
		queryPerformanceCounters(info);

	// Get memory properties and budget
	{
		info->memory_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
		info->memory_properties2.pNext = &info->memory_budget;
		info->memory_budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		info->memory_budget.pNext = NULL;
		vkGetPhysicalDeviceMemoryProperties2(info->physical_device, &info->memory_properties2);
		devicePrintMemoryInfo(&info->memory_properties2.memoryProperties, &info->memory_budget);
	}


	{
		// TODO should we check Vk version first?
		info->properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		info->properties2.pNext = NULL;
		if (info->ray_tracing) {
			info->properties2.pNext = &info->properties_accel;
			info->properties_accel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
			info->properties_accel.pNext = &info->properties_ray_tracing_pipeline;
			info->properties_ray_tracing_pipeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
			info->properties_ray_tracing_pipeline.pNext = NULL;
		}
		vkGetPhysicalDeviceProperties2(info->physical_device, &info->properties2);
		if (info->ray_tracing) {
			//??? g_rtx.sbt_record_size = ALIGN_UP(info->properties_ray_tracing_pipeline.shaderGroupHandleSize, info->properties_ray_tracing_pipeline.shaderGroupHandleAlignment);
			info->sbt_record_size = ALIGN_UP(info->properties_ray_tracing_pipeline.shaderGroupHandleSize, info->properties_ray_tracing_pipeline.shaderGroupBaseAlignment);
		}
	}

	Mem_Free(extensions.items);
}

typedef ARRAY_DYNAMIC_DECLARE(VDeviceInfo, VDeviceInfos);

static VDeviceInfos enumerateDevices(void) {
	VDeviceInfos infos;
	arrayDynamicInitT(&infos);

#define FUNC(COUNT, ITEMS) XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, COUNT, ITEMS))
	GET_VULKAN_ARRAY(VkPhysicalDevice, physical_devices, FUNC);
#undef FUNC
	if (physical_devices.count == 0) {
		ERR("No physical Vulkan devices found");
		return infos;
	}

	arrayDynamicResizeT(&infos, physical_devices.count);

	INFO("Got %u physical devices:", physical_devices.count);
	int devices_having_rt = 0;
	for (uint32_t i = 0; i < physical_devices.count; ++i) {
		VDeviceInfo *const info = infos.items + i;
		*info = (VDeviceInfo) {
			.physical_device = physical_devices.items[i],
		};

		vkGetPhysicalDeviceProperties(info->physical_device, &info->properties);

		INFO("\t%u: %04x:%04x %d %s %u.%u.%u %u.%u.%u",
			i, info->properties.vendorID, info->properties.deviceID, info->properties.deviceType, info->properties.deviceName,
			XVK_PARSE_VERSION(info->properties.driverVersion), XVK_PARSE_VERSION(info->properties.apiVersion));

		info->queue_index = findUsableQueueFamilyIndex(info->physical_device);
		if (info->queue_index == VK_QUEUE_FAMILY_IGNORED) {
			WARN("\t\tSkipping this device as compatible queue (which has both compute and graphics and also can present) not found" );
			continue;
		}

		readPhysicalDeviceInfo(info);
		devices_having_rt += !!info->ray_tracing;

		// FIXME also pay attention to various device limits. We depend on them implicitly now.
	}

	Mem_Free(physical_devices.items);

	if (devices_having_rt == 0) {
		gEngine.Con_Printf( "^6===================================================^7\n" );
		gEngine.Con_Printf(S_ERROR "^1No ray tracing extensions found.^7\n");
		#if defined XASH_64BIT
		gEngine.Con_Printf(S_NOTE "^3Check that you have compatible hardware and drivers.^7\n");
		#else
		gEngine.Con_Printf(S_WARN "^3You're running in ^132-bit ^3mode!^7\n");
		gEngine.Con_Printf(S_NOTE "^3Ray Tracing REQUIRES ^264-bit ^3process!\n^5Please rebuild and start the 64-bit xash3d binary.^7\n");
		#endif
		gEngine.Con_Printf( "^6===================================================^7\n" );
	}

	return infos;
}

static void loadDeviceFunctions(dllfunc_t *funcs, int count) {
	for (int i = 0; i < count; ++i) {
		*funcs[i].func = vkGetDeviceProcAddr(v_device, funcs[i].name);
		if (!*funcs[i].func) {
			WARN("Function %s was not loaded", funcs[i].name);
		}
	}
}

static qboolean createDevice(const VDeviceInfo* info) {
	void *head = NULL;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_feature = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
		.pNext = head,
		.accelerationStructure = VK_TRUE,
	};
	head = &accel_feature;
	VkPhysicalDevice16BitStorageFeatures sixteen_bit_feature = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
		.pNext = head,
		// TODO verify support
		.storageBuffer16BitAccess = VK_TRUE,
	};
	head = &sixteen_bit_feature;
	VkPhysicalDeviceVulkan12Features vk12_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = head,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // Needed for texture sampling in closest hit shader
		.storageBuffer8BitAccess = VK_TRUE,
		.uniformAndStorageBuffer8BitAccess = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE,

		// VK_KHR_performance_query requires host-side query reset, cause it doesn't like query reset cmd in the same cmdbuf
		.hostQueryReset = info->perf_query ? VK_TRUE : VK_FALSE,
	};
	head = &vk12_features;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_feature = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
		.pNext = head,
		.rayTracingPipeline = VK_TRUE,
		// TODO .rayTraversalPrimitiveCulling = VK_TRUE,
	};
	head = &ray_tracing_pipeline_feature;
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_pipeline_feature = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
		.pNext = head,
		.rayQuery = VK_TRUE,
	};
	head = info->ray_tracing ? &ray_query_pipeline_feature : NULL;

	VkPhysicalDevicePerformanceQueryFeaturesKHR perf_query_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR,
		.pNext = head,
		.performanceCounterQueryPools = VK_TRUE,
	};
	if (info->perf_query)
		head = &perf_query_features;

	VkPhysicalDeviceVulkan13Features vk13_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = head,
		.synchronization2 = VK_TRUE,
	};
	head = &vk13_features;

	VkPhysicalDeviceFeatures2 features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = head,
		.features.samplerAnisotropy = info->features.features.samplerAnisotropy,
		.features.shaderInt16 = true, // TODO verfiy support first
	};
	head = &features;

	VkDeviceDiagnosticsConfigCreateInfoNV diag_config_nv = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV,
		.pNext = head,
		.flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV | 0x00000008 /*VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV */
	};

	if (info->nv_checkpoint)
		head = &diag_config_nv;

	const float queue_priorities[1] = {1.f};
	VkDeviceQueueCreateInfo queue_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.flags = 0,
		.queueFamilyIndex = info->queue_index,
		.queueCount = COUNTOF(queue_priorities),
		.pQueuePriorities = queue_priorities,
	};

	const char* device_extensions[MAX_DEVICE_EXTENSIONS];
	int device_extensions_count = 0;
	device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_req, COUNTOF(device_extensions_req));
	if (info->ray_tracing)
		device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_rt, COUNTOF(device_extensions_rt));
	if (info->nv_checkpoint)
		device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_nv_checkpoint, COUNTOF(device_extensions_nv_checkpoint));
	if (info->calibrated_timestamps)
		device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_extra, COUNTOF(device_extensions_extra));
	if (info->perf_query)
		device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_perf_query, COUNTOF(device_extensions_perf_query));

	VkDeviceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = head,
		.flags = 0,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_info,
		.enabledExtensionCount = device_extensions_count,
		.ppEnabledExtensionNames = device_extensions,
	};

	{
		const VkResult result = vkCreateDevice(info->physical_device, &create_info, NULL, &v_device);
		if (result != VK_SUCCESS) {
			gEngine.Con_Printf( S_ERROR "%s:%d vkCreateDevice failed (%d): %s\n",
				__FILE__, __LINE__, result, R_VkResultName(result));
			return 0;
		}
	}

	loadDeviceFunctions(device_funcs, COUNTOF(device_funcs));

	if (info->ray_tracing)
		loadDeviceFunctions(device_funcs_rtx, COUNTOF(device_funcs_rtx));

	// TODO do not access vk_core directly
	vk_core.device = v_device;
	vk_core.rtx = info->ray_tracing;
	vk_core.nv_checkpoint = info->nv_checkpoint;
	vkGetDeviceQueue(v_device, 0, 0, &vk_core.queue);

	v_device_info = *info;

	if (info->perf_query) {
		const VkAcquireProfilingLockInfoKHR apli = {
			.sType = VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR,
			.timeout = UINT64_MAX,
		};
		const VkResult result = vkAcquireProfilingLockKHR(v_device, &apli);
		if (result != VK_SUCCESS) {
			ERR("Failed to acquire profiling lock: %#x: %s. Disabling performance query.\n",
				result, R_VkResultName(result));
			v_device_info.perf_query = false;
		}
	}

	return 1;
}

int vDeviceInit(VDeviceInitArgs args) {
	ASSERT(v_device == VK_NULL_HANDLE);
	VDeviceInfos physical_devices = enumerateDevices();

#if 0 // TODO
	char unique_deviceID[16];
	const qboolean is_target_device = vk_device_target_id && Q_stricmp(vk_device_target_id->string, "") && num_available_devices > 0;
	qboolean is_target_device_found = false;
#endif

	for (uint32_t i = 0; i < physical_devices.count; ++i) {
		VDeviceInfo* const devinfo = physical_devices.items + i;

		// Skip non-target device
#if 0 // TODO
		Q_snprintf( unique_deviceID, sizeof( unique_deviceID ), "%04x:%04x", candidate_device->props.vendorID, candidate_device->props.deviceID );
		if (is_target_device && !is_target_device_found && Q_stricmp(vk_device_target_id->string, unique_deviceID)) {
			if (i == num_available_devices-1) {
				gEngine.Con_Printf("Not found device %s, start on %s. Please set a valid device.\n", vk_device_target_id->string, unique_deviceID);
			} else {
				gEngine.Con_Printf("Skip device %s, because selected %s\n", unique_deviceID, vk_device_target_id->string);
				continue;
			}
		} else {
			is_target_device_found = true;
		}
#endif

		if (args.force_disable_rt && devinfo->ray_tracing) {
			WARN("Device[%d] supports ray tracing, but rt_force_disable is set, force-disabling ray tracing support.", i);
			devinfo->ray_tracing = 0;
		}

		if (devinfo->perf_query && !args.enable_perf_query) {
			INFO("Device[%d] supports performance query, but -vkperfquery wasn't supplied. Peformance query support will not be enabled.", i);
			devinfo->perf_query = 0;
		}

		INFO("Trying device #%d: %04x:%04x %d %s %u.%u.%u %u.%u.%u",
			i, devinfo->properties.vendorID, devinfo->properties.deviceID, devinfo->properties.deviceType, devinfo->properties.deviceName,
			XVK_PARSE_VERSION(devinfo->properties.driverVersion), XVK_PARSE_VERSION(devinfo->properties.apiVersion));

		if (createDevice(devinfo) == 1)
			break;
	}

	Mem_Free(physical_devices.items);

	if (v_device == VK_NULL_HANDLE) {
		ERR("No compatibe Vulkan devices found. Vulkan render will not be available" );
		return false;
	}

	return true;
}

void vDeviceShutdown(void) {
	if (v_device_info.perf_query) {
		vkReleaseProfilingLockKHR(v_device);
	}

	vkDestroyDevice(v_device, NULL);
	v_device = VK_NULL_HANDLE;

	if (v_device_info.perf_counters.count) {
		Mem_Free((void*)v_device_info.perf_counters.counters);
		Mem_Free((void*)v_device_info.perf_counters.desc);
	}
}
