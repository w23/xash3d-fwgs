#include "vk_devmem.h"
#include "alolcator.h"

#define MAX_DEVMEM_ALLOCS 32
#define DEFAULT_ALLOCATION_SIZE (64 * 1024 * 1024)

static qboolean Impl_Init( void );
static     void Impl_Shutdown( void );

RVkModule g_module_devmem = {
	.name = "devmem",
	.state = RVkModuleState_NotInitialized,
	.dependencies = RVkModuleDependencies_Empty,
	.Init = Impl_Init,
	.Shutdown = Impl_Shutdown
};

typedef struct {
	uint32_t type_index;
	VkMemoryPropertyFlags property_flags; // device vs host
	VkMemoryAllocateFlags allocate_flags;
	VkDeviceMemory device_memory;
	VkDeviceSize size;

	void *map;
	int refcount;

	struct alo_pool_s *allocator;
} vk_device_memory_t;

static struct {
	vk_device_memory_t allocs[MAX_DEVMEM_ALLOCS];
	int num_allocs;

	qboolean verbose;
} g_vk_devmem;

static int findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	for (int i = 0; i < (int)vk_core.physical_device.memory_properties2.memoryProperties.memoryTypeCount; ++i) {
		if (!(type_index_bits & (1 << i)))
			continue;

		if ((vk_core.physical_device.memory_properties2.memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}

	return UINT32_MAX;
}

static VkDeviceSize optimalSize(VkDeviceSize size) {
	if (size < DEFAULT_ALLOCATION_SIZE)
		return DEFAULT_ALLOCATION_SIZE;

	// TODO:
	// 1. have a way to iterate for smaller sizes if allocation failed
	// 2. bump to nearest power-of-two-ish based size (e.g. a multiple of 32Mb or something)

	return size;
}

static int allocateDeviceMemory(VkMemoryRequirements req, int type_index, VkMemoryAllocateFlags allocate_flags) {
	if (g_vk_devmem.num_allocs == MAX_DEVMEM_ALLOCS) {
		gEngine.Host_Error("Ran out of device memory allocation slots\n");
		return -1;
	}

	{
		const VkMemoryAllocateFlagsInfo mafi = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
			.flags = allocate_flags,
		};

		const VkMemoryAllocateInfo mai = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = allocate_flags ? &mafi : NULL,
			.allocationSize = optimalSize(req.size),
			.memoryTypeIndex = type_index,
		};

		if (g_vk_devmem.verbose) {
			gEngine.Con_Reportf("allocateDeviceMemory size=%llu memoryTypeBits=0x%x allocate_flags=0x%x => typeIndex=%d\n",
				(unsigned long long)mai.allocationSize, req.memoryTypeBits,
				allocate_flags,
				mai.memoryTypeIndex);
		}
		ASSERT(mai.memoryTypeIndex != UINT32_MAX);

		vk_device_memory_t *device_memory = g_vk_devmem.allocs + g_vk_devmem.num_allocs;
		XVK_CHECK(vkAllocateMemory(vk_core.device, &mai, NULL, &device_memory->device_memory));
		device_memory->property_flags = vk_core.physical_device.memory_properties2.memoryProperties.memoryTypes[mai.memoryTypeIndex].propertyFlags;
		device_memory->allocate_flags = allocate_flags;
		device_memory->type_index = mai.memoryTypeIndex;
		device_memory->refcount = 0;
		device_memory->size = mai.allocationSize;

		device_memory->allocator = aloPoolCreate(device_memory->size, 0, 16);

		if (device_memory->property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
			XVK_CHECK(vkMapMemory(vk_core.device, device_memory->device_memory, 0, device_memory->size, 0, &device_memory->map));
		} else {
			device_memory->map = NULL;
		}
	}

	return g_vk_devmem.num_allocs++;
}

