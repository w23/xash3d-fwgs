#include "vk_brush.h"

#include "vk_core.h"
#include "vk_const.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_framectl.h"
#include "vk_math.h"
#include "vk_textures.h"
#include "vk_lightmap.h"
#include "vk_scene.h"
#include "vk_render.h"
#include "vk_light.h"
#include "vk_mapents.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

typedef struct vk_brush_model_s {
	vk_render_model_t render_model;
	int num_water_surfaces;

	rt_light_add_polygon_t *polylights;
	int polylights_count;
} vk_brush_model_t;

static struct {
	struct {
		int num_vertices, num_indices;
	} stat;

	int rtable[MOD_FRAMES][MOD_FRAMES];
} g_brush;

void VK_InitRandomTable( void )
{
	int	tu, tv;

	// make random predictable
	gEngine.COM_SetRandomSeed( 255 );

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			g_brush.rtable[tu][tv] = gEngine.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngine.COM_SetRandomSeed( 0 );
}

qboolean VK_BrushInit( void )
{
	VK_InitRandomTable ();

	return true;
}

void VK_BrushShutdown( void )
{
}

// speed up sin calculations
static const float r_turbsin[] =
{
	#include "warpsin.h"
};

#define SUBDIVIDE_SIZE	64
#define TURBSCALE		( 256.0f / ( M_PI2 ))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
static void EmitWaterPolys( const cl_entity_t *ent, const msurface_t *warp, qboolean reverse )
{
	const float time = gpGlobals->time;
	float	*v, nv, waveHeight;
	float	s, t, os, ot;
	glpoly_t	*p;
	int	i;
	int num_vertices = 0, num_indices = 0;
	int vertex_offset = 0;
	uint16_t *indices;
	r_geometry_buffer_lock_t buffer;

#define MAX_WATER_VERTICES 16
	vk_vertex_t poly_vertices[MAX_WATER_VERTICES];

	const qboolean useQuads = FBitSet( warp->flags, SURF_DRAWTURB_QUADS );

	if( !warp->polys ) return;

	// set the current waveheight
	// FIXME VK if( warp->polys->verts[0][2] >= RI.vieworg[2] )
	// 	waveHeight = -ent->curstate.scale;
	// else
	waveHeight = ent->curstate.scale;

	// reset fog color for nonlightmapped water
	// FIXME VK GL_ResetFogColor();

	// Compute vertex count
	for( p = warp->polys; p; p = p->next ) {
		const int triangles = p->numverts - 2;
		num_vertices += p->numverts;
		num_indices += triangles * 3;
	}

	if (!R_GeometryBufferAllocAndLock( &buffer, num_vertices, num_indices, LifetimeSingleFrame )) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate geometry for %s\n", ent->model->name );
		return;
	}

	indices = buffer.indices.ptr;

	for( p = warp->polys; p; p = p->next )
	{
		ASSERT(p->numverts <= MAX_WATER_VERTICES);

		if( reverse )
			v = p->verts[0] + ( p->numverts - 1 ) * VERTEXSIZE;
		else v = p->verts[0];

		for( i = 0; i < p->numverts; i++ )
		{
			if( waveHeight )
			{
				nv = r_turbsin[(int)(time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				nv = (r_turbsin[(int)(v[0] * 5.0f + time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + nv;
				nv = nv * waveHeight + v[2];
			}
			else nv = v[2];

			os = v[3];
			ot = v[4];

			s = os + r_turbsin[(int)((ot * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			s *= ( 1.0f / SUBDIVIDE_SIZE );

			t = ot + r_turbsin[(int)((os * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			t *= ( 1.0f / SUBDIVIDE_SIZE );

			poly_vertices[i].pos[0] = v[0];
			poly_vertices[i].pos[1] = v[1];
			poly_vertices[i].pos[2] = nv;

			poly_vertices[i].gl_tc[0] = s;
			poly_vertices[i].gl_tc[1] = t;

			poly_vertices[i].lm_tc[0] = 0;
			poly_vertices[i].lm_tc[1] = 0;

#define WATER_NORMALS
			poly_vertices[i].normal[0] = 0;
			poly_vertices[i].normal[1] = 0;
#ifdef WATER_NORMALS
			poly_vertices[i].normal[2] = 0;
#else
			poly_vertices[i].normal[2] = 1;
#endif

			// Ray tracing apparently expects triangle list only (although spec is not very clear about this kekw)
			if (i > 1) {
#ifdef WATER_NORMALS
				vec3_t e0, e1, normal;
				VectorSubtract( poly_vertices[i - 1].pos, poly_vertices[0].pos, e0 );
				VectorSubtract( poly_vertices[i].pos, poly_vertices[0].pos, e1 );
				CrossProduct( e1, e0, normal );
				//VectorNormalize(normal);

				VectorAdd(normal, poly_vertices[0].normal, poly_vertices[0].normal);
				VectorAdd(normal, poly_vertices[i].normal, poly_vertices[i].normal);
				VectorAdd(normal, poly_vertices[i - 1].normal, poly_vertices[i - 1].normal);
#endif
				*(indices++) = (uint16_t)(vertex_offset);
				*(indices++) = (uint16_t)(vertex_offset + i - 1);
				*(indices++) = (uint16_t)(vertex_offset + i);
			}

			if( reverse )
				v -= VERTEXSIZE;
			else
				v += VERTEXSIZE;
		}

#ifdef WATER_NORMALS
		for( i = 0; i < p->numverts; i++ ) {
			VectorNormalize(poly_vertices[i].normal);
		}
#endif

		memcpy(buffer.vertices.ptr + vertex_offset, poly_vertices, sizeof(vk_vertex_t) * p->numverts);
		vertex_offset += p->numverts;
	}

	R_GeometryBufferUnlock( &buffer );

	// Render
	{
		const vk_render_geometry_t geometry = {
			.texture = warp->texinfo->texture->gl_texturenum, // FIXME assert >= 0
			.material = kXVkMaterialWater,
			.surf = warp,

			.max_vertex = num_vertices,
			.vertex_offset = buffer.vertices.unit_offset,

			.element_count = num_indices,
			.index_offset = buffer.indices.unit_offset,
		};

		VK_RenderModelDynamicAddGeometry( &geometry );
	}

	// FIXME VK GL_SetupFogColorForSurfaces();
}

void XVK_DrawWaterSurfaces( const cl_entity_t *ent )
{
	const model_t *model = ent->model;
	vec3_t		mins, maxs;

	if( !VectorIsNull( ent->angles ))
	{
		for( int i = 0; i < 3; i++ )
		{
			mins[i] = ent->origin[i] - model->radius;
			maxs[i] = ent->origin[i] + model->radius;
		}
		//rotated = true;
	}
	else
	{
		VectorAdd( ent->origin, model->mins, mins );
		VectorAdd( ent->origin, model->maxs, maxs );
		//rotated = false;
	}

	// if( R_CullBox( mins, maxs ))
	// 	return;

	VK_RenderModelDynamicBegin( ent->curstate.rendermode, "%s water", model->name );

	// Iterate through all surfaces, find *TURB*
	for( int i = 0; i < model->nummodelsurfaces; i++ )
	{
		const msurface_t *surf = model->surfaces + model->firstmodelsurface + i;

		if( !FBitSet( surf->flags, SURF_DRAWTURB ) && !FBitSet( surf->flags, SURF_DRAWTURB_QUADS) )
			continue;

		if( surf->plane->type != PLANE_Z && !FBitSet( ent->curstate.effects, EF_WATERSIDES ))
			continue;

		if( mins[2] + 1.0f >= surf->plane->dist )
			continue;

		EmitWaterPolys( ent, surf, false );
	}

	// submit as dynamic model
	VK_RenderModelDynamicCommit();

	// TODO:
	// - upload water geometry only once, animate in compute/vertex shader
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
const texture_t *R_TextureAnimation( const cl_entity_t *ent, const msurface_t *s, const struct texture_s *base_override )
{
	const texture_t	*base = base_override ? base_override : s->texinfo->texture;
	int	count, reletive;

	if( ent && ent->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int	tx = (int)((s->texturemins[0] + (base->width << 16)) / base->width) % MOD_FRAMES;
		int	ty = (int)((s->texturemins[1] + (base->height << 16)) / base->height) % MOD_FRAMES;

		reletive = g_brush.rtable[tx][ty] % base->anim_total;
	}
	else
	{
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( findTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gpGlobals->time * speed) % base->anim_total;
	}

	count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return s->texinfo->texture;
	}

	return base;
}

void VK_BrushModelDraw( const cl_entity_t *ent, int render_mode, const matrix4x4 model )
{
	// Expect all buffers to be bound
	const model_t *mod = ent->model;
	vk_brush_model_t *bmodel = mod->cache.data;

	if (!bmodel) {
		gEngine.Con_Printf( S_ERROR "Model %s wasn't loaded\n", mod->name);
		return;
	}

	if (bmodel->num_water_surfaces) {
		XVK_DrawWaterSurfaces(ent);
	}

	if (bmodel->render_model.num_geometries == 0)
		return;

	for (int i = 0; i < bmodel->polylights_count; ++i) {
		rt_light_add_polygon_t *polylight = bmodel->polylights + i;
		polylight->transform_row = (const matrix3x4*)model;
		polylight->dynamic = true;
		RT_LightAddPolygon(polylight);
	}

	for (int i = 0; i < bmodel->render_model.num_geometries; ++i) {
		vk_render_geometry_t *geom = bmodel->render_model.geometries + i;
		const int surface_index = geom->surf - mod->surfaces;
		const xvk_patch_surface_t *patch_surface = g_map_entities.patch.surfaces ? g_map_entities.patch.surfaces+surface_index : NULL;

		// Patch by constant texture index first, if it exists
		if (patch_surface && patch_surface->tex_id >= 0) {
			geom->texture = patch_surface->tex_id;
		} else {
			// Optionally patch by texture_s pointer and run animations
			const struct texture_s *texture_override = patch_surface ? patch_surface->tex : NULL;
			const texture_t *t = R_TextureAnimation(ent, geom->surf, texture_override);
			if (t->gl_texturenum >= 0)
				geom->texture = t->gl_texturenum;
		}
	}

	bmodel->render_model.render_mode = render_mode;
	VK_RenderModelDraw(ent, &bmodel->render_model);
}

static qboolean renderableSurface( const msurface_t *surf, int i ) {
// 	if ( i >= 0 && (surf->flags & ~(SURF_PLANEBACK | SURF_UNDERWATER | SURF_TRANSPARENT)) != 0)
// 	{
// 		gEngine.Con_Reportf("\t%d flags: ", i);
// #define PRINTFLAGS(X) \
// 	X(SURF_PLANEBACK) \
// 	X(SURF_DRAWSKY) \
// 	X(SURF_DRAWTURB_QUADS) \
// 	X(SURF_DRAWTURB) \
// 	X(SURF_DRAWTILED) \
// 	X(SURF_CONVEYOR) \
// 	X(SURF_UNDERWATER) \
// 	X(SURF_TRANSPARENT)

// #define PRINTFLAG(f) if (FBitSet(surf->flags, f)) gEngine.Con_Reportf(" %s", #f);
// 		PRINTFLAGS(PRINTFLAG)
// 		gEngine.Con_Reportf("\n");
// 	}
//
	if (g_map_entities.patch.surfaces && g_map_entities.patch.surfaces[i].flags & Patch_Surface_Delete)
		return false;

	//if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
	if( surf->flags & ( SURF_DRAWTURB | SURF_DRAWTURB_QUADS ) ) {
	//if( surf->flags & ( SURF_DRAWSKY | SURF_CONVEYOR ) ) {
		// FIXME don't print this on second sort-by-texture pass
		//gEngine.Con_Reportf("Skipping surface %d because of flags %08x\n", i, surf->flags);
		return false;
	}

	// Explicitly enable SURF_SKY, otherwise they will be skipped by SURF_DRAWTILED
	if( FBitSet( surf->flags, SURF_DRAWSKY )) {
		return true;
	}

	if( FBitSet( surf->flags, SURF_DRAWTILED )) {
		//gEngine.Con_Reportf("Skipping surface %d because of tiled flag\n", i);
		return false;
	}

	return true;
}

typedef struct {
	int num_surfaces, num_vertices, num_indices;
	int max_texture_id;
	int water_surfaces;
	//int sky_surfaces;
	int emissive_surfaces;
} model_sizes_t;

static model_sizes_t computeSizes( const model_t *mod ) {
	model_sizes_t sizes = {0};

	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const int surface_index = mod->firstmodelsurface + i;
		const msurface_t *surf = mod->surfaces + surface_index;
		const int tex_id = surf->texinfo->texture->gl_texturenum;

		sizes.water_surfaces += !!(surf->flags & (SURF_DRAWTURB | SURF_DRAWTURB_QUADS));

		if (!renderableSurface(surf, surface_index))
			continue;

		++sizes.num_surfaces;
		sizes.num_vertices += surf->numedges;
		sizes.num_indices += 3 * (surf->numedges - 1);
		if (tex_id > sizes.max_texture_id)
			sizes.max_texture_id = tex_id;

		{
			const xvk_patch_surface_t *const psurf = g_map_entities.patch.surfaces ? g_map_entities.patch.surfaces + surface_index : NULL;
			vec3_t emissive;
			if ((psurf && (psurf->flags & Patch_Surface_Emissive)) || (RT_GetEmissiveForTexture(emissive, tex_id)))
				++sizes.emissive_surfaces;
		}
	}

	return sizes;
}

static rt_light_add_polygon_t loadPolyLight(const model_t *mod, const int surface_index, const msurface_t *surf, const vec3_t emissive) {
	rt_light_add_polygon_t lpoly = {0};
	lpoly.num_vertices = Q_min(7, surf->numedges);

	// TODO split, don't clip
	if (surf->numedges > 7)
		gEngine.Con_Printf(S_WARN "emissive surface %d has %d vertices; clipping to 7\n", surface_index, surf->numedges);

	VectorCopy(emissive, lpoly.emissive);

	for (int i = 0; i < lpoly.num_vertices; ++i) {
		const int iedge = mod->surfedges[surf->firstedge + i];
		const medge_t *edge = mod->edges + (iedge >= 0 ? iedge : -iedge);
		const mvertex_t *vertex = mod->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);
		VectorCopy(vertex->position, lpoly.vertices[i]);
	}

	lpoly.surface = surf;
	return lpoly;
}

static qboolean loadBrushSurfaces( model_sizes_t sizes, const model_t *mod ) {
	vk_brush_model_t *bmodel = mod->cache.data;
	uint32_t vertex_offset = 0;
	int num_geometries = 0;
	vk_vertex_t *bvert = NULL;
	uint16_t *bind = NULL;
	uint32_t index_offset = 0;
	r_geometry_buffer_lock_t buffer;

	if (!R_GeometryBufferAllocAndLock( &buffer, sizes.num_vertices, sizes.num_indices, LifetimeLong )) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate geometry for %s\n", mod->name );
		return false;
	}

	bvert = buffer.vertices.ptr;
	bind = buffer.indices.ptr;

	index_offset = buffer.indices.unit_offset;

	// Load sorted by gl_texturenum
	// TODO this does not make that much sense in vulkan (can sort later)
	for (int t = 0; t <= sizes.max_texture_id; ++t)
	{
		for( int i = 0; i < mod->nummodelsurfaces; ++i)
		{
			const int surface_index = mod->firstmodelsurface + i;
			msurface_t *surf = mod->surfaces + surface_index;
			mextrasurf_t	*info = surf->info;
			vk_render_geometry_t *model_geometry = bmodel->render_model.geometries + num_geometries;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );
			int index_count = 0;
			vec3_t tangent;
			int tex_id = surf->texinfo->texture->gl_texturenum;
			const xvk_patch_surface_t *const psurf = g_map_entities.patch.surfaces ? g_map_entities.patch.surfaces + surface_index : NULL;

			if (!renderableSurface(surf, surface_index))
				continue;

			if (t != tex_id)
				continue;

			// FIXME move this to rt_light_bsp and static loading
			{
				qboolean is_emissive = false;
				vec3_t emissive;
				rt_light_add_polygon_t polylight;

				if (psurf && (psurf->flags & Patch_Surface_Emissive)) {
					is_emissive = true;
					VectorCopy(psurf->emissive, emissive);
				} else if (RT_GetEmissiveForTexture(emissive, tex_id)) {
					is_emissive = true;
				}

				if (is_emissive) {
					if (bmodel->polylights) {
						ASSERT(bmodel->polylights_count < sizes.emissive_surfaces);
						bmodel->polylights[bmodel->polylights_count++] = loadPolyLight(mod, surface_index, surf, emissive);
					} else {
						polylight = loadPolyLight(mod, surface_index, surf, emissive);
						RT_LightAddPolygon(&polylight);
					}
				}
			}

			++num_geometries;

			//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

			if (vertex_offset + surf->numedges >= UINT16_MAX)
			{
				gEngine.Con_Printf(S_ERROR "Model %s indices don't fit into 16 bits\n", mod->name);
				// FIXME unlock and free buffers
				return false;
			}

			model_geometry->surf = surf;
			model_geometry->texture = tex_id;

			model_geometry->vertex_offset = buffer.vertices.unit_offset;
			model_geometry->max_vertex = vertex_offset + surf->numedges;

			model_geometry->index_offset = index_offset;

			if( FBitSet( surf->flags, SURF_DRAWSKY )) {
				model_geometry->material = kXVkMaterialSky;
			} else {
				model_geometry->material = kXVkMaterialRegular;
				if (!FBitSet( surf->flags, SURF_DRAWTILED )) {
					VK_CreateSurfaceLightmap( surf, mod );
				}
			}

			if (FBitSet( surf->flags, SURF_CONVEYOR )) {
				model_geometry->material = kXVkMaterialConveyor;
			}

			VectorCopy(surf->texinfo->vecs[0], tangent);
			VectorNormalize(tangent);

			for( int k = 0; k < surf->numedges; k++ )
			{
				const int iedge = mod->surfedges[surf->firstedge + k];
				const medge_t *edge = mod->edges + (iedge >= 0 ? iedge : -iedge);
				const mvertex_t *in_vertex = mod->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);
				vk_vertex_t vertex = {
					{in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]},
				};

				float s = DotProduct( in_vertex->position, surf->texinfo->vecs[0] ) + surf->texinfo->vecs[0][3];
				float t = DotProduct( in_vertex->position, surf->texinfo->vecs[1] ) + surf->texinfo->vecs[1][3];

				s /= surf->texinfo->texture->width;
				t /= surf->texinfo->texture->height;

				vertex.gl_tc[0] = s;
				vertex.gl_tc[1] = t;

				// lightmap texture coordinates
				s = DotProduct( in_vertex->position, info->lmvecs[0] ) + info->lmvecs[0][3];
				s -= info->lightmapmins[0];
				s += surf->light_s * sample_size;
				s += sample_size * 0.5f;
				s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

				t = DotProduct( in_vertex->position, info->lmvecs[1] ) + info->lmvecs[1][3];
				t -= info->lightmapmins[1];
				t += surf->light_t * sample_size;
				t += sample_size * 0.5f;
				t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

				if( FBitSet( surf->flags, SURF_PLANEBACK ))
					VectorNegate( surf->plane->normal, vertex.normal );
				else VectorCopy( surf->plane->normal, vertex.normal );

				VectorCopy(tangent, vertex.tangent);

				vertex.lm_tc[0] = s;
				vertex.lm_tc[1] = t;

				*(bvert++) = vertex;

				// Ray tracing apparently expects triangle list only (although spec is not very clear about this kekw)
				if (k > 1) {
					*(bind++) = (uint16_t)(vertex_offset + 0);
					*(bind++) = (uint16_t)(vertex_offset + k - 1);
					*(bind++) = (uint16_t)(vertex_offset + k);
					index_count += 3;
					index_offset += 3;
				}
			}

			model_geometry->element_count = index_count;
			vertex_offset += surf->numedges;
		}
	}

	R_GeometryBufferUnlock( &buffer );

	if (bmodel->polylights) {
		gEngine.Con_Reportf("WHAT %d %d \n", sizes.emissive_surfaces, bmodel->polylights_count);
		ASSERT(sizes.emissive_surfaces == bmodel->polylights_count);
	}
	ASSERT(sizes.num_surfaces == num_geometries);
	bmodel->render_model.num_geometries = num_geometries;

	return true;
}

qboolean VK_BrushModelLoad( VkCommandBuffer cmdbuf, model_t *mod, qboolean map )
{
	if (mod->cache.data)
	{
		gEngine.Con_Reportf( S_WARN "Model %s was already loaded\n", mod->name );
		return true;
	}

	gEngine.Con_Reportf("%s: %s flags=%08x\n", __FUNCTION__, mod->name, mod->flags);

	{
		const model_sizes_t sizes = computeSizes( mod );
		const size_t model_size =
			sizeof(vk_brush_model_t) +
			sizeof(vk_render_geometry_t) * sizes.num_surfaces;

		vk_brush_model_t *bmodel = Mem_Calloc(vk_core.pool, model_size);
		mod->cache.data = bmodel;
		Q_strncpy(bmodel->render_model.debug_name, mod->name, sizeof(bmodel->render_model.debug_name));
		bmodel->render_model.render_mode = kRenderNormal;
		bmodel->render_model.static_map = map;

		bmodel->num_water_surfaces = sizes.water_surfaces;

		if (sizes.num_surfaces != 0) {
			bmodel->render_model.geometries = (vk_render_geometry_t*)((char*)(bmodel + 1));

			if (!map && sizes.emissive_surfaces)
				bmodel->polylights = Mem_Malloc(vk_core.pool, sizeof(bmodel->polylights[0]) * sizes.emissive_surfaces);

			if (!loadBrushSurfaces(sizes, mod) || !VK_RenderModelInit(cmdbuf, &bmodel->render_model)) {
				gEngine.Con_Printf(S_ERROR "Could not load model %s\n", mod->name);
				Mem_Free(bmodel);
				return false;
			}
		}

		g_brush.stat.num_indices += sizes.num_indices;
		g_brush.stat.num_vertices += sizes.num_vertices;

		gEngine.Con_Reportf("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u\n", mod->name, bmodel->render_model.num_geometries, mod->nummodelsurfaces, g_brush.stat.num_vertices, g_brush.stat.num_indices);
	}

	return true;
}

void VK_BrushModelDestroy( model_t *mod ) {
	vk_brush_model_t *bmodel = mod->cache.data;
	ASSERT(mod->type == mod_brush);
	if (!bmodel)
		return;

	VK_RenderModelDestroy(&bmodel->render_model);
	if (bmodel->polylights)
		Mem_Free(bmodel->polylights);
	Mem_Free(bmodel);
	mod->cache.data = NULL;
}

void VK_BrushStatsClear( void )
{
	// Free previous map data
	g_brush.stat.num_vertices = 0;
	g_brush.stat.num_indices = 0;
}
