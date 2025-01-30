#include "vk_sprite.h"
#include "r_textures.h"
#include "camera.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_scene.h"
#include "r_speeds.h"
#include "vk_math.h"
#include "vk_logs.h"

#include "sprite.h"
#include "xash3d_mathlib.h"
#include "com_strings.h"
#include "pmtrace.h"
#include "pm_defs.h"

#include <memory.h>

#define MODULE_NAME "sprite"
#define LOG_MODULE sprite

// it's a Valve default value for LoadMapSprite (probably must be power of two)
#define MAPSPRITE_SIZE	128
#define GLARE_FALLOFF	19000.0f

static struct {
	struct {
		int sprites;
	} stats;

	struct {
		r_geometry_range_t geom;
		vk_render_geometry_t geometry;
		vk_render_model_t model;
	} quad;
} g_sprite;

static qboolean createQuadModel(void) {
	g_sprite.quad.geom = R_GeometryRangeAlloc(4, 6);
	if (g_sprite.quad.geom.block_handle.size == 0) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate geometry for sprite quad\n");
		return false;
	}

	const r_geometry_range_lock_t lock = R_GeometryRangeLock(&g_sprite.quad.geom);

	vec3_t point;
	vk_vertex_t *dst_vtx;
	uint16_t *dst_idx;

	dst_vtx = lock.vertices;
	dst_idx = lock.indices;

	const vec3_t org = {0, 0, 0};
	const vec3_t v_right = {1, 0, 0};
	const vec3_t v_up = {0, 1, 0};
	vec3_t v_normal;
	CrossProduct(v_right, v_up, v_normal);

	VectorMA( org, -1.f, v_up, point );
	VectorMA( point, -1.f, v_right, dst_vtx[0].pos );
	dst_vtx[0].gl_tc[0] = 0.f;
	dst_vtx[0].gl_tc[1] = 1.f;
	dst_vtx[0].lm_tc[0] = dst_vtx[0].lm_tc[1] = 0.f;
	Vector4Set(dst_vtx[0].color, 255, 255, 255, 255);
	VectorCopy(v_normal, dst_vtx[0].normal);

	VectorMA( org, 1.f, v_up, point );
	VectorMA( point, -1.f, v_right, dst_vtx[1].pos );
	dst_vtx[1].gl_tc[0] = 0.f;
	dst_vtx[1].gl_tc[1] = 0.f;
	dst_vtx[1].lm_tc[0] = dst_vtx[1].lm_tc[1] = 0.f;
	Vector4Set(dst_vtx[1].color, 255, 255, 255, 255);
	VectorCopy(v_normal, dst_vtx[1].normal);

	VectorMA( org, 1.f, v_up, point );
	VectorMA( point, 1.f, v_right, dst_vtx[2].pos );
	dst_vtx[2].gl_tc[0] = 1.f;
	dst_vtx[2].gl_tc[1] = 0.f;
	dst_vtx[2].lm_tc[0] = dst_vtx[2].lm_tc[1] = 0.f;
	Vector4Set(dst_vtx[2].color, 255, 255, 255, 255);
	VectorCopy(v_normal, dst_vtx[2].normal);

	VectorMA( org, -1.f, v_up, point );
	VectorMA( point, 1.f, v_right, dst_vtx[3].pos );
	dst_vtx[3].gl_tc[0] = 1.f;
	dst_vtx[3].gl_tc[1] = 1.f;
	dst_vtx[3].lm_tc[0] = dst_vtx[3].lm_tc[1] = 0.f;
	Vector4Set(dst_vtx[3].color, 255, 255, 255, 255);
	VectorCopy(v_normal, dst_vtx[3].normal);

	dst_idx[0] = 0;
	dst_idx[1] = 1;
	dst_idx[2] = 2;
	dst_idx[3] = 0;
	dst_idx[4] = 2;
	dst_idx[5] = 3;

	R_GeometryRangeUnlock( &lock );

	g_sprite.quad.geometry = (vk_render_geometry_t){
		.max_vertex = 4,
		.vertex_offset = g_sprite.quad.geom.vertices.unit_offset,

		.element_count = 6,
		.index_offset = g_sprite.quad.geom.indices.unit_offset,

		.material = R_VkMaterialGetForTexture(tglob.defaultTexture),
		.ye_olde_texture = tglob.defaultTexture,
		.emissive = {1,1,1},
	};

	return R_RenderModelCreate(&g_sprite.quad.model, (vk_render_model_init_t){
		.name = "sprite",
		.geometries = &g_sprite.quad.geometry,
		.geometries_count = 1,
		.dynamic = false,
		});
}

