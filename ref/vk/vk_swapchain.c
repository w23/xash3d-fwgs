#include "vk_swapchain.h"
#include "vk_image.h"
#include "profiler.h"

#include "eiface.h" // ARRAYSIZE

extern ref_globals_t *gpGlobals;

static struct {
	// These don't belong here
	VkRenderPass render_pass;
	VkFormat depth_format;

	VkSwapchainKHR swapchain;
	VkFormat image_format;
	uint32_t num_images;
	VkImage *images;
	VkImageView *image_views;
	VkFramebuffer *framebuffers;

	r_vk_image_t depth;

	uint32_t width, height;

	uint32_t recreate_requested;
} g_swapchain = {0};

// TODO move to common
static uint32_t clamp_u32(uint32_t v, uint32_t min, uint32_t max) {
	if (v < min) v = min;
	if (v > max) v = max;
	return v;
}

static void createDepthImage(int w, int h, VkFormat depth_format) {
	const r_vk_image_create_t xic = {
		.debug_name = "depth",
		.format = depth_format,
		.flags = 0,
		.mips = 1,
		.layers = 1,
		.width = w,
		.height = h,
		.depth = 1,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
	};
	g_swapchain.depth = R_VkImageCreate( &xic );
}

static void destroySwapchainAndFramebuffers( VkSwapchainKHR swapchain ) {
	XVK_CHECK(vkDeviceWaitIdle( vk_core.device ));

	for (uint32_t i = 0; i < g_swapchain.num_images; ++i) {
		vkDestroyImageView(vk_core.device, g_swapchain.image_views[i], NULL);
		vkDestroyFramebuffer(vk_core.device, g_swapchain.framebuffers[i], NULL);
	}

	R_VkImageDestroy( &g_swapchain.depth );

	vkDestroySwapchainKHR(vk_core.device, swapchain, NULL);
}

