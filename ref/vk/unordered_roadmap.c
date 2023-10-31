#include "unordered_roadmap.h"

#ifndef URMOM_TEST
#include "vk_common.h"
#include "vk_logs.h"
#else
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define ERR(msg, ...) fprintf(stderr, msg, ##__VA_ARGS__)
#define ASSERT(...) assert(__VA_ARGS__)
#define COUNTOF(a) (sizeof(a)/sizeof(a[0]))
#endif

#if defined(_WIN32) && !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

static uint32_t hash32FNV1aStr(const char *str) {
	static const uint32_t fnv_offset_basis = 0x811c9dc5u;
	static const uint32_t fnv_prime = 0x01000193u;

	uint32_t hash = fnv_offset_basis;
	while (*str) {
		hash ^= *str;
		hash *= fnv_prime;
		++str;
	}
	return hash;
}
static uint32_t hash32FNV1aStrI(const char *str) {
	static const uint32_t fnv_offset_basis = 0x811c9dc5u;
	static const uint32_t fnv_prime = 0x01000193u;

	uint32_t hash = fnv_offset_basis;
	while (*str) {
		hash ^= (*str & 0xdf);
		hash *= fnv_prime;
		++str;
	}
	return hash;
}

// Sets all items to empty
void urmomInit(const urmom_desc_t* desc) {
	char *ptr = desc->array;

	// Make sure that count is 2^N
	ASSERT((desc->count & (desc->count - 1)) == 0);

	for (int i = 0; i < desc->count; ++i) {
		urmom_header_t *hdr = (urmom_header_t*)(ptr + desc->item_size * i);
		hdr->state = 0;
		hdr->hash = 0;
	}
}

static uint32_t hashKey(urmom_type_t type, const char *key) {
	uint32_t hash;
	switch (type) {
		case kUrmomString:
			hash = hash32FNV1aStr(key);
			break;
		case kUrmomStringInsensitive:
			hash = hash32FNV1aStrI(key);
			break;
		default:
			ASSERT(!"Invalid hash table key type");
	}

	return hash & 0x7fffffffu;
}

static int sameKey(urmom_type_t type, const char *key1, const char *key2) {
	switch (type) {
		case kUrmomString:
			return strcmp(key1, key2) == 0;
		case kUrmomStringInsensitive:
			return strcasecmp(key1, key2) == 0;
		default:
			ASSERT(!"Invalid hash table key type");
	}

	return 0;
}

// Returns index of the element with the key if found, -1 otherwise
int urmomFind(const urmom_desc_t* desc, const char* key) {
	const char *ptr = desc->array;
	const uint32_t hash = hashKey(desc->type, key);
	const uint32_t mask = (desc->count - 1);
	const int start_index = hash & mask;

	for (int index = start_index;;) {
		const urmom_header_t *hdr = (urmom_header_t*)(ptr + desc->item_size * index);

		if (URMOM_IS_OCCUPIED(*hdr)) {
			if (hdr->hash == hash && sameKey(desc->type, key, hdr->key))
				return index;
		} else if (URMOM_IS_EMPTY(*hdr))
			// Reached the end of non-empty chain, not found
			break;

		// No match ;_;, check the next one
		index = (index + 1) & mask;

		// Searched through the entire thing
		if (index == start_index)
			break;
	}

	return -1;
}

// Returns index of the element either found or empty slot where this could be inserted. If full, -1.
urmom_insert_t urmomInsert(const urmom_desc_t* desc, const char *key) {
	char *ptr = desc->array;
	const uint32_t hash = hashKey(desc->type, key);
	const uint32_t mask = (desc->count - 1);
	const int start_index = hash & mask;

	int index = start_index;
	int first_available = -1;
	for (;;) {
		const urmom_header_t *hdr = (urmom_header_t*)(ptr + desc->item_size * index);

		if (URMOM_IS_OCCUPIED(*hdr)) {
			if (hdr->hash == hash && sameKey(desc->type, key, hdr->key))
				// Return existing item
				return (urmom_insert_t){.index = index, .created = 0};
		} else {
			// Remember the very first item that wasn't occupied
			if (first_available < 0)
				first_available = index;

			// Reached the end of occupied chain, return the available slot
			if (URMOM_IS_EMPTY(*hdr))
				break;
		}

		index = (index + 1) & mask;

		// Searched through the entire thing
		if (index == start_index)
			break;
	}

	// If no slots were encountered, exit with error
	if (first_available < 0)
		return (urmom_insert_t){.index = -1, .created = 0};

	urmom_header_t *hdr = (urmom_header_t*)(ptr + desc->item_size * first_available);
	hdr->hash = hash;
	hdr->state = 1;

	// TODO check for key length
	strncpy(hdr->key, key, sizeof(hdr->key));

	return (urmom_insert_t){.index = first_available, .created = 1};
}

// Return the index of item deleted (if found), -1 otherwise
int urmomRemove(const urmom_desc_t* desc, const char *key) {
	const int index = urmomFind(desc, key);
	if (index >= 0)
		urmomRemoveByIndex(desc, index);
	return index;
}

void urmomRemoveByIndex(const urmom_desc_t* desc, int index) {
	char *ptr = desc->array;
	urmom_header_t *hdr = (urmom_header_t*)(ptr + desc->item_size * index);

	if (!URMOM_IS_OCCUPIED(*hdr)) {
		ERR("Hashmap=%p(is=%d, n=%d): lot %d is not occupied", desc->array, desc->item_size, desc->count, index);
		return;
	}

	// Mark it as deleted
	// TODO when can we mark it as empty? For linear search we can do so if the next one is empty
	hdr->state = 0; // not occupied
	hdr->hash = 1; // deleted, not empty
	hdr->key[0] = '\0';
}
