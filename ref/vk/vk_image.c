#include "vk_image.h"
#include "vk_staging.h"
#include "vk_combuf.h"
#include "vk_logs.h"

#include "xash3d_mathlib.h" // Q_max

static const VkImageUsageFlags usage_bits_implying_views =
	VK_IMAGE_USAGE_SAMPLED_BIT |
	VK_IMAGE_USAGE_STORAGE_BIT |
	VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR |
	VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT;
/*
	VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
	VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
*/

uint32_t R_VkImageFormatTexelBlockSize( VkFormat format ) {
	switch (format) {
		case VK_FORMAT_R4G4_UNORM_PACK8:
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
			return 1;
		case VK_FORMAT_R10X6_UNORM_PACK16:
		case VK_FORMAT_R12X4_UNORM_PACK16:
		case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
		case VK_FORMAT_A4B4G4R4_UNORM_PACK16:
		case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		case VK_FORMAT_R5G6B5_UNORM_PACK16:
		case VK_FORMAT_B5G6R5_UNORM_PACK16:
		case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
		case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
		case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R8G8_USCALED:
		case VK_FORMAT_R8G8_SSCALED:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R8G8_SINT:
		case VK_FORMAT_R8G8_SRGB:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SNORM:
		case VK_FORMAT_R16_USCALED:
		case VK_FORMAT_R16_SSCALED:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16_SFLOAT:
			return 2;
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_USCALED:
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SNORM:
		case VK_FORMAT_B8G8R8_USCALED:
		case VK_FORMAT_B8G8R8_SSCALED:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_B8G8R8_SRGB:
			return 3;
		case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
		case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
		case VK_FORMAT_R16G16_S10_5_NV:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_USCALED:
		case VK_FORMAT_B8G8R8A8_SSCALED:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
		case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
		case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
		case VK_FORMAT_A8B8G8R8_UINT_PACK32:
		case VK_FORMAT_A8B8G8R8_SINT_PACK32:
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
		case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
		case VK_FORMAT_A2R10G10B10_UINT_PACK32:
		case VK_FORMAT_A2R10G10B10_SINT_PACK32:
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
		case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
		case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
		case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		case VK_FORMAT_A2B10G10R10_SINT_PACK32:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SNORM:
		case VK_FORMAT_R16G16_USCALED:
		case VK_FORMAT_R16G16_SSCALED:
		case VK_FORMAT_R16G16_UINT:
		case VK_FORMAT_R16G16_SINT:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
		case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
			return 4;
		case VK_FORMAT_R16G16B16_UNORM:
		case VK_FORMAT_R16G16B16_SNORM:
		case VK_FORMAT_R16G16B16_USCALED:
		case VK_FORMAT_R16G16B16_SSCALED:
		case VK_FORMAT_R16G16B16_UINT:
		case VK_FORMAT_R16G16B16_SINT:
		case VK_FORMAT_R16G16B16_SFLOAT:
			return 6;
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SNORM:
		case VK_FORMAT_R16G16B16A16_USCALED:
		case VK_FORMAT_R16G16B16A16_SSCALED:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_UINT:
		case VK_FORMAT_R32G32_SINT:
		case VK_FORMAT_R32G32_SFLOAT:
		case VK_FORMAT_R64_UINT:
		case VK_FORMAT_R64_SINT:
		case VK_FORMAT_R64_SFLOAT:
			return 8;
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32_SFLOAT:
			return 12;
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_R64G64_UINT:
		case VK_FORMAT_R64G64_SINT:
		case VK_FORMAT_R64G64_SFLOAT:
			return 16;
		case VK_FORMAT_R64G64B64_UINT:
		case VK_FORMAT_R64G64B64_SINT:
		case VK_FORMAT_R64G64B64_SFLOAT:
			return 24;
		case VK_FORMAT_R64G64B64A64_UINT:
		case VK_FORMAT_R64G64B64A64_SINT:
		case VK_FORMAT_R64G64B64A64_SFLOAT:
			return 32;

		case VK_FORMAT_D16_UNORM:
			return 2;

		case VK_FORMAT_X8_D24_UNORM_PACK32:
			return 4;

		case VK_FORMAT_D32_SFLOAT:
			return 4;

		case VK_FORMAT_S8_UINT:
			return 2;

		case VK_FORMAT_D16_UNORM_S8_UINT:
			return 3;

		case VK_FORMAT_D24_UNORM_S8_UINT:
			return 4;

		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 5;

		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
			return 8;

		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
			return 8;

		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC2_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC3_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_BC4_UNORM_BLOCK:
		case VK_FORMAT_BC4_SNORM_BLOCK:
			return 8;

		case VK_FORMAT_BC5_UNORM_BLOCK:
		case VK_FORMAT_BC5_SNORM_BLOCK:
			return 16;

		case VK_FORMAT_BC6H_UFLOAT_BLOCK:
		case VK_FORMAT_BC6H_SFLOAT_BLOCK:
			return 16;

		case VK_FORMAT_BC7_UNORM_BLOCK:
		case VK_FORMAT_BC7_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
			return 8;

		case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
			return 8;

		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_EAC_R11_UNORM_BLOCK:
		case VK_FORMAT_EAC_R11_SNORM_BLOCK:
			return 8;

		case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
		case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
		case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
			return 16;

		case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
		case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK:
		case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
		case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
			return 16;
		case VK_FORMAT_G8B8G8R8_422_UNORM:
			return 4;
		case VK_FORMAT_B8G8R8G8_422_UNORM:
			return 4;
		case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
			return 3;
		case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
			return 3;
		case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
			return 3;
		case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
			return 3;
		case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
			return 3;
		case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
			return 8;
		case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G16B16G16R16_422_UNORM:
			return 8;
		case VK_FORMAT_B16G16R16G16_422_UNORM:
			return 8;
		case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
			return 6;
		case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
			return 6;
		case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
			return 6;
		case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
			return 6;
		case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
			return 6;
		case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
		case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
			return 8;
		case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
		case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
			return 8;
		case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
		case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
			return 8;
		case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
		case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
			return 8;
		case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
			return 3;
		case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
			return 6;
		case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
			return 6;
		case VK_FORMAT_UNDEFINED:
		case VK_FORMAT_MAX_ENUM:
			return 4;
	}

	return 4;
}

