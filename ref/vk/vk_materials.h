#pragma once

#include "xash3d_types.h"

typedef struct {
	int tex_base_color;
	int tex_roughness;
	int tex_metalness;
	int tex_normalmap;
	int tex_emissive;

	vec4_t base_color;
	float roughness;
	float metalness;
	float normal_scale;
	float emissive_scale;

	qboolean set;
} xvk_material_t;

void XVK_ReloadMaterials( void );

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index );
