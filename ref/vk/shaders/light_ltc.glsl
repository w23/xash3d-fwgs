#ifndef LIGHT_LTC_GLSL_INCLUDED
#define LIGHT_LTC_GLSL_INCLUDED

// Adapted from Heitz et al., Real-Time Polygonal-Light Shading with Linearly
// Transformed Cosines. See ref/vk/data/valve/ltc_code.LICENSE.
layout(set = 0, binding = 36) uniform sampler2D ltc_lut;

vec4 ltcLookup(vec2 uv, int row_offset) {
	const vec2 coord = clamp(uv, 0.0, 1.0) * 63.0;
	const ivec2 lower = ivec2(coord);
	const ivec2 upper = min(lower + 1, ivec2(63));
	const vec2 f = fract(coord);
	const vec4 a = texelFetch(ltc_lut, ivec2(lower.x, lower.y + row_offset), 0);
	const vec4 b = texelFetch(ltc_lut, ivec2(upper.x, lower.y + row_offset), 0);
	const vec4 c = texelFetch(ltc_lut, ivec2(lower.x, upper.y + row_offset), 0);
	const vec4 d = texelFetch(ltc_lut, ivec2(upper.x, upper.y + row_offset), 0);
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 ltcIntegrateEdge(vec3 a, vec3 b) {
	const float x = clamp(dot(a, b), -1.0, 1.0);
	const vec3 edge = cross(a, b);
	const float y = abs(x);
	const float v = (0.8543985 + (0.4965155 + 0.0145206 * y) * y)
		/ (3.4175940 + (4.1616724 + y) * y);
	return edge * (x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x*x, 1e-7)) - v);
}

float ltcHorizonIntegral(float elevation, float contour_length) {
	if (contour_length <= 1e-8) {
		return 0.0;
	}
	const vec2 uv = vec2(elevation * 0.5 + 0.5, contour_length);
	return contour_length * max(ltcLookup(uv, 64).w, 0.0);
}

mat3 ltcSpecularTransform(vec3 N, vec3 V, float ndotv, MaterialProperties material, out vec3 amplitude) {
	const vec2 uv = vec2(clamp(material.roughness, 0.05, 1.0), sqrt(1.0 - ndotv));
	const vec4 m = ltcLookup(uv, 0);
	const vec2 fresnel = ltcLookup(uv, 64).xy;
	const vec3 f0 = mix(vec3(0.04), material.base_color, material.metalness);
	amplitude = max(f0 * fresnel.x + (vec3(1.0) - f0) * fresnel.y, vec3(0.0));
	const mat3 inverse_ltc = mat3(vec3(m.x, 0.0, m.y), vec3(0.0, 1.0, 0.0), vec3(m.z, 0.0, m.w));
	const vec3 projected = V - N * ndotv;
	const vec3 T = dot(projected, projected) > 1e-8 ? normalize(projected) : orthonormalBasisZ(N)[0];
	return inverse_ltc * transpose(mat3(T, cross(N, T), N));
}

float ltcDiffuseAmplitude(vec3 L, vec3 V, MaterialProperties material) {
	const vec3 half_sum = L + V;
	const float hdotv = dot(half_sum, half_sum) > 1e-10 ? abs(dot(normalize(half_sum), V)) : 0.0;
	return (1.0 - material.metalness) * 0.96 * (1.0 - pow(clamp(1.0 - hdotv, 0.0, 1.0), 5.0));
}

float ltcContourIntegral(vec3 sum) {
	const float len = length(sum);
	return ltcHorizonIntegral(sum.z / max(len, 1e-8), len);
}

