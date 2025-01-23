#include "vk_entity_data.h"

#include "vk_common.h" // ASSERT

#include <stddef.h> // NULL

// TODO proper hash map with dynamic size, etc
#define MAX_ENTITIES 1024

typedef struct {
	const struct cl_entity_s *entity;
	void *userdata;
	entity_data_dtor_f *dtor;
} entity_data_cache_entry_t;

struct {
	int entries_count;
	entity_data_cache_entry_t entries[MAX_ENTITIES];
} g_entdata;

void* VK_EntityDataGet(const struct cl_entity_s* entity) {
	for (int i = 0; i < g_entdata.entries_count; ++i) {
		entity_data_cache_entry_t *const entry = g_entdata.entries + i;
		if (entry->entity == entity)
			return entry->userdata;
	}

	return NULL;
}

void VK_EntityDataClear(void) {
	for (int i = 0; i < g_entdata.entries_count; ++i) {
		entity_data_cache_entry_t *const entry = g_entdata.entries + i;
		entry->dtor(entry->userdata);
	}

	g_entdata.entries_count = 0;
}

void VK_EntityDataSet(const struct cl_entity_s* entity, void *userdata, entity_data_dtor_f *dtor) {
	for (int i = 0; i < g_entdata.entries_count; ++i) {
		entity_data_cache_entry_t *const entry = g_entdata.entries + i;
		if (entry->entity == entity) {
			entry->dtor(entry->userdata);
			entry->userdata = userdata;
			entry->dtor = dtor;
			return;
		}
	}

	ASSERT(g_entdata.entries_count < MAX_ENTITIES);
	entity_data_cache_entry_t *const entry = g_entdata.entries + g_entdata.entries_count;
	entry->entity = entity;
	entry->userdata = userdata;
	entry->dtor = dtor;
	++g_entdata.entries_count;
}
