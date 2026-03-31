#include "vk_ray_internal.h"

#include "shaders/ray_interop.h" // MATERIAL_MODE_...

#include "vk_rtx.h"
#include "vk_render.h"
#include "vk_logs.h"
#include "vk_ray_accel.h"
#include "rt_kusochki.h"
#include "std/profiler.h"
#include "std/alolcator.h" // ALO_ALLOC_FAILED

#include "xash3d_mathlib.h"

typedef struct rt_model_s {
	struct rt_blas_s *blas;
	rt_kusochki_t kusochki;
} rt_model_t;

// TODO this material mapping is context dependent. I.e. different entity types might need different ray tracing behaviours for
// same render_mode/type and even texture.
uint32_t R_VkMaterialModeFromRenderType(vk_render_type_e render_type) {
	switch (render_type) {
		case kVkRenderTypeSolid:
			return MATERIAL_MODE_OPAQUE;
			break;
		case kVkRenderType_A_1mA_RW: // blend: scr*a + dst*(1-a), depth: RW
		case kVkRenderType_A_1mA_R:  // blend: scr*a + dst*(1-a), depth test
			// FIXME where is MATERIAL_MODE_TRANSLUCENT??1
			return MATERIAL_MODE_BLEND_MIX;
			break;
		case kVkRenderType_A_1:   // blend: scr*a + dst, no depth test or write; sprite:kRenderGlow only
			return MATERIAL_MODE_BLEND_GLOW;
			break;
		case kVkRenderType_A_1_R: // blend: scr*a + dst, depth test
		case kVkRenderType_1_1_R: // blend: scr + dst, depth test
			return MATERIAL_MODE_BLEND_ADD;
			break;
		case kVkRenderType_AT: // no blend, depth RW, alpha test
			return MATERIAL_MODE_OPAQUE_ALPHA_TEST;
			break;

		case kVkRenderType_Decal: // decals changing diffuse and rmxx of surface material
			return MATERIAL_MODE_DECAL;
			break;

		default:
			gEngine.Host_Error("Unexpected render type %d\n", render_type);
	}

	return MATERIAL_MODE_OPAQUE;
}

void RT_RayModel_Clear(void) {
	RT_KusochkiClear();

	// FIXME
	// This is a dirty workaround for sub-part memory management in this little project
	// Accel backing buffer gets cleared on NewMap. Therefore, we need to recreate BLASes for dynamic
	// models, even though they might have lived for the entire process lifetime.
	// See #729
	RT_DynamicModelShutdown();
	RT_DynamicModelInit();
}

void XVK_RayModel_ClearForNextFrame( void ) {
	RT_KusochkiFlip();
}

struct rt_model_s *RT_ModelCreate(rt_model_create_t args) {
	const rt_kusochki_t kusochki = RT_KusochkiAllocLong(args.geometries_count);
	if (kusochki.count == 0) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate kusochki for %s\n", args.debug_name);
		return NULL;
	}

	struct rt_blas_s *blas = RT_BlasCreate((rt_blas_create_t){
		.name = args.debug_name,
		.usage = args.usage,
		.geoms = args.geometries,
		.geoms_count = args.geometries_count,
	});
	if (!blas) {
		gEngine.Con_Printf(S_ERROR "Cannot create BLAS for %s\n", args.debug_name);
		goto fail;
	}

	// Invokes staging, so this should be after all resource creation
	RT_KusochkiUpload(kusochki.offset, args.geometries, args.geometries_count, NULL, NULL);

	{
		rt_model_t *const ret = Mem_Malloc(vk_core.pool, sizeof(*ret));
		ret->blas = blas;
		ret->kusochki = kusochki;
		return ret;
	}

fail:
	if (blas)
		RT_BlasDestroy(blas);

	if (kusochki.count)
		RT_KusochkiFree(&kusochki);

	return NULL;
}

void RT_ModelDestroy(struct rt_model_s* model) {
	if (!model)
		return;

	if (model->blas)
		RT_BlasDestroy(model->blas);

	if (model->kusochki.count)
		RT_KusochkiFree(&model->kusochki);

	Mem_Free(model);
}

qboolean RT_ModelUpdate(struct rt_model_s *model, const struct vk_render_geometry_s *geometries, int geometries_count) {
	// TODO: It might be beneficial to be able to supply which parts of the RT model should be updated.
	// E.g.:
	// - A flag to update BLAS (not all model updates need BLAS updates, e.g. waveHeight=0 water updates
	// only update UVs)
	// - A flag to update kusochki. Not all updates update offsets and textures, e.g. studio models have
	// stable textures that don't change.

	// Schedule rebuilding blas
	if (!RT_BlasUpdate(model->blas, geometries, geometries_count))
		return false;

	// Also update materials
	RT_KusochkiUpload(model->kusochki.offset, geometries, geometries_count, NULL, NULL);
	return true;
}

