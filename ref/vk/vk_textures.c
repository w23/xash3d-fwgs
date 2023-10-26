#include "vk_textures.h"

#include "vk_core.h"
#include "vk_descriptor.h"
#include "vk_staging.h"
#include "vk_logs.h"
#include "vk_combuf.h"
#include "r_textures.h"
#include "r_speeds.h"
#include "alolcator.h"
#include "profiler.h"

#include "xash3d_mathlib.h" // bound

#include "ktx2.h"

#include <math.h> // sqrt

#define LOG_MODULE LogModule_Textures
#define MODULE_NAME "textures"

#define MAX_SAMPLERS 8 // TF_NEAREST x 2 * TF_BORDER x 2 * TF_CLAMP x 2

static struct {
	struct {
		int count;
		int size_total;
	} stats;

	struct {
		uint32_t flags;
		VkSampler sampler;
	} samplers[MAX_SAMPLERS];

	VkSampler default_sampler;

	//vk_texture_t textures[MAX_TEXTURES];
	//alo_int_pool_t textures_free;

	// All textures descriptors in their native formats used for RT
	VkDescriptorImageInfo dii_all_textures[MAX_TEXTURES];

	vk_texture_t skybox_cube;
	vk_texture_t cubemap_placeholder;
} g_vktextures;

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

size_t CalcImageSize( pixformat_t format, int width, int height, int depth );
int CalcMipmapCount( vk_texture_t *tex, qboolean haveBuffer );
qboolean validatePicLayers(const char* const name, rgbdata_t *const *const layers, int num_layers);
void BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags );

static VkSampler pickSamplerForFlags( texFlags_t flags );

// FIXME should be static
void unloadSkybox( void );

qboolean R_VkTexturesInit( void ) {
	R_SPEEDS_METRIC(g_vktextures.stats.count, "count", kSpeedsMetricCount);
	R_SPEEDS_METRIC(g_vktextures.stats.size_total, "size_total", kSpeedsMetricBytes);

	// TODO really check device caps for this
	gEngine.Image_AddCmdFlags( IL_DDS_HARDWARE | IL_KTX2_RAW );

	g_vktextures.default_sampler = pickSamplerForFlags(0);
	ASSERT(g_vktextures.default_sampler != VK_NULL_HANDLE);

	/* FIXME
	// validate cvars
	R_SetTextureParameters();
	*/

	/* FIXME
	gEngine.Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );
	*/

	// Fill empty texture with references to the default texture
	{
		const VkImageView default_view = R_TextureGetByIndex(tglob.defaultTexture)->vk.image.view;
		ASSERT(default_view != VK_NULL_HANDLE);
		for (int i = 0; i < MAX_TEXTURES; ++i) {
			const vk_texture_t *const tex = R_TextureGetByIndex(i);
			if (tex->vk.image.view)
				continue;

			g_vktextures.dii_all_textures[i] = (VkDescriptorImageInfo){
				.imageView =  default_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = g_vktextures.default_sampler,
			};
		}
	}

	return true;
}

static void textureDestroy( unsigned int index );

void R_VkTexturesShutdown( void ) {
	unloadSkybox();
	R_VkTextureDestroy(-1, &g_vktextures.cubemap_placeholder);

	for (int i = 0; i < COUNTOF(g_vktextures.samplers); ++i) {
		if (g_vktextures.samplers[i].sampler != VK_NULL_HANDLE)
			vkDestroySampler(vk_core.device, g_vktextures.samplers[i].sampler, NULL);
	}
}

static VkFormat VK_GetFormat(pixformat_t format, colorspace_hint_e colorspace_hint ) {
	switch(format)
	{
		case PF_RGBA_32:
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_R8G8B8A8_UNORM
				: VK_FORMAT_R8G8B8A8_SRGB;
		case PF_BGRA_32:
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_B8G8R8A8_UNORM
				: VK_FORMAT_B8G8R8A8_SRGB;
		case PF_RGB_24:
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_R8G8B8_UNORM
				: VK_FORMAT_R8G8B8_SRGB;
		case PF_BGR_24:
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_B8G8R8_UNORM
				: VK_FORMAT_B8G8R8_SRGB;
		case PF_LUMINANCE:
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_R8_UNORM
				: VK_FORMAT_R8_SRGB;
		case PF_DXT1:
			// TODO UNORM vs SRGB encoded in the format itself
			// ref_gl mentions that alpha is never used
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_BC1_RGB_UNORM_BLOCK
				: VK_FORMAT_BC1_RGB_SRGB_BLOCK;
		case PF_DXT3:
			// TODO UNORM vs SRGB encoded in the format itself
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_BC2_UNORM_BLOCK
				: VK_FORMAT_BC2_SRGB_BLOCK;
		case PF_DXT5:
			// TODO UNORM vs SRGB encoded in the format itself
			return (colorspace_hint == kColorspaceLinear)
				? VK_FORMAT_BC3_UNORM_BLOCK
				: VK_FORMAT_BC3_SRGB_BLOCK;
		case PF_ATI2:
			// TODO UNORM vs SNORM?
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case PF_BC4_UNSIGNED: return VK_FORMAT_BC4_UNORM_BLOCK;
		case PF_BC4_SIGNED:   return VK_FORMAT_BC4_SNORM_BLOCK;
		case PF_BC5_UNSIGNED: return VK_FORMAT_BC5_UNORM_BLOCK;
		case PF_BC5_SIGNED:   return VK_FORMAT_BC5_SNORM_BLOCK;
		case PF_BC6H_UNSIGNED: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		case PF_BC6H_SIGNED:   return VK_FORMAT_BC6H_SFLOAT_BLOCK;
		case PF_BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
		case PF_BC7_SRGB:  return VK_FORMAT_BC7_SRGB_BLOCK;
		default:
			WARN("FIXME unsupported pixformat_t %d", format);
			return VK_FORMAT_UNDEFINED;
	}
}

