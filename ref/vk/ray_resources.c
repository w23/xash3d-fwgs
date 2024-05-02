#include "ray_resources.h"
#include "vk_core.h"
#include "vk_image.h"
#include "vk_common.h"

#include <stdlib.h>

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

