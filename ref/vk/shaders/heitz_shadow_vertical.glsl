#include "heitz_shadow_common.glsl"
layout(set = 0, binding = 24, rgba16f) uniform readonly image2D heitz_noise_blurred;
layout(set = 0, binding = 25, rgba16f) uniform readonly image2D heitz_horizontal_sd;
layout(set = 0, binding = 26, rgba16f) uniform readonly image2D heitz_horizontal_ud;
layout(set = 0, binding = 27, rgba16f) uniform readonly image2D heitz_horizontal_ss;
layout(set = 0, binding = 28, rgba16f) uniform readonly image2D heitz_horizontal_us;
layout(set = 0, binding = 29, rgba16f) uniform readonly image2D heitz_poly_analytic_diffuse;
layout(set = 0, binding = 30, rgba16f) uniform readonly image2D heitz_point_analytic_diffuse;
layout(set = 0, binding = 31, rgba16f) uniform readonly image2D heitz_poly_analytic_specular;
layout(set = 0, binding = 32, rgba16f) uniform readonly image2D heitz_point_analytic_specular;

void heitzDirect(ivec2 res, ivec2 p, out vec3 diffuse, out vec3 specular) {
	const vec4 center = imageLoad(position_t, p);
	const vec3 normal = normalDecode(imageLoad(normals_gs, p).xy);
	const vec2 sigma = heitzSigma(imageLoad(heitz_noise_blurred, p).rg);
	const int radius = min(HEITZ_MAX_RADIUS, int(ceil(3.0 * max(sigma.x, sigma.y))));
	vec3 sd = imageLoad(heitz_horizontal_sd, p).rgb;
	vec3 ud = imageLoad(heitz_horizontal_ud, p).rgb;
	vec3 ss = imageLoad(heitz_horizontal_ss, p).rgb;
	vec3 us = imageLoad(heitz_horizontal_us, p).rgb;
	for (int y = -radius; y <= radius; ++y) {
		if (y == 0) continue;
		const ivec2 tap = p + ivec2(0, y);
		if (!heitzInside(tap, res) || center.w <= 0.0) continue;
		vec2 w = exp(-0.5 * float(y*y) / max(sigma*sigma, vec2(1e-8)));
		w *= heitzGeometryWeight(center, normal, tap);
		if (max(w.x,w.y) < 1e-5) continue;
		sd += imageLoad(heitz_horizontal_sd, tap).rgb*w.x;
		ud += imageLoad(heitz_horizontal_ud, tap).rgb*w.x;
		ss += imageLoad(heitz_horizontal_ss, tap).rgb*w.y;
		us += imageLoad(heitz_horizontal_us, tap).rgb*w.y;
	}
	if (ubo.ubo.debug_display_only == DEBUG_DISPLAY_SHADOW_DIFF ||
		ubo.ubo.debug_display_only == DEBUG_DISPLAY_SHADOW_SPEC) {
		diffuse = heitzFilterDecodeRatio(sd, ud);
		specular = heitzFilterDecodeRatio(ss, us);
		return;
	}
	// The common normalization cancels in S_N/U_N. U stays at this pixel.
	diffuse = (imageLoad(heitz_poly_analytic_diffuse, p).rgb
			 + imageLoad(heitz_point_analytic_diffuse, p).rgb) * heitzFilterDecodeRatio(sd, ud);
	specular = (imageLoad(heitz_poly_analytic_specular, p).rgb
			  + imageLoad(heitz_point_analytic_specular, p).rgb) * heitzFilterDecodeRatio(ss, us);
}
