#include "vk_rtx.h"

#include "shaders/ray_interop.h" // DEBUG_DISPLAY_...

#include "vulkan/VResource.h"
#include "vk_ray_accel.h"
#include "vulkan/VBuffer.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_light.h"
#include "vk_math.h"
#include "vulkan/VMeatpipe.h"
#include "vk_ray_internal.h"
#include "r_textures.h"
#include "vulkan/VCombuf.h"
#include "vk_logs.h"
#include "rt_kusochki.h"

#include "std/profiler.h"

#include "xash3d_mathlib.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE rt

#define MAX_FRAMES_IN_FLIGHT 2

#define MIN_FRAME_WIDTH 1280
#define MIN_FRAME_HEIGHT 800

static struct {
	struct {
		// Holds UniformBuffer data
		vk_buffer_t buffer;
		uint32_t unit_size;

		vk_resource_buffer_t *resource;
		Producer producer;

		struct UniformBuffer current;
	} uniform;

	// TODO with proper intra-cmdbuf sync we don't really need 2x images
	unsigned frame_number;

	struct vk_meatpipe_s *meatpipe;
	rt_resource_t *meatpipe_out;

	matrix4x4 prev_inv_proj, prev_inv_view;

	qboolean reload_pipeline;
	qboolean discontinuity;

	int max_frame_width, max_frame_height;

	struct AsvgfParams asvgf_params;

	struct {
		cvar_t *rt_debug_display_only;
		uint32_t rt_debug_display_only_value;

		cvar_t *rt_debug_flags;
		uint32_t rt_debug_flags_value;

		cvar_t *rt_debug_fixed_random_seed;
	} debug;
} g_rtx = {0};

#define LIST_ASVGF_REPROJECTION_FLOAT_PARAMS(X) \
	X(history_samples_max, 16.0f) \
	X(history_current_weight_min, 0.1f) \
	X(reprojection_depth_threshold_scale, 0.01f) \
	X(parallax_depth_threshold_scale, 1.35f) \
	X(variance_compatibility_luma_scale, 0.1f) \
	X(variance_compatibility_delta_floor, 0.02f) \
	X(variance_compatibility_signal_floor, 0.02f) \
	X(variance_compatibility_smooth_min, 2.0f) \
	X(variance_compatibility_smooth_max, 6.0f) \
	X(variance_compatibility_floor, 0.0f) \
	X(variance_gate_min, 0.04f) \
	X(variance_gate_max, 0.35f) \
	X(variance_gate_floor, 0.0f) \
	X(variance_soften_threshold, 0.2f) \
	X(variance_soften_mix, 0.35f) \
	X(variance_stage_mix, 1.0f) \
	X(reset_min_scale, 0.22f) \
	X(reset_hard_variance_gate, 0.95f) \
	X(reset_hard_compatibility, 0.20f) \
	X(reset_hard_factor, 0.06f) \
	X(variance_reset_boost_smooth_min, 0.10f) \
	X(variance_reset_boost_smooth_max, 0.55f) \
	X(analytical_variance_signal_floor, 0.02f) \
	X(analytical_variance_floor_base, 0.015f) \
	X(analytical_variance_floor_signal_scale, 0.025f) \
	X(analytical_variance_ratio_min, 2.0f) \
	X(analytical_variance_ratio_max, 10.0f) \
	X(analytical_variance_mean_min, 2.5f) \
	X(analytical_variance_mean_max, 7.5f) \
	X(luma_delta_gate_signal_floor, 0.05f) \
	X(luma_delta_gate_denominator_offset, 0.25f) \
	X(luma_delta_gate_smooth_min, 2.0f) \
	X(luma_delta_gate_smooth_max, 7.0f) \
	X(luma_delta_gate_mix, 0.0f) \
	X(external_gate_floor, 0.0f) \
	X(external_gate_mix, 1.0f) \
	X(external_gate_hard_threshold, -1.0f) \
	X(dependency_gate_floor, 0.35f) \
	X(dependency_gate_soft_min, 0.20f) \
	X(dependency_gate_soft_max, 0.85f) \
	X(dependency_gate_mix, 1.0f) \
	X(dependency_luma_signal_floor, 0.05f) \
	X(dependency_luma_denominator_offset, 0.03f) \
	X(dependency_luma_smooth_min, 0.12f) \
	X(dependency_luma_smooth_max, 0.55f) \
	X(dependency_luma_mix, 1.0f) \
	X(dependency_off_history_min, 0.03f) \
	X(dependency_off_current_max, 0.004f) \
	X(dependency_off_ratio_min, 0.06f) \
	X(history_luma_clamp, 1e6f) \
	X(history_outlier_noise_mean_floor, 0.05f) \
	X(history_outlier_noise_gate_min, 0.08f) \
	X(history_outlier_noise_gate_max, 0.45f) \
	X(history_outlier_envelope_sigma_min, 2.5f) \
	X(history_outlier_envelope_sigma_max, 5.5f) \
	X(history_outlier_envelope_mean_min, 0.15f) \
	X(history_outlier_envelope_mean_max, 0.45f) \
	X(history_outlier_envelope_min, 0.03f) \
	X(history_outlier_threshold_low_min, 1.0f) \
	X(history_outlier_threshold_low_max, 1.4f) \
	X(history_outlier_threshold_high_min, 2.5f) \
	X(history_outlier_threshold_high_max, 4.2f) \
	X(history_outlier_luma_cap_min, 2.0f) \
	X(history_outlier_luma_cap_scale_min, 1.0f) \
	X(history_outlier_luma_cap_scale_max, 1.6f) \
	X(history_outlier_external_gate_attenuation_min, 0.85f) \
	X(history_outlier_external_gate_attenuation_max, 0.35f) \
	X(history_outlier_accumulated_luma_scale_min, 1.1f) \
	X(history_outlier_accumulated_luma_scale_max, 1.45f) \
	X(deflicker_threshold_min, 0.10f) \
	X(deflicker_threshold_max, 0.25f) \
	X(parallax_roughness_threshold, 0.1f) \
	X(parallax_shading_normal_threshold, 0.01f)

