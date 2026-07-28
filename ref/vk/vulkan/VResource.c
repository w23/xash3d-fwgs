#include "VResource.h"
#include "vk_common.h"
#include "VBarrier.h"
#include "std/arrays.h"

#define LOG_MODULE rt

static struct {
	ARRAY_DYNAMIC_DECLARE(rt_resource_t*, table);

	// Note that frame_sequence_tag will be shared between all dummy users
	Producer dummy_producer;
} g_res;

static void produceDummyNoop(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx) {
	(void)p;
	(void)combuf;
	(void)ctx;
}

void R_VkResourcesInit(void) {
	arrayDynamicInitT(&g_res.table);

	g_res.dummy_producer = (Producer) {
		.name = "dummy",
		.produce = produceDummyNoop,
	};
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

void R_VkResourceProduce(rt_resource_t *res, vk_combuf_t *combuf, const FrameContext *ctx) {
	ASSERT(res);
	ASSERT(res->producer);

	if (!res->producer)
		return;

	ASSERT(res->producer->produce);

	if (res->producer->frame_sequence_tag == ctx->frame_sequence)
		return;

	res->producer->produce(res->producer, combuf, ctx);
	res->producer->frame_sequence_tag = ctx->frame_sequence;
}

void R_VkResourcesCleanup(void) {
	for (int i = 0; i < g_res.table.count; ++i) {
		rt_resource_t *const res = g_res.table.items[i];
		if (!res->name[0] || res->refcount)
			continue;

		if (res->destroy)
			res->destroy(res);

		// Delete item: replace it last resource into current slot
		g_res.table.items[i] = g_res.table.items[g_res.table.count-1];
		g_res.table.count--;
		g_res.table.items[g_res.table.count] = NULL;
		i--;
	}
}

static vk_descriptor_value_t acquireDummyDescriptor(struct rt_resource_s *res, vk_resource_acquire_descriptor_args_t args) {
	(void)args;
	rt_resource_dummy_t *const dummy = (void*)res;
	return dummy->descriptor_value;
}

void R_VkResourceDummyInit(rt_resource_dummy_t *res, const char *name, VkDescriptorType type, vk_descriptor_value_t value) {
	Q_strncpy(res->header.name, name, sizeof(res->header.name));
	res->header.type = type;
	res->header.acquire_descriptor = acquireDummyDescriptor;
	res->header.producer = &g_res.dummy_producer;
	res->descriptor_value = value;
}

static vk_descriptor_value_t acquireBufferResourceDescriptor(struct rt_resource_s* r, vk_resource_acquire_descriptor_args_t args) {
	vk_resource_buffer_t *const res = (void*)r;

	barrierAddBuffer(args.barriers, (r_vkcombuf_barrier_buffer_t) {
		.buffer = res->buffer,
		.access = args.access,
	});

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
	res->header.producer = args.producer;

	res->buffer = args.buffer;
	res->offset = args.offset;
	res->size = args.size;

	ASSERT(R_VkResourceRegister(&res->header));
	return res;
}
