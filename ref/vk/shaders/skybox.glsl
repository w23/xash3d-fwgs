#ifndef SKYBOX_GLSL_INCLUDED
#define SKYBOX_GLSL_INCLUDED

vec3 sampleSkybox(vec3 direction) {
	if ((ubo.ubo.debug_flags & DEBUG_FLAG_WHITE_FURNACE) == 0) {
		return texture(skybox, direction).rgb * ubo.ubo.skybox_exposure;
	} else {
		return vec3(1.);
	}
}

#endif // #ifndef SKYBOX_GLSL_INCLUDED
