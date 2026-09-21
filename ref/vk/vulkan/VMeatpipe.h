#pragma once

#include <stdint.h>

struct vk_meatpipe_s* R_VkMeatpipeCreateFromFile(const char *filename);
void R_VkMeatpipeDestroy(struct vk_meatpipe_s *mp);

int R_VkMeatpipeAcquireResources(struct vk_meatpipe_s *meatpipe, int max_width, int max_height);

typedef struct {
	struct vk_combuf_s* combuf;
	uint32_t frame_sequence;

	// TODO This is kinda asking to be struct r_frame_context_t or something
	int frame_set_slot; // 0 or 1, until we do num_frame_slots
	int width, height;
	int is_discontinuous;
} vk_meatpipe_dispatch_t;

struct r_vk_image_s* R_VkMeatpipeDispatch(struct vk_meatpipe_s *mp, vk_meatpipe_dispatch_t);
