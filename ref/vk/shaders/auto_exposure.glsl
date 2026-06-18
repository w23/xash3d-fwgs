#ifndef AUTO_EXPOSURE_GLSL_INCLUDED
#define AUTO_EXPOSURE_GLSL_INCLUDED

#ifndef AUTO_EXPOSURE_HISTOGRAM_BINS
#define AUTO_EXPOSURE_HISTOGRAM_BINS 32
#endif

#if AUTO_EXPOSURE_HISTOGRAM_BINS > 32
#error AUTO_EXPOSURE_HISTOGRAM_BINS must fit into one 8x8 storage tile
#endif

const int AE_WORKGROUP_SIZE = 8;
const int AE_BINS_PER_TEXEL = 4;
const int AE_HISTOGRAM_BINS = AUTO_EXPOSURE_HISTOGRAM_BINS;
const int AE_HISTOGRAM_TEXELS = (AUTO_EXPOSURE_HISTOGRAM_BINS + AE_BINS_PER_TEXEL - 1) / AE_BINS_PER_TEXEL;
const int AE_MAX_LEVELS = 8;

const float AE_LOG_LUMINANCE_MIN = -16.;
const float AE_LOG_LUMINANCE_MAX = 24.;
const float AE_TARGET_GREY = 0.18;
const float AE_LOW_PERCENTILE = 0.70;
const float AE_HIGH_PERCENTILE = 0.999;
const float AE_ADAPT_DARKEN_SPEED = 0.12;
const float AE_ADAPT_LIGHTEN_SPEED = 0.05;
const float AE_MIN_EXPOSURE = 1. / 16777216.;
const float AE_MAX_EXPOSURE = 16777216.;

shared uint ae_workgroup_histogram[AUTO_EXPOSURE_HISTOGRAM_BINS];

