#pragma once

#include "r_textures.h"

#include "vk_core.h"
#include "vk_image.h"
#include "vk_const.h"

typedef struct vk_texture_s
{
	char name[256];

	int width, height;
	uint32_t flags;
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

typedef enum {
	kColorspaceNative,
	kColorspaceLinear,
	kColorspaceGamma,
} colorspace_hint_e;

qboolean R_VkTexturesInit( void );
void R_VkTexturesShutdown( void );

qboolean R_VkTexturesSkyboxUpload( const char *name, rgbdata_t *const sides[6], colorspace_hint_e colorspace_hint, qboolean placeholder);

qboolean R_VkTextureUpload(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint);

// FIXME s/R_/R_Vk/
void R_TextureAcquire( unsigned int texnum );
void R_TextureRelease( unsigned int texnum );

#define R_TextureUploadFromBufferNew(name, pic, flags) R_TextureUploadFromBuffer(name, pic, flags, false)

int R_TextureUploadFromFileEx( const char *filename, colorspace_hint_e colorspace, qboolean force_reload );
// Used by materials to piggy-back onto texture name-to-index hash table
int R_TextureCreateDummy_FIXME( const char *name );

VkDescriptorImageInfo R_VkTextureGetSkyboxDescriptorImageInfo( void );
const VkDescriptorImageInfo* R_VkTexturesGetAllDescriptorsArray( void );
VkDescriptorSet R_VkTextureGetDescriptorUnorm( uint index );