static VkFormat unormFormatFor(VkFormat fmt) {
	switch (fmt) {
		case VK_FORMAT_R8_SRGB: return VK_FORMAT_R8_UNORM;
		case VK_FORMAT_R8_UNORM: return VK_FORMAT_R8_UNORM;
		case VK_FORMAT_R8G8_SRGB: return VK_FORMAT_R8G8_UNORM;
		case VK_FORMAT_R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
		case VK_FORMAT_R8G8B8_SRGB: return VK_FORMAT_R8G8B8_UNORM;
		case VK_FORMAT_R8G8B8_UNORM: return VK_FORMAT_R8G8B8_UNORM;
		case VK_FORMAT_B8G8R8_SRGB: return VK_FORMAT_B8G8R8_UNORM;
		case VK_FORMAT_B8G8R8_UNORM: return VK_FORMAT_B8G8R8_UNORM;
		case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
		case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
		case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
		case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case VK_FORMAT_BC2_SRGB_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
		case VK_FORMAT_BC2_UNORM_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
		case VK_FORMAT_BC3_SRGB_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
		case VK_FORMAT_BC3_UNORM_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
		case VK_FORMAT_BC7_SRGB_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
		case VK_FORMAT_BC7_UNORM_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
		case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
		case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
		case VK_FORMAT_ASTC_5x4_UNORM_BLOCK: return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
		case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_6x5_UNORM_BLOCK: return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x5_UNORM_BLOCK: return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x6_UNORM_BLOCK: return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
		case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
		case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
		case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
		case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
		case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
		case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG: return VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG: return VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG: return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG: return VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG: return VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG: return VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG: return VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG;
		case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG: return VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

r_vk_image_t R_VkImageCreate(const r_vk_image_create_t *create) {
	const qboolean is_depth = !!(create->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	r_vk_image_t image = {0};
	VkMemoryRequirements memreq;

	const qboolean is_cubemap = !!(create->flags & kVkImageFlagIsCubemap);

	const VkFormat unorm_format = unormFormatFor(create->format);
	const qboolean create_unorm =
		!!(create->flags & kVkImageFlagCreateUnormView)
		&& unorm_format != VK_FORMAT_UNDEFINED
		&& unorm_format != create->format;

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent.width = create->width,
		.extent.height = create->height,
		.extent.depth = 1,
		.mipLevels = create->mips,
		.arrayLayers = create->layers,
		.format = create->format,
		.tiling = create->tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = create->usage,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.flags = 0
			| (is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0)
			| (create_unorm ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0),
	};

	XVK_CHECK(vkCreateImage(vk_core.device, &ici, NULL, &image.image));

	image.format = ici.format;

	if (create->debug_name)
		SET_DEBUG_NAME(image.image, VK_OBJECT_TYPE_IMAGE, create->debug_name);

	vkGetImageMemoryRequirements(vk_core.device, image.image, &memreq);
	image.devmem = VK_DevMemAllocate(create->debug_name, memreq, create->memory_props ? create->memory_props : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	XVK_CHECK(vkBindImageMemory(vk_core.device, image.image, image.devmem.device_memory, image.devmem.offset));

	if (create->usage & usage_bits_implying_views) {
		const qboolean has_alpha = !!(create->flags & kVkImageFlagHasAlpha);

		VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.format = ici.format,
			.image = image.image,
			.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.baseMipLevel = 0,
			.subresourceRange.levelCount = ici.mipLevels,
			.subresourceRange.baseArrayLayer = 0,
			.subresourceRange.layerCount = ici.arrayLayers,
			// TODO component swizzling based on format, e.g. R8 -> RRRR
			.components = (VkComponentMapping){0, 0, 0, (is_depth || has_alpha) ? 0 : VK_COMPONENT_SWIZZLE_ONE},
		};
		XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &image.view));

		if (create->debug_name)
			SET_DEBUG_NAME(image.view, VK_OBJECT_TYPE_IMAGE_VIEW, create->debug_name);

		if (create_unorm) {
			ivci.format = unorm_format;
			XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &image.view_unorm));

			if (create->debug_name)
				SET_DEBUG_NAMEF(image.view_unorm, VK_OBJECT_TYPE_IMAGE_VIEW, "%s_unorm", create->debug_name);
		}
	}

	image.width = create->width;
	image.height = create->height;
	image.mips = create->mips;
	image.layers = create->layers;

	return image;
}

