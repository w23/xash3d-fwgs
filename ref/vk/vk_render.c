#include "vk_render.h"

#include "vk_core.h"
#include "vulkan/VBuffer.h"
#include "vk_geometry.h"
#include "vulkan/VBarrier.h"
#include "vulkan/VResource.h"
#include "vulkan/VCombuf.h"
#include "vk_common.h"
#include "vulkan/VPipeline.h"
#include "vk_math.h"
#include "vk_rtx.h"
#include "std/profiler.h"
#include "r_speeds.h"
#include "camera.h"
#include "vk_raster.h"

#include "xash3d_mathlib.h"
#include "xash3d_types.h"

#include <memory.h>

#define MODULE_NAME "render"

#define PROFILER_SCOPES(X) \
	X(renderbegin, "VK_RenderBegin"); \

#define SCOPE_DECLARE(scope, name) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

static struct {
	struct {
		int dynamic_model_count;
		int models_count;
	} stats;
} g_render;

static struct {
	matrix4x4 vk_projection;
	matrix4x4 projection_view;

	qboolean current_frame_is_ray_traced;
} g_render_state;

qboolean VK_RenderInit( void ) {
	PROFILER_SCOPES(APROF_SCOPE_INIT);

	R_SPEEDS_COUNTER(g_render.stats.dynamic_model_count, "models_dynamic", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_render.stats.models_count, "models", kSpeedsMetricCount);

	return VK_RasterInit();
}

void VK_RenderShutdown( void )
{
	VK_RasterShutdown();
}

void VK_RenderBegin( qboolean ray_tracing ) {
	APROF_SCOPE_BEGIN(renderbegin);

	g_render_state.current_frame_is_ray_traced = ray_tracing;

	R_GeometryBuffer_Flip();

	VK_RasterBegin();

	if (ray_tracing)
		VK_RayFrameBegin();

	APROF_SCOPE_END(renderbegin);
}

// Vulkan has Y pointing down, and z should end up in (0, 1)
// NOTE this matrix is row-major
static const matrix4x4 vk_proj_fixup = {
	{1, 0, 0, 0},
	{0, -1, 0, 0},
	{0, 0, .5, .5},
	{0, 0, 0, 1}
};

void VK_RenderSetupCamera( const struct ref_viewpass_s *rvp ) {
	R_SetupCamera(rvp);
	Matrix4x4_Concat(g_render_state.vk_projection, vk_proj_fixup, g_camera.projectionMatrix);
	Matrix4x4_Concat(g_render_state.projection_view, g_render_state.vk_projection, g_camera.viewMatrix);
}

void VK_RenderEndRTX( struct vk_combuf_s* combuf, struct r_vk_image_s *dst) {
	ASSERT(vk_core.rtx);

	{
		const vk_ray_frame_render_args_t args = {
			.combuf = combuf,
			.dst = dst,

			.projection = &g_render_state.vk_projection,
			.view = &g_camera.viewMatrix,

			.fov_angle_y = g_camera.fov_y,
		};

		VK_RayFrameEnd(&args);
	}
}

void VK_RenderEnd( struct vk_combuf_s* combuf, qboolean draw, uint32_t width, uint32_t height, int frame_index ) {
	if (!draw)
		return;

	VK_RasterSubmit((vk_raster_submit_t){
		.combuf = combuf,
		.width = width,
		.height = height,
		.frame_index = frame_index,
		.projection = &g_render_state.vk_projection,
		.view = &g_camera.viewMatrix,
		.projection_view = &g_render_state.projection_view,
	});
}

qboolean R_RenderModelCreate( vk_render_model_t *model, vk_render_model_init_t args ) {
	memset(model, 0, sizeof(*model));
	Q_strncpy(model->debug_name, args.name, sizeof(model->debug_name));

	model->geometries = args.geometries;
	model->num_geometries = args.geometries_count;

	if (!vk_core.rtx)
		return true;

	model->rt_model = RT_ModelCreate((rt_model_create_t){
		.debug_name = model->debug_name,
		.geometries = args.geometries,
		.geometries_count = args.geometries_count,
		.usage = args.dynamic ? kBlasBuildDynamicUpdate : kBlasBuildStatic,
	});
	return !!model->rt_model;
}

