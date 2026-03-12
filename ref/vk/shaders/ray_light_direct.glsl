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

uvec4 decodeTemporalIndices(vec4 packed) {
	return uvec4(
		packed.x < 0.0 ? BRIGHTEST_LIGHT_INVALID_INDEX : uint(floor(packed.x)),
		packed.y < 0.0 ? BRIGHTEST_LIGHT_INVALID_INDEX : uint(floor(packed.y)),
		packed.z < 0.0 ? BRIGHTEST_LIGHT_INVALID_INDEX : uint(floor(packed.z)),
		packed.w < 0.0 ? BRIGHTEST_LIGHT_INVALID_INDEX : uint(floor(packed.w)));
}

vec4 encodeTemporalIndices(uvec4 indices) {
	return vec4(
		indices.x == BRIGHTEST_LIGHT_INVALID_INDEX ? -1.0 : float(indices.x),
		indices.y == BRIGHTEST_LIGHT_INVALID_INDEX ? -1.0 : float(indices.y),
		indices.z == BRIGHTEST_LIGHT_INVALID_INDEX ? -1.0 : float(indices.z),
		indices.w == BRIGHTEST_LIGHT_INVALID_INDEX ? -1.0 : float(indices.w));
}

void loadPrevTemporalBrightest(ivec2 p, out BrightestLights brightest) {
	brightest.diffuse_luminance0 = imageLoad(prev_temporal_0, p);
	brightest.diffuse_luminance1 = imageLoad(prev_temporal_1, p);
	brightest.specular_luminance0 = imageLoad(prev_temporal_2, p);
	brightest.specular_luminance1 = imageLoad(prev_temporal_3, p);
	brightest.indices0 = decodeTemporalIndices(imageLoad(prev_temporal_4, p));
	brightest.indices1 = decodeTemporalIndices(imageLoad(prev_temporal_5, p));
}

void storeTemporalBrightest(ivec2 p, BrightestLights brightest, bool confidence_disabled) {
	vec4 out0 = vec4(0.0);
	vec4 out1 = vec4(0.0);
	vec4 out2 = vec4(0.0);
	vec4 out3 = vec4(0.0);
	vec4 out4 = vec4(-1.0);
	vec4 out5 = vec4(-1.0);
	if (!confidence_disabled) {
		out0 = brightest.diffuse_luminance0;
		out1 = brightest.diffuse_luminance1;
		out2 = brightest.specular_luminance0;
		out3 = brightest.specular_luminance1;
		out4 = encodeTemporalIndices(brightest.indices0);
		out5 = encodeTemporalIndices(brightest.indices1);
	}
	imageStore(out_temporal_0, p, out0);
	imageStore(out_temporal_1, p, out1);
	imageStore(out_temporal_2, p, out2);
	imageStore(out_temporal_3, p, out3);
	imageStore(out_temporal_4, p, out4);
	imageStore(out_temporal_5, p, out5);
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
	float current_diffuse_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float prev_diffuse_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float current_specular_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	float prev_specular_weights[BRIGHTEST_LIGHTS_PER_TEXEL];
	vec3 prev_shading_normal = vec3(0.0);
	float prev_roughness = 0.0;
	bool confidence_disabled = (ubo.ubo.renderer_flags & RENDERER_FLAG_DISABLE_CONFIDENCE) != 0;
	float diffuse_confidence = 1.0;
	float specular_confidence = 1.0;

	initBrightestLights(brightest_lights);
	initBrightestLights(prev_brightest_lights);
	for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
		current_diffuse_weights[i] = 0.0;
		prev_diffuse_weights[i] = 0.0;
		current_specular_weights[i] = 0.0;
		prev_specular_weights[i] = 0.0;
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
		computeLightingPointDirect(lighting_position, shading_normal, -direction, material, diffuse, specular, flashlight_diffuse, flashlight_specular, brightest_lights, !confidence_disabled);
#else
		computeLighting(lighting_position, shading_normal, -direction, material, diffuse, specular, brightest_lights, !confidence_disabled);
#endif

		if (!confidence_disabled) {
			finalizeBrightestLightsWeights(brightest_lights, lighting_position, shading_normal, -direction, material.roughness);
			bool has_current_diffuse = hasTrackedBrightestLightByChannel(brightest_lights, BRIGHTEST_LIGHT_CHANNEL_DIFFUSE);
			bool has_current_specular = hasTrackedBrightestLightByChannel(brightest_lights, BRIGHTEST_LIGHT_CHANNEL_SPECULAR);
			diffuse_confidence = has_current_diffuse ? 0.0 : 1.0;
			specular_confidence = has_current_specular ? 0.0 : 1.0;
			processTemporalDiffuseLightEntries(brightest_lights, lighting_position, shading_normal, -direction, material.roughness, current_diffuse_weights);
			processTemporalSpecularLightEntries(brightest_lights, lighting_position, shading_normal, -direction, material.roughness, current_specular_weights);

		const vec3 prev_position = imageLoad(geometry_prev_position, pix).rgb;
		ivec2 reproj_pix = ivec2(-1);
		float reproj_depth_necessary = 0.0;
		float reproj_depth_threshold = 0.0;
		if (reprojectToPrevFramePixel(prev_position, res, reproj_pix, reproj_depth_necessary, reproj_depth_threshold)) {
			initBrightestLights(prev_brightest_lights);
			for (int i = 0; i < BRIGHTEST_LIGHTS_PER_TEXEL; ++i) {
				prev_diffuse_weights[i] = 0.0;
				prev_specular_weights[i] = 0.0;
			}
			loadPrevTemporalBrightest(reproj_pix, prev_brightest_lights);
			unpackTemporalNormalRoughness(imageLoad(prev_temporal_normal_roughness, reproj_pix), prev_shading_normal, prev_roughness);
			processTemporalDiffuseLightEntries(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness, prev_diffuse_weights);
			diffuse_confidence = computeTemporalDiffuseConfidence(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness);
			processTemporalSpecularLightEntries(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness, prev_specular_weights);
			specular_confidence = computeTemporalSpecularConfidence(prev_brightest_lights, lighting_position, prev_shading_normal, -direction, prev_roughness);
		}

		}
	}

	DEBUG_VALIDATE_RANGE_VEC3("direct.diffuse", diffuse, 0., 1e6);
	DEBUG_VALIDATE_RANGE_VEC3("direct.specular", specular, 0., 1e6);

	storeTemporalBrightest(pix, brightest_lights, confidence_disabled);
	imageStore(out_temporal_normal_roughness, pix, packTemporalNormalRoughness(shading_normal, material.roughness));
	imageStore(out_confidence, pix, confidence_disabled ? vec4(1.0, 1.0, 0.0, 0.0) : vec4(diffuse_confidence, specular_confidence, 0.0, 0.0));

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
