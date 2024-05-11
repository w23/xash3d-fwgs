#version 450


#include "color_spaces.glsl"


layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
	vec4 color;
} ubo;

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexture0;
layout(location=3) in vec2 aLightmapUV;
layout(location=4) in vec4 aLightColor;

layout(location=0) out vec3 vPos;
layout(location=1) out vec3 vNormal;
layout(location=2) out vec2 vTexture0;
layout(location=3) out vec2 vLightmapUV;
layout(location=4) out vec4 vColor;

void main() {
	vPos = aPos.xyz;
	// FIXME mul by normal matrix
	vNormal = aNormal;
	vTexture0 = aTexture0;
	vLightmapUV = aLightmapUV;
	vColor = ubo.color * aLightColor;
	gl_Position = ubo.mvp * vec4(aPos.xyz, 1.);
}
