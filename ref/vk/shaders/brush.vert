#version 450

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
	vec4 color;
	uint ignore_lightmap;
	vec3 padding_unused_;
} ubo;

layout(location=0) in vec3 aPos;
//layout(location=1) in vec3 aNormal; // TODO: remove normals uploading for vanilla vk renderer?
layout(location=2) in vec2 aTexture0;
layout(location=3) in vec2 aLightmapUV;
layout(location=4) in vec4 aLightColor;

layout(location=0) out vec2 vTexture0;
layout(location=1) out vec2 vLightmapUV;
layout(location=2) out vec4 vColor;
layout(location=3) flat out uint vIgnoreLightmap;

void main() {
	vTexture0 = aTexture0;
	vLightmapUV = aLightmapUV;
	vColor = ubo.color * aLightColor;
	vIgnoreLightmap = ubo.ignore_lightmap;
	gl_Position = ubo.mvp * vec4(aPos.xyz, 1.);
}
