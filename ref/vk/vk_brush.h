#pragma once

#include "xash3d_types.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;
struct cl_entity_s;
struct texture_s;
struct msurface_s;

qboolean R_BrushInit( void );
void R_BrushShutdown( void );

qboolean R_BrushModelLoad(struct model_s *mod, qboolean is_worldmodel);
void R_BrushModelDestroyAll( void );

void R_BrushModelDraw( const struct cl_entity_s *ent, int render_mode, float blend, const matrix4x4 transform );

const struct texture_s *R_TextureAnimation( const struct cl_entity_s *ent, const struct msurface_s *s );

void R_BrushUnloadTextures( struct model_s *mod );
