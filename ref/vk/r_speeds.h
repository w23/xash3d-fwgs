#pragma once
#include "xash3d_types.h"
#include <stdint.h>

void R_SpeedsInit( void );

struct vk_combuf_scopes_s;
void R_SpeedsDisplayMore(uint32_t prev_frame_index, const struct vk_combuf_scopes_s *gpurofl, int gpurofl_count);

// Called from the engine into ref_api to get the latest speeds info
qboolean R_SpeedsMessage( char *out, size_t size );

typedef enum {
	kSpeedsMetricCount,
	kSpeedsMetricBytes,
	kSpeedsMetricMicroseconds,
} r_speeds_metric_type_t;

// TODO upper limit argument
void R_SpeedsRegisterMetric(int* p_value, const char *module, const char *name, r_speeds_metric_type_t type, qboolean reset, const char *var_name, const char *file, int line);

// A counter is a value accumulated during a single frame, and reset to zero between frames.
// Examples: drawn models count, scope times, etc.
#define R_SPEEDS_COUNTER(var, name, type) \
	R_SpeedsRegisterMetric(&(var), MODULE_NAME, name, type, /*reset*/ true, #var, __FILE__, __LINE__)

// A metric is computed and preserved across frame boundaries.
// Examples: total allocated memory, cache sizes, etc.
#define R_SPEEDS_METRIC(var, name, type) \
	R_SpeedsRegisterMetric(&(var), MODULE_NAME, name, type, /*reset*/ false, #var, __FILE__, __LINE__)
