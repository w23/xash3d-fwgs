#pragma once

#include "vk_descriptor.h"
#include "vk_image.h"
#include "vk_buffer.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "arrays.h"

typedef struct vk_resource_acquire_descriptor_args_s {
	struct vk_combuf_s *combuf;
	struct r_vk_barrier_s *barriers;
	VkAccessFlags2 access;
	VkImageLayout image_layout;
} vk_resource_acquire_descriptor_args_t;

struct rt_resource_s;
typedef void (vk_resource_dtor_f)(struct rt_resource_s*);
typedef vk_descriptor_value_t (vk_resource_acquire_descriptor_f)(struct rt_resource_s*, vk_resource_acquire_descriptor_args_t);

typedef struct rt_resource_s {
	char name[64];
	VkDescriptorType type;
	vk_resource_dtor_f *destroy;
	vk_resource_acquire_descriptor_f *acquire_descriptor;

	// Used for tracking meatpipe resources when reloading meatpipes
	int refcount;
} rt_resource_t;


void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceFindByName(const char *name);
qboolean R_VkResourceRegister(rt_resource_t *res);

// TODO remove these when ping-pong resource is a dedicated type of resource
rt_resource_t *R_VkResourceGetByIndex(int index);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);


// Dummy resource that just returns `descriptor_value` without doing anything else
typedef struct rt_resource_dummy_s {
	rt_resource_t header;
	vk_descriptor_value_t descriptor_value;
} rt_resource_dummy_t;

void R_VkResourceDummyInit(rt_resource_dummy_t *res, const char *name, VkDescriptorType, vk_descriptor_value_t);


typedef struct vk_resource_buffer_t {
	rt_resource_t header;
	vk_buffer_t *buffer;
	size_t offset;
	size_t size;
} vk_resource_buffer_t;

typedef struct {
	const char *name;
	VkDescriptorType type;
	vk_buffer_t *buffer;
	size_t offset;
	size_t size;
} r_vkbuffer_register_as_resource_t;

vk_resource_buffer_t *R_VkBufferRegisterAsResource(r_vkbuffer_register_as_resource_t args);
