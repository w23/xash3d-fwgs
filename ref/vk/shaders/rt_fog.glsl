#ifndef RT_FOG_GLSL_INCLUDED
#define RT_FOG_GLSL_INCLUDED
#include "fog.glsl"
#include "color_spaces.glsl"

float rtFogDepth(vec3 position) {
    return abs((ubo.ubo.view * vec4(position, 1.0)).z);
}

float rtSkyFogDistance(vec3 direction) {
    vec3 cube = abs(direction);
    vec3 forward = -ubo.ubo.inv_view[2].xyz;
    return abs(dot(direction, forward)) * ubo.ubo.fog_sky_half_size /
        max(max(cube.x, cube.y), cube.z);
}

// Medium on the continuing transmission path. Distance is accumulated so that
// splitting a path at transparent objects preserves LINEAR and EXP2 as well as EXP.
struct RayFog {
    vec4 color;
    vec4 params;
    float distance;
    bool liquid;
};

RayFog rtInitialFog(vec3 first_position) {
    return RayFog(vec4(SRGBtoLINEAR(ubo.ubo.fog_color.rgb), 1.0),
        ubo.ubo.fog_params, rtFogDepth(first_position), ubo.ubo.fog_liquid != 0u);
}

void rtCrossLiquid(inout RayFog fog, Material material, vec3 direction, vec4 normals) {
    if (ubo.ubo.fog_enabled == 0u || (material.surface_flags & SURFACE_FLAG_LIQUID) == 0u)
        return;
    fog.liquid = dot(direction, normalDecode(normals.xy)) < 0.0;
    fog.distance = 0.0;
    if (fog.liquid) {
        fog.color = vec4(SRGBtoLINEAR(material.liquid_fog.rgb), 1.0);
        fog.params = vec4(material.liquid_fog.a, 0.0, 0.0, 2.0);
    } else {
        fog.color = vec4(SRGBtoLINEAR(ubo.ubo.fog_color.rgb), 1.0);
        fog.params = ubo.ubo.fog_liquid != 0u ? vec4(0.0) : ubo.ubo.fog_params;
    }
}

void rtFogSegment(inout RayFog fog, float distance, bool sky,
    inout vec3 radiance, inout float weight) {
    vec4 params = fog.params;
    if (sky && !fog.liquid)
        params = ubo.ubo.sky_fog_params;
    // Once camera water is exited, its sky settings must not fog the air.
    if (sky && !fog.liquid && ubo.ubo.fog_liquid != 0u)
        params.w = 0.0;
    float before = fogTransmittance(params, fog.distance);
    float after = fogTransmittance(params, fog.distance + max(distance, 0.0));
    float transmission = before > 0.0 ? clamp(after / before, 0.0, 1.0) : 0.0;
    radiance += weight * (1.0 - transmission) * fog.color.rgb;
    weight *= transmission;
    fog.distance += max(distance, 0.0);
}
#endif
