#include "vk_rtx.h"

#include "shaders/ray_interop.h" // DEBUG_DISPLAY_...

#include "vulkan/VResource.h"
#include "vk_ray_accel.h"
#include "vulkan/VBuffer.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_light.h"
#include "vk_math.h"
#include "vulkan/VMeatpipe.h"
#include "vk_ray_internal.h"
#include "r_textures.h"
#include "vulkan/VCombuf.h"
#include "vk_logs.h"
#include "rt_kusochki.h"

#include "std/profiler.h"

#include "xash3d_mathlib.h"

#include <string.h>

#define LOG_MODULE rt

#define MAX_FRAMES_IN_FLIGHT 2

#define MIN_FRAME_WIDTH 1280
#define MIN_FRAME_HEIGHT 800

static struct {
	struct {
		// Holds UniformBuffer data
		vk_buffer_t buffer;
		uint32_t unit_size;

		vk_resource_buffer_t *resource;
		Producer producer;

		struct UniformBuffer current;
	} uniform;

	// TODO with proper intra-cmdbuf sync we don't really need 2x images
	unsigned frame_number;

	struct vk_meatpipe_s *meatpipe;
	rt_resource_t *meatpipe_out;

	matrix4x4 prev_inv_proj, prev_inv_view;

	qboolean reload_pipeline;
	qboolean discontinuity;

	int max_frame_width, max_frame_height;

	struct {
		cvar_t *rt_debug_display_only;
		uint32_t rt_debug_display_only_value;

		cvar_t *rt_debug_flags;
		uint32_t rt_debug_flags_value;

		cvar_t *rt_debug_fixed_random_seed;
	} debug;
} g_rtx = {0};

void VK_RayNewMapBegin( void ) {
	// TODO it seems like these are unnecessary leftovers. Moreover, they are actively harmful,
	// as they recreate things that are in fact pretty much static. Untangle this.
	RT_VkAccelNewMap();
	RT_RayModel_Clear();
}

void VK_RayFrameBegin( void ) {
	ASSERT(vk_core.rtx);

	XVK_RayModel_ClearForNextFrame();
	RT_LightsFrameBegin();
}

