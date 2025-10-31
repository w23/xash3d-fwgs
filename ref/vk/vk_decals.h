#pragma once

#include "xash3d_types.h"

void VK_ClearDecals( void );
void VK_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale );
float *VK_DecalSetupVerts( decal_t *pDecal, msurface_t *surf, int texture, int *outCount );
void VK_DrawSingleDecal( decal_t *pDecal, msurface_t *fa );
void VK_DrawSurfaceDecals( msurface_t *fa, qboolean single, qboolean reverse );
void VK_DecalRemoveAll( int texture );
int VK_CreateDecalList( struct decallist_s *pList );
void VK_ClearAllDecals( void );
void VK_EntityRemoveDecals( struct model_s *mod );
void VK_SetDecalsTransform( const matrix4x4* transform );
