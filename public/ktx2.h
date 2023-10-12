#pragma once

#include <stdint.h>

#define KTX2_IDENTIFIER_SIZE 12
#define KTX2_IDENTIFIER "\xABKTX 20\xBB\r\n\x1A\n"

/*
static const char k_ktx2_identifier[ktx2_IDENTIFIER_SIZE] = {
  '\xAB', 'K', 'T', 'X', ' ', '2', '0', '\xBB', '\r', '\n', '\x1A', '\n'
};
*/

typedef struct {
	uint32_t vkFormat;
	uint32_t typeSize;
	uint32_t pixelWidth;
	uint32_t pixelHeight;
	uint32_t pixelDepth;
	uint32_t layerCount;
	uint32_t faceCount;
	uint32_t levelCount;
	uint32_t supercompressionScheme;
} ktx2_header_t;

typedef struct {
	uint32_t dfdByteOffset;
	uint32_t dfdByteLength;
	uint32_t kvdByteOffset;
	uint32_t kvdByteLength;
	uint64_t sgdByteOffset;
	uint64_t sgdByteLength;
} ktx2_index_t;

typedef struct {
	uint64_t byteOffset;
	uint64_t byteLength;
	uint64_t uncompressedByteLength;
} ktx2_level_t;

#define KTX2_MINIMAL_HEADER_SIZE (KTX2_IDENTIFIER_SIZE + sizeof(ktx2_header_t) + sizeof(ktx2_index_t) + sizeof(ktx2_level_t))
