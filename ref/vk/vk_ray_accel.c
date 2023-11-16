#include "vk_ray_accel.h"

#include "vk_core.h"
#include "vk_rtx.h"
#include "vk_ray_internal.h"
#include "r_speeds.h"
#include "vk_combuf.h"
#include "vk_staging.h"
#include "vk_math.h"
#include "vk_geometry.h"
#include "vk_render.h"
#include "vk_logs.h"

#include "profiler.h"

#include "xash3d_mathlib.h"

#define LOG_MODULE LogModule_RT

#define MODULE_NAME "accel"

typedef struct rt_blas_s {
	const char *debug_name;
	rt_blas_usage_e usage;

	VkAccelerationStructureKHR blas;

	int max_geoms;
	//uint32_t *max_prim_counts;
	int blas_size;
} rt_blas_t;

static struct {
	// Stores AS built data. Lifetime similar to render buffer:
	// - some portion lives for entire map lifetime
	// - some portion lives only for a single frame (may have several frames in flight)
	// TODO: unify this with render buffer
	// Needs: AS_STORAGE_BIT, SHADER_DEVICE_ADDRESS_BIT
	vk_buffer_t accels_buffer;
	struct alo_pool_s *accels_buffer_alloc;

	// Temp: lives only during a single frame (may have many in flight)
	// Used for building ASes;
	// Needs: AS_STORAGE_BIT, SHADER_DEVICE_ADDRESS_BIT
	vk_buffer_t scratch_buffer;
	VkDeviceAddress accels_buffer_addr, scratch_buffer_addr;

	// Temp-ish: used for making TLAS, contains addressed to all used BLASes
	// Lifetime and nature of usage similar to scratch_buffer
	// TODO: unify them
	// Needs: SHADER_DEVICE_ADDRESS, STORAGE_BUFFER, AS_BUILD_INPUT_READ_ONLY
	vk_buffer_t tlas_geom_buffer;
	VkDeviceAddress tlas_geom_buffer_addr;
	r_flipping_buffer_t tlas_geom_buffer_alloc;

	// TODO need several TLASes for N frames in flight
	VkAccelerationStructureKHR tlas;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	struct {
		int instances_count;
		int accels_built;
	} stats;
} g_accel;

static VkAccelerationStructureBuildSizesInfoKHR getAccelSizes(const VkAccelerationStructureBuildGeometryInfoKHR *build_info, const uint32_t *max_prim_counts) {
	VkAccelerationStructureBuildSizesInfoKHR build_size = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
	};

	vkGetAccelerationStructureBuildSizesKHR(
		vk_core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, build_info, max_prim_counts, &build_size);

	return build_size;
}

static VkAccelerationStructureKHR createAccel(const char *name, VkAccelerationStructureTypeKHR type, uint32_t size) {
	const alo_block_t block = aloPoolAllocate(g_accel.accels_buffer_alloc, size, /*TODO why? align=*/256);

	if (block.offset == ALO_ALLOC_FAILED) {
		ERR("Failed to allocate %u bytes for blas \"%s\"", size, name);
		return VK_NULL_HANDLE;
	}

	const VkAccelerationStructureCreateInfoKHR asci = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = g_accel.accels_buffer.buffer,
		.offset = block.offset,
		.type = type,
		.size = size,
	};

	VkAccelerationStructureKHR accel = VK_NULL_HANDLE;
	XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, &accel));
	SET_DEBUG_NAME(accel, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, name);
	return accel;
}

static VkDeviceAddress getAccelAddress(VkAccelerationStructureKHR as) {
	VkAccelerationStructureDeviceAddressInfoKHR asdai = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = as,
	};
	return vkGetAccelerationStructureDeviceAddressKHR(vk_core.device, &asdai);
}

