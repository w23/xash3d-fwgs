#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

typedef struct {
	uint32_t generation;
} r_vkstaging_handle_t;

typedef struct {
	void *ptr;
	r_vkstaging_handle_t handle;

	// TODO maybe return these on lock?
	VkBuffer buffer;
	VkDeviceSize offset;
} r_vkstaging_region_t;

// Allocate CPU-accessible memory in staging buffer
r_vkstaging_region_t R_VkStagingLock(uint32_t size);

// Mark allocated region as ready for upload
void R_VkStagingUnlock(r_vkstaging_handle_t handle);

// Notify staging that this amount of regions were scheduled to be copied
void R_VkStagingCopied(uint32_t count);

// Finalize current generation, return its tag for R_VkStagingGenerationRelease() call
uint32_t R_VkStagingGenerationCommit(void);

// Free all data for generation tag (returned by commit)
void R_VkStagingGenerationRelease(uint32_t gen);
