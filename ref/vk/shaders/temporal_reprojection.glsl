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

float sampleAverageReflectionRayLength(ivec2 pix, ivec2 res, int indirect_scale, int kernel_radius) {
	float average_ray_length = 0.0;
	float ray_length_samples_count = 0.0;
	for (int x = -kernel_radius; x <= kernel_radius; ++x) {
		for (int y = -kernel_radius; y <= kernel_radius; ++y) {
			const ivec2 p = pix / indirect_scale + ivec2(x, y);
			if (any(greaterThanEqual(p, res / indirect_scale)) || any(lessThan(p, ivec2(0)))) {
				continue;
			}

			average_ray_length += length(imageLoad(reflection_direction_pdf, p).xyz);
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
