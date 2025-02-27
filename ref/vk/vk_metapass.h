#pragma once

struct Metapass;
struct vk_meatpipe_s;
struct vk_combuf_s;
struct rt_resource_s;

typedef struct {
	struct vk_combuf_s* combuf;
	int frame_set_slot; // 0 or 1, until we do num_frame_slots
	int width, height;
	int is_discontinuous;
} MetapassDispatchArgs;

struct Metapass *Metapass_Create(struct vk_meatpipe_s *meatpipe, int max_width, int max_height);
void Metapass_Dispatch(struct Metapass* metapass, MetapassDispatchArgs);
void Metapass_Destroy(struct Metapass*);
