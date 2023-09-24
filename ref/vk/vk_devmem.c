#include "vk_devmem.h"
#include "alolcator.h"
#include "r_speeds.h"

#define MAX_DEVMEM_ALLOC_SLOTS 16
#define DEFAULT_ALLOCATION_SIZE (64 * 1024 * 1024)

#define MODULE_NAME "devmem"

typedef struct {
	uint32_t type_index;
	VkMemoryPropertyFlags property_flags; // device vs host
	VkMemoryAllocateFlags allocate_flags;
	VkDeviceMemory device_memory;
	VkDeviceSize size;

	void *mapped;
	int refcount;

	struct alo_pool_s *allocator;
} vk_device_memory_slot_t;

static struct {
	vk_device_memory_slot_t alloc_slots[MAX_DEVMEM_ALLOC_SLOTS];
	int alloc_slots_count;

	int device_allocated;
	int allocated_current;
	int allocated_total;
	int freed_total;

	qboolean verbose;
} g_vk_devmem;

#define VKMEMPROPFLAGS_COUNT 5
#define VKMEMPROPFLAGS_MINSTRLEN (VKMEMPROPFLAGS_COUNT + 1)

// Fills string `out_flags` with characters at each corresponding flag slot.
// Returns number of flags set.
static int VK_MemoryPropertyFlags_String( VkMemoryPropertyFlags flags, char *out_flags, size_t out_flags_size ) {
	ASSERT( out_flags_size >= VKMEMPROPFLAGS_MINSTRLEN );
	int set_flags = 0;
	if ( out_flags_size < VKMEMPROPFLAGS_MINSTRLEN ) {
		out_flags[0] = '\0';
		return set_flags;
	}

	int flag = 0;
	if ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT     )  {out_flags[flag] = 'D'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT     )  {out_flags[flag] = 'V'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT    )  {out_flags[flag] = 'C'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT      )  {out_flags[flag] = '$'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT )  {out_flags[flag] = 'L'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	// VK_MEMORY_PROPERTY_PROTECTED_BIT
	// VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
	// VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD
	// VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV
	out_flags[flag] = '\0';

	return set_flags;
}

#define VKMEMALLOCFLAGS_COUNT 3
#define VKMEMALLOCFLAGS_MINSTRLEN (VKMEMALLOCFLAGS_COUNT + 1)

// Fills string `out_flags` with characters at each corresponding flag slot.
// Returns number of flags set.
static int VK_MemoryAllocateFlags_String( VkMemoryAllocateFlags flags, char *out_flags, size_t out_flags_size ) {
	ASSERT( out_flags_size >= VKMEMALLOCFLAGS_MINSTRLEN );
	int set_flags = 0;
	if ( out_flags_size < VKMEMALLOCFLAGS_MINSTRLEN ) {
		out_flags[0] = '\0';
		return set_flags;
	}

	int flag = 0;
	if ( flags & VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT                   )  {out_flags[flag] = 'M'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT                )  {out_flags[flag] = 'A'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	if ( flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT )  {out_flags[flag] = 'R'; set_flags += 1;}  else  {out_flags[flag] = '-';}  flag += 1;
	out_flags[flag] = '\0';

	return set_flags;
}

static int findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	VkPhysicalDeviceMemoryProperties properties = vk_core.physical_device.memory_properties2.memoryProperties;
	for ( int type = 0; type < (int)properties.memoryTypeCount; type += 1 ) {
		if ( !( type_index_bits & ( 1 << type ) ) )
			continue;

		if ( ( properties.memoryTypes[type].propertyFlags & flags ) == flags )
			return type;
	}

	return UINT32_MAX;
}

static VkDeviceSize optimalSize(VkDeviceSize size) {
	if ( size < DEFAULT_ALLOCATION_SIZE )
		return DEFAULT_ALLOCATION_SIZE;

	// TODO:
	// 1. have a way to iterate for smaller sizes if allocation failed
	// 2. bump to nearest power-of-two-ish based size (e.g. a multiple of 32Mb or something)

	return size;
}

