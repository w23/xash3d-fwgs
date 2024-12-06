#include "vk_buffer.h"
#include "vk_logs.h"
#include "vk_combuf.h"

#include "arrays.h"

#define LOG_MODULE buf

qboolean VK_BufferCreate(const char *debug_name, vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkMemoryRequirements memreq;
	XVK_CHECK(vkCreateBuffer(vk_core.device, &bci, NULL, &buf->buffer));
	SET_DEBUG_NAME(buf->buffer, VK_OBJECT_TYPE_BUFFER, debug_name);

	vkGetBufferMemoryRequirements(vk_core.device, buf->buffer, &memreq);

	if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) {
		memreq.alignment = ALIGN_UP(memreq.alignment, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupBaseAlignment);
	}

	vk_devmem_allocate_args_t args = (vk_devmem_allocate_args_t) {
		.requirements = memreq,
		.property_flags = flags,
		.allocate_flags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0,
	};
	buf->devmem = VK_DevMemAllocateBuffer( debug_name, args );

	XVK_CHECK(vkBindBufferMemory(vk_core.device, buf->buffer, buf->devmem.device_memory, buf->devmem.offset));

	buf->mapped = buf->devmem.mapped;

	buf->size = size;

	return true;
}

void VK_BufferDestroy(vk_buffer_t *buf) {
	// FIXME destroy staging slot

	if (buf->buffer) {
		vkDestroyBuffer(vk_core.device, buf->buffer, NULL);
		buf->buffer = VK_NULL_HANDLE;
	}

	if (buf->devmem.device_memory) {
		VK_DevMemFree(&buf->devmem);
		buf->devmem.device_memory = VK_NULL_HANDLE;
		buf->devmem.offset = 0;
		buf->mapped = 0;
		buf->size = 0;
	}
}

VkDeviceAddress R_VkBufferGetDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(vk_core.device, &bdai);
}

void R_FlippingBuffer_Init(r_flipping_buffer_t *flibuf, uint32_t size) {
	aloRingInit(&flibuf->ring, size);
	R_FlippingBuffer_Clear(flibuf);
}

void R_FlippingBuffer_Clear(r_flipping_buffer_t *flibuf) {
	aloRingInit(&flibuf->ring, flibuf->ring.size);
	flibuf->frame_offsets[0] = flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}

uint32_t R_FlippingBuffer_Alloc(r_flipping_buffer_t* flibuf, uint32_t size, uint32_t align) {
	const uint32_t offset = aloRingAlloc(&flibuf->ring, size, align);
	if (offset == ALO_ALLOC_FAILED)
		return ALO_ALLOC_FAILED;

	if (flibuf->frame_offsets[1] == ALO_ALLOC_FAILED)
		flibuf->frame_offsets[1] = offset;

	return offset;
}

void R_FlippingBuffer_Flip(r_flipping_buffer_t* flibuf) {
	if (flibuf->frame_offsets[0] != ALO_ALLOC_FAILED)
		aloRingFree(&flibuf->ring, flibuf->frame_offsets[0]);

	flibuf->frame_offsets[0] = flibuf->frame_offsets[1];
	flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}

void R_DEBuffer_Init(r_debuffer_t *debuf, uint32_t static_size, uint32_t dynamic_size) {
	R_FlippingBuffer_Init(&debuf->dynamic, dynamic_size);
	debuf->static_size = static_size;
	debuf->static_offset = 0;
}

uint32_t R_DEBuffer_Alloc(r_debuffer_t* debuf, r_lifetime_t lifetime, uint32_t size, uint32_t align) {
	switch (lifetime) {
		case LifetimeDynamic:
		{
			const uint32_t offset = R_FlippingBuffer_Alloc(&debuf->dynamic, size, align);
			if (offset == ALO_ALLOC_FAILED)
				return ALO_ALLOC_FAILED;
			return offset + debuf->static_size;
		}
		case LifetimeStatic:
		{
			const uint32_t offset = ALIGN_UP(debuf->static_offset, align);
			const uint32_t end = offset + size;
			if (end > debuf->static_size)
				return ALO_ALLOC_FAILED;

			debuf->static_offset = end;
			return offset;
		}
	}

	return ALO_ALLOC_FAILED;
}

