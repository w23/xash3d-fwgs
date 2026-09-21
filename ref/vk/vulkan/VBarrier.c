#include "VBarrier.h"

#include "vk_logs.h"
#include "VBuffer.h"
#include "VImage.h"
#include "VCombuf.h"

#define LOG_MODULE combuf

#define ACCESS_WRITE_BITS (0 \
	| VK_ACCESS_2_SHADER_WRITE_BIT \
	| VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT \
	| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT \
	| VK_ACCESS_2_TRANSFER_WRITE_BIT \
	| VK_ACCESS_2_HOST_WRITE_BIT \
	| VK_ACCESS_2_MEMORY_WRITE_BIT \
	| VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT \
	| VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR \
	)

#define ACCESS_READ_BITS (0 \
	| VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT \
	| VK_ACCESS_2_INDEX_READ_BIT \
	| VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT \
	| VK_ACCESS_2_UNIFORM_READ_BIT \
	| VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT \
	| VK_ACCESS_2_SHADER_READ_BIT \
	| VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT \
	| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT \
	| VK_ACCESS_2_TRANSFER_READ_BIT \
	| VK_ACCESS_2_HOST_READ_BIT \
	| VK_ACCESS_2_MEMORY_READ_BIT \
	| VK_ACCESS_2_SHADER_SAMPLED_READ_BIT \
	| VK_ACCESS_2_SHADER_STORAGE_READ_BIT \
	| VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR \
	)

#define ACCESS_KNOWN_BITS (ACCESS_WRITE_BITS | ACCESS_READ_BITS)

#define PRINT_FLAG(mask, flag) \
	if ((flag) & (mask)) DEBUG("%s%s", prefix, #flag)
static void printAccessMask(const char *prefix, VkAccessFlags2 access) {
	PRINT_FLAG(access, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_INDEX_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_UNIFORM_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_TRANSFER_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_TRANSFER_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_HOST_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_HOST_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_MEMORY_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_MEMORY_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	PRINT_FLAG(access, VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT);
#ifdef VK_EXT_device_generated_commands
	PRINT_FLAG(access, VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT);
#endif
	PRINT_FLAG(access, VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_SHADING_RATE_IMAGE_READ_BIT_NV);
	PRINT_FLAG(access, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_NV);
	PRINT_FLAG(access, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_NV);
	PRINT_FLAG(access, VK_ACCESS_2_FRAGMENT_DENSITY_MAP_READ_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI);
	PRINT_FLAG(access, VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR);
	PRINT_FLAG(access, VK_ACCESS_2_MICROMAP_READ_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT);
	PRINT_FLAG(access, VK_ACCESS_2_OPTICAL_FLOW_READ_BIT_NV);
	PRINT_FLAG(access, VK_ACCESS_2_OPTICAL_FLOW_WRITE_BIT_NV);
}

static void printStageMask(const char *prefix, VkPipelineStageFlags2 stages) {
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_HOST_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_COPY_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_RESOLVE_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_BLIT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_CLEAR_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT);
#ifdef VK_EXT_device_generated_commands
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT);
#endif
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_SHADING_RATE_IMAGE_BIT_NV);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_FRAGMENT_DENSITY_PROCESS_BIT_EXT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_CLUSTER_CULLING_SHADER_BIT_HUAWEI);
	PRINT_FLAG(stages, VK_PIPELINE_STAGE_2_OPTICAL_FLOW_BIT_NV);
}

static int makeBufferBarrier(VkBufferMemoryBarrier2* out_bmb, const r_vkcombuf_barrier_buffer_t *const bufbar, VkPipelineStageFlags2 dst_stage) {
	vk_buffer_t *const buf = bufbar->buffer;
	const int is_write = (bufbar->access & ACCESS_WRITE_BITS) != 0;
	const int is_read = (bufbar->access & ACCESS_READ_BITS) != 0;
	ASSERT((bufbar->access & ~(ACCESS_KNOWN_BITS)) == 0);

	*out_bmb = (VkBufferMemoryBarrier2) {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		.pNext = NULL,
		.buffer = buf->buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
		.dstStageMask = dst_stage,
		.dstAccessMask = bufbar->access,
	};

	// TODO: support read-and-write scenarios
	ASSERT(is_read ^ is_write);
	if (is_write) {
		// Write is synchronized with previous reads and writes
		out_bmb->srcStageMask = buf->sync.write.stage | buf->sync.read.stage;
		out_bmb->srcAccessMask = buf->sync.write.access | buf->sync.read.access;

		// Store where write happened
		buf->sync.write.access = bufbar->access;
		buf->sync.write.stage = dst_stage;

		// If there were no previous reads or writes, there no reason to synchronize with anything
		if (out_bmb->srcStageMask == 0)
			return 0;

		// Reset read state
		// TOOD is_read? for read-and-write
		buf->sync.read.access = 0;
		buf->sync.read.stage = 0;
	}

	if (is_read) {
		// Read is synchronized with previous writes only
		out_bmb->srcStageMask = buf->sync.write.stage;
		out_bmb->srcAccessMask = buf->sync.write.access;

		// Check whether this is a new barrier
		if ((buf->sync.read.access & bufbar->access) != bufbar->access
			&& (buf->sync.read.stage & dst_stage) != dst_stage) {
			// Remember this read happened
			buf->sync.read.access |= bufbar->access;
			buf->sync.read.stage |= dst_stage;
		} else {
			// Already synchronized, no need to do anything
			return 0;
		}

		// Also skip issuing a barrier, if there were no previous writes -- nothing to sync with
		// Note that this needs to happen late, as all reads must still be recorded in sync.read fields
		if (buf->sync.write.stage == 0)
			return 0;
	}

	if (LOG_VERBOSE) {
		DEBUG("  srcAccessMask = %llx", (unsigned long long)out_bmb->srcAccessMask);
		printAccessMask("   ", out_bmb->srcAccessMask);
		DEBUG("  dstAccessMask = %llx", (unsigned long long)out_bmb->dstAccessMask);
		printAccessMask("   ", out_bmb->dstAccessMask);
		DEBUG("  srcStageMask = %llx", (unsigned long long)out_bmb->srcStageMask);
		printStageMask("   ", out_bmb->srcStageMask);
		DEBUG("  dstStageMask = %llx", (unsigned long long)out_bmb->dstStageMask);
		printStageMask("   ", out_bmb->dstStageMask);
	}

	return 1;
}

