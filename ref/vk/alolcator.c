#include "alolcator.h"
#include <stdlib.h> // malloc/free
#include <string.h> // memcpy

#define MALLOC malloc
#define FREE free

#ifndef ASSERT
#include <assert.h>
#define ASSERT(...) assert(__VA_ARGS__)
#endif

#ifndef ALIGN_UP
#define ALIGN_UP(ptr, align) ((((ptr) + (align) - 1) / (align)) * (align))
#endif

typedef struct {
	int item_size;
	int capacity;
	int free;
	char *pool;
	int *free_list;
} pool_t;

static void poolGrow(pool_t *pool, int new_capacity) {
	const size_t new_pool_size = pool->item_size * new_capacity;
	int *const new_free_list = MALLOC(new_pool_size + sizeof(int) * new_capacity);
	char *const new_pool = (char*)(new_free_list + new_capacity);
	const int new_items = new_capacity - pool->capacity;

	for (int i = 0; i < pool->free; ++i)
		new_free_list[i] = pool->free_list[i];

	for (int i = 0; i < new_items; ++i)
		new_free_list[pool->free + i] = new_capacity - i - 1;

	if (pool->capacity)
		memcpy(new_pool, pool->pool, pool->item_size * pool->capacity);

	if (pool->free_list)
		FREE(pool->free_list);

	pool->free_list = new_free_list;
	pool->pool = new_pool;
	pool->free += new_items;
	pool->capacity = new_capacity;
}

static pool_t poolCreate(int item_size, int capacity) {
	pool_t pool = {0};
	pool.item_size = item_size;
	poolGrow(&pool, capacity);
	return pool;
}

static void poolDestroy(pool_t* pool) {
	FREE(pool->free_list);
	pool->capacity = 0;
}

// invalidates all poolGet pointers returned prior to this function
static int poolAlloc(pool_t* pool) {
	if (pool->free == 0) {
		const int new_capacity = pool->capacity * 3 / 2;
		poolGrow(pool, new_capacity);
	}

	pool->free--;
	return pool->free_list[pool->free];
}

inline static void* poolGet(pool_t* pool, int item) {
	ASSERT(item >= 0);
	ASSERT(item < pool->capacity);
	return pool->pool + pool->item_size * item;
}

static void poolFree(pool_t* pool, int item) {
	ASSERT(item >= 0);
	ASSERT(item < pool->capacity);
	ASSERT(pool->free < pool->capacity);

	pool->free_list[pool->free++] = item;
}

enum {
	BlockFlag_Empty = 0,
	BlockFlag_Allocated = 1,
};

typedef struct {
	int next, prev;
	int flags;

	alo_size_t begin;
	alo_size_t end;
} block_t;

typedef struct alo_pool_s {
	pool_t blocks;

	alo_size_t min_alignment;
	alo_size_t size;
	int first_block;

	// TODO optimize: first_free block, and chain of free blocks
} alo_pool_t;

#define DEFAULT_CAPACITY 256

// TODO make it not a pointer. Just Init
struct alo_pool_s* aloPoolCreate(alo_size_t size, int expected_allocations, alo_size_t min_alignment) {
	alo_pool_t *pool = MALLOC(sizeof(*pool));
	block_t *b;
	pool->min_alignment = min_alignment;
	pool->size = size;
	pool->blocks = poolCreate(sizeof(block_t), expected_allocations > 0 ? expected_allocations : DEFAULT_CAPACITY);
	pool->first_block = poolAlloc(&pool->blocks);
	b = poolGet(&pool->blocks, pool->first_block);
	b->flags = BlockFlag_Empty;
	b->next = b->prev = -1;
	b->begin = 0;
	b->end = size;
	return pool;
}

void aloPoolDestroy(struct alo_pool_s *pool) {
	poolDestroy(&pool->blocks);
	FREE(pool);
}

static int splitBlockAt(pool_t *blocks, int index, alo_size_t at) {
	block_t *block = poolGet(blocks, index);
	ASSERT(block->begin < at);
	ASSERT(block->end > at);

	const int new_index = poolAlloc(blocks);
	block_t *const new_block = poolGet(blocks, new_index);

	// poolAlloc may reallocate, retrieve pointer again
	block = poolGet(blocks, index);

	if (block->next >= 0) {
		block_t *const next = poolGet(blocks, block->next);
		ASSERT(next->prev == index);
		next->prev = new_index;
	}

	new_block->next = block->next;
	new_block->prev = index;
	new_block->flags = block->flags;
	new_block->end = block->end;
	new_block->begin = at;

	block->next = new_index;
	block->end = at;
	return new_index;
}

