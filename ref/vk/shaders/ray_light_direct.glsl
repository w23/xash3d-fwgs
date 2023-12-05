#extension GL_ARB_shader_clock: enable
#extension GL_EXT_shader_realtime_clock: enable
#extension GL_ARB_gpu_shader_int64: enable
#include "utils.glsl"
#include "noise.glsl"

#include "ray_kusochki.glsl"

vec4 profile = vec4(0.);

#include "light.glsl"

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

//#define timeNow clockRealtimeEXT
#define timeNow clockARB

void main() {
	const uint64_t time_begin = timeNow();

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
	material.baseColor = vec3(1.);
	material.emissive = vec3(0.f);
	material.metalness = material_data.g;
	material.roughness = material_data.r;

	const vec3 pos = imageLoad(position_t, pix).xyz;

	vec3 geometry_normal, shading_normal;
	readNormals(pix, geometry_normal, shading_normal);

	const vec3 throughput = vec3(1.);
	vec3 diffuse = vec3(0.), specular = vec3(0.);
	computeLighting(pos + geometry_normal * .001, shading_normal, throughput, -direction, material, diffuse, specular);

	const uint64_t time_end = timeNow();
	const uint64_t time_diff = time_end - time_begin;

	const float time_diff_f = float(time_diff) / 1e6;////float(time_diff >> 60);// / 1e6;

#if LIGHT_POINT
	imageStore(out_light_point_diffuse, pix, vec4(diffuse, time_diff_f));
	imageStore(out_light_point_specular, pix, vec4(specular, 0.f));
	//imageStore(out_light_point_profile, pix, profile);
#endif

#if LIGHT_POLYGON
	imageStore(out_light_poly_diffuse, pix, vec4(diffuse, time_diff_f));
	imageStore(out_light_poly_specular, pix, vec4(specular, 0.f));
	//imageStore(out_light_poly_profile, pix, profile);
#endif
}
