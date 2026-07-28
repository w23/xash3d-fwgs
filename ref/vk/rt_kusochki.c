#include "rt_kusochki.h"

#include "vk_materials.h"
#include "vulkan/VResource.h"
#include "vk_render.h" // vk_render_geometry_t
#include "vulkan/VBuffer.h"
#include "vk_logs.h"

#include "xash3d_mathlib.h" // VectorCopy, ...

#include "shaders/ray_interop.h" // Kusok
typedef struct Kusok vk_kusok_data_t;

#define MAX_KUSOCHKI 32768

static struct {
	// Geometry metadata. Lifetime is similar to geometry lifetime itself.
	// Semantically close to render buffer (describes layout for those objects)
	// TODO unify with render buffer?
	// Needs: STORAGE_BUFFER
	vk_buffer_t buffer;
	r_debuffer_t alloc;

	Producer producer;
} g_kusochki;

void RT_KusochkiClear(void) {
	R_DEBuffer_Init(&g_kusochki.alloc, MAX_KUSOCHKI / 2, MAX_KUSOCHKI / 2);
}

void RT_KusochkiFlip(void) {
	R_DEBuffer_Flip(&g_kusochki.alloc);
}

rt_kusochki_t RT_KusochkiAllocLong(int count) {
	// TODO Proper block allocator, not just double-ended buffer
	uint32_t kusochki_offset = R_DEBuffer_Alloc(&g_kusochki.alloc, LifetimeStatic, count, 1);

	if (kusochki_offset == ALO_ALLOC_FAILED) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded\n");
		return (rt_kusochki_t){0,0,-1};
	}

	return (rt_kusochki_t){
		.offset = kusochki_offset,
		.count = count,
		.internal_index__ = 0, // ???
	};
}

uint32_t RT_KusochkiAllocOnce(int count) {
	// TODO Proper block allocator
	uint32_t kusochki_offset = R_DEBuffer_Alloc(&g_kusochki.alloc, LifetimeDynamic, count, 1);

	if (kusochki_offset == ALO_ALLOC_FAILED) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded\n");
		return ALO_ALLOC_FAILED;
	}

	return kusochki_offset;
}

void RT_KusochkiFree(const rt_kusochki_t *kusochki) {
	// TODO block alloc
	PRINT_NOT_IMPLEMENTED();
}

static void applyMaterialToKusok(vk_kusok_data_t* kusok, const vk_render_geometry_t *geom, const r_vk_material_t *override_material, const vec4_t override_color) {
	const r_vk_material_t *const mat = override_material ? override_material : &geom->material;
	ASSERT(mat);

	ASSERT(mat->tex_base_color >= 0);
	ASSERT(mat->tex_base_color < MAX_TEXTURES || mat->tex_base_color == TEX_BASE_SKYBOX);

	ASSERT(mat->tex_roughness >= 0);
	ASSERT(mat->tex_roughness < MAX_TEXTURES);

	ASSERT(mat->tex_metalness >= 0);
	ASSERT(mat->tex_metalness < MAX_TEXTURES);

	ASSERT(mat->tex_normalmap >= 0);
	ASSERT(mat->tex_normalmap < MAX_TEXTURES);

	// TODO split kusochki into static geometry data and potentially dynamic material data
	// This data is static, should never change
	kusok->vertex_offset = geom->vertex_offset;
	kusok->index_offset = geom->index_offset;

	// Material data itself is mostly static. Except for animated textures, which just get a new material slot for each frame.
	kusok->material = (struct Material){
		.tex_base_color = mat->tex_base_color,
		.tex_roughness = mat->tex_roughness,
		.tex_metalness = mat->tex_metalness,
		.tex_normalmap = mat->tex_normalmap,

		.roughness = mat->roughness,
		.metalness = mat->metalness,
		.normal_scale = mat->normal_scale,
	};

	// TODO emissive is potentially "dynamic", not tied to the material directly, as it is specified per-surface in rad files
	VectorCopy(geom->emissive, kusok->emissive);
	Vector4Copy(mat->base_color, kusok->material.base_color);

	if (override_color) {
		kusok->material.base_color[0] *= override_color[0];
		kusok->material.base_color[1] *= override_color[1];
		kusok->material.base_color[2] *= override_color[2];
		kusok->material.base_color[3] *= override_color[3];
	}
}

// TODO this function can't really fail. It'd mean that staging is completely broken.
qboolean RT_KusochkiUpload(uint32_t kusochki_offset, const struct vk_render_geometry_s *geoms, int geoms_count, const r_vk_material_t *override_material, const vec4_t *override_colors) {
	const vk_buffer_lock_t lock_args = {
		.offset = kusochki_offset * sizeof(vk_kusok_data_t),
		.size = geoms_count * sizeof(vk_kusok_data_t),
	};
	const vk_buffer_locked_t lock = R_VkBufferLock(&g_kusochki.buffer, lock_args);

	if (!lock.ptr) {
		gEngine.Con_Printf(S_ERROR "Couldn't allocate staging for %d kusochkov\n", geoms_count);
		return false;
	}

	vk_kusok_data_t *const p = lock.ptr;
	for (int i = 0; i < geoms_count; ++i) {
		const vk_render_geometry_t *geom = geoms + i;
		applyMaterialToKusok(p + i, geom, override_material, override_colors ? override_colors[i] : NULL);
	}

	R_VkBufferUnlock(lock);
	return true;
}

static void produceKusochki(struct Producer* p, struct vk_combuf_s *combuf, const FrameContext *ctx) {
	(void)p; (void)ctx;
	R_VkBufferStagingCommit(&g_kusochki.buffer, combuf);
}

qboolean RT_KusochkiInit(void) {
	if (!VK_BufferCreate("rt_kusochki", &g_kusochki.buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		// FIXME complain, handle
		return false;
	}

	g_kusochki.producer = (Producer) {
		.name = "kusochki",
		.frame_sequence_tag = 0,
		.produce = produceKusochki,
	};

	R_VkBufferRegisterAsResource((r_vkbuffer_register_as_resource_t){
		.name = "kusochki",
		.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.buffer = &g_kusochki.buffer,
		.offset = 0,
		.size = g_kusochki.buffer.size,
		.producer = &g_kusochki.producer,
	});

	return true;
}

void RT_KusochkiShutdown(void) {
	VK_BufferDestroy(&g_kusochki.buffer);
}
