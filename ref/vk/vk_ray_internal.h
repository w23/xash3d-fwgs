#pragma once

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_const.h"
#include "vk_rtx.h"

#define MAX_INSTANCES 2048
#define MAX_KUSOCHKI 32768
#define MODEL_CACHE_SIZE 2048

#include "shaders/ray_interop.h"

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
// FIXME compute this by lazily allocating #define MAX_ACCELS_BUFFER (128*1024*1024)
#define MAX_ACCELS_BUFFER (256*1024*1024)

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

// Memory pointed to by name must remain alive until RT_BlasDestroy
typedef struct {
	const char *name;
	rt_blas_usage_e usage;
	const struct vk_render_geometry_s *geoms;
	int geoms_count;
	qboolean dont_build; // for dynamic models
} rt_blas_create_t;

// Creates BLAS and schedules it to be built next frame
struct rt_blas_s* RT_BlasCreate(rt_blas_create_t args);

void RT_BlasDestroy(struct rt_blas_s* blas);

// Update dynamic BLAS, schedule it for build/update
qboolean RT_BlasUpdate(struct rt_blas_s *blas, const struct vk_render_geometry_s *geoms, int geoms_count);

// TODO blas struct can have its addr field known
VkDeviceAddress RT_BlasGetDeviceAddress(struct rt_blas_s *blas);

qboolean RT_DynamicModelInit(void);
void RT_DynamicModelShutdown(void);

void RT_DynamicModelProcessFrame(void);
