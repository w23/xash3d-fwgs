#pragma once

#include "vk_core.h"

typedef struct VDeviceInfo {
	VkPhysicalDevice physical_device;

	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures2 features;

	VkPhysicalDeviceMemoryProperties2 memory_properties2;
	VkPhysicalDeviceMemoryBudgetPropertiesEXT memory_budget;

	VkPhysicalDeviceProperties2 properties2;
	VkPhysicalDeviceAccelerationStructurePropertiesKHR properties_accel;
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties_ray_tracing_pipeline;

	uint32_t sbt_record_size;
	uint32_t queue_index;

	qboolean anisotropy;
	qboolean ray_tracing;
	qboolean nv_checkpoint;
	qboolean calibrated_timestamps;
	qboolean perf_query;

	struct {
		uint32_t count;
		const VkPerformanceCounterKHR *counters;
		VkPerformanceCounterDescriptionKHR *desc;
	} perf_counters;
} VDeviceInfo;

extern VDeviceInfo v_device_info;
extern VkDevice v_device;

typedef struct {
	int force_disable_rt;
	int enable_perf_query;
} VDeviceInitArgs;

int vDeviceInit(VDeviceInitArgs);
void vDeviceShutdown(void);

void vDevicePrintPerformanceCounters(const VDeviceInfo *info);
