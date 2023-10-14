#include "vk_textures.h"

#include "vk_common.h"
#include "vk_core.h"
#include "vk_staging.h"
#include "vk_const.h"
#include "vk_descriptor.h"
#include "vk_mapents.h" // wadlist
#include "vk_combuf.h"
#include "vk_logs.h"
#include "r_speeds.h"

#include "xash3d_mathlib.h"
#include "crtlib.h"
#include "crclib.h"
#include "com_strings.h"
#include "eiface.h"

#define PCG_IMPLEMENT
#include "pcg.h"

#include <memory.h>
#include <math.h>

#define LOG_MODULE LogModule_Textures
#define MODULE_NAME "textures"

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

static vk_texture_t vk_textures[MAX_TEXTURES];
static vk_texture_t* vk_texturesHashTable[TEXTURES_HASH_SIZE];
static uint vk_numTextures;
vk_textures_global_t tglob = {0};

static struct {
	struct {
		int count;
		int size_total;
	} stats;
} g_textures;

static void VK_CreateInternalTextures(void);
static VkSampler pickSamplerForFlags( texFlags_t flags );

void initTextures( void ) {
	R_SPEEDS_METRIC(g_textures.stats.count, "count", kSpeedsMetricCount);
	R_SPEEDS_METRIC(g_textures.stats.size_total, "size_total", kSpeedsMetricBytes);
	
	tglob.mempool = Mem_AllocPool( "vktextures" );

	memset( vk_textures, 0, sizeof( vk_textures ));
	memset( vk_texturesHashTable, 0, sizeof( vk_texturesHashTable ));
	vk_numTextures = 0;

	tglob.default_sampler_fixme = pickSamplerForFlags(0);
	ASSERT(tglob.default_sampler_fixme != VK_NULL_HANDLE);

	// create unused 0-entry
	Q_strncpy( vk_textures->name, "*unused*", sizeof( vk_textures->name ));
	vk_textures->hashValue = COM_HashKey( vk_textures->name, TEXTURES_HASH_SIZE );
	vk_textures->nextHash = vk_texturesHashTable[vk_textures->hashValue];
	vk_texturesHashTable[vk_textures->hashValue] = vk_textures;
	vk_numTextures = 1;

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
}

static void unloadSkybox( void );

void destroyTextures( void )
{
	for( unsigned int i = 0; i < vk_numTextures; i++ )
		VK_FreeTexture( i );

	unloadSkybox();

	R_VkImageDestroy(&tglob.cubemap_placeholder.vk.image);
	g_textures.stats.size_total -= tglob.cubemap_placeholder.total_size;
	g_textures.stats.count--;
	memset(&tglob.cubemap_placeholder, 0, sizeof(tglob.cubemap_placeholder));

	for (int i = 0; i < ARRAYSIZE(tglob.samplers); ++i) {
		if (tglob.samplers[i].sampler != VK_NULL_HANDLE)
			vkDestroySampler(vk_core.device, tglob.samplers[i].sampler, NULL);
	}

	//memset( tglob.lightmapTextures, 0, sizeof( tglob.lightmapTextures ));
	memset( vk_texturesHashTable, 0, sizeof( vk_texturesHashTable ));
	memset( vk_textures, 0, sizeof( vk_textures ));
	vk_numTextures = 0;
}

vk_texture_t *findTexture(int index)
{
	ASSERT(index >= 0);
	ASSERT(index < MAX_TEXTURES);
	return vk_textures + index;
}

static vk_texture_t *Common_AllocTexture( const char *name, texFlags_t flags )
{
	vk_texture_t	*tex;
	uint		i;

	// find a free texture_t slot
	for( i = 0, tex = vk_textures; i < vk_numTextures; i++, tex++ )
		if( !tex->name[0] ) break;

	if( i == vk_numTextures )
	{
		if( vk_numTextures == MAX_TEXTURES )
			gEngine.Host_Error( "VK_AllocTexture: MAX_TEXTURES limit exceeds\n" );
		vk_numTextures++;
	}

	tex = &vk_textures[i];

	// copy initial params
	Q_strncpy( tex->name, name, sizeof( tex->name ));
	tex->texnum = i; // texnum is used for fast acess into vk_textures array too
	tex->flags = flags;

	// add to hash table
	tex->hashValue = COM_HashKey( name, TEXTURES_HASH_SIZE );
	tex->nextHash = vk_texturesHashTable[tex->hashValue];
	vk_texturesHashTable[tex->hashValue] = tex;

	return tex;
}