#define LIST_ASVGF_REPROJECTION_UINT_PARAMS(X) \
	X(variance_compatibility_strategy, ASVGF_COMPATIBILITY_STATS) \
	X(history_filter_strategy, ASVGF_HISTORY_FILTER_NONE) \
	X(dependency_strategy, ASVGF_DEPENDENCY_NONE)

#define LIST_ASVGF_REPROJECTION_BOOL_PARAMS(X) \
	X(use_dependency_reset_as_gate, 0u) \
	X(deflicker_enabled, 0u)

typedef enum {
	ASVGF_REPROJECTION_PARAM_FLOAT,
	ASVGF_REPROJECTION_PARAM_UINT,
	ASVGF_REPROJECTION_PARAM_BOOL,
} asvgf_reprojection_param_type_t;

typedef struct {
	const char *name;
	size_t offset;
	asvgf_reprojection_param_type_t type;
} asvgf_reprojection_param_desc_t;

static const asvgf_reprojection_param_desc_t asvgf_reprojection_param_descs[] = {
#define X(name, default_value) { #name, offsetof(struct AsvgfReprojectionParams, name), ASVGF_REPROJECTION_PARAM_FLOAT },
	LIST_ASVGF_REPROJECTION_FLOAT_PARAMS(X)
#undef X
#define X(name, default_value) { #name, offsetof(struct AsvgfReprojectionParams, name), ASVGF_REPROJECTION_PARAM_UINT },
	LIST_ASVGF_REPROJECTION_UINT_PARAMS(X)
#undef X
#define X(name, default_value) { #name, offsetof(struct AsvgfReprojectionParams, name), ASVGF_REPROJECTION_PARAM_BOOL },
	LIST_ASVGF_REPROJECTION_BOOL_PARAMS(X)
#undef X
};

typedef enum {
	ASVGF_LOBE_DIRECT_DIFFUSE,
	ASVGF_LOBE_DIRECT_SPECULAR,
	ASVGF_LOBE_INDIRECT_DIFFUSE,
	ASVGF_LOBE_INDIRECT_SPECULAR,
} asvgf_lobe_id_t;

static const struct AsvgfReprojectionParams asvgf_default_reprojection_params = {
#define X(name, default_value) .name = default_value,
	LIST_ASVGF_REPROJECTION_FLOAT_PARAMS(X)
#undef X
#define X(name, default_value) .name = default_value,
	LIST_ASVGF_REPROJECTION_UINT_PARAMS(X)
#undef X
#define X(name, default_value) .name = default_value,
	LIST_ASVGF_REPROJECTION_BOOL_PARAMS(X)
#undef X
};

static float *asvgfReprojectionFloatParamValue(struct AsvgfReprojectionParams *params, const asvgf_reprojection_param_desc_t *desc) {
	return PTR_CAST(float, (char*)params + desc->offset);
}

static uint32_t *asvgfReprojectionUintParamValue(struct AsvgfReprojectionParams *params, const asvgf_reprojection_param_desc_t *desc) {
	return PTR_CAST(uint32_t, (char*)params + desc->offset);
}

