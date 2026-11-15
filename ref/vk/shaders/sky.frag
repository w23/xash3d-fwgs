#version 450
#extension GL_GOOGLE_include_directive : require

#include "fog.glsl"

layout(set = 0, binding = 0) uniform UBO {
	mat4 mvp;
	mat4 inv_proj;
	mat4 inv_view;
	vec2 resolution;
	vec2 pad_;
	vec4 fog_color;
	vec4 fog_params;
	vec4 fog_plane;
} ubo;

layout(set = 0, binding = 1) uniform samplerCube skybox;

layout(location = 0) out vec4 out_color;

vec3 clipToWorldSpace(vec3 clip) {
	const vec4 eye_space = ubo.inv_proj * vec4(clip, 1.);
	return (ubo.inv_view * vec4(eye_space.xyz / eye_space.w, 1.)).xyz;
}

vec3 getDirection(in vec2 uv) {
	uv = uv * 2. - 1.;
	const vec3 world_near = clipToWorldSpace(vec3(uv, 0.));
	const vec3 world_far = clipToWorldSpace(vec3(uv, 1.));
	return normalize(world_far - world_near);
}

void main() {
	const vec2 uv = gl_FragCoord.xy / ubo.resolution;
	const vec3 direction = getDirection(uv);
	out_color = texture(skybox, direction);
	// Match GL's MakeSkyVec cube at farClip / 2, measured along the view axis.
	const vec3 cube = abs(direction);
	const float fog_coord = abs(dot(direction, ubo.fog_plane.xyz)) * ubo.fog_plane.w / max(max(cube.x, cube.y), cube.z);
	out_color.rgb = applyFog(out_color.rgb, ubo.fog_color, ubo.fog_params, fog_coord);
}
