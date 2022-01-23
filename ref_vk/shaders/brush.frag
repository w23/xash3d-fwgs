#version 450
#include "tonemapping.glsl"
#include "color_spaces.glsl"


layout (constant_id = 0) const float alpha_test_threshold = 0.;
layout (constant_id = 1) const uint max_dlights = 1;
layout (constant_id = 2) const int hdr_output = 0;
//layout (constant_id = 3) const float hdr_output_manual_adjust_down = 1.6;

layout(set=1,binding=0) uniform sampler2D sTexture0;
layout(set=2,binding=0) uniform sampler2D sLightmap;

struct Light {
	vec4 pos_r;
	vec4 color;
};

layout(set=3,binding=0) uniform UBO {
	uint num_lights;
	Light lights[max_dlights];
} ubo;

layout(location=0) in vec3 vPos;
layout(location=1) in vec3 vNormal;
layout(location=2) in vec2 vTexture0;
layout(location=3) in vec2 vLightmapUV;
layout(location=4) in vec4 vColor;
layout(location=5) flat in uint vFlags;

layout(location=0) out vec4 outColor;

// FIXME what should this be?
const float dlight_attenuation_const = 5000.;

#define FLAG_VERTEX_LIGHTING 1

void main() {
	outColor = vec4(0.);
	vec4 baseColor = vColor * texture(sTexture0, vTexture0); // base color for all

	if (hdr_output > 0) {
		// FIXME: Need an operator that understands the output luminance and middle gray stays at a reasonable level (400 vs 1000 nit display on 0.5)
		//baseColor.rgb = OECF_sRGB(aces_tonemap(baseColor.rgb));
		//baseColor.rgb = OECF_sRGB(baseColor.rgb);
		//baseColor.rgb = vec3(0.5);
		baseColor.rgb = OECF_sRGB(aces_tonemap(baseColor.rgb) / 1.5);
	}

	if (baseColor.a < alpha_test_threshold)
		discard;

	outColor.a = baseColor.a;

	if ((vFlags & FLAG_VERTEX_LIGHTING) == 0) {
		vec3 lightmap = texture(sLightmap, vLightmapUV).rgb; // lightmap for brush
		if (hdr_output > 0) {
			//lightmap = vec3(1);
			lightmap = OECF_sRGB(aces_tonemap(lightmap) * 1.5);
			//lightmap = OECF_sRGB(lightmap);
			//lightmap = lightmap;
			//lightmap = OECF_sRGB(reinhard02(lightmap,vec3(.600)));
			//lightmap = OECF_sRGB(reinhard02(lightmap,vec3(.400)) * aces_tonemap(lightmap)); // TODO: histogram equalization
			//lightmap = OECF_sRGB(TonemapMGS5(lightmap)); // TODO: histogram equalization
			//lightmap = TonemapMGS5(lightmap * 2); // TODO: histogram equalization
			//lightmap = OECF_sRGB(uncharted2Tonemap(pow(lightmap,vec3(2)) * 2)); // TODO: histogram equalization
		}
		outColor.rgb += baseColor.rgb * lightmap;
	} else {
		outColor.rgb += baseColor.rgb;
	}

	for (uint i = 0; i < ubo.num_lights; ++i) {
		const vec4 light_pos_r = ubo.lights[i].pos_r;
		const vec3 light_dir = light_pos_r.xyz - vPos;
		const vec3 light_color = ubo.lights[i].color.rgb;
		const float d2 = dot(light_dir, light_dir);
		const float r2 = light_pos_r.w * light_pos_r.w;
		const float attenuation = dlight_attenuation_const / (d2 + r2 * .5);
		outColor.rgb += baseColor.rgb * light_color * max(0., dot(normalize(light_dir), vNormal)) * attenuation;
	}

	//outColor.rgb = vNormal * .5 + .5;
}
