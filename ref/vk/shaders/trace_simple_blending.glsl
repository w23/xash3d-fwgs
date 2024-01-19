#ifndef TRACE_SIMPLE_BLENDING_GLSL_INCLUDED
#define TRACE_SIMPLE_BLENDING_GLSL_INCLUDED

#include "debug.glsl"

// Traces geometry with simple blending. Simple means that it's only additive or mix/coverage, and it doesn't participate in lighting, and it doesn't reflect/refract rays.
// Done in sRGB-γ space for legacy-look reasons.
// Returns vec4(emissive_srgb.rgb, revealage)
vec4 traceLegacyBlending(vec3 pos, vec3 dir, float L) {
	const float kGlowSoftOvershoot = 16.;
	vec3 emissive = vec3(0.);

	// TODO probably a better way would be to sort only MIX entries.
	// ADD/GLOW are order-independent relative to each other, but not to MIX
	struct BlendEntry {
		vec3 add;
		float blend;
		float depth;
	};

	// VGPR usage :FeelsBadMan:
#define MAX_ENTRIES 8
	uint entries_count = 0;
	BlendEntry entries[MAX_ENTRIES];

	rayQueryEXT rq;
	const uint flags = 0
		| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		| gl_RayFlagsNoOpaqueEXT // force all to be non-opaque
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_BLEND, pos, 0., dir, L + kGlowSoftOvershoot);
	while (rayQueryProceedEXT(rq)) {
		const MiniGeometry geom = readCandidateMiniGeometry(rq);
		const int model_index = rayQueryGetIntersectionInstanceIdEXT(rq, false);
		const ModelHeader model = getModelHeader(model_index);
		const Kusok kusok = getKusok(geom.kusok_index);
		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);

		// Engage soft particles/blending gradually after a certain distance.
		// Makes see-beams-through-weapons visual glitch mostly disappear.
		// Engage-vs-Full difference to make soft particles appear gradually, and not pop immediately.
		const float kOvershootEngageDist = 20.;
		const float kOvershootFullDist = 40.;
		const float glow_soft_overshoot = kGlowSoftOvershoot * smoothstep(kOvershootEngageDist, kOvershootFullDist, hit_t);

		const float overshoot = hit_t - L;

// Use soft alpha depth effect globally, not only for glow
#define GLOBAL_SOFT_DEPTH
#ifndef GLOBAL_SOFT_DEPTH
		if (overshoot > 0. && model.mode != MATERIAL_MODE_BLEND_GLOW)
			continue;
#endif


//#define DEBUG_BLEND_MODES
#ifdef DEBUG_BLEND_MODES
		if (model.mode == MATERIAL_MODE_BLEND_GLOW) {
			emissive += vec3(1., 0., 0.);
			//emissive += color * smoothstep(glow_soft_overshoot, 0., overshoot);
		} else if (model.mode == MATERIAL_MODE_BLEND_ADD) {
			emissive += vec3(0., 1., 0.);
		} else if (model.mode == MATERIAL_MODE_BLEND_MIX) {
			emissive += vec3(0., 0., 1.);
		} else if (model.mode == MATERIAL_MODE_TRANSLUCENT) {
			emissive += vec3(0., 1., 1.);
		} else if (model.mode == MATERIAL_MODE_OPAQUE) {
			emissive += vec3(1., 1., 1.);
		}
#else
		// Note that simple blending is legacy blending really.
		// It is done in sRGB-γ space for correct legacy-look reasons.
		const vec4 texture_color = LINEARtoSRGB(texture(textures[nonuniformEXT(kusok.material.tex_base_color)], geom.uv));
		const vec4 mm_color = model.color * kusok.material.base_color;
		float alpha = mm_color.a * texture_color.a * geom.vertex_color_srgb.a;
		vec3 color = mm_color.rgb * texture_color.rgb * geom.vertex_color_srgb.rgb * alpha;

#ifdef GLOBAL_SOFT_DEPTH
		const float overshoot_factor = smoothstep(glow_soft_overshoot, 0., overshoot);
		color *= overshoot_factor;
#endif

		if (model.mode == MATERIAL_MODE_BLEND_GLOW) {
			// Glow is additive + small overshoot
#ifndef GLOBAL_SOFT_DEPTH
			const float overshoot_factor = smoothstep(glow_soft_overshoot, 0., overshoot);
			color *= overshoot_factor;
#endif
			alpha = 0.;
		} else if (model.mode == MATERIAL_MODE_BLEND_ADD) {
			// Additive doesn't attenuate what's behind
			alpha = 0.;
		} else if (model.mode == MATERIAL_MODE_BLEND_MIX) {
			// Handled in composite step below
		} else {
			// Signal unhandled blending type
			color = vec3(1., 0., 1.);
		}

		// Collect in random order
		entries[entries_count].add = color;
		entries[entries_count].blend = alpha;
		entries[entries_count].depth = hit_t;

		++entries_count;

		if (entries_count == MAX_ENTRIES) {
			// Max blended entries count exceeded
			// TODO show it as error somehow?
			break;
		}
#endif // !DEBUG_BLEND_MODES
	}

	float revealage = 1.;
	if (entries_count > 0) {
		// Tyno O(N^2) sort
		for (uint i = 0; i < entries_count; ++i) {
			uint min_i = i;
			for (uint j = i+1; j < entries_count; ++j) {
				if (entries[min_i].depth > entries[j].depth) {
					min_i = j;
				}
			}
			if (min_i != i) {
				BlendEntry tmp = entries[min_i];
				entries[min_i] = entries[i];
				entries[i] = tmp;
			}
		}

		// Composite everything in the right order
		for (uint i = 0; i < entries_count; ++i) {
			emissive += entries[i].add * revealage;
			revealage *= 1. - entries[i].blend;
		}
	}

	DEBUG_VALIDATE_RANGE_VEC3("blend.emissive", emissive, 0., 1e6);
	DEBUG_VALIDATE_RANGE(revealage, 0., 1.);

	return vec4(emissive, revealage);
}

#endif //ifndef TRACE_SIMPLE_BLENDING_GLSL_INCLUDED
