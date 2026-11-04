#define GLSL
#include "ray_interop.h"
#undef GLSL

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D FIREFLY_INPUT;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D FIREFLY_OUTPUT;

float fireflyLuminance(vec3 color) {
	return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

void main() {
	const ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	const ivec2 res = ubo.ubo.res / 2;
	if (any(greaterThanEqual(pix, res)))
		return;

	vec4 signal = imageLoad(FIREFLY_INPUT, pix);
	if ((ubo.ubo.renderer_flags & (RENDERER_FLAG_DISABLE_FIREFLY_REJECTION | RENDERER_FLAG_DISABLE_GI)) == 0) {
		float sum = 0.0;
		float sum_squared = 0.0;
		int count = 0;
		for (int y = -4; y <= 4; ++y) {
			for (int x = -4; x <= 4; ++x) {
				if (abs(x) <= 1 && abs(y) <= 1)
					continue;

				const ivec2 sample_pix = pix + ivec2(x, y);
				if (any(lessThan(sample_pix, ivec2(0))) || any(greaterThanEqual(sample_pix, res)))
					continue;

				const float value = fireflyLuminance(imageLoad(FIREFLY_INPUT, sample_pix).rgb);
				if (isnan(value) || isinf(value))
					continue;

				sum += value;
				sum_squared += value * value;
				++count;
			}
		}

		if (any(isnan(signal.rgb)) || any(isinf(signal.rgb))) {
			signal.rgb = vec3(0.0);
		} else if (count > 0) {
			const float mean = sum / float(count);
			const float sigma = sqrt(max(sum_squared / float(count) - mean * mean, 0.0));
			const float limit = mean + 2.0 * sigma;
			const float center = fireflyLuminance(signal.rgb);
			if (center > limit)
				signal.rgb *= limit / center;
		}
	}
	imageStore(FIREFLY_OUTPUT, pix, signal);
}
