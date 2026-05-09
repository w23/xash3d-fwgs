#ifndef TEMPORAL_REPROJECTION_GLSL_INCLUDED
#define TEMPORAL_REPROJECTION_GLSL_INCLUDED

#ifndef LOAD_REFLECTION_RAY_LENGTH
#define LOAD_REFLECTION_RAY_LENGTH(pix) length(imageLoad(reflection_direction_pdf, pix).xyz)
#endif

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

bool computePlaneDepthInPrevFrame(ivec2 prev_pix, ivec2 res, vec3 plane_point, vec3 plane_normal, out float depth) {
	vec2 uv = ((vec2(prev_pix) + vec2(0.5)) / vec2(res)) * 2.0 - vec2(1.0);
	vec4 clip_near = vec4(uv, 0.0, 1.0);
	vec4 clip_far = vec4(uv, 1.0, 1.0);
	vec4 view_near = ubo.ubo.prev_inv_proj * clip_near;
	vec4 view_far = ubo.ubo.prev_inv_proj * clip_far;
	if (abs(view_near.w) <= 1e-6 || abs(view_far.w) <= 1e-6) {
		depth = 0.0;
		return false;
	}

	vec3 world_near = (ubo.ubo.prev_inv_view * vec4(view_near.xyz / view_near.w, 1.0)).xyz;
	vec3 world_far = (ubo.ubo.prev_inv_view * vec4(view_far.xyz / view_far.w, 1.0)).xyz;
	vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 ray_dir = normalize(world_far - world_near);
	float denom = dot(plane_normal, ray_dir);
	if (abs(denom) <= 1e-5) {
		depth = 0.0;
		return false;
	}

	float t = dot(plane_normal, plane_point - prev_origin) / denom;
	if (t <= 0.0) {
		depth = 0.0;
		return false;
	}

	depth = t;
	return true;
}

float sampleAverageReflectionRayLength(ivec2 pix, ivec2 res, int indirect_scale, int kernel_radius) {
	float average_ray_length = 0.0;
	float ray_length_samples_count = 0.0;
	for (int x = -kernel_radius; x <= kernel_radius; ++x) {
		for (int y = -kernel_radius; y <= kernel_radius; ++y) {
			const ivec2 p = pix / indirect_scale + ivec2(x, y);
			if (any(greaterThanEqual(p, res / indirect_scale)) || any(lessThan(p, ivec2(0)))) {
				continue;
			}

			average_ray_length += LOAD_REFLECTION_RAY_LENGTH(p);
			ray_length_samples_count += 1.0;
		}
	}

	if (ray_length_samples_count <= 0.0 || average_ray_length <= 0.0) {
		return 0.0;
	}

	return average_ray_length / ray_length_samples_count;
}

bool parallaxReprojectToPrevFramePixel(vec3 position, vec3 prev_position, vec3 geometry_normal, vec3 origin, vec3 prev_origin, float average_ray_length, ivec2 res, out ivec2 parallax_pix) {
	parallax_pix = ivec2(-1);
	if (average_ray_length <= 0.0) {
		return false;
	}

	const vec3 refl_position = reflect(normalize(position - origin), geometry_normal) * average_ray_length + position;
	const float refl_distance_to_plane = dot(geometry_normal, refl_position - prev_position);
	const vec3 refl_on_plane = refl_position - geometry_normal * refl_distance_to_plane;
	const float prev_distance_to_plane = dot(geometry_normal, prev_origin - prev_position);
	const vec3 prev_origin_on_plane = prev_origin - geometry_normal * prev_distance_to_plane;
	const float refl_center = prev_distance_to_plane / (prev_distance_to_plane + refl_distance_to_plane);
	const vec3 parallax_position = mix(prev_origin_on_plane, refl_on_plane, refl_center);

	float clip_w = 0.0;
	return projectWorldToPrevFramePixel(parallax_position, res, parallax_pix, clip_w);
}

#endif
