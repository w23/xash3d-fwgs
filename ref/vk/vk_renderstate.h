#pragma once

#include "xash3d_types.h"

typedef struct { uint8_t r, g, b, a; } color_rgba8_t;

typedef struct render_state_s {
	color_rgba8_t tri_color;
	qboolean fog_allowed;
	qboolean mode_2d;
	vec2_t offset_2d; // screen-space pixels, added to every 2D draw, see R_Set2DOffset
	int blending_mode; // kRenderNormal, ...
} render_state_t;

extern render_state_t vk_renderstate;

void GL_SetRenderMode( int renderMode );
void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
void R_AllowFog( qboolean allow );
void R_Set2DMode( qboolean enable );
void R_Set2DOffset( float x, float y );
