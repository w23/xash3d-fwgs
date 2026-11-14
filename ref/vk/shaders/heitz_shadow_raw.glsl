layout(set = 0, binding = 4, rgba16f) uniform readonly image2D light_poly_diffuse;
layout(set = 0, binding = 5, rgba16f) uniform readonly image2D light_point_diffuse;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D light_poly_specular;
layout(set = 0, binding = 7, rgba16f) uniform readonly image2D light_point_specular;
layout(set = 0, binding = 8, rgba16f) uniform readonly image2D heitz_poly_unshadowed_diffuse;
layout(set = 0, binding = 9, rgba16f) uniform readonly image2D heitz_point_unshadowed_diffuse;
layout(set = 0, binding = 10, rgba16f) uniform readonly image2D heitz_poly_unshadowed_specular;
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D heitz_point_unshadowed_specular;

void heitzRaw(ivec2 p, out vec3 sd, out vec3 ud, out vec3 ss, out vec3 us) {
	sd = imageLoad(light_poly_diffuse, p).rgb + imageLoad(light_point_diffuse, p).rgb;
	ss = imageLoad(light_poly_specular, p).rgb + imageLoad(light_point_specular, p).rgb;
	ud = imageLoad(heitz_poly_unshadowed_diffuse, p).rgb + imageLoad(heitz_point_unshadowed_diffuse, p).rgb;
	us = imageLoad(heitz_poly_unshadowed_specular, p).rgb + imageLoad(heitz_point_unshadowed_specular, p).rgb;
}
