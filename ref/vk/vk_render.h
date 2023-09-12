#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

struct ref_viewpass_s;
void VK_RenderSetupCamera( const struct ref_viewpass_s *rvp );

// Quirk for passing surface type to the renderer
// xash3d does not really have a notion of materials. Instead there are custom code paths
// for different things. There's also render_mode for entities which determine blending mode
// and stuff.
// For ray tracing we do need to assing a material to each rendered surface, so we need to
// figure out what it is given heuristics like render_mode, texture name, surface flags, source entity type, etc.
typedef enum {
	kXVkMaterialRegular = 0,

	// Set for SURF_DRAWSKY surfaces in vk_brush.c.
	// Used: for setting TEX_BASE_SKYBOX for skybox texture sampling and environment shadows.
	// Remove: pass it as a special texture/material index (e.g. -2).
	kXVkMaterialSky,

	// Set for chrome studio submodels.
	// Used: ray tracing sets gray roughness texture to get smooth surface look.
	// Remove: Have an explicit material for chrome surfaces.
	kXVkMaterialChrome,
} XVkMaterialType;

typedef struct vk_render_geometry_s {
	int index_offset, vertex_offset;

	uint32_t element_count;

	// Maximum index of vertex used for this geometry; needed for ray tracing BLAS building
	uint32_t max_vertex;

	// Non-null only for brush models
	// Used for updating animated textures for brush models
	// Remove: have an explicit list of surfaces with animated textures
	const struct msurface_s *surf_deprecate;

	// Animated textures will be dynamic and change between frames
	int texture;

	// If this geometry is special, it will have a material type override
	XVkMaterialType material;

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

	// This model will be one-frame only, its buffers are not preserved between frames
	// TODO deprecate
	qboolean dynamic;

	// Polylights which need to be added per-frame dynamically
	// Used for non-worldmodel brush models which are not static
	// TODO this doesn't belong here at all
	struct rt_light_add_polygon_s *dynamic_polylights;
	int dynamic_polylights_count;
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
	vk_render_type_e render_type;

	// These are "consumed": copied into internal storage and can be pointers to stack vars
	const vec4_t *color;
	const matrix4x4 *transform, *prev_transform;

	// Global texture override if > 0
	// Used by sprite+quad instancing
	int textures_override;
} r_model_draw_t;

void R_RenderModelDraw(const vk_render_model_t *model, r_model_draw_t args);

typedef struct {
	const char *name;
	const struct vk_vertex_s *vertices;
	const uint16_t *indices;
	int vertices_count, indices_count;

	int render_type;
	int texture;
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
