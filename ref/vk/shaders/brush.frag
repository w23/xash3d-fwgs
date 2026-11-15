#version 450
#extension GL_GOOGLE_include_directive : require

#include "fog.glsl"

layout(set=0,binding=0) uniform UBO {
	mat4 mvp;
	vec4 color;
	vec4 fog_color;
	vec4 fog_params;
	vec4 fog_plane;
} ubo;

layout (constant_id = 0) const float alpha_test_threshold = 0.;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

layout(location=0) in vec2 vTexture0;
layout(location=1) in vec2 vLightmapUV;
layout(location=2) in vec4 vColor;
layout(location=3) in float vFogCoord;

layout(location=0) out vec4 outColor;

void main() {
	// TODO make sure textures are premultiplied alpha
	const vec4 baseColor = vColor * texture(sTexture0, vTexture0);

	if (baseColor.a < alpha_test_threshold)
		discard;

	const vec3 lightmap = texture(sLightmap, vLightmapUV).rgb;
	outColor = vec4(applyFog(lightmap * baseColor.rgb, ubo.fog_color, ubo.fog_params, abs(vFogCoord)), baseColor.a);
}