static void parseDebugDisplayValue( void ) {
	if (!(g_rtx.debug.rt_debug_display_only->flags & FCVAR_CHANGED))
		return;

	g_rtx.debug.rt_debug_display_only->flags &= ~FCVAR_CHANGED;

	const char *cvalue = g_rtx.debug.rt_debug_display_only->string;
#define LIST_DISPLAYS(X) \
	X(BASECOLOR, "material base_color value") \
	X(BASEALPHA, "material alpha value") \
	X(EMISSIVE, "emissive color") \
	X(NSHADE, "shading normal") \
	X(NGEOM, "geometry normal") \
	X(LIGHTING, "all lighting, direct and indirect, w/o base_color") \
	X(SURFHASH, "each surface has random color") \
	X(DIRECT, "direct lighting only, both diffuse and specular") \
	X(DIRECT_DIFF, "direct diffuse lighting only") \
	X(DIRECT_SPEC, "direct specular lighting only") \
	X(INDIRECT, "indirect lighting only (bounced), diffuse and specular together") \
	X(INDIRECT_DIFF, "indirect diffuse only") \
	X(INDIRECT_SPEC, "indirect specular only") \
	X(TRIHASH, "each triangle is drawn with random color") \
	X(MATERIAL, "red = roughness, green = metalness") \
	X(DIFFUSE, "direct + indirect diffuse, spatially denoised") \
	X(SPECULAR, "direct + indirect specular, spatially denoised") \

#define X(suffix, info) \
	if (0 == Q_stricmp(cvalue, #suffix)) { \
		WARN("setting debug display to %s", "DEBUG_DISPLAY_"#suffix); \
		g_rtx.debug.rt_debug_display_only_value = DEBUG_DISPLAY_##suffix; \
		return; \
	}
LIST_DISPLAYS(X)
#undef X

	if (Q_strlen(cvalue) > 0) {
		gEngine.Con_Printf("Invalid rt_debug_display_only mode %s. Valid modes are:\n", cvalue);
#define X(suffix, info) gEngine.Con_Printf("\t%s -- %s\n", #suffix, info);
LIST_DISPLAYS(X)
#undef X
	}

	g_rtx.debug.rt_debug_display_only_value = DEBUG_DISPLAY_DISABLED;
//#undef LIST_DISPLAYS
}

static void parseDebugFlags( void ) {
	if (!(g_rtx.debug.rt_debug_flags->flags & FCVAR_CHANGED))
		return;

	g_rtx.debug.rt_debug_flags->flags &= ~FCVAR_CHANGED;
	g_rtx.debug.rt_debug_flags_value = 0;

#define LIST_DEBUG_FLAGS(X) \
	X(WHITE_FURNACE, "white furnace mode: diffuse white materials, diffuse sky light only") \

	const char *cvalue = g_rtx.debug.rt_debug_flags->string;
#define X(suffix, info) \
	if (0 == Q_stricmp(cvalue, #suffix)) { \
		WARN("setting debug flags to %s", "DEBUG_FLAG_"#suffix); \
		g_rtx.debug.rt_debug_flags_value |= DEBUG_FLAG_##suffix; \
	} else
LIST_DEBUG_FLAGS(X)
#undef X

	/* else: no valid flags found */
	if (Q_strlen(cvalue) > 0) {
		gEngine.Con_Printf("Invalid rt_debug_flags value %s. Valid flags are:\n", cvalue);
#define X(suffix, info) gEngine.Con_Printf("\t%s -- %s\n", #suffix, info);
LIST_DEBUG_FLAGS(X)
#undef X
	}

//#undef LIST_DEBUG_FLAGS
}

static uint32_t getRandomSeed( void ) {
	if (g_rtx.debug.rt_debug_fixed_random_seed->string[0])
		return (uint32_t)g_rtx.debug.rt_debug_fixed_random_seed->value;

	return (uint32_t)gEngine.COM_RandomLong(0, INT32_MAX);
}

static void produceUboResource(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx) {
	// TODO using frame_sequence is only accidental synchronization. It should be done via e.g. resource->consumed or smth.
	const size_t ubo_slot_offset = (ctx->frame_sequence % MAX_FRAMES_IN_FLIGHT) * g_rtx.uniform.unit_size;
	struct UniformBuffer *const ubo = PTR_CAST(struct UniformBuffer, (char*)g_rtx.uniform.buffer.mapped + ubo_slot_offset);
	g_rtx.uniform.resource->offset = ubo_slot_offset;
	ubo->frame_counter = ctx->frame_sequence;
	memcpy(ubo, &g_rtx.uniform.current, sizeof(struct UniformBuffer));
}

static struct UniformBuffer prepareUniformBuffer( const vk_ray_frame_render_args_t *args, float fov_angle_y, int frame_width, int frame_height ) {
	struct UniformBuffer ret;
	matrix4x4 proj_inv, view_inv;
	Matrix4x4_Invert_Full(proj_inv, *args->projection);
	Matrix4x4_ToArrayFloatGL(proj_inv, (float*)ret.inv_proj);

	// TODO there's a more efficient way to construct an inverse view matrix
	// from vforward/right/up vectors and origin in g_camera
	Matrix4x4_Invert_Full(view_inv, *args->view);
	Matrix4x4_ToArrayFloatGL(view_inv, (float*)ret.inv_view);

	// previous frame matrices
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_proj, (float*)ret.prev_inv_proj);
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_view, (float*)ret.prev_inv_view);
	Matrix4x4_Copy(g_rtx.prev_inv_view, view_inv);
	Matrix4x4_Copy(g_rtx.prev_inv_proj, proj_inv);

	ret.res[0] = frame_width;
	ret.res[1] = frame_height;
	ret.ray_cone_width = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)frame_height);
	ret.skybox_exposure = R_TexturesGetSkyboxInfo().exposure;

	parseDebugDisplayValue();
	if (g_rtx.debug.rt_debug_display_only_value) {
		ret.debug_display_only = g_rtx.debug.rt_debug_display_only_value;
	} else {
		ret.debug_display_only = r_lightmap->value != 0 ? DEBUG_DISPLAY_LIGHTING : DEBUG_DISPLAY_DISABLED;
	}

	parseDebugFlags();
	ret.debug_flags = g_rtx.debug.rt_debug_flags_value;

	ret.random_seed = getRandomSeed();

#define SET_RENDERER_FLAG(cvar,flag) (CVAR_TO_BOOL(cvar) ? flag : 0)
	ret.renderer_flags = SET_RENDERER_FLAG(rt_only_diffuse_gi, RENDERER_FLAG_ONLY_DIFFUSE_GI) |
						  SET_RENDERER_FLAG(rt_separated_reflection, RENDERER_FLAG_SEPARATED_REFLECTION) |
						  SET_RENDERER_FLAG(rt_denoise_gi_by_sh, RENDERER_FLAG_DENOISE_GI_BY_SH) |
						  SET_RENDERER_FLAG(rt_disable_gi, RENDERER_FLAG_DISABLE_GI) |
						  SET_RENDERER_FLAG(rt_spatial_reconstruction, RENDERER_FLAG_SPATIAL_RECONSTRUCTION);
#undef SET_RENDERER_FLAG

	return ret;
}

