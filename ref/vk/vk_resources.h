#pragma once

#include "vk_descriptor.h"
#include "vk_image.h"
#include "vk_buffer.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "arrays.h"

typedef struct vk_resource_s {
	// Used for setting descriptor sets to bind
	vk_descriptor_value_t value;
} vk_resource_t;

typedef struct vk_resource_acquire_descriptor_args_s {
	struct vk_combuf_s *combuf;
	struct r_vk_barrier_s *barriers;
	VkAccessFlags2 access;
	VkImageLayout image_layout;
} vk_resource_acquire_descriptor_args_t;

struct rt_resource_s;
typedef vk_descriptor_value_t (vk_resource_acquire_descriptor_f)(struct rt_resource_s*, vk_resource_acquire_descriptor_args_t);

typedef struct rt_resource_s {
	char name[64];
	VkDescriptorType type;
	// TODO dtor
	vk_resource_acquire_descriptor_f *acquire_descriptor;

	// Used for tracking meatpipe resources when reloading meatpipes
	int refcount;

	// Used for meatpipe G-buffer images
	// FIXME remove
	r_vk_image_t image;

	// TODO move into acquire_descriptor
	vk_resource_t resource__;

	// TODO things below are resource-specific

	// Used for ping-pong meatpipe G-buffer images (e.g. for temporal denoiser)
	int source_index_plus_1;
} rt_resource_t;


// Dummy resource that just returns `descriptor_value` without doing anything else
typedef struct rt_resource_dummy_s {
	rt_resource_t header;
	vk_descriptor_value_t descriptor_value;
} rt_resource_dummy_t;

void R_VkResourceDummyInit(rt_resource_dummy_t *res, const char *name, VkDescriptorType, vk_descriptor_value_t);


void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceFindByName(const char *name);
qboolean R_VkResourceRegister(rt_resource_t *res);

// TODO remove these when ping-pong resource is a dedicated type of resource
rt_resource_t *R_VkResourceGetByIndex(int index);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);


typedef struct r_vk_barrier_s {
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_image_t, images, 32);
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_buffer_t, buffers, 16);
} r_vk_barrier_t;

void R_VkBarrierCommit(struct vk_combuf_s* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask);


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
