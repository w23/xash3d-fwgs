#include "vk_resources.h"
#include "vk_core.h"
#include "vk_image.h"
#include "vk_common.h"
#include "vk_logs.h"
#include "vk_combuf.h"
#include "arrays.h"

#define LOG_MODULE rt

// TODO remove
#include "vk_textures.h"
#include "vk_ray_internal.h" // UniformBuffer

#include <stdlib.h>

#define MAX_VK_RESOURCES 128

static struct {
	rt_resource_t res[MAX_VK_RESOURCES];
} g_res;

void R_VkResourcesInit(void) {
#define REGISTER_EXTERNAL(type, name_) \
	Q_strncpy(g_res.res[ExternalResource_##name_].name, #name_, sizeof(g_res.res[0].name)); \
	g_res.res[ExternalResource_##name_].refcount = 1;
	EXTERNAL_RESOUCES(REGISTER_EXTERNAL)
#undef REGISTER_EXTERNAL
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
	for (int i = ExternalResource_COUNT; i < MAX_VK_RESOURCES; ++i) {
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

void R_VkResourcesSetBuiltinFIXME(r_vk_resources_builtin_fixme_t args) {
	g_res.res[ExternalResource_skybox].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.value = (vk_descriptor_value_t){
			.image = R_VkTexturesGetSkyboxDescriptorImageInfo( kSkyboxPatched ),
		},
	};

#define RES_SET_BUFFER(name, type_, source_, offset_, size_) \
	g_res.res[ExternalResource_##name].resource = (vk_resource_t){ \
		.type = type_, \
		.ref.buffer = (source_), \
		.value = (vk_descriptor_value_t) { \
			.buffer = (VkDescriptorBufferInfo) { \
				.buffer = (source_)->buffer, \
				.offset = (offset_), \
				.range = (size_), \
			} \
		} \
	}

	RES_SET_BUFFER(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, args.uniform_buffer, args.frame_index * args.uniform_unit_size, sizeof(struct UniformBuffer));
#undef RES_SET_BUFFER
}

// FIXME not even sure what this functions is supposed to do in the end
void R_VkResourcesFrameBeginStateChangeFIXME(vk_combuf_t* combuf, qboolean discontinuity) {
	// Transfer previous frames before they had a chance of their resource-barrier metadata overwritten (as there's no guaranteed order for them)
	for (int i = ExternalResource_COUNT; i < MAX_VK_RESOURCES; ++i) {
		rt_resource_t* const res = g_res.res + i;
		if (!res->name[0] || !res->image.image || res->source_index_plus_1 <= 0)
			continue;

		ASSERT(res->source_index_plus_1 <= COUNTOF(g_res.res));
		rt_resource_t *const src = g_res.res + res->source_index_plus_1 - 1;
		ASSERT(res != src);

		// Swap resources
		const vk_resource_t tmp_res = res->resource;
		const r_vk_image_t tmp_img = res->image;

		res->resource = src->resource;
		res->image = src->image;

		// TODO this is slightly incorrect, as they technically can have different resource->type values
		src->resource = tmp_res;
		src->image = tmp_img;

		// If there was no initial state, prepare it. (this should happen only for the first frame)
		if (discontinuity || res->image.sync.write.stage == 0) {
			// TODO is there a better way? Can image be cleared w/o explicit clear op?
			WARN("discontinuity: %s", res->name);
			R_VkImageClear( &res->image, combuf, NULL );
		}
	}

	// Clear intra-frame resources
	for (int i = ExternalResource_COUNT; i < MAX_VK_RESOURCES; ++i) {
		rt_resource_t* const res = g_res.res + i;
		if (!res->name[0] || !res->image.image || res->source_index_plus_1 > 0)
			continue;

		// 2024-12-12 E384 1:56:00 Commented out: Try not clearing this state. Could be beneficial for later barrier-based extra-cmdbuf sync
		//res->resource.deprecate.write = (ray_resource_state_t){0};
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
