#include "vk_staging.h"

#include "vk_buffer.h"
#include "vk_combuf.h"
#include "vk_logs.h"
#include "r_speeds.h"

#include "alolcator.h"
#include "arrays.h"

#include <memory.h>

#define MODULE_NAME "staging"
#define LOG_MODULE staging

// FIXME decrease size to something reasonable, see https://github.com/w23/xash3d-fwgs/issues/746
#define DEFAULT_STAGING_SIZE (4*128*1024*1024)

#define MAX_STAGING_USERS 8

typedef struct r_vkstaging_user_t {
	r_vkstaging_user_create_t info;
	uint32_t pending_count;

	struct {
		uint32_t allocs;
		uint32_t size;
	} stats;
} r_vkstaging_user_t;

static struct {
	vk_buffer_t buffer;
	alo_ring_t buffer_alloc_ring;

	BOUNDED_ARRAY_DECLARE(r_vkstaging_user_t, users, MAX_STAGING_USERS);

	struct {
		int total_size;
		int total_chunks;
		//int buffers_size;
		//int images_size;
		//int buffer_chunks;
		//int images;
	} stats;

	//int buffer_upload_scope_id;
	//int image_upload_scope_id;
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	aloRingInit(&g_staging.buffer_alloc_ring, g_staging.buffer.size);

	R_SPEEDS_COUNTER(g_staging.stats.total_size, "total_size", kSpeedsMetricBytes);
	R_SPEEDS_COUNTER(g_staging.stats.total_chunks, "total_chunks", kSpeedsMetricBytes);

	//R_SPEEDS_COUNTER(g_staging.stats.buffers_size, "buffers_size", kSpeedsMetricBytes);
	//R_SPEEDS_COUNTER(g_staging.stats.images_size, "images_size", kSpeedsMetricBytes);

	//R_SPEEDS_COUNTER(g_staging.stats.buffer_chunks, "buffer_chunks", kSpeedsMetricCount);
	//R_SPEEDS_COUNTER(g_staging.stats.images, "images", kSpeedsMetricCount);

	//g_staging.buffer_upload_scope_id = R_VkGpuScope_Register("staging_buffers");
	//g_staging.image_upload_scope_id = R_VkGpuScope_Register("staging_images");

	return true;
}

void R_VkStagingShutdown(void) {
	// TODO ASSERT(g_staging.users.count == 0);
	VK_BufferDestroy(&g_staging.buffer);
}

r_vkstaging_user_t *R_VkStagingUserCreate(r_vkstaging_user_create_t info) {
	ASSERT(g_staging.users.count < MAX_STAGING_USERS);
	g_staging.users.items[g_staging.users.count] = (r_vkstaging_user_t) {
		.info = info,
	};

	// TODO register counters

	return g_staging.users.items + (g_staging.users.count++);
}

void R_VkStagingUserDestroy(r_vkstaging_user_t *user) {
	ASSERT(user->pending_count == 0);
	// TODO destroy
}

r_vkstaging_region_t R_VkStagingAlloc(r_vkstaging_user_t* user, uint32_t size) {
	const uint32_t alignment = 4;
	const uint32_t offset = aloRingAlloc(&g_staging.buffer_alloc_ring, size, alignment);
	ASSERT(offset != ALO_ALLOC_FAILED && "FIXME: workaround: increase staging buffer size");

	DEBUG("Lock alignment=%d size=%d region=%d..%d", alignment, size, offset, offset + size);

	user->pending_count++;

	user->stats.allocs++;
	user->stats.size += size;

	return (r_vkstaging_region_t){
		.offset = offset,
		.buffer = g_staging.buffer.buffer,
		.ptr = (char*)g_staging.buffer.mapped + offset,
	};
}

void R_VkStagingMarkFree(r_vkstaging_user_t* user, uint32_t count) {
	ASSERT(user->pending_count >= count);
	user->pending_count -= count;
}

uint32_t R_VkStagingFrameEpilogue(vk_combuf_t* combuf) {
	for (int i = 0; i < g_staging.users.count; ++i) {
		r_vkstaging_user_t *const user = g_staging.users.items + i;
		if (user->pending_count == 0)
			continue;

		WARN("%s has %u pending staging items, pushing", user->info.name, user->pending_count);
		user->info.push(user->info.userptr, combuf, user->pending_count);
		ASSERT(user->pending_count == 0);
	}

	return g_staging.buffer_alloc_ring.head;
}

void R_VkStagingFrameCompleted(uint32_t frame_boundary_addr) {
	// Note that these stats are for latest frame, not the one for which the frame boundary is.
	g_staging.stats.total_size = 0;
	g_staging.stats.total_chunks = 0;

	for (int i = 0; i < g_staging.users.count; ++i) {
		r_vkstaging_user_t *const user = g_staging.users.items + i;
		user->stats.allocs = 0;
		user->stats.size = 0;
	}

	aloRingFree(&g_staging.buffer_alloc_ring, frame_boundary_addr);
}
