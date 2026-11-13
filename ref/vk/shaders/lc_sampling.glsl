#ifndef LC_SAMPLING_GLSL
#define LC_SAMPLING_GLSL

#include "lightcache_geometry.glsl"

layout(set = 0, binding = 14, rgba16f) uniform readonly image2D lc_radiance;

// Unit-albedo lighting; callers apply the hit material after interpolation.
vec3 lcCachedIrradiance(vec4 uv)
{
    uint encoded = uv.z > 0.0 ? lightcacheLookup(uv.xy) : 0u;
    if (encoded == 0u || !lightcacheInCapacity(encoded - 1u, ubo.ubo.res))
        return vec3(0);

    ivec2 taps[4];
    vec4 weights;
    if (!lightcacheBilinearFootprint(encoded - 1u, uv.xy, ubo.ubo.res, taps, weights))
        return vec3(0);

    vec3 color = vec3(0);
    for (int i = 0; i < 4; ++i) {
        color += imageLoad(lc_radiance, taps[i]).rgb * weights[i];
    }

    return max(color, vec3(0));
}

#endif
