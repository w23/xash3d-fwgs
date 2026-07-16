#include "vk_lightmap.h"
#include "vk_common.h"
#include "r_textures.h"
#include "vk_textures.h"
#include "vk_cvar.h"

#include "com_strings.h"
#include "xash3d_mathlib.h"
#include "protocol.h"

#include <memory.h>

typedef struct
{
	int		allocated[BLOCK_SIZE_MAX];
	int		current_lightmap_texture;
	//msurface_t	*dynamic_surfaces;
	//msurface_t	*lightmap_surfaces[MAX_LIGHTMAPS];
	byte		lightmap_buffer[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*4];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;

xvk_lightmap_state_t g_lightmap;

// TODO this doesn't really need to be this huge
static uint		r_blocklights[BLOCK_SIZE_MAX*BLOCK_SIZE_MAX*3]; // This is just a temp HDR-ish buffer for lightmap generation
static qboolean g_force_full_rebuild = false;
static qboolean g_prev_dlights_active = false;

/*
=================
R_AddDynamicLightsToLightmap

Accumulates active dynamic light contributions for surface into r_blocklights.
R_BuildLightMap already filled r_blocklights with static/lightstyle samples;
this function adds dlight RGB values in-place for each lightmap sample.
=================
*/
static void R_AddDynamicLightsToLightmap( const msurface_t *surface,
	int lightmap_width, int lightmap_height, float lightmap_sample_size )
{
	const mextrasurf_t *const info = surface->info;

	if( !globals.dlights )
		return;

	for( int lnum = 0; lnum < MAX_DLIGHTS; ++lnum )
	{
		const dlight_t *const dl = globals.dlights + lnum;
		vec3_t impact;

		if( !dl || dl->die < gp_cl->time || dl->radius <= 0.0f )
			continue;

		float rad = dl->radius;
		const float dist_plane = PlaneDiff( dl->origin, surface->plane );
		rad -= fabsf( dist_plane );

		float minlight = dl->minlight;
		if( rad < minlight )
			continue;
		minlight = rad - minlight;

		if( surface->plane->type < 3 )
		{
			VectorCopy( dl->origin, impact );
			impact[surface->plane->type] -= dist_plane;
		}
		else
		{
			VectorMA( dl->origin, -dist_plane, surface->plane->normal, impact );
		}

		const float sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		const float tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];

		for( int t = 0; t < lightmap_height; ++t )
		{
			int td = (int)(tl - lightmap_sample_size * t);
			if( td < 0 )
				td = -td;

			for( int s = 0; s < lightmap_width; ++s )
			{
				int sd = (int)(sl - lightmap_sample_size * s);
				if( sd < 0 )
					sd = -sd;

				const float dist = sd > td ? (float)(sd + (td >> 1)) : (float)(td + (sd >> 1));
				if( dist >= minlight )
					continue;

				uint *const bl = &r_blocklights[(s + (t * lightmap_width)) * 3];
				const int add = (int)((rad - dist) * 256.f);
				bl[0] += (add * dl->color.r) / 256;
				bl[1] += (add * dl->color.g) / 256;
				bl[2] += (add * dl->color.b) / 256;
			}
		}
	}
}

static void LM_SetCacheState( msurface_t *surf )
{
	for( int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++ )
		surf->cached_light[maps] = g_lightmap.lightstylevalue[surf->styles[maps]];
}

static qboolean LM_IsSurfaceDirty( const msurface_t *surf )
{
	for( int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++ )
	{
		const int style = surf->styles[maps];
		if( g_lightmap.lightstylevalue[style] != surf->cached_light[maps] )
			return true;
	}

	return false;
}

static void LM_InitBlock( void )
{
	memset( gl_lms.allocated, 0, sizeof( gl_lms.allocated ));
}

