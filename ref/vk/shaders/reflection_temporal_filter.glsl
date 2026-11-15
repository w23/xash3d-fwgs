// Current-frame statistics only. RGB contrast controls compatibility/clipping;
// roughness and relative hit-length spread select the accumulation strength.
struct ReflectionNeighborhood {
    vec3 mean;
    vec3 variance;
    vec3 lower;
    vec3 upper;
    float luma_variance;
    float support;
    float length_mean;
    float length_variance;
    float hit_fraction;
};

ReflectionNeighborhood reflectionNeighborhood(ivec2 pix, vec3 current) {
    ReflectionNeighborhood stats;
    stats.mean = vec3(0.0);
    stats.variance = vec3(0.0);
    stats.lower = current;
    stats.upper = current;
    stats.luma_variance = 0.0;
    stats.support = 0.0;
    stats.length_mean = 0.0;
    stats.length_variance = 0.0;
    stats.hit_fraction = 0.0;
    float length_weight = 0.0;
    float center_length = decodeReflectionLength(imageLoad(INDIRECT_TEMPORAL_INPUT, pix).a);
    vec3 center_pos = imageLoad(position_t, pix).xyz;
    vec3 center_normal = normalDecode(imageLoad(normals_gs, pix).zw);
    float center_roughness = imageLoad(material_rmxx, pix).r;
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        ivec2 tap = pix + ivec2(x, y);
        if (!isReprojectionTexelInside(tap, ubo.ubo.res)) continue;
        float w = computeLinearEdgeWeight(tap, center_pos, center_normal);
        float roughness_delta = abs(imageLoad(material_rmxx, tap).r - center_roughness);
        w *= 1.0 - smoothstep(0.02, 0.10, roughness_delta);
        if (!(w > 0.0)) continue;
        vec4 value = imageLoad(INDIRECT_TEMPORAL_INPUT, tap);
        if (any(isnan(value.rgb)) || any(isinf(value.rgb))) continue;
        vec3 color = max(value.rgb, vec3(0.0));
        stats.mean += color * w;
        stats.variance += color * color * w;
        stats.lower = min(stats.lower, color);
        stats.upper = max(stats.upper, color);
        float luma = luminance(color);
        stats.luma_variance += luma * luma * w;
        stats.support += w;
        float ray_length = decodeReflectionLength(value.a);
        if (reflectionLengthValid(ray_length)) stats.hit_fraction += w;
        // A miss at the center stays a miss. Never average zero/miss distances
        // into valid hits, or let neighboring hits turn a miss into a hit.
        if (center_length > 0.0 && ray_length > 0.0) {
            stats.length_mean += ray_length * w;
            stats.length_variance += ray_length * ray_length * w;
            length_weight += w;
        }
    }
    if (stats.support > 0.0) {
        stats.mean /= stats.support;
        stats.hit_fraction /= stats.support;
        stats.variance = max(stats.variance / stats.support - stats.mean * stats.mean, vec3(0.0));
        float mean_luma = luminance(stats.mean);
        stats.luma_variance = max(stats.luma_variance / stats.support - mean_luma * mean_luma, 0.0);
    } else {
        stats.mean = current;
    }
    if (length_weight > 0.0) {
        stats.length_mean /= length_weight;
        stats.length_variance = max(stats.length_variance / length_weight -
            stats.length_mean * stats.length_mean, 0.0);
    }
    return stats;
}

float reflectionAccumulationStrength(ReflectionNeighborhood stats, float roughness) {
    float rough = smoothstep(REFLECTION_ACCUMULATION_ROUGHNESS_START,
        REFLECTION_ACCUMULATION_ROUGHNESS_END, roughness);
    float relative_spread = sqrt(stats.length_variance) /
        max(stats.length_mean, REFLECTION_LENGTH_SPREAD_MEAN_FLOOR);
    float scattered = reflectionLengthValid(stats.length_mean)
        ? smoothstep(REFLECTION_LENGTH_SPREAD_START, REFLECTION_LENGTH_SPREAD_END, relative_spread) : 0.0;
    // Length spread is useful even at roughness == 0. A smooth mirror with
    // coherent hit lengths keeps the short history; dispersed hits do not.
    return max(rough, scattered);
}

