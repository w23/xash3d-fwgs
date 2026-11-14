#ifndef LC_UPDATE_COMMON_GLSL
#define LC_UPDATE_COMMON_GLSL

#include "lc_scene.glsl"

layout(set = 0, binding = 7, std430) readonly buffer SBOLights
{
    LightsMetadata m;
} lights;
layout(set = 0, binding = 8, align = 1) readonly buffer UBOLightClusters
{
    LightCluster clusters_[MAX_LIGHT_CLUSTERS];
} light_grid;

#include "utils.glsl"
#include "noise.glsl"
#include "brdf.glsl"
const float shadow_offset_fudge = 0.01;
#include "light_common.glsl"
#include "lightcache_geometry.glsl"
#include "lc_independent.glsl"

bool lcLoadCluster(vec3 P, out uint cluster)
{
    // Same world-space grid and packed cluster list as legacy lighting.
    ivec3 cell = ivec3(floor(P / LIGHT_GRID_CELL_SIZE)) - lights.m.grid_min_cell;
    cluster = 0u;
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, lights.m.grid_size)))
        return false;

    cluster = uint(dot(cell, ivec3(1, lights.m.grid_size.x, lights.m.grid_size.x * lights.m.grid_size.y)));

    return cluster < MAX_LIGHT_CLUSTERS;
}

void updatePixel(ivec2 pix);

void main()
{
    // Keep the persistent cache untouched while frozen. Initialize to zero on reload.
    if (lightcacheUpdateDisabled() && !lightcacheIsInitFrame())
        return;

    ivec2 stride = ivec2(gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);

    for (int y = int(gl_GlobalInvocationID.y); y < LIGHTCACHE_SIDE; y += stride.y) {
        for (int x = int(gl_GlobalInvocationID.x); x < LIGHTCACHE_SIDE; x += stride.x) {
            updatePixel(ivec2(x, y));
        }
    }
}

#endif
