#pragma once
#include "vk_core.h"

#include "xash3d_types.h"

#define MAX_CONCURRENT_FRAMES 2

// TODO most of the things below should not be global. Instead, they should be passed as an argument/context to all the drawing functions that want this info
typedef struct vk_framectl_s {
	// TODO only used from 2d and r_speeds, remove
	uint32_t width, height;

	// TODO this is not a reliable way to query whether the next frame will be RT
	qboolean rtx_enabled;
} vk_framectl_t;

extern vk_framectl_t vk_frame;

qboolean VK_FrameCtlInit( void );
void VK_FrameCtlShutdown( void );

void R_BeginFrame( qboolean clearScene );
void VK_RenderFrame( const struct ref_viewpass_s *rvp );
void R_EndFrame( void );

qboolean VID_ScreenShot( const char *filename, int shot_type );
