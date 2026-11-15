#ifndef EXPOSURE_READ_GLSL_INCLUDED
#define EXPOSURE_READ_GLSL_INCLUDED

ivec2 aeExposureCoord(ivec2 res) {
	return max(res - ivec2(1), ivec2(0));
}

// Both shadow preparation and composition read the same previous-frame value.
float readAutoExposure(ivec2 res) {
	float exposure = loadExposureTemporalData(aeExposureCoord(res)).r;
	if (isnan(exposure) || isinf(exposure) || exposure <= 0.) {
		exposure = 1.;
	}
	return clamp(exposure, ubo.ubo.auto_exposure_min, ubo.ubo.auto_exposure_max);
}

#endif