static qboolean recreateSwapchainIfNeeded( qboolean force ) {
	const uint32_t prev_num_images = g_swapchain.num_images;
	uint32_t new_width, new_height;

	VkSurfaceCapabilitiesKHR surface_caps;
	XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface.surface, &surface_caps));

	new_width = surface_caps.currentExtent.width;
	new_height = surface_caps.currentExtent.height;

	if (new_width == 0xfffffffful || new_width == 0)
		new_width = gpGlobals->width;

	if (new_height == 0xfffffffful || new_height == 0)
		new_height = gpGlobals->height;

	new_width = clamp_u32(new_width, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
	new_height = clamp_u32(new_height, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);

	if (new_height == 0 || new_width == 0) {
		return false;
	}

	if (new_width == g_swapchain.width && new_height == g_swapchain.height && !force)
		return true;

	g_swapchain.width = new_width;
	g_swapchain.height = new_height;

	{
		VkSwapchainCreateInfoKHR create_info = {
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.pNext = NULL,
			.surface = vk_core.surface.surface,
			.imageFormat = vk_core.output_surface.format,
			.imageColorSpace = vk_core.output_surface.colorSpace,
			.imageExtent.width = g_swapchain.width,
			.imageExtent.height = g_swapchain.height,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | (vk_core.rtx ? VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.preTransform = surface_caps.currentTransform,
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = VK_PRESENT_MODE_FIFO_KHR, // TODO caps, MAILBOX is better
			//.presentMode = VK_PRESENT_MODE_MAILBOX_KHR, // TODO caps, MAILBOX is better
			//.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR, // TODO caps, MAILBOX is better
			.clipped = VK_TRUE,
			.oldSwapchain = g_swapchain.swapchain,
			.minImageCount = surface_caps.minImageCount + 1,
		};

		if (surface_caps.maxImageCount && surface_caps.maxImageCount < create_info.minImageCount)
			create_info.minImageCount = surface_caps.maxImageCount;

		gEngine.Con_Printf("Creating swapchain %dx%d min_count=%d (extent %dx%d)\n",
			g_swapchain.width, g_swapchain.height, create_info.minImageCount,
			surface_caps.currentExtent.width, surface_caps.currentExtent.height);

		XVK_CHECK(vkCreateSwapchainKHR(vk_core.device, &create_info, NULL, &g_swapchain.swapchain));
		if (create_info.oldSwapchain)
			destroySwapchainAndFramebuffers( create_info.oldSwapchain );
	}

	createDepthImage(g_swapchain.width, g_swapchain.height, g_swapchain.depth_format);

	g_swapchain.num_images = 0;
	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, g_swapchain.swapchain, &g_swapchain.num_images, NULL));
	if (prev_num_images != g_swapchain.num_images)
	{
		if (g_swapchain.images)
		{
			Mem_Free(g_swapchain.images);
			Mem_Free(g_swapchain.image_views);
			Mem_Free(g_swapchain.framebuffers);
		}

		g_swapchain.images = Mem_Malloc(vk_core.pool, sizeof(*g_swapchain.images) * g_swapchain.num_images);
		g_swapchain.image_views = Mem_Malloc(vk_core.pool, sizeof(*g_swapchain.image_views) * g_swapchain.num_images);
		g_swapchain.framebuffers = Mem_Malloc(vk_core.pool, sizeof(*g_swapchain.framebuffers) * g_swapchain.num_images);
	}

	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, g_swapchain.swapchain, &g_swapchain.num_images, g_swapchain.images));

	// TODO move this out to where render pipelines are created
	for (uint32_t i = 0; i < g_swapchain.num_images; ++i) {
		const VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = g_swapchain.image_format,
			.image = g_swapchain.images[i],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.levelCount = 1,
			.subresourceRange.layerCount = 1,
		};

		XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, g_swapchain.image_views + i));

		{
			const VkImageView attachments[] = {
				g_swapchain.image_views[i],
				g_swapchain.depth.view,
			};
			const VkFramebufferCreateInfo fbci = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = g_swapchain.render_pass,
				.attachmentCount = ARRAYSIZE(attachments),
				.pAttachments = attachments,
				.width = g_swapchain.width,
				.height = g_swapchain.height,
				.layers = 1,
			};
			XVK_CHECK(vkCreateFramebuffer(vk_core.device, &fbci, NULL, g_swapchain.framebuffers + i));
		}

		SET_DEBUG_NAMEF(g_swapchain.images[i], VK_OBJECT_TYPE_IMAGE, "swapchain image[%d]", i);
	}

	return true;
}

qboolean R_VkSwapchainInit( VkRenderPass render_pass, VkFormat depth_format ) {
	const uint32_t prev_num_images = g_swapchain.num_images;

	VkSurfaceCapabilitiesKHR surface_caps;
	XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface.surface, &surface_caps));

/*
[2023:10:09|13:03:52] Error: Validation: Validation Error: [ VUID-VkSwapchainCreateInfoKHR-imageFormat-01778 ] Object 0: handle = 0x555556af6a00, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0xc036022f | vkCreateSwapchainKHR(): pCreateInfo->imageFormat VK_FORMAT_B8G8R8A8_SRGB with tiling VK_IMAGE_TILING_OPTIMAL does not support usage that includes VK_IMAGE_USAGE_STORAGE_BIT. The Vulkan spec states: The implied image creation parameters of the swapchain must be supported as reported by vkGetPhysicalDeviceImageFormatProperties (https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-VkSwapchainCreateInfoKHR-imageFormat-01778)
*/
	g_swapchain.image_format = vk_core.output_surface.format;
	g_swapchain.render_pass = render_pass;
	g_swapchain.depth_format = depth_format;

	return recreateSwapchainIfNeeded( true );
}

void R_VkSwapchainShutdown( void ) {
	destroySwapchainAndFramebuffers( g_swapchain.swapchain );
}

