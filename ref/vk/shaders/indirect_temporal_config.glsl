#ifndef INDIRECT_TEMPORAL_CONFIG_GLSL
#define INDIRECT_TEMPORAL_CONFIG_GLSL
#define ASVGF_COMPATIBILITY_STATS 0u
#define ASVGF_COMPATIBILITY_LUMA_DELTA 1u

#define ASVGF_HISTORY_FILTER_NONE 0u
#define ASVGF_HISTORY_FILTER_LUMA_OUTLIER_CLAMP 1u

struct AsvgfReprojectionParams {
    float parallax_depth_threshold_scale;
    float parallax_shading_normal_threshold;
    float parallax_roughness_threshold;
	float history_samples_max;
	float history_current_weight_min;
	float reprojection_depth_threshold_scale;

	float variance_compatibility_luma_scale;
	float variance_compatibility_delta_floor;
	float variance_compatibility_signal_floor;
	float variance_compatibility_smooth_min;
	float variance_compatibility_smooth_max;
	float variance_compatibility_floor;

	float variance_gate_min;
	float variance_gate_max;
	float variance_gate_floor;
	float variance_soften_threshold;
	float variance_soften_mix;
	float variance_stage_mix;

	float reset_min_scale;
	float reset_hard_variance_gate;
	float reset_hard_compatibility;
	float reset_hard_factor;
	float variance_reset_boost_smooth_min;
	float variance_reset_boost_smooth_max;

	float analytical_variance_signal_floor;
	float analytical_variance_floor_base;
	float analytical_variance_floor_signal_scale;
	float analytical_variance_ratio_min;
	float analytical_variance_ratio_max;
	float analytical_variance_mean_min;
	float analytical_variance_mean_max;

	float luma_delta_gate_signal_floor;
	float luma_delta_gate_denominator_offset;
	float luma_delta_gate_smooth_min;
	float luma_delta_gate_smooth_max;
	float luma_delta_gate_mix;

	float external_gate_floor;
	float external_gate_mix;

	float history_luma_clamp;
	float history_outlier_noise_mean_floor;
	float history_outlier_noise_gate_min;
	float history_outlier_noise_gate_max;
	float history_outlier_envelope_sigma_min;
	float history_outlier_envelope_sigma_max;
	float history_outlier_envelope_mean_min;
	float history_outlier_envelope_mean_max;
	float history_outlier_envelope_min;
	float history_outlier_threshold_low_min;
	float history_outlier_threshold_low_max;
	float history_outlier_threshold_high_min;
	float history_outlier_threshold_high_max;
	float history_outlier_luma_cap_min;
	float history_outlier_luma_cap_scale_min;
	float history_outlier_luma_cap_scale_max;
	float history_outlier_external_gate_attenuation_min;
	float history_outlier_external_gate_attenuation_max;
	float history_outlier_accumulated_luma_scale_min;
	float history_outlier_accumulated_luma_scale_max;

	float deflicker_threshold_min;
	float deflicker_threshold_max;

	uint variance_compatibility_strategy;
	uint history_filter_strategy;

	uint deflicker_enabled;
};

AsvgfReprojectionParams reflectionTemporalParams() {
    AsvgfReprojectionParams params;
    params.parallax_depth_threshold_scale = 1.35;
    params.parallax_shading_normal_threshold = 0.01;
    params.parallax_roughness_threshold = 0.1;
    params.history_samples_max = 16.0;
    params.history_current_weight_min = 0.1;
    params.reprojection_depth_threshold_scale = 0.01;
    params.variance_compatibility_luma_scale = 0.1;
    params.variance_compatibility_delta_floor = 0.02;
    params.variance_compatibility_signal_floor = 0.02;
    params.variance_compatibility_smooth_min = 2.0;
    params.variance_compatibility_smooth_max = 6.0;
    params.variance_compatibility_floor = 0.0;
    params.variance_gate_min = 0.04;
    params.variance_gate_max = 0.35;
    params.variance_gate_floor = 0.0;
    params.variance_soften_threshold = 0.2;
    params.variance_soften_mix = 0.35;
    params.variance_stage_mix = 1.0;
    params.reset_min_scale = 0.22;
    params.reset_hard_variance_gate = 0.95;
    params.reset_hard_compatibility = 0.20;
    params.reset_hard_factor = 0.06;
    params.variance_reset_boost_smooth_min = 0.10;
    params.variance_reset_boost_smooth_max = 0.55;
    params.analytical_variance_signal_floor = 0.02;
    params.analytical_variance_floor_base = 0.015;
    params.analytical_variance_floor_signal_scale = 0.025;
    params.analytical_variance_ratio_min = 2.0;
    params.analytical_variance_ratio_max = 10.0;
    params.analytical_variance_mean_min = 2.5;
    params.analytical_variance_mean_max = 7.5;
    params.luma_delta_gate_signal_floor = 0.05;
    params.luma_delta_gate_denominator_offset = 0.25;
    params.luma_delta_gate_smooth_min = 2.0;
    params.luma_delta_gate_smooth_max = 7.0;
    params.luma_delta_gate_mix = 0.0;
    params.external_gate_floor = 0.0;
    params.external_gate_mix = 1.0;
    params.history_luma_clamp = 1e6;
    params.history_outlier_noise_mean_floor = 0.05;
    params.history_outlier_noise_gate_min = 0.08;
    params.history_outlier_noise_gate_max = 0.45;
    params.history_outlier_envelope_sigma_min = 2.5;
    params.history_outlier_envelope_sigma_max = 5.5;
    params.history_outlier_envelope_mean_min = 0.15;
    params.history_outlier_envelope_mean_max = 0.45;
    params.history_outlier_envelope_min = 0.03;
    params.history_outlier_threshold_low_min = 1.0;
    params.history_outlier_threshold_low_max = 1.4;
    params.history_outlier_threshold_high_min = 2.5;
    params.history_outlier_threshold_high_max = 4.2;
    params.history_outlier_luma_cap_min = 2.0;
    params.history_outlier_luma_cap_scale_min = 1.0;
    params.history_outlier_luma_cap_scale_max = 1.6;
    params.history_outlier_external_gate_attenuation_min = 0.85;
    params.history_outlier_external_gate_attenuation_max = 0.35;
    params.history_outlier_accumulated_luma_scale_min = 1.1;
    params.history_outlier_accumulated_luma_scale_max = 1.45;
    params.deflicker_threshold_min = 0.10;
    params.deflicker_threshold_max = 0.35;
    params.variance_compatibility_strategy = ASVGF_COMPATIBILITY_STATS;
    params.history_filter_strategy = ASVGF_HISTORY_FILTER_NONE;
    params.deflicker_enabled = 0u;

    params.variance_stage_mix = 0.45;
    params.variance_gate_floor = 0.35;
    params.reset_min_scale = 0.30;
    params.reset_hard_variance_gate = 1.01;
    params.reset_hard_compatibility = 0.0;
    params.reset_hard_factor = -1.0;

    return params;
}

#endif
