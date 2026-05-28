#version 450

layout (constant_id = 0) const float alpha_test_threshold = 0.;
layout (constant_id = 1) const uint max_dlights = 1;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

struct Light {
	vec4 pos_r;
	vec4 color;
};

layout(set=3,binding=0) uniform UBO {
	uint num_lights;
	uint debug_r_lightmap;
	uvec2 padding_unused_;
	Light lights[max_dlights];
} ubo;

layout(location=0) in vec3 vPos;
layout(location=1) in vec3 vNormal;
layout(location=2) in vec2 vTexture0;
layout(location=3) in vec2 vLightmapUV;
layout(location=4) in vec4 vColor;
layout(location=5) flat in float vIgnoreLightmapAndLights;

layout(location=0) out vec4 outColor;

// Exact legacy behavior:
// lightmap packing converts r_blocklights with >> 7 (divide by 128).
// Keep this as a compile-time constant to match classic dlight-in-lightmap exactly.
const float kLlightmapBlockToTexScale = 128.0;

void main() {
	outColor = vec4(0.);
	const vec4 tex_color = texture(sTexture0, vTexture0);

	// TODO make sure textures are premultiplied alpha
	const vec4 baseColor = vColor * tex_color;

	if (baseColor.a < alpha_test_threshold)
		discard;

	outColor.a = baseColor.a;

	if (uint(vIgnoreLightmapAndLights) == 0) {
		outColor.rgb = texture(sLightmap, vLightmapUV).rgb;

		// Exact dlight emulation for BSP brush geometry, equivalent to adding them into the lightmap.
		for (uint i = 0; i < ubo.num_lights; ++i) {
			const vec4 light_pos_r = ubo.lights[i].pos_r;
			const vec3 light_color = ubo.lights[i].color.rgb;
			const float minlight = ubo.lights[i].color.a;

			const float dist = length(light_pos_r.xyz - vPos);
			const float add = light_pos_r.w - dist;
			if (add <= minlight)
				continue;

			outColor.rgb += light_color * (add / kLlightmapBlockToTexScale);
		}

		outColor.rgb = clamp(outColor.rgb, vec3(0.0), vec3(1.0));

		if (ubo.debug_r_lightmap == 0)
			outColor.rgb *= baseColor.rgb;
	} else {
		outColor.rgb = baseColor.rgb;
	}
}
