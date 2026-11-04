#define GLSL
#include "ray_interop.h"
#undef GLSL
#include "utils.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D SH_BLUR_INPUT_1;
layout(set = 0, binding = 2, rgba16f) uniform readonly image2D SH_BLUR_INPUT_2;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D SH_BLUR_OUTPUT_1;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D SH_BLUR_OUTPUT_2;
layout(set = 0, binding = 5, rgba16f) uniform readonly image2D normals_gs;
layout(set = 0, binding = 6, rgba32f) uniform readonly image2D position_t;

const vec2 offsets[8] = vec2[8](
	vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1),
	vec2(-0.353553, -0.353553), vec2(0.353553, 0.353553),
	vec2(-0.353553, 0.353553), vec2(0.353553, -0.353553)
);

void main() {
	const ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 res = ubo.ubo.res;
	if ((ubo.ubo.renderer_flags & RENDERER_FLAG_DENOISE_GI_BY_SH) == 0 ||
		(ubo.ubo.renderer_flags & RENDERER_FLAG_DISABLE_GI) != 0 ||
		any(greaterThanEqual(pix, res)))
		return;

	const float depth = imageLoad(position_t, pix).w;
	vec4 sum_y = imageLoad(SH_BLUR_INPUT_1, pix);
	vec2 sum_chroma = imageLoad(SH_BLUR_INPUT_2, pix).xy;
	float sum_weight = 1.0;
	if (depth > 0.0) {
		const vec3 normal = normalDecode(imageLoad(normals_gs, pix).xy);
		const vec3 view_normal = transpose(mat3(ubo.ubo.inv_view)) * normal;
		const vec2 screen_normal = view_normal.xy * sign(vec2(ubo.ubo.inv_proj[0][0], ubo.ubo.inv_proj[1][1]));
		const float squeeze = 0.65 / (1.0 + abs(view_normal.z));

		for (int i = 0; i < offsets.length(); ++i) {
			vec2 offset = SH_BLUR_ROTATION * offsets[i];
			offset -= squeeze * dot(offset, screen_normal) * screen_normal;
			const ivec2 sample_pix = pix + ivec2(round(offset * SH_BLUR_RADIUS));
			if (any(lessThan(sample_pix, ivec2(0))) || any(greaterThanEqual(sample_pix, res)))
				continue;

			const float sample_depth = imageLoad(position_t, sample_pix).w;
			if (sample_depth <= 0.0)
				continue;

			const vec3 sample_normal = normalDecode(imageLoad(normals_gs, sample_pix).xy);
			const float spatial_weight = i < 4 ? 0.52 : 0.85;
			const float depth_weight = 1.0 - smoothstep(0.0, 0.1, abs(depth - sample_depth) / depth);
			const float weight = spatial_weight * depth_weight * smoothstep(0.8, 1.0, dot(normal, sample_normal));
			sum_y += weight * imageLoad(SH_BLUR_INPUT_1, sample_pix);
			sum_chroma += weight * imageLoad(SH_BLUR_INPUT_2, sample_pix).xy;
			sum_weight += weight;
		}
	}

	imageStore(SH_BLUR_OUTPUT_1, pix, sum_y / sum_weight);
	imageStore(SH_BLUR_OUTPUT_2, pix, vec4(sum_chroma / sum_weight, 0.0, 0.0));
}
