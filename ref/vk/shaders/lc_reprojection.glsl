#ifndef LC_REPROJECTION_GLSL
#define LC_REPROJECTION_GLSL

bool projectWorldToPrevFrameUvCenter(vec3 world_position, ivec2 res, out vec2 reproj_uv_center, out float clip_w) {
	const vec4 prev_view_position = ubo.ubo.prev_view * vec4(world_position, 1.0);
	const vec4 clip_space = ubo.ubo.prev_proj * vec4(prev_view_position.xyz, 1.0);
	clip_w = clip_space.w;
	if (clip_w <= 0.0) {
		reproj_uv_center = vec2(-1.0);
		return false;
	}

	const vec2 reproj_ndc = clip_space.xy / clip_space.w;
	reproj_uv_center = (reproj_ndc * 0.5 + vec2(0.5)) * vec2(res) - vec2(0.5);
	return all(greaterThanEqual(reproj_uv_center, vec2(-0.5))) &&
		all(lessThan(reproj_uv_center, vec2(res) - vec2(0.5)));
}

bool isReprojectionTexelInside(ivec2 pix, ivec2 res) {
	return all(greaterThanEqual(pix, ivec2(0))) && all(lessThan(pix, res));
}

void buildReprojectionFootprint2x2(vec2 reproj_uv_center, out ivec2 taps[4], out float weights[4]) {
	const ivec2 base = ivec2(floor(reproj_uv_center));
	const vec2 f = fract(reproj_uv_center);

	taps[0] = base + ivec2(0, 0);
	taps[1] = base + ivec2(1, 0);
	taps[2] = base + ivec2(0, 1);
	taps[3] = base + ivec2(1, 1);

	weights[0] = (1.0 - f.x) * (1.0 - f.y);
	weights[1] =  f.x        * (1.0 - f.y);
	weights[2] = (1.0 - f.x) *  f.y;
	weights[3] =  f.x        *  f.y;
}


#endif
