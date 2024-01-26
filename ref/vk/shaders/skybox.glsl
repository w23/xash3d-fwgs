#ifndef SKYBOX_GLSL_INCLUDED
#define SKYBOX_GLSL_INCLUDED

vec3 sampleSkybox(vec3 direction) {
	if (ubo.ubo.debug_display_only != DEBUG_DISPLAY_WHITE_FURNACE) {
		return texture(skybox, direction).rgb * ubo.ubo.skybox_exposure;
	} else {
		return vec3(1.);
	}
}

#endif // #ifndef SKYBOX_GLSL_INCLUDED