#if LIGHT_POLYGON && LIGHT_POLYGON_ANALYTIC
void accumulatePolygonLightLtc(PolygonLight poly, vec3 P, vec3 N, vec3 V,
	MaterialProperties material, inout vec3 diffuse, inout vec3 specular) {
	const uint offset = poly.vertices_count_offset & 0xffffu;
	const uint count = min(poly.vertices_count_offset >> 16, 8u);
	if (count < 3u || dot(poly.plane, vec4(P, 1.0)) <= 0.01) {
		return;
	}

	const uint triangle = 1u + min(uint(rand01() * float(count - 2u)), count - 3u);
	const vec3 first = lights.m.polygon_vertices[offset].xyz - P;
	const vec3 b = lights.m.polygon_vertices[offset + triangle].xyz - P;
	const vec3 c = lights.m.polygon_vertices[offset + triangle + 1u].xyz - P;
	const float r = sqrt(rand01());
	const vec3 shadow_offset = mix(first, mix(b, c, rand01()), r);
	const float shadow_dist = length(shadow_offset);
	if (shadow_dist <= 1e-6 || shadowed(P, shadow_offset / shadow_dist, shadow_dist)) {
		return;
	}

	const float ndotv = clamp(dot(N, V), 0.0, 1.0);
	vec3 amplitude;
	const mat3 transform = ltcSpecularTransform(N, V, ndotv, material, amplitude);
	vec3 previous = normalize(lights.m.polygon_vertices[offset + count - 1u].xyz - P);
	vec3 previous_ltc = normalize(transform * previous);
	vec3 diffuse_sum = vec3(0.0), specular_sum = vec3(0.0);
	float max_elevation = -1.0;
	for (uint i = 0u; i < count; ++i) {
		const vec3 current = normalize(lights.m.polygon_vertices[offset + i].xyz - P);
		const vec3 current_ltc = normalize(transform * current);
		diffuse_sum += ltcIntegrateEdge(previous, current);
		specular_sum += ltcIntegrateEdge(previous_ltc, current_ltc);
		max_elevation = max(max_elevation, dot(N, current));
		previous = current;
		previous_ltc = current_ltc;
	}
	if (max_elevation <= 0.0) {
		return;
	}

	const float len = length(diffuse_sum);
	const float projected = ltcHorizonIntegral(dot(N, diffuse_sum) / max(len, 1e-8), len);
	// Preserve the diffuse scale of the experimental polygon evaluator.
	diffuse += poly.emissive * projected * (2.0 * kOneOverPi)
		* ltcDiffuseAmplitude(normalize(poly.center - P), V, material);
	if (ndotv > 1e-5) {
		specular += poly.emissive * amplitude * ltcContourIntegral(specular_sum);
	}
}
#endif

#if LIGHT_POINT && LIGHT_POINT_ANALYTIC
float ltcSpotAttenuation(PointLight light, vec3 direction) {
	const float spot = dot(direction, light.dir_stopdot2.xyz);
	const float outer = light.dir_stopdot2.w;
	const float inner = light.color_stopdot.w;
	if (spot < outer) {
		return 0.0;
	}
	return spot < inner ? (spot - outer) / (inner - outer) : 1.0;
}

void accumulatePointLightLtc(PointLight light, vec3 P, vec3 N, vec3 V,
	MaterialProperties material, inout vec3 diffuse, inout vec3 specular) {
	const bool environment = light.environment != 0u;
	vec3 axis = light.dir_stopdot2.xyz;
	float cos_radius = clamp(light.dir_stopdot2.w, 0.0, 1.0);
	float sin_radius2 = (1.0 - cos_radius) * (1.0 + cos_radius);
	float distance = 0.0;
	float attenuation = 1.0;
	if (!environment) {
		const vec3 to_light = light.origin_r2.xyz - P;
		const float distance2 = dot(to_light, to_light);
		const float radius2 = light.origin_r2.w;
		if (radius2 <= 0.0 || distance2 <= radius2) {
			return;
		}
		distance = sqrt(distance2);
		axis = to_light / distance;
		sin_radius2 = radius2 / distance2;
		cos_radius = sqrt(1.0 - sin_radius2);
		attenuation = ltcSpotAttenuation(light, axis);
	}

	const float sin_radius = sqrt(sin_radius2);
	const float elevation = clamp(dot(N, axis), -1.0, 1.0);
	if (elevation <= -sin_radius || attenuation <= 0.0) {
		return;
	}

	const mat3 basis = orthonormalBasisZ(axis);
	const vec3 shadow_direction = normalize(basis * sampleConeZ(vec2(rand01(), rand01()), cos_radius));
	if (environment ? shadowedSky(P, shadow_direction)
	                : shadowed(P, shadow_direction, distance + shadow_offset_fudge)) {
		return;
	}

	const vec3 color = light.color_stopdot.rgb * attenuation;
	diffuse += color * ltcHorizonIntegral(elevation, sin_radius2) * ltcDiffuseAmplitude(axis, V, material);

	const float ndotv = clamp(dot(N, V), 0.0, 1.0);
	if (ndotv <= 1e-5) {
		return;
	}

	// Approximate the apparent disk by its inscribed octagon in LTC space.
	const vec2 ring[8] = vec2[8](
		vec2(1.0, 0.0), vec2(0.70710678), vec2(0.0, 1.0), vec2(-0.70710678, 0.70710678),
		vec2(-1.0, 0.0), vec2(-0.70710678), vec2(0.0, -1.0), vec2(0.70710678, -0.70710678));
	vec3 amplitude;
	const mat3 transform = ltcSpecularTransform(N, V, ndotv, material, amplitude) * basis;
	vec3 previous = normalize(transform * vec3(ring[7] * sin_radius, cos_radius));
	vec3 sum = vec3(0.0);
	for (int i = 0; i < 8; ++i) {
		const vec3 current = normalize(transform * vec3(ring[i] * sin_radius, cos_radius));
		sum += ltcIntegrateEdge(previous, current);
		previous = current;
	}
	specular += color * amplitude * ltcContourIntegral(sum);
}
#endif

#endif
