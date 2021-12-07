#version 450
#include "tonemapping.glsl"

layout(set=0,binding=0) uniform sampler2D tex;

layout(location=0) in vec2 vUv;
layout(location=1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout (constant_id = 0) const int hdr_output = 0;
layout (constant_id = 1) const float hdr_output_manual_adjust_ui_down = 1.8;

void main() {
	vec4 img = texture(tex, vUv) * vColor;
	if (hdr_output > 0) {
		// FIXME: Need to composite properly into the color space of the HDR scene, using scRGB backbuffer provides simple solution
		img.rgb = aces_tonemap(img.rgb) / hdr_output_manual_adjust_ui_down;
	}
	outColor = img;
}