static void makeDefaultAsvgfLobeParams(asvgf_lobe_id_t lobe, struct AsvgfReprojectionParams *params) {
	*params = asvgf_default_reprojection_params;

	switch (lobe) {
	case ASVGF_LOBE_DIRECT_DIFFUSE:
		params->reset_min_scale = 0.22f;
		params->reset_hard_variance_gate = 0.95f;
		params->reset_hard_compatibility = 0.20f;
		params->reset_hard_factor = 0.06f;
		params->variance_stage_mix = 1.0f;
		params->external_gate_hard_threshold = 0.01f;
		params->use_dependency_reset_as_gate = 0u;
		break;

	case ASVGF_LOBE_DIRECT_SPECULAR:
		params->reset_min_scale = 0.18f;
		params->reset_hard_variance_gate = 1.01f;
		params->reset_hard_compatibility = 0.0f;
		params->reset_hard_factor = -1.0f;
		params->variance_stage_mix = 0.65f;
		params->history_luma_clamp = 2.0f;
		params->history_filter_strategy = ASVGF_HISTORY_FILTER_LUMA_OUTLIER_CLAMP;
		params->use_dependency_reset_as_gate = 0u;
		break;

	case ASVGF_LOBE_INDIRECT_DIFFUSE:
		// Indirect diffuse is the noisiest lobe and should prefer temporal
		// stability over fast response. These defaults keep the old stable
		// luma-delta compatibility path, but make partial reset and
		// direct-diffuse dependency less sensitive to frame-to-frame noise.
		params->history_samples_max = 32.0f;
		params->history_current_weight_min = 0.04f;

		params->variance_compatibility_strategy = ASVGF_COMPATIBILITY_LUMA_DELTA;
		params->variance_compatibility_luma_scale = 0.45f;
		params->variance_compatibility_delta_floor = 0.18f;
		params->variance_compatibility_signal_floor = 0.05f;
		params->variance_compatibility_smooth_min = 2.0f;
		params->variance_compatibility_smooth_max = 10.0f;
		params->variance_compatibility_floor = 0.75f;

		params->luma_delta_gate_mix = 1.0f;
		params->variance_gate_floor = 0.15f;
		params->variance_stage_mix = 0.35f;

		params->reset_min_scale = 0.965f;
		params->reset_hard_variance_gate = 1.01f;
		params->reset_hard_compatibility = 0.0f;
		params->reset_hard_factor = -1.0f;

		params->external_gate_floor = 0.70f;
		params->dependency_strategy = ASVGF_DEPENDENCY_RELIGHT_CARRY;
		params->dependency_gate_floor = 0.50f;
		params->dependency_gate_mix = 0.65f;
		params->dependency_luma_denominator_offset = 0.06f;
		params->dependency_luma_smooth_min = 0.20f;
		params->dependency_luma_smooth_max = 0.80f;
		params->dependency_luma_mix = 0.65f;
		params->deflicker_enabled = 1u;
		params->deflicker_threshold_min = 0.10f;
		params->deflicker_threshold_max = 0.25f;
		params->use_dependency_reset_as_gate = 1u;
		break;

	case ASVGF_LOBE_INDIRECT_SPECULAR:
		params->variance_stage_mix = 0.45f;
		params->variance_gate_floor = 0.35f;
		params->reset_min_scale = 0.30f;
		params->reset_hard_variance_gate = 1.01f;
		params->reset_hard_compatibility = 0.0f;
		params->reset_hard_factor = -1.0f;
		params->external_gate_hard_threshold = 0.01f;
		params->dependency_strategy = ASVGF_DEPENDENCY_VARIANCE_CARRY;
		params->use_dependency_reset_as_gate = 1u;
		break;
	}
}

static const asvgf_reprojection_param_desc_t *findAsvgfReprojectionParam(const char *name) {
	for (size_t i = 0; i < COUNTOF(asvgf_reprojection_param_descs); ++i) {
		if (0 == Q_stricmp(name, asvgf_reprojection_param_descs[i].name)) {
			return asvgf_reprojection_param_descs + i;
		}
	}

	return NULL;
}

static qboolean parseAsvgfBool(const char *value, uint32_t *out_value) {
	if (0 == Q_stricmp(value, "1") || 0 == Q_stricmp(value, "true") || 0 == Q_stricmp(value, "yes") || 0 == Q_stricmp(value, "on")) {
		*out_value = 1u;
		return true;
	}

	if (0 == Q_stricmp(value, "0") || 0 == Q_stricmp(value, "false") || 0 == Q_stricmp(value, "no") || 0 == Q_stricmp(value, "off")) {
		*out_value = 0u;
		return true;
	}

	return false;
}

static void resetAsvgfParams( void ) {
	makeDefaultAsvgfLobeParams(ASVGF_LOBE_DIRECT_DIFFUSE, &g_rtx.asvgf_params.direct_diffuse);
	makeDefaultAsvgfLobeParams(ASVGF_LOBE_DIRECT_SPECULAR, &g_rtx.asvgf_params.direct_specular);
	makeDefaultAsvgfLobeParams(ASVGF_LOBE_INDIRECT_DIFFUSE, &g_rtx.asvgf_params.indirect_diffuse);
	makeDefaultAsvgfLobeParams(ASVGF_LOBE_INDIRECT_SPECULAR, &g_rtx.asvgf_params.indirect_specular);
}

