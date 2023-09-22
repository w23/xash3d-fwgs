#pragma once
#include "vk_core.h"

qboolean VK_DevMemInit( void );
void VK_DevMemDestroy( void );

typedef struct vk_devmem_s {
	VkDeviceMemory device_memory;
	uint32_t offset;
	void *mapped;

	// Internal
	int _slot_index;
	int _block_index; 
	int _block_size;
} vk_devmem_t;

vk_devmem_t VK_DevMemAllocate(const char *name, VkMemoryRequirements req, VkMemoryPropertyFlags props, VkMemoryAllocateFlags flags);
void VK_DevMemFree(const vk_devmem_t *mem);
