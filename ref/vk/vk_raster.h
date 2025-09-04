#pragma once

#include "vk_common.h"
#include "xash3d_types.h" // matrix4x4, vec4_t

qboolean VK_RasterInit(void);
void VK_RasterShutdown(void);

void VK_RasterBegin(void);

struct vk_render_geometry_s;
typedef struct {
	const char *debug_name;
	int lightmap; // TODO per-geometry
	const struct vk_render_geometry_s *geometries;
	int geometries_count;
	const matrix4x4 *transform;
	const matrix4x4 *projection_view; // TODO remove, this is only needed when submitting for rendering
	const vec4_t *color;
	int render_type;
	int textures_override;
} vk_raster_add_model_t;

void VK_RasterAddModel( vk_raster_add_model_t args );

typedef struct {
	struct vk_combuf_s* combuf;
	uint32_t width;
	uint32_t height;
	int frame_index;
	const matrix4x4 *projection;
	const matrix4x4 *view;
	const matrix4x4 *projection_view;
} vk_raster_submit_t;

void VK_RasterSubmit(vk_raster_submit_t args);
