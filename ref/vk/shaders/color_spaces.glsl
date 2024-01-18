// LDR: VK_FORMAT_B8G8R8A8_UNORM(44) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
#define LDR_B8G8R8A8_UNORM_SRGB_NONLINEAR 0

// LDR: VK_FORMAT_B8G8R8A8_SRGB(50) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
// LDR: VK_FORMAT_R8G8B8A8_UNORM(37) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
// LDR: VK_FORMAT_R8G8B8A8_SRGB(43) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)
// LDR: VK_FORMAT_A2B10G10R10_UNORM_PACK32(64) VK_COLOR_SPACE_SRGB_NONLINEAR_KHR(0)

// HDR: VK_FORMAT_A2B10G10R10_UNORM_PACK32(64) VK_COLOR_SPACE_HDR10_ST2084_EXT(1000104008)
#define HDR_A2B10G10R10_UNORM_PACK32_HDR10_ST2084 1

// HDR: VK_FORMAT_R16G16B16A16_SFLOAT(97) VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT(1000104002)
#define HDR_R16G16B16A16_SFLOAT_EXTENDED_SRGB_LINEAR 2



#ifndef RT_COLOR_SPACES_GLSL_INCLUDED
#define RT_COLOR_SPACES_GLSL_INCLUDED
#ifdef SRGB_FAST_APPROXIMATION
#define LINEARtoSRGB OECF_sRGBFast
#define SRGBtoLINEAR sRGB_OECFFast
#else
#define LINEARtoSRGB OECF_sRGB
#define SRGBtoLINEAR sRGB_OECF
#endif

// based on https://github.com/OGRECave/ogre/blob/f49bc9be79f6711a88f01892711120da717f6148/Samples/Media/PBR/filament/pbr_filament.frag.glsl#L108-L124
float sRGB_OECF(const float sRGB) {
	// IEC 61966-2-1:1999
	float linearLow = sRGB / 12.92;
	float linearHigh = pow((sRGB + 0.055) / 1.055, 2.4);
	return sRGB <= 0.04045 ? linearLow : linearHigh;
}
/**
 * Reverse opto-electronic conversion function to the one that filament
 * provides. Filament version has LDR RGB linear color -> LDR RGB non-linear
 * color in sRGB space. This function will thus provide LDR RGB non-linear
 * color in sRGB space -> LDR RGB linear color conversion.
 */
vec3 sRGB_OECF(const vec3 sRGB) {
	return vec3(sRGB_OECF(sRGB.r), sRGB_OECF(sRGB.g), sRGB_OECF(sRGB.b));
}
vec4 sRGB_OECF(const vec4 sRGB) {
	return vec4(sRGB_OECF(sRGB.r), sRGB_OECF(sRGB.g), sRGB_OECF(sRGB.b), sRGB.w);
}
vec3 sRGB_OECFFast(const vec3 sRGB) {
	return pow(sRGB, vec3(2.2));
}
float sRGB_OECFFast(const float sRGB) {
	return pow(sRGB, 2.2);
}
vec4 sRGB_OECFFast(const vec4 sRGB) {
	return vec4(pow(sRGB.rgb, vec3(2.2)), sRGB.w);
}

// based on https://github.com/abhirocks1211/filament/blob/3e97ac5268a47d5625c7d166eb7dda0bbba14a4d/shaders/src/conversion_functions.fs#L20-L55
//------------------------------------------------------------------------------
// Opto-electronic conversion functions (linear to non-linear)
//------------------------------------------------------------------------------

float OECF_sRGB(const float linear) {
	// IEC 61966-2-1:1999
	float sRGBLow  = linear * 12.92;
	float sRGBHigh = (pow(linear, 1.0 / 2.4) * 1.055) - 0.055;
	return linear <= 0.0031308 ? sRGBLow : sRGBHigh;
}

