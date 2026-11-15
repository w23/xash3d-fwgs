#include "vk_fog.h"
#include "vk_common.h"
#include "vk_cvar.h"
#include "vk_renderstate.h"
#include "vk_textures.h"

#include "ref_params.h"
#include "pmove.h"

vk_fog_t g_fog;

// Adapted from ref/gl/gl_rmain.c and gl_triapi.c, without GL state calls.
/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static vk_texture_t *R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	vk_texture_t *tex = NULL;

	// assure the initial node is not null
	// we could check it here, but we would rather check it
	// outside the call to get rid of one additional recursion level
	ASSERT( node != NULL );

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return NULL;

	if( node->contents < 0 )
	{
		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			 return NULL;

		// find texture
		mleaf_t *pleaf = (mleaf_t *)node;
		msurface_t **mark = pleaf->firstmarksurface;
		int c = pleaf->nummarksurfaces;

		for( int i = 0; i < c; i++, mark++ )
		{
			if( (*mark)->flags & SURF_DRAWTURB && (*mark)->texinfo && (*mark)->texinfo->texture )
				return R_TextureGetByIndex( (*mark)->texinfo->texture->gl_texturenum );
		}

		// texture not found
		return NULL;
	}

	// this is a regular node
	// traverse children
	mnode_t *child = node_child( node, 0, WORLDMODEL );

	if( child && ( child != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( child, node, true );
		if( tex ) return tex;
	}

	child = node_child( node, 1, WORLDMODEL );

	if( child && ( child != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( child, node, true );
		if( tex ) return tex;
	}

	// for down recursion, return immediately
	if( down ) return NULL;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return NULL;
}

/*
=============
R_CheckFog

check for underwater fog
Using backward recursion to find waterline leaf
from underwater leaf (idea: XaeroX)
=============
*/
void R_CheckFog( const ref_viewpass_t *rvp )
{
	const mleaf_t *viewleaf = FBitSet( rvp->flags, RF_DRAW_WORLD ) && WORLDMODEL
		? gEngine.Mod_PointInLeaf( rvp->vieworigin, WORLDMODEL->nodes, WORLDMODEL ) : NULL;

	// quake global fog
	if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
	{
		if( !MOVEVARS->fog_settings )
		{
			g_fog.fogEnabled = false;
			return;
		}

		// quake-style global fog
		g_fog.fogColor[0] = ((MOVEVARS->fog_settings & 0xFF000000) >> 24) / 255.0f;
		g_fog.fogColor[1] = ((MOVEVARS->fog_settings & 0xFF0000) >> 16) / 255.0f;
		g_fog.fogColor[2] = ((MOVEVARS->fog_settings & 0xFF00) >> 8) / 255.0f;
		g_fog.fogDensity = ((MOVEVARS->fog_settings & 0xFF) / 255.0f) * 0.01f;
		g_fog.fogStart = g_fog.fogEnd = 0.0f;
		g_fog.fogColor[3] = 1.0f;
		g_fog.fogCustom = false;
		g_fog.fogEnabled = true;
		g_fog.fogSkybox = true;
		return;
	}

	g_fog.fogEnabled = false;

	if( FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW ) || ENGINE_GET_PARM( PARM_WATER_LEVEL ) < 3 || !FBitSet( rvp->flags, RF_DRAW_WORLD ) || !viewleaf )
	{
		if( g_fog.cached_waterlevel == 3 )
		{
			// in some cases waterlevel jumps from 3 to 1. Catch it
			g_fog.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );
			g_fog.cached_contents = CONTENTS_EMPTY;
		}
		return;
	}

	cl_entity_t *ent = gEngine.CL_GetWaterEntity( rvp->vieworigin );
	int cnt;
	if( ent && ent->model && ent->model->type == mod_brush && ent->curstate.skin < 0 )
		cnt = ent->curstate.skin;
	else cnt = viewleaf->contents;

	g_fog.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );

	if( !IsLiquidContents( g_fog.cached_contents ) && IsLiquidContents( cnt ))
	{
		vk_texture_t *tex = NULL;

		// check for water texture
		if( ent && ent->model && ent->model->type == mod_brush )
		{
			int count = ent->model->nummodelsurfaces;

			msurface_t *surf = &ent->model->surfaces[ent->model->firstmodelsurface];
			for( int i = 0; i < count; i++, surf++ )
			{
				if( surf->flags & SURF_DRAWTURB && surf->texinfo && surf->texinfo->texture )
				{
					tex = R_TextureGetByIndex( surf->texinfo->texture->gl_texturenum );
					g_fog.cached_contents = ent->curstate.skin;
					break;
				}
			}
		}
		else
		{
			tex = viewleaf->parent ? R_RecursiveFindWaterTexture( viewleaf->parent, NULL, false ) : NULL;
			if( tex ) g_fog.cached_contents = viewleaf->contents;
		}

		if( !tex ) return;	// no valid fogs

		// copy fog params
		g_fog.fogColor[0] = tex->fogParams[0] / 255.0f;
		g_fog.fogColor[1] = tex->fogParams[1] / 255.0f;
		g_fog.fogColor[2] = tex->fogParams[2] / 255.0f;
		g_fog.fogDensity = tex->fogParams[3] * 0.000025f;
		g_fog.fogStart = g_fog.fogEnd = 0.0f;
		g_fog.fogColor[3] = 1.0f;
		g_fog.fogCustom = false;
		g_fog.fogEnabled = true;
		g_fog.fogSkybox = true;
	}
	else
	{
		g_fog.fogCustom = false;
		g_fog.fogEnabled = true;
		g_fog.fogSkybox = true;
	}
}

