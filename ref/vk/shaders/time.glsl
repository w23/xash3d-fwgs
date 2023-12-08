#ifndef TIME_GLSL_INCLUDED
#define TIME_GLSL_INCLUDED

//#define PROF_USE_REALTIME
#ifdef PROF_USE_REALTIME
#extension GL_ARB_gpu_shader_int64: enable
#extension GL_EXT_shader_realtime_clock: enable
#else
#extension GL_ARB_shader_clock: enable
#endif

#define time_t uvec2
#define T0 uvec2(0)
#define clockRealtime clockRealtime2x32EXT
#define clockShader clock2x32ARB

#define clockRealtime64 clockRealtimeEXT
#define clockShader64 clockARB

#ifdef PROF_USE_REALTIME
uint clockRealtimeDelta(time_t begin, time_t end) {
	const uint64_t begin64 = begin.x | (uint64_t(begin.y) << 32);
	const uint64_t end64 = end.x | (uint64_t(end.y) << 32);
	const uint64_t time_diff = end64 - begin64;
	return uint(time_diff);
}
#endif

uint clockShaderDelta(time_t begin, time_t end) {
	// AMD RNDA2 SHADER_CYCLES reg is limited to 20 bits
	return (end.x - begin.x) & 0xfffffu;
}

#ifdef PROF_USE_REALTIME
// On mesa+amdgpu there's a clear gradient: pixels on top of screen take 2-3x longer to compute than bottom ones. Also,
// it does flicker a lot.
// Deltas are about 30000-100000 parrots
#define timeNow clockRealtime
#define timeDelta clockRealtimeDelta
#else
// clockARB doesn't give directly usable time values on mesa+amdgpu
// even deltas between them are not meaningful enough.
// On AMD clockARB() values are limited to lower 20 bits (see RDNA 2 ISA SHADER_CYCLES reg), and they wrap around a lot.
// Absolute difference value are often 30-50% of the available range, so it's not that far off from wrapping around
// multiple times, rendering the value completely useless.
// Deltas are around 300000-500000 parrots.
// Other than that, the values seem uniform across the screen (as compared to realtime clock, which has a clearly
// visible gradient: top differences are larger than bottom ones.
#define timeNow clockShader
#define timeDelta clockShaderDelta
#endif

#endif //ifndef TIME_GLSL_INCLUDED
