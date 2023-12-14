#include "vk_devmem.h"
#include "alolcator.h"
#include "r_speeds.h"

#define MAX_DEVMEM_ALLOC_SLOTS 16
#define DEFAULT_ALLOCATION_SIZE (64 * 1024 * 1024)

#define MODULE_NAME "devmem"

typedef struct vk_device_memory_slot_s {
	uint32_t type_index;
	VkMemoryPropertyFlags property_flags; // device vs host
	VkMemoryAllocateFlags allocate_flags;
	VkDeviceMemory device_memory;
	VkDeviceSize size;

	void *mapped;
	int refcount;

	struct alo_pool_s *allocator;
} vk_device_memory_slot_t;

typedef struct vk_devmem_allocation_stats_s {
	// Metrics updated on every allocation and deallocation.
	struct {
		int allocations;          // Current number of active (not freed) allocations.
		int allocated;            // Current size of allocated memory.
		int align_holes;          // Current number of alignment holes in active (not freed) allocations.
		int align_holes_size;     // Current size of alignment holes in active (not freed) allocations.
	} current;

	// Metrics updated whenever new highest value is registered.
	struct {
		int allocations;          // Highest number of allocations made.
		int allocated;            // Largest size of allocated memory. 
		int align_holes;          // Highest number of alignment holes made.
		int align_holes_size;     // Largest size of alignment holes made. 
		int align_hole_size;      // Largest size of the largest alignment hole made.
	} peak;
} vk_devmem_allocation_stats_t;

static struct {
	vk_device_memory_slot_t alloc_slots[MAX_DEVMEM_ALLOC_SLOTS];
	int alloc_slots_count;

	// Size of memory allocated on logical device `VkDevice` 
	// (which is basically bound to physical device `VkPhysicalDevice`).
	int device_allocated;

	// Allocation statistics for each usage type.
	vk_devmem_allocation_stats_t stats[VK_DEVMEM_USAGE_TYPES_COUNT];

	qboolean verbose;
} g_devmem;

// Format for printf-like functions to represent bits of `VkMemoryPropertyFlags`.
// Usage example:   gEngine.Con_Reportf( "property_flags: " PRI_VKMEMPROPFLAGS_FMT "\n", PRI_VKMEMPROPFLAGS_ARG( property_flags ) );
#define PRI_VKMEMPROPFLAGS_FMT "%c%c%c%c%c"

// Inline arguments for `PRI_VKMEMPROPFLAGS_FMT` format macro.
#define PRI_VKMEMPROPFLAGS_ARG( flags ) \
	( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT     ) ? 'D' : '-', \
	( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT     ) ? 'V' : '-', \
	( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT    ) ? 'C' : '-', \
	( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT      ) ? '$' : '-', \
	( flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ) ? 'L' : '-'
	// Not used:
	// VK_MEMORY_PROPERTY_PROTECTED_BIT
	// VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
	// VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD
	// VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV

// Format for printf-like functions to represent bits of `VkMemoryAllocateFlags`.
// Usage example:   gEngine.Con_Reportf( "allocate_flags: " PRI_VKMEMALLOCFLAGS_FMT "\n", PRI_VKMEMALLOCFLAGS_ARG( allocate_flags ) );
#define PRI_VKMEMALLOCFLAGS_FMT "%c%c%c"

// Inline arguments for `PRI_VKMEMALLOCFLAGS_FMT` format macro.
#define PRI_VKMEMALLOCFLAGS_ARG( flags ) \
	( flags & VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT                   ) ? 'M' : '-', \
	( flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT                ) ? 'A' : '-', \
	( flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT ) ? 'R' : '-'

// Register allocation in overall stats and for the corresponding type stats too.
#define REGISTER_ALLOCATION( type, size, alignment, alignment_hole ) \
	register_allocation_for_type( VK_DEVMEM_USAGE_TYPE_ALL, size, alignment, alignment_hole ); \
	register_allocation_for_type( type, size, alignment, alignment_hole );

// Register deallocation (freeing) in overall stats and for the corresponding type stats too.
#define REGISTER_FREE( type, size, alignment, alignment_hole ) \
	register_free_for_type( VK_DEVMEM_USAGE_TYPE_ALL, size, alignment, alignment_hole ); \
	register_free_for_type( type, size, alignment, alignment_hole );

