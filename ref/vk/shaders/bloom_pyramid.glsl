#define GLSL
#include "ray_interop.h"
#undef GLSL
#include "bloom_config.glsl"
#include "bloom_layout.glsl"

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform readonly image2D BLOOM_INPUT;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D BLOOM_OUTPUT;
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;

// Clamp every bilinear tap to its own rectangle, never to a neighbouring mip.
vec3 bloomSample(vec2 uv, ivec2 origin, ivec2 size) {
    vec2 p = uv * vec2(size) - 0.5;
    ivec2 lo = ivec2(floor(p));
    vec2 f = fract(p);
    vec3 a = imageLoad(BLOOM_INPUT, origin + clamp(lo, ivec2(0), size - 1)).rgb;
    vec3 b = imageLoad(BLOOM_INPUT, origin + clamp(lo + ivec2(1,0), ivec2(0), size - 1)).rgb;
    vec3 c = imageLoad(BLOOM_INPUT, origin + clamp(lo + ivec2(0,1), ivec2(0), size - 1)).rgb;
    vec3 d = imageLoad(BLOOM_INPUT, origin + clamp(lo + ivec2(1,1), ivec2(0), size - 1)).rgb;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
vec3 bloomDownsample(vec2 uv, ivec2 origin, ivec2 size) {
    vec2 t = 1.0 / vec2(size);
    // Normalized 13-tap low-pass filter, including the four inner diagonal taps.
    vec3 value = bloomSample(uv, origin, size) * 0.125;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) continue;
            float weight = (x == 0 || y == 0) ? 0.0625 : 0.03125;
            value += bloomSample(uv + vec2(x, y) * 2.0 * t, origin, size) * weight;
        }
    }
    for (int y = -1; y <= 1; y += 2)
        for (int x = -1; x <= 1; x += 2)
            value += bloomSample(uv + vec2(x, y) * t, origin, size) * 0.125;
    return value;
}
vec3 bloomTent(vec2 uv, ivec2 origin, ivec2 size) {
    vec3 value = vec3(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float weight = float((x == 0 ? 2 : 1) * (y == 0 ? 2 : 1)) / 16.0;
            value += bloomSample(uv + vec2(x,y) * BLOOM_RADIUS / vec2(size), origin, size) * weight;
        }
    return value;
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 res = ubo.ubo.res;
    if (any(greaterThanEqual(pix, res))) return;
    int count = bloomLevelCount(res);
    // Copy only the still-needed earlier levels while ping-ponging the atlas.
    // There is no read/write aliasing within a dispatch.
    for (int level = 0; level < count; ++level) {
        ivec2 origin = bloomLevelOrigin(res, level);
        ivec2 size = bloomLevelSize(res, level);
        ivec2 local = pix - origin;
        if (any(lessThan(local, ivec2(0))) || any(greaterThanEqual(local, size))) continue;
        if (level > BLOOM_PASS_LEVEL) return;
        vec3 value = vec3(0.0);
        if (ubo.ubo.bloom_enabled != 0u && bloomDisplay(ubo.ubo.debug_display_only)) {
            if (level < BLOOM_PASS_LEVEL) {
                value = imageLoad(BLOOM_INPUT, pix).rgb;
            } else {
                vec2 uv = (vec2(local) + 0.5) / vec2(size);
#ifdef BLOOM_DOWNSAMPLE
#if BLOOM_PASS_LEVEL == 0
                value = bloomDownsample(uv, ivec2(0), res);
#else
                value = bloomDownsample(uv, bloomLevelOrigin(res, level - 1), bloomLevelSize(res, level - 1));
#endif
#else
                value = imageLoad(BLOOM_INPUT, pix).rgb;
                if (level + 1 < count) {
                    float coarseWeight = 0.0;
                    for (int i = level + 1; i < count; ++i) coarseWeight += bloomLevelWeight(i);
                    float totalWeight = bloomLevelWeight(level) + coarseWeight;
                    vec3 coarse = bloomTent(uv, bloomLevelOrigin(res, level + 1), bloomLevelSize(res, level + 1));
                    value = totalWeight > 0.0
                        ? (value * bloomLevelWeight(level) + coarse * coarseWeight) / totalWeight : vec3(0.0);
                }
#endif
            }
        }
        imageStore(BLOOM_OUTPUT, pix, vec4(min(value, vec3(65504.0)), 0.0));
        return;
    }
}
