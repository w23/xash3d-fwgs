#pragma once

#include "vk_core.h"
#include "vk_module.h"

#include "vk_buffer.h"
#include "vk_const.h"
#include "vk_rtx.h"

#define MAX_INSTANCES 2048
#define MAX_KUSOCHKI 32768
#define MODEL_CACHE_SIZE 2048

#include "shaders/ray_interop.h"

extern RVkModule g_module_ray_model;

typedef struct Kusok vk_kusok_data_t;

typedef struct rt_draw_instance_s {
	VkDeviceAddress blas_addr;
	uint32_t kusochki_offset;
	matrix3x4 transform_row;
	matrix4x4 prev_transform_row;
	vec4_t color;
	uint32_t material_mode; // MATERIAL_MODE_ from ray_interop.h
	uint32_t material_flags; // material_flag_bits_e
} rt_draw_instance_t;

typedef struct {
	const char *debug_name;
	VkAccelerationStructureKHR *p_accel;
	const VkAccelerationStructureGeometryKHR *geoms;
	const uint32_t *max_prim_counts;
	const VkAccelerationStructureBuildRangeInfoKHR *build_ranges;
	uint32_t n_geoms;
	VkAccelerationStructureTypeKHR type;
	qboolean dynamic;

	VkDeviceAddress *out_accel_addr;
	uint32_t *inout_size;
} as_build_args_t;

struct vk_combuf_s;
qboolean createOrUpdateAccelerationStructure(struct vk_combuf_s *combuf, const as_build_args_t *args);

#define MAX_SCRATCH_BUFFER (32*1024*1024)
#define MAX_ACCELS_BUFFER (128*1024*1024)

typedef struct {
	// Geometry metadata. Lifetime is similar to geometry lifetime itself.
	// Semantically close to render buffer (describes layout for those objects)
	// TODO unify with render buffer?
	// Needs: STORAGE_BUFFER
	vk_buffer_t kusochki_buffer;
	r_debuffer_t kusochki_alloc;
	// TODO when fully rt_model: r_blocks_t alloc;

	// Model header
	// Array of struct ModelHeader: color, material_mode, prev_transform
	vk_buffer_t model_headers_buffer;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		rt_draw_instance_t instances[MAX_INSTANCES];
		int instances_count;

		uint32_t scratch_offset; // for building dynamic blases
	} frame;
} xvk_ray_model_state_t;

extern xvk_ray_model_state_t g_ray_model_state;

void XVK_RayModel_ClearForNextFrame( void );
void XVK_RayModel_Validate(void);

void RT_RayModel_Clear(void);

// Just creates an empty BLAS structure, doesn't alloc anything
// Memory pointed to by name must remain alive until RT_BlasDestroy
struct rt_blas_s* RT_BlasCreate(const char *name, rt_blas_usage_e usage);

// Preallocate BLAS with given estimates
typedef struct {
	int max_geometries;
	int max_prims_per_geometry;
	int max_vertex_per_geometry;
} rt_blas_preallocate_t;
qboolean RT_BlasPreallocate(struct rt_blas_s* blas, rt_blas_preallocate_t args);

void RT_BlasDestroy(struct rt_blas_s* blas);

// 1. Schedules BLAS build (allocates geoms+ranges from a temp pool, etc).
// 2. Allocates kusochki (if not) and fills them with geom and initial material data
qboolean RT_BlasBuild(struct rt_blas_s *blas, const struct vk_render_geometry_s *geoms, int geoms_count);

VkDeviceAddress RT_BlasGetDeviceAddress(struct rt_blas_s *blas);

typedef struct rt_kusochki_s {
	uint32_t offset;
	int count;
	int internal_index__;
} rt_kusochki_t;

rt_kusochki_t RT_KusochkiAllocLong(int count);
uint32_t RT_KusochkiAllocOnce(int count);
void RT_KusochkiFree(const rt_kusochki_t*);

//struct vk_render_geometry_s;
//qboolean RT_KusochkiUpload(uint32_t kusochki_offset, const struct vk_render_geometry_s *geoms, int geoms_count, int override_texture_id, const vec4_t *override_color);

void RT_DynamicModelProcessFrame(void);
