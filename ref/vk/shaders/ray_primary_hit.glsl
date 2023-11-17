#ifndef RAY_PRIMARY_HIT_GLSL_INCLUDED
#define RAY_PRIMARY_HIT_GLSL_INCLUDED
#extension GL_EXT_nonuniform_qualifier : enable

#include "utils.glsl"
#include "ray_primary_common.glsl"
#include "ray_kusochki.glsl"
#include "rt_geometry.glsl"
#include "color_spaces.glsl"

#include "noise.glsl" // for DEBUG_DISPLAY_SURFHASH

layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 7) uniform samplerCube skybox;

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
#ifndef RAY_BOUNCE
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
#else
	return textureLod(textures[nonuniformEXT(tex_index)], uv, 2.);
#endif
}

void primaryRayHit(rayQueryEXT rq, inout RayPayloadPrimary payload) {
	Geometry geom = readHitGeometry(rq, ubo.ubo.ray_cone_width, rayQueryGetIntersectionBarycentricsEXT(rq, true));
	const float hitT = rayQueryGetIntersectionTEXT(rq, true);  //gl_HitTEXT;
	const vec3 rayDirection = rayQueryGetWorldRayDirectionEXT(rq); //gl_WorldRayDirectionEXT
	payload.hit_t = vec4(geom.pos, hitT);
	payload.prev_pos_t = vec4(geom.prev_pos, 0.);

	const Kusok kusok = getKusok(geom.kusok_index);
	const Material material = kusok.material;

	if (kusok.material.tex_base_color == TEX_BASE_SKYBOX) {
		payload.emissive.rgb = texture(skybox, rayDirection).rgb;
		return;
	} else {
		payload.base_color_a = sampleTexture(material.tex_base_color, geom.uv, geom.uv_lods);
		payload.material_rmxx.r = sampleTexture(material.tex_roughness, geom.uv, geom.uv_lods).r * material.roughness;
		payload.material_rmxx.g = sampleTexture(material.tex_metalness, geom.uv, geom.uv_lods).r * material.metalness;

#ifndef RAY_BOUNCE
		const uint tex_normal = material.tex_normalmap;
		vec3 T = geom.tangent;
		if (tex_normal > 0 && dot(T,T) > .5) {
			T = normalize(T - dot(T, geom.normal_shading) * geom.normal_shading);
			const vec3 B = normalize(cross(geom.normal_shading, T));
			const mat3 TBN = mat3(T, B, geom.normal_shading);

// Get to KTX2 normal maps eventually
//#define KTX2
#ifdef KTX2
// We expect KTX2 normalmaps to have only 2 SNORM components.
// TODO: BC6H only can do signed or unsigned 16-bit floats. It can't normalize them on its own. So we either deal with
// sub-par 10bit precision for <1 values. Or do normalization manually in shader. Manual normalization implies prepa-
// ring normalmaps in a special way, i.e. scaling vector components to full f16 scale.
#define NORMALMAP_SNORM
#define NORMALMAP_2COMP
#endif

#ifdef NORMALMAP_SNORM // [-1..1]
			// TODO is this sampling correct for normal data?
			vec3 tnorm = sampleTexture(tex_normal, geom.uv, geom.uv_lods).xyz;
#else // Older UNORM [0..1]
			vec3 tnorm = sampleTexture(tex_normal, geom.uv, geom.uv_lods).xyz * 2. - 1.;
#endif

#ifndef NORMALMAP_2COMP
			// Older 8-bit PNG suffers from quantization.
			// Smoothen quantization by normalizing it
			tnorm = normalize(tnorm);
#endif

			tnorm.xy *= material.normal_scale;

			// Restore z based on scaled xy
			tnorm.z = sqrt(max(0., 1. - dot(tnorm.xy, tnorm.xy)));

			geom.normal_shading = normalize(TBN * tnorm);
		}
#endif
	}

	payload.normals_gs.xy = normalEncode(geom.normal_geometry);
	payload.normals_gs.zw = normalEncode(geom.normal_shading);

#if 1
	// Real correct emissive color
	//payload.emissive.rgb = kusok.emissive;
	//payload.emissive.rgb = kusok.emissive * SRGBtoLINEAR(payload.base_color_a.rgb);
	//payload.emissive.rgb = clamp((kusok.emissive * (1.0/3.0) / 20), 0, 1.0) * SRGBtoLINEAR(payload.base_color_a.rgb);
	//payload.emissive.rgb = (sqrt(sqrt(kusok.emissive)) * (1.0/3.0)) * SRGBtoLINEAR(payload.base_color_a.rgb);
	payload.emissive.rgb = (sqrt(kusok.emissive) / 8) * payload.base_color_a.rgb;
	//payload.emissive.rgb = kusok.emissive * payload.base_color_a.rgb;
#else
	// Fake texture color
	if (any(greaterThan(kusok.emissive, vec3(0.))))
		payload.emissive.rgb *= payload.base_color_a.rgb;
#endif

	const int model_index = rayQueryGetIntersectionInstanceIdEXT(rq, true);
	const ModelHeader model = getModelHeader(model_index);
	const vec4 color = model.color * kusok.material.base_color;

	payload.base_color_a *= color;
	payload.emissive.rgb *= color.rgb;

	if (ubo.ubo.debug_display_only == DEBUG_DISPLAY_DISABLED) {
		// Nop
	} else if (ubo.ubo.debug_display_only == DEBUG_DISPLAY_SURFHASH) {
		const uint hash = xxhash32(geom.kusok_index);
		payload.emissive.rgb = vec3(0xff & (hash>>16), 0xff & (hash>>8), 0xff & hash) / 255.;
	} else if (ubo.ubo.debug_display_only == DEBUG_DISPLAY_TRIHASH) {
		const int primitive_index = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
		const uint hash = xxhash32(geom.kusok_index + primitive_index * 2246822519U);
		payload.emissive.rgb = vec3(0xff & (hash>>16), 0xff & (hash>>8), 0xff & hash) / 255.;
	}
}

#endif // ifndef RAY_PRIMARY_HIT_GLSL_INCLUDED