vec3 OECF_sRGB(const vec3 linear) {
	return vec3(OECF_sRGB(linear.r), OECF_sRGB(linear.g), OECF_sRGB(linear.b));
}
vec4 OECF_sRGB(const vec4 linear) {
	return vec4(OECF_sRGB(linear.r), OECF_sRGB(linear.g), OECF_sRGB(linear.b), linear.w);
}
vec3 OECF_sRGBFast(const vec3 linear) {
	return pow(linear, vec3(1.0 / 2.2));
}
vec4 OECF_sRGBFast(const vec4 linear) {
	return vec4(pow(linear.rgb, vec3(1.0 / 2.2)), linear.w);
}

#endif //ifndef RT_COLOR_SPACES_GLSL_INCLUDED


// HDR stuff (High Dynamic Range Color Grading and Display in Frostbite)
// ported to GLSL, something maybe incorrect

// RGB with sRGB/Rec.709 primaries to CIE XYZ
vec3 RGBToXYZ(vec3 c) {
	mat3 mat = mat3(
		0.4124564, 0.2126729, 0.0193339,
		0.3575761, 0.7151522, 0.1191920,
		0.1804375, 0.0721750, 0.9503041
	);
	return mat * c;
}
vec3 XYZToRGB(vec3 c) {
	mat3 mat = mat3(
		3.24045483602140870, -0.96926638987565370, 0.05564341960421366,
		-1.53713885010257510, 1.87601092884249100, -0.20402585426769815,
		-0.49853154686848090, 0.04155608234667354, 1.05722516245792870
	);
	return mat * c;
}

// Converts XYZ tristimulus values into cone responses for the three types of cones in the human visual system, matching long, medium, and short wavelengths.
// Note that there are many LMS color spaces; this one follows the ICtCp color space specification.
vec3 XYZToLMS(vec3 c) {
	mat3 mat = mat3(
		0.3592, -0.1922, 0.0070,
		0.6976, 1.1004, 0.0749,
		-0.0358, 0.0755, 0.8434
	);
	return mat * c;
}
vec3 LMSToXYZ(vec3 c) {
	mat3 mat = mat3(
		2.07018005669561320, 0.36498825003265756, -0.04959554223893212,
		-1.32645687610302100, 0.68046736285223520, -0.04942116118675749,
		0.206616006847855170, -0.045421753075853236, 1.187995941732803400
	);
	return mat * c;
}

const float PQ_constant_N = (2610.0 / 4096.0 / 4.0);
const float PQ_constant_M = (2523.0 / 4096.0 * 128.0);
const float PQ_constant_C1 = (3424.0 / 4096.0);
const float PQ_constant_C2 = (2413.0 / 4096.0 * 32.0);
const float PQ_constant_C3 = (2392.0 / 4096.0 * 32.0);
// PQ (Perceptual Quantiser; ST.2084) encode/decode used for HDR TV and grading
vec3 linearToPQ(vec3 linearCol, const float maxPqValue) {
	linearCol /= maxPqValue;
	vec3 colToPow = pow(linearCol, vec3(PQ_constant_N));
	vec3 numerator = PQ_constant_C1 + PQ_constant_C2*colToPow;
	vec3 denominator = 1.0 + PQ_constant_C3*colToPow;
	vec3 pq = pow(numerator / denominator, vec3(PQ_constant_M));
	return pq;
}
vec3 PQtoLinear(vec3 linearCol, const float maxPqValue) {
	vec3 colToPow = pow(linearCol, 1.0 / vec3(PQ_constant_M));
	vec3 numerator = max(colToPow - PQ_constant_C1, 0.0);
	vec3 denominator = PQ_constant_C2 - (PQ_constant_C3 * colToPow);
	vec3 linearColor = pow(numerator / denominator, vec3(1.0 / PQ_constant_N));
	linearColor *= maxPqValue;
	return linearColor;
}
vec3 linearToPQ(const vec3 x) {
	return linearToPQ(x, 100);
}

