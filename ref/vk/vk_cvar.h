#pragma once

#include "cvardef.h"

#include "xash3d_types.h" // required for ref_api.h
#include "const.h" // required for ref_api.h
#include "com_model.h" // required for ref_api.h
#include "ref_api.h"

// from engine/common/cvar.h
#define FCVAR_READ_ONLY		(1<<17)	// cannot be set by user at all, and can't be requested by CvarGetPointer from game dlls

#define CVAR_TO_BOOL( x )		((x) && ((x)->value != 0.0f) ? true : false )

void VK_LoadCvars( void );
void VK_LoadCvarsAfterInit( void );

#define DECLARE_CVAR(X) \
	X(cl_lightstyle_lerping) \
	X(r_lighting_modulate) \
	X(r_lightmap) \
	X(r_infotool) \
	X(vk_device_target_id) \
	X(vk_debug_log) \
	X(rt_capable) \
	X(rt_enable) \
	X(rt_bounces) \

#define EXTERN_CVAR(cvar) extern cvar_t *cvar;
DECLARE_CVAR(EXTERN_CVAR)
#undef EXTERN_CVAR

DECLARE_ENGINE_SHARED_CVAR_LIST()
