#ifndef LC_PATH_COMMON_GLSL
#define LC_PATH_COMMON_GLSL

// Both path passes use the same alpha-test policy and ray interval.
void lcTraceRay(rayQueryEXT query, vec3 origin, vec3 direction)
{
    rayQueryInitializeEXT(query, tlas, gl_RayFlagsCullFrontFacingTrianglesEXT,
        GEOMETRY_BIT_OPAQUE | GEOMETRY_BIT_ALPHA_TEST, origin, 0.0, direction, 10000.0);

    while (rayQueryProceedEXT(query)) {
        AlphaCandidateGeometry geometry = readCandidateAlphaGeometry(query);
        uint texture_id = getKusok(geometry.kusok_index).material.tex_base_color;

        if (textureLod(textures[nonuniformEXT(texture_id)], geometry.uv, 2.0).a >= 0.1)
            rayQueryConfirmIntersectionEXT(query);
    }
}

// Scale maps a path pixel to the full-resolution primary surface.
bool lcHistoryPixel(ivec2 pixel, int scale, out ivec2 source)
{
    source = ivec2(0);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, ubo.ubo.res / scale)))
        return false;

    if (imageLoad(position_t, pixel * scale).w <= 0.0)
        return false;

    vec4 clip = ubo.ubo.prev_proj * ubo.ubo.prev_view * vec4(imageLoad(geometry_prev_position, pixel * scale).xyz, 1);
    if (any(isnan(clip)) || any(isinf(clip)) || clip.w <= 0.0)
        return false;

    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1))))
        return false;

    source = ivec2(floor(uv * vec2(ubo.ubo.res))) / scale;

    return all(lessThan(source, ubo.ubo.res / scale));
}

#if LC_PATH_MAG_DEDUPLICATION
// Salts preserve each pass's random domains: reset, replacement, neighbor selection.
bool lcDeduplicateHistory(ivec2 pixel, int scale, uvec3 salts, inout ivec2 source)
{
    const ivec2 offsets[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
    bool duplicate = false;

    for (int i = 0; i < 4; ++i) {
        ivec2 neighbor;
        if (lcHistoryPixel(pixel + offsets[i], scale, neighbor) && all(equal(neighbor, source)))
            duplicate = true;
    }

    if (!duplicate)
        return true;

    float reset = uintToFloat01(xxhash32(uvec4(uvec2(pixel), ubo.ubo.random_seed, salts.x)));
    if (reset < BOUNCE_PATH_MAG_RESET_PROBABILITY)
        return false;

    float replace = uintToFloat01(xxhash32(uvec4(uvec2(pixel), ubo.ubo.random_seed, salts.y)));
    if (replace < BOUNCE_PATH_MAG_REPLACEMENT_PROBABILITY) {
        ivec2 res = ubo.ubo.res / scale;
        int index = temporalHistoryNeighbor3x3Index(
            source.x, source.y, res.x, res.y, xxhash32(uvec4(uvec2(pixel), ubo.ubo.random_seed, salts.z)));

        source = ivec2(index % res.x, index / res.x);
    }

    return true;
}
#endif

#endif
