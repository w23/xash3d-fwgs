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

// Hardcoded expected blue noise texture slot
// TODO consider moving it into a separate resource bindable by request
// TODO make it a 3D texture. Currently it's just a sequence of BLUE_NOISE_SIZE textures, loaded into consecutive slots.
#define BLUE_NOISE_TEXTURE_ID 7

// Hardcode blue noise texture size to 64x64x64
#define BLUE_NOISE_SIZE 64

	// TODO wire it up for ref_interface_t return
	qboolean fCustomSkybox;

	// TODO:
	//1. vk_texture_t blue_noise; 3d texture
	//2. separate binding similar to skybox in vk_rtx.c and shaders
	//3. patch shader function
} vk_textures_global_t;

// TODO rename this consistently
extern vk_textures_global_t tglob;

qboolean R_TexturesInit( void );
void R_TexturesShutdown( void );

// Ref interface functions, exported
// TODO mark names somehow, ie. R_TextureApi... ?
int R_TextureFindByName( const char *name );
const char* R_TextureGetNameByIndex( unsigned int texnum );

void R_TextureSetupSky( const char *skyboxname );

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags );
int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only );
void R_TextureFree( unsigned int texnum );

int R_TexturesGetParm( int parm, int arg );

// Extra functions used in ref_vk

int R_TextureFindByNameF( const char *fmt, ...);

// Tries to find a texture by its short name
// Full names depend on map name, wad name, etc. This function tries them all.
// Returns -1 if not found
int R_TextureFindByNameLike( const char *texture_name );
