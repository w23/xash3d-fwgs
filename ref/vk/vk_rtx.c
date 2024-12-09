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

#include "eiface.h"
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

	RT_VkAccelFrameBegin();
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
		.uniform_buffer = g_rtx.uniform_buffer.buffer,
		.uniform_unit_size = g_rtx.uniform_unit_size,
		.geometry_data.buffer = args->render_args->geometry_data.buffer,
		.geometry_data.size = args->render_args->geometry_data.size,
		.light_bindings = args->light_bindings,
	});

	// Upload kusochki updates
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.buffer = g_ray_model_state.kusochki_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		}, {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.buffer = g_ray_model_state.model_headers_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		} };

		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	R_VkResourcesFrameBeginStateChangeFIXME(cmdbuf, g_rtx.discontinuity);
	if (g_rtx.discontinuity) {
		DEBUG("discontinuity => false");
		g_rtx.discontinuity = false;
	}

	DEBUG_BEGIN(cmdbuf, "yay tracing");

	// FIXME move this to "TLAS producer"
	{
		rt_resource_t *const tlas = R_VkResourceGetByIndex(ExternalResource_tlas);
		tlas->resource = RT_VkAccelPrepareTlas(combuf);
	}

	prepareUniformBuffer(args->render_args, args->frame_index, args->frame_counter, args->fov_angle_y, args->frame_width, args->frame_height);

	{ // FIXME this should be done automatically inside meatpipe, TODO
		//const uint32_t size = sizeof(struct Lights);
		//const uint32_t size = sizeof(struct LightsMetadata); // + 8 * sizeof(uint32_t);
		const VkBufferMemoryBarrier bmb[] = {{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.buffer = args->light_bindings->buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	// Update image resource links after the prev_-related swap above
	// TODO Preserve the indexes somewhere to avoid searching
	// FIXME I don't really get why we need this, the pointers should have been preserved ?!
	for (int i = 0; i < g_rtx.mainpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = g_rtx.mainpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);
		if (create && mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			// THIS FAILS WHY?! ASSERT(g_rtx.mainpipe_resources[i]->value.image_object == &res->image);
			g_rtx.mainpipe_resources[i]->value.image_object = &res->image;
	}

	R_VkMeatpipePerform(g_rtx.mainpipe, combuf, (vk_meatpipe_perfrom_args_t) {
		.frame_set_slot = args->frame_index,
		.width = args->frame_width,
		.height = args->frame_height,
		.resources = g_rtx.mainpipe_resources,
	});

	{
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.src = {
				.image = g_rtx.mainpipe_out->image.image,
				.width = args->frame_width,
				.height = args->frame_height,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			},
			.dst = {
				.image = args->render_args->dst.image,
				.width = args->render_args->dst.width,
				.height = args->render_args->dst.height,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			},
		};

		R_VkImageBlit( cmdbuf, &blit_args );

		// TODO this is to make sure we remember image layout after image_blit
		// The proper way to do this would be to teach R_VkImageBlit to properly track the image metadata (i.e. vk_resource_t state)
		g_rtx.mainpipe_out->resource.write.image_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
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
					.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					.flags = 0,
				};
				res->image = R_VkImageCreate(&create);
				Q_strncpy(res->name, mr->name, sizeof(res->name));
			}
		}

		newpipe_resources[i] = &res->resource;

		if (create) {
			if (mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
				newpipe_resources[i]->value.image_object = &res->image;
			}

			// TODO full r/w initialization
			res->resource.write.pipelines = 0;
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

	// TODO currently changing texture format is not handled. It will try to reuse existing image with the old format
	// which will probably fail. To handle it we'd need to refactor this:
	// 1. r_vk_image_t should have a field with its current format? (or we'd also store if with the resource here)
	// 2. do another loop here to detect format mismatch and recreate.

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

	const VkCommandBuffer cmdbuf = args->combuf->cmdbuf;
	// const xvk_ray_frame_images_t* current_frame = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself

	RT_LightsFrameEnd();
	const vk_lights_bindings_t light_bindings = VK_LightsUpload();

	g_rtx.frame_number++;

	// if (vk_core.debug)
	// 	XVK_RayModel_Validate();

	qboolean need_reload = g_rtx.reload_pipeline;

	if (g_rtx.max_frame_width < args->dst.width) {
		g_rtx.max_frame_width = ALIGN_UP(args->dst.width, 16);
		WARN("Increasing max_frame_width to %d", g_rtx.max_frame_width);
		// TODO only reload resources, no need to reload the entire pipeline
		need_reload = true;
	}

	if (g_rtx.max_frame_height < args->dst.height) {
		g_rtx.max_frame_height = ALIGN_UP(args->dst.height, 16);
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

	ASSERT(args->dst.width <= g_rtx.max_frame_width);
	ASSERT(args->dst.height <= g_rtx.max_frame_height);

	// TODO dynamic scaling based on perf
	const int frame_width = args->dst.width;
	const int frame_height = args->dst.height;

	// Do not draw when we have no swapchain
	if (args->dst.image_view == VK_NULL_HANDLE)
		goto tail;

	if (g_ray_model_state.frame.instances_count == 0) {
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.src = {
				.image = g_rtx.mainpipe_out->image.image,
				.width = frame_width,
				.height = frame_height,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			},
			.dst = {
				.image = args->dst.image,
				.width = args->dst.width,
				.height = args->dst.height,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			},
		};

		R_VkImageClear( cmdbuf, g_rtx.mainpipe_out->image.image, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
		R_VkImageBlit( cmdbuf, &blit_args );
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