static void destroyQuadModel(void) {
	if (g_sprite.quad.model.num_geometries)
		R_RenderModelDestroy(&g_sprite.quad.model);

	if (g_sprite.quad.geom.block_handle.size)
		R_GeometryRangeFree(&g_sprite.quad.geom);

	g_sprite.quad.model.num_geometries = 0;
	g_sprite.quad.geom.block_handle.size = 0;
}

qboolean R_SpriteInit(void) {
	R_SPEEDS_COUNTER(g_sprite.stats.sprites, "count", kSpeedsMetricCount);

	return true;
	// TODO return createQuadModel();
}

void R_SpriteShutdown(void) {
	destroyQuadModel();
}

void R_SpriteNewMapFIXME(void) {
	destroyQuadModel();
	ASSERT(createQuadModel());
}

static mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe = NULL;
	float		*pintervals, fullinterval;
	int		i, numframes;
	float		targettime;

	ASSERT( pModel != NULL );
	psprite = pModel->cache.data;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		if( frame > psprite->numframes )
			gEngine.Con_Printf( S_WARN "R_GetSpriteFrame: no such frame %d (%s)\n", frame, pModel->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == SPR_SINGLE )
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == SPR_GROUP )
	{
		pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[frame].frameptr);
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = gp_cl->time - ((int)( gp_cl->time / fullinterval )) * fullinterval;

		for( i = 0; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
		}
		pspriteframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == SPR_ANGLED )
	{
		//int	angleframe = (int)(Q_rint(( g_camera.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;
		const int	angleframe = (int)(Q_rint(( 0 - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		gEngine.Con_Printf(S_WARN "VK FIXME: %s doesn't know about viewangles\n", __FUNCTION__);

		// e.g. doom-style sprite monsters
		pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[frame].frameptr);
		pspriteframe = pspritegroup->frames[angleframe];
	}

	return pspriteframe;
}

void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	mspriteframe_t	*pFrame;

	if( !pSprite || pSprite->type != mod_sprite ) return; // bad model ?
	pFrame = R_GetSpriteFrame( pSprite, currentFrame, 0.0f );

	if( frameWidth ) *frameWidth = pFrame->width;
	if( frameHeight ) *frameHeight = pFrame->height;
	if( numFrames ) *numFrames = pSprite->numframes;
}

typedef struct {
	char sprite_name[MAX_QPATH];
	char group_suffix[8];
	uint r_texFlags;
	int sprite_version;
	float sprite_radius;
} SpriteLoadContext;

static const dframetype_t *VK_SpriteLoadFrame( model_t *mod, const void *pin, mspriteframe_t **ppframe, int num, const SpriteLoadContext *ctx )
{
	dspriteframe_t	pinframe;
	mspriteframe_t	*pspriteframe;
	int		gl_texturenum = 0;
	char		texname[128];
	int		bytes = 1;

	memcpy( &pinframe, pin, sizeof(dspriteframe_t));

	if( ctx->sprite_version == SPRITE_VERSION_32 )
		bytes = 4;

	// build uinque frame name
	if( FBitSet( mod->flags, MODEL_CLIENT )) // it's a HUD sprite
	{
		Q_snprintf( texname, sizeof( texname ), "#HUD/%s(%s:%i%i).spr", ctx->sprite_name, ctx->group_suffix, num / 10, num % 10 );
		gl_texturenum = R_TextureUploadFromFile( texname, pin, pinframe.width * pinframe.height * bytes, ctx->r_texFlags );
	}
	else
	{
		Q_snprintf( texname, sizeof( texname ), "#%s(%s:%i%i).spr", ctx->sprite_name, ctx->group_suffix, num / 10, num % 10 );
		gl_texturenum = R_TextureUploadFromFile( texname, pin, pinframe.width * pinframe.height * bytes, ctx->r_texFlags );
	}

	// setup frame description
	pspriteframe = Mem_Malloc( mod->mempool, sizeof( mspriteframe_t ));
	pspriteframe->width = pinframe.width;
	pspriteframe->height = pinframe.height;
	pspriteframe->up = pinframe.origin[1];
	pspriteframe->left = pinframe.origin[0];
	pspriteframe->down = pinframe.origin[1] - pinframe.height;
	pspriteframe->right = pinframe.width + pinframe.origin[0];
	pspriteframe->gl_texturenum = gl_texturenum;
	*ppframe = pspriteframe;

	return PTR_CAST(const dframetype_t, ( const byte* )pin + sizeof( dspriteframe_t ) + pinframe.width * pinframe.height * bytes );
}

