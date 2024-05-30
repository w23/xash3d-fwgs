#include "vk_image.h"
#include "vk_staging.h"
#include "vk_combuf.h"
#include "vk_logs.h"
#include "arrays.h"

#include "xash3d_mathlib.h" // Q_max

// Long type lists functions
#include "vk_image_extra.h"

#define LOG_MODULE img

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

	vk_devmem_allocate_args_t args = (vk_devmem_allocate_args_t) {
		.requirements = memreq,
		.property_flags = (create->memory_props) ? create->memory_props : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.allocate_flags = 0,
	};
	image.devmem = VK_DevMemAllocateImage( create->debug_name, args );

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
	image.image_size = memreq.size;
	image.upload_slot = -1;

	return image;
}

void R_VkImageDestroy(r_vk_image_t *img) {
	// Need to make sure that there are no references to this image anywhere.
	// It might have been added to upload queue, but then immediately deleted, leaving references
	// in the queue. See https://github.com/w23/xash3d-fwgs/issues/464
	R_VkImageUploadCancel(img);

	// Image destroy calls are not explicitly synchronized with rendering. GPU might still be
	// processing previous frame. We need to make sure that GPU is done by the time we start
	// messing with any VkImage objects.
	// TODO: textures are usually destroyed in bulk, so we don't really need to wait for each one.
	// TODO: check with framectl for any in-flight frames or any other GPU activity
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	if (img->view_unorm != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view_unorm, NULL);

	if (img->view != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view, NULL);

	if (img->image != VK_NULL_HANDLE)
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

typedef struct {
	r_vk_image_t *image;

	struct {
		// arena for entire layers * mips image
		r_vkstaging_region_t lock;

		// current write offset into the arena
		int cursor;
	} staging;

	struct {
		int begin, cursor, end;
	} slices;
} image_upload_t;

static struct {
	ARRAY_DYNAMIC_DECLARE(image_upload_t, images);
	ARRAY_DYNAMIC_DECLARE(VkBufferImageCopy, slices);
	ARRAY_DYNAMIC_DECLARE(VkImageMemoryBarrier, barriers);
} g_image_upload;

qboolean R_VkImageInit(void) {
	arrayDynamicInitT(&g_image_upload.images);
	arrayDynamicInitT(&g_image_upload.slices);
	arrayDynamicInitT(&g_image_upload.barriers);

	return true;
}

void R_VkImageShutdown(void) {
	ASSERT(g_image_upload.images.count == 0);
	arrayDynamicDestroyT(&g_image_upload.images);
	arrayDynamicDestroyT(&g_image_upload.slices);
	arrayDynamicDestroyT(&g_image_upload.barriers);
}

void R_VkImageUploadCommit( struct vk_combuf_s *combuf, VkPipelineStageFlagBits dst_stages ) {
	const int images_count = g_image_upload.images.count;
	if (images_count == 0)
		return;

	DEBUG("Uploading %d images", images_count);

	static int gpu_scope_id = -2;
	if (gpu_scope_id == -2)
		gpu_scope_id = R_VkGpuScope_Register("image_upload");
	const int gpu_scope_begin = R_VkCombufScopeBegin(combuf, gpu_scope_id);

	// Pre-allocate temp barriers buffer
	arrayDynamicResizeT(&g_image_upload.barriers, images_count);

	// 1. Phase I: prepare all images to be transferred into
	// 1.a Set up barriers for every valid image
	for (int i = 0; i < images_count; ++i) {
		image_upload_t *const up = g_image_upload.images.items + i;
		if (!up->image)
			continue;

		ASSERT(up->image->upload_slot == i);

		g_image_upload.barriers.items[i] = (VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = up->image->image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = up->image->mips,
				.baseArrayLayer = 0,
				.layerCount = up->image->layers,
			},
		};
	}

	// 1.b Invoke the barriers
	vkCmdPipelineBarrier(combuf->cmdbuf,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL,
		images_count, (VkImageMemoryBarrier*)g_image_upload.barriers.items
	);

	// 2. Phase 2: issue copy commands for each valid image
	for (int i = 0; i < images_count; ++i) {
		image_upload_t *const up = g_image_upload.images.items + i;
		if (!up->image)
			continue;

		ASSERT(up->staging.lock.buffer != VK_NULL_HANDLE);
		ASSERT(up->slices.end == up->slices.cursor);

		vkCmdCopyBufferToImage(combuf->cmdbuf,
			up->staging.lock.buffer,
			up->image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			up->slices.end - up->slices.begin,
			g_image_upload.slices.items + up->slices.begin);
	}

	// 3. Phase 3: change all images layout to shader read only optimal
	// 3.a Set up barriers for layout transition
	for (int i = 0; i < images_count; ++i) {
		image_upload_t *const up = g_image_upload.images.items + i;
		if (!up->image)
			continue;

		g_image_upload.barriers.items[i] = (VkImageMemoryBarrier) {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = up->image->image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = up->image->mips,
				.baseArrayLayer = 0,
				.layerCount = up->image->layers,
			},
		};

		R_VkStagingReleaseAfterNextFrame(up->staging.lock.handle);

		// Mark image as uploaded
		up->image = NULL;
		up->image->upload_slot = -1;

		// TODO it would be nice to track uploading status further:
		// 1. When uploading cmdbuf has been submitted to the GPU
		// 2. When that cmdbuf has been processed.
		// But that would entail quite a bit more state tracking, etc etc. Discomfort.
	}

	// 3.b Submit the barriers
	/* const VkPipelineStageFlagBits dest_stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | ( */
	/* 	vk_core.rtx */
	/* 	? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR */
	/* 	: 0); */
	vkCmdPipelineBarrier(combuf->cmdbuf,
		VK_PIPELINE_STAGE_TRANSFER_BIT, dst_stages,
		0, 0, NULL, 0, NULL,
		images_count, (VkImageMemoryBarrier*)g_image_upload.barriers.items
	);

	R_VkCombufScopeEnd(combuf, gpu_scope_begin, VK_PIPELINE_STAGE_TRANSFER_BIT);

	// Clear out image upload queue
	arrayDynamicResizeT(&g_image_upload.images, 0);
	arrayDynamicResizeT(&g_image_upload.slices, 0);
	arrayDynamicResizeT(&g_image_upload.barriers, 0);
}