alo_block_t aloPoolAllocate(struct alo_pool_s* pool, alo_size_t size, alo_size_t alignment) {
	alo_block_t ret = {.offset = ALO_ALLOC_FAILED};
	block_t *b;
	ASSERT(size > 0);

	alignment = alignment > pool->min_alignment ? alignment : pool->min_alignment;

	for (int i = pool->first_block; i >= 0; i = b->next) {
		b = poolGet(&pool->blocks, i);
		if (b->flags != BlockFlag_Empty)
			continue;

		{
			const alo_size_t offset = ALIGN_UP(b->begin, alignment);
			const alo_size_t end = offset + size;
			const alo_size_t alignment_hole = offset - b->begin;

			if (end > b->end)
				continue;

			// TODO min allocation size?
			if (alignment_hole > 0) {
				// old block remains the alignment_hole
				// new block is where we'll allocate
				i = splitBlockAt(&pool->blocks, i, offset);
				b = poolGet(&pool->blocks, i);
			}

			if (end != b->end) {
				// new block is after the one we'll allocate on
				// so we don't care about it
				splitBlockAt(&pool->blocks, i, end);

				// splitting may have incurred realloc, retrieve the pointer again
				b = poolGet(&pool->blocks, i);
			}

			b->flags = BlockFlag_Allocated;

			ret.index = i;
			ret.offset = offset;
			ret.size = size;
			ret.alignment_hole = alignment_hole;
			break;
		}
	}

	return ret;
}

void aloPoolFree(struct alo_pool_s *pool, int index) {
	block_t *iblock = poolGet(&pool->blocks, index);
	ASSERT((iblock->flags & BlockFlag_Allocated) != 0);

	iblock->flags = BlockFlag_Empty;

	{
		block_t *const prev = (iblock->prev >= 0) ? poolGet(&pool->blocks, iblock->prev) : NULL;
		block_t *const next = (iblock->next >= 0) ? poolGet(&pool->blocks, iblock->next) : NULL;

		// join with previous block if empty
		if (prev && prev->flags == BlockFlag_Empty) {
			const int prev_index = iblock->prev;
			prev->end = iblock->end;
			prev->next = iblock->next;
			if (next)
				next->prev = iblock->prev;

			poolFree(&pool->blocks, index);
			index = prev_index;
			iblock = prev;
		}

		// join with next block if empty
		if (next && next->flags == BlockFlag_Empty) {
			const int next_index = iblock->next;

			const int next_next_index = next->next;
			block_t *next_next = next_next_index >=0 ? poolGet(&pool->blocks, next_next_index) : NULL;

			iblock->end = next->end;
			iblock->next = next_next_index;
			if (next_next)
				next_next->prev = index;

			poolFree(&pool->blocks, next_index);
		}
	}
}

void aloRingInit(alo_ring_t* ring, uint32_t size) {
	ring->size = size;
	ring->head = 0;
	ring->tail = size;
}

// Marks everything up-to-pos as free (expects up-to-pos to be valid)
void aloRingFree(alo_ring_t* ring, uint32_t up_to_pos) {
	ASSERT(up_to_pos < ring->size);
	// FIXME assert that up_to_pos is valid and within allocated region
	if (up_to_pos == ring->head) {
		ring->head = 0;
		ring->tail = ring->size;
	} else
		ring->tail = up_to_pos;
}

// Allocates a new aligned region and returns offset to it (AllocFailed if allocation failed)
uint32_t aloRingAlloc(alo_ring_t* ring, uint32_t size, uint32_t alignment) {
	const uint32_t align = (alignment > 0) ? alignment : 1;
	const uint32_t pos = ALIGN_UP(ring->head, alignment);

	ASSERT(size != 0);

	// [XXX.....XXX]
	//     h    t
	if (ring->head <= ring->tail) {
		if (pos + size > ring->tail)
			return ALO_ALLOC_FAILED;

		ring->head = pos + size;
		return pos;
	}

	// [...XXXXXX...]
	//     t     h
	//  2        1

	// 1. Check if we have enough space immediately in front of head
	if (pos + size <= ring->size) {
		ring->head = pos + size;
		return pos;
	}

	// 2. wrap around
	if (size > ring->tail)
		return ALO_ALLOC_FAILED;

	ring->head = size;
	return 0;
}

//  free--><- allocated
// [a....p|q.r.]
//  free->
// [a....|pq.r.]
//  freeing item:
//  - swap with first allocated
// [a....r|q.p.]

