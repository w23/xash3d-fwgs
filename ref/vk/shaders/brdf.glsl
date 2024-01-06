#ifndef BRDF_GLSL_INCLUDED
#define BRDF_GLSL_INCLUDED


// TODO math|common.glsl
const float kPi = 3.1415926;
const float kOneOverPi = 1. / kPi;

//#define BRDF_COMPARE
#ifdef BRDF_COMPARE
bool g_mat_gltf2 = true;
#endif

#ifdef BRDF_COMPARE
#include "brdf.h"
#else
struct MaterialProperties {
	vec3 base_color;
	//vec3 normal;
	float metalness;
	float roughness;
};
#endif

float ggxD(float a2, float h_dot_n) {
	if (h_dot_n <= 0.)
		return 0.;

	h_dot_n = max(1e-4, h_dot_n);
	const float denom = h_dot_n * h_dot_n * (a2 - 1.) + 1.;

	// Need to make alpha^2 non-zero to make sure that smooth surfaces get at least some specular reflections
	// Otherwise it will just multiply by zero.
	return max(1e-5, a2) * kOneOverPi / (denom * denom);
}

float ggxG(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

	l_dot_n = max(1e-4, l_dot_n);
	n_dot_v = max(1e-4, n_dot_v);
	const float denom1 = abs(l_dot_n) + sqrt(a2 + (1. - a2) * l_dot_n * l_dot_n);
	const float denom2 = abs(n_dot_v) + sqrt(a2 + (1. - a2) * n_dot_v * n_dot_v);
	return 4. * abs(l_dot_n) * abs(n_dot_v) / (denom1 * denom2);
}

float ggxV(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

	l_dot_n = max(1e-4, l_dot_n);
	n_dot_v = max(1e-4, n_dot_v);
	const float denom1 = abs(l_dot_n) + sqrt(a2 + (1. - a2) * l_dot_n * l_dot_n);
	const float denom2 = abs(n_dot_v) + sqrt(a2 + (1. - a2) * n_dot_v * n_dot_v);
	return 1. / (denom1 * denom2);
}

// glTF 2.0 BRDF sample implementation
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation
// TODO oneMinusVdotH5
//vec3 gltf2_conductor_fresnel(vec3 f0, vec3 bsdf, float VdotH) {
//  return bsdf * (f0 + (1. - f0) * pow(1. - abs(VdotH), 5.));
//}
//
//vec3 fresnel_mix(float ior, vec3 base, vec3 layer, float VdotH) {
//  const float f0 = pow((1.-ior)/(1.+ior), 2.);
//  const float fr = f0 + (1. - f0) * pow(1. - abs(VdotH), 5.);
//  return mix(base, layer, fr)
//}

void brdfComputeGltfModel(vec3 N, vec3 L, vec3 V, MaterialProperties material, out vec3 out_diffuse, out vec3 out_specular) {
	const float alpha = material.roughness * material.roughness;
	const float a2 = alpha * alpha;
	const vec3 H = normalize(L + V);
	const float h_dot_v = dot(H, V);
	const float h_dot_n = dot(H, N);
	const float l_dot_n = dot(L, N);
	const float h_dot_l = dot(H, L);
	const float n_dot_v = dot(N, V);

	/* Original for when the color is known already.
	const vec3 diffuse_color = mix(material.base_color, vec3(0.), material.metalness);
	const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
	const vec3 fresnel = f0 + (vec3(1.) - f0) * pow(1. - abs(h_dot_v), 5.);
	*/

	// Use white for diffuse color here, as compositing happens way later in denoiser/smesitel
	const vec3 diffuse_color = mix(vec3(1.), vec3(0.), material.metalness);

	// Specular does get the real color, as its contribution is light-direction-dependent
	const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
	const float fresnel_factor = pow(1. - abs(h_dot_v), 5.);
	const vec3 fresnel = vec3(1.) * fresnel_factor + f0 * (1. - fresnel_factor);

	// This is taken directly from glTF 2.0 spec. It seems incorrect to me: it should not include the base_color twice.
	// Note: here diffuse_color doesn't include base_color as it is mixed later, but we can clearly see that
	// base_color is still visible in the direct_diff term, which it shouldn't be. See E357 ~37:00
	// TODO make a PR against glTF spec with the correct derivation.
	//out_diffuse = (vec3(1.) - fresnel) * kOneOverPi * diffuse_color * l_dot_n;

	// This is the correctly derived diffuse term that doesn't include the base_color twice
	out_diffuse = diffuse_color * kOneOverPi * .96 * (1. - fresnel_factor);

	//if (isnan(l_dot_n))
	if (any(isnan(N)))
		out_diffuse = 10.*vec3(1., 1., 0.);

	out_specular = fresnel * ggxD(a2, h_dot_n) * ggxV(a2, l_dot_n, h_dot_l, n_dot_v, h_dot_v);
}

