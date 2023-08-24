#include "vk_studio_model.h"
#include "r_speeds.h"
#include "vk_entity_data.h"

#include "xash3d_mathlib.h"

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

static void studioSubmodelCalcBones(int numbones, const mstudiobone_t *pbone, const mstudioanim_t *panim, int frame, float out_pos[][3], vec4_t *out_q) {
	for(int b = 0; b < numbones; b++ ) {
		// TODO check pbone->bonecontroller, if the bone can be dynamically controlled by entity
		float *const adj = NULL;
		const float interpolation = 0;
		R_StudioCalcBoneQuaternion( frame, interpolation, pbone + b, panim + b, adj, out_q[b] );
		R_StudioCalcBonePosition( frame, interpolation, pbone + b, panim + b, adj, out_pos[b] );
	}
}

qboolean Vector4CompareEpsilon( const vec4_t vec1, const vec4_t vec2, vec_t epsilon )
{
	vec_t	ax, ay, az, aw;

	ax = fabs( vec1[0] - vec2[0] );
	ay = fabs( vec1[1] - vec2[1] );
	az = fabs( vec1[2] - vec2[2] );
	aw = fabs( vec1[3] - vec2[3] );

	if(( ax <= epsilon ) && ( ay <= epsilon ) && ( az <= epsilon ) && ( aw <= epsilon ))
		return true;
	return false;
}

// FIXME:
// - [ ] (CRASH) Cache coherence: entities are reused between frames for different studio models. Catch and handle it. If it was literally reused between sequential frames, then we're screwed, because we can't delete the previous one, as it might be still in use on GPU. Needs refcounts infrastructure.
// - [ ] (POTENTIAL CRASH) g_studio_current.bodypartindex doesn't directly correspond to a single mstudiosubmodel_t. Currently it's tracked as it does. This can break and crash.
// - [ ] Proper submodel cache (by pointer, these pointers should be stable)
/*
 * - submodels[] (global? per-model?)
 *   - mstudiosubmodel_t *key
 *   - qboolean is_static (if true, there's only one render_model_t that is instanced by all users)
 *   - entries[entries_count|entries_capacity]
 *     - r_studio_render_submodel_t render_submodel
 *     - int refcount (is in use by anything? only needed for cache cleaning really, which we can work around by waiting for gpu idle and having hiccups, which is fine as long as this is a really rare situation)
 *     - int used_frames_ago (or last_used_frame)
 */

// Action items:
// 1. Try to extract per-submodel static-ness data:
//   a. Count all submodels (can limit by max submodels constant, only seen ~11 max)
//   b. Have an array of submodels static-ness, set to true.
//   c. While iterating through bones for all sequences, update static-ness if specific bones for that submodel has changed; Do not break early if any other bone has changed.
// 2. Pre-allocate submodels cache, as we now know all submodels. Fill it with is_static data.
// 3. When R_StudioDrawPoints()
//   a. Get the correct studio entmodel from the cache. For prev_transform, etc.
//   b. Get the correct submodel render model from the cache.
//   c. If there's no such submodel, generate the vertices and cache it.
//   d. If there is:
//     1. Static is instanced (a single vk_render_model_t used by everyone with different transform matrices and potentially texture patches). No vertices generation is needed
//     2. Non-static ones need vertices updates each frame.
//   e. Submit the submodel for rendering.

// TODO Potential optimization for the above: (MIGHT STILL NEED THIS FOR DENOISER MOVE VECTORS)
// Keep last used submodel within studio entmodel. Keep last generated verts. Compare new verts with previous ones. If they are the same, do not rebuild the BLAS.

// TODO
// - [ ] (MORE MODELS CAN BE STATIC) Bones are global, calculated only once per frame for the entire studio model. Submodels reference an indexed subset of bones.