void R_VkImageDestroy(r_vk_image_t *img) {
	if (img->view_unorm != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view_unorm, NULL);

	if (img->view != VK_NULL_HANDLE)
		vkDestroyImageView(vk_core.device, img->view, NULL);

	vkDestroyImage(vk_core.device, img->image, NULL);

	VK_DevMemFree(&img->devmem);
	*img = (r_vk_image_t){0};
}

void R_VkImageClear(VkCommandBuffer cmdbuf, VkImage image) {
	const VkImageMemoryBarrier image_barriers[] = { {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = image,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
	}} };

	const VkClearColorValue clear_value = {0};

	vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);

	vkCmdClearColorImage(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &image_barriers->subresourceRange);
}

void R_VkImageBlit(VkCommandBuffer cmdbuf, const r_vkimage_blit_args *blit_args) {
	{
		const VkImageMemoryBarrier image_barriers[] = { {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->src.image,
			.srcAccessMask = blit_args->src.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = blit_args->src.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}, {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = blit_args->dst.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = blit_args->dst.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		} };

		vkCmdPipelineBarrier(cmdbuf,
			blit_args->in_stage,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);
	}

	{
		VkImageBlit region = {0};
		region.srcOffsets[1].x = blit_args->src.width;
		region.srcOffsets[1].y = blit_args->src.height;
		region.srcOffsets[1].z = 1;
		region.dstOffsets[1].x = blit_args->dst.width;
		region.dstOffsets[1].y = blit_args->dst.height;
		region.dstOffsets[1].z = 1;
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		vkCmdBlitImage(cmdbuf,
			blit_args->src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			blit_args->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region,
			VK_FILTER_NEAREST);
	}

	{
		VkImageMemoryBarrier image_barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, COUNTOF(image_barriers), image_barriers);
	}
}

void R_VkImageUploadBegin( r_vk_image_t *img ) {
	const VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img->image,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = img->mips,
			.baseArrayLayer = 0,
			.layerCount = img->layers,
		}
	};

	// Command buffer might be invalidated on any slice load
	const VkCommandBuffer cmdbuf = R_VkStagingGetCommandBuffer();
	vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, 1, &image_barrier);
}

void R_VkImageUploadSlice( r_vk_image_t *img, int layer, int mip, int size, const void *data ) {
	const uint32_t width = Q_max(1, img->width >> mip);
	const uint32_t height = Q_max(1, img->height >> mip);
	const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(img->format);

	const vk_staging_image_args_t staging_args = {
		.image = img->image,
		.region = (VkBufferImageCopy) {
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = (VkImageSubresourceLayers){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip,
				.baseArrayLayer = layer,
				.layerCount = 1,
			},
			.imageExtent = (VkExtent3D){
				.width = width,
				.height = height,
				.depth = 1,
			},
		},
		.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.size = size,
		.alignment = texel_block_size,
	};

	{
		const vk_staging_region_t staging = R_VkStagingLockForImage(staging_args);
		ASSERT(staging.ptr);
		memcpy(staging.ptr, data, size);
		R_VkStagingUnlock(staging.handle);
	}
}

void R_VkImageUploadEnd( r_vk_image_t *img ) {
	// TODO Don't change layout here. Alternatively:
	// I. Attach layout metadata to the image, and request its change next time it is used.
	// II. Build-in layout transfer to staging commit and do it there on commit.

	const VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = img->image,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = img->mips,
			.baseArrayLayer = 0,
			.layerCount = img->layers,
		}
	};

	// Commit is needed to make sure that all previous image loads have been submitted to cmdbuf
	const VkCommandBuffer cmdbuf = R_VkStagingCommit()->cmdbuf;
	vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			// FIXME incorrect, we also use them in compute and potentially ray tracing shaders
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, NULL, 0, NULL, 1, &image_barrier);
}
