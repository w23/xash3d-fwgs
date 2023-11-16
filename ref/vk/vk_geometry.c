#include "vk_geometry.h"
#include "vk_buffer.h"
#include "vk_staging.h"
#include "r_speeds.h"

#define MODULE_NAME "geom"

#define MAX_BUFFER_VERTICES_STATIC (128 * 1024)
#define MAX_BUFFER_INDICES_STATIC (MAX_BUFFER_VERTICES_STATIC * 3)
#define GEOMETRY_BUFFER_STATIC_SIZE ALIGN_UP(MAX_BUFFER_VERTICES_STATIC * sizeof(vk_vertex_t) + MAX_BUFFER_INDICES_STATIC * sizeof(uint16_t), sizeof(vk_vertex_t))

#define MAX_BUFFER_VERTICES_DYNAMIC (128 * 1024 * 2)
#define MAX_BUFFER_INDICES_DYNAMIC (MAX_BUFFER_VERTICES_DYNAMIC * 3)
#define GEOMETRY_BUFFER_DYNAMIC_SIZE ALIGN_UP(MAX_BUFFER_VERTICES_DYNAMIC * sizeof(vk_vertex_t) + MAX_BUFFER_INDICES_DYNAMIC * sizeof(uint16_t), sizeof(vk_vertex_t))

#define GEOMETRY_BUFFER_SIZE (GEOMETRY_BUFFER_STATIC_SIZE + GEOMETRY_BUFFER_DYNAMIC_SIZE)

// TODO profiler counters

static struct {
	vk_buffer_t buffer;
	r_blocks_t alloc;

	struct {
		int vertices, indices;
		int dyn_vertices, dyn_indices;
	} stats;
} g_geom;

r_geometry_range_t R_GeometryRangeAlloc(int vertices, int indices) {
	const uint32_t vertices_size = vertices * sizeof(vk_vertex_t);
	const uint32_t indices_size = indices * sizeof(uint16_t);
	const uint32_t total_size = vertices_size + indices_size;

	r_geometry_range_t ret = {
		.block_handle = R_BlockAllocLong(&g_geom.alloc, total_size, sizeof(vk_vertex_t)),
	};

	if (!ret.block_handle.size)
		return ret;

	ret.vertices.unit_offset = ret.block_handle.offset / sizeof(vk_vertex_t);
	ret.indices.unit_offset = (ret.block_handle.offset + vertices_size) / sizeof(uint16_t);

	ret.vertices.count = vertices;
	ret.indices.count = indices;

	g_geom.stats.indices += indices;
	g_geom.stats.vertices += vertices;

	return ret;
}

void R_GeometryRangeFree(const r_geometry_range_t* range) {
	R_BlockRelease(&range->block_handle);

	g_geom.stats.indices -= range->indices.count;
	g_geom.stats.vertices -= range->vertices.count;
}

r_geometry_range_lock_t R_GeometryRangeLock(const r_geometry_range_t *range) {
	const vk_staging_buffer_args_t staging_args = {
		.buffer = g_geom.buffer.buffer,
		.offset = range->block_handle.offset,
		.size = range->block_handle.size,
		.alignment = 4,
	};

	const vk_staging_region_t staging = R_VkStagingLockForBuffer(staging_args);
	ASSERT(staging.ptr);

	const uint32_t vertices_size = range->vertices.count * sizeof(vk_vertex_t);

	ASSERT( range->block_handle.offset % sizeof(vk_vertex_t) == 0 );
	ASSERT( (range->block_handle.offset + vertices_size) % sizeof(uint16_t) == 0 );

	return (r_geometry_range_lock_t){
		.vertices = (vk_vertex_t *)staging.ptr,
		.indices = (uint16_t *)((char*)staging.ptr + vertices_size),
		.impl_ = {
			.staging_handle = staging.handle,
		},
	};
}

r_geometry_range_lock_t R_GeometryRangeLockSubrange(const r_geometry_range_t *range, int vertices_offset, int vertices_count ) {
	const vk_staging_buffer_args_t staging_args = {
		.buffer = g_geom.buffer.buffer,
		.offset = range->block_handle.offset + sizeof(vk_vertex_t) * vertices_offset,
		.size = sizeof(vk_vertex_t) * vertices_count,
		.alignment = 4,
	};

	ASSERT(staging_args.offset >= range->block_handle.offset);
	ASSERT(staging_args.offset + staging_args.size <= range->block_handle.offset + range->block_handle.size);

	const vk_staging_region_t staging = R_VkStagingLockForBuffer(staging_args);
	ASSERT(staging.ptr);

	ASSERT( range->block_handle.offset % sizeof(vk_vertex_t) == 0 );

	return (r_geometry_range_lock_t){
		.vertices = (vk_vertex_t *)staging.ptr,
		.indices = NULL,
		.impl_ = {
			.staging_handle = staging.handle,
		},
	};
}

void R_GeometryRangeUnlock(const r_geometry_range_lock_t *lock) {
	R_VkStagingUnlock(lock->impl_.staging_handle);
}

qboolean R_GeometryBufferAllocOnceAndLock(r_geometry_buffer_lock_t *lock, int vertex_count, int index_count) {
	const uint32_t vertices_size = vertex_count * sizeof(vk_vertex_t);
	const uint32_t indices_size = index_count * sizeof(uint16_t);
	const uint32_t total_size = vertices_size + indices_size;

	const uint32_t offset = R_BlockAllocOnce(&g_geom.alloc, total_size, sizeof(vk_vertex_t));

	if (offset == ALO_ALLOC_FAILED) {
		/* gEngine.Con_Printf(S_ERROR "Cannot allocate %s geometry buffer for %d vertices (%d bytes) and %d indices (%d bytes)\n", */
		/* 	lifetime == LifetimeSingleFrame ? "dynamic" : "static", */
		/* 	vertex_count, vertices_size, index_count, indices_size); */
		return false;
	}

	{
		const uint32_t vertices_offset = offset / sizeof(vk_vertex_t);
		const uint32_t indices_offset = (offset + vertices_size) / sizeof(uint16_t);
		const vk_staging_buffer_args_t staging_args = {
			.buffer = g_geom.buffer.buffer,
			.offset = offset,
			.size = total_size,
			.alignment = 4,
		};

		const vk_staging_region_t staging = R_VkStagingLockForBuffer(staging_args);
		ASSERT(staging.ptr);

		ASSERT( offset % sizeof(vk_vertex_t) == 0 );
		ASSERT( (offset + vertices_size) % sizeof(uint16_t) == 0 );

		*lock = (r_geometry_buffer_lock_t) {
			.vertices = {
				.count = vertex_count,
				.ptr = (vk_vertex_t *)staging.ptr,
				.unit_offset = vertices_offset,
			},
			.indices = {
				.count = index_count,
				.ptr = (uint16_t *)((char*)staging.ptr + vertices_size),
				.unit_offset = indices_offset,
			},
			.impl_ = {
				.staging_handle = staging.handle,
			},
		};
	}

	g_geom.stats.dyn_vertices += vertex_count;
	g_geom.stats.dyn_indices += index_count;

	return true;
}

void R_GeometryBufferUnlock( const r_geometry_buffer_lock_t *lock ) {
	R_VkStagingUnlock(lock->impl_.staging_handle);
}

void R_GeometryBuffer_MapClear( void ) {
	// Obsolete, don't really need to do anything
	// TODO for diag/debug reasons we might want to check that there are no leaks, i.e.
	// allocated blocks count remains constant and doesn't grow between maps
}

qboolean R_GeometryBuffer_Init(void) {
	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!VK_BufferCreate("geometry buffer", &g_geom.buffer, GEOMETRY_BUFFER_SIZE,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (vk_core.rtx ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0),
		(vk_core.rtx ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0)))
		return false;

#define EXPECTED_ALLOCS 1024
	R_BlocksCreate(&g_geom.alloc, GEOMETRY_BUFFER_SIZE, GEOMETRY_BUFFER_DYNAMIC_SIZE, EXPECTED_ALLOCS);

	R_SPEEDS_METRIC(g_geom.alloc.allocated_long, "used", kSpeedsMetricBytes);
	R_SPEEDS_METRIC(g_geom.stats.vertices, "vertices", kSpeedsMetricCount);
	R_SPEEDS_METRIC(g_geom.stats.indices, "indices", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_geom.stats.dyn_vertices, "dyn_vertices", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_geom.stats.dyn_indices, "dyn_indices", kSpeedsMetricCount);
	return true;
}

void R_GeometryBuffer_Shutdown(void) {
	R_BlocksDestroy(&g_geom.alloc);
	VK_BufferDestroy( &g_geom.buffer );
}

void R_GeometryBuffer_Flip(void) {
	R_BlocksClearOnce(&g_geom.alloc);
}

VkBuffer R_GeometryBuffer_Get(void) {
	return g_geom.buffer.buffer;
}
