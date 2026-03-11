#include "utils.glsl"
#include "noise.glsl"

#include "ray_kusochki.glsl"
#include "color_spaces.glsl"

#include "light.glsl"
#include "temporal_reprojection.glsl"

const int TEMPORAL_PARALLAX_INDIRECT_SCALE = 2;
const int TEMPORAL_PARALLAX_KERNEL = 1;

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

void main() {
#ifdef RAY_TRACE
	const ivec2 res = ivec2(gl_LaunchSizeEXT.xy);
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

	const vec4 target = ubo.ubo.inv_proj * vec4(uv.x, uv.y, 1, 1);
	const vec3 direction = normalize((ubo.ubo.inv_view * vec4(target.xyz, 0)).xyz);
	const vec3 origin = (ubo.ubo.inv_view * vec4(0., 0., 0., 1.)).xyz;
	const vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0., 0., 0., 1.)).xyz;

	const vec4 material_data = imageLoad(material_rmxx, pix);

	MaterialProperties material;
	material.base_color = SRGBtoLINEAR(imageLoad(base_color_a, pix).rgb);
	material.metalness = material_data.g;
	material.roughness = material_data.r;

#ifdef BRDF_COMPARE
	g_mat_gltf2 = pix.x > ubo.ubo.res.x / 2.;
#endif

	const vec4 pos_t = imageLoad(position_t, pix);

	vec3 diffuse = vec3(0.0), specular = vec3(0.0);
#if LIGHT_POINT
	vec3 flashlight_diffuse = vec3(0.0), flashlight_specular = vec3(0.0);
#endif
	vec3 geometry_normal = vec3(0.0), shading_normal = vec3(0.0);
	vec3 lighting_position = pos_t.xyz;
	BrightestLights brightest_lights;
	BrightestLights prev_brightest_lights;
	float current_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float prev_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	vec3 prev_shading_normal = vec3(0.0);
	float prev_roughness = 0.0;
	float diffuse_confidence = 0.0;
	float specular_confidence = 0.0;
    vec4 packed_brightest_0 = vec4(-1.0);
    vec4 packed_brightest_1 = vec4(-1.0);

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
#if LIGHT_POINT
		computeLightingPointDirect(lighting_position, shading_normal, -direction, material, diffuse, specular, flashlight_diffuse, flashlight_specular, brightest_lights);
#else
		computeLighting(lighting_position, shading_normal, -direction, material, diffuse, specular, brightest_lights);
#endif

		processTemporalLightEntries(brightest_lights, lighting_position, shading_normal, -direction, material.roughness, current_weights);

		const vec3 prev_position = imageLoad(geometry_prev_position, pix).rgb;
		ivec2 reproj_pix = ivec2(-1);
		float reproj_depth_necessary = 0.0;
		float reproj_depth_threshold = 0.0;
		if (reprojectToPrevFramePixel(prev_position, res, reproj_pix, reproj_depth_necessary, reproj_depth_threshold)) {
			initBrightestLights(prev_brightest_lights);
			for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
				prev_weights[i] = 0.0;
			}
			unpackBrightestLights(imageLoad(prev_temporal_0, reproj_pix), imageLoad(prev_temporal_1, reproj_pix), prev_brightest_lights);
			unpackTemporalNormalRoughness(imageLoad(prev_temporal_normal_roughness, reproj_pix), prev_shading_normal, prev_roughness);
			processTemporalLightEntries(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness, prev_weights);
			diffuse_confidence = computeTemporalConfidence(brightest_lights, prev_brightest_lights, current_weights, prev_weights);
		}

		const float average_ray_length = sampleAverageReflectionRayLength(pix, res, TEMPORAL_PARALLAX_INDIRECT_SCALE, TEMPORAL_PARALLAX_KERNEL);
		ivec2 parallax_pix = ivec2(-1);
		if (parallaxReprojectToPrevFramePixel(pos_t.xyz, prev_position, geometry_normal, origin, prev_origin, average_ray_length, res, parallax_pix)) {
			initBrightestLights(prev_brightest_lights);
			for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
				prev_weights[i] = 0.0;
			}
			unpackBrightestLights(imageLoad(prev_temporal_0, parallax_pix), imageLoad(prev_temporal_1, parallax_pix), prev_brightest_lights);
			unpackTemporalNormalRoughness(imageLoad(prev_temporal_normal_roughness, parallax_pix), prev_shading_normal, prev_roughness);
			processTemporalLightEntries(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness, prev_weights);
			specular_confidence = computeTemporalConfidence(brightest_lights, prev_brightest_lights, current_weights, prev_weights);
		}
	}

	DEBUG_VALIDATE_RANGE_VEC3("direct.diffuse", diffuse, 0., 1e6);
	DEBUG_VALIDATE_RANGE_VEC3("direct.specular", specular, 0., 1e6);

	packBrightestLights(brightest_lights, packed_brightest_0, packed_brightest_1);
	imageStore(out_temporal_0, pix, packed_brightest_0);
	imageStore(out_temporal_1, pix, packed_brightest_1);
	imageStore(out_temporal_normal_roughness, pix, packTemporalNormalRoughness(shading_normal, material.roughness));
	imageStore(out_confidence, pix, vec4(diffuse_confidence, specular_confidence, 0.0, 0.0));

#if LIGHT_POINT
	imageStore(out_light_point_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_point_specular, pix, vec4(specular, 0.f));
	imageStore(out_light_point_flashlight_diffuse, pix, vec4(flashlight_diffuse, 0.f));
	imageStore(out_light_point_flashlight_specular, pix, vec4(flashlight_specular, 0.f));
#endif

#if LIGHT_POLYGON
	imageStore(out_light_poly_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_poly_specular, pix, vec4(specular, 0.f));
#endif
}


