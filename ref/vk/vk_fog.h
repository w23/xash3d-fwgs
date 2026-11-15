#pragma once

#include "xash3d_types.h"

// Same fog parameters as ref/gl's ref_instance_t.
typedef struct {
	qboolean fogCustom;
	qboolean fogEnabled;
	qboolean fogSkybox;
	vec4_t fogColor;
	float fogDensity;
	float fogStart;
	float fogEnd;
	int cached_contents;
	int cached_waterlevel;
} vk_fog_t;

extern vk_fog_t g_fog;

struct ref_viewpass_s;
void R_CheckFog( const struct ref_viewpass_s *rvp );
void R_GetFogParams( vec4_t color, vec4_t params, qboolean skybox );
void R_GetSceneFogParams( vec4_t color, vec4_t params, qboolean skybox );
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn );
void TriFogParams( float flDensity, int iFogSkybox );