void R_DEBuffer_Flip(r_debuffer_t* debuf) {
	R_FlippingBuffer_Flip(&debuf->dynamic);
}

#define MAX_STAGING_BUFFERS 16
#define MAX_STAGING_ENTRIES 2048

// TODO this should be part of the vk_buffer_t object itself
typedef struct {
	vk_buffer_t *buffer;
	VkBuffer staging;
	BOUNDED_ARRAY_DECLARE(VkBufferCopy, regions, MAX_STAGING_ENTRIES);
} r_vk_staging_buffer_t;

// TODO remove this when staging is tracked by the buffer object itself
static struct {
	BOUNDED_ARRAY_DECLARE(r_vk_staging_buffer_t, staging, MAX_STAGING_BUFFERS);
} g_buf;

static r_vk_staging_buffer_t *findExistingStagingSlotForBuffer(vk_buffer_t *buf) {
	for (int i = 0; i < g_buf.staging.count; ++i) {
		r_vk_staging_buffer_t *const stb = g_buf.staging.items + i;
		if (stb->buffer == buf)
			return stb;
	}

	return NULL;
}

static r_vk_staging_buffer_t *findOrCreateStagingSlotForBuffer(vk_buffer_t *buf) {
	r_vk_staging_buffer_t *stb = findExistingStagingSlotForBuffer(buf);
	if (stb)
		return stb;

	ASSERT(BOUNDED_ARRAY_HAS_SPACE(g_buf.staging, 1));
	stb = &BOUNDED_ARRAY_APPEND_UNSAFE(g_buf.staging);
	stb->staging = VK_NULL_HANDLE;
	stb->buffer = buf;
	stb->regions.count = 0;
	return stb;
}

vk_buffer_locked_t R_VkBufferLock(vk_buffer_t *buf, vk_buffer_lock_t lock) {
	DEBUG("Lock buf=%p size=%d region=%d..%d", buf, lock.size, lock.offset, lock.offset + lock.size);

	r_vk_staging_buffer_t *const stb = findOrCreateStagingSlotForBuffer(buf);
	ASSERT(stb);

	r_vkstaging_region_t staging_lock = R_VkStagingLock(lock.size);
	ASSERT(staging_lock.ptr);

	// TODO perf: adjacent region coalescing

	ASSERT(BOUNDED_ARRAY_HAS_SPACE(stb->regions, 1));
	BOUNDED_ARRAY_APPEND_UNSAFE(stb->regions) = (VkBufferCopy){
		.srcOffset = staging_lock.offset,
		.dstOffset = lock.offset,
		.size = lock.size,
	};

	if (stb->staging != VK_NULL_HANDLE)
		ASSERT(stb->staging == staging_lock.buffer);
	else
		stb->staging = staging_lock.buffer;

	return (vk_buffer_locked_t) {
		.ptr = staging_lock.ptr,
		.impl_ = {
			.buf = buf,
			.handle = staging_lock.handle,
		},
	};
}

void R_VkBufferUnlock(vk_buffer_locked_t lock) {
	R_VkStagingUnlock(lock.impl_.handle);
}

void R_VkBufferStagingCommit(vk_buffer_t *buf, struct vk_combuf_s *combuf) {
	r_vk_staging_buffer_t *const stb = findExistingStagingSlotForBuffer(buf);
	if (!stb || stb->regions.count == 0)
		return;

	const r_vkcombuf_barrier_buffer_t barrier[] = {{
		.buffer = buf,
		.access = VK_ACCESS_TRANSFER_WRITE_BIT,
	}};

	R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t) {
		.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		.buffers = { barrier, COUNTOF(barrier) },
		.images = { NULL, 0 },
	});

	//FIXME const int begin_index = R_VkCombufScopeBegin(combuf, g_staging.buffer_upload_scope_id);

	const VkCommandBuffer cmdbuf = combuf->cmdbuf;
	DEBUG_NV_CHECKPOINTF(cmdbuf, "staging dst_buffer=%p count=%d", buf->buffer, stb->regions.count);
	vkCmdCopyBuffer(cmdbuf, stb->staging, buf->buffer, stb->regions.count, stb->regions.items);

	stb->regions.count = 0;

	//FIXME R_VkCombufScopeEnd(combuf, begin_index, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

