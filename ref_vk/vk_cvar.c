#include "vk_cvar.h"
#include "vk_common.h"
#include "vk_core.h"

#define NONEXTERN_CVAR(cvar) cvar_t *cvar;
DECLARE_CVAR(NONEXTERN_CVAR)
#undef NONEXTERN_CVAR

static cvar_t *r_drawentities;

void VK_LoadCvars( void )
{
	r_lighting_modulate = gEngine.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	cl_lightstyle_lerping = gEngine.pfnGetCvarPointer( "cl_lightstyle_lerping", 0 );
	r_drawentities = gEngine.pfnGetCvarPointer( "r_drawentities", 0 );
	r_lightmap = gEngine.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	ui_infotool = gEngine.Cvar_Get( "ui_infotool", "0", FCVAR_CHEAT, "DEBUG: print entity info under crosshair" );
	vk_only = gEngine.Cvar_Get( "vk_only", "0", FCVAR_GLCONFIG, "Full disable Ray Tracing pipeline" );
	vk_device_target_id = gEngine.Cvar_Get( "vk_device_target_id", "", FCVAR_GLCONFIG, "Selected video device id" );
}
void VK_LoadCvarsAfterInit( void )
{
	vk_rtx_extension = gEngine.Cvar_Get( "vk_rtx_extension", vk_core.rtx ? "1" : "0", FCVAR_READ_ONLY, "" );
	if (vk_core.rtx) {
		vk_rtx = gEngine.Cvar_Get( "vk_rtx", "1", FCVAR_GLCONFIG, "Enable or disable Ray Tracing mode" );
		vk_rtx_bounces = gEngine.Cvar_Get( "vk_rtx_bounces", "3", FCVAR_GLCONFIG, "RTX path tracing ray bounces" );
		vk_rtx_light_begin = gEngine.Cvar_Get( "vk_rtx_light_begin", "0", FCVAR_CHEAT, "DEBUG: disable lights with index lower than this");
		vk_rtx_light_end = gEngine.Cvar_Get( "vk_rtx_light_end", "0", FCVAR_CHEAT, "DEBUG: disable lights with index higher than this ");
	} else {
		vk_rtx = gEngine.Cvar_Get( "vk_rtx", "0", FCVAR_READ_ONLY, "DISABLED: not supported by your hardware/software" );
	}
	vk_hdr_output_extension = gEngine.Cvar_Get( "vk_hdr_output_extension", vk_core.hdr_output ? "1" : "0", FCVAR_READ_ONLY, "" );
	//vk_hdr_output_max_luminance = gEngine.Cvar_Get( "vk_hdr_output_max_luminance", vk_core.hdr_output_max_luminance, FCVAR_READ_ONLY, "" );
	//vk_hdr_output_auto_adjust = gEngine.Cvar_Get( "vk_hdr_output_auto_adjust", "1", FCVAR_GLCONFIG, "" );
	if (vk_core.hdr_output) {
		vk_hdr_output = gEngine.Cvar_Get( "vk_hdr_output", "0", FCVAR_GLCONFIG, "EXPERIMENTAL: Enable or disable High Dynamic Range output (ENABLED HDR IN OS AND RESTART REQUIRED)" );
		vk_hdr_output_manual_rtx_adjust_down = gEngine.Cvar_Get( "vk_hdr_output_manual_rtx_adjust_down", "3", FCVAR_GLCONFIG, "EXPERIMENTAL: Adjust down output HDR level for color, specular (RESTART REQUIRED)" );
		vk_hdr_output_manual_rtx_adjust_additive_down = gEngine.Cvar_Get( "vk_hdr_output_manual_rtx_adjust_additive_down", "1.5", FCVAR_GLCONFIG, "EXPERIMENTAL: Adjust down output HDR level for additive (RESTART REQUIRED)" );
		vk_hdr_output_manual_adjust_ui_down = gEngine.Cvar_Get( "vk_hdr_output_manual_adjust_ui_down", "1.8", FCVAR_GLCONFIG, "EXPERIMENTAL: Adjust down output HDR level for UI (RESTART REQUIRED)" );
		vk_hdr_output_manual_adjust_down = gEngine.Cvar_Get( "vk_hdr_output_manual_adjust_down", "1.6", FCVAR_GLCONFIG, "EXPERIMENTAL: Adjust down output HDR level without RTX (RESTART REQUIRED)" );
	} else {
		vk_hdr_output = gEngine.Cvar_Get( "vk_hdr_output", "0", FCVAR_READ_ONLY, "DISABLED: not supported by your hardware/software" );
	}
}