static qboolean Common_CheckTexName( const char *name )
{
	int len;

	if( !COM_CheckString( name ))
		return false;

	len = Q_strlen( name );

	// because multi-layered textures can exceed name string
	if( len >= sizeof( vk_textures->name ))
	{
		ERR("LoadTexture: too long name %s (%d)", name, len );
		return false;
	}

	return true;
}

static vk_texture_t *Common_TextureForName( const char *name )
{
	vk_texture_t	*tex;
	uint		hash;

	// find the texture in array
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = vk_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return tex;
	}

	return NULL;
}

static rgbdata_t *Common_FakeImage( int width, int height, int depth, int flags )
{
	// TODO: Fix texture and it's buffer leaking.
	rgbdata_t *r_image = Mem_Malloc( tglob.mempool, sizeof( rgbdata_t ) );

	// also use this for bad textures, but without alpha
	r_image->width  = Q_max( 1, width );
	r_image->height = Q_max( 1, height );
	r_image->depth  = Q_max( 1, depth );
	r_image->flags  = flags;
	r_image->type   = PF_RGBA_32;
	
	r_image->size = r_image->width * r_image->height * r_image->depth * 4;
	if( FBitSet( r_image->flags, IMAGE_CUBEMAP )) r_image->size *= 6;

	r_image->buffer  = Mem_Malloc( tglob.mempool, r_image->size);
	r_image->palette = NULL;
	r_image->numMips = 1;
	r_image->encode  = 0;

	memset( r_image->buffer, 0xFF, r_image->size );

	return r_image;
}

/*
===============
GL_ProcessImage

do specified actions on pixels
===============
*/
static void VK_ProcessImage( vk_texture_t *tex, rgbdata_t *pic )
{
	float	emboss_scale = 0.0f;
	uint	img_flags = 0;

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	//FIXME provod: ??? tex->encode = pic->encode; // share encode method

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		/* FIXME provod: ???
		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = gEngine.FS_CopyImage( pic ); // because current pic will be expanded to rgba
		*/

		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		/* FIXME provod: ???
		// dedicated server doesn't register this variable
		if( gl_emboss_scale != NULL )
			emboss_scale = gl_emboss_scale->value;
		*/

		// processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer ) gEngine.Image_Process( &pic, 0, 0, img_flags, emboss_scale );

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

static qboolean uploadTexture(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint);

static int loadTextureInternal( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint );

static int VK_LoadTextureF(int flags, colorspace_hint_e colorspace, const char *fmt, ...) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	return loadTextureInternal(buffer, NULL, 0, flags, colorspace);
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
		const int texid = VK_LoadTextureInternal(name, &pic, TF_NOMIPMAP);
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
		const int texid = VK_LoadTextureF(TF_NOMIPMAP, kColorspaceLinear, BLUE_NOISE_NAME_F, i);

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

	tglob.defaultTexture = VK_LoadTextureInternal( REF_DEFAULT_TEXTURE, pic, TF_COLORMAP );

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

	tglob.particleTexture = VK_LoadTextureInternal( REF_PARTICLE_TEXTURE, pic, TF_CLAMP );

	// white texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFFFFFFFF;
	tglob.whiteTexture = VK_LoadTextureInternal( REF_WHITE_TEXTURE, pic, TF_COLORMAP );

	// gray texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF7F7F7F;
	tglob.grayTexture = VK_LoadTextureInternal( REF_GRAY_TEXTURE, pic, TF_COLORMAP );

	// black texture
	pic = Common_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF000000;
	tglob.blackTexture = VK_LoadTextureInternal( REF_BLACK_TEXTURE, pic, TF_COLORMAP );

	// cinematic dummy
	pic = Common_FakeImage( 640, 100, 1, IMAGE_HAS_COLOR );
	tglob.cinTexture = VK_LoadTextureInternal( "*cintexture", pic, TF_NOMIPMAP|TF_CLAMP );

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
		default:
			WARN("FIXME unsupported pixformat_t %d", format);
			return VK_FORMAT_UNDEFINED;
	}
}

static size_t CalcImageSize( pixformat_t format, int width, int height, int depth ) {
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_LUMINANCE:
		size = width * height * depth;
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	default:
		ERR("unsupported pixformat_t %d", format);
		ASSERT(!"Unsupported format encountered");
	}

	return size;
}