static qboolean buildAccel(VkBuffer geometry_buffer, VkAccelerationStructureBuildGeometryInfoKHR *build_info, uint32_t scratch_buffer_size, const VkAccelerationStructureBuildRangeInfoKHR *build_ranges) {
	// FIXME this is definitely not the right place. We should upload everything in bulk, and only then build blases in bulk too
	vk_combuf_t *const combuf = R_VkStagingCommit();
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT, // FIXME
			.buffer = geometry_buffer,
			.offset = 0, // FIXME
			.size = VK_WHOLE_SIZE, // FIXME
		} };
		vkCmdPipelineBarrier(combuf->cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			//VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, COUNTOF(bmb), bmb, 0, NULL);
	}

	//gEngine.Con_Reportf("sratch offset = %d, req=%d", g_accel.frame.scratch_offset, scratch_buffer_size);

	if (MAX_SCRATCH_BUFFER < g_accel.frame.scratch_offset + scratch_buffer_size) {
		ERR("Scratch buffer overflow: left %u bytes, but need %u",
			MAX_SCRATCH_BUFFER - g_accel.frame.scratch_offset,
			scratch_buffer_size);
		return false;
	}

	build_info->scratchData.deviceAddress = g_accel.scratch_buffer_addr + g_accel.frame.scratch_offset;

	//uint32_t scratch_offset_initial = g_accel.frame.scratch_offset;
	g_accel.frame.scratch_offset += scratch_buffer_size;
	g_accel.frame.scratch_offset = ALIGN_UP(g_accel.frame.scratch_offset, vk_core.physical_device.properties_accel.minAccelerationStructureScratchOffsetAlignment);

	//gEngine.Con_Reportf("AS=%p, n_geoms=%u, scratch: %#x %d %#x", *args->p_accel, args->n_geoms, scratch_offset_initial, scratch_buffer_size, scratch_offset_initial + scratch_buffer_size);

	g_accel.stats.accels_built++;

	static int scope_id = -2;
	if (scope_id == -2)
		scope_id = R_VkGpuScope_Register("build_as");
	const int begin_index = R_VkCombufScopeBegin(combuf, scope_id);
	const VkAccelerationStructureBuildRangeInfoKHR *p_build_ranges = build_ranges;
	vkCmdBuildAccelerationStructuresKHR(combuf->cmdbuf, 1, build_info, &p_build_ranges);
	R_VkCombufScopeEnd(combuf, begin_index, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

	return true;
}

// TODO split this into smaller building blocks in a separate module
qboolean createOrUpdateAccelerationStructure(vk_combuf_t *combuf, const as_build_args_t *args) {
	ASSERT(args->geoms);
	ASSERT(args->n_geoms > 0);
	ASSERT(args->p_accel);

	const qboolean should_create = *args->p_accel == VK_NULL_HANDLE;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = args->type,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = args->n_geoms,
		.pGeometries = args->geoms,
		.srcAccelerationStructure = VK_NULL_HANDLE,
	};

	const VkAccelerationStructureBuildSizesInfoKHR build_size = getAccelSizes(&build_info, args->max_prim_counts);

	if (should_create) {
		*args->p_accel = createAccel(args->debug_name, args->type, build_size.accelerationStructureSize);

		if (!args->p_accel)
			return false;

		if (args->out_accel_addr)
			*args->out_accel_addr = getAccelAddress(*args->p_accel);

		if (args->inout_size)
			*args->inout_size = build_size.accelerationStructureSize;

		// gEngine.Con_Reportf("AS=%p, n_geoms=%u, build: %#x %d %#x", *args->p_accel, args->n_geoms, buffer_offset, asci.size, buffer_offset + asci.size);
	}

	// If not enough data for building, just create
	if (!combuf || !args->build_ranges)
		return true;

	if (args->inout_size)
		ASSERT(*args->inout_size >= build_size.accelerationStructureSize);

	build_info.dstAccelerationStructure = *args->p_accel;
	const VkBuffer geometry_buffer = R_GeometryBuffer_Get();
	return buildAccel(geometry_buffer, &build_info, build_size.buildScratchSize, args->build_ranges);
}

