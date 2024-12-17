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
	uint32_t locked_count;

	struct {
		int allocs;
		int size;
	} stats;
} r_vkstaging_user_t;

static struct {
	vk_buffer_t buffer;
	alo_ring_t buffer_alloc_ring;

	BOUNDED_ARRAY_DECLARE(r_vkstaging_user_t, users, MAX_STAGING_USERS);

	struct {
		int total_size;
		int total_chunks;
	} stats;

	//int buffer_upload_scope_id;
	//int image_upload_scope_id;
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	aloRingInit(&g_staging.buffer_alloc_ring, g_staging.buffer.size);

	R_SPEEDS_COUNTER(g_staging.stats.total_size, "total_size", kSpeedsMetricBytes);
	R_SPEEDS_COUNTER(g_staging.stats.total_chunks, "total_chunks", kSpeedsMetricBytes);

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

	r_vkstaging_user_t *const user = g_staging.users.items + (g_staging.users.count++);
	*user = (r_vkstaging_user_t) {
		.info = info,
	};

	char buf[64];
	snprintf(buf, sizeof(buf), "%s.size", info.name);
	R_SPEEDS_COUNTER(user->stats.size, buf, kSpeedsMetricBytes);

	snprintf(buf, sizeof(buf), "%s.allocs", info.name);
	R_SPEEDS_COUNTER(user->stats.allocs, buf, kSpeedsMetricCount);

	return user;
}

void R_VkStagingUserDestroy(r_vkstaging_user_t *user) {
	ASSERT(user->locked_count == 0);
	// TODO remove from the table
}

r_vkstaging_region_t R_VkStagingLock(r_vkstaging_user_t* user, uint32_t size) {
	const uint32_t alignment = 4;
	const uint32_t offset = aloRingAlloc(&g_staging.buffer_alloc_ring, size, alignment);
	ASSERT(offset != ALO_ALLOC_FAILED && "FIXME: workaround: increase staging buffer size");

	DEBUG("Lock alignment=%d size=%d region=%d..%d", alignment, size, offset, offset + size);

	user->locked_count++;

	user->stats.allocs++;
	user->stats.size += size;

	g_staging.stats.total_chunks++;
	g_staging.stats.total_size += size;

	return (r_vkstaging_region_t){
		.offset = offset,
		.buffer = g_staging.buffer.buffer,
		.ptr = (char*)g_staging.buffer.mapped + offset,
	};
}

void R_VkStagingUnlockBulk(r_vkstaging_user_t* user, uint32_t count) {
	ASSERT(user->locked_count >= count);
	user->locked_count -= count;
}

uint32_t R_VkStagingFrameEpilogue(vk_combuf_t* combuf) {
	for (int i = 0; i < g_staging.users.count; ++i) {
		r_vkstaging_user_t *const user = g_staging.users.items + i;
		if (user->locked_count == 0)
			continue;

		WARN("%s has %u locked staging items, pushing", user->info.name, user->locked_count);
		user->info.push(user->info.userptr, combuf, user->locked_count);
		ASSERT(user->locked_count == 0);
	}

	// TODO it would be nice to attach a finalization callback to combuf
	// So that when the combuf is done on GPU, the callback is called and we can clean its memory
	// instead of depending on framectl calling Completed function manually.

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
