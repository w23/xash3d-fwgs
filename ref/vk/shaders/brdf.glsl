#ifndef BRDF_GLSL_INCLUDED
#define BRDF_GLSL_INCLUDED


// TODO math|common.glsl
const float kPi = 3.1415926;
const float kOneOverPi = 1. / kPi;

#define BRDF_COMPARE
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

	const float denom = h_dot_n * h_dot_n * (a2 - 1.) + 1.;

	// Need to make alpha^2 non-zero to make sure that smooth surfaces get at least some specular reflections
	// Otherwise it will just multiply by zero.
	return max(1e-5, a2) * kOneOverPi / (denom * denom);
}

float ggxG(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

	const float denom1 = abs(l_dot_n) + sqrt(a2 + (1. - a2) * l_dot_n * l_dot_n);
	const float denom2 = abs(n_dot_v) + sqrt(a2 + (1. - a2) * n_dot_v * n_dot_v);
	return 4. * abs(l_dot_n) * abs(n_dot_v) / (denom1 * denom2);
}

float ggxV(float a2, float l_dot_n, float h_dot_l, float n_dot_v, float h_dot_v) {
	if (h_dot_l <= 0. || h_dot_v <= 0.)
		return 0.;

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
	out_diffuse = l_dot_n * diffuse_color * kOneOverPi * .96 * (1. - fresnel_factor);

	out_specular = l_dot_n * fresnel * ggxD(a2, h_dot_n) * ggxV(a2, l_dot_n, h_dot_l, n_dot_v, h_dot_v);
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


#endif //ifndef BRDF_GLSL_INCLUDED
