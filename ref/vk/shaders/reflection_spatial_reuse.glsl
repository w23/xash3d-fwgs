#define GLSL
#include "ray_interop.h"
#include "utils.glsl"
#include "noise.glsl"
#include "color_spaces.glsl"
#include "brdf.glsl"

layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform UBO { UniformBuffer ubo; } ubo;
layout(set=0,binding=1,rgba16f) uniform readonly image2D firefly_filtered_specular;
layout(set=0,binding=2,rgba32f) uniform readonly image2D reflection_reuse_base;
layout(set=0,binding=3,rgba32f) uniform readonly image2D reflection_hit_pos;
layout(set=0,binding=4,rgba32f) uniform readonly image2D position_t;
layout(set=0,binding=5,rgba16f) uniform readonly image2D normals_gs;
layout(set=0,binding=6,rgba8) uniform readonly image2D material_rmxx;
layout(set=0,binding=7,rgba8) uniform readonly image2D base_color_a;
layout(set=0,binding=8,rgba16f) uniform writeonly image2D out_reflection_reused_specular;
#if REFLECTION_REUSE_FIREFLIES
layout(set=0,binding=9,rgba32f) uniform readonly image2D reflection_fireflies;
#endif

#if RECON_USE_CENTER_BRDF && RECON_USE_PHONG_SPOT
layout(set=0,binding=10,rgba32f) uniform readonly image2D reflection_direction_pdf;

#if REFLECTION_REUSE_FIREFLIES
layout(set=0,binding=11,rgba16f) uniform readonly image2D indirect_specular;
#endif

vec3 spotFresnel(vec3 V, vec3 L, vec3 F0) {
    vec3 H = normalize(V+L);
    return mix(F0,vec3(1.0),pow(1.0-clamp(dot(V,H),0.0,1.0),5.0));
}

// Approximate the convolution of the reflection lobe and the angular footprint
// by adding their squared angular widths. Normalize over solid angle.
float spotPhong(vec3 N, vec3 V, vec3 L, float alpha, float angularRadius) {
    float width = 2.0 * alpha;
    float widthSquared = width*width + angularRadius*angularRadius;
    float exponent = min(2.0 / max(widthSquared, 1e-5), 2048.0);
    float alignment = max(dot(reflect(-V,N),L), 0.0);
    return (exponent+1.0) * (0.5*kOneOverPi) * pow(alignment, exponent);
}
#endif

const vec2 poisson[8] = vec2[8](
    vec2(-0.47,-0.44), vec2(-0.91,0.30), vec2(-0.35,0.40), vec2(0.10,0.64),
    vec2(0.57,0.35), vec2(0.29,-0.11), vec2(0.78,-0.42), vec2(0.16,-0.82));

float viewDepth(vec3 p) {
    return abs((ubo.ubo.view * vec4(p,1.0)).z);
}

bool inFrame(ivec2 p) {
    return all(greaterThanEqual(p,ivec2(0))) && all(lessThan(p,ubo.ubo.res));
}

