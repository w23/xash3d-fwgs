// Indirect-only ASVGF lobe filtering from vk_light_cache_indirect.
struct LuminanceStats {
	float mean;
	float variance;
	float deflicker_luma;
};

struct AsvgfGateState {
	float reset_factor;
	float variance_gate;
	float variance_reset_boost;
	float current_luma;
	float history_luma;
	bool hard_reset;
};

struct AsvgfLobeContext {
	vec3 current_color;
	vec3 history_color;
	LuminanceStats current_stats;
	LuminanceStats history_stats;
	float history_samples;
	bool history_valid;
};

struct AsvgfLobeReprojectionInput {
	AsvgfLobeContext lobe;
	AsvgfGateState external_gate;
};

struct AsvgfHistoryFilterResult {
	vec3 history_color;
	float outlier;
	float external_gate_factor;
	float accumulated_luma_cap;
};

struct AsvgfLobeReprojectionResult {
	vec3 color;
	LuminanceStats stats;
	float history_samples;
	AsvgfGateState gate;
};

struct AsvgfDeflickerResult {
	vec3 color;
	float stored_luma;
};

LuminanceStats decodeLuminanceStats(vec4 packed) {
	LuminanceStats stats;
	stats.mean = max(packed.x, 0.0);
	stats.variance = max(packed.y, 0.0);
	stats.deflicker_luma = packed.z > 0.0 ? packed.z : stats.mean;
	return stats;
}

vec4 encodeLuminanceStats(LuminanceStats stats) {
	return vec4(max(stats.mean, 0.0), max(stats.variance, 0.0), max(stats.deflicker_luma, 0.0), 0.0);
}

vec3 clampSpecularLuminance(vec3 specular, float max_luminance) {
	float lum = luminance(specular);
	if (lum <= 0.0) {
		return vec3(0.0);
	}

	float clamped = min(max_luminance, lum);
	return specular * (clamped / lum);
}

AsvgfGateState makeGate(float reset_factor, bool hard_reset) {
	AsvgfGateState gate;
	gate.reset_factor = clamp(reset_factor, 0.0, 1.0);
	gate.variance_gate = 0.0;
	gate.variance_reset_boost = 0.0;
	gate.current_luma = 0.0;
	gate.history_luma = 0.0;
	gate.hard_reset = hard_reset;
	return gate;
}

AsvgfGateState makeNeutralGate() {
	return makeGate(1.0, false);
}

AsvgfGateState makeHardResetGate() {
	return makeGate(0.0, true);
}

AsvgfLobeContext makeLobeContext(vec3 current_color, vec3 history_color, LuminanceStats current_stats, LuminanceStats history_stats, float history_samples, bool history_valid) {
	AsvgfLobeContext ctx;
	ctx.current_color = max(current_color, vec3(0.0));
	ctx.history_color = max(history_color, vec3(0.0));
	ctx.current_stats = current_stats;
	ctx.history_stats = history_stats;
	ctx.history_samples = max(history_samples, 0.0);
	ctx.history_valid = history_valid;
	return ctx;
}

AsvgfLobeReprojectionInput makeLobeReprojectionInput(AsvgfLobeContext lobe, AsvgfGateState external_gate) {
	AsvgfLobeReprojectionInput input_value;
	input_value.lobe = lobe;
	input_value.external_gate = external_gate;
	return input_value;
}

float computeVarianceCompatibility(AsvgfReprojectionParams params, float current_luma, LuminanceStats current_stats, float history_luma, LuminanceStats history_stats) {
	float compatibility = 1.0;

	if (params.variance_compatibility_strategy == ASVGF_COMPATIBILITY_LUMA_DELTA) {
		float signal = max(max(current_luma, history_luma), params.variance_compatibility_signal_floor);
		float expected_delta = max(params.variance_compatibility_delta_floor, params.variance_compatibility_luma_scale * signal);
		float mismatch = abs(current_luma - history_luma) / max(expected_delta, 1e-6);
		compatibility = 1.0 - smoothstep(params.variance_compatibility_smooth_min, params.variance_compatibility_smooth_max, mismatch);
	} else {
		float expected_sigma = sqrt(max(max(current_stats.variance, history_stats.variance), 0.0));
		float signal = max(current_stats.mean, history_stats.mean);
		float expected_delta = max(expected_sigma + params.variance_compatibility_luma_scale * signal, params.variance_compatibility_delta_floor);
		float mismatch = max(abs(current_luma - history_luma), abs(current_stats.mean - history_stats.mean)) / max(expected_delta, 1e-6);
		compatibility = 1.0 - smoothstep(params.variance_compatibility_smooth_min, params.variance_compatibility_smooth_max, mismatch);
	}

	return max(clamp(compatibility, 0.0, 1.0), clamp(params.variance_compatibility_floor, 0.0, 1.0));
}