static const dframetype_t *VK_SpriteLoadGroup( model_t *mod, const void *pin, mspriteframe_t **ppframe, int framenum, const SpriteLoadContext *ctx )
{
	const dspritegroup_t	*pingroup;
	mspritegroup_t	*pspritegroup;
	const dspriteinterval_t	*pin_intervals;
	float		*poutintervals;
	int		i, groupsize, numframes;
	const void		*ptemp;

	pingroup = (const dspritegroup_t *)pin;
	numframes = pingroup->numframes;

	groupsize = sizeof( mspritegroup_t ) + (numframes - 1) * sizeof( pspritegroup->frames[0] );
	pspritegroup = Mem_Calloc( mod->mempool, groupsize );
	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;
	pin_intervals = (const dspriteinterval_t *)(pingroup + 1);
	poutintervals = Mem_Calloc( mod->mempool, numframes * sizeof( float ));
	pspritegroup->intervals = poutintervals;

	for( i = 0; i < numframes; i++ )
	{
		*poutintervals = pin_intervals->interval;
		if( *poutintervals <= 0.0f )
			*poutintervals = 1.0f; // set error value
		poutintervals++;
		pin_intervals++;
	}

	ptemp = (const void *)pin_intervals;
	for( i = 0; i < numframes; i++ )
	{
		ptemp = VK_SpriteLoadFrame( mod, ptemp, &pspritegroup->frames[i], framenum * 10 + i, ctx );
	}

	return (const dframetype_t *)ptemp;
}

