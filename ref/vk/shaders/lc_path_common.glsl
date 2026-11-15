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

        // Sky uses a sentinel material ID, not an index into textures.
        if (texture_id == TEX_BASE_SKYBOX ||
            textureLod(textures[nonuniformEXT(texture_id)], geometry.uv, 2.0).a >= 0.1)
            rayQueryConfirmIntersectionEXT(query);
    }
}

// Validate the actual donor, including any permutation/parallax displacement.
// A failed optional displacement must leave the ordinary history available.
bool lcHistoryDonorValid(ivec2 pixel, int scale, ivec2 source)
{
    if (any(lessThan(source, ivec2(0))) || any(greaterThanEqual(source, ubo.ubo.res / scale)))
        return false;
    vec4 donor = imageLoad(prev_position_t, source * scale);
    vec3 donor_normal = normalDecode(imageLoad(prev_normals_gs, source * scale).xy);
    vec3 current_normal = normalDecode(imageLoad(normals_gs, pixel * scale).xy);
    vec3 previous_position = imageLoad(geometry_prev_position, pixel * scale).xyz;
    float error = abs(dot(previous_position - donor.xyz, donor_normal));
    return donor.w > 0.0 && !any(isnan(donor)) && !any(isinf(donor)) &&
        !isnan(error) && !isinf(error) && error <= BOUNCE_PATH_REPROJECTION_PLANE_TOLERANCE &&
        dot(current_normal, donor_normal) >= BOUNCE_PATH_REPROJECTION_NORMAL_MIN_DOT;
}

// Version the seed + reservoir estimator together on shader reload.
const float LC_PATH_HISTORY_VERSION = 3.0;

bool lcPathSeedMetadataValid(vec4 metadata)
{
    return !any(isnan(metadata)) && !any(isinf(metadata)) &&
        metadata.y == LC_PATH_HISTORY_VERSION &&
        metadata.w == float(((ubo.ubo.frame_counter - 1u) & 65535u) + 1u) &&
        metadata.x >= 0.0 && metadata.x <= 8388607.0;
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

    return all(lessThan(source, ubo.ubo.res / scale)) && lcHistoryDonorValid(pixel, scale, source);
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
