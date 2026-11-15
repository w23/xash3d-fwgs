#ifndef ALPHA_BLENDING_GLSL
#define ALPHA_BLENDING_GLSL

// Opaque decal coverage over an ordinary surface alpha (including IOR-derived alpha).
float coveredAlpha(float surface_alpha, float coverage) {
    return coverage + (1.0f - coverage) * surface_alpha;
}

#endif
