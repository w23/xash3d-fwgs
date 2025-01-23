#pragma once

#include "xash3d_types.h"

typedef struct model_s model_t;

void TriRenderMode( int mode );
void TriSetTexture( int texture_index );
int TriSpriteTexture( model_t *pSpriteModel, int frame );

void TriBegin( int mode );

void TriTexCoord2f( float u, float v );
void TriColor4f( float r, float g, float b, float a );
void TriColor4ub_( byte r, byte g, byte b, byte a ); // FIXME consolidate with vk_renderstate

void TriNormal3fv( const float *v );
void TriNormal3f( float x, float y, float z );

// Emits next vertex
void TriVertex3fv( const float *v );
void TriVertex3f( float x, float y, float z );

void TriEnd( void );
void TriEndEx( const vec4_t color, const char* name );
