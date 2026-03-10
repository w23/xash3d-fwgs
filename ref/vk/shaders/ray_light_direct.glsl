#include "utils.glsl"
#include "noise.glsl"

#include "ray_kusochki.glsl"
#include "color_spaces.glsl"

#include "light.glsl"

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

void main() {
#ifdef RAY_TRACE
	const vec2 uv = (gl_LaunchIDEXT.xy + .5) / gl_LaunchSizeEXT.xy * 2. - 1.;
	const ivec2 pix = ivec2(gl_LaunchIDEXT.xy);
#elif defined(RAY_QUERY)
	const ivec2 pix = ivec2(gl_GlobalInvocationID);
	const ivec2 res = ubo.ubo.res;
	if (any(greaterThanEqual(pix, res))) {
		return;
	}
	const vec2 uv = (gl_GlobalInvocationID.xy + .5) / res * 2. - 1.;
#else
#error You have two choices here. Ray trace, or Rake Yuri. So what it's gonna be, huh? Choose wisely.
#endif

	rand01_state = ubo.ubo.random_seed + pix.x * 1833 + pix.y * 31337;

	// FIXME incorrect for reflection/refraction
	const vec4 target    = ubo.ubo.inv_proj * vec4(uv.x, uv.y, 1, 1);
	const vec3 direction = normalize((ubo.ubo.inv_view * vec4(target.xyz, 0)).xyz);

	const vec4 material_data = imageLoad(material_rmxx, pix);

	MaterialProperties material;
	material.base_color = SRGBtoLINEAR(imageLoad(base_color_a, pix).rgb);
	material.metalness = material_data.g;
	material.roughness = material_data.r;

#ifdef BRDF_COMPARE
	g_mat_gltf2 = pix.x > ubo.ubo.res.x / 2.;
#endif

	const vec4 pos_t = imageLoad(position_t, pix);

	vec3 diffuse = vec3(0.), specular = vec3(0.);
	vec3 geometry_normal = vec3(0.0), shading_normal = vec3(0.0);
	vec3 lighting_position = pos_t.xyz;
	BrightestLightEntry brightest_lights[BRIGHTEST_LIGHTS_PER_TEXEL];
	BrightestLightEntry prev_brightest_lights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	vec3 prev_shading_normal = vec3(0.0);
	float prev_roughness = 0.0;
	float confidence = 0.0;

	initBrightestLights(brightest_lights);
	initBrightestLights(prev_brightest_lights);
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		current_weights[i] = 0.0;
		prev_weights[i] = 0.0;
	}

	if (pos_t.w > 0.) {
		const vec4 packed_normal = imageLoad(normals_gs, pix);
		geometry_normal = normalDecode(packed_normal.xy);
		shading_normal = normalDecode(packed_normal.zw);
		lighting_position = pos_t.xyz + geometry_normal * .001;
#ifdef DEBUG_VALIDATE_EXTRA
		if (IS_INVALIDV(pos_t.xyz) || IS_INVALIDV(geometry_normal)) {
			debugPrintfEXT("ray_light_direct.glsl:%d INVALID pos_t.xyz=(%f,%f,%f) geometry_normal=(%f,%f,%f) packed_normal=(%f,%f,%f,%f)",
				__LINE__, PRIVEC3(pos_t.xyz), PRIVEC3(geometry_normal), PRIVEC4(packed_normal));
		} else
#endif
		computeLighting(lighting_position, shading_normal, -direction, material, diffuse, specular, brightest_lights);

		unpackBrightestLights(imageLoad(prev_temporal, pix), prev_brightest_lights);
		unpackTemporalNormalRoughness(imageLoad(prev_temporal_normal_roughness, pix), prev_shading_normal, prev_roughness);

		processTemporalLightEntries(brightest_lights, lighting_position, shading_normal, -direction, material.roughness, current_weights);
		processTemporalLightEntries(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness, prev_weights);
		confidence = computeTemporalConfidence(brightest_lights, prev_brightest_lights, current_weights, prev_weights);
	}

	DEBUG_VALIDATE_RANGE_VEC3("direct.diffuse", diffuse, 0., 1e6);
	DEBUG_VALIDATE_RANGE_VEC3("direct.specular", specular, 0., 1e6);

	imageStore(out_temporal, pix, packBrightestLights(brightest_lights));
	imageStore(out_temporal_normal_roughness, pix, packTemporalNormalRoughness(shading_normal, material.roughness));
	imageStore(out_confidence, pix, vec4(confidence, 0.0, 0.0, 0.0));

#if LIGHT_POINT
	imageStore(out_light_point_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_point_specular, pix, vec4(specular, 0.f));
#endif

#if LIGHT_POLYGON
	imageStore(out_light_poly_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_poly_specular, pix, vec4(specular, 0.f));
#endif
}