static void createTlas( vk_combuf_t *combuf, VkDeviceAddress instances_addr ) {
	const VkAccelerationStructureGeometryKHR tl_geom[] = {
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			//.flags = VK_GEOMETRY_OPAQUE_BIT,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry.instances =
				(VkAccelerationStructureGeometryInstancesDataKHR){
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
					.data.deviceAddress = instances_addr,
					.arrayOfPointers = VK_FALSE,
				},
		},
	};
	const uint32_t tl_max_prim_counts[COUNTOF(tl_geom)] = { MAX_INSTANCES };
	const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
		.primitiveCount = g_ray_model_state.frame.instances_count,
	};
	const as_build_args_t asrgs = {
		.geoms = tl_geom,
		.max_prim_counts = tl_max_prim_counts,
		.build_ranges = !combuf ? NULL : &tl_build_range,
		.n_geoms = COUNTOF(tl_geom),
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		// we can't really rebuild TLAS because instance count changes are not allowed .dynamic = true,
		.dynamic = false,
		.p_accel = &g_accel.tlas,
		.debug_name = "TLAS",
		.out_accel_addr = NULL,
		.inout_size = NULL,
	};
	if (!createOrUpdateAccelerationStructure(combuf, &asrgs)) {
		gEngine.Host_Error("Could not create/update TLAS\n");
		return;
	}
}

vk_resource_t RT_VkAccelPrepareTlas(vk_combuf_t *combuf) {
	APROF_SCOPE_DECLARE_BEGIN(prepare, __FUNCTION__);
	ASSERT(g_ray_model_state.frame.instances_count > 0);
	DEBUG_BEGIN(combuf->cmdbuf, "prepare tlas");

	R_FlippingBuffer_Flip( &g_accel.tlas_geom_buffer_alloc );

	const uint32_t instance_offset = R_FlippingBuffer_Alloc(&g_accel.tlas_geom_buffer_alloc, g_ray_model_state.frame.instances_count, 1);
	ASSERT(instance_offset != ALO_ALLOC_FAILED);

	// Upload all blas instances references to GPU mem
	{
		const vk_staging_region_t headers_lock = R_VkStagingLockForBuffer((vk_staging_buffer_args_t){
			.buffer = g_ray_model_state.model_headers_buffer.buffer,
			.offset = 0,
			.size = g_ray_model_state.frame.instances_count * sizeof(struct ModelHeader),
			.alignment = 16,
		});

		ASSERT(headers_lock.ptr);

		VkAccelerationStructureInstanceKHR* inst = ((VkAccelerationStructureInstanceKHR*)g_accel.tlas_geom_buffer.mapped) + instance_offset;
		for (int i = 0; i < g_ray_model_state.frame.instances_count; ++i) {
			const rt_draw_instance_t* const instance = g_ray_model_state.frame.instances + i;
			ASSERT(instance->blas_addr != 0);
			inst[i] = (VkAccelerationStructureInstanceKHR){
				.instanceCustomIndex = instance->kusochki_offset,
				.instanceShaderBindingTableRecordOffset = 0,
				.accelerationStructureReference = instance->blas_addr,
			};
			switch (instance->material_mode) {
				case MATERIAL_MODE_OPAQUE:
					inst[i].mask = GEOMETRY_BIT_OPAQUE;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_REGULAR,
					// Force no-culling because there are cases where culling leads to leaking shadows, holes in reflections, etc
					// CULL_DISABLE_BIT disables culling even if the gl_RayFlagsCullFrontFacingTrianglesEXT bit is set in shaders
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
					break;
				case MATERIAL_MODE_OPAQUE_ALPHA_TEST:
					inst[i].mask = GEOMETRY_BIT_ALPHA_TEST;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_ALPHA_TEST,
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
					break;
				case MATERIAL_MODE_TRANSLUCENT:
					inst[i].mask = GEOMETRY_BIT_REFRACTIVE;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_REGULAR,
					// Disable culling for translucent surfaces: decide what side it is based on normal wrt ray directions
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
					break;
				case MATERIAL_MODE_BLEND_ADD:
				case MATERIAL_MODE_BLEND_MIX:
				case MATERIAL_MODE_BLEND_GLOW:
					inst[i].mask = GEOMETRY_BIT_BLEND;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_ADDITIVE,
					// Force no-culling because these should be visible from any angle
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
					break;
				default:
					gEngine.Host_Error("Unexpected material mode %d\n", instance->material_mode);
					break;
			}
			memcpy(&inst[i].transform, instance->transform_row, sizeof(VkTransformMatrixKHR));

			struct ModelHeader *const header = ((struct ModelHeader*)headers_lock.ptr) + i;
			header->mode = instance->material_mode;
			Vector4Copy(instance->color, header->color);
			Matrix4x4_ToArrayFloatGL(instance->prev_transform_row, (float*)header->prev_transform);
		}

		R_VkStagingUnlock(headers_lock.handle);
	}

	g_accel.stats.instances_count = g_ray_model_state.frame.instances_count;

	// Barrier for building all BLASes
	// BLAS building is now in cmdbuf, need to synchronize with results
	{
		VkBufferMemoryBarrier bmb[] = {{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, // | VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.buffer = g_accel.accels_buffer.buffer,
			.offset = instance_offset * sizeof(VkAccelerationStructureInstanceKHR),
			.size = g_ray_model_state.frame.instances_count * sizeof(VkAccelerationStructureInstanceKHR),
		}};
		vkCmdPipelineBarrier(combuf->cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, COUNTOF(bmb), bmb, 0, NULL);
	}

	// 2. Build TLAS
	createTlas(combuf, g_accel.tlas_geom_buffer_addr + instance_offset * sizeof(VkAccelerationStructureInstanceKHR));
	DEBUG_END(combuf->cmdbuf);

	// TODO return vk_resource_t with callback to all this "do the preparation and barriers" crap, instead of doing it here
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.buffer = g_accel.accels_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		} };
		vkCmdPipelineBarrier(combuf->cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, NULL, COUNTOF(bmb), bmb, 0, NULL);
	}

	APROF_SCOPE_END(prepare);
	return (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.value = (vk_descriptor_value_t){
			.accel = (VkWriteDescriptorSetAccelerationStructureKHR) {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
				.accelerationStructureCount = 1,
				.pAccelerationStructures = &g_accel.tlas,
				.pNext = NULL,
			},
		},
	};
}