void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags )
{
	const dsprite_t		*pin;
	const short		*numi = NULL;
	const dframetype_t	*pframetype;
	msprite_t		*psprite;
	int		i;
	SpriteLoadContext ctx = {0};

	pin = buffer;
	psprite = mod->cache.data;

	if( pin->version == SPRITE_VERSION_Q1 || pin->version == SPRITE_VERSION_32 )
		numi = NULL;
	else if( pin->version == SPRITE_VERSION_HL )
		numi = (const short *)(void *)((const byte*)buffer + sizeof( dsprite_hl_t ));

	ctx.r_texFlags = texFlags;
	ctx.sprite_version = pin->version;
	Q_strncpy( ctx.sprite_name, mod->name, sizeof( ctx.sprite_name ));
	COM_StripExtension( ctx.sprite_name );

	if( numi == NULL )
	{
		rgbdata_t	*pal;

		pal = gEngine.FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = (const dframetype_t *)(void *)((const byte*)buffer + sizeof( dsprite_q1_t )); // pinq1 + 1
		gEngine.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else if( *numi == 256 )
	{
		const byte	*src = (const byte *)(numi+1);
		rgbdata_t	*pal;

		// install palette
		switch( psprite->texFormat )
		{
		case SPR_INDEXALPHA:
			pal = gEngine.FS_LoadImage( "#gradient.pal", src, 768 );
			break;
		case SPR_ALPHTEST:
			pal = gEngine.FS_LoadImage( "#masked.pal", src, 768 );
			break;
		default:
			pal = gEngine.FS_LoadImage( "#normal.pal", src, 768 );
			break;
		}

		pframetype = (const dframetype_t *)(void *)(src + 768);
		gEngine.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else
	{
		gEngine.Con_DPrintf( S_ERROR "%s has wrong number of palette colors %i (should be 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
		return;

	for( i = 0; i < mod->numframes; i++ )
	{
		frametype_t frametype = pframetype->type;
		psprite->frames[i].type = (spriteframetype_t)frametype;

		switch( frametype )
		{
		case FRAME_SINGLE:
			Q_strncpy( ctx.group_suffix, "frame", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadFrame( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		case FRAME_GROUP:
			Q_strncpy( ctx.group_suffix, "group", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		case FRAME_ANGLED:
			Q_strncpy( ctx.group_suffix, "angle", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		}
		if( pframetype == NULL ) break; // technically an error
	}

	if( loaded ) *loaded = true;	// done
}

int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	if( !m_pSpriteModel || m_pSpriteModel->type != mod_sprite || !m_pSpriteModel->cache.data )
		return 0;

	return R_GetSpriteFrame( m_pSpriteModel, frame, 0.0f )->gl_texturenum;
}

/*
================
R_GetSpriteFrameInterpolant

NOTE: we using prevblending[0] and [1] for holds interval
between frames where are we lerping
================
*/
static float R_GetSpriteFrameInterpolant( cl_entity_t *ent, mspriteframe_t **oldframe, mspriteframe_t **curframe )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	int		i, j, numframes, frame;
	float		lerpFrac, time, jtime, jinterval;
	float		*pintervals, fullinterval, targettime;
	int		m_fDoInterp;

	psprite = ent->model->cache.data;
	frame = (int)ent->curstate.frame;
	lerpFrac = 1.0f;

	// misc info
	m_fDoInterp = (ent->curstate.effects & EF_NOINTERP) ? false : true;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		gEngine.Con_Reportf( S_WARN "R_GetSpriteFrameInterpolant: no such frame %d (%s)\n", frame, ent->model->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == FRAME_SINGLE )
	{
		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_SINGLE )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * 11.0f;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		if( ent->latched.prevblending[0] >= psprite->numframes )
		{
			// reset interpolation on change model
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			ent->latched.sequencetime = gp_cl->time;
			lerpFrac = 0.0f;
		}

		// get the interpolated frames
		if( oldframe ) *oldframe = psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( curframe ) *curframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == SPR_GROUP )
	{
		pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[frame].frameptr);
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];
		jinterval = pintervals[1] - pintervals[0];
		time = gp_cl->time;
		jtime = 0.0f;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		// LordHavoc: since I can't measure the time properly when it loops from numframes - 1 to 0,
		// i instead measure the time of the first frame, hoping it is consistent
		for( i = 0, j = numframes - 1; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
			j = i;
			jinterval = pintervals[i] - jtime;
			jtime = pintervals[i];
		}

		if( m_fDoInterp )
			lerpFrac = (targettime - jtime) / jinterval;
		else j = i; // no lerping

		// get the interpolated frames
		if( oldframe ) *oldframe = pspritegroup->frames[j];
		if( curframe ) *curframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == SPR_ANGLED )
	{
		// e.g. doom-style sprite monsters
		float	yaw = ent->angles[YAW];
		int	angleframe = (int)(Q_rint(( g_camera.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != SPR_ANGLED )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * ent->curstate.framerate;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[ent->latched.prevblending[0]].frameptr);
		if( oldframe ) *oldframe = pspritegroup->frames[angleframe];

		pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[frame].frameptr);
		if( curframe ) *curframe = pspritegroup->frames[angleframe];
	}

	return lerpFrac;
}

/* FIXME VK
// Cull sprite model by bbox
qboolean R_CullSpriteModel( cl_entity_t *e, vec3_t origin )
{
	vec3_t	sprite_mins, sprite_maxs;
	float	scale = 1.0f;

	if( !e->model->cache.data )
		return true;

	if( e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// scale original bbox (no rotation for sprites)
	VectorScale( e->model->mins, scale, sprite_mins );
	VectorScale( e->model->maxs, scale, sprite_maxs );

	sprite_radius = RadiusFromBounds( sprite_mins, sprite_maxs );

	VectorAdd( sprite_mins, origin, sprite_mins );
	VectorAdd( sprite_maxs, origin, sprite_maxs );

	return R_CullModel( e, sprite_mins, sprite_maxs );
}
*/

// Set sprite brightness factor
static float R_SpriteGlowBlend( vec3_t origin, int rendermode, int renderfx, float *pscale )
{
	float	dist, brightness;
	vec3_t	glowDist;
	pmtrace_t	*tr;

	VectorSubtract( origin, g_camera.vieworg, glowDist );
	dist = VectorLength( glowDist );

	// FIXME VK if( RP_NORMALPASS( ))
	{
		tr = gEngine.EV_VisTraceLine( g_camera.vieworg, origin,
				// FIXME VK r_traceglow->value ? PM_GLASS_IGNORE :
				(PM_GLASS_IGNORE|PM_STUDIO_IGNORE));

		if(( 1.0f - tr->fraction ) * dist > 8.0f )
			return 0.0f;
	}

	if( renderfx == kRenderFxNoDissipation )
		return 1.0f;

	brightness = GLARE_FALLOFF / ( dist * dist );
	brightness = bound( 0.05f, brightness, 1.0f );
	*pscale *= dist * ( 1.0f / 200.0f );

	return brightness;
}

// Do occlusion test for glow-sprites
static qboolean spriteIsOccluded( const cl_entity_t *e, vec3_t origin, float *pscale, float *blend )
{
	if( e->curstate.rendermode == kRenderGlow )
	{
		vec3_t	v;

		TriWorldToScreen( origin, v );

		if( v[0] < g_camera.viewport[0] || v[0] > g_camera.viewport[0] + g_camera.viewport[2] )
			return true; // do scissor
		if( v[1] < g_camera.viewport[1] || v[1] > g_camera.viewport[1] + g_camera.viewport[3] )
			return true; // do scissor

		*blend *= R_SpriteGlowBlend( origin, e->curstate.rendermode, e->curstate.renderfx, pscale );

		if( *blend <= 0.01f )
			return true; // faded
	}
	else
	{
		// FIXME VK if( R_CullSpriteModel( e, origin )) return true;
		return false;
	}

	return false;
}

static vk_render_type_e spriteRenderModeToRenderType( int render_mode ) {
	switch (render_mode) {
		case kRenderNormal:       return kVkRenderTypeSolid;
		case kRenderTransColor:   return kVkRenderType_A_1mA_RW;
		case kRenderTransTexture: return kVkRenderType_A_1mA_RW;
		case kRenderGlow:         return kVkRenderType_A_1;
		case kRenderTransAlpha:   return kVkRenderType_A_1mA_R;
		case kRenderTransAdd:     return kVkRenderType_A_1_R;
		default: ASSERT(!"Unxpected render_mode");
	}

	return kVkRenderTypeSolid;
}

static void R_DrawSpriteQuad( const char *debug_name, const mspriteframe_t *frame, const vec3_t org, const vec3_t v_right, const vec3_t v_up, float scale, int texture, int render_mode, const vec4_t color ) {
	vec3_t v_normal;
	CrossProduct(v_right, v_up, v_normal);

	// TODO can frame->right/left and frame->up/down be asymmetric?
	vec3_t right, up;
	VectorScale(v_right, frame->right * scale, right);
	VectorScale(v_up, frame->up * scale, up);

	matrix4x4 transform;
	Matrix4x4_CreateFromVectors(transform, right, up, v_normal, org);

	const vk_render_type_e render_type = spriteRenderModeToRenderType(render_mode);
	const r_vk_material_t material_override = R_VkMaterialGetForTexture(texture);
	const material_mode_e material_mode = R_VkMaterialModeFromRenderType(render_type);

	R_RenderModelDraw(&g_sprite.quad.model, (r_model_draw_t){
		.render_type = render_type,
		.material_mode = material_mode,
		.material_flags = kMaterialFlag_None,
		.color = (const vec4_t*)color,
		.transform = &transform,
		.prev_transform = &transform,
		.override = {
			.material = &material_override,
			.old_texture = texture,
		},
	});
}

#if 0
static qboolean R_SpriteHasLightmap( cl_entity_t *e, int texFormat )
{
	/* FIXME VK
	if( !r_sprite_lighting->value )
		return false;
	*/

	if( texFormat != SPR_ALPHTEST )
		return false;

	if( e->curstate.effects & EF_FULLBRIGHT )
		return false;

	if( e->curstate.renderamt <= 127 )
		return false;

	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
	case kRenderTransAlpha:
	case kRenderTransTexture:
		break;
	default:
		return false;
	}

	return true;
}
#endif

static qboolean R_SpriteAllowLerping( const cl_entity_t *e, msprite_t *psprite )
{
	/* FIXME VK
	if( !r_sprite_lerping->value )
		return false;
	*/

	if( psprite->numframes <= 1 )
		return false;

	if( psprite->texFormat != SPR_ADDITIVE )
		return false;

	if( e->curstate.rendermode == kRenderNormal || e->curstate.rendermode == kRenderTransAlpha )
		return false;

	return true;
}

void R_VkSpriteDrawModel( cl_entity_t *e, float blend )
{
	mspriteframe_t	*frame = NULL, *oldframe = NULL;
	msprite_t		*psprite;
	model_t		*model;
	int		i, type;
	float		angle, dot, sr, cr;
	float		lerp = 1.0f, ilerp, scale;
	vec3_t		v_forward, v_right, v_up;
	vec3_t		origin, color;

	/* FIXME VK
	if( RI.params & RP_ENVVIEW )
		return;
	*/

	model = e->model;
	psprite = (msprite_t * )model->cache.data;
	VectorCopy( e->origin, origin );	// set render origin

	// do movewith
	if( e->curstate.aiment > 0 && e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t	*parent;

		parent = globals.entities + e->curstate.aiment;

		if( parent && parent->model )
		{
			if( parent->model->type == mod_studio && e->curstate.body > 0 )
			{
				int num = bound( 1, e->curstate.body, MAXSTUDIOATTACHMENTS );
				VectorCopy( parent->attachment[num-1], origin );
			}
			else VectorCopy( parent->origin, origin );
		}
	}

	scale = e->curstate.scale;
	if( !scale ) scale = 1.0f;

	if( spriteIsOccluded( e, origin, &scale, &blend))
		return; // sprite culled

	g_sprite.stats.sprites++;

	/* FIXME VK
	r_stats.c_sprite_models_drawn++;

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( false );
	*/

	/* FIXME VK compare with pipeline state
	// select properly rendermode
	switch( e->curstate.rendermode )
	{
	case kRenderTransAlpha:
		pglDepthMask( GL_FALSE ); // <-- FIXME this is different. GL render doesn't write depth, VK one does, as it expects it to be solid-like
		// fallthrough
	case kRenderTransColor:
	case kRenderTransTexture:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case kRenderGlow:
		pglDisable( GL_DEPTH_TEST );
		// fallthrough
	case kRenderTransAdd:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		pglDepthMask( GL_FALSE );
		break;
	case kRenderNormal:
	default:
		pglDisable( GL_BLEND );
		break;
	}

	// all sprites can have color
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglEnable( GL_ALPHA_TEST );
	*/

	// NOTE: never pass sprites with rendercolor '0 0 0' it's a stupid Valve Hammer Editor bug
	if( e->curstate.rendercolor.r || e->curstate.rendercolor.g || e->curstate.rendercolor.b )
	{
		color[0] = (float)e->curstate.rendercolor.r * ( 1.0f / 255.0f );
		color[1] = (float)e->curstate.rendercolor.g * ( 1.0f / 255.0f );
		color[2] = (float)e->curstate.rendercolor.b * ( 1.0f / 255.0f );
	}
	else
	{
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
	}

	/* FIXME VK
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		colorVec lightColor = R_LightPoint( origin );
		// FIXME: collect light from dlights?
		color2[0] = (float)lightColor.r * ( 1.0f / 255.0f );
		color2[1] = (float)lightColor.g * ( 1.0f / 255.0f );
		color2[2] = (float)lightColor.b * ( 1.0f / 255.0f );
		// NOTE: sprites with 'lightmap' looks ugly when alpha func is GL_GREATER 0.0
		pglAlphaFunc( GL_GREATER, 0.5f );
	}
	*/

	if( R_SpriteAllowLerping( e, psprite ))
		lerp = R_GetSpriteFrameInterpolant( e, &oldframe, &frame );
	else
		frame = oldframe = R_GetSpriteFrame( model, e->curstate.frame, e->angles[YAW] );

	type = psprite->type;

	// automatically roll parallel sprites if requested
	if( e->angles[ROLL] != 0.0f && type == SPR_FWD_PARALLEL )
		type = SPR_FWD_PARALLEL_ORIENTED;

	switch( type )
	{
	case SPR_ORIENTED:
		AngleVectors( e->angles, v_forward, v_right, v_up );
		VectorScale( v_forward, 0.01f, v_forward );	// to avoid z-fighting
		VectorSubtract( origin, v_forward, origin );
		break;
	case SPR_FACING_UPRIGHT:
		VectorSet( v_right, origin[1] - g_camera.vieworg[1], -(origin[0] - g_camera.vieworg[0]), 0.0f );
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_UPRIGHT:
		dot = g_camera.vforward[2];
		if(( dot > 0.999848f ) || ( dot < -0.999848f ))	// cos(1 degree) = 0.999848
			return; // invisible
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorSet( v_right, g_camera.vforward[1], -g_camera.vforward[0], 0.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_ORIENTED:
		angle = e->angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );
		for( i = 0; i < 3; i++ )
		{
			v_right[i] = (g_camera.vright[i] * cr + g_camera.vup[i] * sr);
			v_up[i] = g_camera.vright[i] * -sr + g_camera.vup[i] * cr;
		}
		break;
	case SPR_FWD_PARALLEL: // normal sprite
	default:
		VectorCopy( g_camera.vright, v_right );
		VectorCopy( g_camera.vup, v_up );
		break;
	}

	/* FIXME VK
	if( psprite->facecull == SPR_CULL_NONE )
		GL_Cull( GL_NONE );
	*/

	if( oldframe == frame )
	{
		// draw the single non-lerped frame
		const vec4_t color4 = {color[0], color[1], color[2], blend};
		R_DrawSpriteQuad( model->name, frame, origin, v_right, v_up, scale, frame->gl_texturenum, e->curstate.rendermode, color4 );
	}
	else
	{
		// draw two combined lerped frames
		lerp = bound( 0.0f, lerp, 1.0f );
		ilerp = 1.0f - lerp;

		if( ilerp != 0.0f )
		{
			const vec4_t color4 = {color[0], color[1], color[2], blend * ilerp};
			ASSERT(oldframe);
			R_DrawSpriteQuad( model->name, oldframe, origin, v_right, v_up, scale, oldframe->gl_texturenum, e->curstate.rendermode, color4 );
		}

		if( lerp != 0.0f )
		{
			const vec4_t color4 = {color[0], color[1], color[2], blend * lerp};
			ASSERT(frame);
			R_DrawSpriteQuad( model->name, frame, origin, v_right, v_up, scale, frame->gl_texturenum, e->curstate.rendermode, color4 );
		}
	}

	/* FIXME VK
	// draw the sprite 'lightmap' :-)
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		if( !r_lightmap->value )
			pglEnable( GL_BLEND );
		else pglDisable( GL_BLEND );
		pglDepthFunc( GL_EQUAL );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		pglColor4f( color2[0], color2[1], color2[2], tr.blend );
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale, ubo_index, frame->gl_texturenum, e->curstate.rendermode  );
		pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
		pglDepthFunc( GL_LEQUAL );
	}
	*/
}

void Mod_SpriteUnloadTextures( void *data )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int		i, j;

	psprite = data;

	if( psprite )
	{
		// release all textures
		for( i = 0; i < psprite->numframes; i++ )
		{
			if( psprite->frames[i].type == SPR_SINGLE )
			{
				pspriteframe = psprite->frames[i].frameptr;
				R_TextureFree( pspriteframe->gl_texturenum );
			}
			else
			{
				pspritegroup = PTR_CAST(mspritegroup_t, psprite->frames[i].frameptr);

				for( j = 0; j < pspritegroup->numframes; j++ )
				{
					pspriteframe = pspritegroup->frames[i];
					R_TextureFree( pspriteframe->gl_texturenum );
				}
			}
		}
	}
}
