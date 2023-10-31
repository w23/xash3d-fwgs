#include "r_textures.h"
#include "vk_textures.h"

#include "vk_common.h"
#include "vk_const.h"
#include "vk_mapents.h" // wadlist
#include "vk_logs.h"
#include "r_speeds.h"
#include "profiler.h"
#include "unordered_roadmap.h"

#include "xash3d_mathlib.h"
#include "crtlib.h"
#include "crclib.h" // COM_HashKey
#include "com_strings.h"
#include "eiface.h" // ARRAYSIZE

#include <memory.h>
#include <math.h>

#define LOG_MODULE LogModule_Textures
#define MODULE_NAME "textures"

vk_textures_global_t tglob = {0};

static struct {
	poolhandle_t mempool;

	vk_texture_t all[MAX_TEXTURES];
	urmom_desc_t all_desc;
} g_textures;

// FIXME imported from vk_textures.h
void unloadSkybox( void );

static void createDefaultTextures( void );
static void destroyDefaultTextures( void );
static void destroyTexture( uint texnum );

#define R_TextureUploadFromBufferNew(name, pic, flags) R_TextureUploadFromBuffer(name, pic, flags, /*update_only=*/false)

qboolean R_TexturesInit( void ) {
	g_textures.mempool = Mem_AllocPool( "vktextures" );

	g_textures.all_desc = (urmom_desc_t){
		.array = g_textures.all,
		.count = COUNTOF(g_textures.all),
		.item_size = sizeof(g_textures.all[0]),
		.type = kUrmomStringInsensitive,
	};

	urmomInit(&g_textures.all_desc);

	// Mark index 0 as occupied to have a special "no texture" value
	g_textures.all[0].hdr_.hash = 0x7fffffff;
	g_textures.all[0].hdr_.state = 1;
	Q_strncpy( g_textures.all[0].hdr_.key, "*unused*", sizeof(g_textures.all[0].hdr_.key));

	createDefaultTextures();

	if (!R_VkTexturesInit())
		return false;

	return true;
}

void R_TexturesShutdown( void )
{
	destroyDefaultTextures();

	// By this point ideally all texture should have been destroyed.
	// However, there are two possible ways some texture could have been left over:
	// 1. Our coding mistakes, not releasing textures when done
	// 2. Engine and other external things not cleaning up (e.g. mainui is known to leave textures)
	for( int i = 1; i < COUNTOF(g_textures.all); i++ ) {
		const vk_texture_t *const tex = g_textures.all + i;
		if (!URMOM_IS_OCCUPIED(tex->hdr_))
			continue;

		// Try to free external textures
		R_TextureFree( i );

		// If it is still not deleted, complain loudly
		if (URMOM_IS_OCCUPIED(tex->hdr_)) {
			// TODO consider ASSERT, as this is a coding mistake
			ERR("stale texture[%d] '%s' refcount=%d", i, TEX_NAME(tex), tex->refcount);
			destroyTexture( i );
		}
	}

	int is_deleted_count = 0;
	int clusters[16] = {0};
	int current_cluster_begin = -1;
	for( int i = 1; i < COUNTOF(g_textures.all); i++ ) {
		const vk_texture_t *const tex = g_textures.all + i;

		if (URMOM_IS_EMPTY(tex->hdr_)) {
			if (current_cluster_begin >= 0) {
				const int cluster_length = i - current_cluster_begin;
				clusters[cluster_length >= COUNTOF(clusters) ? 0 : cluster_length]++;
			}
			current_cluster_begin = -1;
		} else {
			if (current_cluster_begin < 0)
				current_cluster_begin = i;
		}

		if (URMOM_IS_DELETED(tex->hdr_))
			++is_deleted_count;

		ASSERT(!URMOM_IS_OCCUPIED(tex->hdr_));
	}

	// TODO handle wraparound clusters
	if (current_cluster_begin >= 0) {
		const int cluster_length = COUNTOF(g_textures.all) - current_cluster_begin;
		clusters[cluster_length >= COUNTOF(clusters) ? 0 : cluster_length]++;
	}

	DEBUG("Deleted slots in texture hash table: %d", is_deleted_count);
	for (int i = 1; i < COUNTOF(clusters); ++i)
		DEBUG("Texture hash table cluster[%d] = %d", i, clusters[i]);

	DEBUG("Clusters longer than %d: %d", (int)COUNTOF(clusters)-1, clusters[0]);

	R_VkTexturesShutdown();
}

