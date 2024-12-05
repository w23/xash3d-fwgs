#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

typedef int r_vkstaging_handle_t;

typedef struct {
	void *ptr;
	r_vkstaging_handle_t handle;

	// TODO maybe return these on lock?
	VkBuffer buffer;
	VkDeviceSize offset;
} r_vkstaging_region_t;

// Allocate CPU-accessible memory in staging buffer
r_vkstaging_region_t R_VkStagingLock(uint32_t size);

// Release when next frame is done
// TODO synch with specific combuf: void R_VkStagingRelease(r_vkstaging_handle_t handle, uint32_t gen);
void R_VkStagingReleaseAfterNextFrame(r_vkstaging_handle_t handle);


typedef struct {
	void *ptr;
	r_vkstaging_handle_t handle;
} vk_staging_region_t;

// Allocate region for uploadting to buffer
typedef struct {
	VkBuffer buffer;
	uint32_t offset;
	uint32_t size;
	uint32_t alignment;
} vk_staging_buffer_args_t;
vk_staging_region_t R_VkStagingLockForBuffer(vk_staging_buffer_args_t args);

// Mark allocated region as ready for upload
void R_VkStagingUnlock(r_vkstaging_handle_t handle);

// Append copy commands to command buffer.
struct vk_combuf_s* R_VkStagingCommit(void);

// Mark previous frame data as uploaded and safe to use.
void R_VkStagingFrameBegin(void);

// Uploads staging contents and returns the command buffer ready to be submitted.
// Can return NULL if there's nothing to upload.
struct vk_combuf_s *R_VkStagingFrameEnd(void);

// Commit all staging data into current cmdbuf, submit it and wait for completion.
// Needed for CPU-GPU sync
void R_VkStagingFlushSync( void );