static int LM_AllocBlock( int w, int h, int *x, int *y )
{
	int	i, j;
	int	best, best2;

	best = BLOCK_SIZE;

	for( i = 0; i < BLOCK_SIZE - w; i++ )
	{
		best2 = 0;

		for( j = 0; j < w; j++ )
		{
			if( gl_lms.allocated[i+j] >= best )
				break;
			if( gl_lms.allocated[i+j] > best2 )
				best2 = gl_lms.allocated[i+j];
		}

		if( j == w )
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if( best + h > BLOCK_SIZE )
		return false;

	for( i = 0; i < w; i++ )
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

static void LM_UploadBlock( void )
{
	rgbdata_t	r_lightmap;
	char	lmName[16];
	int	i;

	i = gl_lms.current_lightmap_texture;

	memset( &r_lightmap, 0, sizeof( r_lightmap ));
	Q_snprintf( lmName, sizeof( lmName ), "*lightmap%i", i );

	r_lightmap.width = BLOCK_SIZE;
	r_lightmap.height = BLOCK_SIZE;
	r_lightmap.type = PF_RGBA_32;
	r_lightmap.size = r_lightmap.width * r_lightmap.height * 4;
	r_lightmap.flags = IMAGE_HAS_COLOR;
	r_lightmap.buffer = gl_lms.lightmap_buffer;

	tglob.lightmapTextures[i] = R_TextureUploadFromBuffer( lmName, &r_lightmap, TF_ATLAS_PAGE|TF_NOMIPMAP|TF_CLAMP, false );

	if( ++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS )
		gEngine.Host_Error( "Maximum number of lightmap atlases reached (%d)\n", MAX_LIGHTMAPS );
}

/*
=================
R_BuildLightmap

Combine and scale multiple lightmaps into the floating
format in r_blocklights
=================
*/
static void R_BuildLightMap( msurface_t *surf, byte *dest, int stride, qboolean dynamic )
{
	int		smax, tmax;
	uint		*bl;
	int		i, map, size, s, t;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	color24		*lm;
	sample_size = gEngine.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;
	size = smax * tmax;

	lm = surf->samples;

	memset( r_blocklights, 0, sizeof( uint ) * size * 3 );

	// add all the lightmaps
	for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255 && lm; map++ )
	{
		const uint scale = g_lightmap.lightstylevalue[surf->styles[map]];
		for( i = 0, bl = r_blocklights; i < size; i++, bl += 3, lm++ )
		{
			bl[0] += LightToTexGamma( lm->r ) * scale;
			bl[1] += LightToTexGamma( lm->g ) * scale;
			bl[2] += LightToTexGamma( lm->b ) * scale;
		}
	}

	// add all the dynamic lights
	if( dynamic )
		R_AddDynamicLightsToLightmap( surf, smax, tmax, (float)sample_size );

	// Put into texture format
	stride -= (smax << 2);
	bl = r_blocklights;

	for( t = 0; t < tmax; t++, dest += stride )
	{
		for( s = 0; s < smax; s++ )
		{
			dest[0] = Q_min((bl[0] >> 7), 255 );
			dest[1] = Q_min((bl[1] >> 7), 255 );
			dest[2] = Q_min((bl[2] >> 7), 255 );
			dest[3] = 255;

			bl += 3;
			dest += 4;
		}
	}
}

void VK_CreateSurfaceLightmap( msurface_t *surf, const model_t *loadmodel )
{
	int		smax, tmax;
	int		sample_size;
	mextrasurf_t	*info = surf->info;
	byte		*base;

	if( !loadmodel->lightdata )
		return;

	if( FBitSet( surf->flags, SURF_DRAWTILED ))
		return;

	sample_size = gEngine.Mod_SampleSizeForFace( surf );
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
	{
		LM_UploadBlock();
		LM_InitBlock();

		if( !LM_AllocBlock( smax, tmax, &surf->light_s, &surf->light_t ))
			gEngine.Host_Error( "Surface lightmap %dx%d for model \"%s\" does not fit into a %dx%d atlas\n",
				smax, tmax, loadmodel->name, BLOCK_SIZE, BLOCK_SIZE );
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * 4;

	R_BuildLightMap( surf, base, BLOCK_SIZE * 4, false );
	LM_SetCacheState( surf );
}

void VK_UploadLightmap( void )
{
	LM_UploadBlock();
}

void VK_ClearLightmap( void )
{
	for (int i = 0; i < gl_lms.current_lightmap_texture; ++i)
		R_TextureFree(tglob.lightmapTextures[i]);
	gl_lms.current_lightmap_texture = 0;
	g_force_full_rebuild = false;
	g_prev_dlights_active = false;

	LM_InitBlock();
}

static void LM_SurfaceSize( const msurface_t *surf, int *smax, int *tmax )
{
	const int sample_size = gEngine.Mod_SampleSizeForFace( surf );
	const mextrasurf_t *const info = surf->info;

	*smax = ( info->lightextents[0] / sample_size ) + 1;
	*tmax = ( info->lightextents[1] / sample_size ) + 1;
}

static void LM_UploadSurfaceRegion( msurface_t *surf, int atlas_count, qboolean dynamic )
{
	int smax, tmax;
	LM_SurfaceSize( surf, &smax, &tmax );

	const int atlas = surf->lightmaptexturenum;
	if( atlas < 0 || atlas >= atlas_count )
		return;

	if( surf->light_s < 0 || surf->light_t < 0 ||
		surf->light_s + smax > BLOCK_SIZE || surf->light_t + tmax > BLOCK_SIZE )
	{
		gEngine.Host_Error( "%s: invalid lightmap region atlas=%d pos=(%d,%d) size=(%d,%d)\n",
			__FUNCTION__, atlas, surf->light_s, surf->light_t, smax, tmax );
		return;
	}

	const int texnum = tglob.lightmapTextures[atlas];
	if( texnum <= 0 )
		return;

	vk_texture_t *const texture = R_TextureGetByIndex( texnum );
	if( !texture || texture->vk.image.image == VK_NULL_HANDLE )
		return;

	R_BuildLightMap( surf, gl_lms.lightmap_buffer, smax * 4, dynamic );
	R_VkImageUploadRegion( &texture->vk.image, &(r_vk_image_upload_region_t) {
		.layer = 0,
		.mip = 0,
		.x = surf->light_s,
		.y = surf->light_t,
		.width = smax,
		.height = tmax,
		.src_row_stride = smax * 4,
		.data = gl_lms.lightmap_buffer,
	});
	LM_SetCacheState( surf );
}

static void LM_UploadSurfaceRegions( const model_t *world, int atlas_count, qboolean all_surfaces, qboolean dynamic )
{
	for( int i = 0; i < world->numsurfaces; ++i )
	{
		msurface_t *const surf = world->surfaces + i;
		if( FBitSet( surf->flags, SURF_DRAWTILED ) || !surf->samples )
			continue;

		if( !all_surfaces && !LM_IsSurfaceDirty( surf ) )
			continue;

		LM_UploadSurfaceRegion( surf, atlas_count, dynamic );
	}
}

void VK_ForceRebuildLightmaps( void )
{
	// Used when switching RT->raster to prepare a fresh fallback lightmap.
	g_force_full_rebuild = true;
}

/*
=================
LM_HasActiveDlights

Checks whether the map currently has live dynamic lights. This avoids
refreshing every lightmap region when the dlight array is empty or stale.
=================
*/
static qboolean LM_HasActiveDlights( void )
{
	if( !globals.dlights )
		return false;

	for( int i = 0; i < MAX_DLIGHTS; ++i )
	{
		const dlight_t *const dl = globals.dlights + i;
		if( !dl || dl->die < gp_cl->time || dl->radius <= 0.0f )
			continue;
		return true;
	}

	return false;
}

void VK_UpdateLightmapsIfNeeded( void )
{
	const model_t *const world = WORLDMODEL;
	if( !world || !world->lightdata )
		return;

	const int atlas_count = gl_lms.current_lightmap_texture;
	if( atlas_count <= 0 )
		return;

	const qboolean have_active_dlights = LM_HasActiveDlights();
	const qboolean dlight_activity_changed = have_active_dlights || g_prev_dlights_active;
	const qboolean update_all_surfaces = g_force_full_rebuild || dlight_activity_changed;

	g_force_full_rebuild = false;
	g_prev_dlights_active = have_active_dlights;

	LM_UploadSurfaceRegions( world, atlas_count, update_all_surfaces, have_active_dlights );
}

void VK_RunLightStyles( lightstyle_t *styles )
{
	int		i, k, flight, clight;
	float		l, lerpfrac, backlerp;
	float		frametime = (gp_cl->time -   gp_cl->oldtime);
	float		scale;
	lightstyle_t	*ls;
	const model_t *world = WORLDMODEL;

	if( !world ) return;

	scale = r_lighting_modulate->value;

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright

	// TODO
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		ls = styles + i;
		if( !world->lightdata )
		{
			g_lightmap.lightstylevalue[i] = 256 * 256;
			continue;
		}

		if( !gEngine.EngineGetParm( PARAM_GAMEPAUSED, 0 ) && frametime <= 0.1f )
			ls->time += frametime; // evaluate local time

		flight = (int)Q_floor( ls->time * 10 );
		clight = (int)Q_ceil( ls->time * 10 );
		lerpfrac = ( ls->time * 10 ) - flight;
		backlerp = 1.0f - lerpfrac;

		if( !ls->length )
		{
			g_lightmap.lightstylevalue[i] = 256 * scale;
			continue;
		}
		else if( ls->length == 1 )
		{
			// single length style so don't bother interpolating
			g_lightmap.lightstylevalue[i] = ls->map[0] * 22 * scale;
			continue;
		}
		else if( !ls->interp || !CVAR_TO_BOOL( cl_lightstyle_lerping ))
		{
			g_lightmap.lightstylevalue[i] = ls->map[flight%ls->length] * 22 * scale;
			continue;
		}

		// interpolate animating light
		// frame just gone
		k = ls->map[flight % ls->length];
		l = (float)( k * 22.0f ) * backlerp;

		// upcoming frame
		k = ls->map[clight % ls->length];
		l += (float)( k * 22.0f ) * lerpfrac;

		g_lightmap.lightstylevalue[i] = (int)l * scale;
	}
}