void evalSplitBRDF(vec3 N, vec3 L, vec3 V, MaterialProperties material, out vec3 out_diffuse, out vec3 out_specular) {
	out_diffuse = vec3(0.);
	out_specular = vec3(0.);

/* glTF 2.0 BRDF mixing model
https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation

material = mix(dielectric_brdf, metal_brdf, metallic)
         = (1.0 - metallic) * dielectric_brdf + metallic * metal_brdf

metal_brdf =
  conductor_fresnel(
    f0 = baseColor,
    bsdf = specular_brdf(α = roughness ^ 2))

dielectric_brdf =
  fresnel_mix(
    ior = 1.5,
    base = diffuse_brdf(color = baseColor),
    layer = specular_brdf(α = roughness ^ 2))
 */

/* glTF 2.0 BRDF sample implementation
https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation

metal_brdf = specular_brdf(roughness^2) * (baseColor.rgb + (1 - baseColor.rgb) * (1 - abs(VdotH))^5)
dielectric_brdf = mix(diffuse_brdf(baseColor.rgb), specular_brdf(roughness^2), 0.04 + (1 - 0.04) * (1 - abs(VdotH))^5)
 */

/* suggested simplification
const black = 0

c_diff = lerp(baseColor.rgb, black, metallic)
f0 = lerp(0.04, baseColor.rgb, metallic)
α = roughness^2

F = f0 + (1 - f0) * (1 - abs(VdotH))^5

f_diffuse = (1 - F) * (1 / π) * c_diff
f_specular = F * D(α) * G(α) / (4 * abs(VdotN) * abs(LdotN))

material = f_diffuse + f_specular
 */

#ifdef BRDF_COMPARE
if (g_mat_gltf2) {
#endif
	brdfComputeGltfModel(N, L, V, material, out_diffuse, out_specular);
	const float l_dot_n = dot(L, N);
	out_diffuse *= l_dot_n;
	out_specular *= l_dot_n;
#ifdef BRDF_COMPARE
} else {
	// Prepare data needed for BRDF evaluation - unpack material properties and evaluate commonly used terms (e.g. Fresnel, NdotL, ...)
	BrdfData data = prepareBRDFData(N, L, V, material);

	// Ignore V and L rays "below" the hemisphere
	//if (data.Vbackfacing || data.Lbackfacing) return vec3(0.0f, 0.0f, 0.0f);

	// Eval specular and diffuse BRDFs
	out_specular = evalSpecular(data);

	// Our renderer mixes base_color into diffuse component later in denoiser/smesitel
	data.diffuseReflectance = baseColorToDiffuseReflectance(vec3(1.), material.metalness);
	out_diffuse = evalDiffuse(data);

	// Combine specular and diffuse layers
#if COMBINE_BRDFS_WITH_FRESNEL
	// Specular is already multiplied by F, just attenuate diffuse
	out_diffuse *= vec3(1.) - data.F;
#endif
}
#endif
}

vec3 sampleCosineHemisphereAroundVectorUnnormalized(vec2 rnd, vec3 n) {
	// Ray Tracing Gems, §16.6.2
	const float a = 1. - 2. * rnd.x;
	const float b = sqrt(1. - a * a);
	const float phi = 2. * kPi * rnd.y;

	// TODO const float pdf = a / kPi;
	return n + vec3(b * cos(phi), b * sin(phi), a);
}