/* OBSOLETE
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

	// FIXME this is not strictly correct. Refcount management should be done differently wrt public ref_interface_t
	tex->refcount = 1;
	return tex;
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
*/

static qboolean checkTextureName( const char *name )
{
	int len;

	if( !COM_CheckString( name ))
		return false;

	len = Q_strlen( name );

	// because multi-layered textures can exceed name string
	if( len >= sizeof( g_textures.all[0].hdr_.key ))
	{
		ERR("LoadTexture: too long name %s (%d)", name, len );
		return false;
	}

	return true;
}

static rgbdata_t *Common_FakeImage( int width, int height, int depth, int flags )
{
	// TODO: Fix texture and it's buffer leaking.
	rgbdata_t *r_image = Mem_Malloc( g_textures.mempool, sizeof( rgbdata_t ) );

	// also use this for bad textures, but without alpha
	r_image->width  = Q_max( 1, width );
	r_image->height = Q_max( 1, height );
	r_image->depth  = Q_max( 1, depth );
	r_image->flags  = flags;
	r_image->type   = PF_RGBA_32;

	r_image->size = r_image->width * r_image->height * r_image->depth * 4;
	if( FBitSet( r_image->flags, IMAGE_CUBEMAP )) r_image->size *= 6;

	r_image->buffer  = Mem_Malloc( g_textures.mempool, r_image->size);
	r_image->palette = NULL;
	r_image->numMips = 1;
	r_image->encode  = 0;

	memset( r_image->buffer, 0xFF, r_image->size );

	return r_image;
}

static void createDefaultTextures( void )
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

		R_VkTexturesSkyboxUpload( "skybox_placeholder", sides, kColorspaceGamma, true );
	}
}

static void destroyDefaultTextures( void ) {
	if (tglob.cinTexture > 0)
		R_TextureFree( tglob.cinTexture );

	if (tglob.blackTexture > 0)
		R_TextureFree( tglob.blackTexture );

	if (tglob.grayTexture > 0)
		R_TextureFree( tglob.grayTexture );

	if (tglob.whiteTexture > 0)
		R_TextureFree( tglob.whiteTexture );

	if (tglob.particleTexture > 0)
		R_TextureFree( tglob.particleTexture );

	if (tglob.defaultTexture > 0)
		R_TextureFree( tglob.defaultTexture );
}

static void ProcessImage( vk_texture_t *tex, rgbdata_t *pic )
{
	float	emboss_scale = 0.0f;
	uint	img_flags = 0;

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	//FIXME provod: ??? tex->encode = pic->encode; // share encode method

	if( ImageCompressed( pic->type ))
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

size_t CalcImageSize( pixformat_t format, int width, int height, int depth ) {
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
	case PF_BC4_UNSIGNED:
	case PF_BC4_SIGNED:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_BC6H_UNSIGNED:
	case PF_BC6H_SIGNED:
	case PF_BC7_UNORM:
	case PF_BC7_SRGB:
	case PF_ATI2:
	case PF_BC5_UNSIGNED:
	case PF_BC5_SIGNED:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	default:
		ERR("%s: unsupported pixformat_t %d", __FUNCTION__, format);
		ASSERT(!"Unsupported format encountered");
	}

	return size;
}

int CalcMipmapCount( int width, int height, int depth, uint32_t flags, qboolean haveBuffer )
{
	int	mipcount;

	if( !haveBuffer )// || tex->target == GL_TEXTURE_3D )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( flags, TF_NOMIPMAP ))
		return 1;

	// mip-maps can't exceeds 16
	for( mipcount = 0; mipcount < 16; mipcount++ )
	{
		const int mip_width = Q_max( 1, ( width >> mipcount ));
		const int mip_height = Q_max( 1, ( height >> mipcount ));
		const int mip_depth = Q_max( 1, ( depth >> mipcount ));
		if( mip_width == 1 && mip_height == 1 && mip_depth == 1 )
			break;
	}

	return mipcount + 1;
}

void BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
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

qboolean validatePicLayers(const char* const name, rgbdata_t *const *const layers, int num_layers) {
	for (int i = 0; i < num_layers; ++i) {
		// FIXME create empty black texture if there's no buffer
		if (!layers[i]->buffer) {
			ERR("Texture %s layer %d missing buffer", name, i);
			return false;
		}

		if (i == 0)
			continue;

		if (layers[0]->type != layers[i]->type) {
			ERR("Texture %s layer %d has type %d inconsistent with layer 0 type %d", name, i, layers[i]->type, layers[0]->type);
			return false;
		}

		if (layers[0]->width != layers[i]->width
			|| layers[0]->height != layers[i]->height
			|| layers[0]->depth != layers[i]->depth) {
			ERR("Texture %s layer %d has resolution %dx%d%d inconsistent with layer 0 resolution %dx%dx%d",
				name, i,
				layers[i]->width, layers[i]->height, layers[i]->depth,
				layers[0]->width, layers[0]->height, layers[0]->depth);
			return false;
		}

		if ((layers[0]->flags ^ layers[i]->flags) & IMAGE_HAS_ALPHA) {
			ERR("Texture %s layer %d has_alpha=%d inconsistent with layer 0 has_alpha=%d",
				name, i,
				!!(layers[i]->flags & IMAGE_HAS_ALPHA),
				!!(layers[0]->flags & IMAGE_HAS_ALPHA));
			return false;
		}

		if (layers[0]->numMips != layers[i]->numMips) {
			ERR("Texture %s layer %d has numMips %d inconsistent with layer 0 numMips %d",
				name, i, layers[i]->numMips, layers[0]->numMips);
			return false;
		}
	}

	return true;
}

///////////// Render API funcs /////////////
int R_TextureFindByName( const char *name )
{
	vk_texture_t *tex;

	if( !checkTextureName( name ))
		return 0;

	const int index = urmomFind(&g_textures.all_desc, name);
	return index > 0 ? index : 0;
}

const char* R_TextureGetNameByIndex( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return g_textures.all[texnum].hdr_.key;
}

static int loadTextureInternalFromFile( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint, qboolean force_update, qboolean ref_interface ) {
	qboolean success = false;
	if( !checkTextureName( name ))
		return 0;

	const urmom_insert_t insert = urmomInsert(&g_textures.all_desc, name);
	if (insert.index < 0) {
		ERR("Cannot allocate texture slot for \"%s\"", name);
		return 0;
	}

	ASSERT(insert.index < COUNTOF(g_textures.all));

	vk_texture_t *const tex = g_textures.all + insert.index;

	// return existing if already loaded and was not forced to reload
	if (!insert.created && !force_update) {
		DEBUG("Found existing texture %s(%d) refcount=%d", TEX_NAME(tex), insert.index, tex->refcount);

		// Increment refcount for refcount-aware calls (e.g. materials)
		if (!ref_interface) {
			tex->refcount++;
		} else if (!tex->ref_interface_visible) {
			tex->ref_interface_visible = true;
			tex->refcount++;
		}

		return insert.index;
	}

	uint picFlags = 0;

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	// set some image flags
	gEngine.Image_SetForceFlags( picFlags );

	rgbdata_t *const pic = gEngine.FS_LoadImage( name, buf, size );
	if( !pic )
		goto cleanup;

	// Process flags, convert to rgba, etc
	tex->flags = flags;
	ProcessImage( tex, pic );

	if( !R_VkTextureUpload( insert.index, tex, &pic, 1, colorspace_hint ))
		goto cleanup;

	// New textures should have refcount = 1 regardless of refcount-aware calls
	if (insert.created) {
		tex->refcount = 1;

		// Mark it as visible from refount-unaware calls if it came from one
		if (ref_interface)
			tex->ref_interface_visible = true;
	}

	success = true;

cleanup:
	if ( !success && insert.created )
		urmomRemoveByIndex(&g_textures.all_desc, insert.index);
	if ( pic )
		gEngine.FS_FreeImage( pic );

	return success ? insert.index : 0;
}

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags ) {
	const qboolean force_update = false;
	const qboolean ref_interface = true;
	return loadTextureInternalFromFile(name, buf, size, flags, kColorspaceGamma, force_update, ref_interface);
}

