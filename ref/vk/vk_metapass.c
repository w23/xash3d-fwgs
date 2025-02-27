#include "vk_metapass.h"

#include "vk_meatpipe.h"
#include "vk_resources.h"
#include "vk_logs.h"
#include "vk_combuf.h" // r_vkcombuf_barrier_buffer_t
#include "ray_pass.h"

#include "profiler.h"

#include "arrays.h"

#define LOG_MODULE rt

typedef struct Metapass {
	vk_meatpipe_t *meatpipe;

	// Helper list of resource pointers to global resource map
	// Needed as an argument to `R_VkMeatpipePerform()` so that meatpipe can access resources
	vk_resource_p *resources;

	// Pointer to the `dest` image produced by meatpipe
	// TODO this should be a regular registered resource, nothing special about it
	rt_resource_t *dest;
} Metapass;

struct Metapass *Metapass_Create(struct vk_meatpipe_s *meatpipe, int max_width, int max_height) {
	const size_t newpipe_resources_size = sizeof(vk_resource_p) * meatpipe->resources_count;
	vk_resource_p *newpipe_resources = Mem_Calloc(vk_core.pool, newpipe_resources_size);
	rt_resource_t *newpipe_out = NULL;

	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		DEBUG("res %d/%d: %s descriptor=%u count=%d flags=[%c%c] image_format=(%s)%u",
			i, meatpipe->resources_count, mr->name, mr->descriptor_type, mr->count,
			(mr->flags & MEATPIPE_RES_WRITE) ? 'W' : ' ',
			(mr->flags & MEATPIPE_RES_CREATE) ? 'C' : ' ',
			R_VkFormatName(mr->image_format),
			mr->image_format);

		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);

		if (create && mr->descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			ERR("Only storage image creation is supported for meatpipes");
			goto fail;
		}

		// TODO this should be specified as a flag, from rt.json
		const qboolean output = Q_strcmp("dest", mr->name) == 0;

		rt_resource_t *const res = create ? R_VkResourceFindOrAlloc(mr->name) : R_VkResourceFindByName(mr->name);
		if (!res) {
			ERR("Couldn't find resource/slot for %s", mr->name);
			goto fail;
		}

		if (output)
			newpipe_out = res;

		if (create) {
			const qboolean is_compatible = (res->image.image != VK_NULL_HANDLE)
				&& (mr->image_format == res->image.format)
				&& (max_width <= res->image.width)
				&& (max_height <= res->image.height);

			if (!is_compatible) {
				if (res->image.image != VK_NULL_HANDLE)
					R_VkImageDestroy(&res->image);

				const r_vk_image_create_t create = {
					.debug_name = mr->name,
					.width = max_width,
					.height = max_height,
					.depth = 1,
					.mips = 1,
					.layers = 1,
					.format = mr->image_format,
					.tiling = VK_IMAGE_TILING_OPTIMAL,
					// TODO figure out how to detect this need properly. prev_dest is not defined as "output"
					//.usage = VK_IMAGE_USAGE_STORAGE_BIT | (output ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
					.usage = VK_IMAGE_USAGE_STORAGE_BIT
						//| VK_IMAGE_USAGE_SAMPLED_BIT // required by VK_IMAGE_LAYOUT_SHADER_READ_OPTIMAL
						| VK_IMAGE_USAGE_TRANSFER_SRC_BIT
						| VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					.flags = 0,
				};
				res->image = R_VkImageCreate(&create);
			}
		}

		newpipe_resources[i] = &res->resource;

		if (create) {
			if (mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
				newpipe_resources[i]->ref.image = &res->image;
			}

			// TODO full r/w initialization
			// FIXME not sure if not needed res->resource.deprecate.write.pipelines = 0;
			res->resource.type = mr->descriptor_type;
		} else {
			// TODO no assert, complain and exit
			// can't do before all resources are properly registered by their producers and not all this temp crap we have right now
			// ASSERT(res->resource.type == mr->descriptor_type);
		}
	}

	if (!newpipe_out) {
		ERR("New rt.json doesn't define an 'dest' output texture");
		goto fail;
	}

	// Resolve prev_ frame resources
	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		if (mr->prev_frame_index_plus_1 <= 0)
			continue;

		ASSERT(mr->prev_frame_index_plus_1 < meatpipe->resources_count);

		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);

		const vk_meatpipe_resource_t *pr = meatpipe->resources + (mr->prev_frame_index_plus_1 - 1);

		const int dest_index = R_VkResourceFindIndexByName(pr->name);
		if (dest_index < 0) {
			ERR("Couldn't find prev_ resource/slot %s for resource %s", pr->name, mr->name);
			goto fail;
		}

		res->source_index_plus_1 = dest_index + 1;
	}

	// Loading successful
	// Update refcounts
	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);
		res->refcount++;
	}

	Metapass *ret = Mem_Malloc(vk_core.pool, sizeof(*ret));
	ret->meatpipe = meatpipe;
	ret->resources = newpipe_resources;
	ret->dest = newpipe_out;

	return ret;

fail:
	R_VkResourcesCleanup();

	if (newpipe_resources)
		Mem_Free(newpipe_resources);

	return NULL;
}

void Metapass_Destroy(Metapass *mp) {
	if (!mp)
		return;

	ASSERT(mp->resources);

	for (int i = 0; i < mp->meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = mp->meatpipe->resources + i;
		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);
		ASSERT(res->refcount > 0);
		res->refcount--;
	}

	R_VkResourcesCleanup();
	R_VkMeatpipeDestroy(mp->meatpipe);

	Mem_Free(mp->resources);
	Mem_Free(mp);
}

void Metapass_Dispatch(struct Metapass* metapass, MetapassDispatchArgs args) {
	APROF_SCOPE_DECLARE_BEGIN(dispatch, __FUNCTION__);

	R_VkResourcesFrameBeginStateChangeFIXME(args.combuf, args.is_discontinuous);

	// Update image resource links after the prev_-related swap above
	// TODO Preserve the indexes somewhere to avoid searching
	// FIXME I don't really get why we need this, the pointers should have been preserved ?!
	for (int i = 0; i < metapass->meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = metapass->meatpipe->resources + i;

		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);
		if (create && mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			// THIS FAILS WHY?! ASSERT(g_rtx.mainpipe_resources[i]->value.image_object == &res->image);
			metapass->resources[i]->ref.image = &res->image;
	}

	const vk_meatpipe_t *const mp = metapass->meatpipe;
	for (int i = 0; i < mp->passes_count; ++i) {
		const struct vk_meatpipe_pass_s *pass = metapass->meatpipe->passes + i;
		RayPassPerform(pass->pass, args.combuf,
			(ray_pass_perform_args_t){
				.frame_set_slot = args.frame_set_slot,
				.width = args.width,
				.height = args.height,
				.resources = metapass->resources,
				.resources_map = pass->resource_map,
			}
		);
	}
	APROF_SCOPE_END(dispatch);
}
