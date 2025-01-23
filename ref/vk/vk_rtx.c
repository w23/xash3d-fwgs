#include "vk_rtx.h"

#include "vk_resources.h"
#include "vk_ray_accel.h"

#include "vk_buffer.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_descriptor.h"
#include "vk_light.h"
#include "vk_math.h"
#include "vk_meatpipe.h"
#include "vk_pipeline.h"
#include "vk_ray_internal.h"
#include "r_textures.h"
#include "vk_combuf.h"
#include "vk_logs.h"

#include "profiler.h"

#include "xash3d_mathlib.h"

#include <string.h>

#define LOG_MODULE rt

#define MAX_FRAMES_IN_FLIGHT 2

#define MIN_FRAME_WIDTH 1280
#define MIN_FRAME_HEIGHT 800

static struct {
	// Holds UniformBuffer data
	vk_buffer_t uniform_buffer;
	uint32_t uniform_unit_size;

	// TODO with proper intra-cmdbuf sync we don't really need 2x images
	unsigned frame_number;

	// Main RT rendering pipeline configuration
	vk_meatpipe_t *mainpipe;

	// Helper list of resource pointers to global resource map
	// Needed as an argument to `R_VkMeatpipePerform()` so that meatpipe can access resources
	vk_resource_p *mainpipe_resources;

	// Pointer to the `dest` image produced by mainpipe
	// TODO this should be a regular registered resource, nothing special about it
	rt_resource_t *mainpipe_out;

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

static void prepareUniformBuffer( const vk_ray_frame_render_args_t *args, int frame_index, uint32_t frame_counter, float fov_angle_y, int frame_width, int frame_height ) {
	struct UniformBuffer *ubo = PTR_CAST(struct UniformBuffer, (char*)g_rtx.uniform_buffer.mapped + frame_index * g_rtx.uniform_unit_size);

	matrix4x4 proj_inv, view_inv;
	Matrix4x4_Invert_Full(proj_inv, *args->projection);
	Matrix4x4_ToArrayFloatGL(proj_inv, (float*)ubo->inv_proj);

	// TODO there's a more efficient way to construct an inverse view matrix
	// from vforward/right/up vectors and origin in g_camera
	Matrix4x4_Invert_Full(view_inv, *args->view);
	Matrix4x4_ToArrayFloatGL(view_inv, (float*)ubo->inv_view);

	// previous frame matrices
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_proj, (float*)ubo->prev_inv_proj);
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_view, (float*)ubo->prev_inv_view);
	Matrix4x4_Copy(g_rtx.prev_inv_view, view_inv);
	Matrix4x4_Copy(g_rtx.prev_inv_proj, proj_inv);

	ubo->res[0] = frame_width;
	ubo->res[1] = frame_height;
	ubo->ray_cone_width = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)frame_height);
	ubo->frame_counter = frame_counter;
	ubo->skybox_exposure = R_TexturesGetSkyboxInfo().exposure;

	parseDebugDisplayValue();
	if (g_rtx.debug.rt_debug_display_only_value) {
		ubo->debug_display_only = g_rtx.debug.rt_debug_display_only_value;
	} else {
		ubo->debug_display_only = r_lightmap->value != 0 ? DEBUG_DISPLAY_LIGHTING : DEBUG_DISPLAY_DISABLED;
	}

	parseDebugFlags();
	ubo->debug_flags = g_rtx.debug.rt_debug_flags_value;

	ubo->random_seed = getRandomSeed();

#define SET_RENDERER_FLAG(cvar,flag) (CVAR_TO_BOOL(cvar) ? flag : 0)

	ubo->renderer_flags = SET_RENDERER_FLAG(rt_only_diffuse_gi, RENDERER_FLAG_ONLY_DIFFUSE_GI) |
						  SET_RENDERER_FLAG(rt_separated_reflection, RENDERER_FLAG_SEPARATED_REFLECTION) |
						  SET_RENDERER_FLAG(rt_denoise_gi_by_sh, RENDERER_FLAG_DENOISE_GI_BY_SH) |
						  SET_RENDERER_FLAG(rt_disable_gi, RENDERER_FLAG_DISABLE_GI) |
						  SET_RENDERER_FLAG(rt_spatial_reconstruction, RENDERER_FLAG_SPATIAL_RECONSTRUCTION);

