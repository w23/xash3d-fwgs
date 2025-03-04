#pragma once

#include "vk_descriptor.h"
#include "vk_image.h"
#include "vk_buffer.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "arrays.h"

typedef struct vk_resource_s {
	// Used for adding resource into correct typed barriers arrays (images and buffers)
	VkDescriptorType type;

	// Used for setting descriptor sets to bind
	vk_descriptor_value_t value;

	// Used for barriers "only"
	union {
		vk_buffer_t *buffer;
		r_vk_image_t *image;
	} ref;
} vk_resource_t;

typedef struct vk_resource_s *vk_resource_p;

typedef struct rt_resource_s {
	char name[64];

	// TODO move into producer
	vk_resource_t resource;

	// TODO things below are resource-specific

	// Used for meatpipe G-buffer images
	r_vk_image_t image;

	// Used for tracking meatpipe resources when reloading meatpipes
	int refcount;

	// Used for ping-pong meatpipe G-buffer images (e.g. for temporal denoiser)
	int source_index_plus_1;
} rt_resource_t;

void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceFindByName(const char *name);
qboolean R_VkResourceRegister(rt_resource_t *res);

// TODO remove these when ping-pong resource is a dedicated type of resource
rt_resource_t *R_VkResourceGetByIndex(int index);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);


typedef struct {
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_image_t, images, 32);
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_buffer_t, buffers, 16);
} r_vk_barrier_t;

void R_VkBarrierCommit(struct vk_combuf_s* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask);

void R_VkResourceAddToBarrier(vk_resource_t *res, qboolean write, VkPipelineStageFlags2 dst_stage_mask, r_vk_barrier_t *barrier);
