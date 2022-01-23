#version 450
#include "tonemapping.glsl"
#include "color_spaces.glsl"

layout (constant_id = 2) const int hdr_output = 0;
layout (constant_id = 3) const float hdr_output_manual_adjust_down = 1.6;

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
	vec4 color;
} ubo;

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexture0;
layout(location=3) in vec2 aLightmapUV;
layout(location=4) in vec4 aLightColor;
layout(location=5) in uint aFlags;

layout(location=0) out vec3 vPos;
layout(location=1) out vec3 vNormal;
layout(location=2) out vec2 vTexture0;
layout(location=3) out vec2 vLightmapUV;
layout(location=4) out vec4 vColor;
layout(location=5) flat out uint vFlags;

#define FLAG_VERTEX_LIGHTING 1

void main() {
	vPos = aPos.xyz;
	vNormal = aNormal;
	vTexture0 = aTexture0;
	vLightmapUV = aLightmapUV;
	vColor = ubo.color;
	vec4 lightmap = aLightColor;
	if (hdr_output > 0) {
		// FIXME: Avoid tone mapping "fix-ups", ideally done in scene-referred space
		vColor.rgb = OECF_sRGB(aces_tonemap(vColor.rgb)) / hdr_output_manual_adjust_down;
		lightmap.rgb /= hdr_output_manual_adjust_down;
	}

	if ((aFlags & FLAG_VERTEX_LIGHTING) != 0) {
		vColor *= lightmap;
	}

	vFlags = aFlags;
	gl_Position = ubo.mvp * vec4(aPos.xyz, 1.);
}