// DONE?
// - [x] How to find all studio model submodels regardless of bodyparts. -- NIKAQUE. they all are under bodyparts
//
// - [x] Is crossbow model really static? -- THIS SEEMS OK
/* [2023:08:24|13:37:23] Studio model valve/models/w_crossbow.mdl, sequences = 2: */
/* [2023:08:24|13:37:23]   0: fps=30.000000 numframes=1 */
/* [2023:08:24|13:37:23]   1: fps=30.000000 numframes=7 */
/* [2023:08:24|13:37:23]  Bodypart 0/0: body (nummodels=1) */
/* [2023:08:24|13:37:23]   Submodel 0: w_crossbow */
/* [2023:08:24|13:37:23] Studio model valve/models/w_crossbow.mdl bones are static */
/* [2023:08:24|13:37:23] Studio model valve/models/w_satchel.mdl, sequences = 2: */
/* [2023:08:24|13:37:23]   0: fps=30.000000 numframes=1 */
/* [2023:08:24|13:37:23]   1: fps=30.000000 numframes=101 */
/* [2023:08:24|13:37:23]  Bodypart 0/0: studio (nummodels=1) */
/* [2023:08:24|13:37:23]   Submodel 0: world_satchel */
/* [2023:08:24|13:37:23] Studio model valve/models/w_satchel.mdl bones are static */

static qboolean areStudioBonesDynamic(const model_t *const model, const studiohdr_t *const hdr) {
	qboolean is_static = true;
	for (int i = 0; i < hdr->numseq; ++i) {
		const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + i;

		const mstudiobone_t* const pbone = (mstudiobone_t *)((byte *)hdr + hdr->boneindex);
		const mstudioanim_t* const panim = gEngine.R_StudioGetAnim( (studiohdr_t*)hdr, (model_t*)model, (mstudioseqdesc_t*)pseqdesc );

		vec4_t first_q[MAXSTUDIOBONES];
		float first_pos[MAXSTUDIOBONES][3];

		studioSubmodelCalcBones(hdr->numbones, pbone, panim, 0, first_pos, first_q);

		for (int frame = 1; frame < pseqdesc->numframes; ++frame) {
			vec4_t q[MAXSTUDIOBONES];
			float pos[MAXSTUDIOBONES][3];
			studioSubmodelCalcBones(hdr->numbones, pbone, panim, frame, pos, q);

			for (int b = 0; b < hdr->numbones; ++b) {
				if (!Vector4CompareEpsilon(first_q[b], q[b], 1e-4f)){
					is_static = false;
					break;
				}

				if (!VectorCompareEpsilon(first_pos[b], pos[b], 1e-4f)){
					is_static = false;
					break;
				}
			}

			if (!is_static)
				break;
		}
	}

	gEngine.Con_Reportf("Studio model %s bones are %s\n", hdr->name, is_static ? "static" : "dynamic");
	return !is_static;
}

static qboolean isStudioModelDynamic(const model_t *model, const studiohdr_t *hdr) {
	qboolean is_dynamic = false;

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
	/* const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + 0; */
	/* const qboolean is_dynamic = hdr->numseq > 1 || (pseqdesc->fps > 0 && pseqdesc->numframes > 1); */
	/* gEngine.Con_Reportf("Studio model %s is %s\n", hdr->name, is_dynamic ? "dynamic" : "static"); */

	for (int i = 0; i < hdr->numbodyparts; ++i) {
		const mstudiobodyparts_t* const bodypart = (mstudiobodyparts_t *)((byte *)hdr + hdr->bodypartindex) + i;
		gEngine.Con_Reportf(" Bodypart %d/%d: %s (nummodels=%d)\n", i, hdr->numbodyparts - 1, bodypart->name, bodypart->nummodels);
		for (int j = 0; j < bodypart->nummodels; ++j) {
			const mstudiomodel_t * const submodel = (mstudiomodel_t *)((byte *)hdr + bodypart->modelindex) + j;
			gEngine.Con_Reportf("  Submodel %d: %s\n", j, submodel->name);
		}
	}

	is_dynamic |= areStudioBonesDynamic(model, hdr);

	return is_dynamic;
}

qboolean R_StudioModelPreload(model_t *mod) {
	const studiohdr_t *const hdr = (const studiohdr_t *)gEngine.Mod_Extradata(mod_studio, mod);

	ASSERT(g_studio_cache.models_count < MAX_STUDIO_MODELS);

	g_studio_cache.models[g_studio_cache.models_count++] = (r_studio_model_info_entry_t){
		.studio_header_key = hdr,
		.info = {
			.is_static = !isStudioModelDynamic(mod, hdr),
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
