#pragma once
#include "vk_common.h"
#include "r_block.h"
#include "vk_core.h"

#include <stdint.h>

// General buffer usage pattern
// 1. alloc (allocates buffer mem, stores allocation data)
// 2. (returns void* buf and handle) write to buf
// 3. upload and lock (ensures that all this data is in gpu mem, e.g. uploads from staging)
// 4. ... use it
// 5. free (frame/map end)

// TODO is this a good place?
typedef struct vk_vertex_s {
	// TODO padding needed for storage buffer reading, figure out how to fix in GLSL/SPV side
	vec3_t pos; float pad0_;
	vec3_t prev_pos; float pad1_;
	vec3_t normal; uint32_t pad2_;
	vec3_t tangent; uint32_t pad3_;
	vec2_t gl_tc; //float p2_[2];
	vec2_t lm_tc; //float p3_[2];
	rgba_t color;
	float pad4_[3];
} vk_vertex_t;

typedef struct {
	struct {
		int count, unit_offset;
	} vertices;

	struct {
		int count, unit_offset;
	} indices;

	r_block_t block_handle;
} r_geometry_range_t;

// Allocates a range in geometry buffer with a long lifetime
r_geometry_range_t R_GeometryRangeAlloc(int vertices, int indices);
void R_GeometryRangeFree(const r_geometry_range_t*);

typedef struct {
	vk_vertex_t *vertices;
	uint16_t *indices;

	struct {
		int staging_handle;
	} impl_;
} r_geometry_range_lock_t;

// Lock staging memory for uploading
r_geometry_range_lock_t R_GeometryRangeLock(const r_geometry_range_t *range);
r_geometry_range_lock_t R_GeometryRangeLockSubrange(const r_geometry_range_t *range, int vertices_offset, int vertices_count );
void R_GeometryRangeUnlock(const r_geometry_range_lock_t *lock);

typedef struct {
	struct {
		vk_vertex_t *ptr;
		int count;
		int unit_offset;
	} vertices;

	struct {
		uint16_t *ptr;
		int count;
		int unit_offset;
	} indices;

	struct {
		int staging_handle;
	} impl_;
} r_geometry_buffer_lock_t;

typedef enum {
	LifetimeLong,
	LifetimeSingleFrame
} r_geometry_lifetime_t;

qboolean R_GeometryBufferAllocOnceAndLock(r_geometry_buffer_lock_t *lock, int vertex_count, int index_count);
void R_GeometryBufferUnlock( const r_geometry_buffer_lock_t *lock );

void R_GeometryBuffer_MapClear( void ); // Free the entire buffer for a new map

qboolean R_GeometryBuffer_Init(void);
void R_GeometryBuffer_Shutdown(void);

void R_GeometryBuffer_Flip(void);

// FIXME is there a better way?
VkBuffer R_GeometryBuffer_Get(void);