float softenVarianceCompatibility(AsvgfReprojectionParams params, float compatibility) {
	if (compatibility <= params.variance_soften_threshold) {
		return 0.0;
	}
	return mix(1.0, compatibility, params.variance_soften_mix);
}

float computeLumaDeltaGate(AsvgfReprojectionParams params, float current_luma, float history_luma) {
	float signal = max(max(current_luma, history_luma), params.luma_delta_gate_signal_floor);
	float normalized_delta = abs(current_luma - history_luma) / max(signal + params.luma_delta_gate_denominator_offset, 1e-6);
	return smoothstep(params.luma_delta_gate_smooth_min, params.luma_delta_gate_smooth_max, normalized_delta);
}

float computeVarianceGate(AsvgfReprojectionParams params, LuminanceStats current_stats, LuminanceStats history_stats, float current_luma, float history_luma) {
	float current_sigma = sqrt(max(current_stats.variance, 0.0));
	float history_sigma = sqrt(max(history_stats.variance, 0.0));
	float sigma = max(current_sigma, history_sigma);
	float signal = max(max(current_stats.mean, history_stats.mean), params.analytical_variance_signal_floor);
	float normalized_sigma = sigma / (signal + 1e-4);
	float stats_gate = smoothstep(params.variance_gate_min, params.variance_gate_max, normalized_sigma);
	float luma_gate = computeLumaDeltaGate(params, current_luma, history_luma);
	float gated = mix(stats_gate, max(stats_gate, luma_gate), clamp(params.luma_delta_gate_mix, 0.0, 1.0));
	return max(gated, clamp(params.variance_gate_floor, 0.0, 1.0));
}

float applyVarianceSoftReset(AsvgfReprojectionParams params, float reset_factor, float variance_gate) {
	float gate = clamp(variance_gate, 0.0, 1.0);
	float scaled_reset = reset_factor * mix(params.reset_min_scale, 1.0, gate);
	return clamp(scaled_reset, 0.0, 1.0);
}

float computeEffectiveHistorySamples(AsvgfReprojectionParams params, float history_samples, float reset_factor) {
	float clamped_reset_factor = clamp(reset_factor, 0.0, 1.0);
	float history_samples_max = max(params.history_samples_max, 1.0);
	float current_weight_min = clamp(params.history_current_weight_min, 1e-4, 1.0);
	float max_effective_history_samples = min(
		history_samples_max - 1.0,
		(1.0 / current_weight_min) - 1.0);
	return min(max(history_samples, 0.0) * clamped_reset_factor, max_effective_history_samples);
}

float computeAnalyticalVarianceCompatibility(AsvgfReprojectionParams params, LuminanceStats current_stats, LuminanceStats history_stats, float history_samples, float reset_factor) {
	float effective_history_samples = computeEffectiveHistorySamples(params, history_samples, reset_factor);
	float total_samples = effective_history_samples + 1.0;
	float history_mix = effective_history_samples / total_samples;
	float current_mix = 1.0 / total_samples;

	float current_variance = max(current_stats.variance, 0.0);
	float history_variance = max(history_stats.variance, 0.0);
	float expected_variance = history_mix * history_mix * history_variance + current_mix * current_mix * current_variance;
	float mean_delta = current_stats.mean - history_stats.mean;
	float observed_variance = expected_variance + history_mix * current_mix * mean_delta * mean_delta;

	float signal = max(max(current_stats.mean, history_stats.mean), params.analytical_variance_signal_floor);
	float variance_floor = pow(params.analytical_variance_floor_base + params.analytical_variance_floor_signal_scale * signal, 2.0);
	float variance_ratio = (observed_variance + variance_floor) / (expected_variance + variance_floor);
	float ratio_compatibility = 1.0 - smoothstep(params.analytical_variance_ratio_min, params.analytical_variance_ratio_max, variance_ratio);

	float delta_sigma = abs(mean_delta) / sqrt(max(current_variance + history_variance + variance_floor, 1e-8));
	float mean_compatibility = 1.0 - smoothstep(params.analytical_variance_mean_min, params.analytical_variance_mean_max, delta_sigma);
	return clamp(min(ratio_compatibility, mean_compatibility), 0.0, 1.0);
}

LuminanceStats accumulateHistoryStats(AsvgfReprojectionParams params, LuminanceStats current_stats, LuminanceStats history_stats, float history_samples, float reset_factor) {
	float effective_history_samples = computeEffectiveHistorySamples(params, history_samples, reset_factor);
	float total_samples = effective_history_samples + 1.0;
	float history_mix = effective_history_samples / total_samples;
	float current_mix = 1.0 / total_samples;

	LuminanceStats accumulated_stats;
	accumulated_stats.mean = mix(current_stats.mean, history_stats.mean, history_mix);
	accumulated_stats.variance =
		history_mix * history_mix * max(history_stats.variance, 0.0)
		+ current_mix * current_mix * max(current_stats.variance, 0.0);
	return accumulated_stats;
}

