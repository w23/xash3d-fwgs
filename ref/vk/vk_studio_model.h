#pragma once

#include "vk_render.h"
#include "vk_geometry.h"

struct r_studio_submodel_info_s;
typedef struct r_studio_submodel_render_s {
	vk_render_model_t model;
	r_geometry_range_t geometry_range;
	vk_render_geometry_t *geometries;

	// TODO figure out how to precompute this and store it in info
	int geometries_count;
	int vertex_count, index_count;

	// TODO vec3_t prev_verts;

	struct {
		struct r_studio_submodel_info_s *info;
		struct r_studio_submodel_render_s *next;
	} _;
} r_studio_submodel_render_t;

typedef struct r_studio_submodel_info_s {
	const mstudiomodel_t *submodel_key;
	qboolean is_dynamic;

	// TODO int verts_count; for prev_verts

	r_studio_submodel_render_t *cached_head;
} r_studio_submodel_info_t;

r_studio_submodel_render_t *studioSubmodelRenderModelAcquire(r_studio_submodel_info_t *info);
void studioSubmodelRenderModelRelease(r_studio_submodel_render_t *render_submodel);

typedef struct {
	int submodels_count;
	r_studio_submodel_info_t *submodels;
} r_studio_model_info_t;

void VK_StudioModelInit(void);

// Entity model cache/pool
typedef struct {
	const studiohdr_t *studio_header;
	const r_studio_model_info_t *model_info;

	// ??? probably unnecessary matrix3x4 transform;
	matrix3x4 prev_transform;

	int bodyparts_count;
	r_studio_submodel_render_t **bodyparts;
} r_studio_entity_model_t;

//r_studio_entity_model_t *studioEntityModelGet(const cl_entity_t *ent);

// TOOD manual cleanup function? free unused?

//void studioEntityModelClear(void);

void studioRenderSubmodelDestroy( r_studio_submodel_render_t *submodel );

r_studio_model_info_t *getStudioModelInfo(model_t *model);
