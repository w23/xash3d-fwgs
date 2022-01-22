#pragma once

#include "vk_rtx.h"

#include "shaders/ray_primary_iface.h"
#include "shaders/ray_light_direct_iface.h"

typedef struct {
	uint32_t width, height;

	struct {
		VkAccelerationStructureKHR tlas;

		vk_buffer_region_t ubo;
		vk_buffer_region_t kusochki, indices, vertices;
		VkDescriptorImageInfo *all_textures; // [MAX_TEXTURES]

		vk_buffer_region_t lights;
		vk_buffer_region_t light_clusters;
	} scene;

#define X(index, name, ...) VkImageView name;
	struct {
		RAY_PRIMARY_OUTPUTS(X)
	} primary;

	struct {
		RAY_LIGHT_DIRECT_OUTPUTS(X)
	} light_direct_polygon;
#undef X
} vk_ray_resources_t;

