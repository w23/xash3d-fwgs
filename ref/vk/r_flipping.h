#pragma once

#include "alolcator.h"

typedef struct {
	alo_ring_t ring;
	uint32_t frame_offsets[2];
} r_flipping_buffer_t;

void R_FlippingBuffer_Init(r_flipping_buffer_t *flibuf, uint32_t size);
uint32_t R_FlippingBuffer_Alloc(r_flipping_buffer_t* flibuf, uint32_t size, uint32_t align);

// (╯°□°)╯︵ ┻━┻
void R_FlippingBuffer_Flip(r_flipping_buffer_t* flibuf);

// ┬─┬ノ( º _ ºノ)
void R_FlippingBuffer_Clear(r_flipping_buffer_t *flibuf);
