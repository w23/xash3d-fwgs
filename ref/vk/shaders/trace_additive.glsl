#ifndef TRACE_ADDITIVE_GLSL_INCLUDED
#define TRACE_ADDITIVE_GLSL_INCLUDED

void traceAdditive(vec3 pos, vec3 dir, float L, inout vec3 emissive, inout vec3 background) {
	const float additive_soft_overshoot = 16.;

#define WEIGHTED_OIT
#ifdef WEIGHTED_OIT
	// See https://jcgt.org/published/0002/02/09/
	// Morgan McGuire and Louis Bavoil, Weighted Blended Order-Independent Transparency, Journal of Computer Graphics Techniques (JCGT), vol. 2, no. 2, 122-141, 2013
	float alpha_sum = 0.;
	float alpha_m1_mul = 1.;
	vec3 color_sum = vec3(0.);
#endif

	rayQueryEXT rq;
	const uint flags = 0
		| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		| gl_RayFlagsNoOpaqueEXT // force all to be non-opaque
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_BLEND, pos, 0., dir, L + additive_soft_overshoot);
	while (rayQueryProceedEXT(rq)) {
		const MiniGeometry geom = readCandidateMiniGeometry(rq);
		const Kusok kusok = getKusok(geom.kusok_index);

		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.material.tex_base_color)], geom.uv);
		const float alpha = texture_color.a * kusok.model.color.a * geom.vertex_color.a;
		const vec3 color = kusok.model.color.rgb * texture_color.rgb * SRGBtoLINEAR(geom.vertex_color.rgb) * alpha;

		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);
		const float overshoot = hit_t - L;

//#define DEBUG_BLEND_MODES
#ifdef DEBUG_BLEND_MODES
		if (kusok.material.mode == MATERIAL_MODE_BLEND_GLOW) {
			emissive += vec3(1., 0., 0.);
			//ret += color * smoothstep(additive_soft_overshoot, 0., overshoot);
		} else if (kusok.material.mode == MATERIAL_MODE_BLEND_ADD) {
			emissive += vec3(0., 1., 0.);
		} else if (kusok.material.mode == MATERIAL_MODE_BLEND_MIX) {
			emissive += vec3(0., 0., 1.);
		} else if (kusok.material.mode == MATERIAL_MODE_TRANSLUCENT) {
			emissive += vec3(0., 1., 1.);
		} else if (kusok.material.mode == MATERIAL_MODE_OPAQUE) {
			emissive += vec3(1., 1., 1.);
		}
#else
		if (kusok.material.mode == MATERIAL_MODE_BLEND_GLOW) {
			// Glow is additive + small overshoot
			emissive += color * smoothstep(additive_soft_overshoot, 0., overshoot);
		} else if (overshoot < 0.) {
			if (kusok.material.mode == MATERIAL_MODE_BLEND_ADD) {
				emissive += color;
				//emissive += kusok.model.color.rgb;
				//emissive += kusok.emissive.rgb;
				//emissive += geom.vertex_color.rgb;
				//emissive += texture_color.rgb;
			} else if (kusok.material.mode == MATERIAL_MODE_BLEND_MIX) {
#ifdef WEIGHTED_OIT
				alpha_sum += alpha;
				alpha_m1_mul *= (1. - alpha);
				color_sum += color;
#else
				// FIXME OIT incorrect
				emissive = emissive * (1. - alpha) + color;
				background *= (1. - alpha);
#endif
			} else {
				// Signal unhandled blending type
				emissive += vec3(1., 0., 1.);
			}
		}
#endif // DEBUG_BLEND_MODES

#ifdef WEIGHTED_OIT
		if (alpha_sum > 1e-4)
			background = color_sum / alpha_sum * (1. - alpha_m1_mul) + background * alpha_m1_mul;
#endif
	}
}

#endif //ifndef TRACE_ADDITIVE_GLSL_INCLUDED