static void printStrategyHelp( void ) {
	gEngine.Con_Printf("Strategies:\n");
	gEngine.Con_Printf("\tvariance_compatibility_strategy: 0 stats, 1 luma_delta\n");
	gEngine.Con_Printf("\thistory_filter_strategy: 0 none, 1 luma_outlier_clamp\n");
	gEngine.Con_Printf("\tdependency_strategy: 0 none, 1 combine_min, 2 relight_carry, 3 variance_carry\n");
	gEngine.Con_Printf("\tdeflicker_enabled: false/true; thresholds are relative luminance delta, defaults 0.10..0.25, enabled by default only for indirect_diffuse\n");
}

static void printAsvgfReprojectionParams(const char *lobe_name, asvgf_lobe_id_t lobe, struct AsvgfReprojectionParams *params) {
	struct AsvgfReprojectionParams defaults;
	makeDefaultAsvgfLobeParams(lobe, &defaults);

	gEngine.Con_Printf("ASVGF %s reprojection params:\n", lobe_name);

	for (size_t i = 0; i < COUNTOF(asvgf_reprojection_param_descs); ++i) {
		const asvgf_reprojection_param_desc_t *const desc = asvgf_reprojection_param_descs + i;
		if (desc->type == ASVGF_REPROJECTION_PARAM_FLOAT) {
			gEngine.Con_Printf("\t%s = %g (default %g)\n",
				desc->name,
				*asvgfReprojectionFloatParamValue(params, desc),
				*asvgfReprojectionFloatParamValue(&defaults, desc));
		} else if (desc->type == ASVGF_REPROJECTION_PARAM_BOOL) {
			gEngine.Con_Printf("\t%s = %s (default %s)\n",
				desc->name,
				*asvgfReprojectionUintParamValue(params, desc) ? "true" : "false",
				*asvgfReprojectionUintParamValue(&defaults, desc) ? "true" : "false");
		} else {
			gEngine.Con_Printf("\t%s = %u (default %u)\n",
				desc->name,
				*asvgfReprojectionUintParamValue(params, desc),
				*asvgfReprojectionUintParamValue(&defaults, desc));
		}
	}

	printStrategyHelp();
}

static void denoiserLobeParamCmd(const char *command_name, const char *lobe_name, asvgf_lobe_id_t lobe, struct AsvgfReprojectionParams *params) {
	const int argc = gEngine.Cmd_Argc();
	const char *const arg = argc >= 2 ? gEngine.Cmd_Argv(1) : "list";

	if (argc == 1 || (argc == 2 && 0 == Q_stricmp(arg, "list"))) {
		gEngine.Con_Printf("Usage:\n");
		gEngine.Con_Printf("\t%s list\n", command_name);
		gEngine.Con_Printf("\t%s reset\n", command_name);
		gEngine.Con_Printf("\t%s <paramName> [value]\n", command_name);
		printAsvgfReprojectionParams(lobe_name, lobe, params);
		return;
	}

	if (argc == 2 && 0 == Q_stricmp(arg, "reset")) {
		makeDefaultAsvgfLobeParams(lobe, params);
		g_rtx.discontinuity = true;
		gEngine.Con_Printf("ASVGF %s reprojection params reset to defaults\n", lobe_name);
		return;
	}

	const asvgf_reprojection_param_desc_t *const desc = findAsvgfReprojectionParam(arg);
	if (!desc) {
		gEngine.Con_Printf("Unknown ASVGF reprojection param \"%s\". Valid params:\n", arg);
		printAsvgfReprojectionParams(lobe_name, lobe, params);
		return;
	}

	struct AsvgfReprojectionParams defaults;
	makeDefaultAsvgfLobeParams(lobe, &defaults);

	if (argc == 2) {
		if (desc->type == ASVGF_REPROJECTION_PARAM_FLOAT) {
			gEngine.Con_Printf("%s.%s = %g (default %g)\n",
				lobe_name,
				desc->name,
				*asvgfReprojectionFloatParamValue(params, desc),
				*asvgfReprojectionFloatParamValue(&defaults, desc));
		} else if (desc->type == ASVGF_REPROJECTION_PARAM_BOOL) {
			gEngine.Con_Printf("%s.%s = %s (default %s)\n",
				lobe_name,
				desc->name,
				*asvgfReprojectionUintParamValue(params, desc) ? "true" : "false",
				*asvgfReprojectionUintParamValue(&defaults, desc) ? "true" : "false");
		} else {
			gEngine.Con_Printf("%s.%s = %u (default %u)\n",
				lobe_name,
				desc->name,
				*asvgfReprojectionUintParamValue(params, desc),
				*asvgfReprojectionUintParamValue(&defaults, desc));
		}
		return;
	}

	if (argc != 3) {
		gEngine.Con_Printf("Usage: %s <paramName> [value]\n", command_name);
		return;
	}

	if (desc->type == ASVGF_REPROJECTION_PARAM_FLOAT) {
		*asvgfReprojectionFloatParamValue(params, desc) = Q_atof(gEngine.Cmd_Argv(2));
		gEngine.Con_Printf("%s.%s = %g\n", lobe_name, desc->name, *asvgfReprojectionFloatParamValue(params, desc));
	} else if (desc->type == ASVGF_REPROJECTION_PARAM_BOOL) {
		uint32_t bool_value;
		if (!parseAsvgfBool(gEngine.Cmd_Argv(2), &bool_value)) {
			gEngine.Con_Printf("Expected boolean value for %s.%s\n", lobe_name, desc->name);
			return;
		}

		*asvgfReprojectionUintParamValue(params, desc) = bool_value;
		gEngine.Con_Printf("%s.%s = %s\n", lobe_name, desc->name, bool_value ? "true" : "false");
	} else {
		*asvgfReprojectionUintParamValue(params, desc) = (uint32_t)atoi(gEngine.Cmd_Argv(2));
		gEngine.Con_Printf("%s.%s = %u\n", lobe_name, desc->name, *asvgfReprojectionUintParamValue(params, desc));
	}

	g_rtx.discontinuity = true;
}

