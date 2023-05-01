#ifndef TRACE_SIMPLE_BLENDING_GLSL_INCLUDED
#define TRACE_SIMPLE_BLENDING_GLSL_INCLUDED

// Traces geometry with simple blending. Simple means that it's only additive or mix/coverage, and it doesn't participate in lighting, and it doesn't reflect/refract rays.
void traceSimpleBlending(vec3 pos, vec3 dir, float L, inout vec3 emissive, inout vec3 background) {
	const float glow_soft_overshoot = 16.;

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
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_BLEND, pos, 0., dir, L + glow_soft_overshoot);
	while (rayQueryProceedEXT(rq)) {
		const MiniGeometry geom = readCandidateMiniGeometry(rq);
		const int model_index = rayQueryGetIntersectionInstanceIdEXT(rq, false);
		const ModelHeader model = getModelHeader(model_index);
		const Kusok kusok = getKusok(geom.kusok_index);
		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);
		const float overshoot = hit_t - L;
		if (overshoot > 0. && model.mode != MATERIAL_MODE_BLEND_GLOW)
			continue;

//#define DEBUG_BLEND_MODES
#ifdef DEBUG_BLEND_MODES
		if (model.mode == MATERIAL_MODE_BLEND_GLOW) {
			emissive += vec3(1., 0., 0.);
			//ret += color * smoothstep(glow_soft_overshoot, 0., overshoot);
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
		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.material.tex_base_color)], geom.uv);
		const vec4 mm_color = model.color * kusok.material.base_color;
		float alpha = mm_color.a * texture_color.a * geom.vertex_color.a;
		vec3 color = mm_color.rgb * SRGBtoLINEAR(texture_color.rgb) * geom.vertex_color.rgb * alpha;

		if (model.mode == MATERIAL_MODE_BLEND_GLOW) {
			// Glow is additive + small overshoot
			const float overshoot_factor = smoothstep(glow_soft_overshoot, 0., overshoot);
			color *= overshoot_factor;
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

	if (entries_count == 0)
		return;

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
	float revealage = 1.;
	vec3 add = vec3(0.);
	for (uint i = 0; i < entries_count; ++i) {
		add += entries[i].add * revealage;
		revealage *= 1. - entries[i].blend;
	}

	emissive = emissive * revealage + add;
	background *= revealage;
}

#endif //ifndef TRACE_SIMPLE_BLENDING_GLSL_INCLUDED
