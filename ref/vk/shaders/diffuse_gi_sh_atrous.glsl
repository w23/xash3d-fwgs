
#ifndef KERNEL_X
#define KERNEL_X 2
#endif

#ifndef KERNEL_Y
#define KERNEL_Y 3
#endif


#ifndef OFFSET
#define OFFSET ivec(1, 1)
#endif

#ifndef DEPTH_THRESHOLD
#define DEPTH_THRESHOLD 0.1
#endif

#define GI_BLUR_NORMALS_THRESHOLD_LOW 0.5
#define GI_BLUR_NORMALS_THRESHOLD_MAX 0.9

#include "noise.glsl"
#include "brdf.h"
#include "utils.glsl"
#include "denoiser_config.glsl"
#include "denoiser_utils.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D OUTPUT_GI_1;
layout(set = 0, binding = 1, rgba16f) uniform image2D OUTPUT_GI_2;

layout(set = 0, binding = 2, rgba16f) uniform readonly image2D INPUT_GI_1;
layout(set = 0, binding = 3, rgba16f) uniform readonly image2D INPUT_GI_2;
layout(set = 0, binding = 4, rgba8)   uniform readonly image2D material_rmxx;
layout(set = 0, binding = 5, rgba32f) uniform readonly image2D position_t;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D normals_gs;

#define GLSL
#include "ray_interop.h"
#undef GLSL

layout(set = 0, binding = 7) uniform UBO { UniformBuffer ubo; } ubo;

void main() {
	ivec2 res = ivec2(imageSize(INPUT_GI_1));
	ivec2 pix = ivec2(gl_GlobalInvocationID);

	if ((ubo.ubo.renderer_flags & RENDERER_FLAG_DENOISE_GI_BY_SH) == 0) {
		return;
	}

	const vec4 gi_sh2_src = FIX_NAN(imageLoad(INPUT_GI_2, pix));
	const float depth = FIX_NAN(imageLoad(position_t, pix)).w;
	const float metalness_factor = /*FIX_NAN(imageLoad(material_rmxx, pix)).y > .5 ? 1. : 0.*/ 1.;
	const vec3 normal = normalDecode(FIX_NAN(imageLoad(normals_gs, pix)).xy);

	vec4 gi_sh1 = vec4(0.);
	vec2 gi_sh2 = vec2(0.);

	float weight_sum = 0.;
	for (int x = -KERNEL_X; x <= KERNEL_X; ++x) {
		for (int y = -KERNEL_Y; y <= KERNEL_Y; ++y) {
			const ivec2 p = (pix + ivec2(x, y) * OFFSET);
			if (any(greaterThanEqual(p, res)) || any(lessThan(p, ivec2(0)))) {
				continue;
			}

			// metal surfaces have gi after 2 bounce, diffuse after 1, don't mix them
//			const float current_metalness = FIX_NAN(imageLoad(material_rmxx, p)).y;
//			if (abs(metalness_factor - current_metalness) > .5)
//				continue;

			const vec4 current_gi_sh1 = FIX_NAN(imageLoad(INPUT_GI_1, p));
			const vec4 current_gi_sh2 = FIX_NAN(imageLoad(INPUT_GI_2, p));
			const vec3 current_normal = normalDecode(FIX_NAN(imageLoad(normals_gs, p)).xy);

			const float depth_current = FIX_NAN(imageLoad(position_t, p)).w;
			const float depth_offset = abs(depth - depth_current) / max(0.001, depth);
			const float gi_depth_factor = 1. - smoothstep(0., DEPTH_THRESHOLD, depth_offset);
			const float normals_factor = smoothstep(GI_BLUR_NORMALS_THRESHOLD_LOW, GI_BLUR_NORMALS_THRESHOLD_MAX, dot(normal, current_normal));

			float weight = gi_depth_factor * normals_factor; // square blur for more efficient light spreading

//		#ifdef SPREAD_UPSCALED
//			weight *= (GI_DOWNSAMPLE * GI_DOWNSAMPLE);
//		#endif

//			const float sigma = KERNEL_X / 2.;
//			const float weight = normpdf(x, sigma) * normpdf(y, sigma) * gi_depth_factor * normals_factor;

			gi_sh1 += current_gi_sh1 * weight;
			gi_sh2 += current_gi_sh2.xy * weight;
			weight_sum += weight;
		}
	}

	if (weight_sum > 0.) {
		gi_sh1 /= weight_sum;
		gi_sh2 /= weight_sum;
	}

	imageStore(OUTPUT_GI_1, pix, gi_sh1);
	imageStore(OUTPUT_GI_2, pix, vec4(gi_sh2, gi_sh2_src.zw));
}
