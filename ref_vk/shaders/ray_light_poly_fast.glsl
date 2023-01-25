#extension GL_EXT_control_flow_attributes : require

#include "utils.glsl"
#include "noise.glsl"

#define GLSL
#include "ray_interop.h"
#undef GLSL

#define X(index, name, format) layout(set=0,binding=index,format) uniform readonly image2D name;
RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

#ifdef LIGHT_VISIBLITY_COLLECT
layout(set=0,binding=20,r32ui) uniform writeonly uimage2D OUT_VISIBLITY_IMAGE;
#else
layout(set=0,binding=13,r32ui) uniform readonly uimage2D VISIBLITY_IMAGE;

layout(set=0,binding=20,rgba16f) uniform writeonly image2D OUT_DIFFUSE_IMAGE;
layout(set=0,binding=21,rgba16f) uniform writeonly image2D OUT_SPECULAR_IMAGE;
#endif

layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;

#include "ray_kusochki.glsl"

#undef SHADER_OFFSET_HIT_SHADOW_BASE
#define SHADER_OFFSET_HIT_SHADOW_BASE 0
#undef SHADER_OFFSET_MISS_SHADOW
#define SHADER_OFFSET_MISS_SHADOW 0
#undef PAYLOAD_LOCATION_SHADOW
#define PAYLOAD_LOCATION_SHADOW 0

#define BINDING_LIGHTS 7
#define BINDING_LIGHT_CLUSTERS 8
#include "light.glsl"

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

#define ALL_BITS_U32 4294967295
#define UINT_BITS_COUNT 32
#define MAX_LIGHTS 512
#define BITMASKS_COUNT (MAX_LIGHTS / UINT_BITS_COUNT + 1)

shared uint shared_visiblity[ BITMASKS_COUNT ];

#ifndef LIGHT_VISIBLITY_COLLECT
shared uint shared_sample_always[ BITMASKS_COUNT ];
#endif

#if LIGHT_POLYGON
vec3 fastSampleShadowPosPoly(const PolygonLight poly, vec3 rnd) {
	const uint vertices_offset = poly.vertices_count_offset & 0xffffu;
	const uint vertices_count = poly.vertices_count_offset >> 16;
	const uint selected = 2 + uint(rnd.z * (float(vertices_count - 2) + .99));
	const vec3 light_dir = baryMix(
		lights.m.polygon_vertices[vertices_offset + 0].xyz,
		lights.m.polygon_vertices[vertices_offset + selected - 1].xyz,
		lights.m.polygon_vertices[vertices_offset + selected].xyz,
		rnd.xy);
	return light_dir;
}
#endif

