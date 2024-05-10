#pragma once

#include "const.h" // required for ref_api.h
#include "cvardef.h"
#include "com_model.h"
#include "ref_api.h"
#include "com_strings.h"
#include "crtlib.h"

#define ASSERT(x) if(!( x )) gEngine.Host_Error( "assert %s failed at %s:%d\n", #x, __FILE__, __LINE__ )
// TODO ASSERTF(x, fmt, ...)

#define Mem_Malloc( pool, size ) gEngine._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) gEngine._Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngine._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) gEngine._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngine._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngine._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngine._Mem_EmptyPool( pool, __FILE__, __LINE__ )

#define ALIGN_UP(ptr, align) ((((ptr) + (align) - 1) / (align)) * (align))

#define COUNTOF(a) (sizeof(a)/sizeof((a)[0]))

inline static int clampi32(int v, int min, int max) {
	if (v < min) return min;
	if (v > max) return max;
	return v;
}

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;

// TODO improve and make its own file
#define BOUNDED_ARRAY_DECLARE(NAME, TYPE, MAX_SIZE) \
		struct { \
			TYPE items[MAX_SIZE]; \
			int count; \
		} NAME

#define BOUNDED_ARRAY(NAME, TYPE, MAX_SIZE) \
	BOUNDED_ARRAY_DECLARE(NAME, TYPE, MAX_SIZE) = {0}

#define BOUNDED_ARRAY_APPEND(var, item) \
		do { \
			ASSERT(var.count < COUNTOF(var.items)); \
			var.items[var.count++] = item; \
		} while(0)
