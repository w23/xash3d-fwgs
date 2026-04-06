#include "vk_common.h"
#include "vk_lightmap.h"

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "xash3d_mathlib.h"
#include "mod_local.h"

#define SUBDIVIDE_SIZE	64


static void BoundPoly( int numverts, float *verts, vec3_t mins, vec3_t maxs )
{
	int	i, j;
	float	*v;

	ClearBounds( mins, maxs );

	for( i = 0, v = verts; i < numverts; i++ )
	{
		for( j = 0; j < 3; j++, v++ )
		{
			if( *v < mins[j] ) mins[j] = *v;
			if( *v > maxs[j] ) maxs[j] = *v;
		}
	}
}

static void SubdividePolygon_r( model_t *loadmodel, msurface_t *warpface, int numverts, float *verts )
{
	vec3_t		front[SUBDIVIDE_SIZE], back[SUBDIVIDE_SIZE];
	mextrasurf_t	*warpinfo = warpface->info;
	float		dist[SUBDIVIDE_SIZE];
	float		m, frac, s, t, *v;
	int		i, j, k, f, b;
	float		sample_size;
	vec3_t		mins, maxs;
	glpoly2_t		*poly;

	if( numverts > ( SUBDIVIDE_SIZE - 4 ))
		gEngine.Host_Error( "Mod_SubdividePolygon: too many vertexes on face ( %i )\n", numverts );

	sample_size = gEngine.Mod_SampleSizeForFace( warpface );
	BoundPoly( numverts, verts, mins, maxs );

	for( i = 0; i < 3; i++ )
	{
		m = ( mins[i] + maxs[i] ) * 0.5f;
		m = SUBDIVIDE_SIZE * floor( m / SUBDIVIDE_SIZE + 0.5f );
		if( maxs[i] - m < 8 ) continue;
		if( m - mins[i] < 8 ) continue;

		// cut it
		v = verts + i;
		for( j = 0; j < numverts; j++, v += 3 )
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v -= i;
		VectorCopy( verts, v );

		f = b = 0;
		v = verts;
		for( j = 0; j < numverts; j++, v += 3 )
		{
			if( dist[j] >= 0 )
			{
				VectorCopy( v, front[f] );
				f++;
			}

			if( dist[j] <= 0 )
			{
				VectorCopy (v, back[b]);
				b++;
			}

			if( dist[j] == 0 || dist[j+1] == 0 )
				continue;

			if(( dist[j] > 0 ) != ( dist[j+1] > 0 ))
			{
				// clip point
				frac = dist[j] / ( dist[j] - dist[j+1] );
				for( k = 0; k < 3; k++ )
					front[f][k] = back[b][k] = v[k] + frac * (v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon_r( loadmodel, warpface, f, front[0] );
		SubdividePolygon_r( loadmodel, warpface, b, back[0] );
		return;
	}

	if( numverts != 4 )
		ClearBits( warpface->flags, SURF_DRAWTURB_QUADS );

	// add a point in the center to help keep warp valid
	poly = Mem_Calloc( loadmodel->mempool, sizeof( glpoly2_t ) + numverts * VERTEXSIZE * sizeof( float ));
	poly->next = warpface->polys;
	poly->flags = warpface->flags;
	warpface->polys = poly;
	poly->numverts = numverts;

	for( i = 0; i < numverts; i++, verts += 3 )
	{
		VectorCopy( verts, poly->verts[i] );

		if( FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			s = DotProduct( verts, warpface->texinfo->vecs[0] );
			t = DotProduct( verts, warpface->texinfo->vecs[1] );
		}
		else
		{
			s = DotProduct( verts, warpface->texinfo->vecs[0] ) + warpface->texinfo->vecs[0][3];
			t = DotProduct( verts, warpface->texinfo->vecs[1] ) + warpface->texinfo->vecs[1][3];
			s /= warpface->texinfo->texture->width;
			t /= warpface->texinfo->texture->height;
		}

		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// for speed reasons
		if( !FBitSet( warpface->flags, SURF_DRAWTURB ))
		{
			// lightmap texture coordinates
			s = DotProduct( verts, warpinfo->lmvecs[0] ) + warpinfo->lmvecs[0][3];
			s -= warpinfo->lightmapmins[0];
			s += warpface->light_s * sample_size;
			s += sample_size * 0.5f;
			s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

			t = DotProduct( verts, warpinfo->lmvecs[1] ) + warpinfo->lmvecs[1][3];
			t -= warpinfo->lightmapmins[1];
			t += warpface->light_t * sample_size;
			t += sample_size * 0.5f;
			t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

			poly->verts[i][5] = s;
			poly->verts[i][6] = t;
		}
	}
}

static void R_GetEdgePosition( const model_t *mod, const msurface_t *fa, int i, vec3_t vec )
{
	const int lindex = mod->surfedges[fa->firstedge + i];

	if( FBitSet( mod->flags, MODEL_QBSP2 ))
	{
		const medge32_t *pedges = mod->edges32;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
	else
	{
		const medge16_t *pedges = mod->edges16;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa )
{
	vec3_t	verts[SUBDIVIDE_SIZE];
	int	i;

	// convert edges back to a normal polygon
	for( i = 0; i < fa->numedges; i++ )
		R_GetEdgePosition( loadmodel, fa, i, verts[i] );

	SetBits( fa->flags, SURF_DRAWTURB_QUADS ); // predict state

	// do subdivide
	SubdividePolygon_r( loadmodel, fa, fa->numedges, verts[0] );
}

/*
================
R_LightmapCoord

Total copypaste of R_LightmapCoord from gl_rsurf.c
================
*/
void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords )
{
	const mextrasurf_t *info = surf->info;
	float s, t;

	s = DotProduct( v, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	s += surf->light_s * sample_size;
	s += sample_size * 0.5f;
	s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

	t = DotProduct( v, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	t += surf->light_t * sample_size;
	t += sample_size * 0.5f;
	t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

	Vector2Set( coords, s, t );
}

/*
================
R_TextureCoord

Total copypaste of R_TextureCoord from gl_rsurf.c
================
*/
static void R_TextureCoord( const vec3_t v, const msurface_t *surf, vec2_t coords )
{
	const mtexinfo_t *info = surf->texinfo;
	float s, t;

	s = DotProduct( v, info->vecs[0] );
	t = DotProduct( v, info->vecs[1] );

	if( !FBitSet( surf->flags, SURF_DRAWTURB ))
	{
		s = ( s + info->vecs[0][3] ) / info->texture->width;
		t = ( t + info->vecs[1][3] ) / info->texture->height;
	}

	Vector2Set( coords, s, t );
}

/*
================
R_BuildPolygonFromSurface

Init surf->polys for decals
Adapted copypaste of GL_BuildPolygonFromSurface
================
*/
int R_BuildPolygonFromSurface( model_t *mod, msurface_t *fa )
{
	int		i, lnumverts, nColinElim = 0;
	float		sample_size;
	//texture_t		*tex;
	//gl_texture_t	*glt;
	glpoly2_t		*poly;

	if( !mod || !fa->texinfo || !fa->texinfo->texture )
		return nColinElim; // bad polygon ?

	// if( FBitSet( fa->flags, SURF_CONVEYOR ) && fa->texinfo->texture->gl_texturenum != 0 )
	// {
	// 	glt = R_GetTexture( fa->texinfo->texture->gl_texturenum );
	// 	tex = fa->texinfo->texture;
	// 	Assert( glt != NULL && tex != NULL );

	// 	// update conveyor widths for keep properly speed of scrolling
	// 	glt->srcWidth = tex->width;
	// 	glt->srcHeight = tex->height;
	// }

	sample_size = gEngine.Mod_SampleSizeForFace( fa );

	// reconstruct the polygon
	lnumverts = fa->numedges;

	// detach if already created, reconstruct again
	poly = fa->polys;
	fa->polys = NULL;

	// quake simple models (healthkits etc) need to be reconstructed their polys because LM coords has changed after the map change
	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly2_t ) + lnumverts * VERTEXSIZE * sizeof( float ));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for( i = 0; i < lnumverts; i++ )
	{
		R_GetEdgePosition( mod, fa, i, poly->verts[i] );
		R_TextureCoord( poly->verts[i], fa, &poly->verts[i][3] );
		R_LightmapCoord( poly->verts[i], fa, sample_size, &poly->verts[i][5] );
	}

	// remove co-linear points - Ed
	if( /*!gl_keeptjunctions.value &&*/ !FBitSet( fa->flags, SURF_UNDERWATER )) // TODO: fugure out gl_keeptjunctions
	{
		for( i = 0; i < lnumverts; i++ )
		{
			vec3_t	v1, v2;
			float	*prev, *this, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			next = poly->verts[(i + 1) % lnumverts];
			this = poly->verts[i];

			VectorSubtract( this, prev, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next, prev, v2 );
			VectorNormalize( v2 );

			// skip co-linear points
			if(( fabs( v1[0] - v2[0] ) <= 0.001f) && (fabs( v1[1] - v2[1] ) <= 0.001f) && (fabs( v1[2] - v2[2] ) <= 0.001f))
			{
				int	j, k;

				for( j = i + 1; j < lnumverts; j++ )
				{
					for( k = 0; k < VERTEXSIZE; k++ )
						poly->verts[j-1][k] = poly->verts[j][k];
				}

				// retry next vertex next time, which is now current vertex
				lnumverts--;
				nColinElim++;
				i--;
			}
		}
	}

	poly->numverts = lnumverts;
	return nColinElim;
}