static int makeImageBarrier(VkImageMemoryBarrier2* out_imb, const r_vkcombuf_barrier_image_t *const imgbar, VkPipelineStageFlags2 dst_stage) {
	r_vk_image_t *const img = imgbar->image;
	const int is_write = (imgbar->access & ACCESS_WRITE_BITS) != 0;
	const int is_read = (imgbar->access & ACCESS_READ_BITS) != 0;
	const VkImageLayout old_layout = (!is_read) ? VK_IMAGE_LAYOUT_UNDEFINED : img->sync.layout;
	const int is_layout_transfer = imgbar->layout != old_layout;
	ASSERT((imgbar->access & ~(ACCESS_KNOWN_BITS)) == 0);

	*out_imb = (VkImageMemoryBarrier2) {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.pNext = NULL,
		.srcStageMask = img->sync.write.stage,
		.srcAccessMask = img->sync.write.access,
		.dstStageMask = dst_stage,
		.dstAccessMask = imgbar->access,
		.oldLayout = old_layout,
		.newLayout = imgbar->layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img->image,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	// TODO: support read-and-write scenarios
	//ASSERT(is_read ^ is_write);

	if (is_write || is_layout_transfer) {
		out_imb->srcStageMask |= img->sync.read.stage;
		out_imb->srcAccessMask |= img->sync.read.access;

		img->sync.write.access = imgbar->access;
		img->sync.write.stage = dst_stage;

		img->sync.read.access = 0;
		img->sync.read.stage = 0;
	}

	if (is_read) {
		const int same_access = (img->sync.read.access & imgbar->access) == imgbar->access;
		const int same_stage = (img->sync.read.stage & dst_stage) == dst_stage;

		if (same_access && same_stage && !is_layout_transfer)
			return 0;

		img->sync.read.access |= imgbar->access;
		img->sync.read.stage |= dst_stage;

		// Layout transfer makes write state no longer usable (supposedly)
		if (is_layout_transfer) {
			img->sync.write.access = 0;
			img->sync.write.stage = 0;
		}
	}

	if (!is_layout_transfer && out_imb->srcAccessMask == 0 && out_imb->srcStageMask == 0) {
		return 0;
	}

	if (LOG_VERBOSE) {
		DEBUG("  srcAccessMask = %llx", (unsigned long long)out_imb->srcAccessMask);
		printAccessMask("   ", out_imb->srcAccessMask);
		DEBUG("  dstAccessMask = %llx", (unsigned long long)out_imb->dstAccessMask);
		printAccessMask("   ", out_imb->dstAccessMask);
		DEBUG("  srcStageMask = %llx", (unsigned long long)out_imb->srcStageMask);
		printStageMask("   ", out_imb->srcStageMask);
		DEBUG("  dstStageMask = %llx", (unsigned long long)out_imb->dstStageMask);
		printStageMask("   ", out_imb->dstStageMask);
		DEBUG("  oldLayout = %s (%llx)", R_VkImageLayoutName(out_imb->oldLayout), (unsigned long long)out_imb->oldLayout);
		DEBUG("  newLayout = %s (%llx)", R_VkImageLayoutName(out_imb->newLayout), (unsigned long long)out_imb->newLayout);
	}

	// Store new layout
	img->sync.layout = imgbar->layout;

	return 1;
}

void barrierAddImage(Barrier *bar, r_vkcombuf_barrier_image_t image) {
	if (LOG_VERBOSE) {
		DEBUG(" barrier img=0x%llx (%s) barrier:", (unsigned long long)image.image->image, image.image->name);
	}

	VkImageMemoryBarrier2 imb;
	if (makeImageBarrier(&imb, &image, bar->stage))
		BOUNDED_ARRAY_APPEND_ITEM(bar->images, imb);
}

void barrierAddBuffer(Barrier *bar, r_vkcombuf_barrier_buffer_t buffer) {
	if (LOG_VERBOSE) {
		DEBUG(" barrier buf=0x%llx (%s) barrier:",
			(unsigned long long)buffer.buffer->buffer,
			buffer.buffer->name);
	}

	VkBufferMemoryBarrier2 bmb;
	if (makeBufferBarrier(&bmb, &buffer, bar->stage)) {
		BOUNDED_ARRAY_APPEND_ITEM(bar->buffers, bmb);
	}
}

void barrierCommit(Barrier *bar, struct vk_combuf_s * combuf) {
	ASSERT(bar->stage != 0);

	if (bar->buffers.count == 0 && bar->images.count == 0)
		return;

	vkCmdPipelineBarrier2(combuf->cmdbuf, &(VkDependencyInfo) {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.pNext = NULL,
		.dependencyFlags = 0,
		.bufferMemoryBarrierCount = bar->buffers.count,
		.pBufferMemoryBarriers = bar->buffers.items,
		.imageMemoryBarrierCount = bar->images.count,
		.pImageMemoryBarriers = bar->images.items,
	});

	bar->stage = 0;
}
