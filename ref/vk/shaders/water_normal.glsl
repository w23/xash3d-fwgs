#ifndef WATER_NORMAL_GLSL
#define WATER_NORMAL_GLSL

// Mark Finch, GPU Gems 1, ch. 1, sections 1.2.2 and 1.2.4 (eqs. 8a/8b):
// https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models
// Directional sharpened sine waves: broad troughs and narrower crests. Evaluate
// the analytic height gradient, not finite differences of an advected noise field.
// Components: direction angle (radians), wavelength, amplitude (HL units), phase.
const vec4 WATER_WAVES[12] = vec4[12](
    vec4( 0.35, 192.0, 1.400, 0.0),
    vec4(-0.48, 137.0, 0.950, 2.1),
    vec4( 0.89,  93.0, 0.620, 0.9),
    vec4(-0.17,  64.0, 0.400, 4.8),
    vec4( 1.23,  43.0, 0.250, 1.7),
    vec4(-0.76,  29.0, 0.150, 3.4),
    vec4( 0.54,  19.0, 0.085, 5.2),
    vec4( 1.67,  12.7, 0.048, 2.8),
    vec4(-1.13,   8.5, 0.026, 0.4),
    vec4( 0.13,   5.7, 0.014, 4.1),
    vec4( 2.21,   3.8, 0.007, 1.2),
    vec4(-0.37,   2.6, 0.003, 5.7)
);
const float WATER_GRAVITY = 980.665; // 9.80665 m/s^2, with one world unit ~ one centimetre
const float WATER_TIME_SCALE = 0.65;

vec2 waterSlope(vec2 p, float time, float footprint, vec2 wave_settings) {
    vec2 slope = vec2(0);
    for (int i = 0; i < 12; ++i) {
        vec4 wave = WATER_WAVES[i];
        vec2 d = vec2(cos(wave.x), sin(wave.x));
        // Scale wavelength and height together, keeping slope strength independent.
        wave.yz *= wave_settings.x;
        float k = 6.28318530718 / wave.y;
        float omega = sqrt(WATER_GRAVITY * k);
        float phase = k * dot(d,p) - omega * time * WATER_TIME_SCALE + wave.w;
        // h = A * (2 * ((1 + sin(phase))/2)^2 - 1).
        // dh/dp = A*k*d * (cos(phase) + 0.5*sin(2*phase)).
        // Gaussian footprint filtering suppresses unresolved fundamentals AND
        // the sharper crest's second harmonic independently.
        float f = k * max(footprint,0.0);
        float fundamental = exp(-0.5 * f*f);
        float harmonic = exp(-2.0 * f*f);
        slope += wave.z * k * d * (cos(phase) * fundamental + 0.5 * sin(2.0*phase) * harmonic);
    }
    return slope * wave_settings.y;
}

vec3 waterNormal(vec3 p, vec3 geometry_normal, vec3 shading_normal, float time, float footprint, vec2 wave_settings) {
    if (wave_settings.y <= 0.0)
        return shading_normal;
    // Use the geometric plane: a texture normal must not warp the wave phase
    // coordinates or make the entire wave pattern slide as the camera moves.
    vec3 g = geometry_normal;
    vec3 axis = abs(g.z) < 0.9 ? vec3(0,0,1) : vec3(0,1,0);
    vec3 t = normalize(cross(axis,g)), b = cross(g,t);
    vec2 slope = waterSlope(vec2(dot(p,t),dot(p,b)), time, footprint, wave_settings);
    // Add slopes of the existing material normal map in this same fixed frame.
    vec2 material_slope = -vec2(dot(shading_normal,t),dot(shading_normal,b))
        / max(dot(shading_normal,g),0.1);
    slope += material_slope;
    return normalize(g - t*slope.x - b*slope.y);
}
#endif
