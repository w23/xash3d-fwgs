#include "flipping.h"

void R_FlippingBuffer_Init(r_flipping_buffer_t *flibuf, uint32_t size) {
	aloRingInit(&flibuf->ring, size);
	R_FlippingBuffer_Clear(flibuf);
}

void R_FlippingBuffer_Clear(r_flipping_buffer_t *flibuf) {
	aloRingInit(&flibuf->ring, flibuf->ring.size);
	flibuf->frame_offsets[0] = flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}

uint32_t R_FlippingBuffer_Alloc(r_flipping_buffer_t* flibuf, uint32_t size, uint32_t align) {
	const uint32_t offset = aloRingAlloc(&flibuf->ring, size, align);
	if (offset == ALO_ALLOC_FAILED)
		return ALO_ALLOC_FAILED;

	if (flibuf->frame_offsets[1] == ALO_ALLOC_FAILED)
		flibuf->frame_offsets[1] = offset;

	return offset;
}

void R_FlippingBuffer_Flip(r_flipping_buffer_t* flibuf) {
	if (flibuf->frame_offsets[0] != ALO_ALLOC_FAILED)
		aloRingFree(&flibuf->ring, flibuf->frame_offsets[0]);

	flibuf->frame_offsets[0] = flibuf->frame_offsets[1];
	flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}
