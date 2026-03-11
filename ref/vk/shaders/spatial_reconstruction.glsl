// originally implemented by Mikhail Gorobets for Diligent Engine
// https://github.com/DiligentGraphics/DiligentEngine

#ifndef SPATIAL_RECONSTRUCTION_RADIUS
#define SPATIAL_RECONSTRUCTION_RADIUS 7.
#endif

#ifndef SPECULAR_INPUT_IMAGE
#define SPECULAR_INPUT_IMAGE indirect_specular
#endif

#ifndef SPECULAR_OUTPUT_IMAGE
#define SPECULAR_OUTPUT_IMAGE out_indirect_specular_reconstructed
#endif

#ifndef SPATIAL_RECONSTRUCTION_INPUT_RAY_LENGTH
#define SPATIAL_RECONSTRUCTION_INPUT_RAY_LENGTH(pix) length(imageLoad(reflection_direction_pdf, pix).xyz)
#endif

#ifndef SPATIAL_RECONSTRUCTION_FINAL_PASS
#define SPATIAL_RECONSTRUCTION_FINAL_PASS 0
#endif

#include "debug.glsl"

#define SPECULAR_CLAMPING_MAX 1.2
#define SPATIAL_RECONSTRUCTION_SAMPLES 8
#define SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR 4.
#define SPATIAL_RECONSTRUCTION_SIGMA 0.9
#define INDIRECT_SCALE 2

#define GLSL
#include "ray_interop.h"
#undef GLSL

#define RAY_BOUNCE
#define RAY_QUERY
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D SPECULAR_OUTPUT_IMAGE;

layout(set = 0, binding = 1, rgba32f) uniform readonly image2D position_t;
layout(set = 0, binding = 2, rgba16f) uniform readonly image2D normals_gs;
layout(set = 0, binding = 3, rgba8) uniform readonly image2D material_rmxx;
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D SPECULAR_INPUT_IMAGE;
layout(set = 0, binding = 5, rgba32f) uniform readonly image2D reflection_direction_pdf;

layout(set = 0, binding = 6) uniform UBO { UniformBuffer ubo; } ubo;

#if SPATIAL_RECONSTRUCTION_FINAL_PASS
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D out_indirect_specular_ray_length;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D prev_indirect_specular_ray_length;
#endif

#include "utils.glsl"
#include "noise.glsl"
#include "brdf.glsl"

#ifndef PI
	#define PI 3.14 // FIXME please
#endif

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

struct PixelAreaStatistic {
	float mean;
	float variance;
	float weightSum;
	vec4 colorSum;
};

float computeGaussianWeight(float texelDistance) {
	return exp(-0.66 * texelDistance * texelDistance);
}

float smithGGXVisibilityCorrelated(float NdotL, float NdotV, float alphaRoughness) {
	float a2 = alphaRoughness * alphaRoughness;
	float GGXV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 1e-7));
	float GGXL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 1e-7));
	return 0.5 / (GGXV + GGXL);
}

float normalDistribution_GGX(float NdotH, float alphaRoughness) {
	alphaRoughness = max(alphaRoughness, 1e-3);
	float a2 = alphaRoughness * alphaRoughness;
	float nh2 = NdotH * NdotH;
	float f = nh2 * a2 + (1.0 - nh2);
	return a2 / max(PI * f * f, 1e-9);
}

vec2 computeWeightRayLength(ivec2 pix, vec3 V, vec3 N, float roughness, float NdotV, float weight) {
	vec4 rayDirectionPDF = imageLoad(reflection_direction_pdf, pix);
	float rayLength = SPATIAL_RECONSTRUCTION_INPUT_RAY_LENGTH(pix);
	float directionLength = length(rayDirectionPDF.xyz);
	vec3 rayDirection = directionLength > 0.0 ? rayDirectionPDF.xyz / directionLength : vec3(0.0);
	float PDF = rayDirectionPDF.w;
	float alphaRoughness = roughness * roughness;

	vec3 L = rayDirection;
	vec3 H = normalize(L + V);
	float NdotH = saturate(dot(N, H));
	float NdotL = saturate(dot(N, L));

	float vis = smithGGXVisibilityCorrelated(NdotL, NdotV, alphaRoughness);
	float D = normalDistribution_GGX(NdotH, alphaRoughness);
	float localBRDF = vis * D * NdotL;
	localBRDF *= computeGaussianWeight(weight);
	float rcpRayLength = rayLength == 0.0 ? 0.0 : 1.0 / rayLength;
	return vec2(max(localBRDF / max(PDF, 1.0e-5f), 1e-6), rcpRayLength);
}

void computeWeightedVariance(inout PixelAreaStatistic stat, vec3 sampleColor, float weight) {
	stat.colorSum.xyz += weight * sampleColor;
	stat.weightSum += weight;
	float value = luminance(sampleColor.rgb);
	float prevMean = stat.mean;
	float rcpWeightSum = stat.weightSum == 0.0 ? 0.0 : 1.0 / stat.weightSum;
	stat.mean += weight * rcpWeightSum * (value - prevMean);
	stat.variance += weight * (value - prevMean) * (value - stat.mean);
}

float computeResolvedDepth(vec3 origin, vec3 position, float surfaceHitDistance) {
	return distance(origin, position) + surfaceHitDistance;
}

float computeSpatialWeight(float texelDistance, float sigma) {
	return exp(-(texelDistance) / (2.0 * sigma * sigma));
}

float computeRayLengthConfidence(float currentLength, float previousLength) {
	if (currentLength <= 0.0 || previousLength <= 0.0) {
		return 0.0;
	}
	float relativeDelta = abs(currentLength - previousLength) / max(max(currentLength, previousLength), 1e-3);
	return 1.0 - smoothstep(0.06, 0.40, relativeDelta);
}

