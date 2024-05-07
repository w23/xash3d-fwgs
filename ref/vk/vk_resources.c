#include "vk_resources.h"
#include "vk_core.h"
#include "vk_image.h"
#include "vk_common.h"
#include "vk_logs.h"

#define LOG_MODULE rt

// TODO remove
#include "vk_textures.h"
#include "vk_ray_internal.h" // UniformBuffer

#include <stdlib.h>

#define MAX_RESOURCES 32

static struct {
	rt_resource_t res[MAX_RESOURCES];
} g_res;

void R_VkResourcesInit(void) {
#define REGISTER_EXTERNAL(type, name_) \
	Q_strncpy(g_res.res[ExternalResource_##name_].name, #name_, sizeof(g_res.res[0].name)); \
	g_res.res[ExternalResource_##name_].refcount = 1;
	EXTERNAL_RESOUCES(REGISTER_EXTERNAL)
#undef REGISTER_EXTERNAL

	g_res.res[ExternalResource_textures].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.value = (vk_descriptor_value_t){
			.image_array = R_VkTexturesGetAllDescriptorsArray(),
		}
	};
	g_res.res[ExternalResource_textures].refcount = 1;
}

rt_resource_t *R_VkResourceGetByIndex(int index) {
	ASSERT(index >= 0);
	ASSERT(index < MAX_RESOURCES);
	return g_res.res + index;
}

int R_VkResourceFindIndexByName(const char *name) {
	// TODO hash table
	// Find the exact match if exists
	// There might be gaps, so we need to check everything
	for (int i = 0; i < MAX_RESOURCES; ++i) {
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
	for (int i = ExternalResource_COUNT; i < MAX_RESOURCES; ++i) {
		if (!g_res.res[i].name[0])
			return g_res.res + i;
	}

	return NULL;
}

void R_VkResourcesCleanup(void) {
	for (int i = 0; i < MAX_RESOURCES; ++i) {
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

	g_res.res[ExternalResource_blue_noise_texture].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.value = (vk_descriptor_value_t){
			.image = R_VkTexturesGetBlueNoiseImageInfo(),
		},
	};

#define RES_SET_BUFFER(name, type_, source_, offset_, size_) \
	g_res.res[ExternalResource_##name].resource = (vk_resource_t){ \
		.type = type_, \
		.value = (vk_descriptor_value_t) { \
			.buffer = (VkDescriptorBufferInfo) { \
				.buffer = (source_), \
				.offset = (offset_), \
				.range = (size_), \
			} \
		} \
	}

	RES_SET_BUFFER(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, args.uniform_buffer, args.frame_index * args.uniform_unit_size, sizeof(struct UniformBuffer));

#define RES_SET_SBUFFER_FULL(name, source_) \
	RES_SET_BUFFER(name, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, source_.buffer, 0, source_.size)

	// TODO move this to ray model producer
	RES_SET_SBUFFER_FULL(kusochki, g_ray_model_state.kusochki_buffer);
	RES_SET_SBUFFER_FULL(model_headers, g_ray_model_state.model_headers_buffer);

	// TODO move these to vk_geometry
	RES_SET_SBUFFER_FULL(indices, args.geometry_data);
	RES_SET_SBUFFER_FULL(vertices, args.geometry_data);

	// TODO move this to lights
	RES_SET_BUFFER(lights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args.light_bindings->buffer, args.light_bindings->metadata.offset, args.light_bindings->metadata.size);
	RES_SET_BUFFER(light_grid, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args.light_bindings->buffer, args.light_bindings->grid.offset, args.light_bindings->grid.size);
#undef RES_SET_SBUFFER_FULL
#undef RES_SET_BUFFER
}

// FIXME not even sure what this functions is supposed to do in the end
void R_VkResourcesFrameBeginStateChangeFIXME(VkCommandBuffer cmdbuf, qboolean discontinuity) {
	// Transfer previous frames before they had a chance of their resource-barrier metadata overwritten (as there's no guaranteed order for them)
	for (int i = ExternalResource_COUNT; i < MAX_RESOURCES; ++i) {
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
		if (discontinuity || res->resource.write.pipelines == 0) {
			// TODO is there a better way? Can image be cleared w/o explicit clear op?
			DEBUG("discontinuity: %s", res->name);
			R_VkImageClear( cmdbuf, res->image.image );
			res->resource.write.pipelines = VK_PIPELINE_STAGE_TRANSFER_BIT;
			res->resource.write.image_layout = VK_IMAGE_LAYOUT_GENERAL;
			res->resource.write.access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
	}

	// Clear intra-frame resources
	for (int i = ExternalResource_COUNT; i < MAX_RESOURCES; ++i) {
		rt_resource_t* const res = g_res.res + i;
		if (!res->name[0] || !res->image.image || res->source_index_plus_1 > 0)
			continue;

		//res->resource.read = res->resource.write = (ray_resource_state_t){0};
		res->resource.write = (ray_resource_state_t){0};
	}
}

#define MAX_BARRIERS 16

void R_VkResourcesPrepareDescriptorsValues(VkCommandBuffer cmdbuf, vk_resources_write_descriptors_args_t args) {
	VkImageMemoryBarrier image_barriers[MAX_BARRIERS];
	int image_barriers_count = 0;
	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;

	for (int i = 0; i < args.count; ++i) {
		const int index = args.resources_map ? args.resources_map[i] : i;
		vk_resource_t* const res = args.resources[index];

		const vk_descriptor_value_t *const src_value = &res->value;
		vk_descriptor_value_t *const dst_value = args.values + i;

		const qboolean write = i >= args.write_begin;

		if (res->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			ASSERT(image_barriers_count < COUNTOF(image_barriers));

			if (write) {
				// No reads are happening
				//ASSERT(res->read.pipelines == 0);

				src_stage_mask |= res->read.pipelines | res->write.pipelines;

				const ray_resource_state_t new_state = {
					.pipelines = args.pipeline,
					.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
					.image_layout = VK_IMAGE_LAYOUT_GENERAL,
				};

				image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.image = src_value->image_object->image,
					// FIXME MEMORY_WRITE is needed to silence write-after-write layout-transition validation hazard
					.srcAccessMask = res->read.access_mask | res->write.access_mask | VK_ACCESS_MEMORY_WRITE_BIT,
					.dstAccessMask = new_state.access_mask,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = new_state.image_layout,
					.subresourceRange = (VkImageSubresourceRange) {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				};

				// Mark that read would need a transition
				res->read = (ray_resource_state_t){0};
				res->write = new_state;
			} else {
				// Write happened
				ASSERT(res->write.pipelines != 0);

				// No barrier was issued
				if (!(res->read.pipelines & args.pipeline)) {
					src_stage_mask |= res->write.pipelines;

					res->read = (ray_resource_state_t) {
						.pipelines = res->read.pipelines | args.pipeline,
						.access_mask = VK_ACCESS_SHADER_READ_BIT,
						.image_layout = VK_IMAGE_LAYOUT_GENERAL,
					};

					image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.image = src_value->image_object->image,
						.srcAccessMask = res->write.access_mask,
						.dstAccessMask = res->read.access_mask,
						.oldLayout = res->write.image_layout,
						.newLayout = res->read.image_layout,
						.subresourceRange = (VkImageSubresourceRange) {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1,
						},
					};
				}
			}

			dst_value->image = (VkDescriptorImageInfo) {
				.imageLayout = write ? res->write.image_layout : res->read.image_layout,
				.imageView = src_value->image_object->view,
				.sampler = VK_NULL_HANDLE,
			};
		} else {
			*dst_value = *src_value;
		}
	}

	if (image_barriers_count) {
		if (!src_stage_mask)
			src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		vkCmdPipelineBarrier(cmdbuf,
			src_stage_mask,
			args.pipeline,
			0, 0, NULL, 0, NULL, image_barriers_count, image_barriers);
	}
}

