#pragma once
#include "vk_materials.h"
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

struct ref_viewpass_s;
void VK_RenderSetupCamera( const struct ref_viewpass_s *rvp );

typedef struct vk_render_geometry_s {
	int index_offset, vertex_offset;

	uint32_t element_count;

	// Maximum index of vertex used for this geometry; needed for ray tracing BLAS building
	uint32_t max_vertex;

	// Non-null only for brush models
	// Used for updating animated textures for brush models
	// Remove: have an explicit list of surfaces with animated textures
	const struct msurface_s *surf_deprecate;

	// If this geometry is special, it will have a material type override
	r_vk_material_t material;

	// Olde unpatched texture used for traditional renderer
	int ye_olde_texture;

	// for kXVkMaterialEmissive{,Glow} and others
	vec3_t emissive;
} vk_render_geometry_t;

typedef enum {
	kVkRenderTypeSolid,     // no blending, depth RW

	// Mix alpha blending with depth test and write
	// Set by:
	// - brush:  kRenderTransColor
	// - studio: kRenderTransColor, kRenderTransTexture, kRenderTransAlpha, kRenderGlow
	// - sprite: kRenderTransColor, kRenderTransTexture
	// - triapi: kRenderTransColor, kRenderTransTexture
	kVkRenderType_A_1mA_RW, // blend: src*a + dst*(1-a), depth: RW

	// Mix alpha blending with depth test only
	// Set by:
	// - brush:  kRenderTransTexture, kRenderGlow
	// - sprite: kRenderTransAlpha
	// - triapi: kRenderTransAlpha
	kVkRenderType_A_1mA_R,  // blend: src*a + dst*(1-a), depth test

	// Additive alpha blending, no depth
	// Set by:
	// - sprite: kRenderGlow
	kVkRenderType_A_1,      // blend: src*a + dst, no depth test or write

	// Additive alpha blending with depth test
	// Set by:
	// - brush: kRenderTransAdd
	// - beams: all modes except kRenderNormal and beams going through triapi
	// - sprite: kRenderTransAdd
	// - triapi: kRenderTransAdd, kRenderGlow
	kVkRenderType_A_1_R,    // blend: src*a + dst, depth test

	// No blend, alpha test, depth test and write
	// Set by:
	// - brush: kRenderTransAlpha
	kVkRenderType_AT,       // no blend, depth RW, alpha test

	// Additive no alpha blend, depth test only
	// Set by:
	// - studio: kRenderTransAdd
	kVkRenderType_1_1_R,    // blend: src + dst, depth test

	kVkRenderType_COUNT
} vk_render_type_e;

typedef enum {
	// MUST be congruent to MATERIAL_MODE_* definitions in shaders/ray_interop.h
	kMaterialMode_Opaque = 0,
	kMaterialMode_AlphaTest = 1,
	kMaterialMode_Translucent = 2,
	kMaterialMode_BlendAdd = 3,
	kMaterialMode_BlendMix = 4,
	kMaterialMode_BlendGlow = 5,

	kMaterialMode_COUNT = 6,
} material_mode_e;

uint32_t R_VkMaterialModeFromRenderType(vk_render_type_e render_type);

struct rt_light_add_polygon_s;
struct rt_model_s;

typedef struct vk_render_model_s {
#define MAX_MODEL_NAME_LENGTH 64
	char debug_name[MAX_MODEL_NAME_LENGTH];

	// TODO per-geometry?
	int lightmap; // <= 0 if no lightmap

	int num_geometries;
	vk_render_geometry_t *geometries;

	struct rt_model_s *rt_model;
} vk_render_model_t;

// Initialize model from scratch
typedef struct {
	const char *name;
	vk_render_geometry_t *geometries;
	int geometries_count;

	// Geometry data can and will be updated
	// Upading geometry locations is not supported though, only vertex/index values
	qboolean dynamic;
} vk_render_model_init_t;
qboolean R_RenderModelCreate( vk_render_model_t *model, vk_render_model_init_t args );
void R_RenderModelDestroy( vk_render_model_t* model );

qboolean R_RenderModelUpdate( const vk_render_model_t *model );
qboolean R_RenderModelUpdateMaterials( const vk_render_model_t *model, const int *geom_indices, int geom_indices_count);

typedef struct {
	vk_render_type_e render_type; // TODO rename legacy
	material_mode_e material_mode;

	// These are "consumed": copied into internal storage and can be pointers to stack vars
	const vec4_t *color;
	const matrix4x4 *transform, *prev_transform;

	struct {
		const r_vk_material_t* material;
		int old_texture;
	} override;
} r_model_draw_t;

void R_RenderModelDraw(const vk_render_model_t *model, r_model_draw_t args);

typedef struct {
	const char *name;
	const struct vk_vertex_s *vertices;
	const uint16_t *indices;
	int vertices_count, indices_count;

	int render_type;
	r_vk_material_t material;
	int ye_olde_texture;
	const vec4_t *emissive;
	const vec4_t *color;
} r_draw_once_t;
void R_RenderDrawOnce(r_draw_once_t args);

void VK_RenderDebugLabelBegin( const char *label );
void VK_RenderDebugLabelEnd( void );

void VK_RenderBegin( qboolean ray_tracing );
void VK_RenderEnd( VkCommandBuffer cmdbuf, qboolean draw );
struct vk_combuf_s;
void VK_RenderEndRTX( struct vk_combuf_s* combuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h );

void VK_Render_FIXME_Barrier( VkCommandBuffer cmdbuf );