static int CalcMipmapCount( vk_texture_t *tex, qboolean haveBuffer )
{
	int	width, height;
	int	mipcount;

	ASSERT( tex != NULL );

	if( !haveBuffer )// || tex->target == GL_TEXTURE_3D )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;

	// mip-maps can't exceeds 16
	for( mipcount = 0; mipcount < 16; mipcount++ )
	{
		width = Q_max( 1, ( tex->width >> mipcount ));
		height = Q_max( 1, ( tex->height >> mipcount ));
		if( width == 1 && height == 1 )
			break;
	}

	return mipcount + 1;
}

static void BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	byte *out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	mipHeight = Q_max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
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

	for (int i = 0; i < ARRAYSIZE(tglob.samplers); ++i) {
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

static qboolean uploadTexture(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint) {
	const VkFormat format = VK_GetFormat(layers[0]->type, colorspace_hint);
	int mipCount = 0;

	tex->total_size = 0;

	// TODO non-rbga textures

	for (int i = 0; i < num_layers; ++i) {
		// FIXME create empty black texture if there's no buffer
		if (!layers[i]->buffer) {
			ERR("Texture %s layer %d missing buffer", tex->name, i);
			return false;
		}

		if (i == 0)
			continue;

		if (layers[0]->type != layers[i]->type) {
			ERR("Texture %s layer %d has type %d inconsistent with layer 0 type %d", tex->name, i, layers[i]->type, layers[0]->type);
			return false;
		}

		if (layers[0]->width != layers[i]->width || layers[0]->height != layers[i]->height) {
			ERR("Texture %s layer %d has resolution %dx%d inconsistent with layer 0 resolution %dx%d",
				tex->name, i, layers[i]->width, layers[i]->height, layers[0]->width, layers[0]->height);
			return false;
		}

		if ((layers[0]->flags ^ layers[i]->flags) & IMAGE_HAS_ALPHA) {
			ERR("Texture %s layer %d has_alpha=%d inconsistent with layer 0 has_alpha=%d",
				tex->name, i,
				!!(layers[i]->flags & IMAGE_HAS_ALPHA),
				!!(layers[0]->flags & IMAGE_HAS_ALPHA));
		}
	}

	tex->width = layers[0]->width;
	tex->height = layers[0]->height;
	mipCount = CalcMipmapCount( tex, true);

	DEBUG("Uploading texture[%d] %s, mips=%d, layers=%d", (int)(tex-vk_textures), tex->name, mipCount, num_layers);

	// TODO this vvv
	// // NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
	// if(( tex->depth == 1 ) && (( layers->width != tex->width ) || ( layers->height != tex->height )))
	// 	data = GL_ResampleTexture( buf, layers->width, layers->height, tex->width, tex->height, normalMap );
	// else data = buf;

	// if( !ImageDXT( layers->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( layers->flags, IMAGE_ONEBIT_ALPHA ))
	// 	data = GL_ApplyFilter( data, tex->width, tex->height );

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
				| ((layers[0]->flags & IMAGE_HAS_ALPHA) ? kVkImageFlagHasAlpha : 0)
				| (cubemap ? kVkImageFlagIsCubemap : 0)
				| (colorspace_hint == kColorspaceGamma ? kVkImageFlagCreateUnormView : 0),
		};
		tex->vk.image = R_VkImageCreate(&create);
	}

	{
		// 	5.1 upload buf -> image:layout:DST
		// 		5.1.1 transitionToLayout(UNDEFINED -> DST)
		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = tex->vk.image.image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = mipCount,
				.baseArrayLayer = 0,
				.layerCount = num_layers,
			}};

		{
			// cmdbuf may become invalidated in locks in the loops below
			const VkCommandBuffer cmdbuf = R_VkStagingGetCommandBuffer();
			vkCmdPipelineBarrier(cmdbuf,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, NULL, 0, NULL, 1, &image_barrier);
		}

		// 		5.1.2 copyBufferToImage for all mip levels
		for (int layer = 0; layer < num_layers; ++layer) {
			for (int mip = 0; mip < mipCount; ++mip) {
				const rgbdata_t *const pic = layers[layer];
				byte *buf = pic->buffer;
				const int width = Q_max( 1, ( pic->width >> mip ));
				const int height = Q_max( 1, ( pic->height >> mip ));
				const size_t mip_size = CalcImageSize( pic->type, width, height, 1 );
				const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(format);
				const vk_staging_image_args_t staging_args = {
					.image = tex->vk.image.image,
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
					.size = mip_size,
					.alignment = texel_block_size,
				};

				const vk_staging_region_t staging = R_VkStagingLockForImage(staging_args);
				ASSERT(staging.ptr);
				memcpy(staging.ptr, buf, mip_size);

				tex->total_size += mip_size;

				// Build mip in place for the next mip level
				if ( mip < mipCount - 1 )
				{
					BuildMipMap( buf, width, height, 1, tex->flags );
				}

				R_VkStagingUnlock(staging.handle);
			}
		}

		// TODO Don't change layout here. Alternatively:
		// I. Attach layout metadata to the image, and request its change next time it is used.
		// II. Build-in layout transfer to staging commit and do it there on commit.
		const VkCommandBuffer cmdbuf = R_VkStagingCommit()->cmdbuf;

		// 	5.2 image:layout:DST -> image:layout:SAMPLED
		// 		5.2.1 transitionToLayout(DST -> SHADER_READ_ONLY)
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barrier.subresourceRange = (VkImageSubresourceRange){
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mipCount,
			.baseArrayLayer = 0,
			.layerCount = num_layers,
		};
		vkCmdPipelineBarrier(cmdbuf,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // FIXME incorrect, we also use them in compute and potentially ray tracing shaders
				0, 0, NULL, 0, NULL, 1, &image_barrier);

	}

	// TODO how should we approach this:
	// - per-texture desc sets can be inconvenient if texture is used in different incompatible contexts
	// - update descriptor sets in batch?
	if (vk_desc.next_free < MAX_TEXTURES-2) {
		const int index = tex - vk_textures;
		const VkDescriptorSet ds = vk_desc.sets[vk_desc.next_free++];
		const VkDescriptorSet ds_unorm =
			(colorspace_hint == kColorspaceGamma && tex->vk.image.view_unorm != VK_NULL_HANDLE)
			? vk_desc.sets[vk_desc.next_free++] : VK_NULL_HANDLE;

		const VkDescriptorImageInfo dii = {
			.imageView = tex->vk.image.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.sampler = pickSamplerForFlags( tex->flags ),
		};

		const VkDescriptorImageInfo dii_unorm = {
			.imageView = tex->vk.image.view_unorm,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.sampler = dii.sampler,
		};

		VkWriteDescriptorSet wds[2] = { {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &dii,
			.dstSet = ds,
		}, {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &dii_unorm,
			.dstSet = ds_unorm,
		}};
		vkUpdateDescriptorSets(vk_core.device, ds_unorm != VK_NULL_HANDLE ? 2 : 1 , wds, 0, NULL);

		// FIXME handle cubemaps properly w/o this garbage. they should be the same as regular textures.
		if (num_layers == 1) {
			tglob.dii_all_textures[index] = dii;
		}

		tex->vk.descriptor_unorm = ds_unorm != VK_NULL_HANDLE ? ds_unorm : ds;
	}
	else
	{
		tex->vk.descriptor_unorm = VK_NULL_HANDLE;
	}

	g_textures.stats.size_total += tex->total_size;
	g_textures.stats.count++;
	return true;
}

