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
#define MAX_STAGING_ALLOCS (2048)
#define MAX_CONCURRENT_FRAMES 2
#define COMMAND_BUFFER_COUNT (MAX_CONCURRENT_FRAMES + 1) // to accommodate two frames in flight plus something trying to upload data before waiting for the next frame to complete

static struct {
	vk_buffer_t buffer;
	r_flipping_buffer_t buffer_alloc;

	struct {
		VkBuffer dest[MAX_STAGING_ALLOCS];
		VkBufferCopy copy[MAX_STAGING_ALLOCS];
		int count;
	} buffers;

	vk_combuf_t *combuf[3];

	// Currently opened command buffer, ready to accept new commands
	vk_combuf_t *current;

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

	g_staging.combuf[0] = R_VkCombufOpen();
	g_staging.combuf[1] = R_VkCombufOpen();
	g_staging.combuf[2] = R_VkCombufOpen();

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

// FIXME There's a severe race condition here. Submitting things manually and prematurely (before framectl had a chance to synchronize with the previous frame)
// may lead to data races and memory corruption (e.g. writing into memory that's being read in some pipeline stage still going)
void R_VkStagingFlushSync( void ) {
	ASSERT(!"SHOULD NEVER HAPPEN");
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);

	vk_combuf_t *combuf = R_VkStagingCommit();
	if (!combuf)
		goto end;

	R_VkCombufEnd(combuf);
	g_staging.current = NULL;

	//gEngine.Con_Reportf(S_WARN "flushing staging buffer img count=%d\n", g_staging.images.count);

	{
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &combuf->cmdbuf,
		};

		// TODO wait for previous command buffer completion. Why: we might end up writing into the same dst

		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));

		// TODO wait for fence, not this
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
	}

	g_staging.buffers.count = 0;
	R_FlippingBuffer_Clear(&g_staging.buffer_alloc);

end:
	APROF_SCOPE_END(function);
};

static uint32_t allocateInRing(uint32_t size, uint32_t alignment) {
	alignment = alignment < 1 ? 1 : alignment;

	const uint32_t offset = R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment );
	if (offset != ALO_ALLOC_FAILED)
		return offset;

	R_VkStagingFlushSync();

	return R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment );
}

vk_staging_region_t R_VkStagingLockForBuffer(vk_staging_buffer_args_t args) {
	if ( g_staging.buffers.count >= MAX_STAGING_ALLOCS )
		R_VkStagingFlushSync();

	const uint32_t offset = allocateInRing(args.size, args.alignment);
	if (offset == ALO_ALLOC_FAILED)
		return (vk_staging_region_t){0};

	DEBUG("Lock buf alignment=%d size=%d region=%d..%d", args.alignment, args.size, offset, offset + args.size);

	const int index = g_staging.buffers.count;

	g_staging.buffers.dest[index] = args.buffer;
	g_staging.buffers.copy[index] = (VkBufferCopy){
		.srcOffset = offset,
		.dstOffset = args.offset,
		.size = args.size,
	};

	g_staging.buffers.count++;

	return (vk_staging_region_t){
		.ptr = (char*)g_staging.buffer.mapped + offset,
		.handle = index,
	};
}

void R_VkStagingUnlock(r_vkstaging_handle_t handle) {
	ASSERT(handle >= 0);
	ASSERT(handle < MAX_STAGING_ALLOCS * 2);

	// FIXME mark and check ready
}

