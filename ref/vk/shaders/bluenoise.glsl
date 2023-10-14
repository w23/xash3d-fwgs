#ifndef BLUENOISE_H_INCLUDED
#define BLUENOISE_H_INCLUDED

// Depends on uniform sampler2D textures[MAX_TEXTURES] binding being defined

// This is the same hardcoded value as in vk_textures.h
// Please keep them in sync onegai uwu
// TODO:
// - make bluenoise texture a separate binding, not part of textures[] array
// - make it a 3D texture
#define BLUE_NOISE_TEXTURE_BEGIN 7

// Also see vk_textures.h, keep in sync, etc etc
#define BLUE_NOISE_SIZE 64
vec4 blueNoise(ivec3 v) {
	v %= BLUE_NOISE_SIZE;
	return texelFetch(textures[BLUE_NOISE_TEXTURE_BEGIN+v.z], v.xy, 0);
}

#endif // ifndef BLUENOISE_H_INCLUDED
