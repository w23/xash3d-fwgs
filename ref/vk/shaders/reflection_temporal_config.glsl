#ifndef REFLECTION_TEMPORAL_CONFIG_GLSL
#define REFLECTION_TEMPORAL_CONFIG_GLSL

// Reconstruction alpha carries metric hit distance / 64 in RGBA16F.
const float REFLECTION_LENGTH_STORAGE_SCALE = 1.0 / 64.0;
const float REFLECTION_HISTORY_WEIGHT_MAX = 0.25;
const float REFLECTION_SCATTERED_HISTORY_WEIGHT_MAX = 0.85;
const float REFLECTION_ACCUMULATION_ROUGHNESS_START = 0.10;
const float REFLECTION_ACCUMULATION_ROUGHNESS_END = 0.40;
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
