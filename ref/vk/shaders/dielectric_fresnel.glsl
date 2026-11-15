#ifndef DIELECTRIC_FRESNEL_GLSL
#define DIELECTRIC_FRESNEL_GLSL

// Exact unpolarized dielectric Fresnel reflectance. eta = incident IOR / transmitted IOR.
// The same ratio is passed to refract(), including when leaving the material.
float dielectricFresnel(float cos_i, float eta) {
    // Identical media have no interface, including at grazing incidence.
    if (eta == 1.0f) return 0.0f;
    cos_i = clamp(cos_i, 0.0f, 1.0f);
    float sin2_t = eta * eta * max(0.0f, 1.0f - cos_i * cos_i);
    if (sin2_t >= 1.0f) return 1.0f; // Total internal reflection (Snell's law).
    float cos_t = sqrt(1.0f - sin2_t);
    float rs = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
    float rp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
    return 0.5f * (rs * rs + rp * rp);
}

#endif