// Register allocation in stats of the provided type.
static void register_allocation_for_type( vk_devmem_usage_type_t type, int size, int alignment, int alignment_hole ) {
	ASSERT( type >= VK_DEVMEM_USAGE_TYPE_ALL );
	ASSERT( type <  VK_DEVMEM_USAGE_TYPES_COUNT );

	vk_devmem_allocation_stats_t *const stats = &g_devmem.stats[type];
	
	/* Update allocations stats. */
	
	// Update current allocations.
	stats->current.allocations += 1;
	stats->current.allocated   += size;

	// Update peak allocations.
	if ( stats->peak.allocations < stats->current.allocations )
		stats->peak.allocations = stats->current.allocations;

	if ( stats->peak.allocated < stats->current.allocated )
		stats->peak.allocated = stats->current.allocated;

	/* Update alignment holes stats. */

	if ( alignment_hole > 0 ) {
		// Update current alignment holes stats.
		stats->current.align_holes      += 1;
		stats->current.align_holes_size += alignment_hole;

		// Update peak alignment holes stats.
		if ( stats->peak.align_holes < stats->current.align_holes )
			stats->peak.align_holes = stats->current.align_holes;

		if ( stats->peak.align_holes_size < stats->current.align_holes_size )
			stats->peak.align_holes_size = stats->current.align_holes_size;

		if ( stats->peak.align_hole_size < alignment_hole )
			stats->peak.align_hole_size = alignment_hole;
	}
}

// Register deallocation (freeing) in stats of the provided type.
static void register_free_for_type( vk_devmem_usage_type_t type, int size, int alignment, int alignment_hole ) {
	ASSERT( type >= VK_DEVMEM_USAGE_TYPE_ALL );
	ASSERT( type <  VK_DEVMEM_USAGE_TYPES_COUNT );

	vk_devmem_allocation_stats_t *const stats = &g_devmem.stats[type];

	/* Update current allocations stats. */

	stats->current.allocations -= 1;
	stats->current.allocated   -= size;
	
	/* Update current alignment holes stats. */

	if ( alignment_hole > 0 ) {
		stats->current.align_holes      -= 1;
		stats->current.align_holes_size -= size;
	}
}

static int findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	const VkPhysicalDeviceMemoryProperties *const properties = &vk_core.physical_device.memory_properties2.memoryProperties;
	for ( int type = 0; type < (int)properties->memoryTypeCount; type += 1 ) {
		if ( !( type_index_bits & ( 1 << type ) ) )
			continue;

		if ( ( properties->memoryTypes[type].propertyFlags & flags ) == flags )
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
	if ( g_devmem.alloc_slots_count == MAX_DEVMEM_ALLOC_SLOTS ) {
		gEngine.Host_Error( "Ran out of %d device memory allocation slots\n", (int)MAX_DEVMEM_ALLOC_SLOTS );
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

		if ( g_devmem.verbose ) {
			gEngine.Con_Reportf( "  ^3->^7 ^6AllocateDeviceMemory:^7 { size: %s, memoryTypeBits: 0x%x, allocate_flags: " PRI_VKMEMALLOCFLAGS_FMT " => typeIndex: %d }\n",
				Q_memprint( (float)mai.allocationSize ), req.memoryTypeBits, PRI_VKMEMALLOCFLAGS_ARG( allocate_flags ), mai.memoryTypeIndex );
		}
		ASSERT( mai.memoryTypeIndex != UINT32_MAX );

		vk_device_memory_slot_t *slot = &g_devmem.alloc_slots[g_devmem.alloc_slots_count];
		XVK_CHECK( vkAllocateMemory( vk_core.device, &mai, NULL, &slot->device_memory ) );

		const VkPhysicalDeviceMemoryProperties *const properties = &vk_core.physical_device.memory_properties2.memoryProperties;
		slot->property_flags = properties->memoryTypes[mai.memoryTypeIndex].propertyFlags;
		slot->allocate_flags = allocate_flags;
		slot->type_index     = mai.memoryTypeIndex;
		slot->refcount       = 0;
		slot->size           = mai.allocationSize;

		g_devmem.device_allocated += mai.allocationSize;

		const int expected_allocations = 0;
		const int min_alignment = 16;
		slot->allocator = aloPoolCreate( slot->size, expected_allocations, min_alignment );

		if ( slot->property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
			XVK_CHECK( vkMapMemory( vk_core.device, slot->device_memory, 0, slot->size, 0, &slot->mapped ) );
			if ( g_devmem.verbose ) {
				size_t device        = (size_t) vk_core.device;
				size_t device_memory = (size_t) slot->device_memory;
				// `z` - specifies `size_t` length
				gEngine.Con_Reportf( "  ^3->^7 ^6Mapped:^7 { device: 0x%zx, mapped: 0x%zx, device_memory: 0x%zx, size: %s }\n",
					device, slot->mapped, device_memory, Q_memprint( (float)slot->size ) );
			}
		} else {
			slot->mapped = NULL;
		}
	}

	return g_devmem.alloc_slots_count++;
}