qboolean RT_ModelUpdateMaterials(struct rt_model_s *model, const struct vk_render_geometry_s *geometries, int geometries_count, const int *geom_indices, int geom_indices_count) {
	if (!geom_indices_count)
		return true;

	APROF_SCOPE_DECLARE_BEGIN(update_materials, __FUNCTION__);

	int begin = 0;
	for (int i = 1; i < geom_indices_count; ++i) {
		const int geom_index = geom_indices[i];
		ASSERT(geom_index >= 0);
		ASSERT(geom_index < geometries_count);

		if (geom_indices[i - 1] + 1 != geom_index) {
			const int offset = geom_indices[begin];
			const int count = i - begin;
			ASSERT(offset + count <= geometries_count);
			if (!RT_KusochkiUpload(model->kusochki.offset + offset, geometries + offset, count, NULL, NULL)) {
				APROF_SCOPE_END(update_materials);
				return false;
			}

			begin = i;
		}
	}

	{
		const int offset = geom_indices[begin];
		const int count = geom_indices_count - begin;
		ASSERT(offset + count <= geometries_count);
		if (!RT_KusochkiUpload(model->kusochki.offset + offset, geometries + offset, count, NULL, NULL)) {

			APROF_SCOPE_END(update_materials);
			return false;
		}
	}

	APROF_SCOPE_END(update_materials);
	return true;
}

static qboolean isLegacyBlendingMode(int material_mode) {
	switch (material_mode) {
		case MATERIAL_MODE_BLEND_ADD:
		case MATERIAL_MODE_BLEND_MIX:
		case MATERIAL_MODE_BLEND_GLOW:
			return true;
		default:
			return false;
	}
}

static float sRGBtoLinearScalar(const float sRGB) {
	// IEC 61966-2-1:1999
	const float linearLow = sRGB / 12.92f;
	const float linearHigh = powf((sRGB + 0.055f) / 1.055f, 2.4f);
	return sRGB <= 0.04045f ? linearLow : linearHigh;
}

static void sRGBtoLinearVec4(const vec4_t in, vec4_t out) {
	out[0] = sRGBtoLinearScalar(in[0]);
	out[1] = sRGBtoLinearScalar(in[1]);
	out[2] = sRGBtoLinearScalar(in[2]);

	// Historically: sprite animation lerping is linear
	// To-linear conversion should not be done on anything with blending, therefore
	// it's irrelevant really.
	out[3] = in[3];
}

/*
static void sRGBAtoLinearVec4(const vec4_t in, vec4_t out) {
	out[0] = sRGBtoLinearScalar(in[0]);
	out[1] = sRGBtoLinearScalar(in[1]);
	out[2] = sRGBtoLinearScalar(in[2]);

	// α also needs to be linearized for tau-cannon hit position sprite to look okay
	out[3] = sRGBtoLinearScalar(in[3]);
}
*/

void RT_FrameAddModel( struct rt_model_s *model, rt_frame_add_model_t args ) {
	if (!model || !model->blas)
		return;

	uint32_t kusochki_offset = model->kusochki.offset;

	if (args.override.material != NULL) {
		kusochki_offset = RT_KusochkiAllocOnce(args.override.geoms_count);
		if (kusochki_offset == ALO_ALLOC_FAILED)
			return;

		if (!RT_KusochkiUpload(kusochki_offset, args.override.geoms, args.override.geoms_count, args.override.material, NULL)) {
			gEngine.Con_Printf(S_ERROR "Couldn't upload kusochki for instanced model\n");
			return;
		}
	}

	rt_draw_instance_t draw_instance = {
		.blas = model->blas,
		.kusochki_offset = kusochki_offset,
		.material_mode = args.material_mode,
		.material_flags = args.material_flags,
	};

	// Legacy blending is done in sRGB-γ space
	if (isLegacyBlendingMode(args.material_mode))
		Vector4Copy(*args.color_srgb, draw_instance.color);
	else
		sRGBtoLinearVec4(*args.color_srgb, draw_instance.color);

	Matrix3x4_Copy(draw_instance.transform_row, args.transform);
	Matrix4x4_Copy(draw_instance.prev_transform_row, args.prev_transform);

	RT_VkAccelAddDrawInstance(&draw_instance);
}

#define MAX_RT_DYNAMIC_GEOMETRIES 1024
#define MAX_RT_DYNAMIC_GEOMETRIES_VERTICES 256
#define MAX_RT_DYNAMIC_GEOMETRIES_PRIMITIVES 256

typedef struct {
	struct rt_blas_s *blas;
	VkDeviceAddress blas_addr;
	vk_render_geometry_t geometries[MAX_RT_DYNAMIC_GEOMETRIES];
	int geometries_count;
	vec4_t colors[MAX_RT_DYNAMIC_GEOMETRIES];
} rt_dynamic_t;

