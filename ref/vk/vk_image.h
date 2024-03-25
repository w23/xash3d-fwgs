#pragma once
#include "vk_core.h"
#include "vk_devmem.h"

typedef struct r_vk_image_s {
	vk_devmem_t devmem;
	VkImage image;
	VkImageView view;

	// Optional, created by kVkImageFlagCreateUnormView
	// Used for sRGB-γ-unaware traditional renderer
	VkImageView view_unorm;

	uint32_t width, height, depth;
	int mips, layers;
	VkFormat format;
	uint32_t flags;
} r_vk_image_t;

enum {
	kVkImageFlagIgnoreAlpha = (1<<0),
	kVkImageFlagIsCubemap = (1<<1),
	kVkImageFlagCreateUnormView = (1<<2),
};

typedef struct {
	const char *debug_name;
	uint32_t width, height, depth;
	int mips, layers;
	VkFormat format;
	VkImageTiling tiling;
	VkImageUsageFlags usage;
	VkMemoryPropertyFlags memory_props;
	uint32_t flags;
} r_vk_image_create_t;

r_vk_image_t R_VkImageCreate(const r_vk_image_create_t *create);
void R_VkImageDestroy(r_vk_image_t *img);

void R_VkImageClear(VkCommandBuffer cmdbuf, VkImage image);

typedef struct {
	VkPipelineStageFlags in_stage;
	struct {
		VkImage image;
		int width, height;
		VkImageLayout oldLayout;
		VkAccessFlags srcAccessMask;
	} src, dst;
} r_vkimage_blit_args;

void R_VkImageBlit( VkCommandBuffer cmdbuf, const r_vkimage_blit_args *blit_args );

uint32_t R_VkImageFormatTexelBlockSize( VkFormat format );

void R_VkImageUploadBegin( r_vk_image_t *img );
void R_VkImageUploadSlice( r_vk_image_t *img, int layer, int mip, int size, const void *data );
void R_VkImageUploadEnd( r_vk_image_t *img );
