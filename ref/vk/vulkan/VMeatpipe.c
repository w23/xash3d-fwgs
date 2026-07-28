#include "VMeatpipe.h"

#include "VPipeline.h"
#include "VResource.h"
#include "VBarrier.h"
#include "VPass.h"
#include "vk_common.h"
#include "vk_logs.h"
#include "std/profiler.h"

#define LOG_MODULE meat

#define MIN(a,b) ((a)<(b)?(a):(b))

#define CHAR4UINT(a,b,c,d) (((d)<<24)|((c)<<16)|((b)<<8)|(a))
static const uint32_t k_meatpipe_magic = CHAR4UINT('M', 'E', 'A', 'T');

enum {
	MEATPIPE_RES_WRITE = (1<<0),
	MEATPIPE_RES_CREATE = (1<<1),
	// TMP ..
};

typedef struct {
	char name[64];
	uint32_t descriptor_type;
	int count;
	uint32_t flags;
	union {
		uint32_t image_format;
	};

	// Index+1 of resource image to read data from if this resource is a "previous frame" contents of another one.
	// Value of zero means that it is a standalone resource. The real index is the value - 1.
	int prev_frame_index_plus_1;
} vk_meatpipe_resource_t;

struct vk_meatpipe_pass_s;
typedef struct vk_meatpipe_s {
	int passes_count;
	struct vk_meatpipe_pass_s *passes;

	int resources_count;
	vk_meatpipe_resource_t *resources;

	// TODO move these into passes as ready-to-go rt_resource_p[]
	// Helper list of resource pointers to global resource map
	// Needed as an argument to `R_VkMeatpipePerform()` so that meatpipe can access resources
	rt_resource_t* *acquired_resources;

	r_vk_image_t *image_dest;
} vk_meatpipe_t;

struct ray_pass_s;
typedef struct vk_meatpipe_pass_s {
	struct ray_pass_s* pass;
	int write_from;
	int resource_count;
	int *resource_map;
} vk_meatpipe_pass_t;

typedef struct cursor_t {
	const byte *data;
	int off, size;
	qboolean error;
} cursor_t;

typedef struct load_context_t {
	cursor_t cur;

	int shaders_count;
	VkShaderModule *shaders;

	vk_meatpipe_t meatpipe;
} load_context_t;

static const void* curReadPtr(cursor_t *cur, int size) {
	const int left = cur->size - cur->off;
	if (left < size) {
		cur->error = true;
		return NULL;
	}

	const void* const ret = cur->data + cur->off;
	cur->off += size;
	return ret;
}

