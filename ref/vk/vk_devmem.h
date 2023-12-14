#pragma once
#include "vk_core.h"

qboolean VK_DevMemInit( void );
void VK_DevMemDestroy( void );

typedef int vk_devmem_usage_type_t;
enum VK_DevMemUsageTypes {
	// NOTE(nilsoncore):
	// This type should not be used as it is there
	// to not overcomplicate things and index through
	// internal global stats directly by these indices.
	VK_DEVMEM_USAGE_TYPE_ALL     = 0,
	
	// Those are `vk_buffer_t` buffers.
	VK_DEVMEM_USAGE_TYPE_BUFFER  = 1,

	// Those are `xvk_image_t` images.
	VK_DEVMEM_USAGE_TYPE_IMAGE   = 2,

	VK_DEVMEM_USAGE_TYPES_COUNT
};

typedef struct vk_devmem_s {
	VkDeviceMemory device_memory;
	uint32_t offset;
	vk_devmem_usage_type_t usage_type;
	void *mapped;

	struct {
		int slot_index;
		int block_index;
		
		// alolcator.h:
		// typedef uint32_t alo_size_t; 
		uint32_t block_size;
		uint32_t block_alignment;
		uint32_t block_alignment_hole;
	} internal;
} vk_devmem_t;

typedef struct vk_devmem_allocate_args_s {
	VkMemoryRequirements  requirements;
	VkMemoryPropertyFlags property_flags;
	VkMemoryAllocateFlags allocate_flags;
} vk_devmem_allocate_args_t;

#define VK_DevMemAllocateBuffer( name, devmem_allocate_args ) \
	VK_DevMemAllocate( name, VK_DEVMEM_USAGE_TYPE_BUFFER, devmem_allocate_args );

#define VK_DevMemAllocateImage( name, devmem_allocate_args ) \
	VK_DevMemAllocate( name, VK_DEVMEM_USAGE_TYPE_IMAGE, devmem_allocate_args );

vk_devmem_t VK_DevMemAllocate(const char *name, vk_devmem_usage_type_t usage_type, vk_devmem_allocate_args_t devmem_allocate_args);
void VK_DevMemFree(const vk_devmem_t *mem);

// Returns short string representation of `vk_devmem_usage_type_t` usage type.
static const char *VK_DevMemUsageTypeString( vk_devmem_usage_type_t type );

