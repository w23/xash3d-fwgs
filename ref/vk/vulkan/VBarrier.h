#pragma once

#include "vk_core.h"
#include "std/arrays.h"

#define MAX_BUFFER_BARRIERS 16
#define MAX_IMAGE_BARRIERS 32

struct vk_combuf_s;

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

typedef struct Barrier {
	VkPipelineStageFlagBits2 stage;
	BOUNDED_ARRAY_DECLARE(VkBufferMemoryBarrier2, buffers, MAX_BUFFER_BARRIERS);
	BOUNDED_ARRAY_DECLARE(VkImageMemoryBarrier2, images, MAX_IMAGE_BARRIERS);
} Barrier;

static inline Barrier barrierMake(VkPipelineStageFlagBits2 stage) {
	return (Barrier) {
		.stage = stage,
		.images.count = 0,
		.buffers.count = 0,
	};
}

void barrierAddImage(Barrier *, r_vkcombuf_barrier_image_t);
void barrierAddBuffer(Barrier *, r_vkcombuf_barrier_buffer_t);
void barrierCommit(Barrier *, struct vk_combuf_s *);