static VkSampler createSamplerForFlags( texFlags_t flags ) {
	VkSampler sampler;
	const VkFilter filter_mode = (flags & TF_NEAREST) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	const VkSamplerAddressMode addr_mode =
		  (flags & TF_BORDER) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
		: ((flags & TF_CLAMP) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT);
	const VkSamplerCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter =  filter_mode,
		.minFilter = filter_mode,
		.addressModeU = addr_mode,
		.addressModeV = addr_mode,
		.addressModeW = addr_mode,
		.anisotropyEnable = vk_core.physical_device.anisotropy_enabled,
		.maxAnisotropy = vk_core.physical_device.properties.limits.maxSamplerAnisotropy,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.minLod = 0.f,
		.maxLod = 16.,
	};
	XVK_CHECK(vkCreateSampler(vk_core.device, &sci, NULL, &sampler));
	return sampler;
}

static VkSampler pickSamplerForFlags( texFlags_t flags ) {
	flags &= (TF_BORDER | TF_CLAMP | TF_NEAREST);

	for (int i = 0; i < COUNTOF(g_vktextures.samplers); ++i) {
		if (g_vktextures.samplers[i].sampler == VK_NULL_HANDLE) {
			g_vktextures.samplers[i].flags = flags;
			return g_vktextures.samplers[i].sampler = createSamplerForFlags(flags);
		}

		if (g_vktextures.samplers[i].flags == flags)
			return g_vktextures.samplers[i].sampler;
	}

	ERR("Couldn't find/allocate sampler for flags %x", flags);
	return g_vktextures.default_sampler;
}

static void setDescriptorSet(int index, vk_texture_t* const tex, colorspace_hint_e colorspace_hint) {
	if (index < 0)
		return;

	ASSERT(index < MAX_TEXTURES);

	const VkImageView view = tex->vk.image.view != VK_NULL_HANDLE
		? tex->vk.image.view
		: R_TextureGetByIndex(tglob.defaultTexture)->vk.image.view;

	if (view == VK_NULL_HANDLE)
		return;

	VkDescriptorImageInfo dii = {
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.sampler = pickSamplerForFlags( tex->flags ),
	};

	// Set descriptor for bindless/ray tracing
	g_vktextures.dii_all_textures[index] = dii;

	// Continue with setting unorm descriptor for traditional renderer

	// TODO how should we approach this:
	// - per-texture desc sets can be inconvenient if texture is used in different incompatible contexts
	// - update descriptor sets in batch?

	if (colorspace_hint == kColorspaceGamma && tex->vk.image.view_unorm != VK_NULL_HANDLE)
		dii.imageView = tex->vk.image.view_unorm;

	const VkDescriptorSet ds = vk_desc_fixme.texture_sets[index];
	VkWriteDescriptorSet wds[1] = { {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &dii,
		.dstSet = ds,
	}};

	vkUpdateDescriptorSets(vk_core.device, COUNTOF(wds), wds, 0, NULL);

	tex->vk.descriptor_unorm = ds;
}

