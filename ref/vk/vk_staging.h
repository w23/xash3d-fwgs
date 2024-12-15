#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

struct vk_combuf_s;
typedef void (r_vkstaging_push_f)(void* userptr, struct vk_combuf_s *combuf, uint32_t pending);

typedef struct {
	// Expected to be static, stored as a pointer
	const char *name;

	void *userptr;
	r_vkstaging_push_f *push;
} r_vkstaging_user_create_t;

struct r_vkstaging_user_t;
typedef struct r_vkstaging_user_t *r_vkstaging_user_handle_t;
r_vkstaging_user_handle_t R_VkStagingUserCreate(r_vkstaging_user_create_t);
void R_VkStagingUserDestroy(r_vkstaging_user_handle_t);

typedef struct {
	// CPU-accessible memory
	void *ptr;

	// GPU buffer to copy from
	VkBuffer buffer;
	VkDeviceSize offset;
} r_vkstaging_region_t;

// Allocate CPU-accessible memory in staging buffer
r_vkstaging_region_t R_VkStagingAlloc(r_vkstaging_user_handle_t, uint32_t size);

// Notify staging that this amount of regions are about to be consumed when the next combuf ends
// I.e. they're "free" from the staging standpoint
void R_VkStagingMarkFree(r_vkstaging_user_handle_t, uint32_t count);

// This gets called just before the combuf is ended and submitted.
// Gives the last chance for the users that haven't yet used their data.
// This is a workaround to patch up the impedance mismatch between top-down push model,
// where the engine "pushes down" the data to be rendered, and "bottom-up" pull model,
// where the frame is constructed based on render graph dependency tree. Not all pushed
// resources could be used, and this gives the opportunity to at least ingest the data
// to make sure that it remains complete, in case it might be needed in the future.
// Returns current frame tag to be closed in the R_VkStagingCombufCompleted() function.
uint32_t R_VkStagingFrameEpilogue(struct vk_combuf_s*);

// This function is called when a frame is finished. It allows staging to free all the
// data used in that frame.
// TODO make this dependency more explicit, i.e. combuf should track when it's done
// and what finalization functions it should call when it's done (there are many).
void R_VkStagingFrameCompleted(uint32_t tag);