qboolean RT_VkAccelInit(void) {
	if (!VK_BufferCreate("ray accels_buffer", &g_accel.accels_buffer, MAX_ACCELS_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		))
	{
		return false;
	}
	g_accel.accels_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.accels_buffer.buffer);

	if (!VK_BufferCreate("ray scratch_buffer", &g_accel.scratch_buffer, MAX_SCRATCH_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		)) {
		return false;
	}
	g_accel.scratch_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.scratch_buffer.buffer);

	// TODO this doesn't really need to be host visible, use staging
	if (!VK_BufferCreate("ray tlas_geom_buffer", &g_accel.tlas_geom_buffer, sizeof(VkAccelerationStructureInstanceKHR) * MAX_INSTANCES * 2,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}
	g_accel.tlas_geom_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.tlas_geom_buffer.buffer);
	R_FlippingBuffer_Init(&g_accel.tlas_geom_buffer_alloc, MAX_INSTANCES * 2);

	g_accel.accels_buffer_alloc = aloPoolCreate(MAX_ACCELS_BUFFER, MAX_INSTANCES, /* why */ 256);

	R_SPEEDS_COUNTER(g_accel.stats.instances_count, "instances", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_accel.stats.accels_built, "built", kSpeedsMetricCount);

	return true;
}

void RT_VkAccelShutdown(void) {
	if (g_accel.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_accel.tlas, NULL);

	VK_BufferDestroy(&g_accel.scratch_buffer);
	VK_BufferDestroy(&g_accel.accels_buffer);
	VK_BufferDestroy(&g_accel.tlas_geom_buffer);
	if (g_accel.accels_buffer_alloc)
		aloPoolDestroy(g_accel.accels_buffer_alloc);
}