#undef SET_RENDERER_FLAG
}

typedef struct {
	const vk_ray_frame_render_args_t* render_args;
	int frame_index;
	uint32_t frame_counter;
	float fov_angle_y;
	const vk_lights_bindings_t *light_bindings;
	int frame_width, frame_height;
} perform_tracing_args_t;

static void performTracing( vk_combuf_t *combuf, const perform_tracing_args_t* args) {
	APROF_SCOPE_DECLARE_BEGIN(perform, __FUNCTION__);
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	R_VkResourcesSetBuiltinFIXME((r_vk_resources_builtin_fixme_t){
		.frame_index = args->frame_index,
		.uniform_buffer = &g_rtx.uniform_buffer,
		.uniform_unit_size = g_rtx.uniform_unit_size,
		.geometry_data.buffer = args->render_args->geometry_data.buffer,
		.geometry_data.size = args->render_args->geometry_data.size,
		.light_bindings = args->light_bindings,
	});

	R_VkResourcesFrameBeginStateChangeFIXME(combuf, g_rtx.discontinuity);
	if (g_rtx.discontinuity) {
		DEBUG("discontinuity => false");
		g_rtx.discontinuity = false;
	}

	DEBUG_BEGIN(cmdbuf, "yay tracing");

	prepareUniformBuffer(args->render_args, args->frame_index, args->frame_counter, args->fov_angle_y, args->frame_width, args->frame_height);

	// Update image resource links after the prev_-related swap above
	// TODO Preserve the indexes somewhere to avoid searching
	// FIXME I don't really get why we need this, the pointers should have been preserved ?!
	for (int i = 0; i < g_rtx.mainpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = g_rtx.mainpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);
		if (create && mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			// THIS FAILS WHY?! ASSERT(g_rtx.mainpipe_resources[i]->value.image_object == &res->image);
			g_rtx.mainpipe_resources[i]->ref.image = &res->image;
	}

	R_VkMeatpipePerform(g_rtx.mainpipe, combuf, (vk_meatpipe_perfrom_args_t) {
		.frame_set_slot = args->frame_index,
		.width = args->frame_width,
		.height = args->frame_height,
		.resources = g_rtx.mainpipe_resources,
	});

	DEBUG_END(cmdbuf);

	APROF_SCOPE_END(perform);
}

static void destroyMainpipe(void) {
	if (!g_rtx.mainpipe)
		return;

	ASSERT(g_rtx.mainpipe_resources);

	for (int i = 0; i < g_rtx.mainpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = g_rtx.mainpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);
		ASSERT(res->refcount > 0);
		res->refcount--;
	}

	R_VkResourcesCleanup();
	R_VkMeatpipeDestroy(g_rtx.mainpipe);
	g_rtx.mainpipe = NULL;

	Mem_Free(g_rtx.mainpipe_resources);
	g_rtx.mainpipe_resources = NULL;
	g_rtx.mainpipe_out = NULL;
}

