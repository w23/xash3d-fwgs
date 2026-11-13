// Reflection radiance history only. Diffuse GI accumulates SH before normal-map shading.
#define GLSL
#include "ray_interop.h"
#undef GLSL
#include "utils.glsl"
float luminance(vec3 rgb) { return dot(rgb, vec3(0.2126, 0.7152, 0.0722)); }
#include "indirect_temporal_config.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D INDIRECT_TEMPORAL_INPUT;
layout(set = 0, binding = 2, rgba16f) uniform readonly image2D INDIRECT_TEMPORAL_HISTORY;
layout(set = 0, binding = 3, rgba32f) uniform readonly image2D INDIRECT_TEMPORAL_STATS;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D INDIRECT_TEMPORAL_OUTPUT;
layout(set = 0, binding = 5, rgba32f) uniform writeonly image2D INDIRECT_TEMPORAL_OUTPUT_STATS;
layout(set = 0, binding = 6, rgba32f) uniform readonly image2D position_t;
layout(set = 0, binding = 7, rgba32f) uniform readonly image2D geometry_prev_position;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D normals_gs;
layout(set = 0, binding = 9, rgba16f) uniform readonly image2D prev_temporal_asvgf_reproj_depth;
layout(set = 0, binding = 10, rgba8) uniform readonly image2D material_rmxx;
layout(set = 0, binding = 11, rgba32f) uniform readonly image2D reflection_direction_pdf;
#define LOAD_REFLECTION_RAY_LENGTH(p) length(imageLoad(reflection_direction_pdf, (p)).xyz)

#define REPROJECTION_LOAD_PREV_DEPTH_META(p) imageLoad(prev_temporal_asvgf_reproj_depth, (p))
#include "indirect_reprojection.glsl"
#include "indirect_temporal_filter.glsl"

const float EDGE_NORMAL_MIN_DOT = 0.98;
const float EDGE_POSITION_MIN_RADIUS = 1.0;
const float EDGE_POSITION_RAY_CONE_SCALE = 3.0;

float computeLinearEdgeWeight(ivec2 p, vec3 center_pos, vec3 center_shading_normal) {
	vec3 sample_shading_normal = normalDecode(imageLoad(normals_gs, p).zw);
	if (dot(sample_shading_normal, center_shading_normal) < EDGE_NORMAL_MIN_DOT) {
		return 0.0;
	}

	vec3 sample_position = imageLoad(position_t, p).xyz;
	vec3 p_diff = sample_position - center_pos;
	vec3 origin = (ubo.ubo.inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float center_depth = max(length(center_pos - origin), 1.0);
	float position_radius = max(
		EDGE_POSITION_MIN_RADIUS,
		center_depth * max(ubo.ubo.ray_cone_width, 1e-6) * EDGE_POSITION_RAY_CONE_SCALE);
	float p_dist2 = dot(p_diff, p_diff) / max(position_radius * position_radius, 1e-6);
	return 1.0 - clamp(p_dist2, 0.0, 1.0);
}

LuminanceStats currentLuminanceStats(ivec2 pix) {
    // Same edge-qualified 3x3 moments as vk_light_cache_indirect/final_atrous.
    // This gathers statistics only; it does not spatially filter radiance.
    float sum = 0.0, squared = 0.0, count = 0.0;
    vec3 center_pos = imageLoad(position_t, pix).xyz;
    vec3 center_shading_normal = normalDecode(imageLoad(normals_gs, pix).zw);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        ivec2 tap = pix + ivec2(x, y);
        if (!isReprojectionTexelInside(tap, ubo.ubo.res)) continue;
        if (computeLinearEdgeWeight(tap, center_pos, center_shading_normal) <= 0.0) continue;
        float value = luminance(imageLoad(INDIRECT_TEMPORAL_INPUT, tap).rgb);
        sum += value;
        squared += value * value;
        count += 1.0;
    }
    float inverse_count = count > 0.0 ? 1.0 / count : 0.0;
    float mean = sum * inverse_count;
    return LuminanceStats(mean, max(squared * inverse_count - mean * mean, 0.0),
        luminance(imageLoad(INDIRECT_TEMPORAL_INPUT, pix).rgb));
}

