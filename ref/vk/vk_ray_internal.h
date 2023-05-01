#pragma once

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_const.h"

#define MAX_ACCELS 2048
#define MAX_KUSOCHKI 32768
#define MODEL_CACHE_SIZE 2048

#include "shaders/ray_interop.h"

typedef struct vk_ray_model_s {
	VkAccelerationStructureKHR as;
	VkAccelerationStructureGeometryKHR *geoms;
	int max_prims;
	int num_geoms;
	int size;
	uint32_t kusochki_offset;
	qboolean dynamic;
	qboolean taken;

	// TODO remove with the split of Kusok in Model+Material+Kusok
	uint32_t material_mode;
	vec4_t color;
	matrix4x4 prev_transform;

	struct {
		uint32_t as_offset;
	} debug;
} vk_ray_model_t;

typedef struct Kusok vk_kusok_data_t;

typedef struct {
	matrix3x4 transform_row;
	vk_ray_model_t *model;
	uint32_t material_mode; // MATERIAL_MODE_ from ray_interop.h
} vk_ray_draw_model_t;

typedef struct {
	const char *debug_name;
	VkAccelerationStructureKHR *p_accel;
	const VkAccelerationStructureGeometryKHR *geoms;
	const uint32_t *max_prim_counts;
	const VkAccelerationStructureBuildRangeInfoKHR *build_ranges;
	uint32_t n_geoms;
	VkAccelerationStructureTypeKHR type;
	qboolean dynamic;
} as_build_args_t;

struct vk_combuf_s;
qboolean createOrUpdateAccelerationStructure(struct vk_combuf_s *combuf, const as_build_args_t *args, vk_ray_model_t *model);

typedef struct {
	// Geometry metadata. Lifetime is similar to geometry lifetime itself.
	// Semantically close to render buffer (describes layout for those objects)
	// TODO unify with render buffer
	// Needs: STORAGE_BUFFER
	vk_buffer_t kusochki_buffer;
	r_debuffer_t kusochki_alloc;

	// Model header
	// Array of struct ModelHeader: color, material_mode, prev_transform
	vk_buffer_t model_headers_buffer;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		int num_models;
		int num_lighttextures;
		vk_ray_draw_model_t models[MAX_ACCELS];
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	vk_ray_model_t models_cache[MODEL_CACHE_SIZE];
} xvk_ray_model_state_t;

extern xvk_ray_model_state_t g_ray_model_state;

void XVK_RayModel_ClearForNextFrame( void );
void XVK_RayModel_Validate(void);

void RT_RayModel_Clear(void);
