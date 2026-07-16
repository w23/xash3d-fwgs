#version 450

layout (constant_id = 0) const float alpha_test_threshold = 0.;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

layout(location=0) in vec2 vTexture0;
layout(location=1) in vec2 vLightmapUV;
layout(location=2) in vec4 vColor;

layout(location=0) out vec4 outColor;

void main() {
	// TODO make sure textures are premultiplied alpha
	const vec4 baseColor = vColor * texture(sTexture0, vTexture0);

	if (baseColor.a < alpha_test_threshold)
		discard;

	// Match GL's default VBO overbright path (ref/gl/gl_rsurf.c:R_SetLightmap):
	// the CPU lightmap is encoded with lightscale 171, then modulated at 2x.
	const vec3 lightmap = texture(sLightmap, vLightmapUV).rgb * 2.0;
	outColor = vec4(lightmap * baseColor.rgb, baseColor.a);
}
