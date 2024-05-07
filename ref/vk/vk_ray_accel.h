#pragma once

#include "vk_resources.h"

qboolean RT_VkAccelInit(void);
void RT_VkAccelShutdown(void);

void RT_VkAccelNewMap(void);
void RT_VkAccelFrameBegin(void);

struct vk_combuf_s;
vk_resource_t RT_VkAccelPrepareTlas(struct vk_combuf_s *combuf);
