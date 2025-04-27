#pragma once
#include <stdint.h>

typedef uint32_t alo_size_t;

struct alo_pool_s;

struct alo_pool_s* aloPoolCreate(alo_size_t size, int expected_allocations, alo_size_t min_alignment);
void aloPoolDestroy(struct alo_pool_s*);

typedef struct {
	alo_size_t offset;
	alo_size_t size;
	alo_size_t alignment_hole;

	int index;
} alo_block_t;

alo_block_t aloPoolAllocate(struct alo_pool_s*, alo_size_t size, alo_size_t alignment);
void aloPoolFree(struct alo_pool_s *pool, int index);

//  <-          size          ->
// [.....|AAAAAAAAAAAAAAA|......]
//        ^ -- tail       ^ -- head
typedef struct {
	uint32_t size, head, tail;
} alo_ring_t;

#define ALO_ALLOC_FAILED 0xffffffffu

// Marks the entire buffer as free
void aloRingInit(alo_ring_t* ring, uint32_t size);

// Allocates a new aligned region and returns offset to it (AllocFailed if allocation failed)
uint32_t aloRingAlloc(alo_ring_t* ring, uint32_t size, uint32_t alignment);

// Marks everything up-to-pos as free (expects up-to-pos to be valid)
void aloRingFree(alo_ring_t* ring, uint32_t up_to_pos);

// Integer pool/freelist
// Get integers from 0 to capacity
typedef struct alo_int_pool_s {
	int *free_list;
	int capacity;
	int free;
} alo_int_pool_t;

void aloIntPoolGrow(alo_int_pool_t *pool, int new_capacity);
int aloIntPoolAlloc(alo_int_pool_t *pool);
void aloIntPoolFree(alo_int_pool_t *pool, int);
void aloIntPoolClear(alo_int_pool_t *pool);
void aloIntPoolDestroy(alo_int_pool_t *pool);
