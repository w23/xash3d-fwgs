#pragma once

#include <stddef.h> // size_t

#define VIEW_DECLARE_CONST(TYPE, NAME) \
	struct { \
		const TYPE *items; \
		int count; \
	} NAME

// Array with compile-time maximum size
#define BOUNDED_ARRAY_DECLARE(TYPE, NAME, MAX_SIZE) \
		struct { \
			TYPE items[MAX_SIZE]; \
			int count; \
		} NAME

#define BOUNDED_ARRAY(TYPE, NAME, MAX_SIZE) \
	BOUNDED_ARRAY_DECLARE(TYPE, NAME, MAX_SIZE) = {0}

#define BOUNDED_ARRAY_HAS_SPACE(array_, space_) \
	((COUNTOF((array_).items) - (array_).count) >= space_)

#define BOUNDED_ARRAY_APPEND_UNSAFE(array_) \
	((array_).items[(array_).count++])

#define BOUNDED_ARRAY_APPEND_ITEM(var, item) \
		do { \
			ASSERT(BOUNDED_ARRAY_HAS_SPACE(var, 1)); \
			var.items[var.count++] = item; \
		} while(0)


// Dynamically-sized array
// I. Type-agnostic

typedef struct array_dynamic_s {
	void *items;
	size_t count, capacity;
	size_t item_size;
} array_dynamic_t;

void arrayDynamicInit(array_dynamic_t *array, int item_size);
void arrayDynamicDestroy(array_dynamic_t *array);

void arrayDynamicReserve(array_dynamic_t *array, int capacity);
void arrayDynamicAppend(array_dynamic_t *array, void *item);
#define arrayDynamicAppendItem(array, item) \
	do { \
		ASSERT((array)->item_size == sizeof(&(item))); \
		arrayDynamicAppend(array, item); \
	} while (0)
/* void *arrayDynamicGet(array_dynamic_t *array, int index); */
/* #define arrayDynamicAt(array, type, index) \ */
/* 		(ASSERT((array)->item_size == sizeof(type)), \ */
/* 		 ASSERT((array)->count > (index)), \ */
/* 		 arrayDynamicGet(array, index)) */
void arrayDynamicResize(array_dynamic_t *array, int count);
//void arrayDynamicErase(array_dynamic_t *array, int begin, int end);

//void arrayDynamicInsert(array_dynamic_t *array, int before, int count, void *items);

// II. Type-specific
#define ARRAY_DYNAMIC_DECLARE(TYPE, NAME) \
	struct { \
		TYPE *items; \
		size_t count, capacity; \
		size_t item_size; \
	} NAME

#define arrayDynamicInitT(array) \
	arrayDynamicInit((array_dynamic_t*)array, sizeof((array)->items[0]))

#define arrayDynamicDestroyT(array) \
	arrayDynamicDestroy((array_dynamic_t*)array)

#define arrayDynamicResizeT(array, size) \
	arrayDynamicResize((array_dynamic_t*)(array), (size))

#define arrayDynamicAppendT(array, item) \
	arrayDynamicAppend((array_dynamic_t*)(array), (item))

#define arrayDynamicInsertT(array, before, count, items) \
	arrayDynamicInsert((array_dynamic_t*)(array), before, count, items)

#define arrayDynamicAppendManyT(array, items_count, items) \
	arrayDynamicInsert((array_dynamic_t*)(array), (array)->count, items_count, items)