#define CUR_ERROR(errmsg, ...) \
	if (ctx->cur.error) { \
		ERR("(off=%d left=%d) " errmsg "", ctx->cur.off, (ctx->cur.size - ctx->cur.off), ##__VA_ARGS__); \
		goto finalize; \
	}

#define CUR_ERROR_RETURN(retval, errmsg, ...) \
	if (ctx->cur.error) { \
		ERR("(off=%d left=%d) " errmsg "", ctx->cur.off, (ctx->cur.size - ctx->cur.off), ##__VA_ARGS__); \
		return retval; \
	}

#define READ_PTR(size, errmsg, ...) \
	curReadPtr(&ctx->cur, size); CUR_ERROR(errmsg, ##__VA_ARGS__)

static uint32_t curReadU32(cursor_t *cur) {
	const void *src = curReadPtr(cur, sizeof(uint32_t));
	if (!src)
		return 0;

	uint32_t ret;
	memcpy(&ret, src, sizeof(uint32_t));
	return ret;
}

#define READ_U32(errmsg, ...) \
	curReadU32(&ctx->cur); CUR_ERROR(errmsg, ##__VA_ARGS__)

#define READ_U32_RETURN(retval, errmsg, ...) \
	curReadU32(&ctx->cur); CUR_ERROR_RETURN(retval, errmsg, ##__VA_ARGS__)

static int curReadStr(cursor_t *cur, char* out, int out_size) {
	const int len = curReadU32(cur);
	if (cur->error)
		return -1;

	const char *src = curReadPtr(cur, len);
	if (cur->error)
		return -1;

	const int max = MIN(out_size, len); \
	memcpy(out, src, max); \
	out[max] = '\0';
	return len;
}

#define READ_STR(out, errmsg, ...) \
	curReadStr(&ctx->cur, out, sizeof(out)); CUR_ERROR(errmsg, ##__VA_ARGS__)

#define READ_STR_RETURN(retval, out, errmsg, ...) \
	curReadStr(&ctx->cur, out, sizeof(out)); CUR_ERROR_RETURN(retval, errmsg, ##__VA_ARGS__)

#define NO_SHADER 0xffffffff

static struct ray_pass_s *pipelineLoadCompute(load_context_t *ctx, int i, const char *name, const ray_pass_layout_t *layout) {
	const uint32_t shader_comp = READ_U32_RETURN(NULL, "Couldn't read comp shader for %d %s", i, name);

	if (shader_comp >= ctx->shaders_count) {
		ERR("Pipeline %s shader index out of bounds %d (count %d)", name, shader_comp, ctx->shaders_count);
		return NULL;
	}

	const ray_pass_create_compute_t rpcc = {
		.debug_name = name,
		.layout = *layout,
		.shader_module = ctx->shaders[shader_comp],
	};

	return RayPassCreateCompute(&rpcc);
}

static struct ray_pass_s *pipelineLoadRT(load_context_t *ctx, int i, const char *name, const ray_pass_layout_t *layout) {
	ray_pass_p ret = NULL;
	ray_pass_create_tracing_t rpct = {
		.debug_name = name,
		.layout = *layout,
	};

	// FIXME bounds check shader indices

	const uint32_t shader_rgen = READ_U32("Couldn't read rgen shader for %d %s", i, name);
	rpct.raygen_module = ctx->shaders[shader_rgen];

	rpct.miss_count = READ_U32("Couldn't read miss count for %d %s", i, name);
	if (rpct.miss_count) {
		rpct.miss_module = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * rpct.miss_count);
		for (int j = 0; j < rpct.miss_count; ++j) {
			const uint32_t shader_miss = READ_U32("Couldn't read miss shader %d for %d %s", j, i, name);
			rpct.miss_module[j] = ctx->shaders[shader_miss];
		}
	}

	rpct.hit_count = READ_U32("Couldn't read hit count for %d %s", i, name);
	if (rpct.hit_count) {
		ray_pass_hit_group_t *hit = Mem_Malloc(vk_core.pool, sizeof(rpct.hit[0]) * rpct.hit_count);
		rpct.hit = hit;
		for (int j = 0; j < rpct.hit_count; ++j) {
			const uint32_t closest = READ_U32("Couldn't read closest shader %d for %d %s", j, i, name);
			const uint32_t any = READ_U32("Couldn't read any shader %d for %d %s", j, i, name);

			hit[j] = (ray_pass_hit_group_t){
				.closest_module = (closest == NO_SHADER) ? VK_NULL_HANDLE : ctx->shaders[closest],
				.any_module = (any == NO_SHADER) ? VK_NULL_HANDLE : ctx->shaders[any],
			};
		}
	}

	ret = RayPassCreateTracing(&rpct);

finalize:
	if (rpct.hit)
		Mem_Free((void*)rpct.hit);

	if (rpct.miss_module)
		Mem_Free(rpct.miss_module);

	return ret;
}

#define MAX_BINDINGS 64
static qboolean readBindings(load_context_t *ctx, VkDescriptorSetLayoutBinding *bindings, vk_meatpipe_pass_t* pass ) {
	pass->resource_map = NULL;
	int write_from = -1;
	const int count = READ_U32("Coulnd't read bindings count");

	if (count > MAX_BINDINGS) {
		ERR("Too many binding (%d), max: %d", count, MAX_BINDINGS);
		goto finalize;
	}

	pass->resource_map = count ? Mem_Malloc(vk_core.pool, sizeof(pass->resource_map[0]) * count) : NULL;

	for (int i = 0; i < count; ++i) {
		const uint32_t header = READ_U32("Couldn't read header for binding %d", i);
		const uint32_t res_index = READ_U32("Couldn't read res index for binding %d", i);
		const uint32_t stages = READ_U32("Couldn't read stages for binding %d", i);

		if (res_index >= ctx->meatpipe.resources_count) {
			ERR("Resource %d is out of bound %d for binding %d", res_index, ctx->meatpipe.resources_count, i);
			goto finalize;
		}

		vk_meatpipe_resource_t *res = ctx->meatpipe.resources + res_index;

#define BINDING_WRITE_BIT 0x80000000u
#define BINDING_CREATE_BIT 0x40000000u
		const qboolean write = !!(header & BINDING_WRITE_BIT);
		const qboolean create = !!(header & BINDING_CREATE_BIT);
		const uint32_t descriptor_set = (header >> 8) & 0xffu;
		const uint32_t binding = header & 0xffu;

		if (write && write_from < 0)
			write_from = i;

		if (!write && write_from >= 0) {
			ERR("Unsorted non-write binding found at %d(%s), writable started at %d",
				i, res->name, write_from);
			goto finalize;
		}

		const char *name = res->name;

		bindings[i] = (VkDescriptorSetLayoutBinding){
			.binding = binding,
			.descriptorType = res->descriptor_type,
			.descriptorCount = res->count,
			.stageFlags = stages,
			.pImmutableSamplers = NULL,
		};

		pass->resource_map[i] = res_index;

		if (write)
			res->flags |= MEATPIPE_RES_WRITE;

		if (create)
			res->flags |= MEATPIPE_RES_CREATE;

		DEBUG("Binding %d: %s ds=%d b=%d s=%08x res=%d type=%d write=%d",
			i, name, descriptor_set, binding, stages, res_index, res->descriptor_type, write);
	}

	pass->write_from = write_from;
	pass->resource_count = count;
	return true;

finalize:
	if (pass->resource_map)
		Mem_Free(pass->resource_map);

	pass->resource_map = NULL;
	return false;
}

static qboolean readAndCreatePass(load_context_t *ctx, int i) {
	VkDescriptorSetLayoutBinding bindings[MAX_BINDINGS];
	ray_pass_layout_t layout = {
		.bindings = bindings,
		.push_constants = {0},
	};

	vk_meatpipe_pass_t *pass = ctx->meatpipe.passes + i;
	memset(pass, 0, sizeof(*pass));

	const uint32_t type = READ_U32("Couldn't read pipeline %d type", i);

	char name[64];
	READ_STR(name, "Couldn't read pipeline %d name", i);

	DEBUG("%d: loading pipeline %s", i, name);

	if (!readBindings(ctx, bindings, pass)) {
		ERR("Couldn't read bindings for pipeline %s", name);
		return false;
	}

	layout.bindings_count = pass->resource_count;
	layout.write_from = pass->write_from;

#define PIPELINE_COMPUTE 1
#define PIPELINE_RAYTRACING 2

	switch (type) {
		case PIPELINE_COMPUTE:
			pass->pass = pipelineLoadCompute(ctx, i, name, &layout);
			break;
		case PIPELINE_RAYTRACING:
			pass->pass = pipelineLoadRT(ctx, i, name, &layout);
			break;
		default:
			ERR("Unexpected pipeline type %d", type);
	}

	if (pass->pass)
		return true;

finalize:
	if (pass->resource_map)
		Mem_Free(pass->resource_map);
	return false;
}

static qboolean readResources(load_context_t *ctx) {
	ctx->meatpipe.resources_count = READ_U32("Couldn't read resources count");
	ctx->meatpipe.resources = Mem_Malloc(vk_core.pool, sizeof(ctx->meatpipe.resources[0]) * ctx->meatpipe.resources_count);

	for (int i = 0; i < ctx->meatpipe.resources_count; ++i) {
		vk_meatpipe_resource_t *res = ctx->meatpipe.resources + i;
		*res = (vk_meatpipe_resource_t){0};

		READ_STR(res->name, "Couldn't read resource %d name", i);

		res->descriptor_type = READ_U32("Couldn't read resource %d:%s type", i, res->name);
		res->count = READ_U32("Couldn't read resource %d:%s count", i, res->name);

		const qboolean is_image = res->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || res->descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		if (is_image) {
			res->image_format = READ_U32("Couldn't read image format for res %d:%s", i, res->name);
			res->prev_frame_index_plus_1 = READ_U32("Couldn't read resource %d:%s previous frame index", i, res->name);
		}

		DEBUG("Resource %d:%s = %08x is_image=%d image_format=%08x count=%d",
			i, res->name, res->descriptor_type, is_image, res->image_format, res->count);
	}

	return true;
finalize:
	return false;
}

static qboolean readAndLoadShaders(load_context_t *ctx) {
	ctx->shaders_count = READ_U32("Couldn't read shaders count");
	ctx->shaders = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * ctx->shaders_count);
	for (int i = 0; i < ctx->shaders_count; ++i) {
		ctx->shaders[i] = VK_NULL_HANDLE;

		char name[64];
		READ_STR(name, "Couldn't read shader %d name", i);

		const int size = READ_U32("Couldn't read shader %s size", name);
		const void *src = READ_PTR(size, "Couldn't read shader %s data", name);

		if (VK_NULL_HANDLE == (ctx->shaders[i] = R_VkShaderLoadFromMem(src, size, name))) {
			ERR("Failed to load shader %d:%s", i, name);
			goto finalize;
		}

		DEBUG("%d: Shader loaded %s", i, name);
	}

	return true;
finalize:
	return false;
}

vk_meatpipe_t* R_VkMeatpipeCreateFromFile(const char *filename) {
	vk_meatpipe_t *ret = NULL;
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		ERR("Couldn't read \"%s\"", filename);
		return NULL;
	}

	load_context_t context = {
		.cur = { .data = buf, .off = 0, .size = size },
		.shaders_count = 0,
		.shaders = NULL,
		.meatpipe = (vk_meatpipe_t){0},
	};
	load_context_t *ctx = &context;

	{
		const uint32_t magic = READ_U32("Couldn't read magic");

		if (magic != k_meatpipe_magic) {
			ERR("Meatpipe magic invalid for \"%s\": got %08x expected %08x", filename, magic, k_meatpipe_magic);
			goto finalize;
		}
	}

	if (!readResources(ctx))
		goto finalize;

	if (!readAndLoadShaders(ctx))
		goto finalize;

	ctx->meatpipe.passes_count = READ_U32("Couldn't read pipelines count");
	ctx->meatpipe.passes = Mem_Malloc(vk_core.pool, sizeof(ctx->meatpipe.passes[0]) * ctx->meatpipe.passes_count);
	for (int i = 0; i < ctx->meatpipe.passes_count; ++i) {
		if (!readAndCreatePass(ctx, i)) {
			for (int j = 0; j < i; ++j) {
				RayPassDestroy(ctx->meatpipe.passes[j].pass);
				Mem_Free(ctx->meatpipe.passes[j].resource_map);
			}
			goto finalize;
		}
	}

	// Loading successful, allocate and fill
	ret = Mem_Malloc(vk_core.pool, sizeof(*ret));
	memcpy(ret, &ctx->meatpipe, sizeof(*ret));
	ctx->meatpipe.resources = NULL;

	INFO("Loaded meatpipe %s with %d passes and %d resources", filename, ret->passes_count, ret->resources_count);

finalize:
	for (int i = 0; i < ctx->shaders_count; ++i) {
		if (ctx->shaders[i] == VK_NULL_HANDLE)
			break;

		R_VkShaderDestroy(ctx->shaders[i]);
	}

	if (ctx->shaders)
		Mem_Free(ctx->shaders);

	if (ctx->meatpipe.resources)
		Mem_Free(ctx->meatpipe.resources);

	Mem_Free(buf);
	return ret;
}

void R_VkMeatpipeDestroy(vk_meatpipe_t *mp) {
	if (!mp)
		return;

	if (mp->acquired_resources) {
		for (int i = 0; i < mp->resources_count; ++i) {
			const vk_meatpipe_resource_t *mr = mp->resources + i;
			rt_resource_t *const res = R_VkResourceFindByName(mr->name);
			ASSERT(res);
			ASSERT(res->refcount > 0);
			res->refcount--;
		}

		R_VkResourcesCleanup();

		Mem_Free(mp->acquired_resources);
	}

	for (int i = 0; i < mp->passes_count; ++i) {
		vk_meatpipe_pass_t *pass = mp->passes + i;
		RayPassDestroy(pass->pass);
		Mem_Free(pass->resource_map);
	}

	Mem_Free(mp->passes);
	Mem_Free(mp->resources);
	Mem_Free(mp);
}


typedef struct {
	rt_resource_t header;
	r_vk_image_t image;
} vk_resource_storage_image_t;

static vk_descriptor_value_t acquireStorageImageDescriptor(struct rt_resource_s* r, vk_resource_acquire_descriptor_args_t args) {
	vk_resource_storage_image_t *const res = (void*)r;

	barrierAddImage(args.barriers, (r_vkcombuf_barrier_image_t) {
		.image = &res->image,
		.layout = args.image_layout,
		.access = args.access,
	});

	// TODO how do we make sure that the same image isn't used more than once with different layouts in the same barrier set?

	return (vk_descriptor_value_t){
		.image = (VkDescriptorImageInfo) {
			.sampler = VK_NULL_HANDLE,
			.imageView = res->image.view,
			.imageLayout = args.image_layout,
		},
	};
}

static void destroyStorageImage(rt_resource_t *r) {
	vk_resource_storage_image_t *const res = (void*)r;
	if (res->image.image != VK_NULL_HANDLE)
		R_VkImageDestroy(&res->image);
	Mem_Free(res);
}

static rt_resource_t *createStorageImageResource(const vk_meatpipe_resource_t *const mr, Producer *producer,
		int max_width, int max_height) {
	if (mr->descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
		ERR("Only storage image creation is supported for meatpipes");
		return NULL;
	}

	vk_resource_storage_image_t *res = NULL;
	rt_resource_t *found = R_VkResourceFindByName(mr->name);
	if (found) {
		if (found->type != mr->descriptor_type) {
			ERR("Expected resource[%s] type %s(%d) doesn't match registered %s(%d)",
				mr->name,
				R_VkDescriptorTypeName(mr->descriptor_type), mr->descriptor_type,
				R_VkDescriptorTypeName(found->type), found->type);
			return NULL;
		}

		// TODO how to check that it's really vk_resource_storage_image_t?
		res = (void*)found;
	} else {
		res = Mem_Calloc(vk_core.pool, sizeof *res);
		Q_strncpy(res->header.name, mr->name, sizeof(res->header.name));
		res->header.type = mr->descriptor_type;
		res->header.destroy = destroyStorageImage;
		res->header.acquire_descriptor = acquireStorageImageDescriptor;
		res->header.producer = producer;
		ASSERT(R_VkResourceRegister(&res->header));
	}

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

	return &res->header;
}

static void producePass(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx) {
	(void)p;
	(void)combuf;
	(void)ctx;
}

// FIXME make the real producer when https://github.com/w23/xash3d-fwgs/issues/774 is solved or worked around
static Producer fake_producer = {
	.name = "fake_meatpipe_producer",
	.produce = producePass,
};

int R_VkMeatpipeAcquireResources(struct vk_meatpipe_s *meatpipe, int max_width, int max_height) {
	const size_t newpipe_resources_size = sizeof(rt_resource_t*) * meatpipe->resources_count;
	rt_resource_t* *acquired_resources = Mem_Calloc(vk_core.pool, newpipe_resources_size);
	r_vk_image_t *newpipe_out = NULL;

	// TODO acquire/release resources properly
	// TODO recreate images later -- only when we're sure that older resources won't be used anymore
	// - etc

	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		DEBUG("res %d/%d: %s descriptor=%u count=%d flags=[%c%c] image_format=(%s)%u",
			i, meatpipe->resources_count, mr->name, mr->descriptor_type, mr->count,
			(mr->flags & MEATPIPE_RES_WRITE) ? 'W' : ' ',
			(mr->flags & MEATPIPE_RES_CREATE) ? 'C' : ' ',
			R_VkFormatName(mr->image_format),
			mr->image_format);

		// TODO this should be specified as a flag, from rt.json
		const qboolean output = Q_strcmp("dest", mr->name) == 0;

		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);

		rt_resource_t *const res = create
			? createStorageImageResource(mr, &fake_producer, max_width, max_height)
			: R_VkResourceFindByName(mr->name);

		if (!res) {
			ERR("Couldn't acquire resource with name \"%s\"", mr->name);
			goto fail;
		}

		if (res->type != mr->descriptor_type) {
			ERR("Expected resource[%s] type %s(%d) doesn't match registered %s(%d)",
				res->name,
				R_VkDescriptorTypeName(mr->descriptor_type), mr->descriptor_type,
				R_VkDescriptorTypeName(res->type), res->type);
			goto fail;
		}

		if (output)
			newpipe_out = &((vk_resource_storage_image_t*)res)->image;

		acquired_resources[i] = res;
	}

	if (!newpipe_out) {
		ERR("New rt.json doesn't define an 'dest' output texture");
		goto fail;
	}

	// Validate prev frame ping pong links
	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		if (mr->prev_frame_index_plus_1 <= 0)
			continue;

		if (mr->descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			ERR("Resource[%s] prev_frame_index_plus_1=%d: unsupported descriptor_type=%s(%d), only storage image is supported",
				mr->name,
				mr->prev_frame_index_plus_1,
				R_VkDescriptorTypeName(mr->descriptor_type),
				mr->descriptor_type);
			goto fail;
		}

		// TODO check, don't crash
		ASSERT(mr->prev_frame_index_plus_1 < meatpipe->resources_count);

		rt_resource_t *const res = R_VkResourceFindByName(mr->name);
		ASSERT(res);
		ASSERT(res->type == mr->descriptor_type);

		const vk_meatpipe_resource_t *pr = meatpipe->resources + (mr->prev_frame_index_plus_1 - 1);
		if (mr->descriptor_type != pr->descriptor_type) {
			ERR("Current and previous resource typed don't match");
			goto fail;
		}

		rt_resource_t *const prev = R_VkResourceFindByName(pr->name);
		ASSERT(prev);
		ASSERT(prev->type == pr->descriptor_type);

		// TODO check for image compatibility
	}

	// Loading successful
	// Update refcounts if no resources were previously acquired
	if (!meatpipe->acquired_resources) {
		for (int i = 0; i < meatpipe->resources_count; ++i) {
			const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
			rt_resource_t *const res = R_VkResourceFindByName(mr->name);
			ASSERT(res);
			res->refcount++;
		}

		meatpipe->acquired_resources = acquired_resources;
	} else {
		// FIXME release?
		// FIXME reuse prev allocation
		Mem_Free(meatpipe->acquired_resources);
		meatpipe->acquired_resources = acquired_resources;
	}

	meatpipe->image_dest = newpipe_out;
	return 1;

fail:
	R_VkResourcesCleanup();

	if (acquired_resources)
		Mem_Free(acquired_resources);

	return 0;
}

static void swapPingPongImages(vk_meatpipe_t *meatpipe, vk_combuf_t* combuf, qboolean discontinuity) {
	// TODO special rt_resource_t ping-pong subclass
	// Transfer previous frames before they had a chance of their resource-barrier metadata overwritten (as there's no guaranteed order for them)
	// Assumes resources were validated already
	for (int i = 0; i < meatpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = meatpipe->resources + i;
		if (mr->prev_frame_index_plus_1 <= 0)
			continue;

		vk_resource_storage_image_t *const write = (void*)meatpipe->acquired_resources[i];
		vk_resource_storage_image_t *const read = (void*)meatpipe->acquired_resources[mr->prev_frame_index_plus_1 - 1];

		// Swap images
		const r_vk_image_t tmp_img = write->image;
		write->image = read->image;
		read->image = tmp_img;

		// If there was no initial state, prepare it. (this should happen only for the first frame)
		if (discontinuity || read->image.sync.write.stage == 0) {
			// TODO is there a better way? Can image be cleared w/o explicit clear op?
			WARN("discontinuity: %s", read->header.name);
			R_VkImageClear( &read->image, combuf, NULL );
		}
	}
}

struct r_vk_image_s* R_VkMeatpipeDispatch(struct vk_meatpipe_s *meatpipe, vk_meatpipe_dispatch_t args) {
	APROF_SCOPE_DECLARE_BEGIN(dispatch, __FUNCTION__);

	swapPingPongImages(meatpipe, args.combuf, args.is_discontinuous);

	const vk_meatpipe_t *const mp = meatpipe;
	for (int i = 0; i < mp->passes_count; ++i) {
		const struct vk_meatpipe_pass_s *pass = meatpipe->passes + i;
		RayPassPerform(pass->pass, args.combuf,
			(ray_pass_perform_args_t){
				.frame_sequence = args.frame_sequence,
				.frame_set_slot = args.frame_set_slot,
				.width = args.width,
				.height = args.height,
				.resources = meatpipe->acquired_resources,
				.resources_map = pass->resource_map,
			}
		);
	}
	APROF_SCOPE_END(dispatch);

	return meatpipe->image_dest;
}