vec4 accumulateHistoryRadiance(AsvgfReprojectionParams params, vec3 current, vec3 history, float history_samples, float reset_factor) {
	float effective_history_samples = computeEffectiveHistorySamples(params, history_samples, reset_factor);
	float total_samples = effective_history_samples + 1.0;
	float history_mix = effective_history_samples / total_samples;
	vec3 accumulated = mix(current, history, history_mix);
	return vec4(accumulated, min(total_samples, max(params.history_samples_max, 1.0)));
}

float computeExternalGateFactor(AsvgfReprojectionParams params, AsvgfGateState external_gate) {
	float base = mix(clamp(params.external_gate_floor, 0.0, 1.0), 1.0, external_gate.reset_factor);
	return mix(1.0, base, clamp(params.external_gate_mix, 0.0, 1.0));
}

AsvgfHistoryFilterResult filterHistory(AsvgfReprojectionParams params, vec3 current_color, vec3 history_color, LuminanceStats current_stats) {
	AsvgfHistoryFilterResult result;
	result.history_color = history_color;
	result.outlier = 0.0;
	result.external_gate_factor = 1.0;
	result.accumulated_luma_cap = 1e20;

	if (params.history_luma_clamp > 0.0) {
		result.history_color = clampSpecularLuminance(result.history_color, params.history_luma_clamp);
	}

	if (params.history_filter_strategy != ASVGF_HISTORY_FILTER_LUMA_OUTLIER_CLAMP) {
		return result;
	}

	float current_sigma = sqrt(max(current_stats.variance, 0.0));
	float noise_norm = current_sigma / (current_stats.mean + max(params.history_outlier_noise_mean_floor, 1e-6));
	float noise_gate = smoothstep(params.history_outlier_noise_gate_min, params.history_outlier_noise_gate_max, noise_norm);

	float envelope_sigma_scale = mix(params.history_outlier_envelope_sigma_min, params.history_outlier_envelope_sigma_max, noise_gate);
	float envelope_mean_scale = mix(params.history_outlier_envelope_mean_min, params.history_outlier_envelope_mean_max, noise_gate);
	float envelope = max(current_sigma * envelope_sigma_scale + envelope_mean_scale * current_stats.mean, params.history_outlier_envelope_min);

	float history_delta = abs(luminance(result.history_color) - current_stats.mean);
	float outlier_lo = mix(params.history_outlier_threshold_low_min, params.history_outlier_threshold_low_max, noise_gate);
	float outlier_hi = mix(params.history_outlier_threshold_high_min, params.history_outlier_threshold_high_max, noise_gate);
	result.outlier = smoothstep(outlier_lo, outlier_hi, history_delta / max(envelope, 1e-6));

	float luma_cap_scale = mix(params.history_outlier_luma_cap_scale_min, params.history_outlier_luma_cap_scale_max, noise_gate);
	float luma_cap = max(current_stats.mean + envelope * luma_cap_scale, params.history_outlier_luma_cap_min);

	if (result.outlier > 0.0) {
		result.history_color = clampSpecularLuminance(result.history_color, luma_cap);
	}

	float attenuation = mix(params.history_outlier_external_gate_attenuation_min, params.history_outlier_external_gate_attenuation_max, noise_gate);
	result.external_gate_factor = clamp(1.0 - attenuation * result.outlier, 0.0, 1.0);

	float accumulated_scale = mix(params.history_outlier_accumulated_luma_scale_min, params.history_outlier_accumulated_luma_scale_max, noise_gate);
	result.accumulated_luma_cap = luma_cap * accumulated_scale;
	return result;
}

vec3 setLuminancePreserveChromaticity(vec3 color, float target_luma) {
	float current_luma = luminance(color);
	if (current_luma <= 1e-6) {
		return color;
	}

	return max(color * (max(target_luma, 0.0) / current_luma), vec3(0.0));
}

AsvgfDeflickerResult applyLobeDeflicker(AsvgfReprojectionParams params, vec3 color, float previous_stabilized_luma, bool reset_memory) {
	AsvgfDeflickerResult result;
	result.color = max(color, vec3(0.0));
	result.stored_luma = luminance(result.color);

	if (params.deflicker_enabled == 0u || reset_memory) {
		return result;
	}

	float previous_luma = max(previous_stabilized_luma, 0.0);
	float current_luma = max(result.stored_luma, 0.0);
	if (previous_luma <= 0.0 || current_luma <= 0.0) {
		result.stored_luma = current_luma;
		return result;
	}

	float signal = max(max(previous_luma, current_luma), max(params.analytical_variance_signal_floor, 1e-6));
	float relative_delta = abs(current_luma - previous_luma) / signal;
	float threshold_min = min(params.deflicker_threshold_min, params.deflicker_threshold_max);
	float threshold_max = max(max(params.deflicker_threshold_min, params.deflicker_threshold_max), threshold_min + 1e-5);
	float accept_new = smoothstep(threshold_min, threshold_max, relative_delta);
	float stabilized_luma = mix(previous_luma, current_luma, accept_new);

	result.color = setLuminancePreserveChromaticity(result.color, stabilized_luma);
	result.stored_luma = stabilized_luma;
	return result;
}

