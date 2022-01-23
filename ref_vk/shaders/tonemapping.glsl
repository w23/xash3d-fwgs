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
	//return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));
	return clamp(m2 * (a / b), 0.0, 1.0);
}

vec3 reinhard(vec3 color){
	return color / (color + 1.0);
}

vec3 reinhard02(vec3 c, vec3 Cwhite2) {
	return c * (1. + c / Cwhite2) / (1. + c);
}

// https://github.com/SNMetamorph/PrimeXT/blob/7e3ac4bd6924c42e1d8f467dcf88fc8b6f105ca5/game_dir/glsl/postfx/tonemap_fp.glsl#L52-L60
vec3 TonemapMGS5(vec3 source)
{
	const float a = 0.6;
	const float b = 0.45333;
	vec3 t = step(a, source);
	vec3 f1 = source;
	vec3 f2 = min(vec3(1.0), a + b - (b*b) / (f1 - a + b));
	return mix(f1, f2, t);
}


// https://github.com/dmnsgn/glsl-tone-map/blob/master/uncharted2.glsl
vec3 uncharted2Tonemap(vec3 x) {
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
vec3 uncharted2(vec3 color) {
	const float W = 11.2;
	float exposureBias = 2.0;
	vec3 curr = uncharted2Tonemap(exposureBias * color);
	vec3 whiteScale = 1.0 / uncharted2Tonemap(vec3(W));
	return curr * whiteScale;
}
float uncharted2Tonemap(float x) {
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	float W = 11.2;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float uncharted2(float color) {
	const float W = 11.2;
	const float exposureBias = 2.0;
	float curr = uncharted2Tonemap(exposureBias * color);
	float whiteScale = 1.0 / uncharted2Tonemap(W);
	return curr * whiteScale;
}