int R_TextureUploadFromFileExAcquire( const char *filename, colorspace_hint_e colorspace, qboolean force_reload) {
	const qboolean ref_interface = false;
	return loadTextureInternalFromFile( filename, NULL, 0, 0, colorspace, force_reload, ref_interface );
}

// Unconditionally destroy the texture
static void destroyTexture( uint texnum ) {
	ASSERT(texnum > 0); // 0 is *unused, cannot be destroyed
	ASSERT(texnum < COUNTOF(g_textures.all));
	vk_texture_t *const tex = g_textures.all + texnum;

	DEBUG("Destroying texture=%d(%s)", texnum, TEX_NAME(tex));

	if (tex->refcount > 0)
		WARN("Texture '%s'(%d) has refcount=%d", TEX_NAME(tex), texnum, tex->refcount);

	ASSERT(URMOM_IS_OCCUPIED(tex->hdr_));

	// remove from hash table
	urmomRemoveByIndex(&g_textures.all_desc, texnum);

	/*
	// release source
	if( tex->original )
		gEngine.FS_FreeImage( tex->original );
	*/

	R_VkTextureDestroy( texnum, tex );
	tex->refcount = 0;
	tex->ref_interface_visible = false;
	tex->flags = 0;
}

// Decrement refcount and destroy the texture if refcount has reached zero
static void releaseTexture( unsigned int texnum, qboolean ref_interface ) {
	vk_texture_t *tex;
	vk_texture_t **prev;
	vk_texture_t *cur;

	APROF_SCOPE_DECLARE_BEGIN(free, __FUNCTION__);

	if( texnum <= 0 )
		goto end;

	ASSERT(texnum < COUNTOF(g_textures.all));

	tex = g_textures.all + texnum;

	// already freed?
	if( !tex->vk.image.image )
		goto end;

	// debug
	if( !TEX_NAME(tex)[0] )
	{
		ERR("%s: trying to free unnamed texture with index %u", __FUNCTION__, texnum );
		goto end;
	}

	// Textures coming from legacy ref_interface_t api are not refcount-friendly
	// Track them separately with a flags (and a single refcount ++/--)
	if (ref_interface) {
		if (!tex->ref_interface_visible)
			return;
		tex->ref_interface_visible = false;
	}

	DEBUG("Releasing texture=%d(%s) refcount=%d", texnum, TEX_NAME(tex), tex->refcount);
	ASSERT(tex->refcount > 0);
	--tex->refcount;

	if (tex->refcount > 0)
		goto end;

	destroyTexture(texnum);

end:
	APROF_SCOPE_END(free);
}

void R_TextureFree( unsigned int texnum ) {
	const qboolean ref_interface = true;
	releaseTexture( texnum, ref_interface );
}


int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only ) {
	// couldn't loading image
	if( !pic )
		return 0;

	if( !checkTextureName( name ))
		return 0;

	urmom_insert_t insert = {0};
	if (update_only)
		insert.index = urmomFind(&g_textures.all_desc, name);
	else
		insert = urmomInsert(&g_textures.all_desc, name);

	if (insert.index < 0) {
		if (update_only) {
			gEngine.Host_Error( "%s: couldn't find texture %s for update\n", __FUNCTION__, name );
		} else {
			ERR("Cannot allocate texture slot for \"%s\"", name);
		}
		return 0;
	}

	ASSERT(insert.index < COUNTOF(g_textures.all));

	vk_texture_t *const tex = g_textures.all + insert.index;
	// see if already loaded
	if (!insert.created && !update_only)
		return insert.index;

	if( update_only )
		SetBits( tex->flags, flags );
	else
		tex->flags = flags;

	ProcessImage( tex, pic );

	if( !R_VkTextureUpload( insert.index, tex, &pic, 1, kColorspaceGamma ))
	{
		if ( !update_only && insert.created )
			urmomRemoveByIndex(&g_textures.all_desc, insert.index);
		return 0;
	}

	if (insert.created) {
		tex->refcount = 1;

		// Loading from buffer is ref_interface only
		tex->ref_interface_visible = true;
	}

	return insert.index;
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

	if( !checkTextureName( prefix ))
		goto cleanup;

	success = R_VkTexturesSkyboxUpload( prefix, sides, kColorspaceGamma, false );

cleanup:
	for (int j = 0; j < i; ++j)
		gEngine.FS_FreeImage( sides[j] ); // release source texture

	if (success) {
		tglob.fCustomSkybox = true;
		DEBUG( "Skybox done" );
	} else {
		ERR( "Skybox failed" );
		unloadSkybox();
	}

	return success;
}

