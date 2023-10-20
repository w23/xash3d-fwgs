#pragma once
#include "vk_core.h"
#include "vk_image.h"
#include "vk_const.h"

#include "xash3d_types.h"
#include "const.h"
#include "render_api.h"
#include "com_image.h"

typedef struct vk_texture_s
{
	char name[256];

	int width, height;
	texFlags_t flags;
	int total_size;

	struct {
		r_vk_image_t image;
		VkDescriptorSet descriptor_unorm;
	} vk;

	// Internals for hash table
	uint texnum;
	uint hashValue;
	struct vk_texture_s	*nextHash;

	int refcount;

	// TODO "cache" eviction
	// int used_maps_ago;
} vk_texture_t;

#define MAX_LIGHTMAPS 256
#define MAX_SAMPLERS 8 // TF_NEAREST x 2 * TF_BORDER x 2 * TF_CLAMP x 2

typedef struct vk_textures_global_s
{
	poolhandle_t mempool;

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

	qboolean fCustomSkybox; // TODO do we need this for anything?

	vk_texture_t skybox_cube;
	vk_texture_t cubemap_placeholder;

	// All textures descriptors in their native formats used for RT
	VkDescriptorImageInfo dii_all_textures[MAX_TEXTURES];

	// FIXME this should not exist, all textures should have their own samplers based on flags
	VkSampler default_sampler_fixme;

	struct {
		texFlags_t flags;
		VkSampler sampler;
	} samplers[MAX_SAMPLERS];
} vk_textures_global_t;

// TODO rename this consistently
extern vk_textures_global_t tglob;

void R_TexturesInit( void );
void R_TexturesShutdown( void );

// Ref interface functions, exported
int R_TextureFindByName( const char *name );
const char* R_TextureGetNameByIndex( unsigned int texnum );

void R_TextureSetupSky( const char *skyboxname );

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags );
int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only );

void R_TextureFree( unsigned int texnum );

// Functions only used in this renderer
vk_texture_t *R_TextureGetByIndex(int index);

void R_TextureAcquire( unsigned int texnum );
void R_TextureRelease( unsigned int texnum );

#define R_TextureUploadFromBufferNew(name, pic, flags) R_TextureUploadFromBuffer(name, pic, flags, false)

typedef enum {
	kColorspaceNative,
	kColorspaceLinear,
	kColorspaceGamma,
} colorspace_hint_e;

int R_TextureUploadFromFileEx( const char *filename, colorspace_hint_e colorspace, qboolean force_reload );
int R_TextureFindByNameF( const char *fmt, ...);

// Tries to find a texture by its short name
// Full names depend on map name, wad name, etc. This function tries them all.
// Returns -1 if not found
int R_TextureFindByNameLike( const char *texture_name );

// Used by materials to piggy-back onto texture name-to-index hash table
int R_TextureCreateDummy_FIXME( const char *name );
