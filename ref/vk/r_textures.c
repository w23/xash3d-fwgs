#include "r_textures.h"
#include "vk_textures.h"

#include "vk_common.h"
#include "vk_const.h"
#include "vk_mapents.h" // wadlist
#include "vk_logs.h"
#include "r_speeds.h"

#include "xash3d_mathlib.h"
#include "crtlib.h"
#include "crclib.h" // COM_HashKey
#include "com_strings.h"
#include "eiface.h" // ARRAYSIZE

#include <memory.h>
#include <math.h>

#define LOG_MODULE LogModule_Textures
#define MODULE_NAME "textures"

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

vk_texture_t vk_textures[MAX_TEXTURES];
vk_texture_t* vk_texturesHashTable[TEXTURES_HASH_SIZE];
uint vk_numTextures;

// FIXME imported from vk_textures.h
qboolean uploadTexture(vk_texture_t *tex, rgbdata_t *const *const layers, int num_layers, qboolean cubemap, colorspace_hint_e colorspace_hint);
void unloadSkybox( void );

qboolean R_TexturesInit( void ) {
	tglob.mempool = Mem_AllocPool( "vktextures" );

	memset( vk_textures, 0, sizeof( vk_textures ));
	memset( vk_texturesHashTable, 0, sizeof( vk_texturesHashTable ));
	vk_numTextures = 0;

	// create unused 0-entry
	Q_strncpy( vk_textures->name, "*unused*", sizeof( vk_textures->name ));
	vk_textures->hashValue = COM_HashKey( vk_textures->name, TEXTURES_HASH_SIZE );
	vk_textures->nextHash = vk_texturesHashTable[vk_textures->hashValue];
	vk_texturesHashTable[vk_textures->hashValue] = vk_textures;
	vk_numTextures = 1;

	return R_VkTexturesInit();
}

void R_TexturesShutdown( void )
{
	R_VkTexturesShutdown();

	//memset( tglob.lightmapTextures, 0, sizeof( tglob.lightmapTextures ));
	memset( vk_texturesHashTable, 0, sizeof( vk_texturesHashTable ));
	memset( vk_textures, 0, sizeof( vk_textures ));
	vk_numTextures = 0;
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

	// FIXME this is not strictly correct. Refcount management should be done differently wrt public ref_interface_t
	tex->refcount = 1;
	return tex;
}

/* FIXME static */ qboolean Common_CheckTexName( const char *name )
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

/* FIXME static*/ rgbdata_t *Common_FakeImage( int width, int height, int depth, int flags )
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

int CalcMipmapCount( vk_texture_t *tex, qboolean haveBuffer )
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

		if (layers[0]->width != layers[i]->width || layers[0]->height != layers[i]->height) {
			ERR("Texture %s layer %d has resolution %dx%d inconsistent with layer 0 resolution %dx%d",
				name, i, layers[i]->width, layers[i]->height, layers[0]->width, layers[0]->height);
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

	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = Common_TextureForName( name )))
		return (tex - vk_textures);

	return 0;
}

const char* R_TextureGetNameByIndex( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return vk_textures[texnum].name;
}

/* FIXME static */ int loadTextureInternal( const char *name, const byte *buf, size_t size, int flags, colorspace_hint_e colorspace_hint, qboolean force_update ) {
	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	vk_texture_t * tex = Common_TextureForName( name );
	if( tex && !force_update ) {
		DEBUG("Found existing texture %s(%d) refcount=%d", tex->name, (int)(tex-vk_textures), tex->refcount);
		return (tex - vk_textures);
	}

	uint picFlags = 0;

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );

	// set some image flags
	gEngine.Image_SetForceFlags( picFlags );

	rgbdata_t *const pic = gEngine.FS_LoadImage( name, buf, size );
	if( !pic ) return 0; // couldn't loading image

	// allocate the new one if needed
	if (!tex)
		tex = Common_AllocTexture( name, flags );
	else
		tex->flags = flags;

	// upload texture
	VK_ProcessImage( tex, pic );

	if( !uploadTexture( tex, &pic, 1, false, colorspace_hint ))
	{
		// FIXME remove from hash table
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

int R_TextureUploadFromFile( const char *name, const byte *buf, size_t size, int flags ) {
	const qboolean force_update = false;
	return loadTextureInternal(name, buf, size, flags, kColorspaceGamma, force_update);
}

void R_TextureFree( unsigned int texnum ) {
	// FIXME this is incorrect and leads to missing textures
	R_TextureRelease( texnum );
}

static int loadTextureFromBuffers( const char *name, rgbdata_t *const *const pic, int pic_count, texFlags_t flags, qboolean update_only ) {
	vk_texture_t	*tex;

	if( !Common_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = Common_TextureForName( name )) && !update_only )
		return (tex - vk_textures);

	// couldn't loading image
	if( !pic ) return 0;

	if( update_only )
	{
		if( tex == NULL )
			gEngine.Host_Error( "loadTextureFromBuffer: couldn't find texture %s for update\n", name );
		SetBits( tex->flags, flags );
	}
	else
	{
		// allocate the new one
		ASSERT(!tex);
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

int R_TextureUploadFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update_only ) {
	return loadTextureFromBuffers(name, &pic, 1, flags, update_only);
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

void R_TextureSetupSky( const char *skyboxname ) {
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
		// FIXME infinite recursion
		R_TextureSetupSky( "desert" ); // force to default
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
	//DEBUG("Looked up texture %s -> %d", buffer, tex_id);
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

int R_TextureCreateDummy_FIXME( const char *name ) {
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

	return R_TextureUploadFromBufferNew(name, pic, TF_NOMIPMAP);
}
