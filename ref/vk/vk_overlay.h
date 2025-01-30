#pragma once

#include "vk_core.h"
#include "xash3d_types.h"

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
void R_DrawTileClear( int texnum, int x, int y, int w, int h );
void CL_FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a );

qboolean R_VkOverlay_Init( void );
void R_VkOverlay_Shutdown( void );

void R_VkOverlay_DrawAndFlip( VkCommandBuffer cmdbuf, qboolean draw );