vk_devmem_t VK_DevMemAllocate(const char *name, vk_devmem_usage_type_t usage_type, vk_devmem_allocate_args_t devmem_allocate_args) {
	VkMemoryRequirements  req            = devmem_allocate_args.requirements;
	VkMemoryPropertyFlags property_flags = devmem_allocate_args.property_flags;
	VkMemoryAllocateFlags allocate_flags = devmem_allocate_args.allocate_flags;
	
	vk_devmem_t devmem = { .usage_type = usage_type };
	const int type_index = findMemoryWithType(req.memoryTypeBits, property_flags);

	if ( g_devmem.verbose ) {
		const char *usage_type_str = VK_DevMemUsageTypeString( usage_type );
		gEngine.Con_Reportf( "^3VK_DevMemAllocate:^7 { name: \"%s\", usage: %s, size: %s, alignment: %llu, memoryTypeBits: 0x%x, property_flags: " PRI_VKMEMPROPFLAGS_FMT ", allocate_flags: " PRI_VKMEMALLOCFLAGS_FMT " => type_index: %d }\n",
			name, usage_type_str, Q_memprint( (float)req.size ), req.alignment, req.memoryTypeBits, PRI_VKMEMPROPFLAGS_ARG( property_flags ), PRI_VKMEMALLOCFLAGS_ARG( allocate_flags ), type_index );
	}

	if ( vk_core.rtx ) {
		// TODO this is needed only for the ray tracer and only while there's no proper staging
		// Once staging is established, we can avoid forcing this on every devmem allocation
		allocate_flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	}

	alo_block_t block;
	int selected_slot_index = -1;
	for ( int slot_index = 0; slot_index < g_devmem.alloc_slots_count; slot_index += 1 ) {
		vk_device_memory_slot_t *const slot = g_devmem.alloc_slots + slot_index;
		if ( slot->type_index != type_index )
			continue;

		if ( ( slot->allocate_flags & allocate_flags ) != allocate_flags )
			continue;

		if ( ( slot->property_flags & property_flags ) != property_flags )
			continue;

		block = aloPoolAllocate( slot->allocator, req.size, req.alignment );
		if ( block.size == 0 )
			continue;

		selected_slot_index = slot_index;
		break;
	}

	if ( selected_slot_index < 0 ) {
		selected_slot_index = allocateDeviceMemory( req, type_index, allocate_flags );
		ASSERT( selected_slot_index >= 0 );
		if ( selected_slot_index < 0 )
			return devmem;

		struct alo_pool_s *allocator = g_devmem.alloc_slots[selected_slot_index].allocator;
		block = aloPoolAllocate( allocator, req.size, req.alignment );
		ASSERT( block.size != 0 );
	}

	{
		vk_device_memory_slot_t *const slot = g_devmem.alloc_slots + selected_slot_index;
		devmem.device_memory = slot->device_memory;
		devmem.offset        = block.offset;
		devmem.mapped        = slot->mapped ? (char *)slot->mapped + block.offset : NULL;

		if ( g_devmem.verbose ) {
			gEngine.Con_Reportf( "  ^3->^7 Allocated: { slot: %d, block: %d, offset: %u, size: %u, hole: %u }\n", 
				selected_slot_index, block.index, block.offset, block.size, block.alignment_hole );
		}

		slot->refcount++;
		devmem.internal.slot_index           = selected_slot_index;
		devmem.internal.block_index          = block.index;
		devmem.internal.block_size           = block.size;
		devmem.internal.block_alignment      = req.alignment;
		devmem.internal.block_alignment_hole = block.alignment_hole;

		REGISTER_ALLOCATION( usage_type, block.size, req.alignment, block.alignment_hole );

		return devmem;
	}
}

void VK_DevMemFree(const vk_devmem_t *mem) {
	int slot_index = mem->internal.slot_index;
	ASSERT( slot_index >= 0 );
	ASSERT( slot_index < g_devmem.alloc_slots_count );

	vk_device_memory_slot_t *const slot = g_devmem.alloc_slots + slot_index;
	ASSERT( mem->device_memory == slot->device_memory );

	if ( g_devmem.verbose ) {
		const char *usage_type = VK_DevMemUsageTypeString( mem->usage_type );
		gEngine.Con_Reportf( "^2VK_DevMemFree:^7 { slot: %d, block: %d, usage: %s, size: %s, alignment: %d, alignment_hole: %d }\n",
			slot_index, mem->internal.block_index, usage_type, Q_memprint( (float)mem->internal.block_size ), mem->internal.block_alignment, mem->internal.block_alignment_hole );
	}

	aloPoolFree( slot->allocator, mem->internal.block_index );

	REGISTER_FREE( mem->usage_type, mem->internal.block_size, mem->internal.block_alignment, mem->internal.block_alignment_hole );

	slot->refcount--;
}

