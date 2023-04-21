#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

// Set UBO state for next VK_RenderScheduleDraw calls
// Why? Xash Ref code is organized in a way where we can't reliably pass this info with
// ScheduleDraw itself, so we need to either set up per-submodule global state, or
// centralize this global state in here
void VK_RenderStateSetMatrixProjection(const matrix4x4 proj, float fov_angle_y);
void VK_RenderStateSetMatrixView(const matrix4x4 view);
void VK_RenderStateSetMatrixModel(const matrix4x4 model);


// Quirk for passing surface type to the renderer
// xash3d does not really have a notion of materials. Instead there are custom code paths
// for different things. There's also render_mode for entities which determine blending mode
// and stuff.
// For ray tracing we do need to assing a material to each rendered surface, so we need to
// figure out what it is given heuristics like render_mode, texture name, surface flags, source entity type, etc.
typedef enum {
	kXVkMaterialRegular = 0,

	// Set for SURF_DRAWSKY surfaces in vk_brush.c.
	// Used: for setting KUSOK_MATERIAL_FLAG_SKYBOX for skybox texture sampling and environment shadows.
	// Remove: pass it as a special texture/material index (e.g. -2).
	kXVkMaterialSky,

	// Set by glow sprites only.
	// Used: glow means no depth test. Allows for slight ray overshoot (KUSOK_MATERIAL_FLAG_FIXME_GLOW). Special exclusive case for sprites. Desired effect: "bloom" from bright light sources.
	// Remove: in favor of "real" pbr hdr, tonemapping and bloop.
	kXVkMaterialEmissiveGlow,

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
struct vk_ray_model_s;

typedef struct vk_render_model_s {
#define MAX_MODEL_NAME_LENGTH 64
	char debug_name[MAX_MODEL_NAME_LENGTH];

	vk_render_type_e render_type;
	vec4_t color;
	int lightmap; // <= 0 if no lightmap

	int num_geometries;
	vk_render_geometry_t *geometries;

	// This model will be one-frame only, its buffers are not preserved between frames
	qboolean dynamic;

	// Non-NULL only for ray tracing
	struct vk_ray_model_s *ray_model;

	// Polylights which need to be added per-frame dynamically
	// Used for non-worldmodel brush models which are not static
	struct rt_light_add_polygon_s *dynamic_polylights;
	int dynamic_polylights_count;

	// previous frame ObjectToWorld (model) matrix
	matrix4x4 prev_transform;
} vk_render_model_t;

qboolean VK_RenderModelInit( vk_render_model_t* model );
void VK_RenderModelDestroy( vk_render_model_t* model );
void VK_RenderModelDraw( const cl_entity_t *ent, vk_render_model_t* model );

void VK_RenderModelDynamicBegin( vk_render_type_e render_type, const vec4_t color, const char *debug_name_fmt, ... );
void VK_RenderModelDynamicAddGeometry( const vk_render_geometry_t *geom );
void VK_RenderModelDynamicCommit( void );

void VK_RenderDebugLabelBegin( const char *label );
void VK_RenderDebugLabelEnd( void );

void VK_RenderBegin( qboolean ray_tracing );
void VK_RenderEnd( VkCommandBuffer cmdbuf );
struct vk_combuf_s;
void VK_RenderEndRTX( struct vk_combuf_s* combuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h );

void VK_Render_FIXME_Barrier( VkCommandBuffer cmdbuf );
