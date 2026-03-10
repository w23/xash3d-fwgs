#ifndef LIGHT_WEIGHT_GLSL_INCLUDED
#define LIGHT_WEIGHT_GLSL_INCLUDED

#include "brdf.glsl"
#include "light_common.glsl"

#ifndef EPSILON
#define EPSILON 1e-2
#endif

#ifndef POLYGON_SELF_LIGHT_PLANE_BIAS
#define POLYGON_SELF_LIGHT_PLANE_BIAS 1.0
#endif

#ifndef POLYGON_SELF_LIGHT_FADE_RANGE
#define POLYGON_SELF_LIGHT_FADE_RANGE 2.0
#endif

#ifndef POLYGON_LIGHT_MIN_DENOM
#define POLYGON_LIGHT_MIN_DENOM 1e-4
#endif

#ifndef LIGHT_SPECULAR_MIN_ANGULAR
#define LIGHT_SPECULAR_MIN_ANGULAR 0.0025
#endif

#ifndef LIGHT_SPECULAR_ANGULAR_SCALE
#define LIGHT_SPECULAR_ANGULAR_SCALE 1.0
#endif

#ifndef LIGHT_SPECULAR_ROUGHNESS_FROM_ANGULAR
#define LIGHT_SPECULAR_ROUGHNESS_FROM_ANGULAR 2.0
#endif

#ifndef LIGHT_SPECULAR_GAIN_FROM_ANGULAR
#define LIGHT_SPECULAR_GAIN_FROM_ANGULAR 4.0
#endif

#ifndef LIGHT_SPECULAR_GAIN_MAX
#define LIGHT_SPECULAR_GAIN_MAX 2.0
#endif

#ifndef NON_BRDF_POINT_LIGHTS_MULTIPLIER
#define NON_BRDF_POINT_LIGHTS_MULTIPLIER 2.0
#endif

#ifndef BRIGHTEST_LIGHTS_PER_TEXEL
#define BRIGHTEST_LIGHTS_PER_TEXEL 4
#endif

struct BrightestLightEntry {
    uint index;
    float luminance;
};

void initBrightestLights(out BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL])
{
    for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
        brightest[i].index = 0xffffffffu;
        brightest[i].luminance = -1.0;
    }
}

void updateBrightestLights(
    vec3 diffuse,
    vec3 specular,
    uint light_index,
    inout BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL])
{
    float light_luminance = luminance(diffuse + specular);
    if (light_luminance <= 0.0) {
        return;
    }

    int dimmest_index = 0;
    float dimmest_luminance = brightest[0].luminance;

    for (int i = 1; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
        if (brightest[i].luminance < dimmest_luminance) {
            dimmest_index = i;
            dimmest_luminance = brightest[i].luminance;
        }
    }

    if (light_luminance > dimmest_luminance) {
        brightest[dimmest_index].index = light_index;
        brightest[dimmest_index].luminance = light_luminance;
    }
}


float packBrightnessEntry(BrightestLightEntry entry)
{
	if (entry.index == 0xffffffffu || entry.luminance <= 0.0) {
		return -1.0;
	}

	float packed_luminance = min(floor(entry.luminance * 100.0), 99.0) / 100.0;
	return float(entry.index) + packed_luminance;
}

BrightestLightEntry unpackBrightnessEntry(float packed)
{
	BrightestLightEntry entry;
	if (packed < 0.0) {
		entry.index = 0xffffffffu;
		entry.luminance = -1.0;
		return entry;
	}

	entry.index = uint(floor(packed));
	entry.luminance = fract(packed) * 100.0;
	return entry;
}

vec4 packBrightestLights(BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	return vec4(
		packBrightnessEntry(brightest[0]),
		packBrightnessEntry(brightest[1]),
		packBrightnessEntry(brightest[2]),
		packBrightnessEntry(brightest[3]));
}

void unpackBrightestLights(vec4 packed, out BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	brightest[0] = unpackBrightnessEntry(packed.x);
	brightest[1] = unpackBrightnessEntry(packed.y);
	brightest[2] = unpackBrightnessEntry(packed.z);
	brightest[3] = unpackBrightnessEntry(packed.w);
}

vec4 packTemporalNormalRoughness(vec3 shading_normal, float roughness)
{
	return vec4(shading_normal, roughness);
}