void RT_VkAccelNewMap(void) {
	const int expected_accels = 512; // TODO actually get this from playing the game
	const int accels_alignment = 256; // TODO where does this come from?
	ASSERT(vk_core.rtx);

	g_accel.frame.scratch_offset = 0;

	if (g_accel.accels_buffer_alloc)
		aloPoolDestroy(g_accel.accels_buffer_alloc);
	g_accel.accels_buffer_alloc = aloPoolCreate(MAX_ACCELS_BUFFER, expected_accels, accels_alignment);

	// Recreate tlas
	// Why here and not in init: to make sure that its memory is preserved. Map init will clear all memory regions.
	{
		if (g_accel.tlas != VK_NULL_HANDLE) {
			vkDestroyAccelerationStructureKHR(vk_core.device, g_accel.tlas, NULL);
			g_accel.tlas = VK_NULL_HANDLE;
		}

		createTlas(VK_NULL_HANDLE, g_accel.tlas_geom_buffer_addr);
	}
}

void RT_VkAccelFrameBegin(void) {
	g_accel.frame.scratch_offset = 0;
}

struct rt_blas_s* RT_BlasCreate(const char *name, rt_blas_usage_e usage) {
	rt_blas_t *blas = Mem_Calloc(vk_core.pool, sizeof(*blas));

	blas->debug_name = name;
	blas->usage = usage;
	blas->blas_size = -1;

	return blas;
}

qboolean RT_BlasPreallocate(struct rt_blas_s* blas, rt_blas_preallocate_t args) {
	ASSERT(!blas->blas);
	ASSERT(blas->usage == kBlasBuildDynamicFast);

	// TODO allocate these from static pool
	VkAccelerationStructureGeometryKHR *const as_geoms = Mem_Calloc(vk_core.pool, args.max_geometries * sizeof(*as_geoms));
	uint32_t *const max_prim_counts = Mem_Malloc(vk_core.pool, args.max_geometries * sizeof(*max_prim_counts));
	VkAccelerationStructureBuildRangeInfoKHR *const build_ranges = Mem_Calloc(vk_core.pool, args.max_geometries * sizeof(*build_ranges));

	for (int i = 0; i < args.max_geometries; ++i) {
		max_prim_counts[i] = args.max_prims_per_geometry;
		as_geoms[i] = (VkAccelerationStructureGeometryKHR)
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // FIXME this is not true. incoming mode might have transparency eventually (and also dynamically)
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = VK_INDEX_TYPE_UINT16,
						.maxVertex = args.max_vertex_per_geometry,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = 0,
						.indexData.deviceAddress = 0,
					},
			};

		build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR) {
			.primitiveCount = args.max_prims_per_geometry,
			.primitiveOffset = 0,
			.firstVertex = 0,
		};
	}

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
		.geometryCount = args.max_geometries,
		.srcAccelerationStructure = VK_NULL_HANDLE,
		.pGeometries = as_geoms,
	};

	const VkAccelerationStructureBuildSizesInfoKHR build_size = getAccelSizes(&build_info, max_prim_counts);
	DEBUG("geoms=%d max_prims=%d max_vertex=%d => blas=%dKiB",
		args.max_geometries, args.max_prims_per_geometry, args.max_vertex_per_geometry, (int)build_size.accelerationStructureSize / 1024);

	qboolean retval = false;

	blas->blas = createAccel(blas->debug_name, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, build_size.accelerationStructureSize);
	if (!blas->blas) {
		ERR("Couldn't preallocate blas %s", blas->debug_name);
		goto finalize;
	}

	retval = true;

	blas->blas_size = build_size.accelerationStructureSize;
	blas->max_geoms = build_info.geometryCount;

finalize:
	Mem_Free(as_geoms);
	Mem_Free(max_prim_counts);
	Mem_Free(build_ranges);
	return retval;
}