static int allocateDeviceMemory(VkMemoryRequirements req, int type_index, VkMemoryAllocateFlags allocate_flags) {
	if ( g_vk_devmem.alloc_slots_count == MAX_DEVMEM_ALLOC_SLOTS ) {
		gEngine.Host_Error( "Ran out of device memory allocation slots\n" );
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

		if ( g_vk_devmem.verbose ) {
			char allocate_flags_str[VKMEMALLOCFLAGS_MINSTRLEN];
			VK_MemoryAllocateFlags_String( allocate_flags, &allocate_flags_str[0], sizeof( allocate_flags_str ) );
			unsigned long long size = (unsigned long long) mai.allocationSize;
			gEngine.Con_Reportf( "  ^3->^7 ^6AllocateDeviceMemory:^7 { size: %llu, memoryTypeBits: 0x%x, allocate_flags: %s => typeIndex: %d }\n",
				size, req.memoryTypeBits, allocate_flags_str, mai.memoryTypeIndex );
		}
		ASSERT( mai.memoryTypeIndex != UINT32_MAX );

		vk_device_memory_slot_t *slot = &g_vk_devmem.alloc_slots[g_vk_devmem.alloc_slots_count];
		XVK_CHECK( vkAllocateMemory( vk_core.device, &mai, NULL, &slot->device_memory ) );

		VkPhysicalDeviceMemoryProperties properties = vk_core.physical_device.memory_properties2.memoryProperties;
		slot->property_flags = properties.memoryTypes[mai.memoryTypeIndex].propertyFlags;
		slot->allocate_flags = allocate_flags;
		slot->type_index     = mai.memoryTypeIndex;
		slot->refcount       = 0;
		slot->size           = mai.allocationSize;

		g_vk_devmem.device_allocated += mai.allocationSize;

		const int expected_allocations = 0;
		const int min_alignment = 16;
		slot->allocator = aloPoolCreate( slot->size, expected_allocations, min_alignment );

		if ( slot->property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
			XVK_CHECK( vkMapMemory( vk_core.device, slot->device_memory, 0, slot->size, 0, &slot->mapped ) );
			if ( g_vk_devmem.verbose ) {
				size_t size          = (size_t) slot->size;
				size_t device        = (size_t) vk_core.device;
				size_t device_memory = (size_t) slot->device_memory;
				// `z` - specifies `size_t` length
				gEngine.Con_Reportf( "  ^3->^7 ^6Mapped:^7 { device: 0x%zx, device_memory: 0x%zx, size: %zu }\n",
					device, device_memory, size );
			}
		} else {
			slot->mapped = NULL;
		}
	}

	return g_vk_devmem.alloc_slots_count++;
}