void R_RenderModelDestroy( vk_render_model_t* model ) {
	if (model->rt_model)
		RT_ModelDestroy(model->rt_model);
}

qboolean R_RenderModelUpdate( const vk_render_model_t *model ) {
	// Non-RT rendering doesn't need to update anything, assuming that geometry regions offsets are not changed, and losing intermediate states is fine
	if (!g_render_state.current_frame_is_ray_traced)
		return true;

	ASSERT(model->rt_model);

	return RT_ModelUpdate(model->rt_model, model->geometries, model->num_geometries);
}

qboolean R_RenderModelUpdateMaterials( const vk_render_model_t *model, const int *geom_indices, int geom_indices_count) {
	if (!model->rt_model)
		return true;

	return RT_ModelUpdateMaterials(model->rt_model, model->geometries, model->num_geometries, geom_indices, geom_indices_count);
}

void R_RenderModelDraw(const vk_render_model_t *model, r_model_draw_t args) {
	++g_render.stats.models_count;

	if (g_render_state.current_frame_is_ray_traced) {
		ASSERT(model->rt_model);
		RT_FrameAddModel(model->rt_model, (rt_frame_add_model_t){
			.material_mode = args.material_mode,
			.material_flags = args.material_flags,
			.transform = (const matrix3x4*)args.transform,
			.prev_transform = (const matrix3x4*)args.prev_transform,
			.color_srgb = args.color,
			.override = {
				.material = args.override.material,
				.geoms = model->geometries,
				.geoms_count = model->num_geometries,
			},
		});
	} else {
		VK_RasterAddModel((vk_raster_add_model_t){
			.debug_name = model->debug_name,
			.lightmap = model->lightmap,
			.geometries = model->geometries,
			.geometries_count = model->num_geometries,
			.transform = args.transform,
			.projection_view = &g_render_state.projection_view,
			.color = args.color,
			.render_type = args.render_type,
			.textures_override = args.override.old_texture,
		});
	}
}

void R_RenderDrawOnce(r_draw_once_t args) {
	r_geometry_buffer_lock_t buffer;
	if (!R_GeometryBufferAllocOnceAndLock( &buffer, args.vertices_count, args.indices_count)) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate geometry for dynamic draw\n");
		return;
	}

	memcpy(buffer.vertices.ptr, args.vertices, sizeof(vk_vertex_t) * args.vertices_count);
	memcpy(buffer.indices.ptr, args.indices, sizeof(uint16_t) * args.indices_count);

	R_GeometryBufferUnlock( &buffer );

	const vk_render_geometry_t geometry = {
		.material = args.material,
		.ye_olde_texture = args.ye_olde_texture,

		.max_vertex = args.vertices_count,
		.vertex_offset = buffer.vertices.unit_offset,

		.element_count = args.indices_count,
		.index_offset = buffer.indices.unit_offset,

		.emissive = { (*args.color)[0], (*args.color)[1], (*args.color)[2] },
	};

	if (g_render_state.current_frame_is_ray_traced) {
		RT_FrameAddOnce((rt_frame_add_once_t){
			.debug_name = args.name,
			.geometries = &geometry,
			.color_srgb = args.color,
			.geometries_count = 1,
			.render_type = args.render_type,
		});
	} else {
		matrix4x4 identity;
		Matrix4x4_LoadIdentity(identity);
		VK_RasterAddModel((vk_raster_add_model_t){
			.debug_name = args.name,
			.lightmap = 0,
			.geometries = &geometry,
			.geometries_count = 1,
			.transform = &identity,
			.projection_view = &g_render_state.projection_view,
			.color = args.color,
			.render_type = args.render_type,
			.textures_override = -1,
		});
	}

	g_render.stats.dynamic_model_count++;
}