vec3 clampSpecular(vec3 specular, float maxLuminace) {
	float lum = luminance(specular);
	if (lum == 0.0)
		return vec3(0.0);
	float clamped = min(maxLuminace, lum);
	return specular * (clamped / lum);
}

void main() {
	const ivec2 pix = ivec2(gl_GlobalInvocationID);
	const ivec2 res = ubo.ubo.res / INDIRECT_SCALE;
	if (any(greaterThanEqual(pix, res))) {
		return;
	}

	if ((ubo.ubo.renderer_flags & RENDERER_FLAG_SPATIAL_RECONSTRUCTION) == 0) {
		vec4 passthrough = imageLoad(SPECULAR_INPUT_IMAGE, pix);
		float rayLength = SPATIAL_RECONSTRUCTION_INPUT_RAY_LENGTH(pix);
#if SPATIAL_RECONSTRUCTION_FINAL_PASS
		imageStore(out_indirect_specular_ray_length, pix, vec4(rayLength, 0.0, 0.0, 0.0));
		imageStore(SPECULAR_OUTPUT_IMAGE, pix, vec4(passthrough.rgb, 0.0));
#else
		imageStore(SPECULAR_OUTPUT_IMAGE, pix, vec4(passthrough.rgb, rayLength));
#endif
		return;
	}

	const vec3 origin = (ubo.ubo.inv_view * vec4(0, 0, 0, 1)).xyz;
	const vec3 position = imageLoad(position_t, pix * INDIRECT_SCALE).xyz;

	vec3 poisson[SPATIAL_RECONSTRUCTION_SAMPLES];
	poisson[0] = vec3(-0.4706069, -0.4427112, +0.6461146);
	poisson[1] = vec3(-0.9057375, +0.3003471, +0.9542373);
	poisson[2] = vec3(-0.3487388, +0.4037880, +0.5335386);
	poisson[3] = vec3(+0.1023042, +0.6439373, +0.6520134);
	poisson[4] = vec3(+0.5699277, +0.3513750, +0.6695386);
	poisson[5] = vec3(+0.2939128, -0.1131226, +0.3149309);
	poisson[6] = vec3(+0.7836658, -0.4208784, +0.8895339);
	poisson[7] = vec3(+0.1564120, -0.8198990, +0.8346850);

	vec3 geometry_normal, shading_normal;
	readNormals(pix * INDIRECT_SCALE, geometry_normal, shading_normal);
	vec3 V = normalize(origin - position);
	float NdotV = saturate(dot(shading_normal, V));
	float roughness = imageLoad(material_rmxx, pix * INDIRECT_SCALE).x;
	float roughness_factor = saturate(float(SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR) * roughness);
	float radius = mix(0.0, SPATIAL_RECONSTRUCTION_RADIUS, roughness_factor);

	PixelAreaStatistic pixelAreaStat;
	pixelAreaStat.colorSum = vec4(0.0);
	pixelAreaStat.weightSum = 0.0;
	pixelAreaStat.variance = 0.0;
	pixelAreaStat.mean = 0.0;

	float nearestSurfaceHitDistance = 0.0;
	float weights_sum = 0.0;
	float ray_length_sum = 0.0;

	for (int i = 0; i < SPATIAL_RECONSTRUCTION_SAMPLES; i++) {
		ivec2 p = max(ivec2(0), min(ivec2(res) - ivec2(1), ivec2(pix + radius * poisson[i].xy)));
		float weightS = computeSpatialWeight(poisson[i].z * poisson[i].z, SPATIAL_RECONSTRUCTION_SIGMA);
		vec2 weightLength = computeWeightRayLength(p, V, shading_normal, roughness, NdotV, weightS);
		vec3 sampleColor = clampSpecular(imageLoad(SPECULAR_INPUT_IMAGE, p).xyz, SPECULAR_CLAMPING_MAX);
		computeWeightedVariance(pixelAreaStat, sampleColor, weightLength.x);
		if (weightLength.x > 1.0e-6) {
			nearestSurfaceHitDistance = max(weightLength.y, nearestSurfaceHitDistance);
		}
		float sampleRayLength = SPATIAL_RECONSTRUCTION_INPUT_RAY_LENGTH(p);
		if (sampleRayLength > 0.0) {
			// Keep ray-length accumulation weighted by the exact same sample weights as radiance.
			ray_length_sum += sampleRayLength * weightLength.x;
			weights_sum += weightLength.x;
		}
	}

	float resolvedRayLength = weights_sum > 0.0 ? ray_length_sum / weights_sum : 0.0;
	vec4 resolvedRadiance = pixelAreaStat.colorSum / max(pixelAreaStat.weightSum, 1e-6f);
	float resolvedVariance = pixelAreaStat.variance / max(pixelAreaStat.weightSum, 1e-6f);
	float resolvedDepth = computeResolvedDepth(origin, position, nearestSurfaceHitDistance);
		
#if SPATIAL_RECONSTRUCTION_FINAL_PASS
	float previousRayLength = imageLoad(prev_indirect_specular_ray_length, pix).r;
	float rayLengthConfidence = computeRayLengthConfidence(resolvedRayLength, previousRayLength);
	imageStore(out_indirect_specular_ray_length, pix, vec4(resolvedRayLength, 0.0, 0.0, 0.0));
	imageStore(SPECULAR_OUTPUT_IMAGE, pix, vec4(resolvedRadiance.xyz, rayLengthConfidence));
#else
	imageStore(SPECULAR_OUTPUT_IMAGE, pix, vec4(resolvedRadiance.xyz, resolvedRayLength));
#endif
}



