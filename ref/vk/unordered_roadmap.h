#pragma once

#include <stdint.h>

#define MAX_KEY_STRING_LENGTH 256

// URMOM = Unordered RoadMap Open addressiMg

// Open-addressed hash table item header
typedef struct urmom_header_s {
	// state == 1, hash == 0 -- item with hash==0
	// state == 0, hash != 0 -- deleted
	// state == 0, hash == 0 -- empty
	uint32_t state:1;
	uint32_t hash:31;

	char key[MAX_KEY_STRING_LENGTH];
} urmom_header_t;

#define URMOM_IS_OCCUPIED(hdr) ((hdr).state != 0)
#define URMOM_IS_EMPTY(hdr) ((hdr).state == 0 && (hdr).hash == 0)
#define URMOM_IS_DELETED(hdr) ((hdr).state == 0 && (hdr).hash != 0)

// TODO:
// - rename this to key type
// - allow passing key not as const char*, but as string_view
// - (or even just void*+size, which would almost make it universal)
typedef enum {
	kUrmomString,
	kUrmomStringInsensitive,
} urmom_type_t;

typedef struct urmom_desc_s {
	// Pointer to the beginning of the array of items.
	// Each item is a struct that has urmom_header_t as its first field
	void *array;

	// Array item size, including the urmom_header_t
	uint32_t item_size;

	// Maximum number of items in the array
	uint32_t count;

	urmom_type_t type;
} urmom_desc_t;

// Sets all items to empty
void urmomInit(const urmom_desc_t* desc);

// Returns index of the element with the key if found, -1 otherwise
int urmomFind(const urmom_desc_t* desc, const char* key);

// Returns index of the element either found or empty slot where this could be inserted. If full, -1.
typedef struct urmom_insert_s {
	int index;
	int created;
} urmom_insert_t;
urmom_insert_t urmomInsert(const urmom_desc_t* desc, const char *key);

// Return the index of item deleted (if found), -1 otherwise
int urmomRemove(const urmom_desc_t* desc, const char *key);

void urmomRemoveByIndex(const urmom_desc_t* desc, int index);

// TODO erase IS_DELETED tails
// void urmomCleanup()

// TODO optimize storage: collapse non-tail IS_DELETED sequences. Items will have new placement, so all indexes will be stale