r_vk_swapchain_framebuffer_t R_VkSwapchainAcquire(  VkSemaphore sem_image_available ) {
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);

	r_vk_swapchain_framebuffer_t ret = {0};

	for (;;) {
		// Check that swapchain has the same size
		if (!recreateSwapchainIfNeeded(!!g_swapchain.recreate_requested)) {
			goto finalize;
		}

		APROF_SCOPE_DECLARE_BEGIN_EX(vkAcquireNextImageKHR, "vkAcquireNextImageKHR", APROF_SCOPE_FLAG_WAIT);
		const VkResult acquire_result = vkAcquireNextImageKHR(vk_core.device, g_swapchain.swapchain, UINT64_MAX, sem_image_available, VK_NULL_HANDLE, &ret.index);
		APROF_SCOPE_END(vkAcquireNextImageKHR);

		switch (acquire_result) {
			case VK_SUCCESS:
				g_swapchain.recreate_requested = 0;
				break;

			case VK_SUBOPTIMAL_KHR:
				// Would need to wait on the semaphore here somehow
				gEngine.Con_Printf(S_WARN "vkAcquireNextImageKHR returned %s (%0#x), will recreate swapchain for the next frame\n", R_VkResultName(acquire_result), acquire_result);
				++g_swapchain.recreate_requested;
				break;

			case VK_ERROR_OUT_OF_HOST_MEMORY:
			case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			case VK_ERROR_DEVICE_LOST:
				gEngine.Host_Error("vkAcquireNextImageKHR returned %s, this is unrecoverable, crashing.\n", R_VkResultName(acquire_result));
				XVK_CHECK(acquire_result);
				goto finalize;

			case VK_TIMEOUT:
			case VK_NOT_READY:
				gEngine.Con_Printf(S_ERROR "vkAcquireNextImageKHR returned %s (%0#x), frame will be lost\n", R_VkResultName(acquire_result), acquire_result);
				goto finalize;

			case VK_ERROR_OUT_OF_DATE_KHR:
			case VK_ERROR_SURFACE_LOST_KHR:
			case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
			default:
				gEngine.Con_Printf(S_WARN "vkAcquireNextImageKHR returned %s (%0#x)\n", R_VkResultName(acquire_result), acquire_result);

				if (g_swapchain.recreate_requested) {
					gEngine.Con_Printf(S_WARN "second vkAcquireNextImageKHR failed with %s, frame will be lost\n", R_VkResultName(acquire_result));
					goto finalize;
				}

				++g_swapchain.recreate_requested;
				continue;
		}

		break;
	}

	ret.framebuffer = g_swapchain.framebuffers[ret.index];
	ret.width = g_swapchain.width;
	ret.height = g_swapchain.height;
	ret.image = g_swapchain.images[ret.index];
	ret.view = g_swapchain.image_views[ret.index];

finalize:
	APROF_SCOPE_END(function);
	return ret;
}

void R_VkSwapchainPresent( uint32_t index, VkSemaphore done ) {
	const VkPresentInfoKHR presinfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pSwapchains = &g_swapchain.swapchain,
		.pImageIndices = &index,
		.swapchainCount = 1,
		.pWaitSemaphores = &done,
		.waitSemaphoreCount = 1,
	};

	const VkResult present_result = vkQueuePresentKHR(vk_core.queue, &presinfo);
	switch (present_result) {
		case VK_ERROR_OUT_OF_DATE_KHR:
		case VK_ERROR_SURFACE_LOST_KHR:
		case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
			gEngine.Con_Printf(S_WARN "vkQueuePresentKHR returned %s, frame will be lost\n", R_VkResultName(present_result));
			break;

		case VK_SUBOPTIMAL_KHR:
			gEngine.Con_Printf(S_WARN "vkQueuePresentKHR returned %s\n", R_VkResultName(present_result));
			break;

		default:
			XVK_CHECK(present_result);
	}
}

// FIXME: remove this
void switchSwapchain( void ) {
	recreateSwapchainIfNeeded( true );
}