// Aplies exponential ("Photographic") luma compression
float rangeCompress(float x) {
	return 1.0 - exp(-x);
}
float rangeCompress(float val, float threshold) {
	float v1 = val;
	float v2 = threshold + (1 - threshold) * rangeCompress((val - threshold) / (1 - threshold));
	return val < threshold ? v1 : v2;
}
vec3 rangeCompress(vec3 val, float threshold) {
	return vec3(
		rangeCompress(val.x, threshold),
		rangeCompress(val.y, threshold),
		rangeCompress(val.z, threshold)
	);
}

// RGB with sRGB/Rec.709 primaries to ICtCp
vec3 RGBToICtCp(vec3 col) {
	col = RGBToXYZ(col);
	col = XYZToLMS(col);
	// 1.0f = 100 nits, 100.0f = 10k nits
	col = linearToPQ(max(0.0.xxx, col), 100.0);
	// Convert PQ-LMS into ICtCp. Note that the "S" channel is not used,
	// but overlap between the cone responses for long, medium, and short wavelengths
	// ensures that the corresponding part of the spectrum contributes to luminance.
	mat3 mat = mat3(
		0.5000, 1.6137, 4.3780,
		0.5000, -3.3234, -4.2455,
		0.0000, 1.7097, -0.1325
	);
	return mat * col;
}
vec3 ICtCpToRGB(vec3 col) {
	mat3 mat = mat3(
		1.0, 1.0, 1.0,
		0.00860514569398152, -0.00860514569398152, 0.56004885956263900,
		0.11103560447547328, -0.11103560447547328, -0.32063747023212210
	);
	col = mat * col;
	// 1.0f = 100 nits, 100.0f = 10k nits
	col = PQtoLinear(col, 100.0);
	col = LMSToXYZ(col);
	return XYZToRGB(col);
}

vec3 applyHuePreservingShoulder(vec3 col) {
	vec3 ictcp = RGBToICtCp(col);
	// Hue-preserving range compression requires desaturation in order to achieve a natural look. We adaptively saturate the input based on its luminance.
	float saturationAmount = pow(smoothstep(1.0, 0.3, ictcp.x), 1.3);
	col = ICtCpToRGB(ictcp * vec3(1, saturationAmount.xx));

	// TODO: how to do it right for HDR?
	/*
	// Only compress luminance starting at a certain point. Dimmer inputs are passed through without modification.
	float linearSegmentEnd = 0.25;
	// Hue-preserving mapping
	float maxCol = max(col.x, max(col.y, col.z));
	float mappedMax = rangeCompress(maxCol, linearSegmentEnd);
	vec3 compressedHuePreserving = col * mappedMax / maxCol;
	// Non-hue preserving mapping
	vec3 perChannelCompressed = rangeCompress(col, linearSegmentEnd);
	// Combine hue-preserving and non-hue-preserving colors. Absolute hue preservation looks unnatural, as bright colors *appear* to have been hue shifted.
	// Actually doing some amount of hue shifting looks more pleasing
	col = mix(perChannelCompressed, compressedHuePreserving, 0.6);
	*/

	vec3 ictcpMapped = RGBToICtCp(col);
	// Smoothly ramp off saturation as brightness increases, but keep some even for very bright input
	float postCompressionSaturationBoost = 0.3 * smoothstep(1.0, 0.5, ictcp.x);
	// Re-introduce some hue from the pre-compression color. Something similar could be accomplished by delaying the luma-dependent desaturation before range compression.
	// Doing it here however does a better job of preserving perceptual luminance of highly saturated colors. Because in the hue-preserving path we only range-compress the max channel,
	// saturated colors lose luminance. By desaturating them more aggressively first, compressing, and then re-adding some saturation, we can preserve their brightness to a greater extent.
	ictcpMapped.yz = mix(ictcpMapped.yz, ictcp.yz * ictcpMapped.x / max(1e-3, ictcp.x), postCompressionSaturationBoost);
	col = ICtCpToRGB(ictcpMapped);
	return col;
}


// TODO: maybe LogC stuff
