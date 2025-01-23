#pragma once

#include "vk_render.h"
#include "vk_geometry.h"

struct r_studio_submodel_info_s;

// Submodel render data that is enough to render given submodel
// Included render model (that also incapsulates BLAS)
// This can be static (built once), or dynamic (updated frequently)
// Lives in per-model-info submodel cache
typedef struct r_studio_submodel_render_s {
	vk_render_model_t model;
	r_geometry_range_t geometry_range;
	vk_render_geometry_t *geometries;

	// TODO figure out how to precompute this and store it in info
	int geometries_count;
	int vertex_count, index_count;

	vec3_t *prev_verts;

	struct {
		struct r_studio_submodel_info_s *info;
		struct r_studio_submodel_render_s *next;
	} _;
} r_studio_submodel_render_t;

// Submodel metadata and render-model cache
typedef struct r_studio_submodel_info_s {
	const mstudiomodel_t *submodel_key;
	qboolean is_dynamic;
	//qboolean has_bonecontroller;

	// TODO int verts_count; for prev_verts

	r_studio_submodel_render_t *cached_head;

	// Mostly for debug: how many cached render models were acquired and not given back
	int render_refcount;
} r_studio_submodel_info_t;

// Submodel cache functions, used in vk_studio.c
r_studio_submodel_render_t *studioSubmodelRenderModelAcquire(r_studio_submodel_info_t *info);
void studioSubmodelRenderModelRelease(r_studio_submodel_render_t *render_submodel);

typedef struct {
	int submodels_count;
	r_studio_submodel_info_t *submodels;
} r_studio_model_info_t;

const r_studio_model_info_t *getStudioModelInfo(model_t *model);

// Entity model cache/pool
typedef struct {
	const studiohdr_t *studio_header;
	const r_studio_model_info_t *model_info;

	// TODO 3x4
	matrix4x4 transform;
	matrix4x4 prev_transform;

	int bodyparts_count;
	r_studio_submodel_render_t **bodyparts;
} r_studio_entity_model_t;

void VK_StudioModelInit(void);
//void VK_StudioModelShutdown(void);
