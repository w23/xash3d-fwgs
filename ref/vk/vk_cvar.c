#include "vk_cvar.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_logs.h"

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

	r_dlight_virtual_radius = gEngine.Cvar_Get( "r_dlight_virtual_radius", "3", FCVAR_GLCONFIG, "increase dlight radius virtually by this amount" );
	r_lighting_modulate = gEngine.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	cl_lightstyle_lerping = gEngine.pfnGetCvarPointer( "cl_lightstyle_lerping" );
	r_lightmap = gEngine.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_infotool = gEngine.Cvar_Get( "r_infotool", "0", FCVAR_CHEAT, "DEBUG: print entity info under crosshair" );
	rt_force_disable = gEngine.Cvar_Get( "rt_force_disable", "0", FCVAR_GLCONFIG, "Force disable Ray Tracing" );
	vk_device_target_id = gEngine.Cvar_Get( "vk_device_target_id", "", FCVAR_GLCONFIG, "Selected video device id" );

	vk_debug_log = gEngine.Cvar_Get("vk_debug_log_", "", FCVAR_GLCONFIG | FCVAR_READ_ONLY, "");
	R_LogSetVerboseModules( vk_debug_log->string );

	gEngine.Cmd_AddCommand("vk_debug_log", setDebugLog, "Set modules to enable debug logs for");
}

void VK_LoadCvarsAfterInit( void )
{
	rt_capable = gEngine.Cvar_Get( "rt_capable", vk_core.rtx ? "1" : "0", FCVAR_READ_ONLY, "" );

	if (vk_core.rtx) {
		rt_enable = gEngine.Cvar_Get( "rt_enable", "1", FCVAR_GLCONFIG, "Enable or disable Ray Tracing mode" );
		rt_bounces = gEngine.Cvar_Get( "rt_bounces", "3", FCVAR_GLCONFIG, "Path tracing ray bounces" );
		rt_bloom = gEngine.Cvar_Get("rt_bloom", "1", FCVAR_GLCONFIG, "Enable HDR bloom");
		rt_auto_exposure_min = gEngine.Cvar_Get("rt_auto_exposure_min", "0.25", FCVAR_GLCONFIG, "Minimum auto-exposure multiplier (1/16777216 to 16777216)");
		rt_auto_exposure_max = gEngine.Cvar_Get("rt_auto_exposure_max", "4.0", FCVAR_GLCONFIG, "Maximum auto-exposure multiplier (at least rt_auto_exposure_min, up to 16777216)");
		rt_only_diffuse_gi = gEngine.Cvar_Get("rt_only_diffuse_gi", "", FCVAR_GLCONFIG, "Make global illumination only diffuse");
		rt_separated_reflection = gEngine.Cvar_Get("rt_separated_reflection", "", FCVAR_GLCONFIG, "Add separated high quality reflection pass");
		rt_denoise_gi_by_sh = gEngine.Cvar_Get("rt_denoise_gi_by_sh", "1", FCVAR_GLCONFIG, "Denoise global illumination by spherical harmonics");
		rt_disable_gi = gEngine.Cvar_Get("rt_disable_gi", "", FCVAR_GLCONFIG, "Disable global illumination calculation");
		rt_spatial_reconstruction = gEngine.Cvar_Get("rt_spatial_reconstruction", "1", FCVAR_GLCONFIG, "Reconstruct light-cache reflections with spatial reuse");
		vk_gi_sh_blur = gEngine.Cvar_Get("vk_gi_sh_blur", "1", FCVAR_GLCONFIG, "Enable rotated bilateral blur of indirect diffuse SH coefficients");
		rt_gi_sh_temporal = gEngine.Cvar_Get("rt_gi_sh_temporal", "1", FCVAR_GLCONFIG, "Accumulate indirect diffuse SH history before spatial blur");
		rt_gi_poisson = gEngine.Cvar_Get("rt_gi_poisson", "1", FCVAR_GLCONFIG, "Enable Poisson reconstruction of indirect diffuse SH");
		rt_disable_reprojection = gEngine.Cvar_Get("rt_disable_reprojection", "0", FCVAR_GLCONFIG, "Disable SH and reflection temporal filtering; preserve path reservoir reuse");
	} else {
		rt_enable = gEngine.Cvar_Get( "rt_enable", "0", FCVAR_READ_ONLY, "DISABLED: Ray tracing is not supported by your hardware/drivers" );
	}
}
