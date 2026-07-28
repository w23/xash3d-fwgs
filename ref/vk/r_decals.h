#pragma once

#include "xash3d_types.h"

void R_ClearDecals( void );
void R_DecalsFrameBegin( void );
void R_DecalsFlush( void );
void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
float *R_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
void R_DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void R_DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
void R_DecalRemoveAll( int texture );
int R_CreateDecalList( struct decallist_s *pList );
void R_ClearAllDecals( void );
void R_EntityRemoveDecals( struct model_s *mod );
void R_SetDecalsTransform( const matrix4x4* transform );
