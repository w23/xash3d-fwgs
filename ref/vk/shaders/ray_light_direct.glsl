#define PROF_USE_REALTIME
#ifdef PROF_USE_REALTIME
#extension GL_ARB_gpu_shader_int64: enable
#extension GL_EXT_shader_realtime_clock: enable
#else
#extension GL_ARB_shader_clock: enable
#endif


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

#ifdef PROF_USE_REALTIME
// On mesa+amdgpu there's a clear gradient: pixels on top of screen take 2-3x longer to compute than bottom ones. Also,
// it does flicker a lot.
// Deltas are about 30000-100000 parrots
#define timeNow clockRealtime2x32EXT
#else
// clockARB doesn't give directly usable time values on mesa+amdgpu
// even deltas between them are not meaningful enough.
// On mesa+amdgpu clockARB() values are limited to lower 20bits, and they wrap around a lot.
// Absolute difference value are often 30-50% of the available range, so it's not that far off from wrapping around
// multiple times, rendering the value completely useless.
// Deltas are around 300000-500000 parrots.
// Other than that, the values seem uniform across the screen (as compared to realtime clock, which has a clearly
// visible gradient: top differences are larger than bottom ones.
#define timeNow clock2x32ARB
#endif

void main() {
	const uvec2 time_begin = timeNow();

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

	const uvec2 time_end = timeNow();
	//const uint64_t time_diff = time_end - time_begin;
	//const uint time_diff = time_begin.x - time_end.x;

#ifdef PROF_USE_REALTIME
	const uint64_t begin64 = time_begin.x | (uint64_t(time_begin.y) << 32);
	const uint64_t end64 = time_end.x | (uint64_t(time_end.y) << 32);
	const uint64_t time_diff = end64 - begin64;
	const float time_diff_f = float(time_diff) / 1e5;
#else
	const uint time_diff = time_begin.x - time_end.x;
	const float time_diff_f = float(time_diff & 0xfffffu) / 1e6;
#endif

	const uint prof_index = pix.x + pix.y * ubo.ubo.res.x;
#if LIGHT_POINT
	imageStore(out_light_point_diffuse, pix, vec4(diffuse, time_diff_f));
	imageStore(out_light_point_specular, pix, vec4(specular, 0.f));
	//imageStore(out_light_point_profile, pix, profile);
	//prof_direct_point[prof_index].data[0] = vec4(time_begin, time_end);
#endif

#if LIGHT_POLYGON
	imageStore(out_light_poly_diffuse, pix, vec4(diffuse, time_diff_f));
	imageStore(out_light_poly_specular, pix, vec4(specular, 0.f));
	//imageStore(out_light_poly_profile, pix, profile);
	prof_direct_poly.a[prof_index].data[0] = uvec4(time_begin, time_end);
#endif
}
