#include "vk_framectl.h"

#include "vk_overlay.h"
#include "vk_scene.h"
#include "vk_render.h"
#include "vk_cvar.h"
#include "vk_devmem.h"
#include "vk_swapchain.h"
#include "vk_image.h"
#include "vk_staging.h"
#include "vk_commandpool.h"
#include "vk_combuf.h"
#include "vk_logs.h"

#include "vk_buffer.h"
#include "vk_geometry.h"

#include "arrays.h"
#include "profiler.h"
#include "r_speeds.h"

#include "eiface.h" // ARRAYSIZE

#include <string.h>

#define LOG_MODULE fctl

extern ref_globals_t *gpGlobals;

vk_framectl_t vk_frame = {0};

// Phase tracking is needed for getting screenshots. Basically, getting a screenshot does the same things as R_EndFrame, and they need to be congruent.
typedef enum {
	Phase_Idle,
	Phase_FrameBegan, // Called R_BeginFrame()
	Phase_FrameRendered, // Called VK_RenderFrame()
	Phase_RenderingEnqueued, //
	Phase_Submitted,
} frame_phase_t;

typedef struct {
	vk_combuf_t *combuf;
	VkFence fence_done;
	VkSemaphore sem_framebuffer_ready;
	VkSemaphore sem_done;

	// This extra semaphore is required because we need to synchronize 2 things on GPU:
	// 1. swapchain
	// 2. next frame command buffer
	// Unfortunately waiting on semaphore also means resetting it when it is signaled
	// so we can't reuse the same one for two purposes and need to mnozhit sunchnosti
	VkSemaphore sem_done2;

	uint32_t staging_frame_tag;
} vk_framectl_frame_t;

static struct {
	vk_framectl_frame_t frames[MAX_CONCURRENT_FRAMES];

	struct {
		int index;
		r_vk_swapchain_framebuffer_t framebuffer;
		frame_phase_t phase;
	} current;
} g_frame;

#define PROFILER_SCOPES(X) \
	X(frame, "Frame", APROF_SCOPE_FLAG_DECOR); \
	X(begin_frame, "R_BeginFrame", 0); \
	X(render_frame, "VK_RenderFrame", 0); \
	X(end_frame, "R_EndFrame", 0); \
	X(frame_gpu_wait, "Wait for GPU", APROF_SCOPE_FLAG_WAIT); \
	X(wait_for_frame_fence, "waitForFrameFence", APROF_SCOPE_FLAG_WAIT); \

#define SCOPE_DECLARE(scope, name, flags) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

// TODO move into vk_image
static VkFormat findSupportedImageFormat(const VkFormat *candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (int i = 0; candidates[i] != VK_FORMAT_UNDEFINED; ++i) {
		VkFormatProperties props;
		VkFormatFeatureFlags props_format;
		vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, candidates[i], &props);
		switch (tiling) {
			case VK_IMAGE_TILING_OPTIMAL:
				props_format = props.optimalTilingFeatures; break;
			case VK_IMAGE_TILING_LINEAR:
				props_format = props.linearTilingFeatures; break;
			default:
				return VK_FORMAT_UNDEFINED;
		}
		if ((props_format & features) == features)
			return candidates[i];
	}

	return VK_FORMAT_UNDEFINED;
}

// TODO sort these based on ???
static const VkFormat depth_formats[] = {
	VK_FORMAT_D32_SFLOAT,
	VK_FORMAT_D24_UNORM_S8_UINT,
	VK_FORMAT_X8_D24_UNORM_PACK32,
	VK_FORMAT_D16_UNORM,
	VK_FORMAT_D32_SFLOAT_S8_UINT,
	VK_FORMAT_D16_UNORM_S8_UINT,
	VK_FORMAT_UNDEFINED
};

