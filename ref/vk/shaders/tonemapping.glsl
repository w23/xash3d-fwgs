// Blatantly copypasted from https://www.shadertoy.com/view/XsGfWV
vec3 aces_tonemap(vec3 color){
	mat3 m1 = mat3(
		0.59719, 0.07600, 0.02840,
		0.35458, 0.90834, 0.13383,
		0.04823, 0.01566, 0.83777
	);
	mat3 m2 = mat3(
		1.60475, -0.10208, -0.00327,
		-0.53108,  1.10813, -0.07276,
		-0.07367, -0.00605,  1.07602
	);
	vec3 v = m1 * color;
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return clamp(m2 * (a / b), 0.0, 1.0);
}

vec3 reinhard_tonemap(vec3 color){
	return color / (color + 1.0);
}

vec3 reinhard02_tonemap(vec3 c, vec3 Cwhite2) {
	return c * (1. + c / Cwhite2) / (1. + c);
}

// https://www.shadertoy.com/view/WdjSW3
float Tonemap_Uchimura(float x, float P, float a, float m, float l, float c, float b) {
	// Uchimura 2017, "HDR theory and practice"
	// Math: https://www.desmos.com/calculator/gslcdxvipg
	// Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
	float l0 = ((P - m) * l) / a;
	float L0 = m - m / a;
	float L1 = m + (1.0 - m) / a;
	float S0 = m + l0;
	float S1 = m + a * l0;
	float C2 = (a * P) / (P - S1);
	float CP = -C2 / P;

	float w0 = 1.0 - smoothstep(0.0, m, x);
	float w2 = step(m + l0, x);
	float w1 = 1.0 - w0 - w2;

	float T = m * pow(x / m, c) + b;
	float S = P - (P - S1) * exp(CP * (x - S0));
	float L = m + a * (x - m);

	return T * w0 + L * w1 + S * w2;
}
float Tonemap_Uchimura(float x) {
	const float P = 1.0;  // max display brightness
	const float a = 1.0;  // contrast
	const float m = 0.22; // linear section start
	const float l = 0.4;  // linear section length
	const float c = 1.33; // black
	const float b = 0.0;  // pedestal
	return Tonemap_Uchimura(x, P, a, m, l, c, b);
}
vec3 Tonemap_Uchimura(vec3 x) {
	return vec3(Tonemap_Uchimura(x.r), Tonemap_Uchimura(x.g), Tonemap_Uchimura(x.b));
}

float Tonemap_Lottes(float x) {
	// Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
	const float a = 1.6;
	const float d = 0.977;
	const float hdrMax = 8.0;
	const float midIn = 0.18;
	const float midOut = 0.267;

	// Can be precomputed
	const float b =
		(-pow(midIn, a) + pow(hdrMax, a) * midOut) /
		((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
	const float c =
		(pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
		((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

	return pow(x, a) / (pow(x, a * d) * b + c);
}

vec3 Tonemap_Lottes(vec3 x) {
	return vec3(Tonemap_Lottes(x.r), Tonemap_Lottes(x.g), Tonemap_Lottes(x.b));
}


// https://iolite-engine.com/blog_posts/minimal_agx_implementation
// 0: Default, 1: Golden, 2: Punchy
#define AGX_LOOK 0

// Mean error^2: 3.6705141e-06
vec3 agxDefaultContrastApprox(vec3 x) {
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;

	return + 15.5     * x4 * x2
	       - 40.14    * x4 * x
	       + 31.96    * x4
	       - 6.868    * x2 * x
	       + 0.4298   * x2
	       + 0.1191   * x
	       - 0.00232;
}
/*
// Mean error^2: 1.85907662e-06
vec3 agxDefaultContrastApprox(vec3 x) {
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	vec3 x6 = x4 * x2;
	
	return - 17.86     * x6 * x
	       + 78.01     * x6
	       - 126.7     * x4 * x
	       + 92.06     * x4
	       - 28.72     * x2 * x
	       + 4.361     * x2
	       - 0.1718    * x
	       + 0.002857;
}
*/

vec3 agx(vec3 val) {
	const mat3 agx_mat = mat3(
		0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992,  0.878468636469772,  0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104
	);

	const float min_ev = -12.47393f;
	const float max_ev = 4.026069f;

	// Input transform (inset)
	val = agx_mat * val;

	// Log2 space encoding
	val = clamp(log2(val), min_ev, max_ev);
	val = (val - min_ev) / (max_ev - min_ev);

	// Apply sigmoid function approximation
	val = agxDefaultContrastApprox(val);

	return val;
}

vec3 agxEotf(vec3 val) {
	const mat3 agx_mat_inv = mat3(
		1.19687900512017, -0.0528968517574562, -0.0529716355144438,
		-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
		-0.0990297440797205, -0.0989611768448433, 1.15107367264116
	);

	// Inverse input transform (outset)
	val = agx_mat_inv * val;

	// sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
	// NOTE: We're linearizing the output here. Comment/adjust when
	// *not* using a sRGB render target
	val = pow(val, vec3(2.2));

	return val;
}

vec3 agxLook(vec3 val) {
	const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
	float luma = dot(val, lw);

	// Default
	vec3 offset = vec3(0.0);
	vec3 slope = vec3(1.0);
	vec3 power = vec3(1.0);
	float sat = 1.0;

#if AGX_LOOK == 1
	// Golden
	slope = vec3(1.0, 0.9, 0.5);
	power = vec3(0.8);
	sat = 0.8;
#elif AGX_LOOK == 2
	// Punchy
	slope = vec3(1.0);
	power = vec3(1.35, 1.35, 1.35);
	sat = 1.4;
#endif
	// ASC CDL
	val = pow(val * slope + offset, power);
	return luma + sat * (val - luma);
}

