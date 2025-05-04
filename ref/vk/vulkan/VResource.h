#pragma once

#include "VDescriptor.h"
#include "VImage.h"
#include "VBuffer.h"
#include "VCombuf.h" // r_vkcombuf_barrier_buffer_t

typedef struct FrameContext {
	uint32_t frame_sequence;
} FrameContext;

struct Producer;
typedef void (ProducerProduceFunc)(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx);
typedef struct Producer {
	char name[64];
	ProducerProduceFunc *produce;
	// TODO function to tell that previously produced data has been consumed and can be freed.
	// This is an alternative to FrameBegin/End functions
	// Valuable for anything that has dynamic data, ring/debuffers, etc:
	// - kusochki
	// - geometry
	// - ...
	// ProducerConsumedFunc *consumed;

	// Used by visitor for calling `produce()` only once per frame
	uint32_t frame_sequence_tag;
} Producer;

typedef struct vk_resource_acquire_descriptor_args_s {
	struct vk_combuf_s *combuf;
	struct Barrier *barriers;
	VkAccessFlags2 access;
	VkImageLayout image_layout;
} vk_resource_acquire_descriptor_args_t;

struct rt_resource_s;
typedef void (vk_resource_dtor_f)(struct rt_resource_s*);
typedef vk_descriptor_value_t (vk_resource_acquire_descriptor_f)(struct rt_resource_s*, vk_resource_acquire_descriptor_args_t);

typedef struct rt_resource_s {
	char name[64];
	VkDescriptorType type;
	vk_resource_dtor_f *destroy;
	vk_resource_acquire_descriptor_f *acquire_descriptor;

	Producer *producer;

	// Used for tracking meatpipe resources when reloading meatpipes
	int refcount;
} rt_resource_t;


void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceFindByName(const char *name);
qboolean R_VkResourceRegister(rt_resource_t *res);

void R_VkResourceProduce(rt_resource_t *res, vk_combuf_t *combuf, const FrameContext *ctx);

// TODO remove these when ping-pong resource is a dedicated type of resource
rt_resource_t *R_VkResourceGetByIndex(int index);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);


// Dummy resource that just returns `descriptor_value` without doing anything else
typedef struct rt_resource_dummy_s {
	rt_resource_t header;
	vk_descriptor_value_t descriptor_value;
} rt_resource_dummy_t;

void R_VkResourceDummyInit(rt_resource_dummy_t *res, const char *name, VkDescriptorType, vk_descriptor_value_t);


typedef struct vk_resource_buffer_t {
	rt_resource_t header;
	vk_buffer_t *buffer;
	size_t offset;
	size_t size;
} vk_resource_buffer_t;

typedef struct {
	const char *name;
	VkDescriptorType type;
	vk_buffer_t *buffer;
	size_t offset;
	size_t size;
	Producer *producer;
} r_vkbuffer_register_as_resource_t;

vk_resource_buffer_t *R_VkBufferRegisterAsResource(r_vkbuffer_register_as_resource_t args);