static VkRenderPass createRenderPass( VkFormat depth_format, qboolean ray_tracing ) {
	VkRenderPass render_pass;

	const VkAttachmentDescription attachments[] = {{
		.format = SWAPCHAIN_FORMAT,
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
		.attachmentCount = ARRAYSIZE(attachments),
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subdesc,
		.dependencyCount = dependencies.count,
		.pDependencies = dependencies.items,
	};

	XVK_CHECK(vkCreateRenderPass(vk_core.device, &rpci, NULL, &render_pass));
	return render_pass;
}

static void waitForFrameFence( void ) {

	// TODO: wait for small amount of time (~1-10ms?), calling gEngine.CL_ExtraUpdate() each time we wake up
	// Why: CL_ExtraUpdate() is needed when the renderer is stuck doing something for a long time
	// It allows the engine to make progress on other things. Think cooperative multitasking.
	// Alt: Make dedicated render thread and wait there. But that'd be a huge and messy project.

	APROF_SCOPE_BEGIN(wait_for_frame_fence);
	const VkFence fence_done[1] = {g_frame.frames[g_frame.current.index].fence_done};
	for(qboolean loop = true; loop; ) {
#define MAX_WAIT (10ull * 1000*1000*1000)
		const VkResult fence_result = vkWaitForFences(vk_core.device, COUNTOF(fence_done), fence_done, VK_TRUE, MAX_WAIT);
#undef MAX_WAIT
		switch (fence_result) {
			case VK_SUCCESS:
				loop = false;
				break;
			case VK_TIMEOUT:
				gEngine.Con_Printf(S_ERROR "Waiting for frame fence to be signaled timed out after 10 seconds. Wat\n");
				break;
			default:
				XVK_CHECK(fence_result);
		}
	}

	XVK_CHECK(vkResetFences(vk_core.device, COUNTOF(fence_done), fence_done));
	APROF_SCOPE_END(wait_for_frame_fence);
}

/*
static void updateGamma( void ) {
	// FIXME when
	{
		cvar_t* vid_gamma = gEngine.pfnGetCvarPointer( "gamma", 0 );
		cvar_t* vid_brightness = gEngine.pfnGetCvarPointer( "brightness", 0 );
		if( gEngine.R_DoResetGamma( ))
		{
			// paranoia cubemaps uses this
			gEngine.BuildGammaTable( 1.8f, 0.0f );

			// paranoia cubemap rendering
			if( gEngine.drawFuncs->GL_BuildLightmaps )
				gEngine.drawFuncs->GL_BuildLightmaps( );
		}
		else if( FBitSet( vid_gamma->flags, FCVAR_CHANGED ) || FBitSet( vid_brightness->flags, FCVAR_CHANGED ))
		{
			gEngine.BuildGammaTable( vid_gamma->value, vid_brightness->value );
			// FIXME rebuild lightmaps
		}
	}
}
*/

void R_BeginFrame( qboolean clearScene ) {
	if (g_frame.current.phase == Phase_FrameBegan) {
		WARN("R_BeginFrame() called without finishing the previous frame");
		return;
	}


	APROF_SCOPE_DECLARE_BEGIN(begin_frame_tail, "R_BeginFrame_tail");
	ASSERT(g_frame.current.phase == Phase_Submitted || g_frame.current.phase == Phase_Idle);
	g_frame.current.index = (g_frame.current.index + 1) % MAX_CONCURRENT_FRAMES;

	vk_framectl_frame_t *const frame = g_frame.frames + g_frame.current.index;

	{
		waitForFrameFence();
		// Current command buffer is done and available
		// Previous might still be in flight
	}

	APROF_SCOPE_END(begin_frame_tail);

	const uint32_t prev_frame_event_index = aprof_scope_frame();

	APROF_SCOPE_BEGIN(frame);
	APROF_SCOPE_BEGIN(begin_frame);

	{
		const vk_combuf_scopes_t gpurofl[] = { R_VkCombufScopesGet(frame->combuf) };
		R_SpeedsDisplayMore(prev_frame_event_index, gpurofl, COUNTOF(gpurofl));
	}

	if (vk_core.rtx && FBitSet( rt_enable->flags, FCVAR_CHANGED )) {
		vk_frame.rtx_enabled = CVAR_TO_BOOL( rt_enable );
	}
	ClearBits( rt_enable->flags, FCVAR_CHANGED );

	//updateGamma();

	ASSERT(!g_frame.current.framebuffer.framebuffer);

	// TODO explicit frame dependency synced on frame-end-event/sema
	R_VkStagingFrameCompleted(frame->staging_frame_tag);

	g_frame.current.framebuffer = R_VkSwapchainAcquire( frame->sem_framebuffer_ready );
	vk_frame.width = g_frame.current.framebuffer.image.width;
	vk_frame.height = g_frame.current.framebuffer.image.height;

	VK_RenderBegin( vk_frame.rtx_enabled );

	R_VkCombufBegin( frame->combuf );

	g_frame.current.phase = Phase_FrameBegan;
	APROF_SCOPE_END(begin_frame);
}

void VK_RenderFrame( const struct ref_viewpass_s *rvp )
{
	ASSERT(g_frame.current.phase == Phase_FrameBegan || g_frame.current.phase == Phase_FrameRendered);
	APROF_SCOPE_BEGIN(render_frame);
	VK_SceneRender( rvp );
	g_frame.current.phase = Phase_FrameRendered;
	APROF_SCOPE_END(render_frame);
}

static void enqueueRendering( vk_combuf_t* combuf, qboolean draw ) {
	APROF_SCOPE_DECLARE_BEGIN(enqueue, __FUNCTION__);
	const uint32_t frame_width = g_frame.current.framebuffer.image.width;
	const uint32_t frame_height = g_frame.current.framebuffer.image.height;

	ASSERT(g_frame.current.phase == Phase_FrameBegan || g_frame.current.phase == Phase_FrameRendered);

	// TODO: should be done by rendering when it requests textures
	R_VkImageUploadCommit(combuf,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | (vk_frame.rtx_enabled ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0));

	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	if (vk_frame.rtx_enabled) {
		VK_RenderEndRTX( combuf, &g_frame.current.framebuffer.image );
	} else {
		// FIXME: how to do this properly before render pass?
		// Needed to avoid VUID-vkCmdCopyBuffer-renderpass
		vk_buffer_t* const geom = R_GeometryBuffer_Get();
		R_VkBufferStagingCommit(geom, combuf);
		R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t){
			.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
			.buffers = {
				.count = 1,
				.items = &(r_vkcombuf_barrier_buffer_t){
					.buffer = geom,
					.access = VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
				},
			},
		});
	}

	if (draw) {
		const r_vkcombuf_barrier_image_t dst_use[] = {{
			.image = &g_frame.current.framebuffer.image,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		}};
		R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t) {
			.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.images = {
				.items = dst_use,
				.count = COUNTOF(dst_use),
			},
		});

		const VkClearValue clear_value[] = {
			// *_UNORM is float
			{.color = {.float32 = {1.f, 0.f, 0.f, 0.f}}},
			{.depthStencil = {1., 0.}} // TODO reverse-z
		};
		const VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_frame.rtx_enabled ? vk_frame.render_pass.after_ray_tracing : vk_frame.render_pass.raster,
			.renderArea.extent.width = frame_width,
			.renderArea.extent.height = frame_height,
			.clearValueCount = ARRAYSIZE(clear_value),
			.pClearValues = clear_value,
			.framebuffer = g_frame.current.framebuffer.framebuffer,
		};
		vkCmdBeginRenderPass(cmdbuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

		{
			const VkViewport viewport[] = {
				{0.f, 0.f, (float)frame_width, (float)frame_height, 0.f, 1.f},
			};
			const VkRect2D scissor[] = {{
				{0, 0},
				{frame_width, frame_height},
			}};

			vkCmdSetViewport(cmdbuf, 0, ARRAYSIZE(viewport), viewport);
			vkCmdSetScissor(cmdbuf, 0, ARRAYSIZE(scissor), scissor);
		}
	}

	if (!vk_frame.rtx_enabled)
		VK_RenderEnd( combuf, draw,
			frame_width, frame_height,
			g_frame.current.index
			);

	R_VkOverlay_DrawAndFlip( cmdbuf, draw );

	if (draw) {
		vkCmdEndRenderPass(cmdbuf);

		// Render pass's finalLayout transitions the image into this one
		g_frame.current.framebuffer.image.sync.read.access = 0;
		g_frame.current.framebuffer.image.sync.write.access = 0;
		g_frame.current.framebuffer.image.sync.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	g_frame.current.phase = Phase_RenderingEnqueued;
	APROF_SCOPE_END(enqueue);
}