static qboolean uploadRawKtx2( int tex_index, vk_texture_t *tex, const rgbdata_t* pic ) {
	DEBUG("Uploading raw KTX2 texture[%d] %s", tex_index, TEX_NAME(tex));

	const byte *const data = pic->buffer;
	const int size = pic->size;

	const ktx2_header_t* header;
	const ktx2_index_t* index;
	const ktx2_level_t* levels;

	header = (const ktx2_header_t*)(data + KTX2_IDENTIFIER_SIZE);
	index = (const ktx2_index_t*)(data + KTX2_IDENTIFIER_SIZE + sizeof(ktx2_header_t));
	levels = (const ktx2_level_t*)(data + KTX2_IDENTIFIER_SIZE + sizeof(ktx2_header_t) + sizeof(ktx2_index_t));

	DEBUG(" header:");
#define X(field) DEBUG("  " # field "=%d", header->field);
	DEBUG("  vkFormat = %s(%d)", R_VkFormatName(header->vkFormat), header->vkFormat);
	X(typeSize)
	X(pixelWidth)
	X(pixelHeight)
	X(pixelDepth)
	X(layerCount)
	X(faceCount)
	X(levelCount)
	X(supercompressionScheme)
#undef X
	DEBUG(" index:");
#define X(field) DEBUG("  " # field "=%llu", (unsigned long long)index->field);
	X(dfdByteOffset)
	X(dfdByteLength)
	X(kvdByteOffset)
	X(kvdByteLength)
	X(sgdByteOffset)
	X(sgdByteLength)
#undef X

	for (int mip = 0; mip < header->levelCount; ++mip) {
		const ktx2_level_t* const level = levels + mip;
		DEBUG(" level[%d]:", mip);
		DEBUG("  byteOffset=%llu", (unsigned long long)level->byteOffset);
		DEBUG("  byteLength=%llu", (unsigned long long)level->byteLength);
		DEBUG("  uncompressedByteLength=%llu", (unsigned long long)level->uncompressedByteLength);
	}

	// FIXME check that format is supported
	// FIXME layers == 0
	// FIXME has_alpha
	// FIXME no supercompressionScheme

	{
		const r_vk_image_create_t create = {
			.debug_name = TEX_NAME(tex),
			.width = header->pixelWidth,
			.height = header->pixelHeight,
			.mips = header->levelCount,
			.layers = 1, // TODO or 6 for cubemap; header->faceCount
			.format = header->vkFormat,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			// FIXME find out if there's alpha
			.flags = 0,
		};
		tex->vk.image = R_VkImageCreate(&create);
	}

	{
		R_VkImageUploadBegin(&tex->vk.image);

		// TODO layers
		for (int mip = 0; mip < header->levelCount; ++mip) {
			const ktx2_level_t* const level = levels + mip;
			const size_t mip_size = level->byteLength;
			const void* const image_data = data + level->byteOffset;
			// FIXME validate wrt file size

			const int layer = 0;
			R_VkImageUploadSlice(&tex->vk.image, layer, mip, mip_size, image_data);
			tex->total_size += mip_size;
		} // for mip levels

		R_VkImageUploadEnd(&tex->vk.image);
	}

	{
		// KTX2 textures are inaccessible from trad renderer (for now)
		tex->vk.descriptor_unorm = VK_NULL_HANDLE;

		const VkDescriptorImageInfo dii = {
			.imageView = tex->vk.image.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.sampler = pickSamplerForFlags( tex->flags ),
		};
		g_vktextures.dii_all_textures[tex_index] = dii;
	}

	g_vktextures.stats.size_total += tex->total_size;
	g_vktextures.stats.count++;

	tex->width = header->pixelWidth;
	tex->height = header->pixelHeight;

	return true;
}

static qboolean needToCreateImage( int index, vk_texture_t *tex, const r_vk_image_create_t *create ) {
	if (tex->vk.image.image == VK_NULL_HANDLE)
		return true;

	if (tex->vk.image.width == create->width
		&& tex->vk.image.height == create->height
		&& tex->vk.image.format == create->format
		&& tex->vk.image.mips == create->mips
		&& tex->vk.image.layers == create->layers
		&& tex->vk.image.flags == create->flags)
		return false;

	WARN("Re-creating texture '%s' image", create->debug_name);
	R_VkTextureDestroy( index, tex );
	return true;
}

