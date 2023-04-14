#pragma once

#include "xash3d_types.h"
#include "vk_render.h" // cl_entity_t

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;
struct cl_entity_s;

qboolean VK_BrushInit( void );
void VK_BrushShutdown( void );

qboolean VK_BrushModelLoad(struct model_s *mod);
void VK_BrushModelDestroy(struct model_s *mod);

typedef struct {
	const model_t *mod;
	const cl_entity_t *ent;
	int render_mode;
	float blend;
	color24 color;
	//TODO const matrix4x4 *matrix;
} vk_brush_model_draw_t;

void VK_BrushModelDraw( vk_brush_model_draw_t args );
void VK_BrushStatsClear( void );

const texture_t *R_TextureAnimation( const cl_entity_t *ent, const msurface_t *s, const struct texture_s *base_override );

void R_VkBrushModelCollectEmissiveSurfaces( const struct model_s *mod, qboolean is_worldmodel );