// FIXME pass frame, not combuf (possible desync)
static void submit( vk_combuf_t* combuf, qboolean wait, qboolean draw ) {
	APROF_SCOPE_DECLARE_BEGIN(submit, __FUNCTION__);
	ASSERT(g_frame.current.phase == Phase_RenderingEnqueued);

	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	vk_framectl_frame_t *const frame = g_frame.frames + g_frame.current.index;
	vk_framectl_frame_t *const prev_frame = g_frame.frames + (g_frame.current.index + 1) % MAX_CONCURRENT_FRAMES;

	// Push things from staging that weren't explicitly pulled by frame builder
	frame->staging_frame_tag = R_VkStagingFrameEpilogue(combuf);

	R_VkCombufEnd(combuf);


	BOUNDED_ARRAY(VkCommandBuffer, cmdbufs, 2);
	BOUNDED_ARRAY_APPEND_ITEM(cmdbufs, cmdbuf);

	{
		BOUNDED_ARRAY(VkSemaphore, waitophores, 2);
		BOUNDED_ARRAY(VkPipelineStageFlags, wait_stageflags, 2);
		BOUNDED_ARRAY(VkSemaphore, signalphores, 2);

		if (draw) {
			BOUNDED_ARRAY_APPEND_ITEM(waitophores, frame->sem_framebuffer_ready);
			BOUNDED_ARRAY_APPEND_ITEM(wait_stageflags, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

			BOUNDED_ARRAY_APPEND_ITEM(signalphores, frame->sem_done);
		}

		BOUNDED_ARRAY_APPEND_ITEM(waitophores, prev_frame->sem_done2);
		// TODO remove this second semaphore altogether, replace it with properly tracked barriers.
		// Why: would allow more parallelizm between consecutive frames.
		BOUNDED_ARRAY_APPEND_ITEM(wait_stageflags, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
		BOUNDED_ARRAY_APPEND_ITEM(signalphores, frame->sem_done2);

		DEBUG("submit: frame=%d, staging_tag=%u, combuf=%p, wait for semaphores[%d]={%llx, %llx}, signal semaphores[%d]={%llx, %llx}",
			g_frame.current.index,
			frame->staging_frame_tag,
			frame->combuf->cmdbuf,
			waitophores.count,
			(unsigned long long)waitophores.items[0],
			(unsigned long long)waitophores.items[1],
			signalphores.count,
			(unsigned long long)signalphores.items[0],
			(unsigned long long)signalphores.items[1]
		);

		ASSERT(waitophores.count == wait_stageflags.count);

		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = NULL,
			.waitSemaphoreCount = waitophores.count,
			.pWaitSemaphores = waitophores.items,
			.pWaitDstStageMask = wait_stageflags.items,
			.commandBufferCount = cmdbufs.count,
			.pCommandBuffers = cmdbufs.items,
			.signalSemaphoreCount = signalphores.count,
			.pSignalSemaphores = signalphores.items,
		};
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, frame->fence_done));
		g_frame.current.phase = Phase_Submitted;
	}

	if (g_frame.current.framebuffer.framebuffer != VK_NULL_HANDLE)
		R_VkSwapchainPresent(g_frame.current.framebuffer.index, frame->sem_done);

	g_frame.current.framebuffer = (r_vk_swapchain_framebuffer_t){0};

	if (wait) {
		APROF_SCOPE_BEGIN(frame_gpu_wait);
		XVK_CHECK(vkWaitForFences(vk_core.device, 1, &frame->fence_done, VK_TRUE, INT64_MAX));
		APROF_SCOPE_END(frame_gpu_wait);

		/* if (vk_core.debug) { */
		/* 	// FIXME more scopes */
		/* 	XVK_CHECK(vkQueueWaitIdle(vk_core.queue)); */
		/* } */
		g_frame.current.phase = Phase_Idle;
	}

	APROF_SCOPE_END(submit);
}

