#version 450

layout (constant_id = 0) const float alpha_test_threshold = 0.;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

layout(location=0) in vec2 vTexture0;
layout(location=1) in vec2 vLightmapUV;
layout(location=2) in vec4 vColor;
layout(location=3) flat in uint vIgnoreLightmap;

layout(location=0) out vec4 outColor;

void main() {
	outColor = vec4(0.);
	const vec4 tex_color = texture(sTexture0, vTexture0);

	// TODO make sure textures are premultiplied alpha
	const vec4 baseColor = vColor * tex_color;

	if (baseColor.a < alpha_test_threshold)
		discard;

	outColor.a = baseColor.a;

	if (vIgnoreLightmap == 0) {
		outColor.rgb = texture(sLightmap, vLightmapUV).rgb * baseColor.rgb;
	} else {
		outColor.rgb = baseColor.rgb;
	}
}