///////////// Render API funcs /////////////

// Texture tools
int	VK_FindTexture( const char *name )
{
	vk_texture_t *tex;

	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = Common_TextureForName( name )))
		return (tex - vk_textures);

	return 0;
}
const char*	VK_TextureName( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return vk_textures[texnum].name;
}

const byte*	VK_TextureData( unsigned int texnum )
{
	PRINT_NOT_IMPLEMENTED_ARGS("texnum=%d", texnum);
	// We don't store original texture data
	// TODO do we need to?
	return NULL;
}


#define KTX_IDENTIFIER_SIZE 12
static const char k_ktx2_identifier[KTX_IDENTIFIER_SIZE] = {
  '\xAB', 'K', 'T', 'X', ' ', '2', '0', '\xBB', '\r', '\n', '\x1A', '\n'
};

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
} ktx_header_t;

typedef struct {
	uint32_t dfdByteOffset;
	uint32_t dfdByteLength;
	uint32_t kvdByteOffset;
	uint32_t kvdByteLength;
	uint64_t sgdByteOffset;
	uint64_t sgdByteLength;
} ktx_index_t;

typedef struct {
	uint64_t byteOffset;
	uint64_t byteLength;
	uint64_t uncompressedByteLength;
} ktx_level_t;

static int loadKtx2( const char *name ) {
	fs_offset_t size = 0;
	byte *data = gEngine.fsapi->LoadFile( name, &size, false );

	DEBUG("Loading KTX2 file \"%s\", exists=%d", name, data != 0);

	if ( !data )
		return 0;

	const ktx_header_t* header;
	const ktx_index_t* index;
	const ktx_level_t* levels;
	vk_texture_t* tex = NULL;

	if (size < (sizeof k_ktx2_identifier + sizeof(ktx_header_t) + sizeof(ktx_index_t) + sizeof(ktx_level_t))) {
		ERR("KTX2 file \"%s\" seems truncated", name);
		goto fail;
	}

	if (memcmp(data, k_ktx2_identifier, sizeof k_ktx2_identifier) != 0) {
		ERR("KTX2 file \"%s\" identifier is invalid", name);
		goto fail;
	}

	header = (const ktx_header_t*)(data + sizeof k_ktx2_identifier);
	index = (const ktx_index_t*)(data + sizeof k_ktx2_identifier + sizeof(ktx_header_t));
	levels = (const ktx_level_t*)(data + sizeof k_ktx2_identifier + sizeof(ktx_header_t) + sizeof(ktx_index_t));

	DEBUG("KTX2 file \"%s\"", name);
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
		const ktx_level_t* const level = levels + mip;
		DEBUG(" level[%d]:", mip);
		DEBUG("  byteOffset=%llu", (unsigned long long)level->byteOffset);
		DEBUG("  byteLength=%llu", (unsigned long long)level->byteLength);
		DEBUG("  uncompressedByteLength=%llu", (unsigned long long)level->uncompressedByteLength);
	}

	{
		const uint32_t flags = 0;
		tex = Common_AllocTexture( name, flags );
		if (!tex)
			goto fail;
	}

	// FIXME check that format is supported
	// FIXME layers == 0
	// FIXME has_alpha
	// FIXME no supercompressionScheme

	// 1. Create image
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

	// 2. Prep cmdbuf, barrier, etc
	{
		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = tex->vk.image.image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = header->levelCount,
				.baseArrayLayer = 0,
				.layerCount = 1, // TODO cubemap
			}
		};

		{
			// cmdbuf may become invalidated in locks in the loops below
			const VkCommandBuffer cmdbuf = R_VkStagingGetCommandBuffer();
			vkCmdPipelineBarrier(cmdbuf,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, 0, NULL, 0, NULL, 1, &image_barrier);
		}

	// 3. For levels
	// 3.1 upload
		for (int mip = 0; mip < header->levelCount; ++mip) {
			const ktx_level_t* const level = levels + mip;
			const uint32_t width = Q_max(1, header->pixelWidth >> mip);
			const uint32_t height = Q_max(1, header->pixelHeight >> mip);
			const size_t mip_size = level->byteLength;
			const uint32_t texel_block_size = R_VkImageFormatTexelBlockSize(header->vkFormat);
			const void* const image_data = data + level->byteOffset;
			const vk_staging_image_args_t staging_args = {
				.image = tex->vk.image.image,
				.region = (VkBufferImageCopy) {
					.bufferOffset = 0,
					.bufferRowLength = 0,
					.bufferImageHeight = 0,
					.imageSubresource = (VkImageSubresourceLayers){
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = mip,
						.baseArrayLayer = 0, // TODO cubemap
						.layerCount = 1,
					},
					.imageExtent = (VkExtent3D){
						.width = width,
						.height = height,
						.depth = 1,
					},
				},
				.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.size = mip_size,
				.alignment = texel_block_size,
			};

			{
				const vk_staging_region_t staging = R_VkStagingLockForImage(staging_args);
				ASSERT(staging.ptr);
				memcpy(staging.ptr, image_data, mip_size);
				tex->total_size += mip_size;
				R_VkStagingUnlock(staging.handle);
			}
		} // for levels

		{
			// TODO Don't change layout here. Alternatively:
			// I. Attach layout metadata to the image, and request its change next time it is used.
			// II. Build-in layout transfer to staging commit and do it there on commit.
			const VkCommandBuffer cmdbuf = R_VkStagingCommit()->cmdbuf;

			// 	5.2 image:layout:DST -> image:layout:SAMPLED
			// 		5.2.1 transitionToLayout(DST -> SHADER_READ_ONLY)
			image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			image_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_barrier.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = header->levelCount,
				.baseArrayLayer = 0,
				.layerCount = 1, // TODO cubemap
			};
			vkCmdPipelineBarrier(cmdbuf,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // FIXME incorrect, we also use them in compute and potentially ray tracing shaders
					0, 0, NULL, 0, NULL, 1, &image_barrier);
		}
	}

	// KTX2 textures are inaccessible from trad renderer (for now)
	tex->vk.descriptor_unorm = VK_NULL_HANDLE;

	// TODO how should we approach this:
	// - per-texture desc sets can be inconvenient if texture is used in different incompatible contexts
	// - update descriptor sets in batch?

	if (vk_desc.next_free != MAX_TEXTURES) {
		const int num_layers = 1; // TODO cubemap
		const int index = tex - vk_textures;
		VkDescriptorImageInfo dii_tmp;
		// FIXME handle cubemaps properly w/o this garbage. they should be the same as regular textures.
		VkDescriptorImageInfo *const dii_tex = (num_layers == 1) ? tglob.dii_all_textures + index : &dii_tmp;
		*dii_tex = (VkDescriptorImageInfo){
			.imageView = tex->vk.image.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.sampler = pickSamplerForFlags( tex->flags ),
		};
		const VkWriteDescriptorSet wds[] = { {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = dii_tex,
			.dstSet = vk_desc.sets[vk_desc.next_free++],
		}};
		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
	}

	g_textures.stats.size_total += tex->total_size;
	g_textures.stats.count++;

	tex->width = header->pixelWidth;
	tex->height = header->pixelHeight;

	goto finalize;

fail:
	if (tex)
		memset( tex, 0, sizeof( vk_texture_t ));

finalize:
	Mem_Free( data );
	return (tex - vk_textures);
}

static int loadTextureUsingEngine( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint ) {
	uint picFlags = 0;

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	// set some image flags
	gEngine.Image_SetForceFlags( picFlags );

	rgbdata_t *const pic = gEngine.FS_LoadImage( name, buf, size );
	if( !pic ) return 0; // couldn't loading image

	// allocate the new one
	vk_texture_t* const tex = Common_AllocTexture( name, flags );

	// upload texture
	VK_ProcessImage( tex, pic );

	if( !uploadTexture( tex, &pic, 1, false, colorspace_hint ))
	{
		memset( tex, 0, sizeof( vk_texture_t ));
		gEngine.FS_FreeImage( pic ); // release source texture
		return 0;
	}

	tex->width = pic->width;
	tex->height = pic->height;

	gEngine.FS_FreeImage( pic ); // release source texture

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return tex - vk_textures;
}

static int loadTextureInternal( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint ) {
	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	vk_texture_t *tex = Common_TextureForName( name );
	if( tex )
		return (tex - vk_textures);

	{
		const char *ext = Q_strrchr(name, '.');
		if (Q_strcmp(ext, ".ktx2") == 0) {
			return loadKtx2(name);
		}
	}

	return loadTextureUsingEngine(name, buf, size, flags, colorspace_hint);
}

int VK_LoadTextureExternal( const char *name, const byte *buf, size_t size, int flags ) {
	return loadTextureInternal(name, buf, size, flags, kColorspaceGamma);
}

int R_VkLoadTexture( const char *filename, colorspace_hint_e colorspace, qboolean force_reload) {
	vk_texture_t	*tex;
	if( !Common_CheckTexName( filename ))
		return 0;

	if (force_reload) {
		// free if already loaded
		// TODO consider leaving intact if loading failed
		if(( tex = Common_TextureForName( filename ))) {
			VK_FreeTexture( tex - vk_textures );
		}
	}

	return loadTextureInternal( filename, NULL, 0, 0, colorspace );
}

int	VK_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	PRINT_NOT_IMPLEMENTED_ARGS("name=%s width=%d height=%d buffer=%p flags=%08x", name, width, height, buffer, flags);
	return 0;
}

int	VK_LoadTextureArray( const char **names, int flags )
{
	PRINT_NOT_IMPLEMENTED();
	return 0;
}

int	VK_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	PRINT_NOT_IMPLEMENTED_ARGS("name=%s width=%d height=%d buffer=%p flags=%08x", name, width, height, buffer, flags);
	return 0;
}

void VK_FreeTexture( unsigned int texnum ) {
	vk_texture_t *tex;
	vk_texture_t **prev;
	vk_texture_t *cur;
	if( texnum <= 0 ) return;

	tex = vk_textures + texnum;

	ASSERT( tex != NULL );

	// already freed?
	if( !tex->vk.image.image ) return;

	// debug
	if( !tex->name[0] )
	{
		ERR("VK_FreeTexture: trying to free unnamed texture with index %u", texnum );
		return;
	}

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

	// Need to make sure that there are no references to this texture anywhere.
	// It might have been added to staging and then immediately deleted, leaving references to its vkimage
	// in the staging command buffer. See https://github.com/w23/xash3d-fwgs/issues/464
	R_VkStagingFlushSync();
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	R_VkImageDestroy(&tex->vk.image);
	g_textures.stats.size_total -= tex->total_size;
	g_textures.stats.count--;
	memset(tex, 0, sizeof(*tex));

	tglob.dii_all_textures[texnum] = (VkDescriptorImageInfo){
		.imageView = vk_textures[tglob.defaultTexture].vk.image.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.sampler = tglob.default_sampler_fixme,
	};
}

static int loadTextureFromBuffers( const char *name, rgbdata_t *const *const pic, int pic_count, texFlags_t flags, qboolean update ) {
	vk_texture_t	*tex;

	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = Common_TextureForName( name )) && !update )
		return (tex - vk_textures);

	// couldn't loading image
	if( !pic ) return 0;

	if( update )
	{
		if( tex == NULL )
			gEngine.Host_Error( "VK_LoadTextureFromBuffer: couldn't find texture %s for update\n", name );
		SetBits( tex->flags, flags );
	}
	else
	{
		// allocate the new one
		tex = Common_AllocTexture( name, flags );
	}

	for (int i = 0; i < pic_count; ++i)
		VK_ProcessImage( tex, pic[i] );

	if( !uploadTexture( tex, pic, pic_count, false, kColorspaceGamma ))
	{
		memset( tex, 0, sizeof( vk_texture_t ));
		return 0;
	}

	return (tex - vk_textures);
}

int VK_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update ) {
	return loadTextureFromBuffers(name, &pic, 1, flags, update);
}

int XVK_TextureLookupF( const char *fmt, ...) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	tex_id = VK_FindTexture(buffer);
	//DEBUG("Looked up texture %s -> %d", buffer, tex_id);
	return tex_id;
}

static void unloadSkybox( void ) {
	if (tglob.skybox_cube.vk.image.image) {
		R_VkImageDestroy(&tglob.skybox_cube.vk.image);
		g_textures.stats.size_total -= tglob.skybox_cube.total_size;
		g_textures.stats.count--;
		memset(&tglob.skybox_cube, 0, sizeof(tglob.skybox_cube));
	}

	tglob.fCustomSkybox = false;
}

static struct {
	const char *suffix;
	uint flags;
} g_skybox_info[6] = {
	{"rt", IMAGE_ROT_90},
	{"lf", IMAGE_FLIP_Y | IMAGE_ROT_90 | IMAGE_FLIP_X},
	{"bk", IMAGE_FLIP_Y},
	{"ft", IMAGE_FLIP_X},
	{"up", IMAGE_ROT_90},
	{"dn", IMAGE_ROT_90},
};

#define SKYBOX_MISSED	0
#define SKYBOX_HLSTYLE	1
#define SKYBOX_Q1STYLE	2

static int CheckSkybox( const char *name )
{
	const char	*skybox_ext[] = { "png", "dds", "tga", "bmp" };
	int		i, j, num_checked_sides;
	char		sidename[MAX_VA_STRING];

	// search for skybox images
	for( i = 0; i < ARRAYSIZE(skybox_ext); i++ )
	{
		num_checked_sides = 0;
		for( j = 0; j < 6; j++ )
		{
			// build side name
			Q_snprintf( sidename, sizeof( sidename ), "%s%s.%s", name, g_skybox_info[j].suffix, skybox_ext[i] );
			if( gEngine.fsapi->FileExists( sidename, false ))
				num_checked_sides++;

		}

		if( num_checked_sides == 6 )
			return SKYBOX_HLSTYLE; // image exists

		for( j = 0; j < 6; j++ )
		{
			// build side name
			Q_snprintf( sidename, sizeof( sidename ), "%s_%s.%s", name, g_skybox_info[j].suffix, skybox_ext[i] );
			if( gEngine.fsapi->FileExists( sidename, false ))
				num_checked_sides++;
		}

		if( num_checked_sides == 6 )
			return SKYBOX_Q1STYLE; // images exists
	}

	return SKYBOX_MISSED;
}

