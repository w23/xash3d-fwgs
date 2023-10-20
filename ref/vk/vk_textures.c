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

#define PCG_IMPLEMENT
#include "pcg.h"

#include "ktx2.h"


#include <math.h> // sqrt

vk_textures_global_t tglob = {0};

#define LOG_MODULE LogModule_Textures
#define MODULE_NAME "textures"

static struct {
	struct {
		int count;
		int size_total;
	} stats;

	//vk_texture_t textures[MAX_TEXTURES];
	//alo_int_pool_t textures_free;
} g_textures;

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

extern vk_texture_t vk_textures[MAX_TEXTURES];
extern vk_texture_t* vk_texturesHashTable[TEXTURES_HASH_SIZE];
extern uint vk_numTextures;

size_t CalcImageSize( pixformat_t format, int width, int height, int depth );
int CalcMipmapCount( vk_texture_t *tex, qboolean haveBuffer );
qboolean validatePicLayers(const char* const name, rgbdata_t *const *const layers, int num_layers);
void BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags );

static void VK_CreateInternalTextures(void);
static VkSampler pickSamplerForFlags( texFlags_t flags );
static void textureDestroyVkImage( vk_texture_t *tex );

// FIXME should be static
void unloadSkybox( void );
qboolean uploadTexture(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint);
/* FIXME static */ int loadTextureInternal( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint, qboolean force_update );
/* FIXME static */ rgbdata_t *Common_FakeImage( int width, int height, int depth, int flags );
/* FIXME static */ qboolean Common_CheckTexName( const char *name );

qboolean R_VkTexturesInit( void ) {
	R_SPEEDS_METRIC(g_textures.stats.count, "count", kSpeedsMetricCount);
	R_SPEEDS_METRIC(g_textures.stats.size_total, "size_total", kSpeedsMetricBytes);

	// TODO really check device caps for this
	gEngine.Image_AddCmdFlags( IL_DDS_HARDWARE | IL_KTX2_RAW );

	tglob.default_sampler_fixme = pickSamplerForFlags(0);
	ASSERT(tglob.default_sampler_fixme != VK_NULL_HANDLE);

	/* FIXME
	// validate cvars
	R_SetTextureParameters();
	*/

	VK_CreateInternalTextures();

	/* FIXME
	gEngine.Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );
	*/

	// Fill empty texture with references to the default texture
	{
		const VkImageView default_view = vk_textures[tglob.defaultTexture].vk.image.view;
		ASSERT(default_view != VK_NULL_HANDLE);
		for (int i = 0; i < MAX_TEXTURES; ++i) {
			const vk_texture_t *const tex = vk_textures + i;
			if (tex->vk.image.view)
				continue;

			tglob.dii_all_textures[i] = (VkDescriptorImageInfo){
				.imageView =  default_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = tglob.default_sampler_fixme,
			};
		}
	}

	return true;
}

static void textureDestroy( int index );

void R_VkTexturesShutdown( void ) {
	/* for( unsigned int i = 0; i < COUNTOF(g_textures.textures); i++ ) */
	/* 	textureDestroy( i ); */

	unloadSkybox();

	R_VkImageDestroy(&tglob.cubemap_placeholder.vk.image);
	g_textures.stats.size_total -= tglob.cubemap_placeholder.total_size;
	g_textures.stats.count--;
	memset(&tglob.cubemap_placeholder, 0, sizeof(tglob.cubemap_placeholder));

	for (int i = 0; i < COUNTOF(tglob.samplers); ++i) {
		if (tglob.samplers[i].sampler != VK_NULL_HANDLE)
			vkDestroySampler(vk_core.device, tglob.samplers[i].sampler, NULL);
	}
}

vk_texture_t *R_TextureGetByIndex(int index)
{
	ASSERT(index >= 0);
	ASSERT(index < MAX_TEXTURES);
	//return g_textures.textures + index;
	return vk_textures + index;
}

static int textureLoadFromFileF(int flags, colorspace_hint_e colorspace, const char *fmt, ...) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	const qboolean force_update = false;
	return loadTextureInternal(buffer, NULL, 0, flags, colorspace, force_update);
}

#define BLUE_NOISE_NAME_F "bluenoise/LDR_RGBA_%d.png"

static qboolean generateFallbackNoiseTextures(void) {
	pcg32_random_t pcg_state = {
		BLUE_NOISE_SIZE * BLUE_NOISE_SIZE - 1,
		17,
	};
	uint32_t scratch[BLUE_NOISE_SIZE * BLUE_NOISE_SIZE];
	rgbdata_t pic = {
		.width = BLUE_NOISE_SIZE,
		.height = BLUE_NOISE_SIZE,
		.depth = 1,
		.flags = 0,
		.type = PF_RGBA_32,
		.size = BLUE_NOISE_SIZE * BLUE_NOISE_SIZE * 4,
		.buffer = (byte*)&scratch,
		.palette = NULL,
		.numMips = 1,
		.encode = 0,
	};

	int blueNoiseTexturesBegin = -1;
	for (int i = 0; i < BLUE_NOISE_SIZE; ++i) {
		for (int j = 0; j < COUNTOF(scratch); ++j) {
			scratch[j] = pcg32_random_r(&pcg_state);
		}

		char name[256];
		snprintf(name, sizeof(name), BLUE_NOISE_NAME_F, i);
		const int texid = R_TextureUploadFromBufferNew(name, &pic, TF_NOMIPMAP);
		ASSERT(texid > 0);

		if (blueNoiseTexturesBegin == -1) {
			ASSERT(texid == BLUE_NOISE_TEXTURE_ID);
			blueNoiseTexturesBegin = texid;
		} else {
			ASSERT(blueNoiseTexturesBegin + i == texid);
		}
	}

	return true;
}

static qboolean loadBlueNoiseTextures(void) {
	int blueNoiseTexturesBegin = -1;
	for (int i = 0; i < 64; ++i) {
		const int texid = textureLoadFromFileF(TF_NOMIPMAP, kColorspaceLinear, BLUE_NOISE_NAME_F, i);

		if (blueNoiseTexturesBegin == -1) {
			if (texid <= 0) {
				ERR("Couldn't find precomputed blue noise textures. Generating bad quality regular noise textures as a fallback");
				return generateFallbackNoiseTextures();
			}

			blueNoiseTexturesBegin = texid;
		} else {
			ASSERT(texid > 0);
			ASSERT(blueNoiseTexturesBegin + i == texid);
		}
	}

	INFO("Base blue noise texture is %d", blueNoiseTexturesBegin);
	ASSERT(blueNoiseTexturesBegin == BLUE_NOISE_TEXTURE_ID);

	return true;
}

static void VK_CreateInternalTextures( void )
{
	int	dx2, dy, d;
	int	x, y;
	rgbdata_t	*pic;

	// emo-texture from quake1
	pic = Common_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR );

	for( y = 0; y < 16; y++ )
	{
		for( x = 0; x < 16; x++ )
		{
			if(( y < 8 ) ^ ( x < 8 ))
				((uint *)pic->buffer)[y*16+x] = 0xFFFF00FF;
			else ((uint *)pic->buffer)[y*16+x] = 0xFF000000;
		}
	}

	tglob.defaultTexture = R_TextureUploadFromBufferNew( REF_DEFAULT_TEXTURE, pic, TF_COLORMAP );

	// particle texture from quake1
	pic = Common_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA );

	for( x = 0; x < 16; x++ )
	{
		dx2 = x - 8;
		dx2 = dx2 * dx2;

		for( y = 0; y < 16; y++ )
		{
			dy = y - 8;
			d = 255 - 35 * sqrt( dx2 + dy * dy );
			pic->buffer[( y * 16 + x ) * 4 + 3] = bound( 0, d, 255 );
		}
	}

	tglob.particleTexture = R_TextureUploadFromBufferNew( REF_PARTICLE_TEXTURE, pic, TF_CLAMP );

	// white texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFFFFFFFF;
	tglob.whiteTexture = R_TextureUploadFromBufferNew( REF_WHITE_TEXTURE, pic, TF_COLORMAP );

	// gray texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF7F7F7F;
	tglob.grayTexture = R_TextureUploadFromBufferNew( REF_GRAY_TEXTURE, pic, TF_COLORMAP );

	// black texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF000000;
	tglob.blackTexture = R_TextureUploadFromBufferNew( REF_BLACK_TEXTURE, pic, TF_COLORMAP );

	// cinematic dummy
	pic = Common_FakeImage( 640, 100, 1, IMAGE_HAS_COLOR );
	tglob.cinTexture = R_TextureUploadFromBufferNew( "*cintexture", pic, TF_NOMIPMAP|TF_CLAMP );

	{
		rgbdata_t *sides[6];
		pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
		for( x = 0; x < 16; x++ )
			((uint *)pic->buffer)[x] = 0xFFFFFFFF;

		sides[0] = pic;
		sides[1] = pic;
		sides[2] = pic;
		sides[3] = pic;
		sides[4] = pic;
		sides[5] = pic;

		uploadTexture( &tglob.cubemap_placeholder, sides, 6, true, kColorspaceGamma );
	}

	loadBlueNoiseTextures();
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

	for (int i = 0; i < COUNTOF(tglob.samplers); ++i) {
		if (tglob.samplers[i].sampler == VK_NULL_HANDLE) {
			tglob.samplers[i].flags = flags;
			return tglob.samplers[i].sampler = createSamplerForFlags(flags);
		}

		if (tglob.samplers[i].flags == flags)
			return tglob.samplers[i].sampler;
	}

	ERR("Couldn't find/allocate sampler for flags %x", flags);
	return tglob.default_sampler_fixme;
}

static void setDescriptorSet(vk_texture_t* const tex, colorspace_hint_e colorspace_hint) {
	// FIXME detect skybox some other way
	if (tex->vk.image.layers > 1)
		return;

	const int index = tex - vk_textures;
	ASSERT(index >= 0);
	ASSERT(index < MAX_TEXTURES);

	const VkImageView view = tex->vk.image.view != VK_NULL_HANDLE ? tex->vk.image.view : vk_textures[tglob.defaultTexture].vk.image.view;

	if (view == VK_NULL_HANDLE)
		return;

	VkDescriptorImageInfo dii = {
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.sampler = pickSamplerForFlags( tex->flags ),
	};

	// Set descriptor for bindless/ray tracing
	tglob.dii_all_textures[index] = dii;

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

static qboolean uploadRawKtx2( vk_texture_t *tex, const rgbdata_t* pic ) {
	DEBUG("Uploading raw KTX2 texture[%d] %s", (int)(tex-vk_textures), tex->name);

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
			.debug_name = tex->name,
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

		const int index = tex - vk_textures;
		const VkDescriptorImageInfo dii = {
			.imageView = tex->vk.image.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.sampler = pickSamplerForFlags( tex->flags ),
		};
		tglob.dii_all_textures[index] = dii;
	}

	g_textures.stats.size_total += tex->total_size;
	g_textures.stats.count++;

	tex->width = header->pixelWidth;
	tex->height = header->pixelHeight;

	return true;
}

qboolean uploadTexture(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint) {
	tex->total_size = 0;

	if (num_layers == 1 && layers[0]->type == PF_KTX2_RAW) {
		if (!uploadRawKtx2(tex, layers[0]))
			return false;
	} else {
		const qboolean compute_mips = layers[0]->type == PF_RGBA_32 && layers[0]->numMips < 2;
		const VkFormat format = VK_GetFormat(layers[0]->type, colorspace_hint);
		const int mipCount = compute_mips ? CalcMipmapCount( tex, true ) : layers[0]->numMips;

		if (format == VK_FORMAT_UNDEFINED) {
			ERR("Unsupported PF format %d", layers[0]->type);
			return false;
		}

		if (!validatePicLayers(tex->name, layers, num_layers))
			return false;

		tex->width = layers[0]->width;
		tex->height = layers[0]->height;

		DEBUG("Uploading texture[%d] %s, mips=%d(build=%d), layers=%d", (int)(tex-vk_textures), tex->name, mipCount, compute_mips, num_layers);

		// TODO (not sure why, but GL does this)
		// if( !ImageCompressed( layers->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( layers->flags, IMAGE_ONEBIT_ALPHA ))
		// 	data = GL_ApplyFilter( data, tex->width, tex->height );

		if (tex->vk.image.image != VK_NULL_HANDLE)
			textureDestroyVkImage( tex );

		{
			const r_vk_image_create_t create = {
				.debug_name = tex->name,
				.width = tex->width,
				.height = tex->height,
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
			tex->vk.image = R_VkImageCreate(&create);
		}

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

	setDescriptorSet(tex, colorspace_hint);

	g_textures.stats.size_total += tex->total_size;
	g_textures.stats.count++;
	return true;
}

int R_TextureUploadFromFileEx( const char *filename, colorspace_hint_e colorspace, qboolean force_reload) {
	vk_texture_t	*tex;
	if( !Common_CheckTexName( filename ))
		return 0;

	return loadTextureInternal( filename, NULL, 0, 0, colorspace, force_reload );
}

static void textureDestroyVkImage( vk_texture_t *tex ) {
	// Need to make sure that there are no references to this texture anywhere.
	// It might have been added to staging and then immediately deleted, leaving references to its vkimage
	// in the staging command buffer. See https://github.com/w23/xash3d-fwgs/issues/464
	R_VkStagingFlushSync();
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	R_VkImageDestroy(&tex->vk.image);
	g_textures.stats.size_total -= tex->total_size;
	g_textures.stats.count--;

	tex->total_size = 0;
}

void R_TextureRelease( unsigned int texnum ) {
	vk_texture_t *tex;
	vk_texture_t **prev;
	vk_texture_t *cur;

	APROF_SCOPE_DECLARE_BEGIN(free, __FUNCTION__);

	if( texnum <= 0 )
		goto end;

	tex = vk_textures + texnum;

	// already freed?
	if( !tex->vk.image.image )
		goto end;

	// debug
	if( !tex->name[0] )
	{
		ERR("R_TextureRelease: trying to free unnamed texture with index %u", texnum );
		goto end;
	}

	DEBUG("Releasing texture=%d(%s) refcount=%d", texnum, tex->name, tex->refcount);
	ASSERT(tex->refcount > 0);
	--tex->refcount;

	if (tex->refcount > 0)
		goto end;

	DEBUG("Freeing texture=%d(%s)", texnum, tex->name);

	// remove from hash table
	prev = &vk_texturesHashTable[tex->hashValue];

	while( 1 )
	{
		cur = *prev;
		if( !cur ) break;

		if( cur == tex )
		{
			*prev = cur->nextHash;
			break;
		}
		prev = &cur->nextHash;
	}

	/*
	// release source
	if( tex->original )
		gEngine.FS_FreeImage( tex->original );
	*/

	textureDestroyVkImage( tex );

	memset(tex, 0, sizeof(*tex));

	// Reset descriptor sets to default texture
	setDescriptorSet(tex, kColorspaceNative);

end:
	APROF_SCOPE_END(free);
}

void unloadSkybox( void ) {
	if (tglob.skybox_cube.vk.image.image) {
		R_VkImageDestroy(&tglob.skybox_cube.vk.image);
		g_textures.stats.size_total -= tglob.skybox_cube.total_size;
		g_textures.stats.count--;
		memset(&tglob.skybox_cube, 0, sizeof(tglob.skybox_cube));
	}

	tglob.fCustomSkybox = false;
}

void R_TextureAcquire( unsigned int texnum ) {
	ASSERT(texnum < vk_numTextures);
	vk_texture_t *const tex = vk_textures + texnum;
	++tex->refcount;

	DEBUG("Acquiring existing texture %s(%d) refcount=%d", tex->name, (int)(tex-vk_textures), tex->refcount);
}
