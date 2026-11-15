// The including pass provides the input/output names, radius and rotation.
// Input, output and normals_gs use the full-resolution pixel grid.
#ifndef ROTATED_BILATERAL_BLUR_ENABLED
#define ROTATED_BILATERAL_BLUR_ENABLED true
#endif
#ifndef ROTATED_BILATERAL_BLUR_BYPASS
#define ROTATED_BILATERAL_BLUR_BYPASS 0
#endif

#define GLSL
#include "ray_interop.h"
#undef GLSL
#include "utils.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D ROTATED_BILATERAL_BLUR_INPUT;
layout(set = 0, binding = 2, rgba16f) uniform readonly image2D normals_gs;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D ROTATED_BILATERAL_BLUR_OUTPUT;
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
layout(set = 0, binding = 4, rgba16f) uniform readonly image2D ROTATED_BILATERAL_BLUR_SECOND_INPUT;
layout(set = 0, binding = 5, rgba16f) uniform writeonly image2D ROTATED_BILATERAL_BLUR_SECOND_OUTPUT;
#endif

const vec2 offsets[8] = vec2[8](
	vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1),
	vec2(-0.353553, -0.353553), vec2(0.353553, 0.353553),
	vec2(-0.353553, 0.353553), vec2(0.353553, -0.353553)
);

void main() {
	const ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 res = ubo.ubo.res;
	if (!(ROTATED_BILATERAL_BLUR_ENABLED) ||
		any(greaterThanEqual(pix, res)))
		return;

	const vec4 center = imageLoad(ROTATED_BILATERAL_BLUR_INPUT, pix);
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
	const vec4 second_center = imageLoad(ROTATED_BILATERAL_BLUR_SECOND_INPUT, pix);
#endif
#if ROTATED_BILATERAL_BLUR_BYPASS
	imageStore(ROTATED_BILATERAL_BLUR_OUTPUT, pix, center);
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
	imageStore(ROTATED_BILATERAL_BLUR_SECOND_OUTPUT, pix, second_center);
#endif
#else
	const vec3 normal = normalDecode(imageLoad(normals_gs, pix).xy);
	const vec3 view_normal = mat3(ubo.ubo.view) * normal;
	// Foreshorten along the projected normal, keeping at least 35% of the radius.
	const vec2 screen_normal = view_normal.xy * sign(vec2(ubo.ubo.proj[0][0], ubo.ubo.proj[1][1]));
	const float squeeze = 0.65 / (1.0 + abs(view_normal.z));

	vec4 sum = center;
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
	vec4 second_sum = second_center;
#endif
	float sum_weight = 1.0;
	for (int i = 0; i < 8; ++i) {
		vec2 offset = ROTATED_BILATERAL_BLUR_ROTATION * offsets[i];
		offset -= squeeze * dot(offset, screen_normal) * screen_normal;
		const ivec2 sample_pix = pix + ivec2(round(offset * ROTATED_BILATERAL_BLUR_RADIUS));
		if (any(lessThan(sample_pix, ivec2(0))) || any(greaterThanEqual(sample_pix, res)))
			continue;

		const vec3 sample_normal = normalDecode(imageLoad(normals_gs, sample_pix).xy);
		const float spatial_weight = i < 4 ? 0.52 : 0.85;
		const float weight = spatial_weight * smoothstep(0.8, 1.0, dot(normal, sample_normal));
		sum += weight * imageLoad(ROTATED_BILATERAL_BLUR_INPUT, sample_pix);
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
		second_sum += weight * imageLoad(ROTATED_BILATERAL_BLUR_SECOND_INPUT, sample_pix);
#endif
		sum_weight += weight;
	}

	vec4 filtered = sum / sum_weight;
#ifdef ROTATED_BILATERAL_BLUR_SECOND_INPUT
	imageStore(ROTATED_BILATERAL_BLUR_SECOND_OUTPUT, pix, second_sum / sum_weight);
#else
	filtered.a = center.a;
#endif
	imageStore(ROTATED_BILATERAL_BLUR_OUTPUT, pix, filtered);
#endif
}