static void reloadMainpipe(void) {
	vk_meatpipe_t *const newpipe = R_VkMeatpipeCreateFromFile("rt.meat");
	if (!newpipe)
		return;

	const size_t newpipe_resources_size = sizeof(vk_resource_p) * newpipe->resources_count;
	vk_resource_p *newpipe_resources = Mem_Calloc(vk_core.pool, newpipe_resources_size);
	rt_resource_t *newpipe_out = NULL;

	for (int i = 0; i < newpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = newpipe->resources + i;
		DEBUG("res %d/%d: %s descriptor=%u count=%d flags=[%c%c] image_format=(%s)%u",
			i, newpipe->resources_count, mr->name, mr->descriptor_type, mr->count,
			(mr->flags & MEATPIPE_RES_WRITE) ? 'W' : ' ',
			(mr->flags & MEATPIPE_RES_CREATE) ? 'C' : ' ',
			R_VkFormatName(mr->image_format),
			mr->image_format);

		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);

		if (create && mr->descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			ERR("Only storage image creation is supported for meatpipes");
			goto fail;
		}

		// TODO this should be specified as a flag, from rt.json
		const qboolean output = Q_strcmp("dest", mr->name) == 0;

		rt_resource_t *const res = create ? R_VkResourceFindOrAlloc(mr->name) : R_VkResourceFindByName(mr->name);
		if (!res) {
			ERR("Couldn't find resource/slot for %s", mr->name);
			goto fail;
		}

		if (output)
			newpipe_out = res;

		if (create) {
			const qboolean is_compatible = (res->image.image != VK_NULL_HANDLE)
				&& (mr->image_format == res->image.format)
				&& (g_rtx.max_frame_width <= res->image.width)
				&& (g_rtx.max_frame_height <= res->image.height);

			if (!is_compatible) {
				if (res->image.image != VK_NULL_HANDLE)
					R_VkImageDestroy(&res->image);

				const r_vk_image_create_t create = {
					.debug_name = mr->name,
					.width = g_rtx.max_frame_width,
					.height = g_rtx.max_frame_height,
					.depth = 1,
					.mips = 1,
					.layers = 1,
					.format = mr->image_format,
					.tiling = VK_IMAGE_TILING_OPTIMAL,
					// TODO figure out how to detect this need properly. prev_dest is not defined as "output"
					//.usage = VK_IMAGE_USAGE_STORAGE_BIT | (output ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
					.usage = VK_IMAGE_USAGE_STORAGE_BIT
						//| VK_IMAGE_USAGE_SAMPLED_BIT // required by VK_IMAGE_LAYOUT_SHADER_READ_OPTIMAL
						| VK_IMAGE_USAGE_TRANSFER_SRC_BIT
						| VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					.flags = 0,
				};
				res->image = R_VkImageCreate(&create);
				Q_strncpy(res->name, mr->name, sizeof(res->name));
			}
		}

		newpipe_resources[i] = &res->resource;

		if (create) {
			if (mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
				newpipe_resources[i]->ref.image = &res->image;
			}

			// TODO full r/w initialization
			// FIXME not sure if not needed res->resource.deprecate.write.pipelines = 0;
			res->resource.type = mr->descriptor_type;
		} else {
			// TODO no assert, complain and exit
			// can't do before all resources are properly registered by their producers and not all this temp crap we have right now
			// ASSERT(res->resource.type == mr->descriptor_type);
		}
	}

	if (!newpipe_out) {
		ERR("New rt.json doesn't define an 'dest' output texture");
		goto fail;
	}

	// Resolve prev_ frame resources
	for (int i = 0; i < newpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = newpipe->resources + i;
		if (mr->prev_frame_index_plus_1 <= 0)
			continue;

		ASSERT(mr->prev_frame_index_plus_1 < newpipe->resources_count);

		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);

		const vk_meatpipe_resource_t *pr = newpipe->resources + (mr->prev_frame_index_plus_1 - 1);

		const int dest_index = R_VkResourceFindIndexByName(pr->name);
		if (dest_index < 0) {
			ERR("Couldn't find prev_ resource/slot %s for resource %s", pr->name, mr->name);
			goto fail;
		}

		res->source_index_plus_1 = dest_index + 1;
	}

	// Loading successful
	// Update refcounts
	for (int i = 0; i < newpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = newpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);
		res->refcount++;
	}

	destroyMainpipe();

	g_rtx.mainpipe = newpipe;
	g_rtx.mainpipe_resources = newpipe_resources;
	g_rtx.mainpipe_out = newpipe_out;

	return;

