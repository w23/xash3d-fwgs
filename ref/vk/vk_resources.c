#include "vk_resources.h"
#include "vk_image.h"
#include "vk_common.h"
#include "vk_combuf.h"
#include "arrays.h"

#define LOG_MODULE rt

#include <stdlib.h>

#define MAX_VK_RESOURCES 128

static struct {
	rt_resource_t res[MAX_VK_RESOURCES];
} g_res;

void R_VkResourcesInit(void) {
}

rt_resource_t *R_VkResourceGetByIndex(int index) {
	ASSERT(index >= 0);
	ASSERT(index < MAX_VK_RESOURCES);
	return g_res.res + index;
}

int R_VkResourceFindIndexByName(const char *name) {
	// TODO hash table
	// Find the exact match if exists
	// There might be gaps, so we need to check everything
	for (int i = 0; i < MAX_VK_RESOURCES; ++i) {
		if (strcmp(g_res.res[i].name, name) == 0)
			return i;
	}

	return -1;
}

rt_resource_t *R_VkResourceFindByName(const char *name) {
	const int index = R_VkResourceFindIndexByName(name);
	return index < 0 ? NULL : g_res.res + index;
}

rt_resource_t *R_VkResourceFindOrAlloc(const char *name) {
	rt_resource_t *const res = R_VkResourceFindByName(name);
	if (res)
		return res;

	// Find first free slot
	for (int i = 0; i < MAX_VK_RESOURCES; ++i) {
		rt_resource_t *const res = g_res.res + i;
		if (res->name[0] != '\0')
			continue;

		Q_strncpy(res->name, name, sizeof(res->name));
		return res;
	}

	return NULL;
}

void R_VkResourcesCleanup(void) {
	for (int i = 0; i < MAX_VK_RESOURCES; ++i) {
		rt_resource_t *const res = g_res.res + i;
		if (!res->name[0] || res->refcount || !res->image.image)
			continue;

		R_VkImageDestroy(&res->image);
		res->name[0] = '\0';
	}
}


static void barrierAddBuffer(r_vk_barrier_t *barrier, vk_buffer_t *buf, VkAccessFlags access) {
	const r_vkcombuf_barrier_buffer_t bb = {
		.buffer = buf,
		.access = access,
	};
	BOUNDED_ARRAY_APPEND_ITEM(barrier->buffers, bb);
}

void R_VkResourceAddToBarrier(vk_resource_t *res, qboolean write, VkPipelineStageFlags2 dst_stage_mask, r_vk_barrier_t *barrier) {
	switch (res->type) {
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			{
				const r_vkcombuf_barrier_image_t image_barrier = {
					.image = res->ref.image,
					// Image must remain in GENERAL layout regardless of r/w.
					// Storage image reads still require GENERAL, not SHADER_READ_ONLY_OPTIMAL
					.layout = VK_IMAGE_LAYOUT_GENERAL,
					.access = write ? VK_ACCESS_2_SHADER_WRITE_BIT : VK_ACCESS_2_SHADER_READ_BIT,
				};
				BOUNDED_ARRAY_APPEND_ITEM(barrier->images, image_barrier);
			}
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			ASSERT(!write);
			barrierAddBuffer(barrier, res->ref.buffer, VK_ACCESS_2_SHADER_READ_BIT);
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			// nothing for now, as all textures are static at this point
			break;
		case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			// nop
			break;
		default:
			ASSERT(!"Unsupported descriptor type");
	}
}

void R_VkBarrierCommit(vk_combuf_t* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask) {
	if (barrier->images.count == 0 && barrier->buffers.count == 0)
		return;

	R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t){
		.stage = dst_stage_mask,
		.buffers.items = barrier->buffers.items,
		.buffers.count = barrier->buffers.count,
		.images.items = barrier->images.items,
		.images.count = barrier->images.count,
	});

	// Mark as used
	barrier->images.count = 0;
	barrier->buffers.count = 0;
}
