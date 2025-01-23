#pragma once

#include "vk_resources.h"

qboolean RT_VkAccelInit(void);
void RT_VkAccelShutdown(void);

void RT_VkAccelNewMap(void);

struct vk_combuf_s;
vk_resource_t RT_VkAccelPrepareTlas(struct vk_combuf_s *combuf);

typedef struct rt_draw_instance_t {
	struct rt_blas_s *blas;
	uint32_t kusochki_offset;
	matrix3x4 transform_row;
	matrix4x4 prev_transform_row;
	vec4_t color;
	uint32_t material_mode; // MATERIAL_MODE_ from ray_interop.h
	uint32_t material_flags; // material_flag_bits_e
} rt_draw_instance_t;

void RT_VkAccelAddDrawInstance(const rt_draw_instance_t*);
