#pragma once

#include "vk_core.h"
#include "vk_descriptor.h"
#include "vk_image.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "arrays.h"

// TODO remove
#include "vk_light.h"

// TODO each of these should be registered by the provider of the resource:
#define EXTERNAL_RESOUCES(X) \
		X(TLAS, tlas) \
		X(Buffer, ubo) \
		X(Buffer, kusochki) \
		X(Buffer, model_headers) \
		X(Buffer, indices) \
		X(Buffer, vertices) \
		X(Buffer, lights) \
		X(Buffer, light_grid) \
		X(Texture, textures) \
		X(Texture, skybox) \
		X(Texture, blue_noise_texture)

enum {
#define RES_ENUM(type, name) ExternalResource_##name,
	EXTERNAL_RESOUCES(RES_ENUM)
#undef RES_ENUM
	ExternalResource_COUNT,
};

struct xvk_image_s;
typedef struct vk_resource_s {
	VkDescriptorType type;
	vk_descriptor_value_t value;
	union {
		vk_buffer_t *buffer;
		r_vk_image_t *image;
	} ref;
} vk_resource_t;

typedef struct vk_resource_s *vk_resource_p;

typedef struct {
		char name[64];
		vk_resource_t resource;
		r_vk_image_t image;
		vk_buffer_t *buffer;
		int refcount;
		int source_index_plus_1;
} rt_resource_t;

void R_VkResourcesInit(void);

rt_resource_t *R_VkResourceGetByIndex(int index);
rt_resource_t *R_VkResourceFindByName(const char *name);
rt_resource_t *R_VkResourceFindOrAlloc(const char *name);
int R_VkResourceFindIndexByName(const char *name);

// Destroys all resources with refcount = 0
void R_VkResourcesCleanup(void);

// FIXME remove this by properly registering global resources
typedef struct {
	uint32_t frame_index;

	vk_buffer_t *uniform_buffer;
	uint32_t uniform_unit_size;

	struct {
		vk_buffer_t *buffer; // must be the same as in vk_ray_model_create_t TODO: validate or make impossible to specify incorrectly
		uint64_t size;
	} geometry_data;
	const vk_lights_bindings_t *light_bindings;
} r_vk_resources_builtin_fixme_t;
void R_VkResourcesSetBuiltinFIXME(r_vk_resources_builtin_fixme_t builtin);

struct vk_combuf_s;
void R_VkResourcesFrameBeginStateChangeFIXME(struct vk_combuf_s* combuf, qboolean discontinuity);

typedef struct {
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_image_t, images, 16);
	BOUNDED_ARRAY_DECLARE(r_vkcombuf_barrier_buffer_t, buffers, 16);
} r_vk_barrier_t;

void R_VkBarrierCommit(struct vk_combuf_s* combuf, r_vk_barrier_t *barrier, VkPipelineStageFlags2 dst_stage_mask);

void R_VkResourceAddToBarrier(vk_resource_t *res, qboolean write, VkPipelineStageFlags2 dst_stage_mask, r_vk_barrier_t *barrier);
