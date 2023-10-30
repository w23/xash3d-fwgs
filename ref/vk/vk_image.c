#include "vk_image.h"
#include "vk_staging.h"
#include "vk_combuf.h"
#include "vk_logs.h"

#include "xash3d_mathlib.h" // Q_max

// Long type lists functions
#include "vk_image_extra.h"

static const VkImageUsageFlags usage_bits_implying_views =
	VK_IMAGE_USAGE_SAMPLED_BIT |
	VK_IMAGE_USAGE_STORAGE_BIT |
	VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR |
	VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT;
/*
	VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
*/

r_vk_image_t R_VkImageCreate(const r_vk_image_create_t *create) {
	const qboolean is_depth = !!(create->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	r_vk_image_t image = {0};
	VkMemoryRequirements memreq;

	const qboolean is_cubemap = !!(create->flags & kVkImageFlagIsCubemap);
	const qboolean is_3d = create->depth > 1;

	ASSERT(create->depth > 0);

	ASSERT(is_cubemap + is_3d != 2);

	const VkFormat unorm_format = unormFormatFor(create->format);
	const qboolean create_unorm =
		!!(create->flags & kVkImageFlagCreateUnormView)
		&& unorm_format != VK_FORMAT_UNDEFINED
		&& unorm_format != create->format;

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
		.extent.width = create->width,
		.extent.height = create->height,
		.extent.depth = create->depth,
		.mipLevels = create->mips,
		.arrayLayers = create->layers,
		.format = create->format,
		.tiling = create->tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = create->usage,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.flags = 0
			| (is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0)
			| (create_unorm ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0),
	};

	XVK_CHECK(vkCreateImage(vk_core.device, &ici, NULL, &image.image));

	image.format = ici.format;

	if (create->debug_name)
		SET_DEBUG_NAME(image.image, VK_OBJECT_TYPE_IMAGE, create->debug_name);

	vkGetImageMemoryRequirements(vk_core.device, image.image, &memreq);
	image.devmem = VK_DevMemAllocate(create->debug_name, memreq, create->memory_props ? create->memory_props : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	XVK_CHECK(vkBindImageMemory(vk_core.device, image.image, image.devmem.device_memory, image.devmem.offset));

	if (create->usage & usage_bits_implying_views) {
		const qboolean ignore_alpha = !!(create->flags & kVkImageFlagIgnoreAlpha) && !is_depth;

		VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : (is_3d ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D),
			.format = ici.format,
			.image = image.image,
			.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.baseMipLevel = 0,
			.subresourceRange.levelCount = ici.mipLevels,
			.subresourceRange.baseArrayLayer = 0,
			.subresourceRange.layerCount = ici.arrayLayers,
			.components = componentMappingForFormat(ici.format, ignore_alpha),
		};
		XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &image.view));

		if (create->debug_name)
			SET_DEBUG_NAME(image.view, VK_OBJECT_TYPE_IMAGE_VIEW, create->debug_name);

		if (create_unorm) {
			ivci.format = unorm_format;
			XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &image.view_unorm));

			if (create->debug_name)
				SET_DEBUG_NAMEF(image.view_unorm, VK_OBJECT_TYPE_IMAGE_VIEW, "%s_unorm", create->debug_name);
		}
	}

	image.width = create->width;
	image.height = create->height;
	image.depth = create->depth;
	image.mips = create->mips;
	image.layers = create->layers;
	image.flags = create->flags;

	return image;
}

void R_VkImageDestroy(r_vk_image_t *img) {
	if (img->view_unorm != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view_unorm, NULL);

	if (img->view != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view, NULL);

	vkDestroyImage(vk_core.device, img->image, NULL);

	VK_DevMemFree(&img->devmem);
	*img = (r_vk_image_t){0};
}

void R_VkImageClear(VkCommandBuffer cmdbuf, VkImage image) {
	const VkImageMemoryBarrier image_barriers[] = { {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = image,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
	}} };

	const VkClearColorValue clear_value = {0};

	vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);

	vkCmdClearColorImage(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &image_barriers->subresourceRange);
}

void R_VkImageBlit(VkCommandBuffer cmdbuf, const r_vkimage_blit_args *blit_args) {
	{
		const VkImageMemoryBarrier image_barriers[] = { {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->src.image,
			.srcAccessMask = blit_args->src.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = blit_args->src.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}, {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = blit_args->dst.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = blit_args->dst.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		} };

		vkCmdPipelineBarrier(cmdbuf,
			blit_args->in_stage,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);
	}

	{
		VkImageBlit region = {0};
		region.srcOffsets[1].x = blit_args->src.width;
		region.srcOffsets[1].y = blit_args->src.height;
		region.srcOffsets[1].z = 1;
		region.dstOffsets[1].x = blit_args->dst.width;
		region.dstOffsets[1].y = blit_args->dst.height;
		region.dstOffsets[1].z = 1;
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		vkCmdBlitImage(cmdbuf,
			blit_args->src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			blit_args->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region,
			VK_FILTER_NEAREST);
	}

	{
		VkImageMemoryBarrier image_barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);
	}
}

void R_VkImageUploadBegin( r_vk_image_t *img ) {
	const VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img->image,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = img->mips,
			.baseArrayLayer = 0,
			.layerCount = img->layers,
		}
	};

	// Command buffer might be invalidated on any slice load
	const VkCommandBuffer cmdbuf = R_VkStagingGetCommandBuffer();
	vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &image_barrier);
}

void R_VkImageUploadSlice( r_vk_image_t *img, int layer, int mip, int size, const void *data ) {
	const uint32_t width = Q_max(1, img->width >> mip);
	const uint32_t height = Q_max(1, img->height >> mip);
	const uint32_t depth = Q_max(1, img->depth >> mip);
	const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(img->format);

	const vk_staging_image_args_t staging_args = {
		.image = img->image,
		.region = (VkBufferImageCopy) {
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = (VkImageSubresourceLayers){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip,
				.baseArrayLayer = layer,
				.layerCount = 1,
			},
			.imageExtent = (VkExtent3D){
				.width = width,
				.height = height,
				.depth = depth,
			},
		},
		.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.size = size,
		.alignment = texel_block_size,
	};

	{
		const vk_staging_region_t staging = R_VkStagingLockForImage(staging_args);
		ASSERT(staging.ptr);
		memcpy(staging.ptr, data, size);
		R_VkStagingUnlock(staging.handle);
	}
}

void R_VkImageUploadEnd( r_vk_image_t *img ) {
	// TODO Don't change layout here. Alternatively:
	// I. Attach layout metadata to the image, and request its change next time it is used.
	// II. Build-in layout transfer to staging commit and do it there on commit.

	const VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img->image,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = img->mips,
			.baseArrayLayer = 0,
			.layerCount = img->layers,
		}
	};

	// Commit is needed to make sure that all previous image loads have been submitted to cmdbuf
	const VkCommandBuffer cmdbuf = R_VkStagingCommit()->cmdbuf;
	vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			// FIXME incorrect, we also use them in compute and potentially ray tracing shaders
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &image_barrier);
}
