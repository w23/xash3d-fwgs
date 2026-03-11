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
#define BRIGHTEST_LIGHTS_PER_TEXEL 8
#endif

#ifndef DISABLE_BRIGHTEST_LIGHTS_TRACKING
#define DISABLE_BRIGHTEST_LIGHTS_TRACKING 0
#endif

#ifndef TEMPORAL_CONFIDENCE_INCLUDE_BRIGHTNESS_SUM_DIFF
#define TEMPORAL_CONFIDENCE_INCLUDE_BRIGHTNESS_SUM_DIFF 0
#endif

#ifndef TEMPORAL_CONFIDENCE_INCLUDE_PREV_LIGHTS_SUM_DIFF
#define TEMPORAL_CONFIDENCE_INCLUDE_PREV_LIGHTS_SUM_DIFF 0
#endif

struct BrightestLightEntry {
    uint index;
    float diffuse_luminance;
    float specular_luminance;
};

struct BrightestLights {
	uvec4 indices0;
	uvec4 indices1;
	vec4 diffuse_luminance0;
	vec4 diffuse_luminance1;
	vec4 specular_luminance0;
	vec4 specular_luminance1;
};

const uint BRIGHTEST_LIGHT_INVALID_INDEX = 0xffffffffu;
const float BRIGHTEST_LIGHT_LUMINANCE_EPSILON = 0.001;
const vec4 BRIGHTEST_LIGHT_RANK_BIAS_0 = vec4(0.0, 1e-6, 2e-6, 3e-6);
const vec4 BRIGHTEST_LIGHT_RANK_BIAS_1 = vec4(4e-6, 5e-6, 6e-6, 7e-6);
const float BRIGHTEST_LIGHT_RANK_EPSILON = 1e-7;
const float BRIGHTEST_LIGHT_PACK_INDEX_SCALE = 1024.0;
const float BRIGHTEST_LIGHT_DIFFUSE_SCALE = 128.0;
const float BRIGHTEST_LIGHT_DIFFUSE_QUANT_MAX = 1023.0;
const float BRIGHTEST_LIGHT_DIFFUSE_LUMINANCE_MAX = 8.0;
const float BRIGHTEST_LIGHT_SPECULAR_LUMINANCE_MAX = 16.0;
const float BRIGHTEST_LIGHT_SPECULAR_PACK_MAX = 0.99;

#define BRIGHTEST_LIGHT_CHANNEL_TOTAL 0
#define BRIGHTEST_LIGHT_CHANNEL_DIFFUSE 1
#define BRIGHTEST_LIGHT_CHANNEL_SPECULAR 2

float clampDiffuseTemporalLuminance(float value)
{
	return clamp(value, 0.0, BRIGHTEST_LIGHT_DIFFUSE_LUMINANCE_MAX);
}

float clampSpecularTemporalLuminance(float value)
{
	return clamp(value, 0.0, BRIGHTEST_LIGHT_SPECULAR_LUMINANCE_MAX);
}

float brightestLightEntryTotalLuminance(BrightestLightEntry entry)
{
	return entry.diffuse_luminance + entry.specular_luminance;
}

float brightestLightEntryChannelLuminance(BrightestLightEntry entry, int channel)
{
	if (channel == BRIGHTEST_LIGHT_CHANNEL_DIFFUSE) {
		return entry.diffuse_luminance;
	}
	if (channel == BRIGHTEST_LIGHT_CHANNEL_SPECULAR) {
		return entry.specular_luminance;
	}
	return brightestLightEntryTotalLuminance(entry);
}

BrightestLightEntry getBrightestLightEntry(BrightestLights brightest, int index)
{
	BrightestLightEntry entry;
	if (index < 4) {
		entry.index = brightest.indices0[index];
		entry.diffuse_luminance = brightest.diffuse_luminance0[index];
		entry.specular_luminance = brightest.specular_luminance0[index];
	} else {
		entry.index = brightest.indices1[index - 4];
		entry.diffuse_luminance = brightest.diffuse_luminance1[index - 4];
		entry.specular_luminance = brightest.specular_luminance1[index - 4];
	}
	return entry;
}

