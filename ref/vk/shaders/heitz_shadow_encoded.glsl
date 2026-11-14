layout(set = 0, binding = 4, rgba16f) uniform readonly image2D heitz_encoded_sd;
layout(set = 0, binding = 5, rgba16f) uniform readonly image2D heitz_encoded_ud;
layout(set = 0, binding = 6, rgba16f) uniform readonly image2D heitz_encoded_ss;
layout(set = 0, binding = 7, rgba16f) uniform readonly image2D heitz_encoded_us;

void heitzEncoded(ivec2 p, out vec3 sd, out vec3 ud, out vec3 ss, out vec3 us) {
	sd = imageLoad(heitz_encoded_sd, p).rgb;
	ud = imageLoad(heitz_encoded_ud, p).rgb;
	ss = imageLoad(heitz_encoded_ss, p).rgb;
	us = imageLoad(heitz_encoded_us, p).rgb;
}