// brdf.h
// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
vec4 getRotationToZAxis(vec3 v) {
	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (v.z < -0.99999f) return vec4(1.0f, 0.0f, 0.0f, 0.0f);

	return normalize(vec4(v.y, -v.x, 0.0f, 1.0f + v.z));
}
// Returns the quaternion with inverted rotation
vec4 invertRotation(vec4 q)
{
	return vec4(-q.x, -q.y, -q.z, q.w);
}
// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
vec3 rotatePoint(vec4 q, vec3 v) {
	const vec3 qAxis = vec3(q.x, q.y, q.z);
	return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}
// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
vec3 sampleHemisphere(vec2 u, out float pdf) {
	float a = sqrt(u.x);
	float b = 2. * kPi * u.y;

	vec3 result = vec3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * kOneOverPi;

	return result;
}
vec3 sampleCosineHemisphereAroundVectorUnnormalized2(vec2 rnd, vec3 n) {
	const vec4 qRotationToZ = getRotationToZAxis(n);
	float pdf;
	return rotatePoint(invertRotation(qRotationToZ), sampleHemisphere(rnd, pdf));
}

#define BRDF_TYPE_NONE 0
#define BRDF_TYPE_DIFFUSE 1
#define BRDF_TYPE_SPECULAR 2

int brdfGetSample(vec2 rnd, MaterialProperties material, vec3 view, vec3 geometry_normal, vec3 shading_normal, /*float alpha, */out vec3 out_direction, inout vec3 inout_throughput) {
#if 1
	// Idiotic sampling, super noisy, bad distribution, etc etc
	// But we need to start somewhere
	// TODO pick diffuse-vs-specular based on expected contribution
	const int brdf_type = BRDF_TYPE_DIFFUSE;// (rand01() > .5) ? BRDF_TYPE_DIFFUSE : BRDF_TYPE_SPECULAR;

	if (any(isnan(geometry_normal))) {
		inout_throughput = 10.*vec3(1.,0.,1.);
		return brdf_type;
	}

	out_direction = sampleCosineHemisphereAroundVectorUnnormalized2(rnd, shading_normal);
	if (dot(out_direction, out_direction) < 1e-5 || dot(out_direction, geometry_normal) <= 0.)
		return BRDF_TYPE_NONE;

	out_direction = normalize(out_direction);

	vec3 diffuse = vec3(0.), specular = vec3(0.);
	brdfComputeGltfModel(shading_normal, out_direction, view, material, diffuse, specular);

	/*
	if (any(isnan(diffuse))) {
		inout_throughput = vec3(0., 1., 0.);
		return brdf_type;
	}
	*/

	inout_throughput *= (brdf_type == BRDF_TYPE_DIFFUSE) ? diffuse : specular;

	const float throughput_threshold = 1e-3;
	if (dot(inout_throughput, inout_throughput) < throughput_threshold)
		return BRDF_TYPE_NONE;

	// FIXME better sampling
	return brdf_type;
#else
	int brdf_type = BRDF_TYPE_DIFFUSE;
	// FIXME address translucency properly
	const float alpha = (base_a.a);
	if (1. > alpha && rand01() > alpha) {
		brdf_type = BRDF_TYPE_SPECULAR;
		// TODO: when not sampling randomly: throughput *= (1. - base_a.a);
		bounce_direction = normalize(refract(direction, geometry_normal, .95));
		geometry_normal = -geometry_normal;
		//throughput /= base_a.rgb;
	} else {
		if (material.metalness == 1.0f && material.roughness == 0.0f) {
			// Fast path for mirrors
			brdf_type = BRDF_TYPE_SPECULAR;
		} else {
			// Decide whether to sample diffuse or specular BRDF (based on Fresnel term)
			const float brdf_probability = getBrdfProbability(material, -direction, shading_normal);
			if (rand01() < brdf_probability) {
				brdf_type = BRDF_TYPE_SPECULAR;
				throughput /= brdf_probability;
			}
		}

		const vec2 u = vec2(rand01(), rand01());
		vec3 brdf_weight = vec3(0.);
		if (!evalIndirectCombinedBRDF(u, shading_normal, geometry_normal, -direction, material, brdf_type, bounce_direction, brdf_weight))
			return false;
		throughput *= brdf_weight;
	}

	const float throughput_threshold = 1e-3;
	if (dot(throughput, throughput) < throughput_threshold)
		return false;

	return true;
#endif
}

#endif //ifndef BRDF_GLSL_INCLUDED
