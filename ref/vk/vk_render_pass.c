#include "vk_render_pass.h"

#include "vk_core.h"
#include "std/arrays.h"

vk_render_pass_t vk_render_pass;

static VkRenderPass createRenderPass(VkFormat format, VkFormat depth_format, qboolean ray_tracing) {
	VkRenderPass render_pass;

	const VkAttachmentDescription attachments[] = {{
		.format = format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = ray_tracing ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR /* TODO: prod renderer should not care VK_ATTACHMENT_LOAD_OP_DONT_CARE */,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = ray_tracing ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	}, {
		// Depth
		.format = depth_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	}};

	const VkAttachmentReference color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	const VkAttachmentReference depth_attachment = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	const VkSubpassDescription subdesc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		.pDepthStencilAttachment = &depth_attachment,
	};

	BOUNDED_ARRAY(VkSubpassDependency, dependencies, 2);
	if (vk_core.rtx) {
		const VkSubpassDependency color = {
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		};
		BOUNDED_ARRAY_APPEND_ITEM(dependencies, color);
	} else {
		const VkSubpassDependency color = {
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		};
		BOUNDED_ARRAY_APPEND_ITEM(dependencies, color);
	}

	const VkSubpassDependency depth = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		.dependencyFlags = 0,
	};
	BOUNDED_ARRAY_APPEND_ITEM(dependencies, depth);

	const VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = COUNTOF(attachments),
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subdesc,
		.dependencyCount = dependencies.count,
		.pDependencies = dependencies.items,
	};

	XVK_CHECK(vkCreateRenderPass(vk_core.device, &rpci, NULL, &render_pass));
	return render_pass;
}

qboolean R_VkRenderPassInit(VkFormat format, VkFormat depth_format) {
	vk_render_pass.raster = createRenderPass(format, depth_format, false);
	if (vk_core.rtx)
		vk_render_pass.after_ray_tracing = createRenderPass(format, depth_format, true);

	return true;
}

void R_VkRenderPassShutdown(void) {
	vkDestroyRenderPass(vk_core.device, vk_render_pass.raster, NULL);
	if (vk_core.rtx)
		vkDestroyRenderPass(vk_core.device, vk_render_pass.after_ray_tracing, NULL);
}
