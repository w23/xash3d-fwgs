#include "r_block.h"

#include "vk_common.h" // ASSERT
#include "vk_core.h" // vk_core.pool

typedef struct r_blocks_block_s {
	int long_index;
	uint32_t refcount;
} r_blocks_block_t;

// logical blocks
//  <---- lifetime long -><-- once -->
// [.....................|............]
//  <--- pool         --><-- ring --->
//    offset ?       --->

static int allocMetablock(r_blocks_t *blocks) {
	return aloIntPoolAlloc(&blocks->blocks.freelist);
	// TODO grow if needed
}

r_block_t R_BlockAllocLong(r_blocks_t *blocks, uint32_t size, uint32_t alignment) {
	r_block_t ret = {
		.offset = ALO_ALLOC_FAILED,
		.size = 0,
		.impl_ = {-1}
	};

	const alo_block_t ablock = aloPoolAllocate(blocks->long_pool, size, alignment);
	if (ablock.offset == ALO_ALLOC_FAILED) {
		gEngine.Con_Reportf(S_ERROR "aloPoolAllocate failed\n");
		return ret;
	}

	const int metablock_index = allocMetablock(blocks);
	if (metablock_index < 0) {
		gEngine.Con_Reportf(S_ERROR "allocMetablock failed\n");
		aloPoolFree(blocks->long_pool, ablock.index);
		return ret;
	}

	ret.offset = ablock.offset;
	ret.size = ablock.size;
	ret.impl_.index = metablock_index;
	ret.impl_.blocks = blocks;

	r_blocks_block_t *metablock = blocks->blocks.storage + metablock_index;
	metablock->long_index = ablock.index;
	metablock->refcount = 1;

	/* gEngine.Con_Reportf("block alloc %dKiB => index=%d offset=%u\n", (int)size/1024, metablock_index, (int)ret.offset); */

	blocks->allocated_long += size;
	return ret;
}

uint32_t R_BlockAllocOnce(r_blocks_t *blocks, uint32_t size, uint32_t alignment) {
	const uint32_t offset = R_FlippingBuffer_Alloc(&blocks->once.flipping, size, alignment);
	if (offset == ALO_ALLOC_FAILED)
		return ALO_ALLOC_FAILED;

	return offset + blocks->once.ring_offset;
}

void R_BlocksCreate(r_blocks_t *blocks, uint32_t size, uint32_t once_size, int expected_allocs) {
	memset(blocks, 0, sizeof(*blocks));

	blocks->size = size;
	blocks->allocated_long = 0;

	blocks->long_pool = aloPoolCreate(size - once_size, expected_allocs, 4);
	aloIntPoolGrow(&blocks->blocks.freelist, expected_allocs);
	blocks->blocks.storage = Mem_Malloc(vk_core.pool, expected_allocs * sizeof(blocks->blocks.storage[0]));

	blocks->once.ring_offset = size - once_size;
	R_FlippingBuffer_Init(&blocks->once.flipping, once_size);
}

void R_BlockRelease(const r_block_t *block) {
	r_blocks_t *const blocks = block->impl_.blocks;
	if (!blocks || !block->size)
		return;

	ASSERT(block->impl_.index >= 0);
	ASSERT(block->impl_.index < blocks->blocks.freelist.capacity);

	r_blocks_block_t *const metablock = blocks->blocks.storage + block->impl_.index;

	/* gEngine.Con_Reportf("block release index=%d offset=%u refcount=%d\n", block->impl_.index, (int)block->offset, (int)metablock->refcount); */

	ASSERT (metablock->refcount > 0);
	if (--metablock->refcount)
		return;

	/* gEngine.Con_Reportf("block free index=%d offset=%u\n", block->impl_.index, (int)block->offset); */

	aloPoolFree(blocks->long_pool, metablock->long_index);
	aloIntPoolFree(&blocks->blocks.freelist, block->impl_.index);
	blocks->allocated_long -= block->size;
}

void R_BlocksDestroy(r_blocks_t *blocks) {
	for (int i = blocks->blocks.freelist.free; i < blocks->blocks.freelist.capacity; ++i) {
		r_blocks_block_t *b = blocks->blocks.storage + blocks->blocks.freelist.free_list[i];
		ASSERT(b->refcount == 0);
	}

	aloPoolDestroy(blocks->long_pool);
	aloIntPoolDestroy(&blocks->blocks.freelist);
	Mem_Free(blocks->blocks.storage);
}

// Clear all LifetimeOnce blocks, checking that they are not referenced by anything
void R_BlocksClearOnce(r_blocks_t *blocks) {
	R_FlippingBuffer_Flip(&blocks->once.flipping);
}
