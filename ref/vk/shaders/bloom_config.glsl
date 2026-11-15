#ifndef BLOOM_CONFIG_GLSL_INCLUDED
#define BLOOM_CONFIG_GLSL_INCLUDED

// Display-linear units, after exposure. The soft knee is a fraction of threshold.
#ifndef BLOOM_THRESHOLD
#define BLOOM_THRESHOLD 1.0
#endif
#ifndef BLOOM_SOFT_KNEE
#define BLOOM_SOFT_KNEE 0.5
#endif
#ifndef BLOOM_INTENSITY
#define BLOOM_INTENSITY 0.15
#endif
#ifndef BLOOM_EMISSION_WEIGHT
#define BLOOM_EMISSION_WEIGHT 1.0
#endif
#ifndef BLOOM_LIGHTING_WEIGHT
#define BLOOM_LIGHTING_WEIGHT 0.5
#endif
#ifndef BLOOM_SKY_WEIGHT
#define BLOOM_SKY_WEIGHT 0.5
#endif
// Active levels (1..5); tiny render targets automatically use fewer levels.
#ifndef BLOOM_LEVEL_COUNT
#define BLOOM_LEVEL_COUNT 5
#endif
// Tent radius in texels of the smaller level.
#ifndef BLOOM_RADIUS
#define BLOOM_RADIUS 1.0
#endif
// Normalized during reconstruction, so level count does not change total gain.
#ifndef BLOOM_LEVEL_WEIGHTS
#define BLOOM_LEVEL_WEIGHTS vec4(1.0, 1.0, 1.0, 1.0)
#endif
#ifndef BLOOM_LAST_LEVEL_WEIGHT
#define BLOOM_LAST_LEVEL_WEIGHT 1.0
#endif

#if BLOOM_LEVEL_COUNT < 1 || BLOOM_LEVEL_COUNT > 5
#error BLOOM_LEVEL_COUNT must be in [1, 5]
#endif

vec3 bloomBrightPass(vec3 colour) {
    colour = max(colour, vec3(0.0));
    float brightness = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(BLOOM_THRESHOLD * BLOOM_SOFT_KNEE, 1e-5);
    float soft = clamp(brightness - BLOOM_THRESHOLD + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    return colour * (max(soft, brightness - BLOOM_THRESHOLD) / max(brightness, 1e-5));
}

bool bloomDisplay(uint mode) {
    return mode == DEBUG_DISPLAY_DISABLED || mode == DEBUG_DISPLAY_BLOOM ||
        mode == DEBUG_DISPLAY_BLOOM_SOURCE;
}
#endif
