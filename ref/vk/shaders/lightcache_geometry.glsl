#ifndef LIGHTCACHE_GEOMETRY_GLSL
#define LIGHTCACHE_GEOMETRY_GLSL

// Direct LM addressing; no CPU probe table, lookup buffer or packed screen coordinates.
layout(set = 0, binding = 200, rgba32f) uniform readonly image2D lc_local_position;
layout(set = 0, binding = 201, rgba32f) uniform readonly image2D lc_local_normal;
layout(set = 0, binding = 202, std430) readonly buffer LightcacheBrushTransforms
{
    vec4 rows[];
    // Three row-major vec4 rows (48 bytes) per stable brush ID.
} lc_brush_transforms;

const int LIGHTCACHE_SIDE = 512;

bool lightcacheIsInitFrame()
{
    return ubo.ubo.lightcache_init_frame == ubo.ubo.frame_counter;
}

bool lightcacheUpdateDisabled()
{
    return (ubo.ubo.renderer_flags & RENDERER_FLAG_DISABLE_LIGHTCACHE_UPDATE) != 0u;
}

ivec2 lightcachePixel(uint index, ivec2 res)
{
    return ivec2(index % uint(LIGHTCACHE_SIDE), index / uint(LIGHTCACHE_SIDE));
}

uint lightcacheIndex(ivec2 pix)
{
    return uint(pix.y * LIGHTCACHE_SIDE + pix.x);
}

bool lightcacheValidPixel(ivec2 pix)
{
    return all(greaterThanEqual(pix, ivec2(0))) && all(lessThan(pix, ivec2(LIGHTCACHE_SIDE))) &&
           imageLoad(lc_local_position, pix).w > 0.0;
}

bool lightcacheInCapacity(uint index, ivec2 res)
{
    return index < uint(LIGHTCACHE_SIDE * LIGHTCACHE_SIDE) && lightcacheValidPixel(lightcachePixel(index, res));
}

uint lightcacheLookup(vec2 uv)
{
    if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1))))
        return 0u;

    // Resolve the island from the nearest occupied texel in the same 2x2
    // footprint used for interpolation. The containing texel may be empty.
    vec2 coordinate = uv * float(LIGHTCACHE_SIDE) - 0.5;
    ivec2 lo = ivec2(floor(coordinate));

    uint encoded = 0u;
    float best_distance2 = 1e30;

    for (int i = 0; i < 4; ++i) {
        ivec2 tap = lo + ivec2(i & 1, i >> 1);
        if (!lightcacheValidPixel(tap))
            continue;

        vec2 delta = vec2(tap) - coordinate;
        float distance2 = dot(delta, delta);

        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            encoded = lightcacheIndex(tap) + 1u;
        }
    }

    return encoded;
}

void lightcacheSurface(uint index, out vec3 P, out vec3 N)
{
    ivec2 pix = lightcachePixel(index, ivec2(LIGHTCACHE_SIDE));
    vec4 local_position = vec4(imageLoad(lc_local_position, pix).xyz, 1.0);
    vec4 local_normal = imageLoad(lc_local_normal, pix);
    uint brush = uint(local_normal.w);

    vec4 row0 = lc_brush_transforms.rows[brush * 3u + 0u];
    vec4 row1 = lc_brush_transforms.rows[brush * 3u + 1u];
    vec4 row2 = lc_brush_transforms.rows[brush * 3u + 2u];

    P = vec3(dot(row0, local_position), dot(row1, local_position), dot(row2, local_position));
    N = normalize(
        vec3(dot(row0.xyz, local_normal.xyz), dot(row1.xyz, local_normal.xyz), dot(row2.xyz, local_normal.xyz)));

    P += N * 0.01;
}

bool lightcacheBilinearFootprint(uint index, vec2 uv, ivec2 res, out ivec2 taps[4], out vec4 weights)
{
    ivec2 center = lightcachePixel(index, res);
    float island = imageLoad(lc_local_position, center).w;

    vec2 coordinate = uv * float(LIGHTCACHE_SIDE) - 0.5;
    ivec2 lo = ivec2(floor(coordinate));
    vec2 f = fract(coordinate);

    weights = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);

    for (int i = 0; i < 4; ++i) {
        taps[i] = lo + ivec2(i & 1, i >> 1);

        if (!lightcacheValidPixel(taps[i]) || imageLoad(lc_local_position, taps[i]).w != island) {
            taps[i] = center;
            weights[i] = 0.0;
        }
    }

    float total = dot(weights, vec4(1));
    if (total > 0.0)
        weights /= total;
    else {
        taps[0] = center;
        weights = vec4(1, 0, 0, 0);
    }

    return island > 0.0;
}

#endif
