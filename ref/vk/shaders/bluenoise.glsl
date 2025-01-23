#ifndef BLUENOISE_H_INCLUDED
#define BLUENOISE_H_INCLUDED

// Depends on uniform sampler3D blue_noise_texture binding being defined

// Also see vk_textures.h, keep in sync, etc etc
#define BLUE_NOISE_SIZE 64

vec4 blueNoise(ivec3 v) {
	ivec3 size = textureSize(blue_noise_texture, 0);
	return texelFetch(blue_noise_texture, v % size, 0);
}

#endif // ifndef BLUENOISE_H_INCLUDED