static void commitBuffers(vk_combuf_t *combuf) {
	if (!g_staging.buffers.count)
		return;

	const VkCommandBuffer cmdbuf = g_staging.current->cmdbuf;
	const int begin_index = R_VkCombufScopeBegin(combuf, g_staging.buffer_upload_scope_id);

	// TODO better coalescing:
	// - upload once per buffer
	// - join adjacent regions

	BOUNDED_ARRAY(VkBufferMemoryBarrier, barriers, 4);

	for (int i = 0; i < g_staging.buffers.count; i++) {
		const VkBuffer dst_buf = g_staging.buffers.dest[i];
		for (int j = 0;; ++j) {
			if (j == COUNTOF(barriers.items)) {
				ERR("Ran out of buffer barrier slots, oh no");
				break;
			}

			// Instert last
			if (j == barriers.count) {
				barriers.count++;
				barriers.items[j] = (VkBufferMemoryBarrier){
					.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.buffer = dst_buf,
					.offset = 0,
					.size = VK_WHOLE_SIZE,
				};
				break;
			}

			// Already inserted
			if (barriers.items[j].buffer == dst_buf)
				break;
		}
	}

	if (barriers.count) {
		vkCmdPipelineBarrier(cmdbuf,
			// FIXME this should be more concrete. Will need to pass buffer "state" around.
			// For now it works, but makes validation uhappy.
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, barriers.count, barriers.items, 0, NULL);
	}

	VkBuffer prev_buffer = VK_NULL_HANDLE;
	int first_copy = 0;
	for (int i = 0; i < g_staging.buffers.count; i++) {
		/* { */
		/* 	const VkBufferCopy *const copy = g_staging.buffers.copy + i; */
		/* 	gEngine.Con_Reportf("  %d: [%08llx, %08llx) => [%08llx, %08llx)\n", i, copy->srcOffset, copy->srcOffset + copy->size, copy->dstOffset, copy->dstOffset + copy->size); */
		/* } */

		if (prev_buffer == g_staging.buffers.dest[i])
			continue;

		if (prev_buffer != VK_NULL_HANDLE) {
			DEBUG_NV_CHECKPOINTF(cmdbuf, "staging dst_buffer=%p count=%d", prev_buffer, i-first_copy);
			g_staging.stats.buffer_chunks++;
			vkCmdCopyBuffer(cmdbuf, g_staging.buffer.buffer,
				prev_buffer,
				i - first_copy, g_staging.buffers.copy + first_copy);
		}

		g_staging.stats.buffers_size += g_staging.buffers.copy[i].size;

		prev_buffer = g_staging.buffers.dest[i];
		first_copy = i;
	}

	if (prev_buffer != VK_NULL_HANDLE) {
		DEBUG_NV_CHECKPOINTF(cmdbuf, "staging dst_buffer=%p count=%d", prev_buffer, g_staging.buffers.count-first_copy);
		g_staging.stats.buffer_chunks++;
		vkCmdCopyBuffer(cmdbuf, g_staging.buffer.buffer,
			prev_buffer,
			g_staging.buffers.count - first_copy, g_staging.buffers.copy + first_copy);
	}

	g_staging.buffers.count = 0;

	R_VkCombufScopeEnd(combuf, begin_index, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

static vk_combuf_t *getCurrentCombuf(void) {
	if (!g_staging.current) {
		g_staging.current = g_staging.combuf[0];
		R_VkCombufBegin(g_staging.current);
	}

	return g_staging.current;
}

vk_combuf_t *R_VkStagingCommit(void) {
	DEBUG("%s: buffers.count=%d current=%p", __FUNCTION__, g_staging.buffers.count, g_staging.current);

	if (!g_staging.buffers.count && !g_staging.current)
		return VK_NULL_HANDLE;

	getCurrentCombuf();
	commitBuffers(g_staging.current);
	return g_staging.current;
}

void R_VkStagingFrameBegin(void) {
	R_VkStagingCommit(); // .... ugh

	R_FlippingBuffer_Flip(&g_staging.buffer_alloc);

	g_staging.buffers.count = 0;
}

vk_combuf_t *R_VkStagingFrameEnd(void) {
	R_VkStagingCommit();
	vk_combuf_t *current = g_staging.current;

	if (current) {
		R_VkCombufEnd(g_staging.current);
	}

	g_staging.current = NULL;
	vk_combuf_t *const tmp = g_staging.combuf[0];
	g_staging.combuf[0] = g_staging.combuf[1];
	g_staging.combuf[1] = g_staging.combuf[2];
	g_staging.combuf[2] = tmp;

	g_staging.stats.total_size = g_staging.stats.images_size + g_staging.stats.buffers_size;

	return current;
}

r_vkstaging_region_t R_VkStagingLock(uint32_t size) {
	const uint32_t alignment = 4;
	const uint32_t offset = R_FlippingBuffer_Alloc(&g_staging.buffer_alloc, size, alignment);
	ASSERT(offset != ALO_ALLOC_FAILED);

	DEBUG("Lock alignment=%d size=%d region=%d..%d", alignment, size, offset, offset + size);

	return (r_vkstaging_region_t){
		.handle = 31337, // FAKE
		.offset = offset,
		.buffer = g_staging.buffer.buffer,
		.ptr = (char*)g_staging.buffer.mapped + offset,
	};
}

void R_VkStagingReleaseAfterNextFrame(r_vkstaging_handle_t handle) {
	// FIXME
}
