#version 460 core
#extension GL_EXT_ray_tracing: require
#extension GL_EXT_shader_16bit_storage : require

#define GLSL
#include "ray_interop.h"
#undef GLSL

layout(set = 0, binding = 30, std430) readonly buffer ModelHeaders { ModelHeader a[]; } model_headers;
layout(set = 0, binding = 31, std430) readonly buffer Kusochki { Kusok a[]; } kusochki;
layout(set = 0, binding = 32, std430) readonly buffer Indices { uint16_t a[]; } indices;
layout(set = 0, binding = 33, std430) readonly buffer Vertices { Vertex a[]; } vertices;

#include "ray_kusochki.glsl"
#include "ray_common.glsl"

layout(location = PAYLOAD_LOCATION_SHADOW) rayPayloadInEXT RayPayloadShadow payload_shadow;

void main() {
	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;
	const Kusok kusok = getKusok(kusok_index);
	const uint tex_base_color = kusok.material.tex_base_color;

	payload_shadow.hit_type = (kusok.material.tex_base_color != TEX_BASE_SKYBOX) ? SHADOW_HIT : SHADOW_SKY ;
}
