#pragma once

#include "vk_common.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;

void VK_StudioInit( void );
void VK_StudioShutdown( void );

void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );

void VK_StudioDrawModel( cl_entity_t *ent, int render_mode, float blend );

void R_RunViewmodelEvents( void );
void R_DrawViewModel( void );

void CL_InitStudioAPI( void );

float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time );
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles );

struct r_studio_model_info_s;
const struct r_studio_model_info_s *R_StudioModelPreload(model_t *mod);

void R_StudioCacheClear( void );

void R_StudioResetPlayerModels( void );
