#pragma once

#include "xash3d_types.h" // matrix4x4, vec4_t

qboolean R_VkRasterInit(void);
void R_VkRasterShutdown(void);

void R_VkRasterBeginFrame(void);

struct vk_render_geometry_s;
typedef struct {
	const char *debug_name;
	int lightmap; // TODO per-geometry
	const struct vk_render_geometry_s *geometries;
	// Optional: indirection, only these geometry indices are to be drawn
	/*const*/ int *geometries_indexes; // [geometries_count]
	int geometries_count;
	const matrix4x4 *transform;
	const vec4_t *color;
	int render_type;
	int textures_override;
} vk_raster_add_model_t;

void R_VkRasterAddModel( vk_raster_add_model_t args );

struct vk_combuf_s;
struct FrameContext;
void R_VkRasterPrepareFrame( struct vk_combuf_s* combuf, const struct FrameContext *ctx );

// Usage:
// - VK_RasterBeginFrame
// - R_VkRasterPrepareFrame
// - VK_RasterSubmit
// - VK_RasterBeginFrame ...

typedef struct {
	struct vk_combuf_s* combuf;
	uint32_t width;
	uint32_t height;
	int frame_index;
	const matrix4x4 *projection;
	const matrix4x4 *view;
	const matrix4x4 *projection_view;
} vk_raster_submit_t;

void R_VkRasterSubmit(vk_raster_submit_t args);