vec3 reconstruct(ivec2 p, vec3 center) {
    vec4 surface = imageLoad(position_t,p);
    vec2 rm = imageLoad(material_rmxx,p).rg;
    vec4 centerHit = imageLoad(reflection_hit_pos,p);
    if (surface.w <= 0.0 || rm.x < 0.004 || centerHit.w <= 0.0 ||
        (ubo.ubo.renderer_flags & RENDERER_FLAG_REFLECTION_RECONSTRUCTION) == 0u)
        return center;

    vec4 normals = imageLoad(normals_gs,p);
    vec3 G = normalDecode(normals.xy);
    vec3 N = normalDecode(normals.zw);
    vec3 V = normalize(ubo.ubo.inv_view[3].xyz - surface.xyz);
    bool separated = (ubo.ubo.renderer_flags & RENDERER_FLAG_SEPARATED_REFLECTION) != 0u;
    MaterialProperties material = MaterialProperties(separated ? vec3(1) :
        SRGBtoLINEAR(imageLoad(base_color_a,p).rgb), separated ? 1.0 : rm.y, rm.x);
    float depth = max(viewDepth(surface.xyz),1e-5);
    float alpha = rm.x * rm.x;
    float pixelsPerWorld = abs(ubo.ubo.proj[1][1]) * float(ubo.ubo.res.y) / (2.0 * depth);
    float contactBlend = 1.0;
#if RECON_USE_CENTER_BRDF && RECON_USE_CONTACT_FALLBACK
    float nearest = 1e30, second = 1e30;
    vec3 nearestHit = vec3(0.0), secondHit = vec3(0.0);
    ivec2 origin = p & ivec2(~1);
    for (int i = 0; i < 4; ++i) {
        ivec2 q = origin + ivec2(i & 1, i >> 1);
        if (!inFrame(q)) continue;
        vec4 primary = imageLoad(position_t,q);
        vec4 hit = imageLoad(reflection_hit_pos,q);
        vec3 normal = normalDecode(imageLoad(normals_gs,q).xy);
        if (primary.w <= 0.0 || hit.w <= 0.0 || dot(G,normal) < 0.9 ||
            abs(viewDepth(primary.xyz)/depth - 1.0) > RECON_DEPTH_FADE_END) continue;
        if (hit.w < nearest) {
            second = nearest;
            secondHit = nearestHit;
            nearest = hit.w;
            nearestHit = hit.xyz;
        } else if (hit.w < second) {
            second = hit.w;
            secondHit = hit.xyz;
        }
    }
    if (second < 1e30 && length(secondHit-nearestHit)*pixelsPerWorld <= 2.0) {
        float contactRadius = 2.0 * alpha * second * pixelsPerWorld;
        contactBlend = smoothstep(1.0,2.0,contactRadius);
        if (contactBlend <= 0.0) return center;
    }
#endif

    // Keep a small gather at contacts, fading it in for near-mirror materials.
    float minRadius = RECON_MIN_RADIUS * smoothstep(0.004, 0.15, rm.x);
    float radius = clamp(2.0 * alpha * centerHit.w * pixelsPerWorld, minRadius, RECON_MAX_RADIUS);
#if RECON_USE_CENTER_BRDF && REFLECTION_REUSE_FIREFLIES
    bool gatherFireflies = (ubo.ubo.renderer_flags & RENDERER_FLAG_FIREFLY_REJECTION) != 0u;
#else
    const bool gatherFireflies = false;
#endif
    if (radius < 1.0 && !gatherFireflies) return center;

    // Rotate the gather between frames while keeping adjacent texels decorrelated.
    float angle = 2.0*kPi*uintToFloat01(xxhash32(uvec4(uvec2(p),ubo.ubo.random_seed,0x706f6973u)));
    float c = cos(angle), s = sin(angle);
    mat2 rotation = mat2(c,s,-s,c);

#if RECON_USE_CENTER_BRDF && RECON_USE_HIT_BOUNDS
    vec3 lower = centerHit.xyz, upper = centerHit.xyz;
    int hitCount = 1;
    for (int y=0; y<2; ++y) {
        for (int x=0; x<2; ++x) {
            ivec2 q = p + ivec2(x,y)*ivec2((p.x & 1)==0 ? 1 : -1, (p.y & 1)==0 ? 1 : -1);
            if (all(equal(p,q)) || !inFrame(q)) continue;
            vec4 hit = imageLoad(reflection_hit_pos,q);
            if (hit.w <= 0.0) continue;
            lower = min(lower,hit.xyz);
            upper = max(upper,hit.xyz);
            ++hitCount;
        }
    }
#endif

    vec3 result = vec3(0.0);
    for (int gather=0; gather<(gatherFireflies ? 2 : 1); ++gather) {
        bool excess = gather == 1;
        float gatherRadius = excess ? RECON_FIREFLY_RADIUS : radius;
        if (gatherRadius < 1.0) {
            result += center;
            continue;
        }
        // Rotate the sparse residual pattern independently from the base pattern.
        float residualAngle = angle + 2.39996323;
        float rc = cos(residualAngle), rs = sin(residualAngle);
        mat2 gatherRotation = excess ? mat2(rc,rs,-rs,rc) : rotation;
        vec3 sum = excess ? vec3(0.0) : center;
        float weightSum = excess ? 0.0 : 1.0;
#if RECON_USE_CENTER_BRDF && RECON_USE_HIT_DEDUP
        vec3 acceptedHits[8];
        int acceptedCount = 0;
#endif
        ivec2 visited[8];
        for (int i=0; i<8; ++i) {
            ivec2 q = p + ivec2(round(gatherRotation * poisson[i] * gatherRadius));
            visited[i] = q;
            if (all(equal(p,q)) || !inFrame(q)) continue;
            bool duplicate = false;
            for (int j=0; j<i; ++j) duplicate = duplicate || all(equal(q,visited[j]));
            if (duplicate) continue;
#if RECON_USE_CENTER_BRDF
    #if REFLECTION_REUSE_FIREFLIES
            vec4 source = excess ? imageLoad(reflection_fireflies,q) : imageLoad(reflection_reuse_base,q);
#else
            vec4 source = imageLoad(reflection_reuse_base,q);
#endif
            vec4 hit = imageLoad(reflection_hit_pos,q);
            vec4 neighbor = imageLoad(position_t,q);
            if (source.w <= 0.0 || hit.w <= 0.0 || neighbor.w <= 0.0) continue;

            float weight = 1.0;
    #if RECON_USE_DEPTH_WEIGHT
            float relativeError = abs(viewDepth(neighbor.xyz)/depth - 1.0);
            weight *= 1.0-smoothstep(RECON_DEPTH_FADE_START,RECON_DEPTH_FADE_END,relativeError);
    #endif
            vec4 neighborNormals = imageLoad(normals_gs,q);
    #if RECON_USE_NORMAL_WEIGHT
            float normalWidth = mix(0.005,0.25,alpha);
            weight *= 1.0-smoothstep(0.0,normalWidth,1.0-dot(N,normalDecode(neighborNormals.zw)));
    #endif
    #if RECON_USE_ROUGHNESS_WEIGHT
            float roughness = imageLoad(material_rmxx,q).x;
            float alphaError = abs(roughness*roughness-alpha)/max(alpha,0.02);
            weight *= 1.0-smoothstep(0.1,0.7,alphaError);
    #endif
    #if RECON_USE_MUTUAL_HORIZONS
            vec3 delta = neighbor.xyz-surface.xyz;
            vec3 D = delta/max(length(delta),1e-6);
            weight *= smoothstep(-0.15,-0.02,dot(G,D));
            weight *= smoothstep(-0.15,-0.02,dot(normalDecode(neighborNormals.xy),-D));
    #endif
    #if RECON_USE_HIT_BOUNDS
            if (hitCount >= 3) {
                float outside = length(hit.xyz-clamp(hit.xyz,lower,upper));
                float margin = max(0.02*centerHit.w,0.5*length(upper-lower));
                weight *= 1.0-smoothstep(margin,2.0*margin,outside);
            }
    #endif
            if (weight <= 0.0) continue;

            vec3 toHit = hit.xyz-surface.xyz;
            float distanceToHit = length(toHit);
            if (distanceToHit <= 1e-6) continue;
            vec3 L = toHit/distanceToHit;
    #if RECON_USE_HIT_HORIZON
            if (dot(G,L) <= 0.0) continue;
    #endif
            if (dot(N,L) <= 0.0) continue;
#if RECON_USE_PHONG_SPOT
            vec4 sourceRay = imageLoad(reflection_direction_pdf,q);
            if (sourceRay.w <= 0.0 || dot(sourceRay.xyz,sourceRay.xyz) <= 1e-12) continue;
            vec2 sourceRM = imageLoad(material_rmxx,q).rg;
            // The same world-space spot subtends different angles at the two origins.
            float sourceAlpha = max(sourceRM.x*sourceRM.x, sqrt(1e-5));
            float spotRadius = RECON_SPOT_RADIUS_SCALE * sourceAlpha * hit.w;
            vec3 sourceN = normalDecode(neighborNormals.zw);
            vec3 sourceV = normalize(ubo.ubo.inv_view[3].xyz-neighbor.xyz);
            vec3 sourceL = normalize(sourceRay.xyz);
            vec3 sourceF0 = separated ? vec3(1.0) :
                mix(vec3(0.04),SRGBtoLINEAR(imageLoad(base_color_a,q).rgb),sourceRM.y);
            vec3 centerF0 = mix(vec3(0.04),material.base_color,material.metalness);
            vec3 sourceResponse = spotFresnel(sourceV,sourceL,sourceF0) *
                spotPhong(sourceN,sourceV,sourceL,sourceAlpha,atan(spotRadius/hit.w));
            vec3 centerResponse = spotFresnel(V,L,centerF0) *
                spotPhong(N,V,L,alpha,atan(spotRadius/distanceToHit));

            // Reweight the resolved RIS color with bounded, symmetric contrast.
            // Equal responses keep the original energy; neither tail can cause a singularity.
            vec3 gain = 2.0*centerResponse / max(centerResponse+sourceResponse,vec3(1e-6));
            vec3 color = imageLoad(firefly_filtered_specular,q).rgb;
#if REFLECTION_REUSE_FIREFLIES
            if (excess) color = max(imageLoad(indirect_specular,q).rgb-color,vec3(0.0));
#endif
            vec3 contribution = color*gain;
#else
            vec3 diffuse, specular;
            evalSplitBRDF(N,L,V,material,diffuse,specular);
            vec3 contribution = source.rgb*specular;
#endif
            if (any(isnan(contribution)) || any(isinf(contribution))) continue;
#if RECON_USE_HIT_DEDUP
            bool repeatedHit = !excess && length(hit.xyz-centerHit.xyz)*pixelsPerWorld <= 1.0;
            for (int j = 0; j < acceptedCount; ++j)
                repeatedHit = repeatedHit || length(hit.xyz-acceptedHits[j])*pixelsPerWorld <= 1.0;
            if (repeatedHit) continue;
            acceptedHits[acceptedCount++] = hit.xyz;
#endif
            sum += contribution*weight;
            weightSum += weight;
#else
            if (imageLoad(position_t,q).w <= 0.0) continue;
            vec3 color = imageLoad(firefly_filtered_specular,q).rgb;
            if (any(isnan(color)) || any(isinf(color))) continue;
            sum += color;
            weightSum += 1.0;
#endif
        }
        if (weightSum > 0.0) result += sum/weightSum;
    }
    return mix(center,result,contactBlend);
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (!inFrame(p)) return;
    vec4 signal = imageLoad(firefly_filtered_specular,p);
    signal.rgb = clamp(reconstruct(p,signal.rgb),vec3(0.0),vec3(65504.0));
    imageStore(out_reflection_reused_specular,p,signal);
}
