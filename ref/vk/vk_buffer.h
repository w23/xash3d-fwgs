#pragma once

#include "vk_core.h"
#include "vk_devmem.h"
#include "vk_staging.h"
#include "r_flipping.h"

typedef struct {
	uint32_t combuf_tag;
	r_vksync_scope_t write, read;
} r_vksync_state_t;

typedef struct vk_buffer_s {
	const char *name; // static
	vk_devmem_t devmem;
	VkBuffer buffer;

	void *mapped;
	uint32_t size;

	r_vksync_state_t sync;
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

typedef struct {
	void *ptr;

	struct {
		vk_buffer_t *buf;
	} impl_;
} vk_buffer_locked_t;

typedef struct {
	uint32_t offset;
	uint32_t size;
} vk_buffer_lock_t;

vk_buffer_locked_t R_VkBufferLock(vk_buffer_t *buf, vk_buffer_lock_t lock);

void R_VkBufferUnlock(vk_buffer_locked_t lock);

// Commits any staged regions for the specified buffer
struct vk_combuf_s;
void R_VkBufferStagingCommit(vk_buffer_t *buf, struct vk_combuf_s *combuf);
