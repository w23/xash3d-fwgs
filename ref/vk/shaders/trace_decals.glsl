#ifndef TRACE_DECALS_GLSL_INCLUDED
#define TRACE_DECALS_GLSL_INCLUDED

#include "debug.glsl"
#include "alpha_blending.glsl"

// Traces geometry with decals.
// Modifies RayPayloadPrimary by blending in decals found near the original surface.
// Decals are sorted by kusok_index to ensure a stable blending order.
// This is a copy-paste from traceLegacyBlending.comp.
void traceDecals(inout RayPayloadPrimary payload) {
	const float MAX_DECALS_DISTANCE = 0.25; // DECAL_DEPTH_OFFSET from vk_decals.c with additional distance

	const vec3 geometry_normal = normalDecode(payload.normals_gs.xy);

	// trace from outside to polygon for right face culling
	const vec3 pos = payload.hit_t.xyz + geometry_normal * MAX_DECALS_DISTANCE;
	const vec3 dir = -geometry_normal;
	const float L = MAX_DECALS_DISTANCE;

	struct DecalEntry {
		uint id; // sorting by id for stable drawing order
		vec4 base_color_a;
		vec2 roughness_metalness; // TODO: decal normal maps
	};

	// VGPR usage :FeelsBadMan:
#define MAX_ENTRIES 8
	uint entries_count = 0;
	DecalEntry entries[MAX_ENTRIES];

	rayQueryEXT rq;
	const uint flags = 0
		| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		| gl_RayFlagsNoOpaqueEXT // force all to be non-opaque
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_BLEND, pos, 0., dir, L);
	while (rayQueryProceedEXT(rq)) {
		const MiniGeometry geom = readCandidateMiniGeometry(rq);
		const int model_index = rayQueryGetIntersectionInstanceIdEXT(rq, false);
		const ModelHeader model = getModelHeader(model_index);

		if (model.mode != MATERIAL_MODE_DECAL)
			continue;

		const Kusok kusok = getKusok(geom.kusok_index);
		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);

		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.material.tex_base_color)], geom.uv);
		const vec4 mm_color = model.color * kusok.material.base_color;
		float alpha = mm_color.a * texture_color.a * geom.vertex_color_srgb.a;
		vec3 color = mm_color.rgb * texture_color.rgb * SRGBtoLINEAR(geom.vertex_color_srgb.rgb);

		// Collect in random order
		entries[entries_count].id = geom.kusok_index;
		entries[entries_count].base_color_a = vec4(color, alpha);
		entries[entries_count].roughness_metalness = vec2(
			textureLod(textures[nonuniformEXT(kusok.material.tex_roughness)], geom.uv, 0.0).r * kusok.material.roughness,
			textureLod(textures[nonuniformEXT(kusok.material.tex_metalness)], geom.uv, 0.0).r * kusok.material.metalness);

		++entries_count;

		if (entries_count == MAX_ENTRIES) {
			// Max blended entries count exceeded
			// TODO show it as error somehow?
			break;
		}
	}

	float revealage = 1.;
	if (entries_count > 0) {
		// Tyno O(N^2) sort
		for (uint i = 0; i < entries_count; ++i) {
			uint min_i = i;
			for (uint j = i+1; j < entries_count; ++j) {
				if (entries[min_i].id < entries[j].id) {
					min_i = j;
				}
			}
			if (min_i != i) {
				DecalEntry tmp = entries[min_i];
				entries[min_i] = entries[i];
				entries[i] = tmp;
			}
		}

		// Composite everything in the right order
		for (uint i = 0; i < entries_count; ++i) {
			float a = entries[i].base_color_a.w;
			payload.base_color_a.rgb = mix(payload.base_color_a.rgb, entries[i].base_color_a.rgb, a);
			payload.base_color_a.a = coveredAlpha(payload.base_color_a.a, a);
			payload.material_rmxx.rg = mix(payload.material_rmxx.rg, entries[i].roughness_metalness, a);
			// Transient decal geometry uses emissive as legacy draw color, not a light source.
			payload.emissive.rgb *= 1.0 - a;
		}
	}
}

#endif //ifndef TRACE_DECALS_GLSL_INCLUDED