void aloIntPoolGrow(alo_int_pool_t *pool, int new_capacity) {
	int *const new_free_list = MALLOC(sizeof(int) * new_capacity);
	const int new_items = new_capacity - pool->capacity;

	for (int i = 0; i < pool->free; ++i)
		new_free_list[i] = pool->free_list[i];

	for (int i = 0; i < new_items; ++i)
		new_free_list[pool->free + i] = new_capacity - i - 1;

	if (pool->free_list)
		FREE(pool->free_list);

	pool->free_list = new_free_list;
	pool->free += new_items;
	pool->capacity = new_capacity;
}

int aloIntPoolAlloc(alo_int_pool_t *pool) {
	if (pool->free == 0)
		return -1;

	pool->free--;
	return pool->free_list[pool->free];
}

void aloIntPoolFree(alo_int_pool_t *pool, int val) {
	ASSERT(pool->free < pool->capacity);
	ASSERT(val >= 0);
	ASSERT(val < pool->capacity);

	// Manager allocated tail list
	for (int i = pool->free; i < pool->capacity; ++i) {
		if (pool->free_list[i] != val)
			continue;

		const int tmp = pool->free_list[pool->free];
		pool->free_list[pool->free] = val;
		pool->free_list[i] = tmp;

		++pool->free;
		return;
	}

	ASSERT(!"Item not found");
}

void aloIntPoolClear(alo_int_pool_t *pool) {
	// Depends on the fact that the tail free_list contains properly maintained allocated ints
	pool->free = pool->capacity;
}

void aloIntPoolDestroy(alo_int_pool_t *pool) {
	if (pool->free_list)
		FREE(pool->free_list);
}

