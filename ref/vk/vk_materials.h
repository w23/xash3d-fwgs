#pragma once

#include "xash3d_types.h"

/* TODO
#define MATERIAL_FIELDS_LIST(X) \
	X(0, int, tex_base_color, basecolor_map, readTexture) \
	X(1, int, tex_roughness, normal_map, readTexture) \
	X(2, int, tex_metalness, metal_map, readTexture) \
	X(3, int, tex_normalmap, roughness_map, readTexture) \
	X(4, vec4_t, base_color, base_color, readVec4) \
	X(5, float, roughness, roughness, readFloat) \
	X(6, float, metalness, metalness, readFloat) \
	X(7, float, normal_scale, normal_scale, readFloat) \
	X(7, int, rendermode, rendermode, readRendermode) \
	X(8, int, _inherit, inherit, readInerit) \
*/

typedef struct r_vk_material_s {
	int tex_base_color;
	int tex_roughness;
	int tex_metalness;
	int tex_normalmap;

	vec4_t base_color;
	float roughness;
	float metalness;
	float normal_scale;
} r_vk_material_t;

typedef struct { int index; } r_vk_material_ref_t;

// Note: invalidates all previously issued material refs
// TODO: track "version" in high bits?
void R_VkMaterialsReload( void );

void R_VkMaterialsShutdown( void );

struct model_s;
void R_VkMaterialsLoadForModel( const struct model_s* mod );

r_vk_material_ref_t R_VkMaterialGetForName( const char *name );
r_vk_material_t R_VkMaterialGetForRef( r_vk_material_ref_t ref );

r_vk_material_t R_VkMaterialGetForTexture( int tex_id );

enum {
	kVkMaterialFlagNone = 0,
	// Studio model STUDIO_NF_CHROME
	kVkMaterialFlagChrome = (1<<0),
};

r_vk_material_t R_VkMaterialGetForTextureWithFlags( int tex_id, uint32_t flags );

qboolean R_VkMaterialGetEx( int tex_id, int rendermode, r_vk_material_t *out_material );
