#ifndef LC_SAMPLING_GLSL
#define LC_SAMPLING_GLSL

#include "lightcache_geometry.glsl"
#include "spherical_harmonics.glsl"

layout(set = 0, binding = 14, rgba16f) uniform readonly image2D lc_sh1;
layout(set = 0, binding = 15, rgba16f) uniform readonly image2D lc_sh2;

// Unit-albedo lighting; callers apply the hit material after interpolation.
vec3 lcCachedIrradiance(vec4 uv, vec3 shading_normal)
{
    uint encoded = uv.z > 0.0 ? lightcacheLookup(uv.xy) : 0u;
    if (encoded == 0u || !lightcacheInCapacity(encoded - 1u, ubo.ubo.res))
        return vec3(0);

    ivec2 taps[4];
    vec4 weights;
    if (!lightcacheBilinearFootprint(encoded - 1u, uv.xy, ubo.ubo.res, taps, weights))
        return vec3(0);

    SH lighting = SH(vec4(0), vec2(0));
    for (int i = 0; i < 4; ++i) {
        accumulate_SH(lighting, SH(imageLoad(lc_sh1, taps[i]), imageLoad(lc_sh2, taps[i]).xy), weights[i]);
    }

    // The bounce projector includes the uniform-hemisphere 2*pi / pi factor.
    // Direct incident radiance needs the Lambertian SH convolution / pi instead.
    return project_SH_irradiance(lighting, shading_normal) / (2.0 * 3.14159265);
}

#endif
