#include "vk_staging.h"
#include "vk_buffer.h"
#include "alolcator.h"
#include "vk_commandpool.h"
#include "profiler.h"
#include "r_speeds.h"
#include "vk_combuf.h"

#include <memory.h>

#define DEFAULT_STAGING_SIZE (128*1024*1024)
#define MAX_STAGING_ALLOCS (2048)
#define MAX_CONCURRENT_FRAMES 2
#define COMMAND_BUFFER_COUNT (MAX_CONCURRENT_FRAMES + 1) // to accommodate two frames in flight plus something trying to upload data before waiting for the next frame to complete

typedef struct {
	VkImage image;
	VkImageLayout layout;
	size_t size; // for stats only
} staging_image_t;

static struct {
	vk_buffer_t buffer;
	r_flipping_buffer_t buffer_alloc;

	struct {
		VkBuffer dest[MAX_STAGING_ALLOCS];
		VkBufferCopy copy[MAX_STAGING_ALLOCS];
		int count;
	} buffers;

	struct {
		staging_image_t dest[MAX_STAGING_ALLOCS];
		VkBufferImageCopy copy[MAX_STAGING_ALLOCS];
		int count;
	} images;

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
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	g_staging.combuf[0] = R_VkCombufOpen();
	g_staging.combuf[1] = R_VkCombufOpen();
	g_staging.combuf[2] = R_VkCombufOpen();

	R_FlippingBuffer_Init(&g_staging.buffer_alloc, DEFAULT_STAGING_SIZE);

	R_SpeedsRegisterMetric(&g_staging.stats.total_size, "staging_total_size", kSpeedsMetricBytes);
	R_SpeedsRegisterMetric(&g_staging.stats.buffers_size, "staging_buffers_size", kSpeedsMetricBytes);
	R_SpeedsRegisterMetric(&g_staging.stats.images_size, "staging_images_size", kSpeedsMetricBytes);

	R_SpeedsRegisterMetric(&g_staging.stats.buffer_chunks, "staging_buffer_chunks", kSpeedsMetricCount);
	R_SpeedsRegisterMetric(&g_staging.stats.images, "staging_images", kSpeedsMetricCount);

	return true;
}

void R_VkStagingShutdown(void) {
	VK_BufferDestroy(&g_staging.buffer);
}

// FIXME There's a severe race condition here. Submitting things manually and prematurely (before framectl had a chance to synchronize with the previous frame)
// may lead to data races and memory corruption (e.g. writing into memory that's being read in some pipeline stage still going)
void R_VkStagingFlushSync( void ) {
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
	g_staging.images.count = 0;
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

vk_staging_region_t R_VkStagingLockForImage(vk_staging_image_args_t args) {
	if ( g_staging.images.count >= MAX_STAGING_ALLOCS )
		R_VkStagingFlushSync();

	const uint32_t offset = allocateInRing(args.size, args.alignment);
	if (offset == ALO_ALLOC_FAILED)
		return (vk_staging_region_t){0};

	const int index = g_staging.images.count;
	staging_image_t *const dest = g_staging.images.dest + index;

	dest->image = args.image;
	dest->layout = args.layout;
	dest->size = args.size;
	g_staging.images.copy[index] = args.region;
	g_staging.images.copy[index].bufferOffset += offset;

	g_staging.images.count++;

	return (vk_staging_region_t){
		.ptr = (char*)g_staging.buffer.mapped + offset,
		.handle = index + MAX_STAGING_ALLOCS,
	};
}

void R_VkStagingUnlock(staging_handle_t handle) {
	ASSERT(handle >= 0);
	ASSERT(handle < MAX_STAGING_ALLOCS * 2);

	// FIXME mark and check ready
}

static void commitBuffers(void) {
	const VkCommandBuffer cmdbuf = g_staging.current->cmdbuf;

	// TODO combuf scopes

	// TODO better coalescing:
	// - upload once per buffer
	// - join adjacent regions

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
}

static void commitImages(void) {
	const VkCommandBuffer cmdbuf = g_staging.current->cmdbuf;
	for (int i = 0; i < g_staging.images.count; i++) {
		/* { */
		/* 	const VkBufferImageCopy *const copy = g_staging.images.copy + i; */
		/* 	gEngine.Con_Reportf("  i%d: [%08llx, ?) => %p\n", i, copy->bufferOffset, g_staging.images.dest[i].image); */
		/* } */

		g_staging.stats.images++;
		g_staging.stats.images_size += g_staging.images.dest[i].size;

		vkCmdCopyBufferToImage(cmdbuf, g_staging.buffer.buffer,
			g_staging.images.dest[i].image,
			g_staging.images.dest[i].layout,
			1, g_staging.images.copy + i);
	}

	g_staging.images.count = 0;
}

static vk_combuf_t *getCurrentCombuf(void) {
	if (!g_staging.current) {
		g_staging.current = g_staging.combuf[0];
		R_VkCombufBegin(g_staging.current);
	}

	return g_staging.current;
}

VkCommandBuffer R_VkStagingGetCommandBuffer(void) {
	return getCurrentCombuf()->cmdbuf;
}

vk_combuf_t *R_VkStagingCommit(void) {
	if (!g_staging.images.count && !g_staging.buffers.count && !g_staging.current)
		return VK_NULL_HANDLE;

	getCurrentCombuf();
	commitBuffers();
	commitImages();
	return g_staging.current;
}

void R_VkStagingFrameBegin(void) {
	R_FlippingBuffer_Flip(&g_staging.buffer_alloc);

	g_staging.buffers.count = 0;
	g_staging.images.count = 0;
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
