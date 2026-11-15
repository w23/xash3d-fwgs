#ifndef REFLECTION_TEMPORAL_CONFIG_GLSL
#define REFLECTION_TEMPORAL_CONFIG_GLSL

// Reconstruction alpha carries metric hit distance / 64 in RGBA16F.
const float REFLECTION_LENGTH_STORAGE_SCALE = 1.0 / 64.0;
const float REFLECTION_HISTORY_WEIGHT_MAX = 0.25;
const float REFLECTION_SCATTERED_HISTORY_WEIGHT_MAX = 0.85;
const float REFLECTION_ACCUMULATION_ROUGHNESS_START = 0.10;
const float REFLECTION_ACCUMULATION_ROUGHNESS_END = 0.40;
// Use surface motion once the reflection is predominantly scattered. Keep the
// same roughness/length-spread classification as the accumulation filter.
const float REFLECTION_SURFACE_REPROJECTION_ACCUMULATION_MIN = 0.5;
// History alpha records the method actually used to accumulate this texel.
// Values also version the raw RGB + metric hit-length statistics layout.
// Legacy alpha == 1 and the previous 2/3/4 layout have no variance history.
const float REFLECTION_HISTORY_RAW = 5.0;
const float REFLECTION_HISTORY_SURFACE = 6.0;
const float REFLECTION_HISTORY_PARALLAX = 7.0;
const float REFLECTION_VARIANCE_UPDATE_WEIGHT = 0.2;
const float REFLECTION_TEMPORAL_NOISE_RELATIVE_START = 0.03;
const float REFLECTION_TEMPORAL_NOISE_RELATIVE_END = 0.20;
// Coefficient of variation of current 3x3 reconstructed hit lengths. This
// also enables stronger accumulation on smooth materials with dispersed hits.
const float REFLECTION_LENGTH_SPREAD_START = 0.10;
const float REFLECTION_LENGTH_SPREAD_END = 0.40;
const float REFLECTION_LENGTH_SPREAD_MEAN_FLOOR = 1.0;
const float REFLECTION_CLIP_SIGMA = 1.25;
const float REFLECTION_SCATTERED_CLIP_SIGMA = 2.0;
const float REFLECTION_SIGNAL_RELATIVE_FLOOR = 0.03;
const float REFLECTION_SIGNAL_ABSOLUTE_FLOOR = 0.002;
const float REFLECTION_CORRECTION_RELATIVE_MAX = 0.03;
const float REFLECTION_SCATTERED_CORRECTION_RELATIVE_MAX = 0.20;
const float REFLECTION_LENGTH_RELATIVE_TOLERANCE = 0.10;
const float REFLECTION_LENGTH_ABSOLUTE_TOLERANCE = 0.5;
const float REFLECTION_LENGTH_SIGMA_SCALE = 2.0;
// On mirrors, a confident hit-distance mismatch rejects history completely.
const float REFLECTION_LENGTH_GATE_STRENGTH = 1.0;
const float REFLECTION_LENGTH_ROUGHNESS_FADE_START = 0.05;
const float REFLECTION_LENGTH_ROUGHNESS_FADE_END = 0.10;

bool reflectionLengthValid(float value) {
    return value > 0.0 && !isnan(value) && !isinf(value);
}

float encodeReflectionLength(float value) {
    return reflectionLengthValid(value)
        ? min(value * REFLECTION_LENGTH_STORAGE_SCALE, 65504.0) : 0.0;
}

float decodeReflectionLength(float value) {
    return reflectionLengthValid(value) ? value / REFLECTION_LENGTH_STORAGE_SCALE : 0.0;
}

#endif
