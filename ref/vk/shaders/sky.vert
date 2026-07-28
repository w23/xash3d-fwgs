#version 450

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
	mat4 inv_proj;
	mat4 inv_view;
	vec2 resolution;
} ubo;

layout(location=0) in vec3 a_pos;

void main() {
	gl_Position = ubo.mvp * vec4(a_pos.xyz, 1.);
}