// Register single stats variable.
#define REGISTER_STATS_METRIC( var, metric_name, var_name, metric_type ) \
	R_SpeedsRegisterMetric( &(var), MODULE_NAME, #metric_name, metric_type, /*reset*/ false, #var_name, __FILE__, __LINE__ );

// NOTE(nilsoncore): I know, this is a mess... Sorry.
// It could have been avoided by having short `VK_DevMemUsageTypes` enum names,
// but I have done it this way because I want those enum names to be as descriptive as possible.
// This basically replaces those enum names with ones provided by suffixes, which are just their endings.
//
//	                     | var                              | metric_name                            | var_name                                              | metric_type        |
//	                     | -------------------------------- | -------------------------------------- | ----------------------------------------------------- | ------------------ |
#define REGISTER_STATS_METRICS( usage_type, usage_suffix ) { \
	vk_devmem_allocation_stats_t *const stats = &g_devmem.stats[usage_type]; \
 	REGISTER_STATS_METRIC( stats->current.allocations,       current_allocations##usage_suffix,       g_devmem.stats[usage_suffix].current.allocations,       kSpeedsMetricCount ); \
 	REGISTER_STATS_METRIC( stats->current.allocated,         current_allocated##usage_suffix,         g_devmem.stats[usage_suffix].current.allocated,         kSpeedsMetricBytes ); \
 	REGISTER_STATS_METRIC( stats->current.align_holes,       current_align_holes##usage_suffix,       g_devmem.stats[usage_suffix].current.align_holes,       kSpeedsMetricCount ); \
 	REGISTER_STATS_METRIC( stats->current.align_holes_size,  current_align_holes_size##usage_suffix,  g_devmem.stats[usage_suffix].current.align_holes_size,  kSpeedsMetricBytes ); \
 	REGISTER_STATS_METRIC( stats->peak.allocations,          peak_allocations##usage_suffix,          g_devmem.stats[usage_suffix].peak.allocations,          kSpeedsMetricCount ); \
 	REGISTER_STATS_METRIC( stats->peak.allocated,            peak_allocated##usage_suffix,            g_devmem.stats[usage_suffix].peak.allocated,            kSpeedsMetricBytes ); \
 	REGISTER_STATS_METRIC( stats->peak.align_holes,          peak_align_holes##usage_suffix,          g_devmem.stats[usage_suffix].peak.align_holes,          kSpeedsMetricCount ); \
 	REGISTER_STATS_METRIC( stats->peak.align_holes_size,     peak_align_holes_size##usage_suffix,     g_devmem.stats[usage_suffix].peak.align_holes_size,     kSpeedsMetricBytes ); \
 	REGISTER_STATS_METRIC( stats->peak.align_hole_size,      peak_align_hole_size##usage_suffix,      g_devmem.stats[usage_suffix].peak.align_hole_size,      kSpeedsMetricBytes ); \
}

qboolean VK_DevMemInit( void ) {
	g_devmem.verbose = !!gEngine.Sys_CheckParm( "-vkdebugmem" );

	// Register standalone metrics.
	R_SPEEDS_METRIC( g_devmem.alloc_slots_count, "allocated_slots", kSpeedsMetricCount );
	R_SPEEDS_METRIC( g_devmem.device_allocated, "device_allocated", kSpeedsMetricBytes );
	
	// Register stats metrics for each usage type.
	REGISTER_STATS_METRICS( VK_DEVMEM_USAGE_TYPE_ALL,    _ALL );
	REGISTER_STATS_METRICS( VK_DEVMEM_USAGE_TYPE_BUFFER, _BUFFER );
	REGISTER_STATS_METRICS( VK_DEVMEM_USAGE_TYPE_IMAGE,  _IMAGE );
	
	return true;
}

void VK_DevMemDestroy( void ) {
	for ( int slot_index = 0; slot_index < g_devmem.alloc_slots_count; slot_index += 1 ) {
		const vk_device_memory_slot_t *const slot = g_devmem.alloc_slots + slot_index;
		ASSERT( slot->refcount == 0 );

		// TODO check that everything has been freed
		aloPoolDestroy( slot->allocator );

		if ( slot->mapped )
			vkUnmapMemory( vk_core.device, slot->device_memory );

		vkFreeMemory( vk_core.device, slot->device_memory, NULL );
	}

	g_devmem.alloc_slots_count = 0;
}

const char *VK_DevMemUsageTypeString( vk_devmem_usage_type_t type ) {
	ASSERT( type >= VK_DEVMEM_USAGE_TYPE_ALL );
	ASSERT( type < VK_DEVMEM_USAGE_TYPES_COUNT );

	switch ( type ) {
		case VK_DEVMEM_USAGE_TYPE_ALL:     return "ALL";
		case VK_DEVMEM_USAGE_TYPE_BUFFER:  return "BUFFER";
		case VK_DEVMEM_USAGE_TYPE_IMAGE:   return "IMAGE";
	}

	return "(unknown)";
}
