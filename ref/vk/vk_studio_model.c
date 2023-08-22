#include "vk_studio_model.h"
#include "r_speeds.h"
#include "vk_entity_data.h"

#define MODULE_NAME "studio"

typedef struct {
	const studiohdr_t *studio_header_key;
	r_studio_model_info_t info;
} r_studio_model_info_entry_t;

/*
typedef struct {
	// BOTH must match
	const cl_entity_t *key_entity;
	const model_t *key_model;

	r_studio_entity_model_t entmodel;
} r_studio_entity_model_entry_t;
*/

static struct {
/*
#define MAX_CACHED_STUDIO_SUBMODELS 1024
	// TODO proper map/hash table
	r_studio_model_cache_entry_t entries[MAX_CACHED_STUDIO_SUBMODELS];
	int entries_count;
*/

#define MAX_STUDIO_MODELS 256
	r_studio_model_info_entry_t models[MAX_STUDIO_MODELS];
	int models_count;

	// TODO hash table
	//r_studio_entity_model_entry_t *entmodels;
	//int entmodels_count;
} g_studio_cache;

void studioRenderSubmodelDestroy( r_studio_render_submodel_t *submodel ) {
	R_RenderModelDestroy(&submodel->model);
	R_GeometryRangeFree(&submodel->geometry_range);
	Mem_Free(submodel->geometries);
	submodel->geometries = NULL;
	submodel->geometries_count = 0;
	submodel->vertex_count = 0;
	submodel->index_count = 0;
}

void R_StudioCacheClear( void ) {
/*
	for (int i = 0; i < g_studio_cache.entries_count; ++i) {
		r_studio_model_cache_entry_t *const entry = g_studio_cache.entries + i;
		ASSERT(entry->key_submodel);
		entry->key_submodel = 0;
		entry->key_entity = NULL;

		studioRenderSubmodelDestroy(&entry->render);
	}
	g_studio_cache.entries_count = 0;
*/

	g_studio_cache.models_count = 0;
}

qboolean isStudioModelDynamic(const studiohdr_t *hdr) {
	gEngine.Con_Reportf("Studio model %s, sequences = %d:\n", hdr->name, hdr->numseq);
	if (hdr->numseq == 0)
		return false;

	for (int i = 0; i < hdr->numseq; ++i) {
		const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + i;
		gEngine.Con_Reportf("  %d: fps=%f numframes=%d\n", i, pseqdesc->fps, pseqdesc->numframes);

		// TODO read the sequence and verify that the animation data does in fact animate
		// There are known cases where all the data is the same and no animation happens
	}

	// This is rather conservative.
	// TODO We might be able to cache:
	// - individual sequences w/o animation frames
	// - individual submodels that are not affected by any sequences or animations
	// - animation sequences without real animations (all frames are the same)
	// - etc
	const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + 0;
	const qboolean is_dynamic = hdr->numseq > 1 || (pseqdesc->fps > 0 && pseqdesc->numframes > 1);
	gEngine.Con_Reportf("Studio model %s is %s\n", hdr->name, is_dynamic ? "dynamic" : "static");
	return is_dynamic;
}

qboolean R_StudioModelPreload(model_t *mod) {
	const studiohdr_t *const hdr = (const studiohdr_t *)gEngine.Mod_Extradata(mod_studio, mod);

	ASSERT(g_studio_cache.models_count < MAX_STUDIO_MODELS);

	g_studio_cache.models[g_studio_cache.models_count++] = (r_studio_model_info_entry_t){
		.studio_header_key = hdr,
		.info = {
			.is_static = !isStudioModelDynamic(hdr),
		}
	};

	// TODO if it is static, pregenerate the geometry

	return true;
}

r_studio_model_info_t *getStudioModelInfo(model_t *model) {
	const studiohdr_t *const hdr = (studiohdr_t *)gEngine.Mod_Extradata( mod_studio, model );

	for (int i = 0; i < g_studio_cache.models_count; ++i) {
		r_studio_model_info_entry_t *const entry = g_studio_cache.models + i;
		if (entry->studio_header_key == hdr) {
			return &entry->info;
		}
	}

	return NULL;
}


/*
const r_studio_model_cache_entry_t *findSubModelInCacheForEntity(const mstudiomodel_t *submodel, const cl_entity_t *ent) {
	// FIXME hash table, there are hundreds of entries
	for (int i = 0; i < g_studio_cache.entries_count; ++i) {
		const r_studio_model_cache_entry_t *const entry = g_studio_cache.entries + i;
		if (entry->key_submodel == submodel && (entry->key_entity == NULL || entry->key_entity == ent))
			return entry;
	}

	return NULL;
}

r_studio_model_cache_entry_t *studioSubModelCacheAlloc(void) {
	if (g_studio_cache.entries_count == MAX_CACHED_STUDIO_SUBMODELS) {
		PRINT_NOT_IMPLEMENTED_ARGS("Studio submodel cache overflow at %d", MAX_CACHED_STUDIO_SUBMODELS);
		return NULL;
	}

	r_studio_model_cache_entry_t *const entry = g_studio_cache.entries + g_studio_cache.entries_count;
	++g_studio_cache.entries_count;

	return entry;
}
*/

// TODO ? void studioSubModelCacheFree(r_studio_model_cache_entry_t*);

void VK_StudioModelInit(void) {
	// ... R_SPEEDS_METRIC(g_studio_cache.entries_count, "cached_submodels", kSpeedsMetricCount);
}