void R_VkImageUploadBegin( r_vk_image_t *img ) {
	ASSERT(img->upload_slot == -1);

	/* TODO compute staging slices sizes properly
	const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(img->format);
	for (int layer = 0; layer < img->layers; ++layer) {
		for (int mip = 0; mip < img->mips; ++mip) {
			const int width = Q_max( 1, ( img->width >> mip ));
			const int height = Q_max( 1, ( img->height >> mip ));
			const int depth = Q_max( 1, ( img->depth >> mip ));
			const size_t mip_size = CalcImageSize( pic->type, width, height, depth );
		}
	}
	*/
	const size_t staging_size = img->image_size;

	// This is done speculatively to preserve internal image_upload invariant.
	// Speculation: we might end up with staging implementation that, upon discovering that it ran out of free memory,
	// would notify other modules that they'd need to commit their staging data, and thus we'd return to this module's
	// R_VkImageUploadCommit(), which needs to see valid data. Therefore, don't touch its state until
	// R_VkStagingLock returns.
	const r_vkstaging_region_t staging_lock = R_VkStagingLock(staging_size);

	img->upload_slot = g_image_upload.images.count;
	arrayDynamicAppendT(&g_image_upload.images, NULL);
	image_upload_t *const up = g_image_upload.images.items + img->upload_slot;

	up->image = img;
	up->staging.lock = staging_lock;
	up->staging.cursor = 0;

	const int slices = img->layers * img->mips;
	up->slices.begin = up->slices.cursor = g_image_upload.slices.count;
	up->slices.end = up->slices.begin + slices;

	//arrayDynamicAppendManyT(&g_image_upload.slices, slices, NULL);
	arrayDynamicResizeT(&g_image_upload.slices, g_image_upload.slices.count + slices);
}

void R_VkImageUploadSlice( r_vk_image_t *img, int layer, int mip, int size, const void *data ) {
	const uint32_t width = Q_max(1, img->width >> mip);
	const uint32_t height = Q_max(1, img->height >> mip);
	const uint32_t depth = Q_max(1, img->depth >> mip);
	const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(img->format);

	ASSERT(img->upload_slot >= 0);
	ASSERT(img->upload_slot < g_image_upload.images.count);

	image_upload_t *const up = g_image_upload.images.items + img->upload_slot;
	ASSERT(up->image == img);

	ASSERT(up->slices.cursor < up->slices.end);
	ASSERT(up->staging.cursor < img->image_size);
	ASSERT(img->image_size - up->staging.cursor >= size);

	memcpy(up->staging.lock.ptr + up->staging.cursor, data, size);

	g_image_upload.slices.items[up->slices.cursor] = (VkBufferImageCopy) {
		.bufferOffset = up->staging.lock.offset + up->staging.cursor,
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
	};

	up->staging.cursor += size;
	up->slices.cursor += 1;
}

void R_VkImageUploadEnd( r_vk_image_t *img ) {
	ASSERT(img->upload_slot >= 0);
	ASSERT(img->upload_slot < g_image_upload.images.count);

	image_upload_t *const up = g_image_upload.images.items + img->upload_slot;
	ASSERT(up->image == img);

	ASSERT(up->slices.cursor == up->slices.end);
	ASSERT(up->staging.cursor <= img->image_size);
}

void R_VkImageUploadCancel( r_vk_image_t *img ) {
	// Skip already uploaded (or never uploaded) images
	if (img->upload_slot < 0)
		return;

	image_upload_t *const up = g_image_upload.images.items + img->upload_slot;
	ASSERT(up->image == img);

	// Technically we won't need that staging region anymore at all, but it doesn't matter,
	// it's just easier to mark it to be freed this way.
	R_VkStagingReleaseAfterNextFrame(up->staging.lock.handle);

	// Mark upload slot as unused, and image as not subjet to uploading
	up->image = NULL;
	img->upload_slot = -1;
}
