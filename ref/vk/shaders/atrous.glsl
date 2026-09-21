#ifndef ATROUS_CONSTS_DECLARED
#define ATROUS_CONSTS_DECLARED

#include "utils.glsl"

// https://jo.dreggn.org/home/2010_atrous.pdf
// https://www.shadertoy.com/view/ldKBzG
#define ATROUS_KERNEL_WIDTH 5
#define ATROUS_KERNEL_HALF 2
const float kATrousKernel[ATROUS_KERNEL_WIDTH] = { 1./16., 1./4., 3./8., 1./4., 1./16. };


// Depends on:
// - image2D normals_gs
// - image2D position_t
float aTrousSampleWeigth(const ivec2 res, const ivec2 pix, vec3 pos, vec3 shading_normal, ivec2 offset, int step_width, int pix_scale, float phi_normal, float phi_pos, out ivec2 p) {
	const float x_kernel = kATrousKernel[offset.x];
	const float y_kernel = kATrousKernel[offset.y];

	const float inv_step_width_sq = 1. / float(step_width * step_width);
	p = pix + (offset - ivec2(ATROUS_KERNEL_HALF)) * step_width;
	const ivec2 p_scaled = p * pix_scale;

	if (any(greaterThanEqual(p_scaled, res)) || any(lessThan(p_scaled, ivec2(0)))) {
		return 0.;
	}

	// Weight normals
	const vec4 ngs = imageLoad(normals_gs, p_scaled);
	const vec3 sample_shading_normal = normalDecode(ngs.zw);

	// TODO should we go geometry_normal too?
	const vec3 sn_diff = sample_shading_normal - shading_normal;
	const float sn_dist2 = max(dot(sn_diff,sn_diff) * inv_step_width_sq, 0.);
	const float weight_sn = min(exp(-(sn_dist2)/phi_normal), 1.0);

	// Weight positions
	const vec3 sample_position = imageLoad(position_t, p_scaled).xyz;
	const vec3 p_diff = sample_position - pos;
	//Original paper: const float p_dist2 = dot(p_diff, p_diff);
	const float p_dist2 = max(dot(p_diff,p_diff) * inv_step_width_sq, 0.);
	const float weight_pos = min(exp(-(p_dist2)/phi_pos),1.0);

	const float weight = (weight_pos * weight_sn) * x_kernel * y_kernel;
	return weight;
}

#endif // ifndef ATROUS_CONSTS_DECLARED
