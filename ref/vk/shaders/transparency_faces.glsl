#ifndef TRANSPARENCY_FACES_GLSL
#define TRANSPARENCY_FACES_GLSL

// Brush triangles use the engine's clockwise winding: the raster-visible side
// is the Vulkan back-facing side (primary rays use CullFrontFacingTriangles).
// Liquids retain vanilla two-sided visibility; physical interfaces also expose exits.
bool transparencyFaceVisible(float refraction_ior, bool liquid, bool translucent, bool front_facing) {
    return refraction_ior > 0.0f || liquid || !translucent || !front_facing;
}

#endif
