#pragma once

#include "vk_core.h"
#include "vk_module.h"

#include "vk_buffer.h"
#include "vk_math.h"
#include "ray_resources.h"

extern RVkModule g_module_ray_accel;

void RT_VkAccelNewMap(void);
void RT_VkAccelFrameBegin(void);

struct vk_combuf_s;
vk_resource_t RT_VkAccelPrepareTlas(struct vk_combuf_s *combuf);
