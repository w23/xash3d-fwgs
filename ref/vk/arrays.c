#include "arrays.h"

#include "vk_core.h" // Mem_Malloc

#include <stddef.h> // NULL


void arrayDynamicInit(array_dynamic_t *array, int item_size) {
	array->items = NULL;
	array->count = 0;
	array->capacity = 0;
	array->item_size = item_size;
}

void arrayDynamicDestroy(array_dynamic_t *array) {
	if (array->items)
		Mem_Free(array->items);
}

static void arrayDynamicEnsureCapacity(array_dynamic_t *array, int min_capacity) {
	if (array->capacity >= min_capacity)
		return;

	if (array->capacity == 0)
		array->capacity = 2;

	while (array->capacity < min_capacity)
		array->capacity = array->capacity * 3 / 2;

	void *new_buffer = Mem_Malloc(vk_core.pool, array->capacity * array->item_size);
	if (array->items) {
		memcpy(new_buffer, array->items, array->count * array->item_size);
		Mem_Free(array->items);
	}
	array->items = new_buffer;
}

void arrayDynamicResize(array_dynamic_t *array, int count) {
	arrayDynamicEnsureCapacity(array, count);
	array->count = count;
}

void arrayDynamicAppend(array_dynamic_t *array, void *item) {
	const int new_count = array->count + 1;
	arrayDynamicEnsureCapacity(array, new_count);

	memcpy(array->items + array->count * array->item_size, item, array->item_size);
	array->count = new_count;
}