static void denoiserDirectDiffuseParamCmd( void ) {
	denoiserLobeParamCmd("rt_denoiser_direct_diffuse", "direct_diffuse", ASVGF_LOBE_DIRECT_DIFFUSE, &g_rtx.asvgf_params.direct_diffuse);
}

static void denoiserDirectSpecularParamCmd( void ) {
	denoiserLobeParamCmd("rt_denoiser_direct_specular", "direct_specular", ASVGF_LOBE_DIRECT_SPECULAR, &g_rtx.asvgf_params.direct_specular);
}

static void denoiserIndirectDiffuseParamCmd( void ) {
	denoiserLobeParamCmd("rt_denoiser_indirect_diffuse", "indirect_diffuse", ASVGF_LOBE_INDIRECT_DIFFUSE, &g_rtx.asvgf_params.indirect_diffuse);
}

static void denoiserIndirectSpecularParamCmd( void ) {
	denoiserLobeParamCmd("rt_denoiser_indirect_specular", "indirect_specular", ASVGF_LOBE_INDIRECT_SPECULAR, &g_rtx.asvgf_params.indirect_specular);
}

static void denoiserParamCmd( void ) {
	const int argc = gEngine.Cmd_Argc();

	if (argc == 2 && 0 == Q_stricmp(gEngine.Cmd_Argv(1), "reset")) {
		resetAsvgfParams();
		g_rtx.discontinuity = true;
		gEngine.Con_Printf("ASVGF denoiser params reset to defaults\n");
		return;
	}

	gEngine.Con_Printf("rt_denoiser_param is deprecated. Use:\n");
	gEngine.Con_Printf("\trt_denoiser_direct_diffuse\n");
	gEngine.Con_Printf("\trt_denoiser_direct_specular\n");
	gEngine.Con_Printf("\trt_denoiser_indirect_diffuse\n");
	gEngine.Con_Printf("\trt_denoiser_indirect_specular\n");
	printStrategyHelp();
}

#undef LIST_ASVGF_REPROJECTION_FLOAT_PARAMS
#undef LIST_ASVGF_REPROJECTION_UINT_PARAMS
#undef LIST_ASVGF_REPROJECTION_BOOL_PARAMS

void VK_RayNewMapBegin( void ) {
	// TODO it seems like these are unnecessary leftovers. Moreover, they are actively harmful,
	// as they recreate things that are in fact pretty much static. Untangle this.
	RT_VkAccelNewMap();
	RT_RayModel_Clear();
}

void VK_RayFrameBegin( void ) {
	ASSERT(vk_core.rtx);

	XVK_RayModel_ClearForNextFrame();
	RT_LightsFrameBegin();
}

