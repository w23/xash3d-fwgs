#pragma once

#include "vk_geometry.h"
#include "vk_core.h"

void VK_RayFrameBegin( void );

typedef struct {
	struct vk_combuf_s *combuf;

	struct {
		VkImageView image_view;
		VkImage image;
		uint32_t width, height;
	} dst;

	const matrix4x4 *projection, *view;

	// Buffer holding vertex and index data
	// TODO remove
	struct {
		VkBuffer buffer; // must be the same as in vk_ray_model_create_t TODO: validate or make impossible to specify incorrectly
		uint64_t size;
	} geometry_data;

	float fov_angle_y;
} vk_ray_frame_render_args_t;
void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args);

void VK_RayNewMap( void );

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

struct vk_render_geometry_s;
struct rt_model_s;
struct r_vk_material_s;

typedef enum {
	kBlasBuildStatic, // builds slow for fast trace
	kBlasBuildDynamicUpdate, // builds if not built, updates if built
	kBlasBuildDynamicFast, // builds fast from scratch (no correlation with previous frame guaranteed, e.g. triapi)
} rt_blas_usage_e;

typedef struct {
	const char *debug_name; // Must remain alive until RT_ModelDestroy
	const struct vk_render_geometry_s *geometries;
	int geometries_count;
	rt_blas_usage_e usage;
} rt_model_create_t;
struct rt_model_s *RT_ModelCreate(rt_model_create_t args);
void RT_ModelDestroy(struct rt_model_s *model);

qboolean RT_ModelUpdate(struct rt_model_s *model, const struct vk_render_geometry_s *geometries, int geometries_count);

qboolean RT_ModelUpdateMaterials(struct rt_model_s *model, const struct vk_render_geometry_s *geometries, int geometries_count, const int *geom_indices, int geom_indices_count);

typedef struct {
	int material_mode;
	const matrix3x4 *transform, *prev_transform;
	const vec4_t *color_srgb;

	struct rt_light_add_polygon_s *dynamic_polylights;
	int dynamic_polylights_count;

	struct {
		const struct r_vk_material_s *material;

		// These are needed in order to recreate kusochki geometry data
		// TODO remove when material data is split from kusochki
		int geoms_count;
		const struct vk_render_geometry_s *geoms;
	} override;
} rt_frame_add_model_t;

void RT_FrameAddModel( struct rt_model_s *model, rt_frame_add_model_t args );

typedef struct {
	const char *debug_name;
	const struct vk_render_geometry_s *geometries;
	const vec4_t *color;
	int geometries_count;
	int render_type;
} rt_frame_add_once_t;

void RT_FrameAddOnce( rt_frame_add_once_t args );

// Signal that the next frame is discontinuous, and all accumulated screen-space
// statistics should be reset. Should help with newmap/saveload/teleport denoiser artifacts.
void RT_FrameDiscontinuity( void );
