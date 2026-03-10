#ifndef TEMPORAL_REPROJECTION_GLSL_INCLUDED
#define TEMPORAL_REPROJECTION_GLSL_INCLUDED

bool projectWorldToPrevFramePixel(vec3 world_position, ivec2 res, out ivec2 reproj_pix, out float clip_w) {
	const vec4 clip_space = inverse(ubo.ubo.prev_inv_proj) * vec4((inverse(ubo.ubo.prev_inv_view) * vec4(world_position, 1.0)).xyz, 1.0);
	clip_w = clip_space.w;
	if (clip_w <= 0.0) {
		reproj_pix = ivec2(-1);
		return false;
	}

	const vec2 reproj_uv = clip_space.xy / clip_space.w;
	reproj_pix = ivec2((reproj_uv * 0.5 + vec2(0.5)) * vec2(res));
	return all(greaterThanEqual(reproj_pix, ivec2(0))) && all(lessThan(reproj_pix, res));
}

bool reprojectToPrevFramePixel(vec3 prev_position, ivec2 res, out ivec2 reproj_pix, out float depth_necessary, out float depth_threshold) {
	float clip_w = 0.0;
	if (!projectWorldToPrevFramePixel(prev_position, res, reproj_pix, clip_w)) {
		depth_necessary = 0.0;
		depth_threshold = 0.0;
		return false;
	}

	const vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	depth_necessary = length(prev_position - prev_origin);
	depth_threshold = 0.01 * clip_w;
	return true;
}

#endif