vk_devmem_t VK_DevMemAllocate(const char *name, VkMemoryRequirements req, VkMemoryPropertyFlags property_flags, VkMemoryAllocateFlags allocate_flags) {
	vk_devmem_t devmem = {0};
	const int type_index = findMemoryWithType(req.memoryTypeBits, property_flags);

	if ( g_vk_devmem.verbose ) {
		char property_flags_str[VKMEMPROPFLAGS_MINSTRLEN];
		char allocate_flags_str[VKMEMALLOCFLAGS_MINSTRLEN];
		VK_MemoryPropertyFlags_String( property_flags, &property_flags_str[0], sizeof( property_flags_str ) );
		VK_MemoryAllocateFlags_String( allocate_flags, &allocate_flags_str[0], sizeof( allocate_flags_str ) );

		unsigned long long req_size      = (unsigned long long) req.size;
		unsigned long long req_alignment = (unsigned long long) req.alignment;
		gEngine.Con_Reportf( "^3VK_DevMemAllocate:^7 { name: \"%s\", size: %llu, alignment: %llu, memoryTypeBits: 0x%x, property_flags: %s, allocate_flags: %s => type_index: %d }\n",
			name, req_size, req_alignment, req.memoryTypeBits, property_flags_str, allocate_flags_str, type_index );
	}

	if ( vk_core.rtx ) {
		// TODO this is needed only for the ray tracer and only while there's no proper staging
		// Once staging is established, we can avoid forcing this on every devmem allocation
		allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	}

	alo_block_t block;
	int slot_index = -1;
	for ( int _slot_index = 0 ; _slot_index < g_vk_devmem.alloc_slots_count; _slot_index += 1 ) {
		vk_device_memory_slot_t *const slot = g_vk_devmem.alloc_slots + _slot_index;
		if ( slot->type_index != type_index )
			continue;

		if ( (slot->allocate_flags & allocate_flags ) != allocate_flags )
			continue;

		if ( ( slot->property_flags & property_flags ) != property_flags )
			continue;

		block = aloPoolAllocate( slot->allocator, req.size, req.alignment );
		if ( block.size == 0 )
			continue;

		slot_index = _slot_index;
		break;
	}

	if ( slot_index < 0 ) {
		slot_index = allocateDeviceMemory( req, type_index, allocate_flags );
		ASSERT( slot_index >= 0 );
		if ( slot_index < 0 )
			return devmem;

		struct alo_pool_s *allocator = g_vk_devmem.alloc_slots[slot_index].allocator;
		block = aloPoolAllocate( allocator, req.size, req.alignment );
		ASSERT( block.size != 0 );
	}

	{
		vk_device_memory_slot_t *const slot = g_vk_devmem.alloc_slots + slot_index;
		devmem.device_memory = slot->device_memory;
		devmem.offset        = block.offset;
		devmem.mapped        = slot->mapped ? (char *)slot->mapped + block.offset : NULL;

		if (g_vk_devmem.verbose) {
			gEngine.Con_Reportf("  ^3->^7 Allocated: { slot: %d, block: %d, offset: %d, size: %d }\n", 
			slot_index, block.index, (int)block.offset, (int)block.size);
		}

		slot->refcount++;
		devmem._slot_index  = slot_index;
		devmem._block_index = block.index;
		devmem._block_size  = block.size;

		g_vk_devmem.allocated_current += block.size;
		g_vk_devmem.allocated_total   += block.size;

		return devmem;
	}
}

void VK_DevMemFree(const vk_devmem_t *mem) {
	ASSERT( mem->_slot_index >= 0 );
	ASSERT( mem->_slot_index < g_vk_devmem.alloc_slots_count );

	int slot_index = mem->_slot_index;
	vk_device_memory_slot_t *const slot = g_vk_devmem.alloc_slots + slot_index;
	ASSERT( mem->device_memory == slot->device_memory );

	if ( g_vk_devmem.verbose ) {
		gEngine.Con_Reportf( "^2VK_DevMemFree:^7 { slot: %d, block: %d }\n", slot_index, mem->_block_index );
	}

	aloPoolFree( slot->allocator, mem->_block_index );

	g_vk_devmem.allocated_current -= mem->_block_size;
	g_vk_devmem.freed_total += mem->_block_size;

	slot->refcount--;
}

qboolean VK_DevMemInit( void ) {
	g_vk_devmem.verbose = gEngine.Sys_CheckParm( "-vkdebugmem" );

	R_SPEEDS_METRIC( g_vk_devmem.alloc_slots_count, "allocated_slots"  , kSpeedsMetricCount );
	R_SPEEDS_METRIC( g_vk_devmem.device_allocated , "device_allocated" , kSpeedsMetricBytes );
	R_SPEEDS_METRIC( g_vk_devmem.allocated_current, "allocated_current", kSpeedsMetricBytes );
	R_SPEEDS_METRIC( g_vk_devmem.allocated_total  , "allocated_total"  , kSpeedsMetricBytes );
	R_SPEEDS_METRIC( g_vk_devmem.freed_total      , "freed_total"      , kSpeedsMetricBytes );
	
	return true;
}

void VK_DevMemDestroy( void ) {
	for ( int slot_index = 0; slot_index < g_vk_devmem.alloc_slots_count; slot_index += 1 ) {
		const vk_device_memory_slot_t *const slot = g_vk_devmem.alloc_slots + slot_index;
		ASSERT( slot->refcount == 0 );

		// TODO check that everything has been freed
		aloPoolDestroy( slot->allocator );

		if ( slot->mapped )
			vkUnmapMemory( vk_core.device, slot->device_memory );

		vkFreeMemory( vk_core.device, slot->device_memory, NULL );
	}

	g_vk_devmem.alloc_slots_count = 0;
}