void unpackTemporalNormalRoughness(vec4 packed, out vec3 shading_normal, out float roughness)
{
	shading_normal = packed.xyz;
	roughness = packed.w;
}

vec2 lightPointWeightCalculation(
	PointLight pl,
	vec3 P, vec3 N, vec3 V,
	float roughness);

vec2 lightPolygonWeightCalculation(
	PolygonLight poly,
	vec3 P, vec3 N, vec3 V,
	float roughness);

vec2 lightWeightFromIndex(uint light_index, vec3 P, vec3 N, vec3 V, float roughness)
{
#if LIGHT_POINT
	if (light_index >= lights.m.num_point_lights) {
		return vec2(0.0);
	}
	return lightPointWeightCalculation(lights.m.point_lights[light_index], P, N, V, roughness);
#elif LIGHT_POLYGON
	if (light_index >= lights.m.num_polygons) {
		return vec2(0.0);
	}
	return lightPolygonWeightCalculation(lights.m.polygons[light_index], P, N, V, roughness);
#else
	return vec2(0.0);
#endif
}

float lightWeightFromEntry(BrightestLightEntry entry, vec3 P, vec3 N, vec3 V, float roughness)
{
	if (entry.index == 0xffffffffu || entry.luminance <= 0.0) {
		return 0.0;
	}

	vec2 weight = lightWeightFromIndex(entry.index, P, N, V, roughness);
	return weight.x + weight.y;
}

void processTemporalLightEntries(
	BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL],
	vec3 P,
	vec3 N,
	vec3 V,
	float roughness,
	out float weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		weights[i] = lightWeightFromEntry(brightest[i], P, N, V, roughness);
	}
}

int findBrightestLightEntry(BrightestLightEntry brightest[BRIGHTEST_LIGHTS_PER_TEXEL], uint light_index)
{
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		if (brightest[i].index == light_index) {
			return i;
		}
	}

	return -1;
}

float computeTemporalConfidence(
	BrightestLightEntry current_brightest[BRIGHTEST_LIGHTS_PER_TEXEL],
	BrightestLightEntry prev_brightest[BRIGHTEST_LIGHTS_PER_TEXEL],
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	float weighted_confidence = 0.0;
	float total_weight = 0.0;

	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		BrightestLightEntry current = current_brightest[i];
		if (current.index == 0xffffffffu || current.luminance <= 0.0) {
			continue;
		}

		int prev_index = findBrightestLightEntry(prev_brightest, current.index);
		if (prev_index < 0) {
			continue;
		}

		float confidence = 1.0 / (1.0 + abs(current_weights[i] - prev_weights[prev_index]));
		float weight = max(current.luminance, 1e-4);
		weighted_confidence += confidence * weight;
		total_weight += weight;
	}

	return total_weight > 0.0 ? weighted_confidence / total_weight : 0.0;
}

#ifndef POLYGON_LIGHT_SAMPLE_NORMAL_EPSILON
#define POLYGON_LIGHT_SAMPLE_NORMAL_EPSILON 1e-3
#endif

vec4 normalizedPolygonPlane(const PolygonLight poly) {
	const float nlen = max(length(poly.plane.xyz), 1e-6);
	return vec4(poly.plane.xyz / nlen, poly.plane.w / nlen);
}

vec3 normalizedPolygonNormal(const PolygonLight poly) {
	return normalizedPolygonPlane(poly).xyz;
}
float specularWeight(vec3 N, vec3 L, vec3 V, float roughness)
{
    vec3 H = normalize(L + V);
    float NoH = max(dot(N, H), 0.0);
    float power = mix(128.0, 4.0, roughness * roughness);
    return pow(NoH, power);
}

float computeSpecularAngularRadius(float source_extent, float dist)
{
    float angular = source_extent / max(abs(dist), EPSILON);
    angular = max(angular * LIGHT_SPECULAR_ANGULAR_SCALE, LIGHT_SPECULAR_MIN_ANGULAR);
    return angular;
}

float computeSpecularCompensation(float angular_radius)
{
    return clamp(1.0 + angular_radius * LIGHT_SPECULAR_GAIN_FROM_ANGULAR, 1.0, LIGHT_SPECULAR_GAIN_MAX);
}

