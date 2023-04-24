#ifndef TRACE_ADDITIVE_GLSL_INCLUDED
#define TRACE_ADDITIVE_GLSL_INCLUDED

vec3 traceAdditive(vec3 pos, vec3 dir, float L) {
	const float additive_soft_overshoot = 16.;
	vec3 ret = vec3(0., 0., 0.);
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
		const vec3 color = texture_color.rgb * kusok.emissive * texture_color.a * kusok.model.color.a * SRGBtoLINEAR(geom.color.rgb * geom.color.a);

		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);
		const float overshoot = hit_t - L;

//#define DEBUG_BLEND_MODES
#ifdef DEBUG_BLEND_MODES
		if (kusok.material.mode == MATERIAL_MODE_BLEND_GLOW) {
			ret += vec3(1., 0., 0.);
			//ret += color * smoothstep(additive_soft_overshoot, 0., overshoot);
		} else if (kusok.material.mode == MATERIAL_MODE_BLEND_ADD) {
			ret += vec3(0., 1., 0.);
		} else if (kusok.material.mode == MATERIAL_MODE_BLEND_MIX) {
			ret += vec3(0., 0., 1.);
		} else if (kusok.material.mode == MATERIAL_MODE_TRANSLUCENT) {
			ret += vec3(0., 1., 1.);
		} else if (kusok.material.mode == MATERIAL_MODE_OPAQUE) {
			ret += vec3(1., 1., 1.);
		}
#else
		if (overshoot < 0.)
			ret += color;
		else if (kusok.material.mode == MATERIAL_MODE_BLEND_GLOW)
			ret += color * smoothstep(additive_soft_overshoot, 0., overshoot);
#endif
	}
	return ret;
}

#endif //ifndef TRACE_ADDITIVE_GLSL_INCLUDED
