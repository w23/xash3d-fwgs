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
	uint texnum;

	struct {
		r_vk_image_t image;
		VkDescriptorSet descriptor_unorm;
	} vk;

	uint hashValue;
	struct vk_texture_s	*nextHash;

	int total_size;
} vk_texture_t;

#define MAX_LIGHTMAPS	256

#define MAX_SAMPLERS 8 // TF_NEAREST x 2 * TF_BORDER x 2 * TF_CLAMP x 2

typedef struct vk_textures_global_s
{
	poolhandle_t mempool;
	
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

// Helper functions
void initTextures( void );
void destroyTextures( void );
vk_texture_t *findTexture(int index);

typedef enum {
	kColorspaceNative,
	kColorspaceLinear,
	kColorspaceGamma,
} colorspace_hint_e;

// Public API functions
int		VK_FindTexture( const char *name );
const char*	VK_TextureName( unsigned int texnum );
const byte*	VK_TextureData( unsigned int texnum );
int		VK_LoadTextureExternal( const char *name, const byte *buf, size_t size, int flags );
int		VK_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int		VK_LoadTextureArray( const char **names, int flags );
int		VK_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void		VK_FreeTexture( unsigned int texnum );
int VK_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );

int R_VkLoadTexture( const char *filename, colorspace_hint_e colorspace, qboolean force_reload);

int XVK_TextureLookupF( const char *fmt, ...);

#define VK_LoadTextureInternal( name, pic, flags ) VK_LoadTextureFromBuffer( name, pic, flags, false )

void XVK_SetupSky( const char *skyboxname );

// Tries to find a texture by its short name
// Full names depend on map name, wad name, etc. This function tries them all.
// Returns -1 if not found
int XVK_FindTextureNamedLike( const char *texture_name );

int XVK_CreateDummyTexture( const char *name );