static const char* group_names[MATERIAL_MODE_COUNT] = {
	"MATERIAL_MODE_OPAQUE",
	"MATERIAL_MODE_OPAQUE_ALPHA_TEST",
	"MATERIAL_MODE_TRANSLUCENT",
	"MATERIAL_MODE_BLEND_ADD",
	"MATERIAL_MODE_BLEND_MIX",
	"MATERIAL_MODE_BLEND_GLOW",
	"MATERIAL_MODE_DECAL",
};

static struct {
	rt_dynamic_t groups[MATERIAL_MODE_COUNT];
} g_dyn;

qboolean RT_DynamicModelInit(void) {
	vk_render_geometry_t *const fake_geoms = Mem_Calloc(vk_core.pool, MAX_RT_DYNAMIC_GEOMETRIES * sizeof(*fake_geoms));
	for (int i = 0; i < MAX_RT_DYNAMIC_GEOMETRIES; ++i) {
		fake_geoms[i].max_vertex = MAX_RT_DYNAMIC_GEOMETRIES_VERTICES;
		fake_geoms[i].element_count = MAX_RT_DYNAMIC_GEOMETRIES_PRIMITIVES * 3;
	}

	for (int i = 0; i < MATERIAL_MODE_COUNT; ++i) {
		struct rt_blas_s *blas = RT_BlasCreate((rt_blas_create_t){
			.name = group_names[i],
			.usage = kBlasBuildDynamicFast,
			.geoms = fake_geoms,
			.geoms_count = MAX_RT_DYNAMIC_GEOMETRIES,
		});

		if (!blas) {
			// FIXME destroy allocated
			gEngine.Con_Printf(S_ERROR "Couldn't create blas for %s\n", group_names[i]);
			return false;
		}

		g_dyn.groups[i].blas = blas;
	}

	Mem_Free(fake_geoms);

	return true;
}

void RT_DynamicModelShutdown(void) {
	for (int i = 0; i < MATERIAL_MODE_COUNT; ++i) {
		RT_BlasDestroy(g_dyn.groups[i].blas);
		g_dyn.groups[i].blas = NULL;
	}
}

void RT_DynamicModelProcessFrame(void) {
	APROF_SCOPE_DECLARE_BEGIN(process, __FUNCTION__);
	for (int i = 0; i < MATERIAL_MODE_COUNT; ++i) {
		rt_dynamic_t *const dyn = g_dyn.groups + i;
		rt_draw_instance_t draw_instance;

		if (!dyn->geometries_count)
			continue;

		const uint32_t kusochki_offset = RT_KusochkiAllocOnce(dyn->geometries_count);
		if (kusochki_offset == ALO_ALLOC_FAILED) {
			gEngine.Con_Printf(S_ERROR "Couldn't allocate kusochki once for %d geoms of %s, skipping\n", dyn->geometries_count, group_names[i]);
			goto tail;
		}

		if (!RT_KusochkiUpload(kusochki_offset, dyn->geometries, dyn->geometries_count, NULL, dyn->colors)) {
			gEngine.Con_Printf(S_ERROR "Couldn't build blas for %d geoms of %s, skipping\n", dyn->geometries_count, group_names[i]);
			goto tail;
		}

		if (!RT_BlasUpdate(dyn->blas, dyn->geometries, dyn->geometries_count)) {
			gEngine.Con_Printf(S_ERROR "Couldn't build blas for %d geoms of %s, skipping\n", dyn->geometries_count, group_names[i]);
			goto tail;
		}

		draw_instance = (rt_draw_instance_t){
			.blas = dyn->blas,
			.kusochki_offset = kusochki_offset,
			.material_mode = i,
			.material_flags = 0,
			.color = {1, 1, 1, 1},
		};

		// xash3d_mathlib is weird, can't just assign these
		// TODO: make my own mathlib of perfectly assignable structs
		Matrix3x4_LoadIdentity(draw_instance.transform_row);
		Matrix4x4_LoadIdentity(draw_instance.prev_transform_row);

		RT_VkAccelAddDrawInstance(&draw_instance);

tail:
		dyn->geometries_count = 0;
	}
	APROF_SCOPE_END(process);
}

void RT_FrameAddOnce( rt_frame_add_once_t args ) {
	// TODO pass material_mode explicitly
	const int material_mode = R_VkMaterialModeFromRenderType(args.render_type);
	rt_dynamic_t *const dyn = g_dyn.groups + material_mode;

	for (int i = 0; i < args.geometries_count; ++i) {
		if (dyn->geometries_count == MAX_RT_DYNAMIC_GEOMETRIES) {
			ERROR_THROTTLED(1, "Too many (>%d) dynamic geometries for mode %s\n", MAX_RT_DYNAMIC_GEOMETRIES, group_names[material_mode]);
			break;
		}

		// Legacy blending is done in sRGB-γ space
		if (isLegacyBlendingMode(material_mode))
			Vector4Copy(*args.color_srgb, dyn->colors[dyn->geometries_count]);
		else
			sRGBtoLinearVec4(*args.color_srgb, dyn->colors[dyn->geometries_count]);

		dyn->geometries[dyn->geometries_count++] = args.geometries[i];
	}
}