#if defined(ALOLCATOR_TEST)
#include <stdio.h>
uint32_t rand_pcg32(uint32_t max) {
	if (!max) return 0;
#define PCG32_INITIALIZER   { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL }
	static struct { uint64_t state;  uint64_t inc; } rng = PCG32_INITIALIZER;
	uint64_t oldstate = rng.state;
	// Advance internal state
	rng.state = oldstate * 6364136223846793005ULL + (rng.inc|1);
	// Calculate output function (XSH RR), uses old state for max ILP
	uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
	uint32_t rot = oldstate >> 59u;
	uint32_t ret = (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
	return ret % max;
}

int testRandom(int num_allocs, alo_size_t pool_size, alo_size_t max_align, alo_size_t size_spread) {
	struct alo_pool_s *pool = aloPoolCreate(pool_size, num_allocs / 4 + 1, 1);
	alo_block_t *blocks = MALLOC(num_allocs * sizeof(alo_block_t));

	int allocated = 0;
	for (int i = 0; i < num_allocs; ++i) {
		const alo_size_t align = 1 + rand_pcg32(max_align - 1);
		const alo_size_t size = pool_size / num_allocs - size_spread + rand_pcg32(size_spread * 2);
		blocks[i] = aloPoolAllocate(pool, size, align);

		if (blocks[i].size == 0) {
			// FIXME assert somehow that we really can't fit anything here
			continue;
		}

		++allocated;

		for (int j = 0; j < i; ++j) {
			if (blocks[j].size == 0)
				continue;

			ASSERT((blocks[j].offset >= (blocks[i].offset + blocks[i].size)) || (blocks[i].offset >= (blocks[j].offset + blocks[j].size)));
		}
	}

	FREE(blocks);
	aloPoolDestroy(pool);
	return allocated;
}

int test(void) {
	struct alo_pool_s *pool = aloPoolCreate(1000, 5, 1);

	{
		// Allocate three blocks to fill the memory entirely
		alo_block_t block1 = aloPoolAllocate(pool, 700, 1);
		alo_block_t block2 = aloPoolAllocate(pool, 200, 1);
		alo_block_t block3 = aloPoolAllocate(pool, 100, 1);

		ASSERT(block1.offset == 0);
		ASSERT(block1.size == 700);

		ASSERT(block2.offset == 700);
		ASSERT(block2.size == 200);

		ASSERT(block3.offset == 900);
		ASSERT(block3.size == 100);

		// Delete an realloc the block in the middle
		aloPoolFree(pool, block2.index);
		block2 = aloPoolAllocate(pool, 150, 1);
		ASSERT(block2.offset == 700);
		ASSERT(block2.size == 150);

		// Delete the first block
		aloPoolFree(pool, block1.index);
		block1 = aloPoolAllocate(pool, 650, 1);
		ASSERT(block1.offset == 0);
		ASSERT(block1.size == 650);

		// Delete the last block
		aloPoolFree(pool, block3.index);
		block3 = aloPoolAllocate(pool, 80, 1);
		ASSERT(block3.offset == 850);
		ASSERT(block3.size == 80);

		aloPoolFree(pool, block1.index);
		aloPoolFree(pool, block2.index);
		aloPoolFree(pool, block3.index);

		block1 = aloPoolAllocate(pool, 1000, 1);
		ASSERT(block1.offset == 0);
		ASSERT(block1.size == 1000);
		aloPoolFree(pool, block1.index);
	}

	{
		// Allocate many small blocks
		alo_block_t b[10];
		for (int i = 0; i < 10; ++i) {
			b[i] = aloPoolAllocate(pool, 100, 1);
			ASSERT(b[i].size == 100);
			ASSERT(b[i].offset == 100*i);
		}

		{
			// Ensure the pool is full
			alo_block_t fail = aloPoolAllocate(pool, 100, 1);
			ASSERT(fail.size == 0);
		}

		// free some blocks in a specific order
		aloPoolFree(pool, b[2].index);
		aloPoolFree(pool, b[4].index);
		aloPoolFree(pool, b[3].index);

		// allocate in the hole
		{
			alo_block_t block1, block2 = aloPoolAllocate(pool, 300, 1);
			ASSERT(block2.size == 300);
			ASSERT(block2.offset == 200);

			aloPoolFree(pool, b[7].index);
			aloPoolFree(pool, b[6].index);
			aloPoolFree(pool, b[5].index);

			block1 = aloPoolAllocate(pool, 300, 1);
			ASSERT(block1.size == 300);
			ASSERT(block1.offset == 500);

			aloPoolFree(pool, block1.index);
			aloPoolFree(pool, b[8].index);
			aloPoolFree(pool, b[9].index);
			aloPoolFree(pool, block2.index);

			block1 = aloPoolAllocate(pool, 800, 1);
			ASSERT(block1.size == 800);
			ASSERT(block1.offset == 200);

			aloPoolFree(pool, b[0].index);
			aloPoolFree(pool, b[1].index);
			aloPoolFree(pool, block1.index);

			block1 = aloPoolAllocate(pool, 1000, 1);
			ASSERT(block1.size == 1000);
			ASSERT(block1.offset == 0);
			aloPoolFree(pool, block1.index);
		}
	}

	// Alignment
	{
		alo_block_t b[6];
		b[0] = aloPoolAllocate(pool, 5, 1);
		ASSERT(b[0].size == 5);
		ASSERT(b[0].offset == 0);

		b[1] = aloPoolAllocate(pool, 19, 4);
		ASSERT(b[1].size == 19);
		ASSERT(b[1].offset == 8);

		b[2] = aloPoolAllocate(pool, 39, 16);
		ASSERT(b[2].size == 39);
		ASSERT(b[2].offset == 32);

		b[3] = aloPoolAllocate(pool, 200, 128);
		ASSERT(b[3].size == 200);
		ASSERT(b[3].offset == 128);

		b[4] = aloPoolAllocate(pool, 488, 512);
		ASSERT(b[4].size == 488);
		ASSERT(b[4].offset == 512);

		b[5] = aloPoolAllocate(pool, 200, 256);
		ASSERT(b[5].size == 0);

		aloPoolFree(pool, b[3].index);

		b[5] = aloPoolAllocate(pool, 200, 256);
		ASSERT(b[5].size == 200);
		ASSERT(b[5].offset == 256);
	}

	aloPoolDestroy(pool);
	return 0;
}

#define REQUIRE_EQUAL_UINT32(a, b) \
	do { \
		const uint32_t va = (a), vb = (b); \
		if (va != vb) { \
			fprintf(stderr, "%s:%d (%s == %s) FAILED: %u != %u\n", \
				__FILE__, __LINE__, \
				#a, #b, \
				va, vb); \
		} \
		ASSERT(va == vb); \
	} while(0)

static void dumpRing(int line, const alo_ring_t* ring) {
	fprintf(stderr, "%d ", line);
	if (ring->tail < ring->head) {
		fprintf(stderr, "t=%03d h=%03d [", ring->tail, ring->head);
		for (int i = 0; i < (int)ring->tail; ++i) fputc('.', stderr);
		fputc('T', stderr);
		for (int i = (int)ring->tail + 1; i < (int)ring->head; ++i) fputc('#', stderr);
		fputc('h', stderr);
		for (int i = (int)ring->head + 1; i < (int)ring->size; ++i) fputc('.', stderr);
	} else {
		fprintf(stderr, "h=%03d t=%03d [", ring->head, ring->tail);
		for (int i = 0; i < (int)ring->head; ++i) fputc('#', stderr);
		fputc('h', stderr);
		for (int i = (int)ring->head + 1; i < (int)ring->tail; ++i) fputc('.', stderr);
		fputc('T', stderr);
		for (int i = (int)ring->tail + 1; i < (int)ring->size; ++i) fputc('#', stderr);
	}
	fputs("]\n", stderr);
}

#define TEST_ALLOC(name, size, expected, alignment) \
	const uint32_t name = aloRingAlloc(&ring, size, alignment); \
	dumpRing(__LINE__, &ring); \
	REQUIRE_EQUAL_UINT32(name, expected)

#define TEST_FREE(to) \
	aloRingFree(&ring, to); \
	dumpRing(__LINE__, &ring)

void testRing(void) {
	alo_ring_t ring;
	aloRingInit(&ring, 128);

	fprintf(stderr, "%s\n", __FUNCTION__);

	TEST_ALLOC(p0, 64, 0, 1);
	TEST_ALLOC(p1, 64, 64, 1);
	TEST_ALLOC(p2, 64, ALO_ALLOC_FAILED, 1);
	TEST_FREE(p1);
	TEST_ALLOC(p3, 32, 0, 1);
	TEST_FREE(p3);
	TEST_ALLOC(p4, 64, 32, 1);
	TEST_ALLOC(p5, 64, ALO_ALLOC_FAILED, 1);
	TEST_ALLOC(p6, 16, 96, 1);
	TEST_ALLOC(p7, 32, ALO_ALLOC_FAILED, 1);
	TEST_FREE(p4);
	TEST_ALLOC(p8, 32, 0, 1);
}

void stressTestRing(void) {
	#define BUFSIZE 128
	#define NUM_ALLOCS 16
	const int rounds = 10000;
	struct { uint32_t pos, size, val; } allocs[NUM_ALLOCS];
	int count = 0, wr = 0, rd = 0;
	uint32_t buf[BUFSIZE];

	alo_ring_t ring;
	aloRingInit(&ring, BUFSIZE);

	for (int i = 0; i < NUM_ALLOCS; ++i)
		allocs[i].pos = ALO_ALLOC_FAILED;

	fprintf(stderr, "%s\n", __FUNCTION__);

	for (int i = 0; i < rounds; ++i) {
		if (count < NUM_ALLOCS) {
			const uint32_t align = 1 << rand_pcg32(5);
			const uint32_t size = 1 + rand_pcg32(BUFSIZE / 5);
			const uint32_t pos = aloRingAlloc(&ring, size, align);
			fprintf(stderr, "ALLOC(%d;%d) size=%d align=%d => pos=%d\n", wr, count, size, align, pos);

			if (pos != ALO_ALLOC_FAILED) {
				dumpRing(__LINE__, &ring);
				allocs[wr].pos = pos;
				allocs[wr].size = size;
				allocs[wr].val = rand_pcg32(0xFFFFFFFFul);
				for (int i = 0; i < (int)size; ++i)
					buf[pos + i] = allocs[wr].val;
				wr = (wr + 1) % NUM_ALLOCS;
				count++;
			} else {
				ASSERT(count);
			}
		}

		if (rand_pcg32(5) == 0) {
			int to_remove = rand_pcg32(5) + 1;
			while (to_remove-- > 0 && count > 0) {
				ASSERT(allocs[rd].pos != ALO_ALLOC_FAILED);
				fprintf(stderr, "FREE(%d;%d) pos=%d(%d) count=%d to_remove=%d\n", rd, count, allocs[rd].pos, allocs[rd].size, count, to_remove);
				for (int i = 0; i < (int)allocs[rd].size; ++i)
					REQUIRE_EQUAL_UINT32(buf[allocs[rd].pos + i], allocs[rd].val);
				aloRingFree(&ring, allocs[rd].pos);
				dumpRing(__LINE__, &ring);
				allocs[rd].pos = ALO_ALLOC_FAILED;
				rd = (rd + 1) % NUM_ALLOCS;
				--count;
			}
		}
	}
}

int main(void) {
	test();

	ASSERT(1000 == testRandom(1000, 1000000, 1, 0));
	testRandom(1000, 1000000, 32, 999);

	testRing();
	stressTestRing();
	return 0;
}
#endif
