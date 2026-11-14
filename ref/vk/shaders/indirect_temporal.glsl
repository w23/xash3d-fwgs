// Reflection radiance history only. Diffuse GI accumulates SH before normal-map shading.
#define GLSL
#include "ray_interop.h"
#undef GLSL
#include "utils.glsl"
float luminance(vec3 rgb) { return dot(rgb, vec3(0.2126, 0.7152, 0.0722)); }
#include "indirect_temporal_config.glsl"
#include "reflection_temporal_config.glsl"

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

const float EDGE_NORMAL_MIN_DOT = 0.98;
const float EDGE_POSITION_MIN_RADIUS = 1.0;
const float EDGE_POSITION_RAY_CONE_SCALE = 3.0;

float computeLinearEdgeWeight(ivec2 p, vec3 center_pos, vec3 center_shading_normal) {
	vec3 sample_shading_normal = normalDecode(imageLoad(normals_gs, p).zw);
	if (dot(sample_shading_normal, center_shading_normal) < EDGE_NORMAL_MIN_DOT) {
		return 0.0;
	}

	vec4 sample_position = imageLoad(position_t, p);
	if (!(sample_position.w > 0.0)) return 0.0;
	vec3 p_diff = sample_position.xyz - center_pos;
	vec3 origin = (ubo.ubo.inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float center_depth = max(length(center_pos - origin), 1.0);
	float position_radius = max(
		EDGE_POSITION_MIN_RADIUS,
		center_depth * max(ubo.ubo.ray_cone_width, 1e-6) * EDGE_POSITION_RAY_CONE_SCALE);
	float p_dist2 = dot(p_diff, p_diff) / max(position_radius * position_radius, 1e-6);
	return 1.0 - clamp(p_dist2, 0.0, 1.0);
}

#include "reflection_temporal_filter.glsl"

void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy), res = ubo.ubo.res;
    if (any(greaterThanEqual(pix, res))) return;

    vec3 current = imageLoad(INDIRECT_TEMPORAL_INPUT, pix).rgb;
    bool finite_current = !any(isnan(current)) && !any(isinf(current));
    current = finite_current ? max(current, vec3(0.0)) : vec3(0.0);
    ReflectionNeighborhood stats = reflectionNeighborhood(pix, current);
    vec3 result = current;
    bool enabled = finite_current && (ubo.ubo.renderer_flags & RENDERER_FLAG_DISABLE_REPROJECTION) == 0u;

    vec4 position = imageLoad(position_t, pix);
    if (enabled && position.w > 0.0) {
        AsvgfReprojectionParams params = reflectionTemporalParams();
        vec4 normals = imageLoad(normals_gs, pix);
        vec3 geometry_normal = normalDecode(normals.xy);
        vec3 shading_normal = normalDecode(normals.zw);
        float roughness = imageLoad(material_rmxx, pix).r;
        vec3 previous_position = imageLoad(geometry_prev_position, pix).xyz;
        ivec2 taps[4];
        float weights[4];
        int tap_count;
        vec2 projected;
        float depth_threshold;
        bool valid = buildValidatedReprojectionHistoryTapsForParams(params, previous_position,
            geometry_normal, pix, res, taps, weights, tap_count, projected, depth_threshold);

        // Prefer reflected motion. Surface fallback is less trustworthy for
        // mirrors, even when depth/normal validation succeeds.
        bool parallax_valid = false;
        if (valid) {
            float ray_length = stats.length_mean;
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
                    float roughness_delta = abs(meta.g - roughness);
                    if (isValidReprojectionDepth(old_depth) && abs(old_depth - plane_depth) < threshold &&
                        normal_delta < params.parallax_shading_normal_threshold * 2.0 &&
                        roughness_delta < params.parallax_roughness_threshold * 2.0) {
                        taps[0] = parallax;
                        weights[0] = 1.0;
                        tap_count = 1;
                        parallax_valid = true;
                    }
                }
            }
        }
        vec3 history = vec3(0);
        float previous_length = 0.0, length_weight = 0.0, total = 0.0;
        float history_motion = 0.0;
        if (valid) for (int i = 0; i < tap_count; ++i) {
            vec4 sample_value = imageLoad(INDIRECT_TEMPORAL_HISTORY, taps[i]);
            if (!(sample_value.a >= 1.0) || any(isnan(sample_value)) || any(isinf(sample_value))) continue;
            vec4 meta = imageLoad(prev_temporal_asvgf_reproj_depth, taps[i]);
            if (dot(shading_normal, normalDecode(meta.zw)) < 0.9) continue;
            if (abs(meta.g - roughness) >= 0.1) continue;
            vec4 sample_stats = imageLoad(INDIRECT_TEMPORAL_STATS, taps[i]);
            // .w tags the new raw-statistics layout, rejecting legacy ASVGF
            // history on shader reload. .z is mean metric hit length, 0 = miss.
            if (sample_stats.w != -1.0 || any(isnan(sample_stats)) || any(isinf(sample_stats))) continue;
            float w = weights[i];
            history += sample_value.rgb * w;
            if (reflectionLengthValid(sample_stats.z)) {
                previous_length += sample_stats.z * w;
                length_weight += w;
            }
            history_motion += length(vec2(taps[i] - pix)) * w;
            total += w;
        }
        if (total > 0.0) {
            history /= total;
            if (length_weight > 0.0) previous_length /= length_weight;
            float mirror = 1.0 - smoothstep(REFLECTION_LENGTH_ROUGHNESS_FADE_START,
                REFLECTION_LENGTH_ROUGHNESS_FADE_END, roughness);
            float confidence = total * (parallax_valid ? 1.0 : mix(1.0, 0.5, mirror));
            result = filterReflectionHistory(current, history, stats, previous_length,
                roughness, confidence, history_motion / total);
        }
    }
    // Alpha is validity, not an accumulating sample count. Geometry metadata
    // always describes this frame's reconstruction, never the temporal color.
    imageStore(INDIRECT_TEMPORAL_OUTPUT, pix, vec4(result, finite_current ? 1.0 : 0.0));
    imageStore(INDIRECT_TEMPORAL_OUTPUT_STATS, pix,
        vec4(luminance(stats.mean), stats.luma_variance, stats.length_mean, -1.0));
}
