#ifndef SKYBOX_GLSL_INCLUDED
#define SKYBOX_GLSL_INCLUDED

vec3 sampleSkybox(vec3 direction, float lod) {
	if ((ubo.ubo.debug_flags & DEBUG_FLAG_WHITE_FURNACE) == 0) {
		return textureLod(skybox, direction, lod).rgb * ubo.ubo.skybox_exposure;
	} else {
		return vec3(1.);
	}
}

vec3 sampleSkybox(vec3 direction) {
	return sampleSkybox(direction, 0.0);
}

#endif // #ifndef SKYBOX_GLSL_INCLUDED
