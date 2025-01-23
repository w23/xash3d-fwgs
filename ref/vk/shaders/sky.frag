#version 450

layout(set = 0, binding = 0) uniform UBO {
	mat4 mvp;
	mat4 inv_proj;
	mat4 inv_view;
	vec2 resolution;
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
}
