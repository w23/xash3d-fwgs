#include "heitz_shadow_config.glsl"
// Heitz, Hill & McGuire, I3D 2018, sections 4.1-4.2.
// Current-frame only. Filter correlated estimates before taking their ratio.
const int HEITZ_MAX_RADIUS = 24;
const float HEITZ_NOISE_TO_SIGMA = 16.0;
const float HEITZ_MAX_SIGMA = 8.0;

float heitzRatioChannel(float shadowed, float unshadowed) {
	// Any positive stored energy can become visible under auto exposure.
	// Clamp before division so tiny denominators cannot overflow the ratio.
	return unshadowed > 0.0 ? clamp(shadowed, 0.0, unshadowed) / unshadowed : 1.0;
}

vec3 heitzRatio(vec3 shadowed, vec3 unshadowed) {
	// No sampled energy is no evidence of occlusion (paper, section 4.2).
	return vec3(heitzRatioChannel(shadowed.r, unshadowed.r),
				heitzRatioChannel(shadowed.g, unshadowed.g),
				heitzRatioChannel(shadowed.b, unshadowed.b));
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
	// Second differences are averaged over line samples. A linear mapping makes
	// sparse but visible texels request a subpixel sigma and remain untouched.
	// A square-root response gives these low-amplitude signals a useful footprint
	// without a discontinuous minimum radius or a noise rejection threshold.
	// The scale/cap are experimental spatial-only settings, not author defaults.
	return min(sqrt(max(noise, vec2(0.0))) * HEITZ_NOISE_TO_SIGMA, vec2(HEITZ_MAX_SIGMA));
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
