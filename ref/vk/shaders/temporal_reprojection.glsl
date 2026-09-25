#ifndef TEMPORAL_REPROJECTION_GLSL_INCLUDED
#define TEMPORAL_REPROJECTION_GLSL_INCLUDED

#ifndef LOAD_REFLECTION_RAY_LENGTH
#define LOAD_REFLECTION_RAY_LENGTH(pix) length(imageLoad(reflection_direction_pdf, pix).xyz)
#endif

#ifndef ASVGF_REPROJECTION_PARAMS
#define ASVGF_REPROJECTION_PARAMS ubo.ubo.asvgf.direct_diffuse
#endif

bool projectWorldToPrevFrameUvCenter(vec3 world_position, ivec2 res, out vec2 reproj_uv_center, out float clip_w) {
	const vec4 prev_view_position = inverse(ubo.ubo.prev_inv_view) * vec4(world_position, 1.0);
	const vec4 clip_space = inverse(ubo.ubo.prev_inv_proj) * vec4(prev_view_position.xyz, 1.0);
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

ivec2 reprojectionUvCenterToNearestTexel(vec2 reproj_uv_center) {
	return ivec2(floor(reproj_uv_center + vec2(0.5)));
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

#ifndef REPROJECTION_TEXEL_SEARCH_MOTION_THRESHOLD
#define REPROJECTION_TEXEL_SEARCH_MOTION_THRESHOLD 1.0
#endif

bool projectWorldToPrevFramePixel(vec3 world_position, ivec2 res, out ivec2 reproj_pix, out float clip_w) {
	vec2 reproj_uv_center = vec2(-1.0);
	if (!projectWorldToPrevFrameUvCenter(world_position, res, reproj_uv_center, clip_w)) {
		reproj_pix = ivec2(-1);
		return false;
	}

	reproj_pix = reprojectionUvCenterToNearestTexel(reproj_uv_center);
	return isReprojectionTexelInside(reproj_pix, res);
}

bool projectWorldToPrevFramePixelLegacy(vec3 world_position, ivec2 res, out ivec2 reproj_pix, out float clip_w) {
	const vec4 prev_view_position = inverse(ubo.ubo.prev_inv_view) * vec4(world_position, 1.0);
	const vec4 clip_space = inverse(ubo.ubo.prev_inv_proj) * vec4(prev_view_position.xyz, 1.0);
	clip_w = clip_space.w;
	if (clip_w <= 0.0) {
		reproj_pix = ivec2(-1);
		return false;
	}

	const vec2 reproj_uv = clip_space.xy / clip_space.w;
	reproj_pix = ivec2((reproj_uv * 0.5 + vec2(0.5)) * vec2(res));
	return isReprojectionTexelInside(reproj_pix, res);
}

bool isValidReprojectionDepth(float depth) {
	return depth > 0.0 && !isnan(depth) && !isinf(depth);
}

// Keep metric reprojection depth in an rgba16f temporal target without losing
// far surfaces to half-float range. Stored .r is depth / 64; shader logic works
// only with decoded metric depth.
const float ASVGF_REPROJECTION_DEPTH_STORAGE_SCALE = 1.0 / 64.0;

float encodeReprojectionDepth(float metric_depth) {
	return max(metric_depth, 0.0) * ASVGF_REPROJECTION_DEPTH_STORAGE_SCALE;
}

float decodeReprojectionDepth(float stored_depth) {
	if (!(stored_depth > 0.0) || isnan(stored_depth) || isinf(stored_depth)) {
		return 0.0;
	}
	return stored_depth / ASVGF_REPROJECTION_DEPTH_STORAGE_SCALE;
}

float makeReprojectionDepthThresholdForParams(AsvgfReprojectionParams params, float expected_depth, float stored_depth, float base_threshold) {
	float reference_depth = max(max(abs(expected_depth), abs(stored_depth)), 1.0);
	float relative_threshold = max(base_threshold, params.reprojection_depth_threshold_scale * reference_depth);

	// The encoded history depth is kept in an rgba16f target. A small relative
	// floor covers fp16 quantization after depth/64 encoding and fp32
	// world-position cancellation on large coordinates.
	float storage_precision_floor = max(0.05, reference_depth * 0.003);

	// At distance, a one-pixel reprojection roundoff covers a larger world-space
	// footprint. This mostly affects slanted planes and parallax validation.
	float pixel_footprint_floor = reference_depth * max(ubo.ubo.ray_cone_width * 2.0, 0.0);

	return max(relative_threshold, max(storage_precision_floor, pixel_footprint_floor));
}

float makeReprojectionDepthThreshold(float expected_depth, float stored_depth, float base_threshold) {
	return makeReprojectionDepthThresholdForParams(ASVGF_REPROJECTION_PARAMS, expected_depth, stored_depth, base_threshold);
}

bool reprojectToPrevFrameUvCenterForParams(AsvgfReprojectionParams params, vec3 prev_position, ivec2 res, out vec2 reproj_uv_center, out float depth_necessary, out float depth_threshold) {
	float clip_w = 0.0;
	if (!projectWorldToPrevFrameUvCenter(prev_position, res, reproj_uv_center, clip_w)) {
		depth_necessary = 0.0;
		depth_threshold = 0.0;
		return false;
	}

	const vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	depth_necessary = length(prev_position - prev_origin);

	// depth_necessary/history_depth are metric camera distances. clip_w can be a
	// poor proxy for that metric with the current projection setup, so seed the
	// threshold with the larger of both and let makeReprojectionDepthThreshold add
	// storage/pixel-footprint tolerance.
	float projected_depth = max(depth_necessary, abs(clip_w));
	float base_threshold = params.reprojection_depth_threshold_scale * projected_depth;
	depth_threshold = makeReprojectionDepthThresholdForParams(params, depth_necessary, projected_depth, base_threshold);
	return true;
}

bool reprojectToPrevFramePixelForParams(AsvgfReprojectionParams params, vec3 prev_position, ivec2 res, out ivec2 reproj_pix, out float depth_necessary, out float depth_threshold) {
	vec2 reproj_uv_center = vec2(-1.0);
	if (!reprojectToPrevFrameUvCenterForParams(params, prev_position, res, reproj_uv_center, depth_necessary, depth_threshold)) {
		reproj_pix = ivec2(-1);
		return false;
	}

	reproj_pix = reprojectionUvCenterToNearestTexel(reproj_uv_center);
	return isReprojectionTexelInside(reproj_pix, res);
}

bool reprojectToPrevFramePixel(vec3 prev_position, ivec2 res, out ivec2 reproj_pix, out float depth_necessary, out float depth_threshold) {
	return reprojectToPrevFramePixelForParams(ASVGF_REPROJECTION_PARAMS, prev_position, res, reproj_pix, depth_necessary, depth_threshold);
}

bool reprojectToPrevFramePixelForParamsLegacy(
	AsvgfReprojectionParams params,
	vec3 prev_position,
	ivec2 res,
	out ivec2 reproj_pix,
	out float depth_necessary,
	out float depth_threshold)
{
	float clip_w = 0.0;
	if (!projectWorldToPrevFramePixelLegacy(prev_position, res, reproj_pix, clip_w)) {
		depth_necessary = 0.0;
		depth_threshold = 0.0;
		return false;
	}

	const vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	depth_necessary = length(prev_position - prev_origin);

	float projected_depth = max(depth_necessary, abs(clip_w));
	float base_threshold = params.reprojection_depth_threshold_scale * projected_depth;
	depth_threshold = makeReprojectionDepthThresholdForParams(params, depth_necessary, projected_depth, base_threshold);
	return true;
}

bool reprojectToPrevFramePixelLegacy(
	vec3 prev_position,
	ivec2 res,
	out ivec2 reproj_pix,
	out float depth_necessary,
	out float depth_threshold)
{
	return reprojectToPrevFramePixelForParamsLegacy(ASVGF_REPROJECTION_PARAMS, prev_position, res, reproj_pix, depth_necessary, depth_threshold);
}

bool computePlaneDepthInPrevFrame(ivec2 prev_pix, ivec2 res, vec3 plane_point, vec3 plane_normal, out float depth) {
	vec2 uv = ((vec2(prev_pix) + vec2(0.5)) / vec2(res)) * 2.0 - vec2(1.0);
	vec4 clip_far = vec4(uv, 1.0, 1.0);
	vec4 view_far = ubo.ubo.prev_inv_proj * clip_far;
	if (abs(view_far.w) <= 1e-6) {
		depth = 0.0;
		return false;
	}

	vec3 ray_dir = view_far.xyz / view_far.w;
	float ray_dir_len = length(ray_dir);
	if (ray_dir_len <= 1e-6) {
		depth = 0.0;
		return false;
	}
	ray_dir /= ray_dir_len;

	// Do the plane test in previous-view space. The old world-space variant built
	// world_near/world_far and then subtracted large values again, which made far
	// planar surfaces fail validation even when the temporal history existed.
	vec3 prev_origin = (ubo.ubo.prev_inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	mat3 world_to_prev_view_rotation = transpose(mat3(ubo.ubo.prev_inv_view));
	vec3 plane_point_view = world_to_prev_view_rotation * (plane_point - prev_origin);
	vec3 plane_normal_view = world_to_prev_view_rotation * plane_normal;
	float plane_normal_len = length(plane_normal_view);
	if (plane_normal_len <= 1e-6) {
		depth = 0.0;
		return false;
	}
	plane_normal_view /= plane_normal_len;

	float denom = dot(plane_normal_view, ray_dir);
	if (abs(denom) <= 1e-5) {
		depth = 0.0;
		return false;
	}

	float t = dot(plane_normal_view, plane_point_view) / denom;
	if (t <= 0.0) {
		depth = 0.0;
		return false;
	}

	depth = length(ray_dir * t);
	return isValidReprojectionDepth(depth);
}


#ifndef REPROJECTION_PLANE_TEXEL_CHECK_MULT
#define REPROJECTION_PLANE_TEXEL_CHECK_MULT 2.0
#endif

#ifndef REPROJECTION_PLANE_DISTANCE_MIN
#define REPROJECTION_PLANE_DISTANCE_MIN 0.02
#endif

bool computePlanePositionInFrame(
	ivec2 pix,
	ivec2 res,
	mat4 inv_proj,
	mat4 inv_view,
	vec3 plane_point,
	vec3 plane_normal,
	out vec3 plane_position)
{
	plane_position = vec3(0.0);
	if (!isReprojectionTexelInside(pix, res)) {
		return false;
	}

	const vec2 uv = ((vec2(pix) + vec2(0.5)) / vec2(res)) * 2.0 - vec2(1.0);
	const vec4 view_far_h = inv_proj * vec4(uv, 1.0, 1.0);
	vec3 view_far = view_far_h.xyz;
	if (abs(view_far_h.w) > 1e-6) {
		view_far /= view_far_h.w;
	}

	vec3 ray_dir = (inv_view * vec4(view_far, 0.0)).xyz;
	const float ray_dir_len = length(ray_dir);
	if (ray_dir_len <= 1e-6) {
		return false;
	}
	ray_dir /= ray_dir_len;

	vec3 N = plane_normal;
	const float normal_len = length(N);
	if (normal_len <= 1e-6) {
		return false;
	}
	N /= normal_len;

	const vec3 ray_origin = (inv_view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	const float denom = dot(N, ray_dir);
	if (abs(denom) <= 1e-5) {
		return false;
	}

	const float t = dot(N, plane_point - ray_origin) / denom;
	if (t <= 0.0) {
		return false;
	}

	plane_position = ray_origin + ray_dir * t;
	return all(not(isnan(plane_position))) && all(not(isinf(plane_position)));
}

bool computePlanePositionInCurrentFrame(
	ivec2 pix,
	ivec2 res,
	vec3 plane_point,
	vec3 plane_normal,
	out vec3 plane_position)
{
	return computePlanePositionInFrame(
		pix,
		res,
		ubo.ubo.inv_proj,
		ubo.ubo.inv_view,
		plane_point,
		plane_normal,
		plane_position);
}

bool computePlanePositionInPrevFrame(
	ivec2 pix,
	ivec2 res,
	vec3 plane_point,
	vec3 plane_normal,
	out vec3 plane_position)
{
	return computePlanePositionInFrame(
		pix,
		res,
		ubo.ubo.prev_inv_proj,
		ubo.ubo.prev_inv_view,
		plane_point,
		plane_normal,
		plane_position);
}

ivec2 reprojectionNeighborTexelForFootprint(ivec2 pix, ivec2 res, ivec2 axis)
{
	ivec2 p = pix + axis;
	if (isReprojectionTexelInside(p, res)) {
		return p;
	}
	p = pix - axis;
	if (isReprojectionTexelInside(p, res)) {
		return p;
	}
	return pix;
}

float estimatePlaneTexelWorldSizeInFrame(
	ivec2 pix,
	ivec2 res,
	mat4 inv_proj,
	mat4 inv_view,
	vec3 plane_point,
	vec3 plane_normal,
	vec3 center_plane_position)
{
	float world_texel_size = 0.0;

	vec3 neighbor_position = vec3(0.0);
	const ivec2 pix_x = reprojectionNeighborTexelForFootprint(pix, res, ivec2(1, 0));
	if (!all(equal(pix_x, pix)) &&
		computePlanePositionInFrame(pix_x, res, inv_proj, inv_view, plane_point, plane_normal, neighbor_position)) {
		world_texel_size = max(world_texel_size, length(neighbor_position - center_plane_position));
	}

	const ivec2 pix_y = reprojectionNeighborTexelForFootprint(pix, res, ivec2(0, 1));
	if (!all(equal(pix_y, pix)) &&
		computePlanePositionInFrame(pix_y, res, inv_proj, inv_view, plane_point, plane_normal, neighbor_position)) {
		world_texel_size = max(world_texel_size, length(neighbor_position - center_plane_position));
	}

	return world_texel_size;
}

float planeTexelCompatibilityWeight(
	vec3 expected_plane_position,
	vec3 sample_position,
	float world_texel_size)
{
	const float threshold = max(
		REPROJECTION_PLANE_DISTANCE_MIN,
		world_texel_size * REPROJECTION_PLANE_TEXEL_CHECK_MULT);
	const float plane_error = length(sample_position - expected_plane_position);
	if (!(plane_error <= threshold)) {
		return 0.0;
	}
	return 1.0 - plane_error / max(threshold, 1e-6);
}

float normalCompatibilityWeight(vec3 center_normal, vec3 sample_normal, float normal_min)
{
	vec3 N0 = center_normal;
	vec3 N1 = sample_normal;
	const float len0 = length(N0);
	const float len1 = length(N1);
	if (len0 <= 1e-6 || len1 <= 1e-6) {
		return 0.0;
	}
	N0 /= len0;
	N1 /= len1;

	const float normal_alignment = dot(N0, N1);
	if (normal_alignment < normal_min) {
		return 0.0;
	}
	return clamp((normal_alignment - normal_min) / max(1.0 - normal_min, 1e-3), 0.0, 1.0);
}

float planeCompatibleTexelWeightInFrame(
	ivec2 sample_pix,
	ivec2 res,
	mat4 inv_proj,
	mat4 inv_view,
	vec3 plane_point,
	vec3 plane_normal,
	vec3 sample_position)
{
	vec3 expected_plane_position = vec3(0.0);
	if (!computePlanePositionInFrame(sample_pix, res, inv_proj, inv_view, plane_point, plane_normal, expected_plane_position)) {
		return 0.0;
	}

	const float world_texel_size = estimatePlaneTexelWorldSizeInFrame(
		sample_pix,
		res,
		inv_proj,
		inv_view,
		plane_point,
		plane_normal,
		expected_plane_position);
	return planeTexelCompatibilityWeight(expected_plane_position, sample_position, world_texel_size);
}

float currentFramePlaneCompatibleTexelWeight(
	ivec2 sample_pix,
	ivec2 res,
	vec3 plane_point,
	vec3 plane_normal,
	vec3 sample_position)
{
	return planeCompatibleTexelWeightInFrame(
		sample_pix,
		res,
		ubo.ubo.inv_proj,
		ubo.ubo.inv_view,
		plane_point,
		plane_normal,
		sample_position);
}

float currentFramePlaneCompatibleTexelWeight(
	ivec2 sample_pix,
	ivec2 res,
	vec3 plane_point,
	vec3 plane_normal,
	vec3 sample_position,
	vec3 sample_normal,
	float normal_min)
{
	const float plane_weight = currentFramePlaneCompatibleTexelWeight(
		sample_pix,
		res,
		plane_point,
		plane_normal,
		sample_position);
	if (plane_weight <= 0.0) {
		return 0.0;
	}
	return plane_weight * normalCompatibilityWeight(plane_normal, sample_normal, normal_min);
}

float prevFramePlaneCompatibleTexelWeight(
	ivec2 sample_pix,
	ivec2 res,
	vec3 plane_point,
	vec3 plane_normal,
	vec3 sample_position)
{
	return planeCompatibleTexelWeightInFrame(
		sample_pix,
		res,
		ubo.ubo.prev_inv_proj,
		ubo.ubo.prev_inv_view,
		plane_point,
		plane_normal,
		sample_position);
}


#ifdef REPROJECTION_LOAD_PREV_DEPTH_META

bool validateReprojectedHistoryTexelForParams(
	AsvgfReprojectionParams params,
	ivec2 history_pix,
	ivec2 res,
	vec3 prev_position,
	vec3 geometry_normal,
	float depth_necessary,
	float depth_threshold,
	out float history_depth_threshold)
{
	history_depth_threshold = 0.0;

	if (!isReprojectionTexelInside(history_pix, res)) {
		return false;
	}

	const vec4 history_depth_meta = REPROJECTION_LOAD_PREV_DEPTH_META(history_pix);
	const float history_depth = decodeReprojectionDepth(history_depth_meta.r);
	if (!isValidReprojectionDepth(history_depth)) {
		return false;
	}

	float expected_depth = depth_necessary;
	float plane_depth = 0.0;
	if (computePlaneDepthInPrevFrame(history_pix, res, prev_position, geometry_normal, plane_depth)) {
		expected_depth = plane_depth;
	}

	history_depth_threshold = makeReprojectionDepthThresholdForParams(params, expected_depth, history_depth, depth_threshold);
	return abs(history_depth - expected_depth) < history_depth_threshold;
}

bool buildValidatedReprojectionHistoryTapsForParams(
	AsvgfReprojectionParams params,
	vec3 prev_position,
	vec3 geometry_normal,
	ivec2 current_pix,
	ivec2 res,
	out ivec2 history_taps[4],
	out float history_weights[4],
	out int history_tap_count,
	out vec2 reproj_uv_center,
	out float accumulated_depth_threshold)
{
	history_tap_count = 0;
	accumulated_depth_threshold = 0.0;
	for (int i = 0; i < 4; ++i) {
		history_taps[i] = ivec2(-1);
		history_weights[i] = 0.0;
	}

	float depth_necessary = 0.0;
	float depth_threshold = 0.0;
	if (!reprojectToPrevFrameUvCenterForParams(params, prev_position, res, reproj_uv_center, depth_necessary, depth_threshold)) {
		return false;
	}

	ivec2 candidate_taps[4];
	float candidate_weights[4];
	int candidate_count = 1;
	const float motion = length(reproj_uv_center - vec2(current_pix));
	if (motion > REPROJECTION_TEXEL_SEARCH_MOTION_THRESHOLD) {
		buildReprojectionFootprint2x2(reproj_uv_center, candidate_taps, candidate_weights);
		candidate_count = 4;
	} else {
		candidate_taps[0] = reprojectionUvCenterToNearestTexel(reproj_uv_center);
		candidate_weights[0] = 1.0;
		for (int i = 1; i < 4; ++i) {
			candidate_taps[i] = ivec2(-1);
			candidate_weights[i] = 0.0;
		}
	}

	float valid_weight_sum = 0.0;
	float weighted_depth_threshold_sum = 0.0;
	for (int i = 0; i < 4; ++i) {
		if (i >= candidate_count) {
			break;
		}

		const ivec2 candidate_pix = candidate_taps[i];
		const float candidate_weight = candidate_weights[i];
		if (candidate_weight <= 0.0) {
			continue;
		}

		float candidate_depth_threshold = 0.0;
		if (!validateReprojectedHistoryTexelForParams(params, candidate_pix, res, prev_position, geometry_normal, depth_necessary, depth_threshold, candidate_depth_threshold)) {
			continue;
		}

		history_taps[history_tap_count] = candidate_pix;
		history_weights[history_tap_count] = candidate_weight;
		history_tap_count += 1;
		valid_weight_sum += candidate_weight;
		weighted_depth_threshold_sum += candidate_depth_threshold * candidate_weight;
	}

	if (history_tap_count <= 0 || valid_weight_sum <= 0.0) {
		history_tap_count = 0;
		return false;
	}

	const float inv_weight_sum = 1.0 / valid_weight_sum;
	for (int i = 0; i < 4; ++i) {
		if (i >= history_tap_count) {
			break;
		}
		history_weights[i] *= inv_weight_sum;
	}
	accumulated_depth_threshold = weighted_depth_threshold_sum * inv_weight_sum;
	return true;
}

bool buildValidatedReprojectionHistoryTaps(
	vec3 prev_position,
	vec3 geometry_normal,
	ivec2 current_pix,
	ivec2 res,
	out ivec2 history_taps[4],
	out float history_weights[4],
	out int history_tap_count,
	out vec2 reproj_uv_center,
	out float accumulated_depth_threshold)
{
	return buildValidatedReprojectionHistoryTapsForParams(
		ASVGF_REPROJECTION_PARAMS,
		prev_position,
		geometry_normal,
		current_pix,
		res,
		history_taps,
		history_weights,
		history_tap_count,
		reproj_uv_center,
		accumulated_depth_threshold);
}

bool findBestReprojectedHistoryTexelForParams(
	AsvgfReprojectionParams params,
	vec3 prev_position,
	vec3 geometry_normal,
	ivec2 current_pix,
	ivec2 res,
	out ivec2 history_pix,
	out float selected_depth_threshold)
{
	history_pix = ivec2(-1);
	selected_depth_threshold = 0.0;

	vec2 reproj_uv_center = vec2(-1.0);
	float depth_necessary = 0.0;
	float depth_threshold = 0.0;
	if (!reprojectToPrevFrameUvCenterForParams(params, prev_position, res, reproj_uv_center, depth_necessary, depth_threshold)) {
		return false;
	}

	ivec2 candidate_taps[4];
	float candidate_weights[4];
	int candidate_count = 1;
	const float motion = length(reproj_uv_center - vec2(current_pix));
	if (motion > REPROJECTION_TEXEL_SEARCH_MOTION_THRESHOLD) {
		buildReprojectionFootprint2x2(reproj_uv_center, candidate_taps, candidate_weights);
		candidate_count = 4;
	} else {
		candidate_taps[0] = reprojectionUvCenterToNearestTexel(reproj_uv_center);
		candidate_weights[0] = 1.0;
		for (int i = 1; i < 4; ++i) {
			candidate_taps[i] = ivec2(-1);
			candidate_weights[i] = 0.0;
		}
	}

	float best_weight = -1.0;
	for (int i = 0; i < 4; ++i) {
		if (i >= candidate_count) {
			break;
		}

		const ivec2 candidate_pix = candidate_taps[i];
		const float candidate_weight = candidate_weights[i];
		float candidate_depth_threshold = 0.0;
		if (!validateReprojectedHistoryTexelForParams(params, candidate_pix, res, prev_position, geometry_normal, depth_necessary, depth_threshold, candidate_depth_threshold)) {
			continue;
		}

		if (candidate_weight > best_weight) {
			best_weight = candidate_weight;
			history_pix = candidate_pix;
			selected_depth_threshold = candidate_depth_threshold;
		}
	}

	return best_weight >= 0.0 && isReprojectionTexelInside(history_pix, res);
}

bool findBestReprojectedHistoryTexel(
	vec3 prev_position,
	vec3 geometry_normal,
	ivec2 current_pix,
	ivec2 res,
	out ivec2 history_pix,
	out float selected_depth_threshold)
{
	return findBestReprojectedHistoryTexelForParams(
		ASVGF_REPROJECTION_PARAMS,
		prev_position,
		geometry_normal,
		current_pix,
		res,
		history_pix,
		selected_depth_threshold);
}

#endif // REPROJECTION_LOAD_PREV_DEPTH_META

#if defined(TEMPORAL_REPROJECTION_ENABLE_HALF_RES_ATLAS_PRIMARY_PLANE)
#ifndef TEMPORAL_REPROJECTION_PRIMARY_PIXEL_COMPATIBLE
#define TEMPORAL_REPROJECTION_PRIMARY_PIXEL_COMPATIBLE(primary_pix_) true
#endif

bool loadHalfResAtlasPrimaryPlane(
	ivec2 local_pix,
	ivec2 half_res,
	ivec2 primary_res,
	out vec3 prev_position,
	out vec3 geometry_normal)
{
	prev_position = vec3(0.0);
	geometry_normal = vec3(0.0, 0.0, 1.0);

	if (any(lessThan(local_pix, ivec2(0))) || any(greaterThanEqual(local_pix, half_res))) {
		return false;
	}

	ivec2 primary_pix = ivec2(-1);
	float best_t = 1e30;
	for (int y = 0; y < 2; ++y) {
		for (int x = 0; x < 2; ++x) {
			const ivec2 candidate_pix = local_pix * 2 + ivec2(x, y);
			if (any(greaterThanEqual(candidate_pix, primary_res))) {
				continue;
			}
			if (!TEMPORAL_REPROJECTION_PRIMARY_PIXEL_COMPATIBLE(candidate_pix)) {
				continue;
			}

			const vec4 pos_t = imageLoad(position_t, candidate_pix);
			if (pos_t.w <= 0.0) {
				continue;
			}

			if (pos_t.w < best_t) {
				best_t = pos_t.w;
				primary_pix = candidate_pix;
			}
		}
	}

	if (primary_pix.x < 0) {
		return false;
	}

	prev_position = imageLoad(geometry_prev_position, primary_pix).rgb;
	geometry_normal = normalDecode(imageLoad(normals_gs, primary_pix).xy);
	return true;
}

bool reprojectHalfResAtlasPrimaryPlanePixel(
	ivec2 local_pix,
	ivec2 half_res,
	AsvgfReprojectionParams params,
	out ivec2 history_local_pix)
{
	history_local_pix = ivec2(-1);

	vec3 prev_position;
	vec3 geometry_normal;
	if (!loadHalfResAtlasPrimaryPlane(local_pix, half_res, ubo.ubo.res, prev_position, geometry_normal)) {
		return false;
	}

	ivec2 history_screen_pix;
	float depth_necessary = 0.0;
	float depth_threshold = 0.0;
	if (!reprojectToPrevFramePixelForParams(params, prev_position, ubo.ubo.res, history_screen_pix, depth_necessary, depth_threshold)) {
		return false;
	}

	const vec4 history_depth_meta = imageLoad(prev_temporal_asvgf_reproj_depth, history_screen_pix);
	const float history_depth = decodeReprojectionDepth(history_depth_meta.r);
	if (!isValidReprojectionDepth(history_depth)) {
		return false;
	}

	float expected_depth = depth_necessary;
	float plane_depth = 0.0;
	if (computePlaneDepthInPrevFrame(history_screen_pix, ubo.ubo.res, prev_position, geometry_normal, plane_depth)) {
		expected_depth = plane_depth;
	}

	const float threshold = makeReprojectionDepthThresholdForParams(params, expected_depth, history_depth, depth_threshold);
	if (abs(history_depth - expected_depth) >= threshold) {
		return false;
	}

	history_local_pix = history_screen_pix / 2;
	return all(greaterThanEqual(history_local_pix, ivec2(0))) &&
		all(lessThan(history_local_pix, half_res));
}

bool reprojectHalfResAtlasPrimaryPlanePixelLegacy(
	ivec2 local_pix,
	ivec2 half_res,
	AsvgfReprojectionParams params,
	out ivec2 history_local_pix)
{
	history_local_pix = ivec2(-1);

	vec3 prev_position;
	vec3 geometry_normal;
	if (!loadHalfResAtlasPrimaryPlane(local_pix, half_res, ubo.ubo.res, prev_position, geometry_normal)) {
		return false;
	}

	ivec2 history_screen_pix;
	float depth_necessary = 0.0;
	float depth_threshold = 0.0;
	if (!reprojectToPrevFramePixelForParamsLegacy(params, prev_position, ubo.ubo.res, history_screen_pix, depth_necessary, depth_threshold)) {
		return false;
	}

	const vec4 history_depth_meta = imageLoad(prev_temporal_asvgf_reproj_depth, history_screen_pix);
	const float history_depth = decodeReprojectionDepth(history_depth_meta.r);
	if (!isValidReprojectionDepth(history_depth)) {
		return false;
	}

	float expected_depth = depth_necessary;
	float plane_depth = 0.0;
	if (computePlaneDepthInPrevFrame(history_screen_pix, ubo.ubo.res, prev_position, geometry_normal, plane_depth)) {
		expected_depth = plane_depth;
	}

	const float threshold = makeReprojectionDepthThresholdForParams(params, expected_depth, history_depth, depth_threshold);
	if (abs(history_depth - expected_depth) >= threshold) {
		return false;
	}

	history_local_pix = history_screen_pix / 2;
	return all(greaterThanEqual(history_local_pix, ivec2(0))) &&
		all(lessThan(history_local_pix, half_res));
}
#endif

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

bool intersectRayPlane(vec3 ray_origin, vec3 ray_direction, vec3 plane_point, vec3 plane_normal, out vec3 hit_position) {
	hit_position = vec3(0.0);
	const float denom = dot(plane_normal, ray_direction);
	if (abs(denom) <= 1e-5) {
		return false;
	}

	const float t = dot(plane_normal, plane_point - ray_origin) / denom;
	if (t <= 0.0) {
		return false;
	}

	hit_position = ray_origin + ray_direction * t;
	return true;
}

bool refractionPlaneReprojectToPrevFramePixel(vec3 prev_plane_position, vec3 plane_normal, vec3 prev_origin, vec3 prev_refracted_target, float eta_ratio, ivec2 res, out ivec2 refraction_pix) {
	refraction_pix = ivec2(-1);

	const float plane_normal_len = length(plane_normal);
	if (plane_normal_len <= 1e-6) {
		return false;
	}

	vec3 N = plane_normal / plane_normal_len;
	if (dot(N, prev_origin - prev_plane_position) < 0.0) {
		N = -N;
	}

	const vec3 target_delta = prev_refracted_target - prev_origin;
	const float target_distance = length(target_delta);
	if (target_distance <= 1e-6) {
		return false;
	}

	vec3 entry_position;
	if (!intersectRayPlane(prev_origin, target_delta / target_distance, prev_plane_position, N, entry_position)) {
		return false;
	}

	const float eta = max(eta_ratio, 0.0);
	if (abs(eta - 1.0) > 1e-4) {
		for (int i = 0; i < 3; ++i) {
			const vec3 incident = normalize(entry_position - prev_origin);
			vec3 oriented_N = N;
			if (dot(incident, oriented_N) > 0.0) {
				oriented_N = -oriented_N;
			}

			const vec3 refracted = refract(incident, oriented_N, eta);
			if (dot(refracted, refracted) <= 1e-8) {
				return false;
			}

			vec3 next_entry_position;
			if (!intersectRayPlane(prev_refracted_target, -normalize(refracted), prev_plane_position, N, next_entry_position)) {
				return false;
			}

			entry_position = next_entry_position;
		}
	}

	float clip_w = 0.0;
	return projectWorldToPrevFramePixel(entry_position, res, refraction_pix, clip_w);
}

#endif
