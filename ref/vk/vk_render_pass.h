#pragma once

#include "vk_core.h"

typedef struct vk_render_pass_s {
	// Used when the entire rendering is traditional triangle rasterization
	// Discards and clears color buffer
	VkRenderPass raster;

	// Used for 2D overlay rendering after ray tracing pass
	// Preserves color buffer contents
	VkRenderPass after_ray_tracing;
} vk_render_pass_t;

extern vk_render_pass_t vk_render_pass;

qboolean R_VkRenderPassInit(VkFormat format, VkFormat depth_format);
void R_VkRenderPassShutdown(void);