static qboolean loadSkybox( const char *prefix, int style ) {
	rgbdata_t *sides[6];
	qboolean success = false;
	int i;

	// release old skybox
	unloadSkybox();
	DEBUG( "SKY:  " );

	for( i = 0; i < 6; i++ ) {
		char sidename[MAX_STRING];
		if( style == SKYBOX_HLSTYLE )
			Q_snprintf( sidename, sizeof( sidename ), "%s%s", prefix, g_skybox_info[i].suffix );
		else Q_snprintf( sidename, sizeof( sidename ), "%s_%s", prefix, g_skybox_info[i].suffix );

		sides[i] = gEngine.FS_LoadImage( sidename, NULL, 0);
		if (!sides[i] || !sides[i]->buffer)
			break;

		{
			uint img_flags = g_skybox_info[i].flags;
			// we need to expand image into RGBA buffer
			if( sides[i]->type == PF_INDEXED_24 || sides[i]->type == PF_INDEXED_32 )
				img_flags |= IMAGE_FORCE_RGBA;
			gEngine.Image_Process( &sides[i], 0, 0, img_flags, 0.f );
		}
		DEBUG( "%s%s%s", prefix, g_skybox_info[i].suffix, i != 5 ? ", " : ". " );
	}

	if( i != 6 )
		goto cleanup;

	if( !Common_CheckTexName( prefix ))
		goto cleanup;

	Q_strncpy( tglob.skybox_cube.name, prefix, sizeof( tglob.skybox_cube.name ));
	success = uploadTexture(&tglob.skybox_cube, sides, 6, true, kColorspaceGamma);

cleanup:
	for (int j = 0; j < i; ++j)
		gEngine.FS_FreeImage( sides[j] ); // release source texture

	if (success) {
		tglob.fCustomSkybox = true;
		DEBUG( "Skybox done" );
	} else {
		tglob.skybox_cube.name[0] = '\0';
		ERR( "Skybox failed" );
		unloadSkybox();
	}

	return success;
}

static const char *skybox_default = "desert";
static const char *skybox_prefixes[] = { "pbr/env/%s", "gfx/env/%s" };

void XVK_SetupSky( const char *skyboxname ) {
	if( !COM_CheckString( skyboxname ))
	{
		unloadSkybox();
		return; // clear old skybox
	}

	for (int i = 0; i < ARRAYSIZE(skybox_prefixes); ++i) {
		char	loadname[MAX_STRING];
		int style, len;

		Q_snprintf( loadname, sizeof( loadname ), skybox_prefixes[i], skyboxname );
		COM_StripExtension( loadname );

		// kill the underline suffix to find them manually later
		len = Q_strlen( loadname );

		if( loadname[len - 1] == '_' )
			loadname[len - 1] = '\0';
		style = CheckSkybox( loadname );

		if (loadSkybox(loadname, style))
			return;
	}

	if (Q_stricmp(skyboxname, skybox_default) != 0) {
		WARN("missed or incomplete skybox '%s'", skyboxname);
		XVK_SetupSky( "desert" ); // force to default
	}
}

int XVK_FindTextureNamedLike( const char *texture_name ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );
	string texname;

	// Try texture name as-is first
	int tex_id = XVK_TextureLookupF("%s", texture_name);

	// Try bsp name
	if (!tex_id)
		tex_id = XVK_TextureLookupF("#%s:%s.mip", map->name, texture_name);

	if (!tex_id) {
		const char *wad = g_map_entities.wadlist;
		for (; *wad;) {
			const char *const wad_end = Q_strchr(wad, ';');
			tex_id = XVK_TextureLookupF("%.*s/%s.mip", wad_end - wad, wad, texture_name);
			if (tex_id)
				break;
			wad = wad_end + 1;
		}
	}

	return tex_id ? tex_id : -1;
}

int XVK_CreateDummyTexture( const char *name ) {
	// emo-texture from quake1
	rgbdata_t *pic = Common_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR );

	for( int y = 0; y < 16; y++ )
	{
		for( int x = 0; x < 16; x++ )
		{
			if(( y < 8 ) ^ ( x < 8 ))
				((uint *)pic->buffer)[y*16+x] = 0xFFFF00FF;
			else ((uint *)pic->buffer)[y*16+x] = 0xFF000000;
		}
	}

	return VK_LoadTextureInternal(name, pic, TF_NOMIPMAP);
}