void initBrightestLights(out BrightestLights brightest)
{
	brightest.indices0 = uvec4(BRIGHTEST_LIGHT_INVALID_INDEX);
	brightest.indices1 = uvec4(BRIGHTEST_LIGHT_INVALID_INDEX);
	brightest.diffuse_luminance0 = vec4(-8.0, -7.0, -6.0, -5.0);
	brightest.diffuse_luminance1 = vec4(-4.0, -3.0, -2.0, -1.0);
	brightest.specular_luminance0 = vec4(0.0);
	brightest.specular_luminance1 = vec4(0.0);
}

void updateBrightestLights(
	vec3 diffuse,
	vec3 specular,
	uint light_index,
	inout BrightestLights brightest)
{
#if DISABLE_BRIGHTEST_LIGHTS_TRACKING
	return;
#else
	float diffuse_luminance = clampDiffuseTemporalLuminance(luminance(diffuse));
	float specular_luminance = clampSpecularTemporalLuminance(luminance(specular));
	float light_luminance = diffuse_luminance + specular_luminance;
	if (light_luminance <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
		return;
	}

	vec4 ranked0 = brightest.diffuse_luminance0 + brightest.specular_luminance0 + BRIGHTEST_LIGHT_RANK_BIAS_0;
	vec4 ranked1 = brightest.diffuse_luminance1 + brightest.specular_luminance1 + BRIGHTEST_LIGHT_RANK_BIAS_1;
	float min0 = min(min(ranked0.x, ranked0.y), min(ranked0.z, ranked0.w));
	float min1 = min(min(ranked1.x, ranked1.y), min(ranked1.z, ranked1.w));
	float min_rank = min(min0, min1);

	bvec4 replace0 = lessThan(abs(ranked0 - vec4(min_rank)), vec4(BRIGHTEST_LIGHT_RANK_EPSILON));
	bvec4 replace1 = lessThan(abs(ranked1 - vec4(min_rank)), vec4(BRIGHTEST_LIGHT_RANK_EPSILON));
	vec4 diffuse_value = vec4(diffuse_luminance);
	vec4 specular_value = vec4(specular_luminance);
	uvec4 index_value = uvec4(light_index);

	brightest.diffuse_luminance0 = mix(brightest.diffuse_luminance0, diffuse_value, replace0);
	brightest.diffuse_luminance1 = mix(brightest.diffuse_luminance1, diffuse_value, replace1);
	brightest.specular_luminance0 = mix(brightest.specular_luminance0, specular_value, replace0);
	brightest.specular_luminance1 = mix(brightest.specular_luminance1, specular_value, replace1);
	brightest.indices0 = mix(brightest.indices0, index_value, replace0);
	brightest.indices1 = mix(brightest.indices1, index_value, replace1);
#endif
}

float packBrightnessEntry(BrightestLightEntry entry)
{
	float entry_total_luminance = brightestLightEntryTotalLuminance(entry);
	if (entry.index == BRIGHTEST_LIGHT_INVALID_INDEX || entry_total_luminance <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
		return -1.0;
	}

	float packed_diffuse_luminance = min(floor(clampDiffuseTemporalLuminance(entry.diffuse_luminance) * BRIGHTEST_LIGHT_DIFFUSE_SCALE), BRIGHTEST_LIGHT_DIFFUSE_QUANT_MAX);
	float packed_specular_luminance = min(clampSpecularTemporalLuminance(entry.specular_luminance) / BRIGHTEST_LIGHT_SPECULAR_LUMINANCE_MAX, BRIGHTEST_LIGHT_SPECULAR_PACK_MAX);
	return float(entry.index) * BRIGHTEST_LIGHT_PACK_INDEX_SCALE + packed_diffuse_luminance + packed_specular_luminance;
}

