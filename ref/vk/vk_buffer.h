#pragma once

#include "vk_core.h"
#include "vk_module.h"

#include "vk_devmem.h"
#include "r_flipping.h"
#include "alolcator.h"

extern RVkModule g_module_buffer;

typedef struct vk_buffer_s {
	vk_devmem_t devmem;
	VkBuffer buffer;

	void *mapped;
	uint32_t size;
} vk_buffer_t;

qboolean VK_BufferCreate(const char *debug_name, vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
void VK_BufferDestroy(vk_buffer_t *buf);

VkDeviceAddress R_VkBufferGetDeviceAddress(VkBuffer buffer);

typedef struct {
	r_flipping_buffer_t dynamic;
	uint32_t static_size;
	uint32_t static_offset;
} r_debuffer_t;

typedef enum {
	LifetimeStatic, LifetimeDynamic,
} r_lifetime_t;

void R_DEBuffer_Init(r_debuffer_t *debuf, uint32_t static_size, uint32_t dynamic_size);
uint32_t R_DEBuffer_Alloc(r_debuffer_t* debuf, r_lifetime_t lifetime, uint32_t size, uint32_t align);
void R_DEBuffer_Flip(r_debuffer_t* debuf);