AsvgfLobeReprojectionResult makeFreshLobeResult(vec3 current_color, LuminanceStats current_stats) {
	AsvgfLobeReprojectionResult result;
	result.color = max(current_color, vec3(0.0));
	result.stats = current_stats;
	result.stats.deflicker_luma = luminance(result.color);
	result.history_samples = 1.0;
	result.gate = makeHardResetGate();
	result.gate.current_luma = luminance(result.color);
	result.gate.history_luma = result.gate.current_luma;
	return result;
}

AsvgfLobeReprojectionResult reprojectAsvgfLobe(AsvgfReprojectionParams params, AsvgfLobeReprojectionInput input_value) {
	AsvgfLobeContext lobe = input_value.lobe;
	if (!lobe.history_valid) {
		return makeFreshLobeResult(lobe.current_color, lobe.current_stats);
	}

	float current_luma = luminance(lobe.current_color);
	float raw_history_luma = luminance(lobe.history_color);

	AsvgfHistoryFilterResult history_filter = filterHistory(params, lobe.current_color, lobe.history_color, lobe.current_stats);
	lobe.history_color = history_filter.history_color;

	float history_luma = luminance(lobe.history_color);

	AsvgfGateState gate = makeNeutralGate();
	gate.current_luma = current_luma;
	gate.history_luma = raw_history_luma;

	float external_factor = computeExternalGateFactor(params, input_value.external_gate) * history_filter.external_gate_factor;
	float analytical_seed = clamp(input_value.external_gate.reset_factor * history_filter.external_gate_factor, 0.0, 1.0);

	float variance_compatibility = computeVarianceCompatibility(params, current_luma, lobe.current_stats, history_luma, lobe.history_stats);
	float analytical_compatibility = computeAnalyticalVarianceCompatibility(params, lobe.current_stats, lobe.history_stats, lobe.history_samples, analytical_seed);
	variance_compatibility = min(variance_compatibility, analytical_compatibility);

	float softened_variance_compatibility = softenVarianceCompatibility(params, variance_compatibility);
	float variance_stage = mix(1.0, softened_variance_compatibility, clamp(params.variance_stage_mix, 0.0, 1.0));

	float variance_gate = computeVarianceGate(params, lobe.current_stats, lobe.history_stats, current_luma, history_luma);

	gate.reset_factor = external_factor * variance_stage;
	gate.variance_gate = variance_gate;

	if (input_value.external_gate.hard_reset) {
		gate.hard_reset = true;
		gate.reset_factor = 0.0;
	}

	bool variance_hard_reset =
		(variance_gate >= params.reset_hard_variance_gate)
		&& (variance_compatibility <= params.reset_hard_compatibility);

	if (gate.hard_reset || variance_hard_reset) {
		gate.hard_reset = true;
		gate.reset_factor = 0.0;
	} else {
		gate.reset_factor = applyVarianceSoftReset(params, gate.reset_factor, variance_gate);
		if (gate.reset_factor <= params.reset_hard_factor) {
			gate.hard_reset = true;
			gate.reset_factor = 0.0;
		}
	}

	gate.variance_reset_boost = smoothstep(
		params.variance_reset_boost_smooth_min,
		params.variance_reset_boost_smooth_max,
		variance_gate * (1.0 - gate.reset_factor));

	vec4 accumulated = accumulateHistoryRadiance(params, lobe.current_color, lobe.history_color, lobe.history_samples, gate.reset_factor);

	AsvgfLobeReprojectionResult result;
	result.color = accumulated.rgb;
	if (params.history_filter_strategy == ASVGF_HISTORY_FILTER_LUMA_OUTLIER_CLAMP && history_filter.outlier > 0.0) {
		result.color = clampSpecularLuminance(result.color, history_filter.accumulated_luma_cap);
	}

	bool reset_deflicker = gate.hard_reset;
	AsvgfDeflickerResult deflicker_result = applyLobeDeflicker(params, result.color, lobe.history_stats.deflicker_luma, reset_deflicker);
	result.color = deflicker_result.color;
	result.history_samples = accumulated.a;
	result.stats = accumulateHistoryStats(params, lobe.current_stats, lobe.history_stats, lobe.history_samples, gate.reset_factor);
	result.stats.deflicker_luma = deflicker_result.stored_luma;
	result.gate = gate;
	return result;
}

