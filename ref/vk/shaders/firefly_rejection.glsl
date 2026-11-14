#define GLSL
#include "ray_interop.h"

layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set=0,binding=1,rgba16f) uniform readonly image2D FIREFLY_INPUT;
layout(set=0,binding=2,rgba16f) uniform writeonly image2D FIREFLY_OUTPUT;
#if FIREFLY_REMOVED_MAP
layout(set=0,binding=6,rgba16f) uniform writeonly image2D out_bounce_fireflies;
#endif

#if FIREFLY_SPLIT_REFLECTION
layout(set=0,binding=3,rgba32f) uniform readonly image2D reflection_reuse_radiance;
layout(set=0,binding=4,rgba32f) uniform writeonly image2D out_reflection_reuse_base;
#if REFLECTION_REUSE_FIREFLIES
layout(set=0,binding=5,rgba32f) uniform writeonly image2D out_reflection_fireflies;
#endif
#endif

float luminance(vec3 color) {
    return dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

void storeSplit(ivec2 p, vec4 signal, float scale) {
    imageStore(FIREFLY_OUTPUT, p, vec4(signal.rgb * scale, signal.a));
#if FIREFLY_REMOVED_MAP
    imageStore(out_bounce_fireflies, p, vec4(signal.rgb * (1.0 - scale), signal.a));
#endif
#if FIREFLY_SPLIT_REFLECTION
    vec4 source = imageLoad(reflection_reuse_radiance, p);
    imageStore(out_reflection_reuse_base, p, vec4(source.rgb * scale, source.a));
#if REFLECTION_REUSE_FIREFLIES
    imageStore(out_reflection_fireflies, p, vec4(source.rgb * (1.0 - scale), source.a));
#endif
#endif
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ubo.ubo.res / FIREFLY_PIXEL_SCALE;
    if (any(greaterThanEqual(p, size))) return;

    vec4 signal = imageLoad(FIREFLY_INPUT, p);
    if ((ubo.ubo.renderer_flags & RENDERER_FLAG_FIREFLY_REJECTION) == 0u) {
        storeSplit(p, signal, 1.0);
        return;
    }

    float sum = 0.0;
    float sumSquared = 0.0;
    int count = 0;
    for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
            if (abs(x) <= 1 && abs(y) <= 1) continue;
            ivec2 q = p + ivec2(x, y);
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) continue;

            float value = luminance(imageLoad(FIREFLY_INPUT, q).rgb);
            sum += value;
            sumSquared += value * value;
            ++count;
        }
    }

    float scale = 1.0;
    if (count > 0) {
        float mean = sum / float(count);
        float sigma = sqrt(max(sumSquared / float(count) - mean * mean, 0.0));
        float limit = mean + 2.0 * sigma;
        float center = luminance(signal.rgb);
        if (center > limit) scale = limit / center;
    }
    storeSplit(p, signal, scale);
}