vk_devmem_t VK_DevMemAllocate(const char *name, VkMemoryRequirements req, VkMemoryPropertyFlags prop_flags, VkMemoryAllocateFlags allocate_flags) {
	vk_devmem_t ret = {0};
	int device_memory_index = -1;
	alo_block_t block;
	const int type_index = findMemoryWithType(req.memoryTypeBits, prop_flags);

	if (g_vk_devmem.verbose) {
		gEngine.Con_Reportf("VK_DevMemAllocate name=\"%s\" size=%llu alignment=%llu memoryTypeBits=0x%x prop_flags=%c%c%c%c%c allocate_flags=0x%x => type_index=%d\n",
			name, (unsigned long long)req.size, (unsigned long long)req.alignment, req.memoryTypeBits,
			prop_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? 'D' : '.',
			prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? 'V' : '.',
			prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? 'C' : '.',
			prop_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? '$' : '.',
			prop_flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? 'L' : '.',
			allocate_flags, type_index);
	}

	for (int i = 0; i < g_vk_devmem.num_allocs; ++i) {
		vk_device_memory_t *const device_memory = g_vk_devmem.allocs + i;
		if (device_memory->type_index != type_index)
			continue;

		if ((device_memory->allocate_flags & allocate_flags) != allocate_flags)
			continue;

		if ((device_memory->property_flags & prop_flags) != prop_flags)
			continue;

		block = aloPoolAllocate(device_memory->allocator, req.size, req.alignment);
		if (block.size == 0)
			continue;

		device_memory_index = i;
		break;
	}

	if (device_memory_index < 0) {
		device_memory_index = allocateDeviceMemory(req, type_index, allocate_flags);
		ASSERT(device_memory_index >= 0);
		if (device_memory_index < 0)
			return ret;

		block = aloPoolAllocate(g_vk_devmem.allocs[device_memory_index].allocator, req.size, req.alignment);
		ASSERT(block.size != 0);
	}

	{
		vk_device_memory_t *const device_memory = g_vk_devmem.allocs + device_memory_index;
		ret.device_memory = device_memory->device_memory;
		ret.offset = block.offset;
		ret.mapped = device_memory->map ? (char*)device_memory->map + block.offset : NULL;

		if (g_vk_devmem.verbose) {
			gEngine.Con_Reportf("Allocated devmem=%d block=%d offset=%d size=%d\n", device_memory_index, block.index, (int)block.offset, (int)block.size);
		}

		device_memory->refcount++;
		ret.priv_.devmem = device_memory_index;
		ret.priv_.block = block.index;

		return ret;
	}
}

void VK_DevMemFree(const vk_devmem_t *mem) {
	ASSERT(mem->priv_.devmem >= 0);
	ASSERT(mem->priv_.devmem < g_vk_devmem.num_allocs);

	vk_device_memory_t *const device_memory = g_vk_devmem.allocs + mem->priv_.devmem;
	ASSERT(mem->device_memory == device_memory->device_memory);

	if (g_vk_devmem.verbose) {
		gEngine.Con_Reportf("Freeing devmem=%d block=%d\n", mem->priv_.devmem, mem->priv_.block);
	}

	aloPoolFree(device_memory->allocator, mem->priv_.block);

	ASSERT(device_memory->refcount > 0);
	device_memory->refcount--;

	if (device_memory->refcount == 0) {
		// FIXME free empty
		gEngine.Con_Reportf(S_WARN "devmem[%d] reached refcount=0\n", mem->priv_.devmem);
	}
}

static qboolean Impl_Init( void ) {
	XRVkModule_OnInitStart( g_module_devmem );

	g_vk_devmem.verbose = !!gEngine.Sys_CheckParm("-vkdebugmem");

	XRVkModule_OnInitEnd( g_module_devmem );
	return true;
}

static void Impl_Shutdown( void ) {
	XRVkModule_OnShutdownStart( g_module_devmem );

	for (int i = 0; i < g_vk_devmem.num_allocs; ++i) {
		const vk_device_memory_t *const device_memory = g_vk_devmem.allocs + i;
		ASSERT(device_memory->refcount == 0);

		// TODO check that everything has been freed
		aloPoolDestroy(device_memory->allocator);

		if (device_memory->map)
			vkUnmapMemory(vk_core.device, device_memory->device_memory);

		vkFreeMemory(vk_core.device, device_memory->device_memory, NULL);
	}

	g_vk_devmem.num_allocs = 0;

	XRVkModule_OnShutdownEnd( g_module_devmem );
}