void main() {

	const ivec2 loc = ivec2(gl_LocalInvocationID);
	const ivec2 pix = ivec2(gl_GlobalInvocationID);
	const ivec2 res = ivec2(imageSize(material_rmxx));
	if (any(greaterThanEqual(pix, res))) {
		return;
	}
	const vec2 uv = (gl_GlobalInvocationID.xy + .5) / res * 2. - 1.;

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

	vec3 geometry_normal, shading_normal;
	readNormals(pix, geometry_normal, shading_normal);

	const vec3 throughput = vec3(1.);
	vec3 diffuse = vec3(0.), specular = vec3(0.);

	const vec3 view_dir = -direction;

	const vec3 pos = imageLoad(position_t, pix).xyz + geometry_normal * 0.001;

	const vec3 rnd = vec3(rand01(), rand01(), rand01());

#ifdef LIGHT_VISIBLITY_COLLECT

	uint local_visiblity[ BITMASKS_COUNT ];
	for(uint b = 0; b < BITMASKS_COUNT; ++b) {
		local_visiblity[b] = 0;
	}

#if LIGHT_POINT
	const uint num_lights = lights.m.num_point_lights;
#else
	const uint num_lights = lights.m.num_polygons;
#endif
	const uint loc_id = loc.x + loc.y * 8;
	const uint loc_count = 8 * 8;
	const uint ids_step = max(1, num_lights / loc_count);
	const uint id_start = ids_step * loc_id;
	const uint id_end = loc_id == (loc_count - 1) ? num_lights : min(id_start + ids_step + 1, num_lights);
	for (uint i = id_start; i < id_end; ++i) {

#ifdef LIGHT_POINT

		// TODO: it's bad, make lightweight function like for polygon lights
		diffuse = vec3(0.);
		specular = vec3(0.);
		sampleSinglePointLight(pos + shading_normal * 0.001, geometry_normal, throughput, view_dir, material, lights.m.point_lights[i], diffuse, specular);

		if (any(equal(diffuse, vec3(0.))) && any(equal(specular, vec3(0.))))
			continue;

#else // LIGHT_POLYGON

		const PolygonLight poly = lights.m.polygons[i];

		const float plane_dist = dot(poly.plane, vec4(pos, 1.f));

		if (plane_dist < 0.)
			continue;

		const vec3 light_sample_dir = fastSampleShadowPosPoly(poly, rnd) - pos;
		const float dist = length(light_sample_dir) - 1.;

		if (shadowed(pos, normalize(light_sample_dir), dist))
			continue;
#endif
		local_visiblity[ i / UINT_BITS_COUNT] |= ( 1 << (i % UINT_BITS_COUNT));
	}

	for(uint b = 0; b < BITMASKS_COUNT; ++b) {
		shared_visiblity[b] = 0;
	}

	memoryBarrierShared();

	for(uint b = 0; b < BITMASKS_COUNT; ++b) {
		atomicOr(shared_visiblity[b], local_visiblity[b]);
	}

	memoryBarrierShared();

	ivec2 tile_pix = (pix / 8) * 8;
	for(int b = 0; b < BITMASKS_COUNT; ++b) {
		ivec2 p = tile_pix + ivec2(b % 8, b / 8);
		if (any(greaterThanEqual(p, res)) || any(lessThan(p, ivec2(0)))) {
			continue;
		}
		imageStore(OUT_VISIBLITY_IMAGE, p, uvec4(shared_visiblity[b]));
	}

#else // sample this lights

	const int bitmask_id = loc.x + loc.y * 8;
	shared_visiblity[bitmask_id] = 0;
	shared_sample_always[bitmask_id] = ALL_BITS_U32;
	for (int x = -VISIBLITY_KERNEL; x <= VISIBLITY_KERNEL; ++x) {
		for (int y = -VISIBLITY_KERNEL; y <= VISIBLITY_KERNEL; ++y) {
			ivec2 p = pix + ivec2(x, y) * 8;
			if (any(greaterThanEqual(p, res)) || any(lessThan(p, ivec2(0)))) {
				continue;
			}
			const uint current_vis = imageLoad(VISIBLITY_IMAGE, p).x;
			shared_visiblity[bitmask_id] |= current_vis;
			shared_sample_always[bitmask_id] &= current_vis;
		}
	}

	memoryBarrierShared();

#if LIGHT_POINT
	const uint num_lights = lights.m.num_point_lights;
#else
	const uint num_lights = lights.m.num_polygons;
#endif

	uint visiblity_mask = 0;
	uint sample_always_mask = 0;
	for (uint i = 0; i < num_lights; ++i) {

		if (i % UINT_BITS_COUNT == 0) {
			visiblity_mask = shared_visiblity[i / UINT_BITS_COUNT];
			sample_always_mask = shared_sample_always[i / UINT_BITS_COUNT];
		}
		
		if ((visiblity_mask & (1 << (i % UINT_BITS_COUNT))) == 0)
			continue;

#ifdef LIGHT_POINT

		sampleSinglePointLight(pos, shading_normal, throughput, view_dir, material, lights.m.point_lights[i], diffuse, specular);

#else // LIGHT_POLYGON

		const PolygonLight poly = lights.m.polygons[i];

		const float plane_dist = dot(poly.plane, vec4(pos, 1.f));

		if (plane_dist < 0.)
			continue;

		const vec4 light_sample_dir = getPolygonLightSampleSimpleSolid(pos, view_dir, poly);

		if (light_sample_dir.w <= 0.)
			continue;

		const float dist = - dot(vec4(pos, 1.f), poly.plane) / dot(light_sample_dir.xyz, poly.plane.xyz);

#ifdef EXTREME_SPEEDUP
		if ((sample_always_mask & (1 << (i % UINT_BITS_COUNT))) == 0)
#endif
		if (shadowed(pos, light_sample_dir.xyz, dist))
			continue;

		vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
		evalSplitBRDF(shading_normal, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
		const float estimate = light_sample_dir.w;
		const vec3 emissive = poly.emissive * estimate;
		diffuse += emissive * poly_diffuse;
		specular += emissive * poly_specular;
#endif
	}

	imageStore(OUT_DIFFUSE_IMAGE, pix, vec4(diffuse / LIGHT_DIVIDE, 0.f));
	imageStore(OUT_SPECULAR_IMAGE, pix, vec4(specular / LIGHT_DIVIDE, 0.f));

#endif
}
