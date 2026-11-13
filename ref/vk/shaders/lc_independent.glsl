#ifndef LC_INDEPENDENT_GLSL
#define LC_INDEPENDENT_GLSL

// Match brdfComputeGltfModel: alpha = roughness^2; ggxD clamps alpha^2.
float lcReflectionProposalAlpha2(float roughness)
{
    float alpha = roughness * roughness;

    return max(alpha * alpha, 1e-5);
}

// Candidate payload stays in the caller. Counts include zero-weight proposals.
struct LCReservoir
{
    float sum;
    float count;
    float target;
};

bool lcOffer(inout LCReservoir r, float target, float inverse_pdf, float random)
{
    float mass = max(target, 0.0) * inverse_pdf;
    r.count += 1.0;
    r.sum += mass;

    bool accept = mass > 0.0 && random * r.sum < mass;
    if (accept)
        r.target = target;

    return accept;
}

float lcNormalization(LCReservoir r)
{
    return r.target > 0.0 ? r.sum / (r.count * r.target) : 0.0;
}

bool lcPathHistoryValid(vec4 h)
{
    return !any(isnan(h)) && !any(isinf(h)) && h.x > 0.0 && h.y > 0.0 && h.z >= 1.0;
}

bool lcMirrorPath(float roughness, float normal_dot)
{
    return roughness < 0.004 && normal_dot >= 0.98;
}

// Symmetric confidence around an unchanged replay value (ratio == 1).
float lcRatioConfidence(float ratio, float limit)
{
    return clamp(ratio < 1.0 ? (ratio - 1.0 / limit) / (1.0 - 1.0 / limit) : (limit - ratio) / (limit - 1.0), 0.0, 1.0);
}

// Replay replaces the selected contribution without increasing the sample count.
LCReservoir lcReplayReservoir(vec4 previous, float target)
{
    return LCReservoir(max(0.0, previous.y - previous.x) + target, previous.z, target);
}

vec4 lcEncodePath(LCReservoir r, float cap)
{
    float count = min(r.count, cap);

    return vec4(r.target, r.sum * (count / r.count), count, lcNormalization(r));
}

float lcPathTarget(vec3 color)
{
    return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-6);
}

float lcWeight(vec3 c)
{
    return max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0);
}

// Unit-albedo outgoing diffuse radiance; geometry normal only, no view/material.
vec3 lcDiffuse(vec3 radiance, vec3 geometry_normal, vec3 direction)
{
    return radiance * (max(dot(geometry_normal, direction), 0.0) / 3.14159265);
}

#endif