static qboolean uploadTexture(int index, vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint) {
	tex->total_size = 0;

	if (num_layers == 1 && layers[0]->type == PF_KTX2_RAW) {
		if (!uploadRawKtx2(index, tex, layers[0]))
			return false;
	} else {
		const qboolean compute_mips = layers[0]->type == PF_RGBA_32 && layers[0]->numMips < 2;
		const VkFormat format = VK_GetFormat(layers[0]->type, colorspace_hint);
		const int mipCount = compute_mips ? CalcMipmapCount( tex, true ) : layers[0]->numMips;
		const int width = layers[0]->width;
		const int height = layers[0]->height;

		if (format == VK_FORMAT_UNDEFINED) {
			ERR("Unsupported PF format %d", layers[0]->type);
			return false;
		}

		if (!validatePicLayers(TEX_NAME(tex), layers, num_layers))
			return false;

		DEBUG("Uploading texture[%d] %s, mips=%d(build=%d), layers=%d", index, TEX_NAME(tex), mipCount, compute_mips, num_layers);

		// TODO (not sure why, but GL does this)
		// if( !ImageCompressed( layers->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( layers->flags, IMAGE_ONEBIT_ALPHA ))
		// 	data = GL_ApplyFilter( data, tex->width, tex->height );

		{
			const r_vk_image_create_t create = {
				.debug_name = TEX_NAME(tex),
				.width = width,
				.height = height,
				.mips = mipCount,
				.layers = num_layers,
				.format = format,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.flags = 0
					| ((layers[0]->flags & IMAGE_HAS_ALPHA) ? 0 : kVkImageFlagIgnoreAlpha)
					| (cubemap ? kVkImageFlagIsCubemap : 0)
					| (colorspace_hint == kColorspaceGamma ? kVkImageFlagCreateUnormView : 0),
			};

			if (needToCreateImage(index, tex, &create))
				tex->vk.image = R_VkImageCreate(&create);
		}

		tex->width = width;
		tex->height = height;

		{
			R_VkImageUploadBegin(&tex->vk.image);

			for (int layer = 0; layer < num_layers; ++layer) {
				const rgbdata_t *const pic = layers[layer];
				byte *buf = pic->buffer;

				for (int mip = 0; mip < mipCount; ++mip) {
					const int width = Q_max( 1, ( pic->width >> mip ));
					const int height = Q_max( 1, ( pic->height >> mip ));
					const size_t mip_size = CalcImageSize( pic->type, width, height, 1 );

					R_VkImageUploadSlice(&tex->vk.image, layer, mip, mip_size, buf);
					tex->total_size += mip_size;

					// Build mip in place for the next mip level
					if (compute_mips) {
						if ( mip < mipCount - 1 )
							BuildMipMap( buf, width, height, 1, tex->flags );
					} else {
						buf += mip_size;
					}
				}
			}

			R_VkImageUploadEnd(&tex->vk.image);
		}
	}

	setDescriptorSet(index, tex, colorspace_hint);

	g_vktextures.stats.size_total += tex->total_size;
	g_vktextures.stats.count++;
	return true;
}

qboolean R_VkTextureUpload(int index, vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, colorspace_hint_e colorspace_hint) {
	return uploadTexture( index, tex, layers, num_layers, false, colorspace_hint );
}

void R_VkTextureDestroy( int index, vk_texture_t *tex ) {
	// Need to make sure that there are no references to this texture anywhere.
	// It might have been added to staging and then immediately deleted, leaving references to its vkimage
	// in the staging command buffer. See https://github.com/w23/xash3d-fwgs/issues/464
	R_VkStagingFlushSync();
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	R_VkImageDestroy(&tex->vk.image);
	g_vktextures.stats.size_total -= tex->total_size;
	g_vktextures.stats.count--;

	// Reset descriptor sets to default texture
	setDescriptorSet(index, tex, kColorspaceNative);

	tex->total_size = 0;
	tex->width = tex->height = 0;

	// TODO: currently cannot do this because vk_render depends on all textures having some descriptor regardless of their alive-ness
	// TODO tex->vk.descriptor_unorm = VK_NULL_HANDLE;
}

void unloadSkybox( void ) {
	if (g_vktextures.skybox_cube.vk.image.image) {
		R_VkTextureDestroy( -1, &g_vktextures.skybox_cube );
		memset(&g_vktextures.skybox_cube, 0, sizeof(g_vktextures.skybox_cube));
	}

	tglob.fCustomSkybox = false;
}

VkDescriptorImageInfo R_VkTextureGetSkyboxDescriptorImageInfo( void ) {
	return (VkDescriptorImageInfo){
		.sampler = g_vktextures.default_sampler,
		.imageView = g_vktextures.skybox_cube.vk.image.view
			? g_vktextures.skybox_cube.vk.image.view
			: g_vktextures.cubemap_placeholder.vk.image.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
}

qboolean R_VkTexturesSkyboxUpload( const char *name, rgbdata_t *const sides[6], colorspace_hint_e colorspace_hint, qboolean placeholder) {
	vk_texture_t *const dest = placeholder ? &g_vktextures.cubemap_placeholder : &g_vktextures.skybox_cube;
	Q_strncpy( TEX_NAME(dest), name, sizeof( TEX_NAME(dest) ));
	return uploadTexture(-1, dest, sides, 6, true, colorspace_hint);
}

VkDescriptorSet R_VkTextureGetDescriptorUnorm( uint index ) {
	ASSERT( index < MAX_TEXTURES );
	// TODO make an array of unorm descriptors
	return R_TextureGetByIndex(index)->vk.descriptor_unorm;
}

const VkDescriptorImageInfo* R_VkTexturesGetAllDescriptorsArray( void ) {
	return g_vktextures.dii_all_textures;
}