BrightestLightEntry unpackBrightnessEntry(float packed)
{
	BrightestLightEntry entry;
	if (packed < 0.0) {
		entry.index = BRIGHTEST_LIGHT_INVALID_INDEX;
		entry.diffuse_luminance = -1.0;
		entry.specular_luminance = -1.0;
		return entry;
	}

	float packed_integer = floor(packed);
	entry.index = uint(floor(packed_integer / BRIGHTEST_LIGHT_PACK_INDEX_SCALE));
	entry.diffuse_luminance = clampDiffuseTemporalLuminance(mod(packed_integer, BRIGHTEST_LIGHT_PACK_INDEX_SCALE) / BRIGHTEST_LIGHT_DIFFUSE_SCALE);
	entry.specular_luminance = clampSpecularTemporalLuminance(fract(packed) * BRIGHTEST_LIGHT_SPECULAR_LUMINANCE_MAX);
	return entry;
}

void packBrightestLights(
	BrightestLights brightest,
	out vec4 packed0,
	out vec4 packed1)
{
	packed0 = vec4(
		packBrightnessEntry(getBrightestLightEntry(brightest, 0)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 1)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 2)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 3)));
	packed1 = vec4(
		packBrightnessEntry(getBrightestLightEntry(brightest, 4)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 5)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 6)),
		packBrightnessEntry(getBrightestLightEntry(brightest, 7)));
}

void unpackBrightestLights(
	vec4 packed0,
	vec4 packed1,
	out BrightestLights brightest)
{
	BrightestLightEntry entry0 = unpackBrightnessEntry(packed0.x);
	BrightestLightEntry entry1 = unpackBrightnessEntry(packed0.y);
	BrightestLightEntry entry2 = unpackBrightnessEntry(packed0.z);
	BrightestLightEntry entry3 = unpackBrightnessEntry(packed0.w);
	BrightestLightEntry entry4 = unpackBrightnessEntry(packed1.x);
	BrightestLightEntry entry5 = unpackBrightnessEntry(packed1.y);
	BrightestLightEntry entry6 = unpackBrightnessEntry(packed1.z);
	BrightestLightEntry entry7 = unpackBrightnessEntry(packed1.w);
	brightest.indices0 = uvec4(entry0.index, entry1.index, entry2.index, entry3.index);
	brightest.indices1 = uvec4(entry4.index, entry5.index, entry6.index, entry7.index);
	brightest.diffuse_luminance0 = vec4(entry0.diffuse_luminance, entry1.diffuse_luminance, entry2.diffuse_luminance, entry3.diffuse_luminance);
	brightest.diffuse_luminance1 = vec4(entry4.diffuse_luminance, entry5.diffuse_luminance, entry6.diffuse_luminance, entry7.diffuse_luminance);
	brightest.specular_luminance0 = vec4(entry0.specular_luminance, entry1.specular_luminance, entry2.specular_luminance, entry3.specular_luminance);
	brightest.specular_luminance1 = vec4(entry4.specular_luminance, entry5.specular_luminance, entry6.specular_luminance, entry7.specular_luminance);
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

float computeTemporalIndexedConfidenceByChannel(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	int channel);

float computeTemporalConfidenceByChannel(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	int channel);

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
	if (entry.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(entry) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
		return 0.0;
	}

	vec2 weight = lightWeightFromIndex(entry.index, P, N, V, roughness);
	return clampDiffuseTemporalLuminance(weight.x) + clampSpecularTemporalLuminance(weight.y);
}

vec2 lightWeightsFromEntry(BrightestLightEntry entry, vec3 P, vec3 N, vec3 V, float roughness)
{
	if (entry.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(entry) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
		return vec2(0.0);
	}

	vec2 weight = lightWeightFromIndex(entry.index, P, N, V, roughness);
	return vec2(
		clampDiffuseTemporalLuminance(weight.x),
		clampSpecularTemporalLuminance(weight.y));
}

