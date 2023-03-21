#pragma once
#include "xash3d_types.h"
#include <stdint.h>

void R_SlowsInit( void );

void R_ShowExtendedProfilingData(uint32_t prev_frame_index, uint64_t gpu_frame_begin_ns, uint64_t gpu_frame_end_ns);

qboolean	R_SpeedsMessage( char *out, size_t size );
