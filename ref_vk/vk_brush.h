#pragma once

#include "xash3d_types.h"
#include "vk_render.h"

struct model_s;
struct cl_entity_s;

qboolean VK_BrushInit( void );
void VK_BrushShutdown( void );

qboolean VK_BrushModelLoad( struct model_s *mod, qboolean map);
void VK_BrushModelDestroy( struct model_s *mod );

void VK_BrushModelDraw( const struct cl_entity_s *ent, int render_mode );
void VK_BrushStatsClear( void );