fail:
	R_VkResourcesCleanup();

	if (newpipe_resources)
		Mem_Free(newpipe_resources);

	R_VkMeatpipeDestroy(newpipe);
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	APROF_SCOPE_DECLARE_BEGIN(ray_frame_end, __FUNCTION__);

	// const xvk_ray_frame_images_t* current_frame = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself

	RT_LightsFrameEnd();
	const vk_lights_bindings_t light_bindings = VK_LightsUpload(args->combuf);

	g_rtx.frame_number++;

	// if (vk_core.debug)
	// 	XVK_RayModel_Validate();

	qboolean need_reload = g_rtx.reload_pipeline;

	if (g_rtx.max_frame_width < args->dst->width) {
		g_rtx.max_frame_width = ALIGN_UP(args->dst->width, 16);
		WARN("Increasing max_frame_width to %d", g_rtx.max_frame_width);
		// TODO only reload resources, no need to reload the entire pipeline
		need_reload = true;
	}

	if (g_rtx.max_frame_height < args->dst->height) {
		g_rtx.max_frame_height = ALIGN_UP(args->dst->height, 16);
		WARN("Increasing max_frame_height to %d", g_rtx.max_frame_height);
		// TODO only reload resources, no need to reload the entire pipeline
		need_reload = true;
	}

	if (need_reload) {
		WARN("Reloading RTX shaders/pipelines");
		XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

		reloadMainpipe();

		g_rtx.reload_pipeline = false;
	}

	ASSERT(g_rtx.mainpipe_out);

	// Feed tlas with dynamic data
	RT_DynamicModelProcessFrame();

	// FIXME what's the right place for this?
	// This needs to happen every frame where we might've locked staging for kusochki
	// - After dynamic stuff (might upload kusochki)
	// - Before performTracing(), even if it is not called
	// See ~3:00:00-3:40:00 of stream E383 about push-vs-pull models and their boundaries.
	R_VkBufferStagingCommit(&g_ray_model_state.kusochki_buffer, args->combuf);

	ASSERT(args->dst->width <= g_rtx.max_frame_width);
	ASSERT(args->dst->height <= g_rtx.max_frame_height);

	// TODO dynamic scaling based on perf
	const int frame_width = args->dst->width;
	const int frame_height = args->dst->height;

	rt_resource_t *const tlas = R_VkResourceGetByIndex(ExternalResource_tlas);

	// Do not draw when we have no swapchain
	if (!args->dst->image)
		goto tail;

	// TODO move this to "TLAS producer"
	tlas->resource = RT_VkAccelPrepareTlas(args->combuf);
	if (tlas->resource.value.accel.accelerationStructureCount == 0) {
		R_VkImageClear( &g_rtx.mainpipe_out->image, args->combuf, NULL );
	} else {
		const perform_tracing_args_t trace_args = {
			.render_args = args,
			.frame_index = (g_rtx.frame_number % 2),
			.frame_counter = g_rtx.frame_number,
			.fov_angle_y = args->fov_angle_y,
			.light_bindings = &light_bindings,
			.frame_width = frame_width,
			.frame_height = frame_height,
		};
		performTracing( args->combuf, &trace_args );
	}

	{
		const r_vkimage_blit_args blit_args = {
			.src = {
				.image = &g_rtx.mainpipe_out->image,
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

	R_VkResourcesInit();

	reloadMainpipe();
	if (!g_rtx.mainpipe)
		return false;

	g_rtx.uniform_unit_size = ALIGN_UP(sizeof(struct UniformBuffer), vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);

	if (!VK_BufferCreate("ray uniform_buffer", &g_rtx.uniform_buffer, g_rtx.uniform_unit_size * MAX_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		return false;
	}

	if (!VK_BufferCreate("ray kusochki_buffer", &g_ray_model_state.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!VK_BufferCreate("model headers", &g_ray_model_state.model_headers_buffer, sizeof(struct ModelHeader) * MAX_INSTANCES,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		// FIXME complain, handle
		return false;
	}

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

	destroyMainpipe();

	VK_BufferDestroy(&g_ray_model_state.model_headers_buffer);
	VK_BufferDestroy(&g_ray_model_state.kusochki_buffer);
	VK_BufferDestroy(&g_rtx.uniform_buffer);

	RT_VkAccelShutdown();
	RT_DynamicModelShutdown();
}

void RT_FrameDiscontinuity( void ) {
	DEBUG("%s", __FUNCTION__);
	g_rtx.discontinuity = true;
}
