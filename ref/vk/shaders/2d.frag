#version 450

//#define SRGB_FAST_APPROXIMATION
#include "color_spaces.glsl"
#include "tonemapping.glsl"

layout(set=0,binding=0) uniform sampler2D tex;

layout(location=0) in vec2 vUv;
layout(location=1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout (constant_id = 0) const int vk_display_dr_mode = 0; // TODO: ubo


void main() {
	outColor = texture(tex, vUv) * vColor;

	switch (vk_display_dr_mode) {
		// LDR: VK_FORMAT_B8G8R8A8_UNORM(44) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
		//case LDR_B8G8R8A8_UNORM_SRGB_NONLINEAR:
			//break;

		// LDR: VK_FORMAT_R8G8B8A8_SRGB(43) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
		// LDR: VK_FORMAT_R8G8B8A8_UNORM(37) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
		// LDR: VK_FORMAT_B8G8R8A8_SRGB(50) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
		// LDR: VK_FORMAT_A2B10G10R10_UNORM_PACK32(64) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)

		// HDR: VK_FORMAT_R16G16B16A16_SFLOAT(97) VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT(1000104002)
		case HDR_R16G16B16A16_SFLOAT_EXTENDED_SRGB_LINEAR:
			outColor.rgb = SRGBtoLINEAR(outColor.rgb); // standard menu
			break;

		// HDR: VK_FORMAT_A2B10G10R10_UNORM_PACK32(64) VK_COLOR_SPACE_HDR10_ST2084_EXT(1000104008)
		case HDR_A2B10G10R10_UNORM_PACK32_HDR10_ST2084: // TODO: need special way for this format.
			outColor.rgb = linearToPQ(Tonemap_Lottes(SRGBtoLINEAR(outColor.rgb))); // hot beautiful menu, brighter than necessary HUD
			break;
	}
}
