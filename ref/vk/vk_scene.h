#pragma once

#include "vk_module.h"
#include "vk_const.h"

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "ref_params.h"

extern RVkModule g_module_scene;

struct ref_viewpass_s;
struct cl_entity_s;

void VK_SceneRender( const struct ref_viewpass_s *rvp );

qboolean R_AddEntity( struct cl_entity_s *clent, int type );
void R_ProcessEntData( qboolean allocate );
void R_ClearScreen( void );
void R_ClearScene( void );
void R_PushScene( void );
void R_PopScene( void );

void R_SceneMapDestroy( void );
void R_NewMap( void );
void R_RenderScene( void );

int R_WorldToScreen( const vec3_t point, vec3_t screen );
int TriWorldToScreen( const float *world, float *screen );

struct beam_s;
void CL_DrawBeams( int fTrans, struct beam_s *active_beams );
void CL_AddCustomBeam( struct cl_entity_s *pEnvBeam );