void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	// overrided by internal fog
	if( g_fog.fogEnabled || !CVAR_TO_BOOL( vk_fog ) ) return;
	g_fog.fogCustom = bOn;

	// check for invalid parms
	if( flEnd <= flStart )
	{
		g_fog.fogCustom = false;
		return;
	}

	// copy fog params
	g_fog.fogColor[0] = flFogColor[0] / 255.0f;
	g_fog.fogColor[1] = flFogColor[1] / 255.0f;
	g_fog.fogColor[2] = flFogColor[2] / 255.0f;
	g_fog.fogColor[3] = 1.0f;

	g_fog.fogStart = flStart;
	g_fog.fogEnd = flEnd;

	if( g_fog.fogDensity <= 0.0f )
		g_fog.fogSkybox = true;
}

void TriFogParams( float flDensity, int iFogSkybox )
{
	g_fog.fogDensity = flDensity;
	g_fog.fogSkybox = iFogSkybox;
}

void R_GetSceneFogParams( vec4_t color, vec4_t params, qboolean skybox )
{
	Vector4Copy( g_fog.fogColor, color );
	Vector4Set( params, g_fog.fogDensity, g_fog.fogStart, g_fog.fogEnd, 0.0f );

	if( !CVAR_TO_BOOL( vk_fog ) )
		return;
	if( !g_fog.fogEnabled && !g_fog.fogCustom )
		return;
	if( skybox && !g_fog.fogSkybox )
		return;

	// Shader modes: 1 = GL_LINEAR, 2 = GL_EXP, 3 = GL_EXP2.
	if( g_fog.fogEnabled )
		params[3] = FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ) ? 3.0f : 2.0f;
	else params[3] = g_fog.fogDensity > 0.0f ? 3.0f : 1.0f;

	// R_DrawSkyBox halves the density of engine fog in GL.
	if( skybox && g_fog.fogEnabled )
		params[0] *= 0.5f;
}

void R_GetFogParams( vec4_t color, vec4_t params, qboolean skybox )
{
	R_GetSceneFogParams( color, params, skybox );
	if( CVAR_TO_BOOL( rt_enable ) || vk_renderstate.mode_2d || !vk_renderstate.fog_allowed )
		params[3] = 0.0f;
}
