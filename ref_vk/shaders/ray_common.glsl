#extension GL_EXT_ray_tracing: require
struct RayPayload {
    float t_offset, pixel_cone_spread_angle;
    vec4 hit_pos_t;
    vec3 normal;
    vec3 base_color;
		float reflection;
		vec3 emissive;
    float roughness;
    int kusok_index;
};
