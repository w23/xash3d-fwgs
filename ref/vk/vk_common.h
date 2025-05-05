#pragma once

#include "const.h" // required for ref_api.h
#include "cvardef.h"
#include "com_model.h"
#include "ref_api.h"
#include "com_strings.h"
#include "crtlib.h"
#include "enginefeatures.h" // ENGINE_LINEAR_GAMMA_SPACE

#define ASSERT(x) do { if(!( x )) gEngine.Host_Error( "assert %s failed at %s:%d\n", #x, __FILE__, __LINE__ ); } while (0)
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

// Sliences -Werror=cast-align
// TODO assert for proper alignment for type_
#define PTR_CAST(type_, ptr_) ((type_*)(void*)(ptr_))

inline static int clampi32(int v, int min, int max) {
	if (v < min) return min;
	if (v > max) return max;
	return v;
}

/* TODO? something like
 * struct {
		ref_api_t api;
		ref_globals_t *globals;
		ref_client_t  *client;
		ref_host_t    *host;

		struct world_static_s *world;
		...
 * } r_globals_t;
 * extern r_globals_t *G;
 */

typedef struct {
	struct world_static_s *world;
	cl_entity_t *entities;
	unsigned int max_entities;
	struct movevars_s *movevars;
	color24 *palette;
	cl_entity_t *viewent;
	dlight_t *dlights;
	dlight_t *elights;
	byte *texgammatable;
	uint *lightgammatable;
	uint *lineargammatable;
	uint *screengammatable;
} r_globals_t;

extern r_globals_t globals;
extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;
extern ref_client_t  *gp_cl;
extern ref_host_t    *gp_host;

#define ENGINE_GET_PARM_ (*gEngine.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( ( parm ), 0 )

#define WORLDMODEL (gp_cl->models[1])
#define MOVEVARS (globals.movevars)

static inline byte TextureToGamma( byte b )
{
	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? globals.texgammatable[b] : b;
}

static inline uint LightToTexGamma( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? globals.lightgammatable[b] : b;
}

static inline uint ScreenGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? globals.screengammatable[b] : b;
}

static inline uint LinearGammaTable( uint b )
{
	if( unlikely( b >= 1024 ))
		return 0;

	return !FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE ) ? globals.lineargammatable[b] : b;
}

void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa );
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec );
