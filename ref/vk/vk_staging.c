#include "vk_staging.h"
#include "vk_buffer.h"
#include "alolcator.h"
#include "vk_commandpool.h"
#include "profiler.h"
#include "r_speeds.h"
#include "vk_combuf.h"
#include "vk_logs.h"
#include "arrays.h"

#include <memory.h>

#define MODULE_NAME "staging"
#define LOG_MODULE staging

// FIXME decrease size to something reasonable, see https://github.com/w23/xash3d-fwgs/issues/746
#define DEFAULT_STAGING_SIZE (4*128*1024*1024)

static struct {
	vk_buffer_t buffer;
	r_flipping_buffer_t buffer_alloc;

	uint32_t locked_count;
	uint32_t current_generation;

	struct {
		int total_size;
		int buffers_size;
		int images_size;
		int buffer_chunks;
		int images;
	} stats;

	int buffer_upload_scope_id;
	int image_upload_scope_id;
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	R_FlippingBuffer_Init(&g_staging.buffer_alloc, DEFAULT_STAGING_SIZE);

	R_SPEEDS_COUNTER(g_staging.stats.total_size, "total_size", kSpeedsMetricBytes);
	R_SPEEDS_COUNTER(g_staging.stats.buffers_size, "buffers_size", kSpeedsMetricBytes);
	R_SPEEDS_COUNTER(g_staging.stats.images_size, "images_size", kSpeedsMetricBytes);

	R_SPEEDS_COUNTER(g_staging.stats.buffer_chunks, "buffer_chunks", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_staging.stats.images, "images", kSpeedsMetricCount);

	g_staging.buffer_upload_scope_id = R_VkGpuScope_Register("staging_buffers");
	g_staging.image_upload_scope_id = R_VkGpuScope_Register("staging_images");

	return true;
}

void R_VkStagingShutdown(void) {
	VK_BufferDestroy(&g_staging.buffer);
}

static uint32_t allocateInRing(uint32_t size, uint32_t alignment) {
	alignment = alignment < 1 ? 1 : alignment;

	const uint32_t offset = R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment );
	ASSERT(offset != ALO_ALLOC_FAILED && "FIXME increase staging buffer size as a quick fix");

	return R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment );
}

void R_VkStagingGenerationRelease(uint32_t gen) {
	DEBUG("Release: gen=%u current_gen=%u ring offsets=[%u, %u, %u]", gen, g_staging.current_generation,
		g_staging.buffer_alloc.frame_offsets[0],
		g_staging.buffer_alloc.frame_offsets[1],
		g_staging.buffer_alloc.ring.head
	);
	R_FlippingBuffer_Flip(&g_staging.buffer_alloc);
}

uint32_t R_VkStagingGenerationCommit(void) {
	DEBUG("Commit: locked_count=%d gen=%u", g_staging.locked_count, g_staging.current_generation);
	ASSERT(g_staging.locked_count == 0);
	g_staging.stats.total_size = g_staging.stats.images_size + g_staging.stats.buffers_size;
	return g_staging.current_generation++;
}

r_vkstaging_region_t R_VkStagingLock(uint32_t size) {
	const uint32_t alignment = 4;
	const uint32_t offset = R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment);
	ASSERT(offset != ALO_ALLOC_FAILED);

	DEBUG("Lock alignment=%d size=%d region=%d..%d", alignment, size, offset, offset + size);

	g_staging.locked_count++;
	return (r_vkstaging_region_t){
		.handle.generation = g_staging.current_generation,
		.offset = offset,
		.buffer = g_staging.buffer.buffer,
		.ptr = (char*)g_staging.buffer.mapped + offset,
	};
}

void R_VkStagingUnlock(r_vkstaging_handle_t handle) {
	ASSERT(g_staging.current_generation == handle.generation);
	ASSERT(g_staging.locked_count > 0);
	g_staging.locked_count--;
}

