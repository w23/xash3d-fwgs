#ifndef RAY_LIGHTMAP_GLSL
#define RAY_LIGHTMAP_GLSL
// Reuse the committed intersection; obtaining UV requires no additional ray.
vec4 rayLightmapUV(rayQueryEXT rq) {
    ModelHeader model = getModelHeader(rayQueryGetIntersectionInstanceIdEXT(rq, true));
    if (model.lightmap_uv_valid != 0u) return vec4(model.lightmap_uv, 1, 0);
    int ko = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true) + rayQueryGetIntersectionGeometryIndexEXT(rq, true);
    Kusok k = getKusok(ko);
    if (k.material.tex_base_color == TEX_BASE_SKYBOX) return vec4(0);
    uint base = k.index_offset + uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true)) * 3u;
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    vec2 a = GET_VERTEX(uint(getIndex(base)) + k.vertex_offset).lm_tc;
    vec2 b = GET_VERTEX(uint(getIndex(base + 1u)) + k.vertex_offset).lm_tc;
    vec2 c = GET_VERTEX(uint(getIndex(base + 2u)) + k.vertex_offset).lm_tc;
    // Keep the exact fractional hit coordinate; only cache lookup selects texels.
    vec2 uv = a * (1.0 - bary.x - bary.y) + b * bary.x + c * bary.y;
    return vec4(uv, any(notEqual(uv, vec2(0))) ? 1.0 : 0.0, 0);
}
#endif
