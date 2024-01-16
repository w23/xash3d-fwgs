#ifndef DEBUG_GLSL_INCLUDED
#define DEBUG_GLSL_INCLUDED

// 1. No validation -- fastest, expects no math errors, will break if anything goes wrong.
//    Public release?

// 2. Validate and clamp sensitive/output values -- cheap, fast, hides any math errors
//    Public beta? Release?
//#define DEBUG_VALIDATE

// 3. Validate, print and clamp sensitive -- printing is relatively slow, can detect errors for subsequent analysis
//    Public alpha/debug? -vkdebug? -vkvalidate?
#define DEBUG_VALIDATE_PRINT

// 4. Validate and clamp everything -- slow, but can help with analysis
//    Development/shader debugging only.
//#define DEBUG_VALIDATE_EXTRA

// Enable extra shader debug printing for custom things
//#define DEBUG_ENABLE_PRINT

// Extra implies print
#ifdef DEBUG_VALIDATE_EXTRA
#define DEBUG_VALIDATE_PRINT
#endif

// Print implies validation
#ifdef DEBUG_VALIDATE_PRINT
#define DEBUG_VALIDATE
#endif

#if defined(DEBUG_ENABLE_PRINT) || defined(DEBUG_VALIDATE_PRINT)
#extension GL_EXT_debug_printf: enable
#endif // SHADER_DEBUG_ENABLE

#define IS_INVALID(v) (isnan(v) || isinf(v))
#define IS_INVALIDV(v) (any(isnan(v)) || any(isinf(v)))
#define PRIVEC3(v) (v).r, (v).g, (v).b
#define PRIVEC4(v) (v).r, (v).g, (v).b, (v).a

#ifndef DEBUG_VALIDATE
// Dummy for no validation
#define DEBUG_VALIDATE_RANGE_VEC3(s, v, min_v, max_v)
#define DEBUG_VALIDATE_RANGE(v, min_v, max_v)
#define DEBUG_VALIDATE_VEC3(v, msg)
#elif !defined(DEBUG_VALIDATE_PRINT) // #indef DEBUG_VALIDATE
// DEBUG_VALIDATE is defined, DEBUG_VALIDATE_PRINT are not
#define DEBUG_VALIDATE_RANGE_VEC3(s, v, min_v, max_v) \
	if (IS_INVALIDV(v) || any(lessThan(v,vec3(min_v))) || any(greaterThan(v,vec3(max_v)))) { \
		v = clamp(v, vec3(min_v), vec3(max_v)); \
	}
#define DEBUG_VALIDATE_RANGE(v, min_v, max_v) \
	if (IS_INVALID(v) || v < min_v || v > max_v) { \
		v = clamp(v, min_v, max_v); \
	}
#define DEBUG_VALIDATE_VEC3(v, msg) \
	if (IS_INVALIDV(v)) { \
		v = vec3(0.); \
	}
#else // #ifndef DEBUG_VALIDATE_PRINT
// Both DEBUG_VALIDATE and DEBUG_VALIDATE_PRINT are defined
#define DEBUG_VALIDATE_RANGE_VEC3(s, v, min_v, max_v) \
	if (IS_INVALIDV(v) || any(lessThan(v,vec3(min_v))) || any(greaterThan(v,vec3(max_v)))) { \
		debugPrintfEXT("%d INVALID vec3=(%f, %f, %f)", __LINE__, PRIVEC3(v)); \
		v = clamp(v, vec3(min_v), vec3(max_v)); \
	}
#define DEBUG_VALIDATE_RANGE(v, min_v, max_v) \
	if (IS_INVALID(v) || v < min_v || v > max_v) { \
		debugPrintfEXT("%d INVALID %f", __LINE__, v); \
		v = clamp(v, min_v, max_v); \
	}
// msg should begin with "%d" for __LINE__
// GLSL u y no string concatenation ;_;
#define DEBUG_VALIDATE_VEC3(v, msg) \
	if (IS_INVALIDV(v)) { \
		debugPrintfEXT(msg, __LINE__, PRIVEC3(v)); \
		v = vec3(0.); \
	}
#endif // #else #ifndef DEBUG_VALIDATE

#endif // ifndef DEBUG_GLSL_INCLUDED
