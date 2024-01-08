#ifndef DEBUG_GLSL_INCLUDED
#define DEBUG_GLSL_INCLUDED

#define IS_INVALID(v) (isnan(v) || isinf(v))
#define IS_INVALID3(v) (any(isnan(v)) || any(isinf(v)))
#define PRIVEC3(v) (v).r, (v).g, (v).b

#define SHADER_DEBUG_ENABLE
#ifdef SHADER_DEBUG_ENABLE

#extension GL_EXT_debug_printf: enable

// msg should begin with "%d" for __LINE__
// GLSL u y no string concatenation ;_;
#define VALIDATE_VEC3(v, msg) \
				if (IS_INVALID3(v)) { \
					debugPrintfEXT(msg, __LINE__, PRIVEC3(v)); \
					v = vec3(0.); \
				}
#else // SHADER_DEBUG_ENABLE
#define VALIDATE_VEC3(v, msg) \
				if (IS_INVALID3(v)) { \
					v = vec3(0.); \
				}

// GLSL u y no variadic macro
//#define debugPrintfEXT(...)

#endif // SHADER_DEBUG_ENABLE

#endif // ifndef DEBUG_GLSL_INCLUDED