vec2 lightPointWeightCalculation(
    PointLight pl,
    vec3 P, vec3 N, vec3 V,
    float roughness)
{
    vec2 result = vec2(0.0);

    vec3 L;
    float geom_weight;
    float spec_angular_radius;

    if (pl.environment != 0) {
        L = pl.dir_stopdot2.xyz;
        geom_weight = 2.0 * kPi * (1.0 - pl.dir_stopdot2.a) * NON_BRDF_POINT_LIGHTS_MULTIPLIER;

        float cone_spread = sqrt(max(1.0 - pl.dir_stopdot2.a * pl.dir_stopdot2.a, 0.0));
        spec_angular_radius = max(LIGHT_SPECULAR_MIN_ANGULAR, cone_spread * LIGHT_SPECULAR_ANGULAR_SCALE);
    } else {
        vec3 toL = pl.origin_r2.xyz - P;
        float dist2 = max(dot(toL, toL), EPSILON);
        float inv_dist = inversesqrt(dist2);
        L = toL * inv_dist;

        float spot_dot = dot(L, pl.dir_stopdot2.xyz);
        float stopdot2 = pl.dir_stopdot2.a;
        float stopdot = pl.color_stopdot.a;
        float spot_att = (spot_dot < stopdot) ? max(0.0, (spot_dot - stopdot2) / (stopdot - stopdot2)) : 1.0;
        float radius_ratio = sqrt(max(0.0, 1.0 - pl.origin_r2.w / dist2));
        geom_weight = 2.0 * kPi * (1.0 - radius_ratio) * spot_att * NON_BRDF_POINT_LIGHTS_MULTIPLIER;

        float source_radius = sqrt(max(pl.origin_r2.w, 0.0));
        spec_angular_radius = max(LIGHT_SPECULAR_MIN_ANGULAR, source_radius * inv_dist * LIGHT_SPECULAR_ANGULAR_SCALE);
    }

    if (geom_weight > 0.0) {
        float roughness_for_spec = clamp(roughness + spec_angular_radius * LIGHT_SPECULAR_ROUGHNESS_FROM_ANGULAR, 0.0, 1.0);
        float light_weight = geom_weight * luminance(pl.color_stopdot.rgb);
        float spec_weight = specularWeight(N, L, V, roughness_for_spec);
        result = vec2(light_weight, light_weight * spec_weight * computeSpecularCompensation(spec_angular_radius));
    }

    return result;
}

vec2 lightPolygonWeightCalculation(
    PolygonLight poly,
    vec3 P, vec3 N, vec3 V,
    float roughness)
{
    vec2 result = vec2(0.0);

    const vec4 plane = normalizedPolygonPlane(poly);
    const float plane_dist = dot(plane, vec4(P, 1.0));

    if (plane_dist > POLYGON_SELF_LIGHT_PLANE_BIAS) {
        vec3 dir = poly.center + plane.xyz * POLYGON_LIGHT_SAMPLE_NORMAL_EPSILON - P;
        float dist2 = max(dot(dir, dir), 1e-6);
        vec3 L = dir * inversesqrt(dist2);
        float denom = dot(L, plane.xyz);

        if (denom < -POLYGON_LIGHT_MIN_DENOM) {
            float geom_weight = poly.area * max(-denom, 0.0) * (0.4 / dist2);
            geom_weight *= smoothstep(
                POLYGON_SELF_LIGHT_PLANE_BIAS,
                POLYGON_SELF_LIGHT_PLANE_BIAS + POLYGON_SELF_LIGHT_FADE_RANGE,
                plane_dist);

            if (geom_weight > 0.0) {
                float dist = max(0.0, -plane_dist / denom);
                float spec_angular_radius = computeSpecularAngularRadius(sqrt(max(poly.area, 0.0) * (1.0 / kPi)), dist);
                float roughness_for_spec = clamp(roughness + spec_angular_radius * LIGHT_SPECULAR_ROUGHNESS_FROM_ANGULAR, 0.0, 1.0);
                float light_weight = geom_weight * luminance(poly.emissive);
                float spec_weight = specularWeight(N, L, V, roughness_for_spec);
                result = vec2(light_weight, light_weight * spec_weight * computeSpecularCompensation(spec_angular_radius));
            }
        }
    }

    return result;
}

#endif // LIGHT_WEIGHT_GLSL_INCLUDED


