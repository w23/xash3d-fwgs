#pragma once

struct cl_entity_s;
void* VK_EntityDataGet(const struct cl_entity_s*);

typedef void (entity_data_dtor_f)(void*);

// Will destroy and overwrite the older userdata if it exists.
// TODO: Make sure that the older userdata is not used (i.e. in parallel on GPU for rendering a still in-flight frame).
//   This'd require a proper usage tracking (e.g. using refcounts) with changes to the rest of the renderer.
//   Someday...
void VK_EntityDataSet(const struct cl_entity_s*, void *userdata, entity_data_dtor_f *dtor);

void VK_EntityDataClear(void);

// TODO a function to LRU-clear userdata that hasn't been used for a few frames
