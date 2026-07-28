#pragma once

#include "vk_core.h"

struct vk_combuf_s;

typedef struct VPerfQuery VPerfQuery;

VPerfQuery *vPerfQueryCreate(const uint32_t *counters, uint32_t counters_count, uint32_t max_queries);
void vPerfQueryDestroy(VPerfQuery *pq);

int vPerfQueryBegin(VPerfQuery *pq, struct vk_combuf_s *cb);
void vPerfQueryEnd(VPerfQuery *pq, struct vk_combuf_s *cb, uint32_t query_index);

// TODO profile, and possibly optimize?
// Returns:
// - pointer to `counters_count` array of values
// - contents is valid only until the next vPerfQueryRead() call
const VkPerformanceCounterResultKHR* vPerfQueryRead(VPerfQuery *pq, struct vk_combuf_s *cb, uint32_t query_index);
