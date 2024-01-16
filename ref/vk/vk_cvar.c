#include "vk_cvar.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_logs.h"
#include "vk_swapchain.h"

#define NONEXTERN_CVAR(cvar) cvar_t *cvar;
DECLARE_CVAR(NONEXTERN_CVAR)
#undef NONEXTERN_CVAR

DEFINE_ENGINE_SHARED_CVAR_LIST()

static void setDebugLog( void ) {
	const int argc = gEngine.Cmd_Argc();
	const char *const modules = argc > 1 ? gEngine.Cmd_Argv(1) : "";
	gEngine.Cvar_Set("vk_debug_log_", modules);
	R_LogSetVerboseModules( modules );
}

void VK_LoadCvars( void )
{
#define gEngfuncs gEngine // ...
	RETRIEVE_ENGINE_SHARED_CVAR_LIST()

	r_lighting_modulate = gEngine.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	cl_lightstyle_lerping = gEngine.pfnGetCvarPointer( "cl_lightstyle_lerping", 0 );
	r_lightmap = gEngine.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_infotool = gEngine.Cvar_Get( "r_infotool", "0", FCVAR_CHEAT, "DEBUG: print entity info under crosshair" );
	rt_force_disable = gEngine.Cvar_Get( "rt_force_disable", "0", FCVAR_GLCONFIG, "Force disable Ray Tracing" );
	vk_device_target_id = gEngine.Cvar_Get( "vk_device_target_id", "", FCVAR_GLCONFIG, "Selected video device id" );

	vk_hdr_output = gEngine.Cvar_Get( "vk_hdr_output", "0", FCVAR_GLCONFIG, "" ); // TODO: refactoring


	vk_debug_log = gEngine.Cvar_Get("vk_debug_log_", "", FCVAR_GLCONFIG | FCVAR_READ_ONLY, "");

	gEngine.Cmd_AddCommand("vk_debug_log", setDebugLog, "Set modules to enable debug logs for");
}

void VK_LoadCvarsAfterInit( void )
{
	rt_capable = gEngine.Cvar_Get( "rt_capable", vk_core.rtx ? "1" : "0", FCVAR_READ_ONLY, "" );
	vk_hdr_output_capable = gEngine.Cvar_Get( "vk_hdr_output_capable", vk_core.hdr_output ? "1" : "0", FCVAR_READ_ONLY, "" );
	if (!vk_core.hdr_output) {
		vk_hdr_output = gEngine.Cvar_Get( "vk_hdr_output", "0", FCVAR_READ_ONLY, "DISABLED: not supported by your hardware/software/drivers" );
	}
	ClearBits( vk_hdr_output->flags, FCVAR_CHANGED );
	//vk_hdr_output_max_brightness_ui = gEngine.Cvar_Get( "vk_hdr_output_max_brightness_ui", "100", FCVAR_GLCONFIG, "Brightness output HDR for UI (RESTART REQUIRED)" );
	//vk_hdr_output_max_brightness = gEngine.Cvar_Get( "vk_hdr_output_max_brightness", "400", FCVAR_GLCONFIG, "Brightness output HDR ingame" );
	// FIXME: remove this
	gEngine.Cmd_AddCommand( "vk_debug_switch_swapchain_format", switchSwapchain, "EXPERIMENTAL: Apply changed swapchain format ^1(UNSTABLE)^3" );


	if (vk_core.rtx) {
		rt_enable = gEngine.Cvar_Get( "rt_enable", "1", FCVAR_GLCONFIG, "Enable or disable Ray Tracing mode" );
		rt_bounces = gEngine.Cvar_Get( "rt_bounces", "3", FCVAR_GLCONFIG, "Path tracing ray bounces" );
	} else {
		rt_enable = gEngine.Cvar_Get( "rt_enable", "0", FCVAR_READ_ONLY, "DISABLED: Ray tracing is not supported by your hardware/drivers" );
	}
}