static void parseDebugDisplayValue( void ) {
	if (!(g_rtx.debug.rt_debug_display_only->flags & FCVAR_CHANGED))
		return;

	g_rtx.debug.rt_debug_display_only->flags &= ~FCVAR_CHANGED;

	const char *cvalue = g_rtx.debug.rt_debug_display_only->string;
#define LIST_DISPLAYS(X) \
	X(BASECOLOR, "material base_color value") \
	X(BASEALPHA, "material alpha value") \
	X(EMISSIVE, "emissive color") \
	X(NSHADE, "shading normal") \
	X(NGEOM, "geometry normal") \
	X(LIGHTING, "all lighting, direct and indirect, w/o base_color") \
	X(SURFHASH, "each surface has random color") \
	X(DIRECT, "direct lighting only, both diffuse and specular") \
	X(DIRECT_DIFF, "direct diffuse lighting only") \
	X(DIRECT_SPEC, "direct specular lighting only") \
	X(INDIRECT, "indirect lighting only (bounced), diffuse and specular together") \
	X(INDIRECT_DIFF, "indirect diffuse only") \
	X(INDIRECT_SPEC, "indirect specular only") \
	X(TRIHASH, "each triangle is drawn with random color") \
	X(MATERIAL, "red = roughness, green = metalness") \
	X(DIFFUSE, "direct + indirect diffuse, spatially denoised") \
	X(SPECULAR, "direct + indirect specular, spatially denoised") \

#define X(suffix, info) \
	if (0 == Q_stricmp(cvalue, #suffix)) { \
		WARN("setting debug display to %s", "DEBUG_DISPLAY_"#suffix); \
		g_rtx.debug.rt_debug_display_only_value = DEBUG_DISPLAY_##suffix; \
		return; \
	}
LIST_DISPLAYS(X)
#undef X

	if (Q_strlen(cvalue) > 0) {
		gEngine.Con_Printf("Invalid rt_debug_display_only mode %s. Valid modes are:\n", cvalue);
#define X(suffix, info) gEngine.Con_Printf("\t%s -- %s\n", #suffix, info);
LIST_DISPLAYS(X)
#undef X
	}

	g_rtx.debug.rt_debug_display_only_value = DEBUG_DISPLAY_DISABLED;
//#undef LIST_DISPLAYS
}

static void parseDebugFlags( void ) {
	if (!(g_rtx.debug.rt_debug_flags->flags & FCVAR_CHANGED))
		return;

	g_rtx.debug.rt_debug_flags->flags &= ~FCVAR_CHANGED;
	g_rtx.debug.rt_debug_flags_value = 0;

#define LIST_DEBUG_FLAGS(X) \
	X(WHITE_FURNACE, "white furnace mode: diffuse white materials, diffuse sky light only") \

	const char *cvalue = g_rtx.debug.rt_debug_flags->string;
#define X(suffix, info) \
	if (0 == Q_stricmp(cvalue, #suffix)) { \
		WARN("setting debug flags to %s", "DEBUG_FLAG_"#suffix); \
		g_rtx.debug.rt_debug_flags_value |= DEBUG_FLAG_##suffix; \
	} else
LIST_DEBUG_FLAGS(X)
#undef X

	/* else: no valid flags found */
	if (Q_strlen(cvalue) > 0) {
		gEngine.Con_Printf("Invalid rt_debug_flags value %s. Valid flags are:\n", cvalue);
#define X(suffix, info) gEngine.Con_Printf("\t%s -- %s\n", #suffix, info);
LIST_DEBUG_FLAGS(X)
#undef X
	}

//#undef LIST_DEBUG_FLAGS
}

static uint32_t getRandomSeed( void ) {
	if (g_rtx.debug.rt_debug_fixed_random_seed->string[0])
		return (uint32_t)g_rtx.debug.rt_debug_fixed_random_seed->value;

	return (uint32_t)gEngine.COM_RandomLong(0, INT32_MAX);
}

static void produceUboResource(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx) {
	// TODO using frame_sequence is only accidental synchronization. It should be done via e.g. resource->consumed or smth.
	const size_t ubo_slot_offset = (ctx->frame_sequence % MAX_FRAMES_IN_FLIGHT) * g_rtx.uniform.unit_size;
	struct UniformBuffer *const ubo = PTR_CAST(struct UniformBuffer, (char*)g_rtx.uniform.buffer.mapped + ubo_slot_offset);
	g_rtx.uniform.resource->offset = ubo_slot_offset;
	ubo->frame_counter = ctx->frame_sequence;
	memcpy(ubo, &g_rtx.uniform.current, sizeof(struct UniformBuffer));
}

static struct UniformBuffer prepareUniformBuffer( const vk_ray_frame_render_args_t *args, float fov_angle_y, int frame_width, int frame_height ) {
	struct UniformBuffer ret;
	matrix4x4 proj_inv, view_inv;
	Matrix4x4_Invert_Full(proj_inv, *args->projection);
	Matrix4x4_ToArrayFloatGL(proj_inv, (float*)ret.inv_proj);

	// TODO there's a more efficient way to construct an inverse view matrix
	// from vforward/right/up vectors and origin in g_camera
	Matrix4x4_Invert_Full(view_inv, *args->view);
	Matrix4x4_ToArrayFloatGL(view_inv, (float*)ret.inv_view);

	// previous frame matrices
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_proj, (float*)ret.prev_inv_proj);
	Matrix4x4_ToArrayFloatGL(g_rtx.prev_inv_view, (float*)ret.prev_inv_view);
	Matrix4x4_Copy(g_rtx.prev_inv_view, view_inv);
	Matrix4x4_Copy(g_rtx.prev_inv_proj, proj_inv);

	ret.res[0] = frame_width;
	ret.res[1] = frame_height;
	ret.ray_cone_width = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)frame_height);
	ret.skybox_exposure = R_TexturesGetSkyboxInfo().exposure;

	parseDebugDisplayValue();
	if (g_rtx.debug.rt_debug_display_only_value) {
		ret.debug_display_only = g_rtx.debug.rt_debug_display_only_value;
	} else {
		ret.debug_display_only = r_lightmap->value != 0 ? DEBUG_DISPLAY_LIGHTING : DEBUG_DISPLAY_DISABLED;
	}

	parseDebugFlags();
	ret.debug_flags = g_rtx.debug.rt_debug_flags_value;

	ret.random_seed = getRandomSeed();

#define SET_RENDERER_FLAG(flag) (legacy_bounce ? 0 : (flag))
	const qboolean legacy_bounce = CVAR_TO_BOOL(rt_legacy_bounce);
	const qboolean disable_reconstruction = CVAR_TO_BOOL(rt_disable_reconstruction);
	const qboolean disable_sh_gi_denoising = CVAR_TO_BOOL(rt_disable_sh_gi_denoising);
	ret.renderer_flags = SET_RENDERER_FLAG(RENDERER_FLAG_ONLY_DIFFUSE_GI) |
					  SET_RENDERER_FLAG(RENDERER_FLAG_SEPARATED_REFLECTION) |
					  (disable_sh_gi_denoising ? 0 : RENDERER_FLAG_DENOISE_GI_BY_SH) |
					  (disable_reconstruction ? 0 : RENDERER_FLAG_SPATIAL_RECONSTRUCTION) |
					  (CVAR_TO_BOOL(rt_disable_gi) ? RENDERER_FLAG_DISABLE_GI : 0) |
					  (CVAR_TO_BOOL(rt_disable_reprojection) ? RENDERER_FLAG_DISABLE_REPROJECTION : 0);
#undef SET_RENDERER_FLAG

	ret.asvgf = g_rtx.asvgf_params;

	return ret;
}

typedef struct {
	const vk_ray_frame_render_args_t* render_args;
	int frame_index;
	uint32_t frame_counter;
	float fov_angle_y;
	int frame_width, frame_height;
} perform_tracing_args_t;

static r_vk_image_t* performTracing( vk_combuf_t *combuf, const perform_tracing_args_t* args) {
	APROF_SCOPE_DECLARE_BEGIN(perform, __FUNCTION__);
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;
	DEBUG_BEGIN(cmdbuf, "yay tracing");

	g_rtx.uniform.current = prepareUniformBuffer(args->render_args, args->fov_angle_y, args->frame_width, args->frame_height);

	ASSERT(g_rtx.meatpipe);
	r_vk_image_t *const ret = R_VkMeatpipeDispatch(g_rtx.meatpipe, (vk_meatpipe_dispatch_t){
		.combuf = combuf,
		.frame_sequence = args->frame_counter,
		.frame_set_slot = args->frame_index,
		.width = args->frame_width,
		.height = args->frame_height,
		.is_discontinuous = g_rtx.discontinuity,
	});

	if (g_rtx.discontinuity) {
		DEBUG("discontinuity => false");
		g_rtx.discontinuity = false;
	}

	DEBUG_END(cmdbuf);
	APROF_SCOPE_END(perform);

	return ret;
}

static void destroyMeatpipe(void) {
	R_VkMeatpipeDestroy(g_rtx.meatpipe);
	g_rtx.meatpipe = NULL;
}

static qboolean reloadMeatpipe(void) {
	struct vk_meatpipe_s *const newpipe = R_VkMeatpipeCreateFromFile("rt.meat");
	if (!newpipe)
		return false;

	if (!R_VkMeatpipeAcquireResources(newpipe, g_rtx.max_frame_width, g_rtx.max_frame_height))
		goto fail;

	destroyMeatpipe();

	g_rtx.meatpipe = newpipe;
	g_rtx.meatpipe_out = R_VkResourceFindByName("dest");
	ASSERT(g_rtx.meatpipe_out);

	return true;

fail:
	R_VkMeatpipeDestroy(newpipe);
	return false;
}

static void reloadOrResizeIfNeeded(const vk_ray_frame_render_args_t* args) {
	qboolean need_resize = false;

	if (g_rtx.max_frame_width < args->dst->width) {
		g_rtx.max_frame_width = ALIGN_UP(args->dst->width, 16);
		WARN("Increasing max_frame_width to %d", g_rtx.max_frame_width);
		need_resize = true;
	}

	if (g_rtx.max_frame_height < args->dst->height) {
		g_rtx.max_frame_height = ALIGN_UP(args->dst->height, 16);
		WARN("Increasing max_frame_height to %d", g_rtx.max_frame_height);
		need_resize = true;
	}

	if (g_rtx.reload_pipeline) {
		WARN("Reloading RTX shaders/pipelines");
		XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

		if (reloadMeatpipe())
			need_resize = false;

		g_rtx.reload_pipeline = false;
	}

	if (need_resize) {
		if (!R_VkMeatpipeAcquireResources(g_rtx.meatpipe, g_rtx.max_frame_width, g_rtx.max_frame_height)) {
			ERR("Unable to reacquire resources and resize RT framebuffer. Bad things will happen.");
		}
		need_resize = false;
	}

	ASSERT(args->dst->width <= g_rtx.max_frame_width);
	ASSERT(args->dst->height <= g_rtx.max_frame_height);
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	APROF_SCOPE_DECLARE_BEGIN(ray_frame_end, __FUNCTION__);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself

	g_rtx.frame_number++;

	reloadOrResizeIfNeeded(args);

	// TODO dynamic scaling based on perf
	const int frame_width = args->dst->width;
	const int frame_height = args->dst->height;

	// Do not draw when we have no swapchain
	if (!args->dst->image)
		goto tail;

	if (RT_VkAccelIsEmpty()) {
		R_VkImageClear( args->dst, args->combuf, NULL );
	} else {
		const perform_tracing_args_t trace_args = {
			.render_args = args,
			.frame_index = (g_rtx.frame_number % 2),
			.frame_counter = g_rtx.frame_number,
			.fov_angle_y = args->fov_angle_y,
			.frame_width = frame_width,
			.frame_height = frame_height,
		};
		r_vk_image_t *const result = performTracing( args->combuf, &trace_args );
		ASSERT(g_rtx.meatpipe_out);
		const r_vkimage_blit_args blit_args = {
			.src = {
				.image = result,
				.width = frame_width,
				.height = frame_height,
			},
			.dst = {
				.image = args->dst,
			},
		};

		R_VkImageBlit( args->combuf, &blit_args );
	}

tail:
	APROF_SCOPE_END(ray_frame_end);
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure

	resetAsvgfParams();

	g_rtx.max_frame_width = MIN_FRAME_WIDTH;
	g_rtx.max_frame_height = MIN_FRAME_HEIGHT;

	if (!RT_VkAccelInit())
		return false;

	// FIXME shutdown accel
	if (!RT_DynamicModelInit())
		return false;

	g_rtx.uniform.unit_size = ALIGN_UP(sizeof(struct UniformBuffer), v_device_info.properties.limits.minUniformBufferOffsetAlignment);

	if (!VK_BufferCreate("ray uniform.buffer", &g_rtx.uniform.buffer, g_rtx.uniform.unit_size * MAX_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		// TODO cleanup
		return false;
	}

	g_rtx.uniform.producer = (Producer) {
		.name = "ubo",
		.produce = produceUboResource,
	};

	g_rtx.uniform.resource = R_VkBufferRegisterAsResource((r_vkbuffer_register_as_resource_t){
		.name = "ubo",
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.buffer = &g_rtx.uniform.buffer,
		.offset = 0, // Will be set dynamically each frame
		.size = sizeof(struct UniformBuffer),
		.producer = &g_rtx.uniform.producer,
	});

	if (!RT_KusochkiInit()) {
		// TODO cleanup
		return false;
	}

	reloadMeatpipe();
	if (!g_rtx.meatpipe)
		return false;

	RT_RayModel_Clear();

	gEngine.Cmd_AddCommand("rt_debug_reload_pipelines", reloadPipeline, "Reload RT pipelines");
	gEngine.Cmd_AddCommand("rt_denoiser_direct_diffuse", denoiserDirectDiffuseParamCmd, "ASVGF direct diffuse params; default stats compatibility, reset_min_scale 0.22, deflicker off");
	gEngine.Cmd_AddCommand("rt_denoiser_direct_specular", denoiserDirectSpecularParamCmd, "ASVGF direct specular params; default outlier history filter, luma clamp 2.0, deflicker off");
	gEngine.Cmd_AddCommand("rt_denoiser_indirect_diffuse", denoiserIndirectDiffuseParamCmd, "ASVGF indirect diffuse params; default stable luma-delta, 32 samples, deflicker 0.10..0.25 on");
	gEngine.Cmd_AddCommand("rt_denoiser_indirect_specular", denoiserIndirectSpecularParamCmd, "ASVGF indirect specular params; default variance dependency carry, parallax validation, deflicker off");
	gEngine.Cmd_AddCommand("rt_denoiser_param", denoiserParamCmd, "Deprecated ASVGF command");

#define X(name, info) #name ", "
	g_rtx.debug.rt_debug_display_only = gEngine.Cvar_Get("rt_debug_display_only", "", FCVAR_GLCONFIG,
		"Display only the specified channel (" LIST_DISPLAYS(X) "etc)");

	g_rtx.debug.rt_debug_flags = gEngine.Cvar_Get("rt_debug_flags", "", FCVAR_GLCONFIG,
		"Enable shader debug flags (" LIST_DEBUG_FLAGS(X) "etc)");
#undef X

	g_rtx.debug.rt_debug_fixed_random_seed = gEngine.Cvar_Get("rt_debug_fixed_random_seed", "", FCVAR_GLCONFIG,
		"Fix random seed value for RT monte carlo sampling. Used for reproducible regression testing");

	return true;
}

void VK_RayShutdown( void ) {
	ASSERT(vk_core.rtx);

	destroyMeatpipe();

	RT_KusochkiShutdown();
	VK_BufferDestroy(&g_rtx.uniform.buffer);

	RT_VkAccelShutdown();
	RT_DynamicModelShutdown();
}

void RT_FrameDiscontinuity( void ) {
	DEBUG("%s", __FUNCTION__);
	g_rtx.discontinuity = true;
}
