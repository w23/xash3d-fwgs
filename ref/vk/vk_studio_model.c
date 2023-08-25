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

	for (int i = 0; i < g_studio_cache.models_count; ++i) {
		r_studio_model_info_t *info = &g_studio_cache.models[i].info;
		if (info->submodels)
			Mem_Free(info->submodels);
	}
	g_studio_cache.models_count = 0;
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

static struct {
	vec4_t first_q[MAXSTUDIOBONES];
	float first_pos[MAXSTUDIOBONES][3];

	vec4_t q[MAXSTUDIOBONES];
	float pos[MAXSTUDIOBONES][3];
} gb;

static void studioModelCalcBones(int numbones, const mstudiobone_t *pbone, const mstudioanim_t *panim, int frame, float out_pos[][3], vec4_t *out_q) {
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

static qboolean isBoneSame(int b) {
	if (!Vector4CompareEpsilon(gb.first_q[b], gb.q[b], 1e-4f))
		return false;

	if (!VectorCompareEpsilon(gb.first_pos[b], gb.pos[b], 1e-4f))
		return false;

	return true;
}

static void studioModelProcessBonesAnimations(const model_t *const model, const studiohdr_t *const hdr, r_studio_submodel_info_t *submodels, int submodels_count) {
	for (int i = 0; i < hdr->numseq; ++i) {
		const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + i;

		const mstudiobone_t* const pbone = (mstudiobone_t *)((byte *)hdr + hdr->boneindex);
		const mstudioanim_t* const panim = gEngine.R_StudioGetAnim( (studiohdr_t*)hdr, (model_t*)model, (mstudioseqdesc_t*)pseqdesc );

		// Compute the first frame bones to compare with
		studioModelCalcBones(hdr->numbones, pbone, panim, 0, gb.first_pos, gb.first_q);

		// Compute bones for each frame
		for (int frame = 1; frame < pseqdesc->numframes; ++frame) {
			studioModelCalcBones(hdr->numbones, pbone, panim, frame, gb.pos, gb.q);

			// Compate bones for each submodel
			for (int si = 0; si < submodels_count; ++si) {
				r_studio_submodel_info_t *const subinfo = submodels + si;

				// Once detected as dynamic, there's no point in checking further
				if (subinfo->is_dynamic)
					continue;

				const mstudiomodel_t *const submodel = subinfo->submodel_key;
				const qboolean use_boneweights = FBitSet(hdr->flags, STUDIO_HAS_BONEWEIGHTS) && submodel->blendvertinfoindex != 0 && submodel->blendnorminfoindex != 0;

				if (use_boneweights) {
					const mstudioboneweight_t *const pvertweight = (mstudioboneweight_t *)((byte *)hdr + submodel->blendvertinfoindex);
					for(int vi = 0; vi < submodel->numverts; vi++) {
						for (int bi = 0; bi < 4; ++bi) {
							const int8_t bone = pvertweight[vi].bone[bi];
							if (bone == -1)
								break;

							subinfo->is_dynamic |= !isBoneSame(bone);
							if (subinfo->is_dynamic)
								break;
						}
						if (subinfo->is_dynamic)
							break;
					} // for submodel verts

				} /* use_boneweights */ else {
					const byte *const pvertbone = ((const byte *)hdr + submodel->vertinfoindex);
					for(int vi = 0; vi < submodel->numverts; vi++) {
							subinfo->is_dynamic |= !isBoneSame(pvertbone[vi]);
							if (subinfo->is_dynamic)
								break;
					}
				} // no use_boneweights
			} // for all submodels
		} // for all frames
	} // for all sequences
}

// Get submodels count and/or fill submodels array
static int studioModelGetSubmodels(const studiohdr_t *hdr, r_studio_submodel_info_t *out_submodels) {
	int count = 0;
	for (int i = 0; i < hdr->numbodyparts; ++i) {
		const mstudiobodyparts_t* const bodypart = (mstudiobodyparts_t *)((byte *)hdr + hdr->bodypartindex) + i;
		if (out_submodels) {
			gEngine.Con_Reportf(" Bodypart %d/%d: %s (nummodels=%d)\n", i, hdr->numbodyparts - 1, bodypart->name, bodypart->nummodels);
			for (int j = 0; j < bodypart->nummodels; ++j) {
				const mstudiomodel_t * const submodel = (mstudiomodel_t *)((byte *)hdr + bodypart->modelindex) + j;
				gEngine.Con_Reportf("  Submodel %d: %s\n", j, submodel->name);
				out_submodels[count++].submodel_key = submodel;
			}
		} else {
			count += bodypart->nummodels;
		}
	}
	return count;
}

qboolean R_StudioModelPreload(model_t *mod) {
	const studiohdr_t *const hdr = (const studiohdr_t *)gEngine.Mod_Extradata(mod_studio, mod);

	ASSERT(g_studio_cache.models_count < MAX_STUDIO_MODELS);

	r_studio_model_info_entry_t *entry = &g_studio_cache.models[g_studio_cache.models_count++];
	entry->studio_header_key = hdr;

	gEngine.Con_Reportf("Studio model %s, sequences = %d:\n", hdr->name, hdr->numseq);
	for (int i = 0; i < hdr->numseq; ++i) {
		const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + i;
		gEngine.Con_Reportf("  %d: fps=%f numframes=%d\n", i, pseqdesc->fps, pseqdesc->numframes);
	}

	// Get submodel array
	const int submodels_count = studioModelGetSubmodels(hdr, NULL);
	r_studio_submodel_info_t *submodels = Mem_Calloc(vk_core.pool, sizeof(*submodels) * submodels_count);
	studioModelGetSubmodels(hdr, submodels);

	studioModelProcessBonesAnimations(mod, hdr, submodels, submodels_count);

	qboolean is_dynamic = false;
	gEngine.Con_Reportf(" submodels_count: %d\n", submodels_count);
	for (int i = 0; i < submodels_count; ++i) {
		const r_studio_submodel_info_t *const subinfo = submodels + i;
		is_dynamic |= subinfo->is_dynamic;
		gEngine.Con_Reportf("  Submodel %d/%d: name=\"%s\", is_dynamic=%d\n", i, submodels_count-1, subinfo->submodel_key->name, subinfo->is_dynamic);
	}

	entry->info.submodels_count = submodels_count;
	entry->info.submodels = submodels;

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
