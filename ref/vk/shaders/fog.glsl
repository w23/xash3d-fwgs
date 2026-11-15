// Fixed-function GL fog equations. params = density, start, end, mode.
vec3 applyFog(vec3 color, vec4 fog_color, vec4 params, float distance) {
	float factor = 1.;
	if (params.w == 1.)
		factor = (params.z - distance) / (params.z - params.y);
	else if (params.w == 2.)
		factor = exp(-params.x * distance);
	else if (params.w == 3.) {
		const float d = params.x * distance;
		factor = exp(-d * d);
	}
	return mix(fog_color.rgb, color, clamp(factor, 0., 1.));
}
