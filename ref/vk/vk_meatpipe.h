#pragma once

#include "vk_core.h"

enum {
	MEATPIPE_RES_WRITE = (1<<0),
	MEATPIPE_RES_CREATE = (1<<1),
	// TMP ..
};

typedef struct {
	char name[64];
	uint32_t descriptor_type;
	int count;
	uint32_t flags;
	union {
		uint32_t image_format;
	};

	// Index+1 of resource image to read data from if this resource is a "previous frame" contents of another one.
	// Value of zero means that it is a standalone resource. The real index is the value - 1.
	int prev_frame_index_plus_1;
} vk_meatpipe_resource_t;

struct vk_meatpipe_pass_s;
typedef struct vk_meatpipe_s {
	int passes_count;
	struct vk_meatpipe_pass_s *passes;

	int resources_count;
	vk_meatpipe_resource_t *resources;
} vk_meatpipe_t;

struct ray_pass_s;
typedef struct vk_meatpipe_pass_s {
	struct ray_pass_s* pass;
	int write_from;
	int resource_count;
	int *resource_map;
} vk_meatpipe_pass_t;

vk_meatpipe_t* R_VkMeatpipeCreateFromFile(const char *filename);
void R_VkMeatpipeDestroy(vk_meatpipe_t *mp);