typedef struct {
	const vk_ray_frame_render_args_t* render_args;
	int frame_index;
	uint32_t frame_counter;
	float fov_angle_y;
	int frame_width, frame_height;
} perform_tracing_args_t;

static r_vk_image_t* performTracing( vk_combuf_t *combuf, const perform_tracing_args_t* args) {
	APROF_SCOPE_DECLARE_BEGIN(perform, __FUNCTION__);
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;
	DEBUG_BEGIN(cmdbuf, "yay tracing");

	g_rtx.uniform.current = prepareUniformBuffer(args->render_args, args->fov_angle_y, args->frame_width, args->frame_height);

	ASSERT(g_rtx.meatpipe);
	r_vk_image_t *const ret = R_VkMeatpipeDispatch(g_rtx.meatpipe, (vk_meatpipe_dispatch_t){
		.combuf = combuf,
		.frame_sequence = args->frame_counter,
		.frame_set_slot = args->frame_index,
		.width = args->frame_width,
		.height = args->frame_height,
		.is_discontinuous = g_rtx.discontinuity,
	});

	if (g_rtx.discontinuity) {
		DEBUG("discontinuity => false");
		g_rtx.discontinuity = false;
	}

	DEBUG_END(cmdbuf);
	APROF_SCOPE_END(perform);

	return ret;
}

static void destroyMeatpipe(void) {
	R_VkMeatpipeDestroy(g_rtx.meatpipe);
	g_rtx.meatpipe = NULL;
}

static qboolean reloadMeatpipe(void) {
	struct vk_meatpipe_s *const newpipe = R_VkMeatpipeCreateFromFile("rt.meat");
	if (!newpipe)
		return false;

	if (!R_VkMeatpipeAcquireResources(newpipe, g_rtx.max_frame_width, g_rtx.max_frame_height))
		goto fail;

	destroyMeatpipe();

	g_rtx.meatpipe = newpipe;
	g_rtx.meatpipe_out = R_VkResourceFindByName("dest");
	ASSERT(g_rtx.meatpipe_out);

	return true;

fail:
	R_VkMeatpipeDestroy(newpipe);
	return false;
}