float aeLuminance(vec3 colour) {
	return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

float aeLogLuminance(vec3 colour) {
	return log2(max(aeLuminance(colour), 1e-6));
}

int aeHistogramBin(float log_luminance) {
	const float normalized = clamp(
		(log_luminance - AE_LOG_LUMINANCE_MIN) / (AE_LOG_LUMINANCE_MAX - AE_LOG_LUMINANCE_MIN),
		0.,
		.999999);
	return int(normalized * float(AE_HISTOGRAM_BINS));
}

float aeBinCenterLogLuminance(int bin) {
	return AE_LOG_LUMINANCE_MIN
		+ (float(bin) + .5) * (AE_LOG_LUMINANCE_MAX - AE_LOG_LUMINANCE_MIN) / float(AE_HISTOGRAM_BINS);
}

ivec2 aeDivideRoundUp(ivec2 v, int divisor) {
	return (v + ivec2(divisor - 1)) / divisor;
}

ivec2 aeLevelSize(ivec2 res, int level) {
	ivec2 size = aeDivideRoundUp(res, AE_WORKGROUP_SIZE);
	for (int i = 0; i < level; ++i) {
		size = aeDivideRoundUp(size, AE_WORKGROUP_SIZE);
	}
	return max(size, ivec2(1));
}

int aeLevelOffsetY(ivec2 res, int level) {
	int offset = 0;
	for (int i = 0; i < level; ++i) {
		offset += aeLevelSize(res, i).y;
	}
	return offset;
}

int aeGlobalLevel(ivec2 res) {
	for (int level = 0; level < AE_MAX_LEVELS; ++level) {
		if (all(equal(aeLevelSize(res, level), ivec2(1)))) {
			return level;
		}
	}
	return AE_MAX_LEVELS - 1;
}

ivec2 aeExposureCoord(ivec2 res) {
	return max(res - ivec2(1), ivec2(0));
}

bool aeCoordInside(ivec2 res, ivec2 coord) {
	return all(greaterThanEqual(coord, ivec2(0))) && all(lessThan(coord, res));
}

ivec2 aeHistogramTexelCoord(ivec2 res, int level, ivec2 cell, int hist_texel) {
	return ivec2(cell.x * AE_HISTOGRAM_TEXELS + hist_texel, aeLevelOffsetY(res, level) + cell.y);
}

vec4 aeLoadHistogramTexel(ivec2 res, int level, ivec2 cell, int hist_texel) {
	const ivec2 coord = aeHistogramTexelCoord(res, level, cell, hist_texel);
	if (!aeCoordInside(res, coord)) {
		return vec4(0.);
	}
	return loadExposureTemporalData(coord);
}

void aeStoreHistogramTexel(ivec2 res, int level, ivec2 cell, int hist_texel, vec4 value) {
	const ivec2 coord = aeHistogramTexelCoord(res, level, cell, hist_texel);
	if (aeCoordInside(res, coord)) {
		storeExposureTemporalData(coord, value);
	}
}

float aeReadHistogramBin(ivec2 res, int level, ivec2 cell, int bin) {
	const vec4 texel = aeLoadHistogramTexel(res, level, cell, bin / AE_BINS_PER_TEXEL);
	return texel[bin % AE_BINS_PER_TEXEL];
}

float readAutoExposure(ivec2 res) {
	float exposure = loadExposureTemporalData(aeExposureCoord(res)).r;
	if (IS_INVALID(exposure) || exposure <= 0.) {
		exposure = 1.;
	}
	return clamp(exposure, AE_MIN_EXPOSURE, AE_MAX_EXPOSURE);
}

// Stage 1: build the current frame L0 histogram inside one 8x8 workgroup.
void aeStoreCurrentWorkgroupHistogram(ivec2 res, ivec2 pix, vec3 linear_colour) {
	const ivec2 workgroup_id = ivec2(gl_WorkGroupID.xy);
	const ivec2 workgroup_origin = workgroup_id * AE_WORKGROUP_SIZE;
	const bool full_workgroup = all(lessThanEqual(workgroup_origin + ivec2(AE_WORKGROUP_SIZE), res));
	const bool pixel_inside = aeCoordInside(res, pix);
	const uint local_index = gl_LocalInvocationIndex;

	if (full_workgroup) {
		if (local_index < uint(AE_HISTOGRAM_BINS)) {
			ae_workgroup_histogram[local_index] = 0u;
		}
		barrier();

		if (pixel_inside) {
			atomicAdd(ae_workgroup_histogram[aeHistogramBin(aeLogLuminance(linear_colour))], 1u);
		}

		barrier();

		if (local_index < uint(AE_HISTOGRAM_TEXELS)) {
			vec4 bins = vec4(0.);
			for (int channel = 0; channel < AE_BINS_PER_TEXEL; ++channel) {
				const int bin = int(local_index) * AE_BINS_PER_TEXEL + channel;
				if (bin < AE_HISTOGRAM_BINS) {
					bins[channel] = float(ae_workgroup_histogram[bin]);
				}
			}
			aeStoreHistogramTexel(res, 0, workgroup_id, int(local_index), bins);
		}
	} else if (local_index == 0u) {
		for (int hist_texel = 0; hist_texel < AE_HISTOGRAM_TEXELS; ++hist_texel) {
			aeStoreHistogramTexel(res, 0, workgroup_id, hist_texel, vec4(0.));
		}
	}
}

// Stage 2: advance the temporal histogram pyramid using only previous-frame data.
vec4 aeReduceHistogramTexel(ivec2 res, int src_level, ivec2 dst_cell, int hist_texel) {
	const ivec2 src_size = aeLevelSize(res, src_level);
	const ivec2 src_origin = dst_cell * AE_WORKGROUP_SIZE;
	vec4 sum = vec4(0.);

	for (int y = 0; y < AE_WORKGROUP_SIZE; ++y) {
		for (int x = 0; x < AE_WORKGROUP_SIZE; ++x) {
			const ivec2 src_cell = src_origin + ivec2(x, y);
			if (all(lessThan(src_cell, src_size))) {
				sum += aeLoadHistogramTexel(res, src_level, src_cell, hist_texel);
			}
		}
	}

	return sum;
}

void aeReducePreviousFrameHistograms(ivec2 res) {
	if (gl_LocalInvocationIndex != 0u) {
		return;
	}

	const ivec2 workgroup_id = ivec2(gl_WorkGroupID.xy);
	const int global_level = aeGlobalLevel(res);

	for (int level = 1; level <= global_level; ++level) {
		const ivec2 dst_size = aeLevelSize(res, level);
		if (!all(lessThan(workgroup_id, dst_size))) {
			continue;
		}

		for (int hist_texel = 0; hist_texel < AE_HISTOGRAM_TEXELS; ++hist_texel) {
			const vec4 bins = aeReduceHistogramTexel(res, level - 1, workgroup_id, hist_texel);
			aeStoreHistogramTexel(res, level, workgroup_id, hist_texel, bins);
		}
	}
}

// Stage 3: convert the previous global histogram into the next adapted exposure.
float aeExposureFromGlobalHistogram(ivec2 res, float previous_exposure) {
	const int global_level = aeGlobalLevel(res);
	float total = 0.;
	for (int bin = 0; bin < AE_HISTOGRAM_BINS; ++bin) {
		total += aeReadHistogramBin(res, global_level, ivec2(0), bin);
	}

	if (IS_INVALID(total) || total <= 0.) {
		return previous_exposure;
	}

	const float low_cut = total * AE_LOW_PERCENTILE;
	const float high_cut = total * AE_HIGH_PERCENTILE;
	float cumulative = 0.;
	float weighted_log_luminance = 0.;
	float kept = 0.;

	for (int bin = 0; bin < AE_HISTOGRAM_BINS; ++bin) {
		const float count = aeReadHistogramBin(res, global_level, ivec2(0), bin);
		const float bin_begin = cumulative;
		const float bin_end = cumulative + count;
		const float keep = max(0., min(bin_end, high_cut) - max(bin_begin, low_cut));
		weighted_log_luminance += keep * aeBinCenterLogLuminance(bin);
		kept += keep;
		cumulative = bin_end;
	}

	if (IS_INVALID(kept) || kept <= 0.) {
		return previous_exposure;
	}

	const float avg_log_luminance = weighted_log_luminance / kept;
	const float target_exposure = clamp(AE_TARGET_GREY / exp2(avg_log_luminance), AE_MIN_EXPOSURE, AE_MAX_EXPOSURE);
	const float adaptation_speed = target_exposure < previous_exposure ? AE_ADAPT_DARKEN_SPEED : AE_ADAPT_LIGHTEN_SPEED;
	return mix(previous_exposure, target_exposure, adaptation_speed);
}

void aeStoreNextExposure(ivec2 res) {
	if (gl_LocalInvocationIndex != 0u || any(notEqual(ivec2(gl_WorkGroupID.xy), ivec2(0)))) {
		return;
	}

	const float previous_exposure = readAutoExposure(res);
	const float exposure = aeExposureFromGlobalHistogram(res, previous_exposure);
	storeExposureTemporalData(aeExposureCoord(res), vec4(exposure, previous_exposure, 0., 0.));
}

// Public entry: update all temporal auto-exposure data for this denoiser dispatch.
void updateAutoExposureTemporalData(ivec2 res, ivec2 pix, vec3 linear_colour) {
	aeStoreCurrentWorkgroupHistogram(res, pix, linear_colour);
	aeReducePreviousFrameHistograms(res);
	aeStoreNextExposure(res);
}

#endif // AUTO_EXPOSURE_GLSL_INCLUDED
