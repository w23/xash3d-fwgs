#pragma once

#include "vk_core.h"
#include "arrays.h"


struct vk_buffer_s;
typedef struct {
	struct vk_buffer_s *buffer;
	VkAccessFlags2 access;
} r_vkcombuf_barrier_buffer_t;

struct r_vk_image_s;
typedef struct {
	struct r_vk_image_s *image;
	VkImageLayout layout;
	VkAccessFlags2 access;
} r_vkcombuf_barrier_image_t;

typedef struct {
	VkPipelineStageFlags2 stage;
	VIEW_DECLARE_CONST(r_vkcombuf_barrier_buffer_t, buffers);
	VIEW_DECLARE_CONST(r_vkcombuf_barrier_image_t, images);
} r_vkcombuf_barrier_t;

struct vk_combuf_s;

// Immediately issues a barrier for the set of resources given desired usage and resources states
void R_VkCombufIssueBarrier(struct vk_combuf_s*, r_vkcombuf_barrier_t);

typedef struct r_vk_barrier_s {
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_image_t, images, 32);
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_buffer_t, buffers, 16);
} r_vk_barrier_t;

void R_VkBarrierCommit(struct vk_combuf_s* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask);

