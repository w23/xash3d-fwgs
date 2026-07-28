#pragma once

#include "xash3d_types.h" // qboolean

#include <stdint.h> // uint32_t

typedef struct rt_kusochki_s {
	uint32_t offset;
	int count;
	int internal_index__;
} rt_kusochki_t;

qboolean RT_KusochkiInit(void);
void RT_KusochkiShutdown(void);

void RT_KusochkiClear(void);

// TODO producer->consumed
void RT_KusochkiFlip(void);

rt_kusochki_t RT_KusochkiAllocLong(int count);
uint32_t RT_KusochkiAllocOnce(int count);
void RT_KusochkiFree(const rt_kusochki_t *kusochki);

struct vk_render_geometry_s;
struct r_vk_material_s;
qboolean RT_KusochkiUpload(uint32_t kusochki_offset, const struct vk_render_geometry_s *geoms, int geoms_count, const struct r_vk_material_s *override_material, const vec4_t *override_colors);