inline static VkCommandBuffer currentCommandBuffer( void ) {
	return g_frame.frames[g_frame.current.index].combuf->cmdbuf;
}

void R_EndFrame( void )
{
	APROF_SCOPE_BEGIN_EARLY(end_frame);

	if (g_frame.current.phase == Phase_FrameBegan || g_frame.current.phase == Phase_FrameRendered) {
		vk_combuf_t *const combuf = g_frame.frames[g_frame.current.index].combuf;
		const qboolean draw = g_frame.current.framebuffer.framebuffer != VK_NULL_HANDLE;
		enqueueRendering( combuf, draw );
		submit( combuf, false, draw );
		//submit( cmdbuf, true, draw );
	}

	APROF_SCOPE_END(end_frame);
	APROF_SCOPE_END(frame);
}

qboolean VK_FrameCtlInit( void )
{
	PROFILER_SCOPES(APROF_SCOPE_INIT_EX);

	const VkFormat depth_format = findSupportedImageFormat(depth_formats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// FIXME move this out to renderers
	vk_frame.render_pass.raster = createRenderPass(depth_format, false);
	if (vk_core.rtx)
		vk_frame.render_pass.after_ray_tracing = createRenderPass(depth_format, true);

	if (!R_VkSwapchainInit(vk_frame.render_pass.raster, depth_format))
		return false;

	for (int i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
		vk_framectl_frame_t *const frame = g_frame.frames + i;
		frame->combuf = R_VkCombufOpen();

		frame->sem_framebuffer_ready = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_framebuffer_ready, VK_OBJECT_TYPE_SEMAPHORE, "framebuffer_ready[%d]", i);
		frame->sem_done = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_done, VK_OBJECT_TYPE_SEMAPHORE, "done[%d]", i);
		frame->sem_done2 = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_done2, VK_OBJECT_TYPE_SEMAPHORE, "done2[%d]", i);
		frame->fence_done = R_VkFenceCreate(true);
		SET_DEBUG_NAMEF(frame->fence_done, VK_OBJECT_TYPE_FENCE, "done[%d]", i);
	}

	// Signal first frame semaphore as done
	{
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = NULL,
			.commandBufferCount = 0,
			.pCommandBuffers = NULL,
			.waitSemaphoreCount = 0,
			.pWaitSemaphores = NULL,
			.pWaitDstStageMask = NULL,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &g_frame.frames[0].sem_done2,
		};
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
	}

	vk_frame.rtx_enabled = vk_core.rtx;

	return true;
}

