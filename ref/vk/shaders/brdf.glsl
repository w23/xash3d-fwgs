#ifndef BRDF_GLSL_INCLUDED
#define BRDF_GLSL_INCLUDED

#include "debug.glsl"

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

float saturate(float x) { return clamp(x, 0.0f, 1.0f); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0f), vec3(1.0f)); }
float mad(float a, float b, float c) { return a * b + c; }
float luminance(vec3 rgb) { return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f)); }
#endif

// Ray Tracing Gems, §16.6.3
// Sample cone oriented in Z+ direction
vec3 sampleConeZ(vec2 rnd, float cos_theta_max) {
	const float cos_theta = (1. - rnd.x) + rnd.x * cos_theta_max;
	const float sin_theta = sqrt(1. - clamp(cos_theta * cos_theta, 0., 1.));
	const float phi = rnd.y * 2. * kPi;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// Building an Orthonormal Basis, Revisited
// https://jcgt.org/published/0006/01/01/
mat3 orthonormalBasisZ(vec3 z) {
	const float s = signP(z.z);
	const float a = -1. / (s + z.z);
	const float b = z.x * z.y * a;
	return mat3(
		vec3(1. + s * z.x * z.x * a, s * b, -s * z.x),
		vec3(b, s + z.y * z.y * a, -z.y),
		z
	);
}

float ggxD(float a2, float h_dot_n) {
	if (h_dot_n <= 0.)
		return 0.;

	// Need to make alpha^2 non-zero to make sure that smooth surfaces get at least some specular reflections
	// Otherwise it will just multiply by zero.
	// This also helps with limiting denom to 1e-5
	a2 = max(1e-5, a2);
	// Limit in case H is "random" due to L~=-V
	h_dot_n = clamp(h_dot_n, 1e-5, 1.);

	const float denom = h_dot_n * h_dot_n * (a2 - 1.) + 1.;
	return a2 * kOneOverPi / (denom * denom);
}

float ggxG(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

	//l_dot_n = max(1e-4, l_dot_n);
	n_dot_v = max(1e-4, n_dot_v);
	const float denom1 = abs(l_dot_n) + sqrt(a2 + (1. - a2) * l_dot_n * l_dot_n);
	const float denom2 = abs(n_dot_v) + sqrt(a2 + (1. - a2) * n_dot_v * n_dot_v);
	return 4. * abs(l_dot_n) * abs(n_dot_v) / (denom1 * denom2);
}

float ggxV(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

	//l_dot_n = max(1e-4, l_dot_n);
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

void brdfComputeGltfModel(vec3 N, vec3 L, vec3 V, MaterialProperties material, out float l_dot_n, out vec3 out_diffuse, out vec3 out_specular) {
	// L facing away from N can happen fairly often, exit early if so.
	l_dot_n = max(0., dot(L, N));
	if (l_dot_n == 0.) {
		out_diffuse = vec3(0.);
		out_specular = vec3(0.);
		return;
	}

	const float alpha = material.roughness * material.roughness;
	const float a2 = alpha * alpha;

	// If L ~= -V, then H will be roughly random, maybe mostly orthogonal to both. See ~03:00:00 E360
	// If L == V, then H will be NaN.
	const vec3 H = normalize(L + V);

	const float h_dot_n = dot(H, N);
	const float n_dot_v = dot(N, V);

	const float h_dot_v = max(0., dot(H, V));
	const float h_dot_l = max(0., dot(H, L));

/*
#ifdef DEBUG_VALIDATE_EXTRA
	if (IS_INVALID(h_dot_v) || h_dot_v < 0.) {
		debugPrintfEXT("INVALID h_dot_v=%f, L=(%f,%f,%f) V=(%f,%f,%f) N=(%f, %f, %f)",
			h_dot_v, PRIVEC3(L), PRIVEC3(V), PRIVEC3(N));
	}

	if (IS_INVALID(h_dot_l) || h_dot_l < 0.) {
		debugPrintfEXT("INVALID h_dot_l=%f, L=(%f,%f,%f) V=(%f,%f,%f) N=(%f, %f, %f)",
			h_dot_l, PRIVEC3(L), PRIVEC3(V), PRIVEC3(N));
	}
#endif
*/

	/* Original for when the color is known already.
	const vec3 diffuse_color = mix(material.base_color, vec3(0.), material.metalness);
	const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
	const vec3 fresnel = f0 + (vec3(1.) - f0) * pow(1. - abs(h_dot_v), 5.);
	*/

	// Use white for diffuse color here, as compositing happens way later in denoiser/smesitel
	const vec3 diffuse_color = mix(vec3(1.), vec3(0.), material.metalness);

	// Specular does get the real color, as its contribution is light-direction-dependent
	const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
	float fresnel_factor = max(0., pow(1. - abs(h_dot_v), 5.));
	const vec3 fresnel = vec3(1.) * fresnel_factor + f0 * (1. - fresnel_factor);

	// This is taken directly from glTF 2.0 spec. It seems incorrect to me: it should not include the base_color twice.
	// Note: here diffuse_color doesn't include base_color as it is mixed later, but we can clearly see that
	// base_color is still visible in the direct_diff term, which it shouldn't be. See E357 ~37:00
	// TODO make a PR against glTF spec with the correct derivation.
	//out_diffuse = (vec3(1.) - fresnel) * kOneOverPi * diffuse_color * l_dot_n;

	// This is the correctly derived diffuse term that doesn't include the base_color twice
	out_diffuse = diffuse_color * kOneOverPi * .96 * (1. - fresnel_factor);

	float ggxd = ggxD(a2, h_dot_n);
#ifdef DEBUG_VALIDATE_EXTRA
	if (IS_INVALID(ggxd) || ggxd < 0. /* || ggxd > 1.*/) {
		debugPrintfEXT("N=(%f,%f,%f) L=(%f,%f,%f) V=(%f,%f,%f) a2=%f h_dot_n=%f INVALID ggxd=%f",
			PRIVEC3(N), PRIVEC3(L), PRIVEC3(V), a2, h_dot_n, ggxd);
		ggxd = 0.;
	}
#endif

	float ggxv = ggxV(a2, l_dot_n, h_dot_l, n_dot_v, h_dot_v);
#ifdef DEBUG_VALIDATE_EXTRA
	if (IS_INVALID(ggxv) || ggxv < 0. /*|| ggxv > 1.*/) {
		debugPrintfEXT("N=(%f,%f,%f) L=(%f,%f,%f) V=(%f,%f,%f) a2=%f h_dot_n=%f INVALID ggxv=%f",
			PRIVEC3(N), PRIVEC3(L), PRIVEC3(V), a2, h_dot_n, ggxv);
		ggxv = 0.;
	}
#endif

	out_specular = fresnel * ggxd * ggxv;

#ifdef DEBUG_VALIDATE_EXTRA
	if (IS_INVALIDV(out_diffuse) || any(lessThan(out_diffuse, vec3(0.)))) {
		debugPrintfEXT("%s:%d INVALID out_diffuse=(%f,%f,%f)", __FILE__, __LINE__, PRIVEC3(out_diffuse));
	}
	if (IS_INVALIDV(out_specular) || any(lessThan(out_specular, vec3(0.)))) {
		debugPrintfEXT("%s:%d INVALID out_specular=(%f,%f,%f)", __FILE__, __LINE__, PRIVEC3(out_specular));
	}
#endif
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
	float l_dot_n;
	brdfComputeGltfModel(N, L, V, material, l_dot_n, out_diffuse, out_specular);
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

#define TEST_LOCAL_FRAME
#ifdef TEST_LOCAL_FRAME
#ifndef BRDF_H_INCLUDED
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
vec3 sampleHemisphereCosine(vec2 u, out float pdf) {
	float a = sqrt(u.x);
	float b = 2. * kPi * u.y;

	vec3 result = vec3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * kOneOverPi;

	return result;
}
#endif // #ifndef BRDF_H_INCLUDED

vec3 sampleCosineHemisphereAroundVectorUnnormalizedLocalFrame(vec2 rnd, vec3 n) {
	const vec4 qRotationToZ = getRotationToZAxis(n);
	float pdf;
	return rotatePoint(invertRotation(qRotationToZ), sampleHemisphereCosine(rnd, pdf));
}
#endif // #ifdef TEST_LOCAL_FRAME

vec3 sampleCosineHemisphereAroundVectorUnnormalized(vec2 rnd, vec3 n) {
	// Ray Tracing Gems, §16.6.2
	const float a = 1. - 2. * rnd.x;
	const float b = sqrt(1. - a * a);
	const float phi = 2. * kPi * rnd.y;

	// TODO const float pdf = a / kPi;
	return n + vec3(b * cos(phi), b * sin(phi), a);
}

// Bounded VNDF Sampling for Smith–GGX Reflections
// Kenta Eto and Yusuke Tokuyoshi. SIGGRAPH Asia 2023.
// https://doi.org/10.1145/3610543.3626163
vec3 SampleGGXReflection ( vec3 i , vec2 alpha , vec2 rand ) {
	vec3 i_std = normalize ( vec3 (i. xy * alpha , i.z));
	// Sample a spherical cap
	float phi = 2.0 * kPi * rand .x;
	float a = saturate ( min ( alpha .x , alpha .y)); // Eq . 6
	float s = 1.0 + length ( vec2 (i.x , i.y)); // Omit sgn for a <=1
	float a2 = a * a; float s2 = s * s;
	float k = (1.0 - a2 ) * s2 / ( s2 + a2 * i.z * i.z); // Eq . 5
	float b = i.z > 0 ? k * i_std .z : i_std .z;
	float z = mad (1.0 - rand .y , 1.0 + b , -b);
	float sinTheta = sqrt ( saturate (1.0 - z * z));
	vec3 o_std = vec3( sinTheta * cos ( phi ) , sinTheta * sin ( phi ) , z );
	// Compute the microfacet normal m
	vec3 m_std = i_std + o_std ;
	vec3 m = normalize ( vec3 ( m_std . xy * alpha , m_std .z));
	// Return the reflection vector o
	return 2.0 * dot (i , m) * m - i;
}

#define BRDF_TYPE_NONE 0
#define BRDF_TYPE_DIFFUSE 1
#define BRDF_TYPE_SPECULAR 2

int brdfGetSample(vec2 rnd, MaterialProperties material, vec3 view, vec3 geometry_normal, vec3 shading_normal, /*float alpha, */out vec3 out_direction, inout vec3 inout_throughput) {
#if 1
	// Idiotic sampling, super noisy, bad distribution, etc etc
	// But we need to start somewhere
	// TODO pick diffuse-vs-specular based on expected contribution
	// TODO fresnel factor
	// TODO base_color also might play a role

	// See SELECTING BRDF LOBES in 14.3.6 RT Gems 2
	// TODO DRY brdfComputeGltfModel
	// Use shading_normal as H estimate
	const float h_dot_v = max(0., dot(shading_normal, view));
	const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
	float fresnel_factor = max(0., pow(1. - abs(h_dot_v), 5.));
	const vec3 fresnel = vec3(1.) * fresnel_factor + f0 * (1. - fresnel_factor);
	const float est_spec = luminance(fresnel);
	const float est_diff = (1. - fresnel_factor) * (1. - material.metalness);

	const float specular_probability = clamp(est_spec / (est_spec + est_diff), 0., 1.);
	const int brdf_type = (rand01() > specular_probability) ? BRDF_TYPE_DIFFUSE : BRDF_TYPE_SPECULAR;

	if (brdf_type == BRDF_TYPE_DIFFUSE) {
#if defined(BRDF_COMPARE) && defined(TEST_LOCAL_FRAME)
if (g_mat_gltf2) {
#endif
	out_direction = sampleCosineHemisphereAroundVectorUnnormalized(rnd, shading_normal);
#if defined(BRDF_COMPARE) && defined(TEST_LOCAL_FRAME)
} else {
	out_direction = sampleCosineHemisphereAroundVectorUnnormalizedLocalFrame(rnd, shading_normal);
}
#endif

		// Cosine weight is already "encoded" in cosine hemisphere sampling.
		const vec3 lambert_diffuse_term = vec3(1.f);
		inout_throughput *= lambert_diffuse_term / (1. - specular_probability);
	} else {
		// Specular

		// nspace = normal vector is Z
		const vec4 to_nspace = getRotationToZAxis(shading_normal);
		const vec3 view_nspace = rotatePoint(to_nspace, view);
		const vec3 sample_dir = SampleGGXReflection(view_nspace, vec2(material.roughness), rnd);
		out_direction = rotatePoint(invertRotation(to_nspace), sample_dir);

		const vec3 H = normalize(out_direction + view);
		const float h_dot_v = max(0., dot(H, view));
		const vec3 f0 = mix(vec3(.04), material.base_color, material.metalness);
		float fresnel_factor = max(0., pow(1. - abs(h_dot_v), 5.));
		// TODO use mix(), higher chance for it to be optimized
		const vec3 fresnel = vec3(1.) * fresnel_factor + f0 * (1. - fresnel_factor);

		inout_throughput *= fresnel / specular_probability;
	}

	if (dot(out_direction, out_direction) < 1e-5 || dot(out_direction, geometry_normal) <= 0.)
		return BRDF_TYPE_NONE;

	out_direction = normalize(out_direction);

	const float throughput_threshold = 1e-3;
	if (dot(inout_throughput, inout_throughput) < throughput_threshold)
		return BRDF_TYPE_NONE;

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
