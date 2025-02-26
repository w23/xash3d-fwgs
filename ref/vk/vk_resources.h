#pragma once

#include "vk_descriptor.h"
#include "vk_image.h"
#include "vk_buffer.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "arrays.h"

struct xvk_image_s;
typedef struct vk_resource_s {
	VkDescriptorType type;
	vk_descriptor_value_t value;
	union {
		vk_buffer_t *buffer;
		r_vk_image_t *image;
	} ref;
} vk_resource_t;

typedef struct vk_resource_s *vk_resource_p;

typedef struct rt_resource_s {
	char name[64];
	vk_resource_t resource;

	// TODO internal
	r_vk_image_t image;
	vk_buffer_t *buffer;

	// TODO remove
	int refcount;
	int source_index_plus_1;
} rt_resource_t;

void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceFindByName(const char *name);
rt_resource_t *R_VkResourceFindOrAlloc(const char *name);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);

struct vk_combuf_s;
void R_VkResourcesFrameBeginStateChangeFIXME(struct vk_combuf_s* combuf, qboolean discontinuity);

typedef struct {
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_image_t, images, 32);
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_buffer_t, buffers, 16);
} r_vk_barrier_t;

void R_VkBarrierCommit(struct vk_combuf_s* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask);

void R_VkResourceAddToBarrier(vk_resource_t *res, qboolean write, VkPipelineStageFlags2 dst_stage_mask, r_vk_barrier_t *barrier);
