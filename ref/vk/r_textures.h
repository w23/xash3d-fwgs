#pragma once

#include "const.h" // required for com_model.h, ref_api.h
#include "cvardef.h" // required for ref_api.h
#include "com_model.h" // required for ref_api.h
#include "ref_api.h" // texFlags_t

#define MAX_LIGHTMAPS 256

typedef struct vk_textures_global_s
{
	// TODO Fix these at compile time statically, akin to BLUE_NOISE_TEXTURE_ID
	int defaultTexture;   	// use for bad textures
	int particleTexture;
	int whiteTexture;
	int grayTexture;
	int blackTexture;
	int solidskyTexture;	// quake1 solid-sky layer
	int alphaskyTexture;	// quake1 alpha-sky layer
	int lightmapTextures[MAX_LIGHTMAPS];
	int dlightTexture;	// custom dlight texture
	int cinTexture;      	// cinematic texture

	// TODO wire it up for ref_interface_t return
	qboolean fCustomSkybox;
} vk_textures_global_t;

// TODO rename this consistently
extern vk_textures_global_t tglob;

qboolean R_TexturesInit( void );
void R_TexturesShutdown( void );

////////////////////////////////////////////////////////////
// Ref interface functions, exported
// TODO mark names somehow, ie. R_TextureApi... ?
int R_TextureFindByName( const char *name );
const char* R_TextureGetNameByIndex( unsigned int texnum );

void R_TextureSetupSky( const char *skyboxname );

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags );
int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only );
void R_TextureFree( unsigned int texnum );

int R_TexturesGetParm( int parm, int arg );

////////////////////////////////////////////////////////////
// Extra functions used in ref_vk
void R_TextureAcquire( unsigned int texnum );
void R_TextureRelease( unsigned int texnum );

typedef enum {
	kColorspaceNative,
	kColorspaceLinear,
	kColorspaceGamma,
} colorspace_hint_e;

int R_TextureUploadFromFileExAcquire( const char *filename, colorspace_hint_e colorspace, qboolean force_reload );

int R_TextureFindByNameF( const char *fmt, ...);

// Tries to find a texture by its short name
// Full names depend on map name, wad name, etc. This function tries them all.
// Returns -1 if not found
int R_TextureFindByNameLike( const char *texture_name );

struct vk_texture_s;
struct vk_texture_s *R_TextureGetByIndex( uint index );