void processTemporalLightEntries(
	BrightestLights brightest,
	vec3 P,
	vec3 N,
	vec3 V,
	float roughness,
	out float weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		weights[i] = lightWeightFromEntry(getBrightestLightEntry(brightest, i), P, N, V, roughness);
	}
}

void processTemporalLightEntriesByChannel(
	BrightestLights brightest,
	vec3 P,
	vec3 N,
	vec3 V,
	float roughness,
	int channel,
	out float weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		vec2 weight = lightWeightsFromEntry(getBrightestLightEntry(brightest, i), P, N, V, roughness);
		if (channel == BRIGHTEST_LIGHT_CHANNEL_DIFFUSE) {
			weights[i] = weight.x;
		} else if (channel == BRIGHTEST_LIGHT_CHANNEL_SPECULAR) {
			weights[i] = weight.y;
		} else {
			weights[i] = weight.x + weight.y;
		}
	}
}

void processTemporalDiffuseLightEntries(
	BrightestLights brightest,
	vec3 P,
	vec3 N,
	vec3 V,
	float roughness,
	out float weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	processTemporalLightEntriesByChannel(brightest, P, N, V, roughness, BRIGHTEST_LIGHT_CHANNEL_DIFFUSE, weights);
}

void processTemporalSpecularLightEntries(
	BrightestLights brightest,
	vec3 P,
	vec3 N,
	vec3 V,
	float roughness,
	out float weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	processTemporalLightEntriesByChannel(brightest, P, N, V, roughness, BRIGHTEST_LIGHT_CHANNEL_SPECULAR, weights);
}

int findBrightestLightEntry(BrightestLights brightest, uint light_index)
{
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		if (getBrightestLightEntry(brightest, i).index == light_index) {
			return i;
		}
	}

	return -1;
}

float sumBrightestLightLuminance(BrightestLights brightest)
{
	float total = 0.0;
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		BrightestLightEntry entry = getBrightestLightEntry(brightest, i);
		if (entry.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(entry) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
			continue;
		}
		total += brightestLightEntryTotalLuminance(entry);
	}
	return total;
}

bool isBrightestLightEntryIndexValid(BrightestLightEntry entry)
{
	if (entry.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(entry) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
		return false;
	}
#if LIGHT_POINT
	return entry.index < lights.m.num_point_lights;
#elif LIGHT_POLYGON
	return entry.index < lights.m.num_polygons;
#else
	return false;
#endif
}

float confidenceWeightEpsilon()
{
	return 1e-4;
}

float invalidEntryMagnitude(BrightestLightEntry entry)
{
	return max(brightestLightEntryTotalLuminance(entry), 0.0);
}

float invalidEntryMagnitudeByChannel(BrightestLightEntry entry, int channel)
{
	return max(brightestLightEntryChannelLuminance(entry, channel), 0.0);
}

void extractSortedPositiveWeights(
	float weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	out float sorted_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	float eps = confidenceWeightEpsilon();
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		sorted_weights[i] = weights[i] > eps ? weights[i] : 0.0;
	}

	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL - 1; ++i) {
		for (int j = i + 1; j < BRIGHTEST_LIGHTS_PER_TEXEL; ++j) {
			if (sorted_weights[j] > sorted_weights[i]) {
				float tmp = sorted_weights[i];
				sorted_weights[i] = sorted_weights[j];
				sorted_weights[j] = tmp;
			}
		}
	}
}

float computeTemporalWeightProfileConfidence(
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	float current_sorted[BRIGHTEST_LIGHTS_PER_TEXEL];
	float prev_sorted[BRIGHTEST_LIGHTS_PER_TEXEL];
	extractSortedPositiveWeights(current_weights, current_sorted);
	extractSortedPositiveWeights(prev_weights, prev_sorted);

	float diff_sum = 0.0;
	float ref_sum = 0.0;
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		diff_sum += abs(current_sorted[i] - prev_sorted[i]);
		ref_sum += max(current_sorted[i], prev_sorted[i]);
	}

	if (ref_sum <= confidenceWeightEpsilon()) {
		return 1.0;
	}

	return 1.0 - clamp(diff_sum / ref_sum, 0.0, 1.0);
}

float computeTemporalIndexedConfidence(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	return computeTemporalIndexedConfidenceByChannel(
		current_brightest,
		prev_brightest,
		current_weights,
		prev_weights,
		BRIGHTEST_LIGHT_CHANNEL_TOTAL);
}

float computeTemporalIndexedConfidenceByChannel(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	int channel)
{
	float diff_sum = 0.0;
	float ref_sum = 0.0;
	float eps = confidenceWeightEpsilon();
	bool visited_current[BRIGHTEST_LIGHTS_PER_TEXEL];
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		visited_current[i] = false;
	}

	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		BrightestLightEntry prev = getBrightestLightEntry(prev_brightest, i);
		if (prev.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(prev) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON) {
			continue;
		}

		float prev_magnitude = 0.0;
		bool prev_is_valid = isBrightestLightEntryIndexValid(prev);
		if (prev_is_valid) {
			prev_magnitude = prev_weights[i];
			if (prev_magnitude <= eps) {
				prev_magnitude = invalidEntryMagnitudeByChannel(prev, channel);
			}
		} else {
			prev_magnitude = invalidEntryMagnitudeByChannel(prev, channel);
		}
		if (prev_magnitude <= eps) {
			continue;
		}

		float current_magnitude = 0.0;
		int current_index = findBrightestLightEntry(current_brightest, prev.index);
		if (current_index >= 0 && isBrightestLightEntryIndexValid(getBrightestLightEntry(current_brightest, current_index))) {
			float matched_weight = current_weights[current_index];
			if (matched_weight > eps) {
				visited_current[current_index] = true;
				current_magnitude = matched_weight;
			}
		}

		diff_sum += abs(current_magnitude - prev_magnitude);
		ref_sum += max(current_magnitude, prev_magnitude);
	}

	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		BrightestLightEntry current = getBrightestLightEntry(current_brightest, i);
		float current_magnitude = current_weights[i];
		if (visited_current[i] || current.index == BRIGHTEST_LIGHT_INVALID_INDEX || brightestLightEntryTotalLuminance(current) <= BRIGHTEST_LIGHT_LUMINANCE_EPSILON || current_magnitude <= eps) {
			continue;
		}

		diff_sum += current_magnitude;
		ref_sum += current_magnitude;
	}

	if (ref_sum <= eps) {
		return 1.0;
	}

	return 1.0 - clamp(diff_sum / ref_sum, 0.0, 1.0);
}

float computeTemporalConfidence(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	return computeTemporalConfidenceByChannel(
		current_brightest,
		prev_brightest,
		current_weights,
		prev_weights,
		BRIGHTEST_LIGHT_CHANNEL_TOTAL);
}

float computeTemporalConfidenceByChannel(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	int channel)
{
	float indexed_confidence = computeTemporalIndexedConfidenceByChannel(
		current_brightest,
		prev_brightest,
		current_weights,
		prev_weights,
		channel);

#if TEMPORAL_CONFIDENCE_INCLUDE_BRIGHTNESS_SUM_DIFF
	float brightness_confidence = computeTemporalWeightProfileConfidence(
		current_weights,
		prev_weights);
	return min(indexed_confidence, brightness_confidence);
#else
	return indexed_confidence;
#endif
}

float computeTemporalDiffuseConfidence(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	return computeTemporalConfidenceByChannel(
		current_brightest,
		prev_brightest,
		current_weights,
		prev_weights,
		BRIGHTEST_LIGHT_CHANNEL_DIFFUSE);
}

float computeTemporalSpecularConfidence(
	BrightestLights current_brightest,
	BrightestLights prev_brightest,
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL],
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL])
{
	return computeTemporalConfidenceByChannel(
		current_brightest,
		prev_brightest,
		current_weights,
		prev_weights,
		BRIGHTEST_LIGHT_CHANNEL_SPECULAR);
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
