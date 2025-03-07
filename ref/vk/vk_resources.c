#include "vk_resources.h"
#include "vk_image.h"
#include "vk_common.h"
#include "vk_combuf.h"
#include "arrays.h"

#define LOG_MODULE rt

#include <stdlib.h>

static struct {
	ARRAY_DYNAMIC_DECLARE(rt_resource_t*, table);
} g_res;

void R_VkResourcesInit(void) {
	arrayDynamicInitT(&g_res.table);
}

rt_resource_t *R_VkResourceGetByIndex(int index) {
	ASSERT(index >= 0);
	ASSERT(index < g_res.table.count);
	return g_res.table.items[index];
}

int R_VkResourceFindIndexByName(const char *name) {
	// TODO hash table
	// Find the exact match if exists
	// There might be gaps, so we need to check everything
	for (int i = 0; i < g_res.table.count; ++i) {
		rt_resource_t *const res = g_res.table.items[i];
		if (strcmp(res->name, name) == 0)
			return i;
	}

	return -1;
}

rt_resource_t *R_VkResourceFindByName(const char *name) {
	const int index = R_VkResourceFindIndexByName(name);
	return index < 0 ? NULL : g_res.table.items[index];
}

qboolean R_VkResourceRegister(rt_resource_t *res) {
	if (R_VkResourceFindByName(res->name))
		return false;

	arrayDynamicAppendT(&g_res.table, &res);
	return true;
}

void R_VkResourcesCleanup(void) {
	for (int i = 0; i < g_res.table.count; ++i) {
		rt_resource_t *const res = g_res.table.items[i];
		if (!res->name[0] || res->refcount || !res->image.image)
			continue;

		// TODO resource dtor
		// FIXME res itself leaks
		R_VkImageDestroy(&res->image);

		// Delete item: replace it last resource into current slot
		g_res.table.items[i] = g_res.table.items[g_res.table.count-1];
		g_res.table.count--;
		g_res.table.items[g_res.table.count] = NULL;
		i--;
	}
}


void R_VkBarrierCommit(vk_combuf_t* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask) {
	if (barrier->images.count == 0 && barrier->buffers.count == 0)
		return;

	R_VkCombufIssueBarrier(combuf, (r_vkcombuf_barrier_t){
		.stage = dst_stage_mask,
		.buffers.items = barrier->buffers.items,
		.buffers.count = barrier->buffers.count,
		.images.items = barrier->images.items,
		.images.count = barrier->images.count,
	});

	// Mark as used
	barrier->images.count = 0;
	barrier->buffers.count = 0;
}

static vk_descriptor_value_t acquireDummyDescriptor(struct rt_resource_s *res, vk_resource_acquire_descriptor_args_t args) {
	(void)args;
	rt_resource_dummy_t *const dummy = (void*)res;
	return dummy->descriptor_value;
}

void R_VkResourceDummyInit(rt_resource_dummy_t *res, const char *name, VkDescriptorType type, vk_descriptor_value_t value) {
	Q_strncpy(res->header.name, name, sizeof(res->header.name));
	res->header.acquire_descriptor = acquireDummyDescriptor;
	res->header.type = type;
	res->descriptor_value = value;
}

static vk_descriptor_value_t acquireBufferResourceDescriptor(struct rt_resource_s* r, vk_resource_acquire_descriptor_args_t args) {
	vk_resource_buffer_t *const res = (void*)r;

	const r_vkcombuf_barrier_buffer_t bb = {
		.buffer = res->buffer,
		.access = args.access,
	};
	BOUNDED_ARRAY_APPEND_ITEM(args.barriers->buffers, bb);

	return (vk_descriptor_value_t) {
		.buffer = (VkDescriptorBufferInfo) {
			.buffer = res->buffer->buffer,
			.offset = res->offset,
			.range = res->size,
		}
	};
}

vk_resource_buffer_t* R_VkBufferRegisterAsResource(r_vkbuffer_register_as_resource_t args) {
	// FIXME this leaks, add dtor?
	vk_resource_buffer_t *const res = Mem_Calloc(vk_core.pool, sizeof *res);

	Q_strncpy(res->header.name, args.name, sizeof(res->header.name));
	res->header.type = args.type;
	res->header.acquire_descriptor = acquireBufferResourceDescriptor;
	res->header.refcount = 1;

	res->buffer = args.buffer;
	res->offset = args.offset;
	res->size = args.size;

	ASSERT(R_VkResourceRegister(&res->header));
	return res;
}
