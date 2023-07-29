#pragma once

#include "vk_render.h"
#include "vk_geometry.h"

typedef struct {
	vk_render_model_t model;
	r_geometry_range_t geometry_range;
	vk_render_geometry_t *geometries;
	int geometries_count;
	int vertex_count, index_count;
} r_studio_render_submodel_t;

typedef struct {
	const mstudiomodel_t *key_submodel;

	// Non-NULL for animated instances
	const cl_entity_t *key_entity;

	r_studio_render_submodel_t render;
} r_studio_model_cache_entry_t;

typedef struct {
	vec3_t *prev_verts;
} r_studio_entity_submodel_t;

typedef struct {
	const cl_entity_t *key_entity;
	int model_index;

	//matrix3x4 transform;
	matrix3x4 prev_transform;

	r_studio_entity_submodel_t *submodels;
} r_studio_entity_model_t;

typedef struct {
	const studiohdr_t *key_model;

	// Model contains no animations and can be used directly
	qboolean is_static;

	// Pre-built submodels for static, NULL if not static
	r_studio_render_submodel_t *render_submodels;
} r_studio_cached_model_t;

const r_studio_model_cache_entry_t *findSubModelInCacheForEntity(const mstudiomodel_t *submodel, const cl_entity_t *ent);
r_studio_model_cache_entry_t *studioSubModelCacheAlloc(void);

qboolean isStudioModelDynamic(const studiohdr_t *hdr);

void VK_StudioModelInit(void);
