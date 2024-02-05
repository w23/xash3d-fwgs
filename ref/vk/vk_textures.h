#pragma once

#include "r_textures.h"
#include "vk_core.h"
#include "vk_image.h"
#include "vk_const.h"

#include "unordered_roadmap.h"

typedef struct vk_texture_s
{
	urmom_header_t hdr_;

	int width, height, depth;
	uint32_t flags;
	int total_size;

	struct {
		r_vk_image_t image;

		// TODO external table
		VkDescriptorSet descriptor_unorm;
	} vk;

	int refcount;
	qboolean ref_interface_visible;

	// TODO "cache" eviction
	// int used_maps_ago;
} vk_texture_t;

#define TEX_NAME(tex) ((tex)->hdr_.key)

qboolean R_VkTexturesInit( void );
void R_VkTexturesShutdown( void );

qboolean R_VkTexturesSkyboxUpload( const char *name, const rgbdata_t *pic, colorspace_hint_e colorspace_hint, skybox_slot_e skybox_slot );
void R_VkTexturesSkyboxUnload(void);

qboolean R_VkTextureUpload(int index, vk_texture_t *tex, const rgbdata_t *pic, colorspace_hint_e colorspace_hint);
void R_VkTextureDestroy(int index, vk_texture_t *tex);

VkDescriptorImageInfo R_VkTexturesGetSkyboxDescriptorImageInfo( skybox_slot_e slot );
const VkDescriptorImageInfo* R_VkTexturesGetAllDescriptorsArray( void );
VkDescriptorSet R_VkTextureGetDescriptorUnorm( uint index );

VkDescriptorImageInfo R_VkTexturesGetBlueNoiseImageInfo( void );