static void reloadOrResizeIfNeeded(const vk_ray_frame_render_args_t* args) {
	qboolean need_resize = false;

	if (g_rtx.max_frame_width < args->dst->width) {
		g_rtx.max_frame_width = ALIGN_UP(args->dst->width, 16);
		WARN("Increasing max_frame_width to %d", g_rtx.max_frame_width);
		need_resize = true;
	}

	if (g_rtx.max_frame_height < args->dst->height) {
		g_rtx.max_frame_height = ALIGN_UP(args->dst->height, 16);
		WARN("Increasing max_frame_height to %d", g_rtx.max_frame_height);
		need_resize = true;
	}

	if (g_rtx.reload_pipeline) {
		WARN("Reloading RTX shaders/pipelines");
		XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

		if (reloadMeatpipe())
			need_resize = false;

		g_rtx.reload_pipeline = false;
	}

	if (need_resize) {
		if (!R_VkMeatpipeAcquireResources(g_rtx.meatpipe, g_rtx.max_frame_width, g_rtx.max_frame_height)) {
			ERR("Unable to reacquire resources and resize RT framebuffer. Bad things will happen.");
		}
		need_resize = false;
	}

	ASSERT(args->dst->width <= g_rtx.max_frame_width);
	ASSERT(args->dst->height <= g_rtx.max_frame_height);
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	APROF_SCOPE_DECLARE_BEGIN(ray_frame_end, __FUNCTION__);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself

	g_rtx.frame_number++;

	reloadOrResizeIfNeeded(args);

	// TODO dynamic scaling based on perf
	const int frame_width = args->dst->width;
	const int frame_height = args->dst->height;

	// Do not draw when we have no swapchain
	if (!args->dst->image)
		goto tail;

	if (RT_VkAccelIsEmpty()) {
		R_VkImageClear( args->dst, args->combuf, NULL );
	} else {
		const perform_tracing_args_t trace_args = {
			.render_args = args,
			.frame_index = (g_rtx.frame_number % 2),
			.frame_counter = g_rtx.frame_number,
			.fov_angle_y = args->fov_angle_y,
			.frame_width = frame_width,
			.frame_height = frame_height,
		};
		r_vk_image_t *const result = performTracing( args->combuf, &trace_args );
		ASSERT(g_rtx.meatpipe_out);
		const r_vkimage_blit_args blit_args = {
			.src = {
				.image = result,
				.width = frame_width,
				.height = frame_height,
			},
			.dst = {
				.image = args->dst,
			},
		};

		R_VkImageBlit( args->combuf, &blit_args );
	}

tail:
	APROF_SCOPE_END(ray_frame_end);
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure

	g_rtx.max_frame_width = MIN_FRAME_WIDTH;
	g_rtx.max_frame_height = MIN_FRAME_HEIGHT;

	if (!RT_VkAccelInit())
		return false;

	// FIXME shutdown accel
	if (!RT_DynamicModelInit())
		return false;

	g_rtx.uniform.unit_size = ALIGN_UP(sizeof(struct UniformBuffer), v_device_info.properties.limits.minUniformBufferOffsetAlignment);

	if (!VK_BufferCreate("ray uniform.buffer", &g_rtx.uniform.buffer, g_rtx.uniform.unit_size * MAX_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		// TODO cleanup
		return false;
	}

	g_rtx.uniform.producer = (Producer) {
		.name = "ubo",
		.produce = produceUboResource,
	};

	g_rtx.uniform.resource = R_VkBufferRegisterAsResource((r_vkbuffer_register_as_resource_t){
		.name = "ubo",
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.buffer = &g_rtx.uniform.buffer,
		.offset = 0, // Will be set dynamically each frame
		.size = sizeof(struct UniformBuffer),
		.producer = &g_rtx.uniform.producer,
	});

	if (!RT_KusochkiInit()) {
		// TODO cleanup
		return false;
	}

	reloadMeatpipe();
	if (!g_rtx.meatpipe)
		return false;

	RT_RayModel_Clear();

	gEngine.Cmd_AddCommand("rt_debug_reload_pipelines", reloadPipeline, "Reload RT pipelines");

#define X(name, info) #name ", "
	g_rtx.debug.rt_debug_display_only = gEngine.Cvar_Get("rt_debug_display_only", "", FCVAR_GLCONFIG,
		"Display only the specified channel (" LIST_DISPLAYS(X) "etc)");

	g_rtx.debug.rt_debug_flags = gEngine.Cvar_Get("rt_debug_flags", "", FCVAR_GLCONFIG,
		"Enable shader debug flags (" LIST_DEBUG_FLAGS(X) "etc)");
#undef X

	g_rtx.debug.rt_debug_fixed_random_seed = gEngine.Cvar_Get("rt_debug_fixed_random_seed", "", FCVAR_GLCONFIG,
		"Fix random seed value for RT monte carlo sampling. Used for reproducible regression testing");

	return true;
}

void VK_RayShutdown( void ) {
	ASSERT(vk_core.rtx);

	destroyMeatpipe();

	RT_KusochkiShutdown();
	VK_BufferDestroy(&g_rtx.uniform.buffer);

	RT_VkAccelShutdown();
	RT_DynamicModelShutdown();
}

void RT_FrameDiscontinuity( void ) {
	DEBUG("%s", __FUNCTION__);
	g_rtx.discontinuity = true;
}