float reflectionLengthGate(ReflectionNeighborhood stats, float previous_length, float roughness,
    float previous_hit_probability) {
    float mirror = 1.0 - smoothstep(REFLECTION_LENGTH_ROUGHNESS_FADE_START,
        REFLECTION_LENGTH_ROUGHNESS_FADE_END, roughness);
    // Dispersed rays are not a coherent virtual surface, even at low roughness.
    mirror *= 1.0 - reflectionAccumulationStrength(stats, roughness);
    if (mirror <= 0.0) return 1.0;
    bool current_hit = reflectionLengthValid(stats.length_mean);
    bool previous_hit = reflectionLengthValid(previous_length);
    float rejection = 0.0;
    if (current_hit != previous_hit) {
        // A stable sky/geometry boundary still rejects history. Mixed spatial
        // samples or temporal hit/miss flicker indicate stochastic visibility.
        float current_probability = current_hit ? stats.hit_fraction : 1.0 - stats.hit_fraction;
        float previous_probability = previous_hit ? previous_hit_probability : 1.0 - previous_hit_probability;
        float current_certainty = max(2.0 * current_probability - 1.0, 0.0);
        float previous_certainty = max(2.0 * previous_probability - 1.0, 0.0);
        rejection = min(current_certainty, previous_certainty);
    } else if (current_hit) {
        float tolerance = max(REFLECTION_LENGTH_RELATIVE_TOLERANCE * stats.length_mean,
            REFLECTION_LENGTH_SIGMA_SCALE * sqrt(stats.length_variance) +
            REFLECTION_LENGTH_ABSOLUTE_TOLERANCE);
        float mismatch = abs(stats.length_mean - previous_length) / tolerance;
        rejection = smoothstep(1.0, 3.0, mismatch);
    }
    return 1.0 - REFLECTION_LENGTH_GATE_STRENGTH * mirror * rejection;
}

vec3 filterReflectionHistory(vec3 current, vec3 history, ReflectionNeighborhood stats,
    vec3 temporal_variance, float roughness, float reprojection_confidence, float motion)
{
    vec3 signal = max(current, stats.mean);
    vec3 floor_delta = REFLECTION_SIGNAL_ABSOLUTE_FLOOR + REFLECTION_SIGNAL_RELATIVE_FLOOR * signal;
    vec3 temporal_sigma = sqrt(max(temporal_variance, vec3(0.0)));
    vec3 relative_noise = temporal_sigma / max(signal, vec3(REFLECTION_SIGNAL_ABSOLUTE_FLOOR));
    float noise = smoothstep(REFLECTION_TEMPORAL_NOISE_RELATIVE_START,
        REFLECTION_TEMPORAL_NOISE_RELATIVE_END, max(relative_noise.r, max(relative_noise.g, relative_noise.b)));
    float accumulation = max(reflectionAccumulationStrength(stats, roughness), noise);
    vec3 sigma = sqrt(max(stats.variance, temporal_variance));

    // Compare with the neighborhood estimate rather than one noisy sample.
    // Only variance from earlier frames is used here: the current innovation
    // must not excuse its own mismatch on a real lighting change.
    vec3 reference = mix(current, stats.mean, accumulation);
    vec3 mismatch = abs(history - reference) / (mix(1.5, 2.5, accumulation) * sigma + floor_delta);
    float signal_confidence = 1.0 - smoothstep(1.0, 3.0, max(mismatch.r, max(mismatch.g, mismatch.b)));
    float support_confidence = clamp((stats.support - 1.0) / 3.0, 0.0, 1.0);
    float motion_confidence = mix(1.0, mix(0.5, 0.8, accumulation), smoothstep(0.5, 4.0, motion));
    float history_max = mix(REFLECTION_HISTORY_WEIGHT_MAX, REFLECTION_SCATTERED_HISTORY_WEIGHT_MAX, accumulation);
    float history_weight = history_max * signal_confidence * support_confidence *
        motion_confidence * clamp(reprojection_confidence, 0.0, 1.0);

    // Intersect the variance box with neighborhood min/max, then include the
    // current center so a genuine narrow highlight is always preserved.
    float clip_sigma = mix(REFLECTION_CLIP_SIGMA, REFLECTION_SCATTERED_CLIP_SIGMA, accumulation);
    // A small allowance lets correlated grain settle even when the whole 3x3
    // fluctuates together. Coherent mirrors retain the original tight box.
    vec3 allowance = accumulation * (floor_delta + temporal_sigma);
    vec3 lower = min(current, max(stats.lower - allowance, stats.mean - clip_sigma * sigma - allowance));
    vec3 upper = max(current, min(stats.upper + allowance, stats.mean + clip_sigma * sigma + allowance));
    vec3 delta = history - current;
    vec3 extent = mix(current - lower, upper - current, greaterThanEqual(delta, vec3(0.0)));
    vec3 ratios = extent / max(abs(delta), vec3(1e-8));
    // Zero-delta channels must not constrain the color-segment intersection.
    for (int i = 0; i < 3; ++i) {
        if (abs(delta[i]) <= 1e-8) ratios[i] = 1.0;
    }
    float clip_factor = min(1.0, min(ratios.r, min(ratios.g, ratios.b)));
    vec3 correction = history_weight * clip_factor * delta;
    float relative_budget = mix(REFLECTION_CORRECTION_RELATIVE_MAX,
        REFLECTION_SCATTERED_CORRECTION_RELATIVE_MAX, accumulation);
    // A relative-only cap also prevents averaging when the estimator's noise
    // exceeds that cap. Let measured noise supply the additional correction.
    vec3 budget = REFLECTION_SIGNAL_ABSOLUTE_FLOOR + relative_budget * signal +
        accumulation * temporal_sigma;
    // Bound the correction without changing its color direction.
    vec3 budget_ratios = budget / max(abs(correction), vec3(1e-8));
    correction *= min(1.0, min(budget_ratios.r, min(budget_ratios.g, budget_ratios.b)));
    return max(current + correction, vec3(0.0));
}
