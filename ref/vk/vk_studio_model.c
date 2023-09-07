#include "vk_studio_model.h"
#include "r_speeds.h"
#include "vk_entity_data.h"
#include "vk_logs.h"

#include "xash3d_mathlib.h"

#define MODULE_NAME "studio"
#define LOG_MODULE LogModule_Studio

typedef struct {
	const studiohdr_t *studio_header_key;
	r_studio_model_info_t info;
} r_studio_model_info_entry_t;

static struct {
#define MAX_STUDIO_MODELS 256
	r_studio_model_info_entry_t models[MAX_STUDIO_MODELS];
	int models_count;

	int submodels_cached_dynamic;
	int submodels_cached_static;
} g_studio_cache;

void studioRenderSubmodelDestroy( r_studio_submodel_render_t *submodel ) {
	R_RenderModelDestroy(&submodel->model);
	R_GeometryRangeFree(&submodel->geometry_range);
	if (submodel->geometries)
		Mem_Free(submodel->geometries);
	if (submodel->prev_verts)
		Mem_Free(submodel->prev_verts);
}

static void studioSubmodelInfoDestroy(r_studio_submodel_info_t *subinfo) {
	// Not zero means that something still holds a cached render submodel instance somewhere
	ASSERT(subinfo->render_refcount == 0);

	while (subinfo->cached_head) {
		r_studio_submodel_render_t *render = subinfo->cached_head;
		subinfo->cached_head = subinfo->cached_head->_.next;
		studioRenderSubmodelDestroy(render);
	}
}

void R_StudioCacheClear( void ) {
	for (int i = 0; i < g_studio_cache.models_count; ++i) {
		r_studio_model_info_t *info = &g_studio_cache.models[i].info;

		for (int j = 0; j < info->submodels_count; ++j)
			studioSubmodelInfoDestroy(info->submodels + j);

		if (info->submodels)
			Mem_Free(info->submodels);
	}
	g_studio_cache.models_count = 0;

	g_studio_cache.submodels_cached_dynamic = g_studio_cache.submodels_cached_static = 0;
}

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
			DEBUG(" Bodypart %d/%d: %s (nummodels=%d)", i, hdr->numbodyparts - 1, bodypart->name, bodypart->nummodels);
			for (int j = 0; j < bodypart->nummodels; ++j) {
				const mstudiomodel_t * const submodel = (mstudiomodel_t *)((byte *)hdr + bodypart->modelindex) + j;
				DEBUG("  Submodel %d: %s", j, submodel->name);
				out_submodels[count++].submodel_key = submodel;
			}
		} else {
			count += bodypart->nummodels;
		}
	}
	return count;
}

const r_studio_model_info_t* R_StudioModelPreload(model_t *mod) {
	const studiohdr_t *const hdr = (const studiohdr_t *)gEngine.Mod_Extradata(mod_studio, mod);

	ASSERT(g_studio_cache.models_count < MAX_STUDIO_MODELS);

	r_studio_model_info_entry_t *entry = &g_studio_cache.models[g_studio_cache.models_count++];
	entry->studio_header_key = hdr;

	DEBUG("Studio model %s, sequences = %d:", hdr->name, hdr->numseq);
	for (int i = 0; i < hdr->numseq; ++i) {
		const mstudioseqdesc_t *const pseqdesc = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + i;
		DEBUG("  %d: fps=%f numframes=%d", i, pseqdesc->fps, pseqdesc->numframes);
	}

	// Get submodel array
	const int submodels_count = studioModelGetSubmodels(hdr, NULL);
	r_studio_submodel_info_t *submodels = Mem_Calloc(vk_core.pool, sizeof(*submodels) * submodels_count);
	studioModelGetSubmodels(hdr, submodels);

	studioModelProcessBonesAnimations(mod, hdr, submodels, submodels_count);

	qboolean is_dynamic = false;
	DEBUG(" submodels_count: %d", submodels_count);
	for (int i = 0; i < submodels_count; ++i) {
		const r_studio_submodel_info_t *const subinfo = submodels + i;
		is_dynamic |= subinfo->is_dynamic;
		DEBUG("  Submodel %d/%d: name=\"%s\", is_dynamic=%d", i, submodels_count-1, subinfo->submodel_key->name, subinfo->is_dynamic);
	}

	entry->info.submodels_count = submodels_count;
	entry->info.submodels = submodels;

	return &entry->info;
}

const r_studio_model_info_t *getStudioModelInfo(model_t *model) {
	const studiohdr_t *const hdr = (studiohdr_t *)gEngine.Mod_Extradata( mod_studio, model );

	for (int i = 0; i < g_studio_cache.models_count; ++i) {
		r_studio_model_info_entry_t *const entry = g_studio_cache.models + i;
		if (entry->studio_header_key == hdr) {
			return &entry->info;
		}
	}

	WARN("Studio model \"%s\" wasn't preloaded. How did that happen?", hdr->name);

	return R_StudioModelPreload(model);
}

void VK_StudioModelInit(void) {
	R_SPEEDS_METRIC(g_studio_cache.submodels_cached_static, "submodels_cached_static", kSpeedsMetricCount);
	R_SPEEDS_METRIC(g_studio_cache.submodels_cached_dynamic, "submodels_cached_dynamic", kSpeedsMetricCount);
}

r_studio_submodel_render_t *studioSubmodelRenderModelAcquire(r_studio_submodel_info_t *subinfo) {
	r_studio_submodel_render_t *render = NULL;
	if (subinfo->cached_head) {
		render = subinfo->cached_head;
		if (subinfo->is_dynamic) {
			subinfo->cached_head = render->_.next;
			render->_.next = NULL;
		}
		subinfo->render_refcount++;
		return render;
	}

	render = Mem_Calloc(vk_core.pool, sizeof(*render));
	render->_.info = subinfo;

	if (!subinfo->is_dynamic) {
		subinfo->cached_head = render;
		++g_studio_cache.submodels_cached_static;
	} else {
		++g_studio_cache.submodels_cached_dynamic;
	}

	subinfo->render_refcount++;
	return render;
}

void studioSubmodelRenderModelRelease(r_studio_submodel_render_t *render_submodel) {
	if (!render_submodel)
		return;

	ASSERT(render_submodel->_.info->render_refcount > 0);
	render_submodel->_.info->render_refcount--;

	if (!render_submodel->_.info->is_dynamic)
		return;

	render_submodel->_.next = render_submodel->_.info->cached_head;
	render_submodel->_.info->cached_head = render_submodel;
}
