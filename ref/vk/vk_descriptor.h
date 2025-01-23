#pragma once

#include "vk_core.h"

#include "vk_const.h"

// Only used for traditional renderer
typedef struct descriptor_pool_s
{
	VkDescriptorPool pool;

	VkDescriptorSet texture_sets[MAX_TEXTURES];
	VkDescriptorSetLayout one_texture_layout;

	// FIXME HOW THE F
	VkDescriptorSet ubo_sets[2];
	VkDescriptorSetLayout one_uniform_buffer_layout;
} descriptor_pool_t;

// FIXME: move to traditional renderer
extern descriptor_pool_t vk_desc_fixme;

qboolean VK_DescriptorInit( void );
void VK_DescriptorShutdown( void );

struct xvk_image_s;
typedef union {
	VkDescriptorBufferInfo buffer;
	VkDescriptorImageInfo image;
	const VkDescriptorImageInfo *image_array;
	VkWriteDescriptorSetAccelerationStructureKHR accel;
} vk_descriptor_value_t;

typedef struct {
	int num_bindings;
	const VkDescriptorSetLayoutBinding *bindings;

	// Used in Write only
	vk_descriptor_value_t *values;

	VkPushConstantRange push_constants;

	VkPipelineLayout pipeline_layout;
	VkDescriptorSetLayout desc_layout;
	VkDescriptorPool desc_pool;

	int num_sets;
	VkDescriptorSet *desc_sets;
} vk_descriptors_t;

void VK_DescriptorsCreate(vk_descriptors_t *desc);
void VK_DescriptorsWrite(const vk_descriptors_t *desc, int set_slot);
void VK_DescriptorsDestroy(const vk_descriptors_t *desc);
