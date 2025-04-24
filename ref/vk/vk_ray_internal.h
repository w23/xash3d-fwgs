#pragma once

#include "vk_rtx.h"

#define MAX_INSTANCES 2048
#define MODEL_CACHE_SIZE 2048

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
