#ifndef LIGHT_COMMON_GLSL_INCLUDED
#define LIGHT_COMMON_GLSL_INCLUDED
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_kusochki.glsl"

#ifdef RAY_TRACE2
#include "ray_shadow_interface.glsl"
layout(location = PAYLOAD_LOCATION_SHADOW) rayPayloadEXT RayPayloadShadow payload_shadow;
#endif

#ifdef RAY_TRACE
uint traceShadowRay(vec3 pos, vec3 dir, float dist, uint flags) {
	payload_shadow.hit_type = SHADOW_HIT;
	traceRayEXT(tlas,
		flags,
		GEOMETRY_BIT_CASTS_SHADOW,
		SHADER_OFFSET_HIT_SHADOW_BASE, SBT_RECORD_SIZE, SHADER_OFFSET_MISS_SHADOW,
		pos, 0., dir, dist - shadow_offset_fudge, PAYLOAD_LOCATION_SHADOW);
	return payload_shadow.hit_type;
}
#endif

#if defined(RAY_QUERY)
bool shadowTestAlphaMask(vec3 pos, vec3 dir, float dist) {
	rayQueryEXT rq;
	const uint flags =  0
		// TODO figure out whether to turn off culling for alpha-tested geometry.
		// Alpha tested geometry usually comes as thick double-sided brushes.
		// Turning culling on makes shadows disappear from one side, which makes it look rather weird from one side.
		// Turning culling off makes such geometry cast "double shadow", which looks a bit weird from both sides.
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsNoOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_ALPHA_TEST, pos, 0., dir, dist);

	while (rayQueryProceedEXT(rq)) {
		// Alpha test, takes 10ms
		// TODO check other possible ways of doing alpha test. They might be more efficient:
		// 1. Do a separate ray query for alpha masked geometry. Reason: here we might accidentally do the expensive
		//    texture sampling for geometry that's ultimately invisible (i.e. behind walls). Also, shader threads congruence.
		//    Separate pass could be more efficient as it'd be doing the same thing for every invocation.
		// 2. Same as the above, but also with a completely independent TLAS. Why: no need to mask-check geometry for opaque-vs-alpha
		const uint instance_kusochki_offset = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
		const uint geometry_index = rayQueryGetIntersectionGeometryIndexEXT(rq, false);
		const uint kusok_index = instance_kusochki_offset + geometry_index;
		const Kusok kusok = getKusok(kusok_index);

		const uint primitive_index = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
		const uint first_index_offset = kusok.index_offset + primitive_index * 3;
		const uint vi1 = uint(getIndex(first_index_offset+0)) + kusok.vertex_offset;
		const uint vi2 = uint(getIndex(first_index_offset+1)) + kusok.vertex_offset;
		const uint vi3 = uint(getIndex(first_index_offset+2)) + kusok.vertex_offset;
		const vec2 uvs[3] = {
			GET_VERTEX(vi1).gl_tc,
			GET_VERTEX(vi2).gl_tc,
			GET_VERTEX(vi3).gl_tc,
		};
		const vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, false);
		const vec2 uv = baryMix(uvs[0], uvs[1], uvs[2], bary);
		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.material.tex_base_color)], uv);

		const float alpha_mask_threshold = .1f;
		if (texture_color.a >= alpha_mask_threshold) {
			rayQueryConfirmIntersectionEXT(rq);
		}
	}

	return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
}
#endif

bool shadowed(vec3 pos, vec3 dir, float dist) {
#ifdef RAY_TRACE
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const uint hit_type = traceShadowRay(pos, dir, dist, flags);
	return payload_shadow.hit_type == SHADOW_HIT;
#elif defined(RAY_QUERY)
	{
		dist -= shadow_offset_fudge;
		if (dist <= 0.)
			return false;

		const uint flags =  0
			// Culling for shadows breaks more things (e.g. de_cbble slightly off the ground boxes) than it probably fixes. Keep it turned off.
			//| gl_RayFlagsCullFrontFacingTrianglesEXT
			| gl_RayFlagsOpaqueEXT
			| gl_RayFlagsTerminateOnFirstHitEXT
			;
		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_CASTS_SHADOW, pos, 0., dir, dist);
		while (rayQueryProceedEXT(rq)) {}

		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
			return true;
	}

	return shadowTestAlphaMask(pos, dir, dist);

#else
#error RAY_TRACE or RAY_QUERY
#endif
}

bool shadowedSky(vec3 pos, vec3 dir) {
#ifdef RAY_TRACE
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const uint hit_type = traceShadowRay(pos, dir, dist, flags);
	return payload_shadow.hit_type != SHADOW_SKY;
#elif defined(RAY_QUERY)

	rayQueryEXT rq;
	const uint flags = 0
		// Culling for shadows breaks more things (e.g. de_cbble slightly off the ground boxes) than it probably fixes. Keep it turned off.
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		| gl_RayFlagsOpaqueEXT
		//| gl_RayFlagsTerminateOnFirstHitEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const float L = 10000.; // TODO Why 10k?
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_CASTS_SHADOW, pos, 0., dir, L);

	// Find closest intersection, and then check whether that was a skybox
	while (rayQueryProceedEXT(rq)) {}

	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
		const uint instance_kusochki_offset = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
		const uint geometry_index = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
		const uint kusok_index = instance_kusochki_offset + geometry_index;
		const Kusok kusok = getKusok(kusok_index);

		if (kusok.material.tex_base_color != TEX_BASE_SKYBOX)
			return true;
	}

	// check for alpha-masked surfaces separately
	const float hit_t = rayQueryGetIntersectionTEXT(rq, true);
	return shadowTestAlphaMask(pos, dir, hit_t);

#else
#error RAY_TRACE or RAY_QUERY
#endif
}

#endif //ifndef LIGHT_COMMON_GLSL_INCLUDED