void RT_BlasDestroy(struct rt_blas_s* blas) {
	if (!blas)
		return;

	/* if (blas->max_prims) */
	/* 	Mem_Free(blas->max_prims); */

	if (blas->blas)
		vkDestroyAccelerationStructureKHR(vk_core.device, blas->blas, NULL);

	Mem_Free(blas);
}

VkDeviceAddress RT_BlasGetDeviceAddress(struct rt_blas_s *blas) {
	return getAccelAddress(blas->blas);
}

qboolean RT_BlasBuild(struct rt_blas_s *blas, const struct vk_render_geometry_s *geoms, int geoms_count) {
	if (!blas || !geoms_count)
		return false;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = geoms_count,
		.srcAccelerationStructure = VK_NULL_HANDLE,
	};

	qboolean is_update = false;

	switch (blas->usage) {
		case kBlasBuildStatic:
			ASSERT(!blas->blas);
			break;
		case kBlasBuildDynamicUpdate:
			if (blas->blas) {
				build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
				build_info.srcAccelerationStructure = blas->blas;
				is_update = true;
			}
			build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
			break;
		case kBlasBuildDynamicFast:
			build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
			break;
	}

	const VkBuffer geometry_buffer = R_GeometryBuffer_Get();
	const VkDeviceAddress buffer_addr = R_VkBufferGetDeviceAddress(geometry_buffer);

	// TODO allocate these from static pool
	VkAccelerationStructureGeometryKHR *const as_geoms = Mem_Calloc(vk_core.pool, geoms_count * sizeof(*as_geoms));
	uint32_t *const max_prim_counts = Mem_Malloc(vk_core.pool, geoms_count * sizeof(*max_prim_counts));
	VkAccelerationStructureBuildRangeInfoKHR *const build_ranges = Mem_Calloc(vk_core.pool, geoms_count * sizeof(*build_ranges));

	for (int i = 0; i < geoms_count; ++i) {
		const vk_render_geometry_t *mg = geoms + i;
		const uint32_t prim_count = mg->element_count / 3;

		max_prim_counts[i] = prim_count;
		as_geoms[i] = (VkAccelerationStructureGeometryKHR)
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // FIXME this is not true. incoming mode might have transparency eventually (and also dynamically)
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = VK_INDEX_TYPE_UINT16,
						.maxVertex = mg->max_vertex,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr,
						.indexData.deviceAddress = buffer_addr,
					},
			};

		build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR) {
			.primitiveCount = prim_count,
			.primitiveOffset = mg->index_offset * sizeof(uint16_t),
			.firstVertex = mg->vertex_offset,
		};
	}

	build_info.pGeometries = as_geoms;

	const VkAccelerationStructureBuildSizesInfoKHR build_size = getAccelSizes(&build_info, max_prim_counts);

	qboolean retval = false;

	// allocate blas
	if (!blas->blas) {
		blas->blas = createAccel(blas->debug_name, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, build_size.accelerationStructureSize);
		if (!blas->blas) {
			ERR("Couldn't create vk accel");
			goto finalize;
		}

		blas->blas_size = build_size.accelerationStructureSize;
		blas->max_geoms = build_info.geometryCount;
	} else {
		if (blas->blas_size < build_size.accelerationStructureSize) {
			ERR("Fast dynamic BLAS %s size exceeded (need %dKiB, have %dKiB, geoms = %d)", blas->debug_name,
				(int)build_size.accelerationStructureSize / 1024,
				blas->blas_size / 1024,
				geoms_count
			);
			goto finalize;
		}
	}

	// Build
	build_info.dstAccelerationStructure = blas->blas;
	if (!buildAccel(geometry_buffer, &build_info, is_update ? build_size.updateScratchSize : build_size.buildScratchSize, build_ranges)) {
		ERR("Couldn't build BLAS %s", blas->debug_name);
		goto finalize;
	}

	retval = true;

finalize:
	Mem_Free(as_geoms);
	Mem_Free(max_prim_counts);
	Mem_Free(build_ranges);
	return retval;
}
