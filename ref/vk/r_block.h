#pragma once

#include "r_flipping.h"
#include "alolcator.h"
#include <stdint.h>

struct r_blocks_s;
typedef struct r_block_s {
	uint32_t offset;
	uint32_t size;

	struct {
		int index;
		struct r_blocks_s *blocks;
	} impl_;
} r_block_t;

struct r_blocks_block_s;
typedef struct r_blocks_s {
	uint32_t size;

	struct alo_pool_s *long_pool;

	struct {
		uint32_t ring_offset;
		r_flipping_buffer_t flipping;
	} once;

	struct {
		alo_int_pool_t freelist;
		struct r_blocks_block_s *storage;
	} blocks;

	// This is an estimate, it doesn't count alignment holes
	int allocated_long;
} r_blocks_t;

r_block_t R_BlockAllocLong(r_blocks_t *blocks, uint32_t size, uint32_t alignment);
uint32_t R_BlockAllocOnce(r_blocks_t *blocks, uint32_t size, uint32_t alignment);

//void R_BlockAcquire(r_block_t *block);
void R_BlockRelease(const r_block_t *block);

void R_BlocksCreate(r_blocks_t *blocks, uint32_t max_size, uint32_t once_max, int expected_allocs);
void R_BlocksDestroy(r_blocks_t *blocks);

void R_BlocksClearOnce(r_blocks_t *blocks);