void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy), res = ubo.ubo.res;
    if (any(greaterThanEqual(pix, res))) return;

    vec3 current = imageLoad(INDIRECT_TEMPORAL_INPUT, pix).rgb;
    LuminanceStats stats = currentLuminanceStats(pix);
    AsvgfLobeReprojectionResult result = makeFreshLobeResult(current, stats);
    bool enabled = (ubo.ubo.renderer_flags & RENDERER_FLAG_DISABLE_REPROJECTION) == 0u;

    vec4 position = imageLoad(position_t, pix);
    if (enabled && position.w > 0.0) {
        AsvgfReprojectionParams params = reflectionTemporalParams();
        vec4 normals = imageLoad(normals_gs, pix);
        vec3 geometry_normal = normalDecode(normals.xy);
        vec3 shading_normal = normalDecode(normals.zw);
        vec3 previous_position = imageLoad(geometry_prev_position, pix).xyz;
        ivec2 taps[4];
        float weights[4];
        int tap_count;
        vec2 projected;
        float depth_threshold;
        bool valid = buildValidatedReprojectionHistoryTapsForParams(params, previous_position,
            geometry_normal, pix, res, taps, weights, tap_count, projected, depth_threshold);

        // Preserve source-branch parallax and previous mirror-plane validation.
        // Fallback is the ordinary validated surface footprint.
        if (valid) {
            float ray_length = sampleAverageReflectionRayLength(pix, res, 1, 1);
            ivec2 parallax;
            if (parallaxReprojectToPrevFramePixel(position.xyz, previous_position, geometry_normal,
                ubo.ubo.inv_view[3].xyz, ubo.ubo.prev_inv_view[3].xyz, ray_length, res, parallax)) {
                vec4 meta = imageLoad(prev_temporal_asvgf_reproj_depth, parallax);
                float old_depth = decodeReprojectionDepth(meta.r);
                float plane_depth;
                if (computePlaneDepthInPrevFrame(parallax, res, previous_position, geometry_normal, plane_depth)) {
                    float threshold = makeReprojectionDepthThresholdForParams(params, plane_depth, old_depth,
                        depth_threshold * params.parallax_depth_threshold_scale);
                    float normal_delta = 1.0 - clamp(dot(shading_normal, normalDecode(meta.ba)), 0.0, 1.0);
                    float roughness_delta = abs(meta.g - imageLoad(material_rmxx, pix).r);
                    if (isValidReprojectionDepth(old_depth) && abs(old_depth - plane_depth) < threshold &&
                        normal_delta < params.parallax_shading_normal_threshold * 2.0 &&
                        roughness_delta < params.parallax_roughness_threshold * 2.0) {
                        taps[0] = parallax;
                        weights[0] = 1.0;
                        tap_count = 1;
                    }
                }
            }
        }
        vec3 history = vec3(0);
        LuminanceStats history_stats = LuminanceStats(0, 0, 0);
        float history_age = 0.0, total = 0.0;
        if (valid) for (int i = 0; i < tap_count; ++i) {
            vec4 sample_value = imageLoad(INDIRECT_TEMPORAL_HISTORY, taps[i]);
            if (!(sample_value.a >= 1.0) || any(isnan(sample_value)) || any(isinf(sample_value))) continue;
            vec4 meta = imageLoad(prev_temporal_asvgf_reproj_depth, taps[i]);
            if (dot(shading_normal, normalDecode(meta.zw)) < 0.9) continue;
            LuminanceStats sample_stats = decodeLuminanceStats(imageLoad(INDIRECT_TEMPORAL_STATS, taps[i]));
            if (isnan(sample_stats.mean) || isinf(sample_stats.mean) ||
                isnan(sample_stats.variance) || isinf(sample_stats.variance)) continue;
            float w = weights[i];
            history += sample_value.rgb * w;
            history_age += sample_value.a * w;
            history_stats.mean += sample_stats.mean * w;
            history_stats.variance += sample_stats.variance * w;
            history_stats.deflicker_luma += sample_stats.deflicker_luma * w;
            total += w;
        }
        if (total > 0.0) {
            history /= total;
            history_age /= total;
            history_stats.mean /= total;
            history_stats.variance /= total;
            history_stats.deflicker_luma /= total;
            result = reprojectAsvgfLobe(params, makeLobeReprojectionInput(
                makeLobeContext(current, history, stats, history_stats, history_age, true), makeNeutralGate()));
        }
    }
    imageStore(INDIRECT_TEMPORAL_OUTPUT, pix, vec4(result.color, result.history_samples));
    imageStore(INDIRECT_TEMPORAL_OUTPUT_STATS, pix, encodeLuminanceStats(result.stats));
}
