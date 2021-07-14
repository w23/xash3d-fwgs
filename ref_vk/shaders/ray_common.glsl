#extension GL_EXT_ray_tracing: require
struct RayPayload {
    vec4 hit_pos_t;
    vec3 albedo;
    vec3 normal;
    float roughness;
    int kusok_index;
};