static const char *skybox_default = "desert";
static const char *skybox_prefixes[] = { "pbr/env/%s", "gfx/env/%s" };

void R_TextureSetupSky( const char *skyboxname ) {
	if( !COM_CheckString( skyboxname ))
	{
		unloadSkybox();
		return; // clear old skybox
	}

	for (int i = 0; i < ARRAYSIZE(skybox_prefixes); ++i) {
		char loadname[MAX_STRING];
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

	// Try default skybox if failed
	if (Q_stricmp(skyboxname, skybox_default) != 0) {
		WARN("missed or incomplete skybox '%s', trying default '%s'", skyboxname, skybox_default);
		R_TextureSetupSky( skybox_default );
	}
}

// FIXME move to r_textures_extra.h

int R_TextureFindByNameF( const char *fmt, ...) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	tex_id = R_TextureFindByName(buffer);
	DEBUG("Looked up texture %s -> %d", buffer, tex_id);
	return tex_id;
}

int R_TextureFindByNameLike( const char *texture_name ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );
	string texname;

	// Try texture name as-is first
	int tex_id = R_TextureFindByNameF("%s", texture_name);

	// Try bsp name
	if (!tex_id)
		tex_id = R_TextureFindByNameF("#%s:%s.mip", map->name, texture_name);

	if (!tex_id) {
		const char *wad = g_map_entities.wadlist;
		for (; *wad;) {
			const char *const wad_end = Q_strchr(wad, ';');
			tex_id = R_TextureFindByNameF("%.*s/%s.mip", wad_end - wad, wad, texture_name);
			if (tex_id)
				break;
			wad = wad_end + 1;
		}
	}

	return tex_id ? tex_id : -1;
}

struct vk_texture_s *R_TextureGetByIndex( uint index )
{
	ASSERT(index >= 0);
	ASSERT(index < MAX_TEXTURES);
	return g_textures.all + index;
}

int R_TexturesGetParm( int parm, int arg ) {
	const vk_texture_t *const tex = R_TextureGetByIndex( arg );
	if (!URMOM_IS_OCCUPIED(tex->hdr_))
		WARN("%s: accessing empty texture %d", __FUNCTION__, arg);

	if (!tex->ref_interface_visible)
		return 0;

	switch(parm){
	case PARM_TEX_WIDTH:
	case PARM_TEX_SRC_WIDTH: // TODO why is this separate?
		return tex->width;
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_HEIGHT:
		return tex->height;
	case PARM_TEX_FLAGS:
		return tex->flags;
	// TODO
	case PARM_TEX_SKYBOX:
	case PARM_TEX_SKYTEXNUM:
	case PARM_TEX_LIGHTMAP:
	case PARM_TEX_TARGET:
	case PARM_TEX_TEXNUM:
	case PARM_TEX_DEPTH:
	case PARM_TEX_GLFORMAT:
	case PARM_TEX_ENCODE:
	case PARM_TEX_MIPCOUNT:
	case PARM_TEX_MEMORY:
		return 0;
	default:
		return 0;
	}
}

void R_TextureAcquire( unsigned int texnum ) {
	ASSERT(texnum > 0);
	vk_texture_t *const tex = R_TextureGetByIndex(texnum);
	ASSERT(URMOM_IS_OCCUPIED(tex->hdr_));
	++tex->refcount;

	DEBUG("Acquiring existing texture %s(%d) refcount=%d", TEX_NAME(tex), texnum, tex->refcount);
}

void R_TextureRelease( unsigned int texnum ) {
	const qboolean ref_interface = false;
	releaseTexture( texnum, ref_interface );
}
