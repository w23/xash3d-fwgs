#ifndef LC_SCENE_GLSL
#define LC_SCENE_GLSL

layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 2) uniform UBO
{
    UniformBuffer ubo;
} ubo;
layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];

layout(set = 0, binding = 30, std430) readonly buffer ModelHeaders
{
    ModelHeader a[];
} model_headers;
layout(set = 0, binding = 31, std430) readonly buffer Kusochki
{
    Kusok a[];
} kusochki;
layout(set = 0, binding = 32, std430) readonly buffer Indices
{
    uint16_t a[];
} indices;
layout(set = 0, binding = 33, std430) readonly buffer Vertices
{
    Vertex a[];
} vertices;

#endif