void VK_FrameCtlShutdown( void ) {
	for (int i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
		vk_framectl_frame_t *const frame = g_frame.frames + i;
		R_VkCombufClose(frame->combuf);
		R_VkSemaphoreDestroy(frame->sem_framebuffer_ready);
		R_VkSemaphoreDestroy(frame->sem_done);
		R_VkSemaphoreDestroy(frame->sem_done2);
		R_VkFenceDestroy(frame->fence_done);
	}

	R_VkSwapchainShutdown();

	vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.raster, NULL);
	if (vk_core.rtx)
		vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.after_ray_tracing, NULL);
}

static qboolean canBlitFromSwapchainToFormat( VkFormat dest_format ) {
	VkFormatProperties props;

	vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, SWAPCHAIN_FORMAT, &props);
	if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
		gEngine.Con_Reportf(S_WARN "Swapchain source format doesn't support blit\n");
		return false;
	}

	vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, dest_format, &props);
	if (!(props.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
		gEngine.Con_Reportf(S_WARN "Destination format doesn't support blit\n");
		return false;
	}

	return true;
}

static rgbdata_t *R_VkReadPixels( void ) {
	const VkFormat dest_format = VK_FORMAT_R8G8B8A8_UNORM;
	r_vk_image_t temp_image;
	r_vk_image_t *const framebuffer_image = &g_frame.current.framebuffer.image;
	rgbdata_t *r_shot = NULL;
	qboolean blit = canBlitFromSwapchainToFormat( dest_format );

	vk_combuf_t *const combuf = g_frame.frames[g_frame.current.index].combuf;
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	if (framebuffer_image->image == VK_NULL_HANDLE) {
		gEngine.Con_Printf(S_ERROR "no current image, can't take screenshot\n");
		return NULL;
	}

	// Create destination image to blit/copy framebuffer pixels to
	{
		const r_vk_image_create_t xic = {
			.debug_name = "screenshot",
			.width = vk_frame.width,
			.height = vk_frame.height,
			.depth = 1,
			.mips = 1,
			.layers = 1,
			.format = dest_format,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.flags = 0,
			.memory_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		};
		temp_image = R_VkImageCreate(&xic);
	}

	// Make sure that all rendering ops are enqueued
	const qboolean draw = true;
	enqueueRendering( combuf, draw );

	// Blit/transfer
	if (blit) {
		R_VkImageBlit(combuf, &(r_vkimage_blit_args){
			.src = {
				.image = framebuffer_image,
				.width = vk_frame.width,
				.height = vk_frame.height,
				.depth = 1,
			},
			.dst = {
				.image = &temp_image,
				.width = vk_frame.width,
				.height = vk_frame.height,
				.depth = 1,
			},
		});
	} else {
		const r_vkcombuf_barrier_image_t image_barriers[] = {{
			.image = &temp_image,
			.access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			}, {
			.image = framebuffer_image,
			.access = VK_ACCESS_2_TRANSFER_READ_BIT,
			.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		}};
		R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t){
				.stage = VK_PIPELINE_STAGE_2_COPY_BIT,
				.images = {
					.count = COUNTOF(image_barriers),
					.items = image_barriers,
				},
		});

		const VkImageCopy copy = {
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
			.dstSubresource.layerCount = 1,
			.extent.width = vk_frame.width,
			.extent.height = vk_frame.height,
			.extent.depth = 1,
		};

		vkCmdCopyImage(cmdbuf,
			framebuffer_image->image, framebuffer_image->sync.layout,
			temp_image.image, temp_image.sync.layout, 1, &copy);

		gEngine.Con_Printf(S_WARN "Blit is not supported, screenshot will likely have mixed components; TODO: swizzle in software\n");
	}

	{
		const r_vkcombuf_barrier_image_t image_barriers[] = {{
			// Temp image: prepare for reading on CPU
			.image = &temp_image,
			.access = VK_ACCESS_2_MEMORY_READ_BIT,
			.layout = VK_IMAGE_LAYOUT_GENERAL,
			}, {
			// Framebuffer image: prepare for displaying
			.image = framebuffer_image,
			.access = 0,
			.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		}};
		R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t){
				.stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
				.images = {
					.count = COUNTOF(image_barriers),
					.items = image_barriers,
				},
		});
	}

	{
		const qboolean wait = true;
		submit( combuf, wait, draw );
	}

	// copy bytes to buffer
	{
		const VkImageSubresource subres = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		};
		VkSubresourceLayout layout;
		const char *mapped = temp_image.devmem.mapped;
		vkGetImageSubresourceLayout(vk_core.device, temp_image.image, &subres, &layout);

		mapped += layout.offset;

		{
			const int row_size = 4 * vk_frame.width;
			poolhandle_t r_temppool = vk_core.pool; // TODO

			r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
			r_shot->width = vk_frame.width;
			r_shot->height = vk_frame.height;
			r_shot->flags = IMAGE_HAS_COLOR;
			r_shot->type = PF_RGBA_32;
			r_shot->size = r_shot->width * r_shot->height * gEngine.Image_GetPFDesc( r_shot->type )->bpp;
			r_shot->palette = NULL;
			r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

			if (!blit) {
				if (dest_format != VK_FORMAT_R8G8B8A8_UNORM || SWAPCHAIN_FORMAT != VK_FORMAT_B8G8R8A8_UNORM) {
					gEngine.Con_Printf(S_WARN "Don't have a blit function for this format pair, will save as-is without conversion; expect image to look wrong\n");
					blit = true;
				} else {
					byte *dst = r_shot->buffer;
					for (int y = 0; y < vk_frame.height; ++y, mapped += layout.rowPitch) {
						const byte *src = (const byte*)mapped;
						for (int x = 0; x < vk_frame.width; ++x, dst += 4, src += 4) {
							dst[0] = src[2];
							dst[1] = src[1];
							dst[2] = src[0];
							dst[3] = src[3];
						}
					}
				}
			}

			if (blit) {
				for (int y = 0; y < vk_frame.height; ++y, mapped += layout.rowPitch) {
					memcpy(r_shot->buffer + row_size * y, mapped, row_size);
				}
			}
		}
	}

	R_VkImageDestroy( &temp_image );

	return r_shot;
}

qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	uint flags = 0;
	int	width = 0, height = 0;
	qboolean	result;

	const uint64_t start_ns = aprof_time_now_ns();

	// get screen frame
	rgbdata_t *r_shot = R_VkReadPixels();
	if (!r_shot)
		return false;

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngine.fsapi->AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
		flags |= IMAGE_RESAMPLE;
		if( gpGlobals->wideScreen )
		{
			height = 480;
			width = 800;
		}
		else
		{
			height = 480;
			width = 640;
		}
		break;
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = 200;
		width = 320;
		break;
	case VID_MAPSHOT:
		flags |= IMAGE_RESAMPLE|IMAGE_QUANTIZE;	// GoldSrc request overviews in 8-bit format
		height = 768;
		width = 1024;
		break;
	}

	gEngine.Image_Process( &r_shot, width, height, flags, 0.0f );

	// write image
	const uint64_t save_begin_ns = aprof_time_now_ns();
	result = gEngine.FS_SaveImage( filename, r_shot );
	const uint64_t save_end_ns = aprof_time_now_ns();

	gEngine.fsapi->AllowDirectPaths( false );			// always reset after store screenshot
	gEngine.FS_FreeImage( r_shot );

	const uint64_t end_ns = aprof_time_now_ns();
	gEngine.Con_Printf("Wrote screenshot %s. Saving file: %.03fms, total: %.03fms\n",
		filename, (save_end_ns - save_begin_ns) / 1e6, (end_ns - start_ns) / 1e6);
	return result;
}
