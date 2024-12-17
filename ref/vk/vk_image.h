#pragma once
#include "vk_core.h"
#include "vk_devmem.h"

qboolean R_VkImageInit(void);
void R_VkImageShutdown(void);

typedef struct r_vk_image_s {
	char name[64];

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
	uint32_t image_size;

	int upload_slot;

	struct {
		VkImageLayout layout;
		r_vksync_scope_t write, read;
	} sync;
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

struct vk_combuf_s;
void R_VkImageClear(r_vk_image_t *img, struct vk_combuf_s* combuf, const VkClearColorValue*);

typedef struct {
	struct {
		r_vk_image_t *image;
		int width, height, depth;
	} src, dst;
} r_vkimage_blit_args;

void R_VkImageBlit(struct vk_combuf_s *combuf, const r_vkimage_blit_args *blit_args );

uint32_t R_VkImageFormatTexelBlockSize( VkFormat format );

// Expects *img to be pinned and valid until either cancel or commit is called
void R_VkImageUploadBegin( r_vk_image_t *img );
void R_VkImageUploadSlice( r_vk_image_t *img, int layer, int mip, int size, const void *data );
void R_VkImageUploadEnd( r_vk_image_t *img );

// Upload all enqueued images using the given command buffer
void R_VkImageUploadCommit( struct vk_combuf_s *combuf, VkPipelineStageFlagBits dst_stages );
