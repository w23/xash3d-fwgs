#include "heitz_shadow_config.glsl"
// Heitz, Hill & McGuire, I3D 2018, sections 4.1-4.2.
// Current-frame only. Filter correlated estimates before taking their ratio.
const int HEITZ_MAX_RADIUS = 24;
const float HEITZ_NOISE_TO_SIGMA = 16.0;
const float HEITZ_MAX_SIGMA = 8.0;
const float HEITZ_EPSILON = 1e-7;

vec3 heitzRatio(vec3 shadowed, vec3 unshadowed) {
	// No sampled energy is no evidence of occlusion (paper, section 4.2).
	return vec3(unshadowed.r > HEITZ_EPSILON ? clamp(shadowed.r / unshadowed.r, 0.0, 1.0) : 1.0,
				unshadowed.g > HEITZ_EPSILON ? clamp(shadowed.g / unshadowed.g, 0.0, 1.0) : 1.0,
				unshadowed.b > HEITZ_EPSILON ? clamp(shadowed.b / unshadowed.b, 0.0, 1.0) : 1.0);
}

bool heitzInside(ivec2 p, ivec2 res) {
	return all(greaterThanEqual(p, ivec2(0))) && all(lessThan(p, res));
}

float heitzGeometryWeight(vec4 center, vec3 normal, ivec2 p) {
	const vec4 tap = imageLoad(position_t, p);
	if (tap.w <= 0.0) return 0.0;
	const vec3 tap_normal = normalDecode(imageLoad(normals_gs, p).xy);
	const vec3 delta = tap.xyz - center.xyz;
	// Symmetric tangent-plane distances prevent leaking between parallel surfaces.
	// Geometric normals allow normal-map shading to remain sharp in analytic U.
	const float plane = max(abs(dot(delta, normal)), abs(dot(delta, tap_normal)));
	const float tolerance = max(0.01, center.w * 0.001);
	return exp(-plane * plane / (tolerance * tolerance))
		* pow(max(0.0, dot(normal, tap_normal)), 32.0);
}

vec2 heitzSigma(vec2 noise) {
	// Paper section 4.1: sigma is proportional to E. Do not subtract a noise
	// threshold: sparse penumbra samples must not be classified as noise-free.
	// The scale/cap are experimental spatial-only settings, not author defaults.
	return clamp(noise * HEITZ_NOISE_TO_SIGMA, vec2(0.0), vec2(HEITZ_MAX_SIGMA));
}

vec3 heitzFilterEncode(vec3 value) {
#if HEITZ_GAMMA_FILTER
    return pow(max(value, vec3(0.0)), vec3(1.0 / HEITZ_FILTER_GAMMA));
#else
    return value;
#endif
}
vec3 heitzFilterDecodeRatio(vec3 numerator, vec3 denominator) {
    vec3 ratio = heitzRatio(numerator, denominator);
#if HEITZ_GAMMA_FILTER
    // For a pure power encoding the common filter normalization cancels.
    return pow(ratio, vec3(HEITZ_FILTER_GAMMA));
#else
    return ratio;
#endif
}
