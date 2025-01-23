#pragma once

#include "vk_buffer.h"
#include "vk_rtx.h"

#define MAX_INSTANCES 2048
#define MAX_KUSOCHKI 32768
#define MODEL_CACHE_SIZE 2048

#include "shaders/ray_interop.h"

typedef struct Kusok vk_kusok_data_t;

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
} rt_blas_create_t;

// Creates BLAS and schedules it to be built next frame
struct rt_blas_s* RT_BlasCreate(rt_blas_create_t args);

void RT_BlasDestroy(struct rt_blas_s* blas);

// Update dynamic BLAS, schedule it for build/update
qboolean RT_BlasUpdate(struct rt_blas_s *blas, const struct vk_render_geometry_s *geoms, int geoms_count);

qboolean RT_DynamicModelInit(void);
void RT_DynamicModelShutdown(void);

void RT_DynamicModelProcessFrame(void);
