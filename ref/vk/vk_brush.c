#include "vk_brush.h"

#include "vk_core.h"
#include "vk_const.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_framectl.h"
#include "vk_math.h"
#include "r_textures.h"
#include "vk_lightmap.h"
#include "vk_scene.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_light.h"
#include "vk_mapents.h"
#include "r_speeds.h"
#include "vk_staging.h"
#include "vk_logs.h"
#include "profiler.h"
#include "camera.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

#define MODULE_NAME "brush"
#define LOG_MODULE LogModule_Brush

typedef struct {
	int surfaces_count;
	const int *surfaces_indices;

	r_geometry_range_t geometry;
	vk_render_model_t render_model;
} r_brush_water_model_t;

typedef struct vk_brush_model_s {
	model_t *engine_model;

	r_geometry_range_t geometry;

	vk_render_model_t render_model;
	int *surface_to_geometry_index;

	int *animated_indexes;
	int animated_indexes_count;

	matrix4x4 prev_transform;
	float prev_time;

	r_brush_water_model_t water;
	r_brush_water_model_t water_sides;
} vk_brush_model_t;

typedef struct {
	int surfaces;
	int vertices;
	int indices;
} water_model_sizes_t;

typedef struct {
	int num_surfaces, num_vertices, num_indices;
	int max_texture_id;
	int animated_count;

	water_model_sizes_t water, side_water;
} model_sizes_t;

typedef struct conn_edge_s {
	int first_surface;
	int count;
} conn_edge_t;

typedef struct linked_value_s {
		int value, link;
} linked_value_t;

#define MAX_VERTEX_SURFACES 16
typedef struct conn_vertex_s {
	int count;
	linked_value_t surfs[MAX_VERTEX_SURFACES];
} conn_vertex_t;

static struct {
	struct {
		int total_vertices, total_indices;
		int models_drawn;
		int water_surfaces_drawn;
		int water_polys_drawn;
	} stat;

	int rtable[MOD_FRAMES][MOD_FRAMES];

	// Unfortunately the engine only tracks the toplevel worldmodel. *xx submodels, while having their own entities and models, are not lifetime-tracked.
	// I.e. the engine doesn't call Mod_ProcessRenderData() on them, so we don't directly know when to create or destroy them.
	// Therefore, we need to track them manually and destroy them based on some other external event, e.g. Mod_ProcessRenderData(worldmodel)
	vk_brush_model_t *models[MAX_MODELS];
	int models_count;

#define MAX_ANIMATED_TEXTURES 256
	int updated_textures[MAX_ANIMATED_TEXTURES];

	// Smoothed normals comptutation
	// Connectome for edges and vertices
	struct {
		int edges_capacity;
		conn_edge_t *edges;

		int vertices_capacity;
		conn_vertex_t *vertices;
	} conn;
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

qboolean R_BrushInit( void )
{
	VK_InitRandomTable ();

	R_SPEEDS_COUNTER(g_brush.stat.models_drawn, "drawn", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_brush.stat.water_surfaces_drawn, "water.surfaces", kSpeedsMetricCount);
	R_SPEEDS_COUNTER(g_brush.stat.water_polys_drawn, "water.polys", kSpeedsMetricCount);

	return true;
}

void R_BrushShutdown( void ) {
	if (g_brush.conn.edges)
		Mem_Free(g_brush.conn.edges);
}

// speed up sin calculations
static const float r_turbsin[] =
{
	#include "warpsin.h"
};

#define SUBDIVIDE_SIZE	64
#define TURBSCALE		( 256.0f / ( M_PI2 ))

static void addWarpVertIndCounts(const msurface_t *warp, int *num_vertices, int *num_indices) {
	for( glpoly_t *p = warp->polys; p; p = p->next ) {
		const int triangles = p->numverts - 2;
		*num_vertices += p->numverts;
		*num_indices += triangles * 3;
	}
}

typedef struct {
	float prev_time;
	float wave_height;
	const msurface_t *warp;
	qboolean reverse;

	vk_vertex_t *dst_vertices;
	uint16_t *dst_indices;
	vk_render_geometry_t *dst_geometry;

	int *out_vertex_count, *out_index_count;
} compute_water_polys_t;

static void brushComputeWaterPolys( compute_water_polys_t args ) {
	const float time = gpGlobals->time;

#define MAX_WATER_VERTICES 16
	vk_vertex_t poly_vertices[MAX_WATER_VERTICES];

	// FIXME unused? const qboolean useQuads = FBitSet( warp->flags, SURF_DRAWTURB_QUADS );

	ASSERT(args.warp->polys);

	// reset fog color for nonlightmapped water
	// FIXME VK GL_ResetFogColor();

	int vertices = 0;
	int indices = 0;

	for( glpoly_t *p = args.warp->polys; p; p = p->next ) {
		ASSERT(p->numverts <= MAX_WATER_VERTICES);
		const float *v;
		if( args.reverse )
			v = p->verts[0] + ( p->numverts - 1 ) * VERTEXSIZE;
		else
			v = p->verts[0];

		for( int i = 0; i < p->numverts; i++ )
		{
			float nv, prev_nv;
			if( args.wave_height )
			{
				nv = r_turbsin[(int)(time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				nv = (r_turbsin[(int)(v[0] * 5.0f + time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + nv;
				nv = nv * args.wave_height + v[2];

				prev_nv = r_turbsin[(int)(args.prev_time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				prev_nv = (r_turbsin[(int)(v[0] * 5.0f + args.prev_time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + prev_nv;
				prev_nv = prev_nv * args.wave_height + v[2];
			}
			else
				prev_nv = nv = v[2];

			const float os = v[3];
			const float ot = v[4];

			float s = os + r_turbsin[(int)((ot * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			s *= ( 1.0f / SUBDIVIDE_SIZE );

			float t = ot + r_turbsin[(int)((os * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			t *= ( 1.0f / SUBDIVIDE_SIZE );

			poly_vertices[i].pos[0] = v[0];
			poly_vertices[i].pos[1] = v[1];
			poly_vertices[i].pos[2] = nv;

			poly_vertices[i].prev_pos[0] = v[0];
			poly_vertices[i].prev_pos[1] = v[1];
			poly_vertices[i].prev_pos[2] = prev_nv;

			poly_vertices[i].gl_tc[0] = s;
			poly_vertices[i].gl_tc[1] = t;

			poly_vertices[i].lm_tc[0] = 0;
			poly_vertices[i].lm_tc[1] = 0;

			Vector4Set(poly_vertices[i].color, 255, 255, 255, 255);

			poly_vertices[i].normal[0] = 0;
			poly_vertices[i].normal[1] = 0;
			poly_vertices[i].normal[2] = 0;

			if (i > 1) {
				vec3_t e0, e1, normal;
				VectorSubtract( poly_vertices[i - 1].pos, poly_vertices[0].pos, e0 );
				VectorSubtract( poly_vertices[i].pos, poly_vertices[0].pos, e1 );
				CrossProduct( e1, e0, normal );

				VectorAdd(normal, poly_vertices[0].normal, poly_vertices[0].normal);
				VectorAdd(normal, poly_vertices[i].normal, poly_vertices[i].normal);
				VectorAdd(normal, poly_vertices[i - 1].normal, poly_vertices[i - 1].normal);

				args.dst_indices[indices++] = (uint16_t)(vertices);
				args.dst_indices[indices++] = (uint16_t)(vertices + i - 1);
				args.dst_indices[indices++] = (uint16_t)(vertices + i);
			}

			if( args.reverse )
				v -= VERTEXSIZE;
			else
				v += VERTEXSIZE;
		}

		for( int i = 0; i < p->numverts; i++ )
			VectorNormalize(poly_vertices[i].normal);

		memcpy(args.dst_vertices + vertices, poly_vertices, sizeof(vk_vertex_t) * p->numverts);
		vertices += p->numverts;
	}

	// FIXME VK GL_SetupFogColorForSurfaces();

	// Render
	const int tex_id = args.warp->texinfo->texture->gl_texturenum;
	const r_vk_material_t material = R_VkMaterialGetForTexture(tex_id);
	*args.dst_geometry = (vk_render_geometry_t){
		.material = material,

		.ye_olde_texture = tex_id,

		.surf_deprecate = args.warp,

		.max_vertex = vertices,
		.element_count = indices,

		.emissive = {0,0,0},
	};

	RT_GetEmissiveForTexture(args.dst_geometry->emissive, tex_id);
	*args.out_vertex_count = vertices;
	*args.out_index_count = indices;

	g_brush.stat.water_surfaces_drawn++;
	g_brush.stat.water_polys_drawn += indices / 3;
}

static vk_render_type_e brushRenderModeToRenderType( int render_mode ) {
	switch (render_mode) {
		case kRenderNormal:       return kVkRenderTypeSolid;
		case kRenderTransColor:   return kVkRenderType_A_1mA_RW;
		case kRenderTransTexture: return kVkRenderType_A_1mA_R;
		case kRenderGlow:         return kVkRenderType_A_1mA_R;
		case kRenderTransAlpha:   return kVkRenderType_AT;
		case kRenderTransAdd:     return kVkRenderType_A_1_R;
		default: ASSERT(!"Unxpected render_mode");
	}

	return kVkRenderTypeSolid;
}

#if 0 // TOO OLD
static void brushDrawWaterSurfaces( const cl_entity_t *ent, const vec4_t color, const matrix4x4 transform ) {
	const model_t *model = ent->model;
	vec3_t mins, maxs;

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

	VK_RenderModelDynamicBegin( brushRenderModeToRenderType(ent->curstate.rendermode), color, transform, "%s water", model->name );

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
#endif

typedef struct {
	const cl_entity_t *ent;
	const msurface_t *surfaces;
	r_brush_water_model_t *wmodel;
	vk_render_geometry_t *geometries;
	float prev_time;
} fill_water_surfaces_args_t;

static void fillWaterSurfaces( fill_water_surfaces_args_t args ) {
	ASSERT(args.wmodel->surfaces_count > 0);

	const float wave_height = (!args.ent) ? 0.f : args.ent->curstate.scale;

	const r_geometry_range_lock_t geom_lock = R_GeometryRangeLock(&args.wmodel->geometry);

	int vertices_offset = 0;
	int indices_offset = 0;
	for (int i = 0; i < args.wmodel->surfaces_count; ++i) {
		const int surf_index = args.wmodel->surfaces_indices[i];
		const msurface_t *warp = args.surfaces + surf_index;

		/* if( warp->plane->type != PLANE_Z && !FBitSet( ent->curstate.effects, EF_WATERSIDES )) */
		/* 	continue; */

		int vertices = 0, indices = 0;
		brushComputeWaterPolys((compute_water_polys_t){
			.prev_time = args.prev_time,
			.wave_height = wave_height,
			.reverse = false, // ??? is it ever true?
			.warp = warp,

			.dst_vertices = geom_lock.vertices + vertices_offset,
			.dst_indices = geom_lock.indices + indices_offset,
			.dst_geometry = args.geometries + i,

			.out_vertex_count = &vertices,
			.out_index_count = &indices,
		});

		args.geometries[i].vertex_offset = args.wmodel->geometry.vertices.unit_offset + vertices_offset;
		args.geometries[i].index_offset = args.wmodel->geometry.indices.unit_offset + indices_offset;

		vertices_offset += vertices;
		indices_offset += indices;

		ASSERT(vertices_offset  <= args.wmodel->geometry.vertices.count);
		ASSERT(indices_offset <= args.wmodel->geometry.indices.count);
	}

	R_GeometryRangeUnlock( &geom_lock );
}

static rt_light_add_polygon_t loadPolyLight(const model_t *mod, const int surface_index, const msurface_t *surf, const vec3_t emissive);

static qboolean doesTextureChainChange( const texture_t *const base ) {
	const texture_t *cur = base;
	if (!cur)
		return false;

	cur = cur->anim_next;
	while (cur && cur != base) {
		if (cur->gl_texturenum != base->gl_texturenum)
			return true;
		cur = cur->anim_next;
	}
	return false;
}

static qboolean isSurfaceAnimated( const msurface_t *s, qboolean is_worldmodel ) {
	const texture_t *const base = s->texinfo->texture;

	if( !base->anim_total && !base->alternate_anims )
		return false;

	/* TODO why did we do this? It doesn't seem to rule out animation really.
	if( base->name[0] == '-' )
		return false;
	*/

	// Worldmodel cannot be triggered and change between alternate_anims and regular anims,
	// therefore it should not be checked. There are lights (e.g. in c2a5) which have alternate anims.
	// These lights get incorrectly marked as dynamic, tanking the performance.

	if (!is_worldmodel && base->alternate_anims && base->gl_texturenum != base->alternate_anims->gl_texturenum)
		return true;

	return doesTextureChainChange(base) || (!is_worldmodel && doesTextureChainChange(base->alternate_anims));
}

typedef enum {
	BrushSurface_Hidden = 0,
	BrushSurface_Regular,
	BrushSurface_Animated,
	BrushSurface_Water,
	BrushSurface_WaterSide,
	BrushSurface_Sky,
} brush_surface_type_e;

static brush_surface_type_e getSurfaceType( const msurface_t *surf, int i, qboolean is_worldmodel ) {
// 	if ( i >= 0 && (surf->flags & ~(SURF_PLANEBACK | SURF_UNDERWATER | SURF_TRANSPARENT)) != 0)
// 	{
// 		DEBUG("\t%d flags: ", i);
// #define PRINTFLAGS(X) \
// 	X(SURF_PLANEBACK) \
// 	X(SURF_DRAWSKY) \
// 	X(SURF_DRAWTURB_QUADS) \
// 	X(SURF_DRAWTURB) \
// 	X(SURF_DRAWTILED) \
// 	X(SURF_CONVEYOR) \
// 	X(SURF_UNDERWATER) \
// 	X(SURF_TRANSPARENT)

// #define PRINTFLAG(f) if (FBitSet(surf->flags, f)) DEBUG(" %s", #f);
// 		PRINTFLAGS(PRINTFLAG)
// 		DEBUG("\n");
// 	}
	const xvk_patch_surface_t *patch_surface = R_VkPatchGetSurface(i);
	if (patch_surface && patch_surface->flags & Patch_Surface_Delete)
		return BrushSurface_Hidden;

	if (surf->flags & (SURF_DRAWTURB | SURF_DRAWTURB_QUADS)) {
		return (!surf->polys) ? BrushSurface_Hidden :
			// Worldmodel doesn't distinguish between !=PLANE_Z sides and not sides
			(is_worldmodel || surf->plane->type == PLANE_Z) ? BrushSurface_Water : BrushSurface_WaterSide;
	}

	// Explicitly enable SURF_SKY, otherwise they will be skipped by SURF_DRAWTILED
	if( FBitSet( surf->flags, SURF_DRAWSKY ))
		return BrushSurface_Sky;

	//if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
	if( surf->flags & ( SURF_DRAWTURB | SURF_DRAWTURB_QUADS ) ) {
	//if( surf->flags & ( SURF_DRAWSKY | SURF_CONVEYOR ) ) {
		// FIXME don't print this on second sort-by-texture pass
		//DEBUG("Skipping surface %d because of flags %08x", i, surf->flags);
		return BrushSurface_Hidden;
	}

	if( FBitSet( surf->flags, SURF_DRAWTILED )) {
		//DEBUG("Skipping surface %d because of tiled flag", i);
		return BrushSurface_Hidden;
	}

	const qboolean patched_material = patch_surface && !!(patch_surface->flags & Patch_Surface_Material);
	if (!patched_material && isSurfaceAnimated(surf, is_worldmodel)) {
		return BrushSurface_Animated;
	}

	return BrushSurface_Regular;
}

static qboolean brushCreateWaterModel(const model_t *mod, r_brush_water_model_t *wmodel, const water_model_sizes_t sizes, brush_surface_type_e type, qboolean is_worldmodel) {

	const r_geometry_range_t geometry = R_GeometryRangeAlloc(sizes.vertices, sizes.indices);
	if (!geometry.block_handle.size) {
		ERR("Cannot allocate geometry (v=%d, i=%d) for water model %s",
			sizes.vertices, sizes.indices, mod->name );
		return false;
	}

	vk_render_geometry_t *const geometries = Mem_Malloc(vk_core.pool, sizeof(vk_render_geometry_t) * sizes.surfaces);

	int* const surfaces_indices = Mem_Malloc(vk_core.pool, sizes.surfaces * sizeof(int));
	int surfaces_count = 0;
	for( int i = 0; i < mod->nummodelsurfaces; ++i) {
		const int surface_index = mod->firstmodelsurface + i;
		const msurface_t *surf = mod->surfaces + surface_index;

		if (getSurfaceType(surf, surface_index, is_worldmodel) == type) {
			surfaces_indices[surfaces_count++] = surface_index;
		}
	}

	ASSERT(surfaces_count == sizes.surfaces);

	wmodel->surfaces_indices = surfaces_indices;
	wmodel->surfaces_count = surfaces_count;
	wmodel->surfaces_indices = surfaces_indices;
	wmodel->geometry = geometry;

	fillWaterSurfaces( (fill_water_surfaces_args_t){
		.ent = NULL,
		.surfaces = mod->surfaces,
		.wmodel = wmodel,
		.geometries = geometries,
		.prev_time = 0.f,
	});

	if (!R_RenderModelCreate(&wmodel->render_model, (vk_render_model_init_t){
		.name = mod->name,
		.geometries = geometries,
		.geometries_count = surfaces_count,
		.dynamic = true,
		})) {
		ERR("Could not create water render model for brush model %s", mod->name);
		return false;
	}

	return true;
}

static material_mode_e brushMaterialModeForRenderType(vk_render_type_e render_type) {
	switch (render_type) {
		case kVkRenderTypeSolid:
			return kMaterialMode_Opaque;
			break;
		case kVkRenderType_A_1mA_RW: // blend: scr*a + dst*(1-a), depth: RW
		case kVkRenderType_A_1mA_R:  // blend: scr*a + dst*(1-a), depth test
			return kMaterialMode_Translucent;
			break;
		case kVkRenderType_A_1:   // blend: scr*a + dst, no depth test or write; sprite:kRenderGlow only
			return kMaterialMode_BlendGlow;
			break;
		case kVkRenderType_A_1_R: // blend: scr*a + dst, depth test
		case kVkRenderType_1_1_R: // blend: scr + dst, depth test
			return kMaterialMode_BlendAdd;
			break;
		case kVkRenderType_AT: // no blend, depth RW, alpha test
			return kMaterialMode_AlphaTest;
			break;

		default:
			gEngine.Host_Error("Unexpected render type %d\n", render_type);
	}

	return kMaterialMode_Opaque;
}

static void brushDrawWater(r_brush_water_model_t *wmodel, const cl_entity_t *ent, const msurface_t *surfaces, int render_type, const vec4_t color, const matrix4x4 transform, const matrix4x4 prev_transform, float prev_time) {
	APROF_SCOPE_DECLARE_BEGIN(brush_draw_water, __FUNCTION__);
	ASSERT(wmodel->surfaces_count > 0);

	fillWaterSurfaces((fill_water_surfaces_args_t){
		.ent = ent,
		.surfaces = surfaces,
		.wmodel = wmodel,
		.geometries = wmodel->render_model.geometries,
		.prev_time = prev_time,
	});

	if (!R_RenderModelUpdate(&wmodel->render_model)) {
		ERR("Failed to update brush model \"%s\" water", wmodel->render_model.debug_name);
	}

	const material_mode_e material_mode = brushMaterialModeForRenderType(render_type);
	R_RenderModelDraw(&wmodel->render_model, (r_model_draw_t){
		.render_type = render_type,
		.material_mode = material_mode,
		.color = (const vec4_t*)color,
		.transform = (const matrix4x4*)transform,
		.prev_transform = (const matrix4x4*)prev_transform,
	});

	APROF_SCOPE_END(brush_draw_water);
}

#if 0
// TODO use this
static void computeConveyorSpeed(const color24 rendercolor, int tex_index, vec2_t speed) {
	float sy, cy;
	float flConveyorSpeed = 0.0f;
	float flRate, flAngle;
	vk_texture_t *texture = R_TextureGetByIndex( tex_index );
	//gl_texture_t	*texture;

	// FIXME
	/* if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && RI.currententity == gEngfuncs.GetEntityByIndex( 0 ) ) */
	/* { */
	/* 	// same as doom speed */
	/* 	flConveyorSpeed = -35.0f; */
	/* } */
	/* else */
	{
		flConveyorSpeed = (rendercolor.g<<8|rendercolor.b) / 16.0f;
		if( rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
	}
	//texture = R_GetTexture( glState.currentTextures[glState.activeTMU] );

	flRate = fabs( flConveyorSpeed ) / (float)texture->width;
	flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;

	SinCos( flAngle * ( M_PI_F / 180.0f ), &sy, &cy );
	speed[0] = cy * flRate;
	speed[1] = sy * flRate;
}
#endif

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
const texture_t *R_TextureAnimation( const cl_entity_t *ent, const msurface_t *s )
{
	const texture_t *base = s->texinfo->texture;
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
		/* TODO
		if( FBitSet( R_TextureGetByIndex( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else */ speed = 20;

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

void R_BrushModelDraw( const cl_entity_t *ent, int render_mode, float blend, const matrix4x4 in_transform ) {
	// Expect all buffers to be bound
	const model_t *mod = ent->model;
	vk_brush_model_t *bmodel = mod->cache.data;

	if (!bmodel) {
		ERR("Model %s wasn't loaded", mod->name);
		return;
	}

	matrix4x4 transform;
	if (in_transform)
		Matrix4x4_Copy(transform, in_transform);
	else
		Matrix4x4_LoadIdentity(transform);

	vec4_t color = {1, 1, 1, 1};
	vk_render_type_e render_type = kVkRenderTypeSolid;
	switch (render_mode) {
		case kRenderNormal:
			Vector4Set(color, 1.f, 1.f, 1.f, 1.f);
			render_type = kVkRenderTypeSolid;
			break;
		case kRenderTransColor:
			render_type = kVkRenderType_A_1mA_RW;
			Vector4Set(color,
				ent->curstate.rendercolor.r / 255.f,
				ent->curstate.rendercolor.g / 255.f,
				ent->curstate.rendercolor.b / 255.f,
				blend);
			break;
		case kRenderTransAdd:
			Vector4Set(color, blend, blend, blend, 1.f);
			render_type = kVkRenderType_A_1_R;
			break;
		case kRenderTransAlpha:
			if( gEngine.EngineGetParm( PARM_QUAKE_COMPATIBLE, 0 ))
			{
				render_type = kVkRenderType_A_1mA_RW;
				Vector4Set(color, 1.f, 1.f, 1.f, blend);
			}
			else
			{
				Vector4Set(color, 1.f, 1.f, 1.f, 1.f);
				render_type = kVkRenderType_AT;
			}
			break;
		case kRenderTransTexture:
		case kRenderGlow:
			render_type = kVkRenderType_A_1mA_R;
			Vector4Set(color, 1.f, 1.f, 1.f, blend);
			break;
	}

	// Only Normal and TransAlpha have lightmaps
	// TODO: on big maps more than a single lightmap texture is possible
	bmodel->render_model.lightmap = (render_mode == kRenderNormal || render_mode == kRenderTransAlpha) ? 1 : 0;

	if (bmodel->water.surfaces_count)
		brushDrawWater(&bmodel->water, ent, bmodel->engine_model->surfaces, render_type, color, transform, bmodel->prev_transform, bmodel->prev_time);

	if (bmodel->water_sides.surfaces_count && FBitSet( ent->curstate.effects, EF_WATERSIDES ) ) {
		brushDrawWater(&bmodel->water_sides, ent, bmodel->engine_model->surfaces, render_type, color, transform, bmodel->prev_transform, bmodel->prev_time);
	}


	++g_brush.stat.models_drawn;

	if (bmodel->render_model.num_geometries == 0)
		return;

	// Animate textures
	{
		APROF_SCOPE_DECLARE_BEGIN(brush_update_textures, "brush: update animated textures");
		// Update animated textures
		int updated_textures_count = 0;
		for (int i = 0; i < bmodel->animated_indexes_count; ++i) {
			const int geom_index = bmodel->animated_indexes[i];
			vk_render_geometry_t *geom = bmodel->render_model.geometries + geom_index;
			const int surface_index = geom->surf_deprecate - mod->surfaces;

			// Optionally patch by texture_s pointer and run animations
			const texture_t *t = R_TextureAnimation(ent, geom->surf_deprecate);
			const int new_tex_id = t->gl_texturenum;

			if (new_tex_id >= 0 && new_tex_id != geom->ye_olde_texture) {
				geom->ye_olde_texture = new_tex_id;
				geom->material = R_VkMaterialGetForTexture(new_tex_id);
				if (updated_textures_count < MAX_ANIMATED_TEXTURES) {
					g_brush.updated_textures[updated_textures_count++] = bmodel->animated_indexes[i];
				}
			}

			// Animated textures can be emissive
			// Add them as dynamic lights for now. It would probably be better if they were static lights (for worldmodel),
			// but there's no easy way to do it for now.
			vec3_t *emissive = &bmodel->render_model.geometries[geom_index].emissive;
			if (RT_GetEmissiveForTexture(*emissive, new_tex_id)) {
				rt_light_add_polygon_t polylight = loadPolyLight(mod, surface_index, geom->surf_deprecate, *emissive);
				polylight.dynamic = true;
				polylight.transform_row = (const matrix3x4*)&transform;
				RT_LightAddPolygon(&polylight);
			}
		}

		if (updated_textures_count > 0) {
			R_RenderModelUpdateMaterials(&bmodel->render_model, g_brush.updated_textures, updated_textures_count);
		}
		APROF_SCOPE_END(brush_update_textures);
	}

	const material_mode_e material_mode = brushMaterialModeForRenderType(render_type);
	R_RenderModelDraw(&bmodel->render_model, (r_model_draw_t){
		.render_type = render_type,
		.material_mode = material_mode,
		.color = &color,
		.transform = &transform,
		.prev_transform = &bmodel->prev_transform,
	});

	Matrix4x4_Copy(bmodel->prev_transform, transform);
	bmodel->prev_time = gpGlobals->time;
}

static model_sizes_t computeSizes( const model_t *mod, qboolean is_worldmodel ) {
	model_sizes_t sizes = {0};

	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const int surface_index = mod->firstmodelsurface + i;
		const msurface_t *surf = mod->surfaces + surface_index;
		const int tex_id = surf->texinfo->texture->gl_texturenum;

		if (tex_id > sizes.max_texture_id)
			sizes.max_texture_id = tex_id;

		switch (getSurfaceType(surf, surface_index, is_worldmodel)) {
		case BrushSurface_Water:
			sizes.water.surfaces++;
			addWarpVertIndCounts(surf, &sizes.water.vertices, &sizes.water.indices);
			continue;
		case BrushSurface_WaterSide:
			sizes.side_water.surfaces++;
			addWarpVertIndCounts(surf, &sizes.side_water.vertices, &sizes.side_water.indices);
			continue;
		case BrushSurface_Hidden:
			continue;

		case BrushSurface_Animated:
			sizes.animated_count++;
		case BrushSurface_Regular:
		case BrushSurface_Sky:
			break;
		}

		++sizes.num_surfaces;
		sizes.num_vertices += surf->numedges;
		sizes.num_indices += 3 * (surf->numedges - 1);
	}

	DEBUG("Computed sizes for brush model \"%s\":", mod->name);
	DEBUG("  num_surfaces=%d animated_count=%d num_vertices=%d num_indices=%d max_texture_id=%d",
		sizes.num_surfaces, sizes.animated_count, sizes.num_vertices, sizes.num_indices, sizes.max_texture_id);
	DEBUG("  water_surfaces=%d water_vertices=%d water_indices=%d",
		sizes.water.surfaces, sizes.water.vertices, sizes.water.indices);

	DEBUG("  side_water_surfaces=%d side_water_vertices=%d side_water_indices=%d",
		sizes.side_water.surfaces, sizes.side_water.vertices, sizes.side_water.indices);

	return sizes;
}

typedef struct {
	const model_t *mod;
	const vk_brush_model_t *bmodel;
	model_sizes_t sizes;
	uint32_t base_vertex_offset;
	uint32_t base_index_offset;

	vk_render_geometry_t *out_geometries;
	vk_vertex_t *out_vertices;
	uint16_t *out_indices;
	qboolean is_worldmodel;
} fill_geometries_args_t;

static void getSurfaceNormal( const msurface_t *surf, vec3_t out_normal) {
	if( FBitSet( surf->flags, SURF_PLANEBACK ))
		VectorNegate( surf->plane->normal, out_normal );
	else
		VectorCopy( surf->plane->normal, out_normal );

	// TODO scale normal by area -- bigger surfaces should have bigger impact
	// NOTE scaling normal by area might be totally incorrect in many circumstances
	// The more corect logic there is way more difficult
	//VectorScale(normal, surf->plane.
}

static qboolean shouldSmoothLinkSurfaces(const model_t* mod, qboolean smooth_entire_model, int surf1, int surf2) {
	// Filter explicit exclusion
	for (int i = 0; i < g_map_entities.smoothing.excluded_pairs_count; i+=2) {
		const int cand1 = g_map_entities.smoothing.excluded_pairs[i];
		const int cand2 = g_map_entities.smoothing.excluded_pairs[i+1];

		if ((cand1 == surf1 && cand2 == surf2)
			|| (cand1 == surf2 && cand2 == surf1))
			return false;
	}

	qboolean excluded = false;
	for (int i = 0; i < g_map_entities.smoothing.excluded_count; ++i) {
		const int cand = g_map_entities.smoothing.excluded[i];
		if (cand == surf1 || cand == surf2) {
			excluded = true;
			break;
		}
	}

	if (smooth_entire_model && !excluded)
		return true;

	// Smoothing groups have priority over individual exclusion.
	// That way we can exclude a surface from smoothing with most of its neighbours,
	// but still smooth it with some.
	for (int i = 0; i < g_map_entities.smoothing.groups_count; ++i) {
		const xvk_smoothing_group_t *g = g_map_entities.smoothing.groups + i;
		uint32_t bits = 0;
		for (int j = 0; j < g->count; ++j) {
			if (g->surfaces[j] == surf1) {
				bits |= 1;
				if (bits == 3)
					return true;
			}
			else if (g->surfaces[j] == surf2) {
				bits |= 2;
				if (bits == 3)
					return true;
			}
		}
	}

	if (excluded)
		return false;

	// Do not join surfaces with different textures. Assume they belong to different objects.
	{
		// Should we also check texture/material patches too to filter out pairs which originally had
		// same textures, but with patches do not?
		if (mod->surfaces[surf1].texinfo->texture->gl_texturenum
			!= mod->surfaces[surf2].texinfo->texture->gl_texturenum)
			return false;
	}

	vec3_t n1, n2;
	getSurfaceNormal(mod->surfaces + surf1, n1);
	getSurfaceNormal(mod->surfaces + surf2, n2);

	const float dot = DotProduct(n1, n2);
	DEBUG("Smoothing: dot(%d, %d) = %f (t=%f)", surf1, surf2, dot, g_map_entities.smoothing.threshold);

	return dot >= g_map_entities.smoothing.threshold;
}

static int lvFindValue(const linked_value_t *li, int count, int needle) {
	for (int i = 0; i < count; ++i)
		if (li[i].value == needle)
			return i;
	return -1;
}
static int lvFindOrAddValue(linked_value_t *li, int *count, int capacity, int needle) {
	const int found = lvFindValue(li, *count, needle);
	if (found >= 0)
		return found;
	if (*count == capacity)
		return -1;
	li[*count].value = needle;
	li[*count].link = *count;
	return (*count)++;
}

static int lvFindBaseIndex(const linked_value_t *li, int index) {
	while (li[index].link != index)
		index = li[index].link;
	return index;
}

static void lvFlatten(linked_value_t *li, int count) {
	for (int i = 0; i < count; ++i) {
		for (int j = i; j < count; ++j) {
			if (lvFindBaseIndex(li, j) == i) {
				li[j].link = i;
			}
		}
	}
}

static void linkSmoothSurfaces(const model_t* mod, int surf1, int surf2, int vertex_index) {
	conn_vertex_t *v = g_brush.conn.vertices + vertex_index;

	int i1 = lvFindOrAddValue(v->surfs, &v->count, COUNTOF(v->surfs), surf1);
	int i2 = lvFindOrAddValue(v->surfs, &v->count, COUNTOF(v->surfs), surf2);

	DEBUG("Link %d(%d)<->%d(%d) v=%d", surf1, i1, surf2, i2, vertex_index);

	if (i1 < 0 || i2 < 0) {
		ERR("Model %s cannot smooth link surf %d<->%d for vertex %d", mod->name, surf1, surf2, vertex_index);
		return;
	}

	i1 = lvFindBaseIndex(v->surfs, i1);
	i2 = lvFindBaseIndex(v->surfs, i2);

	// Link them
	v->surfs[Q_max(i1, i2)].link = Q_min(i1, i2);
}

static void connectVertices( const model_t *mod, qboolean smooth_entire_model ) {
	if (mod->numedges > g_brush.conn.edges_capacity) {
		if (g_brush.conn.edges)
			Mem_Free(g_brush.conn.edges);

		g_brush.conn.edges_capacity = mod->numedges;
		g_brush.conn.edges = Mem_Calloc(vk_core.pool, sizeof(*g_brush.conn.edges) * g_brush.conn.edges_capacity);
	}

	if (mod->numvertexes > g_brush.conn.vertices_capacity) {
		if (g_brush.conn.vertices)
			Mem_Free(g_brush.conn.vertices);

		g_brush.conn.vertices_capacity = mod->numvertexes;
		g_brush.conn.vertices = Mem_Calloc(vk_core.pool, sizeof(*g_brush.conn.vertices) * g_brush.conn.vertices_capacity);
	}

	// Find connection edges
	for (int i = 0; i < mod->nummodelsurfaces; ++i) {
		const int surface_index = mod->firstmodelsurface + i;
		const msurface_t *surf = mod->surfaces + surface_index;

		for(int k = 0; k < surf->numedges; k++) {
			const int iedge_dir = mod->surfedges[surf->firstedge + k];
			const int iedge = iedge_dir >= 0 ? iedge_dir : -iedge_dir;

			ASSERT(iedge >= 0);
			ASSERT(iedge < mod->numedges);

			conn_edge_t *cedge = g_brush.conn.edges + iedge;
			if (cedge->count == 0) {
				cedge->first_surface = surface_index;
			} else {
				const medge_t *edge = mod->edges + iedge;
				if (shouldSmoothLinkSurfaces(mod, smooth_entire_model, cedge->first_surface, surface_index)) {
					linkSmoothSurfaces(mod, cedge->first_surface, surface_index, edge->v[0]);
					linkSmoothSurfaces(mod, cedge->first_surface, surface_index, edge->v[1]);
				}

				if (cedge->count > 1) {
					WARN("Model %s edge %d has %d surfaces", mod->name, i, cedge->count);
				}
			}
			cedge->count++;
		} // for surf->numedges
	} // for mod->nummodelsurfaces

	int hist[17] = {0};
	for (int i = 0; i < mod->numvertexes; ++i) {
		conn_vertex_t *vtx = g_brush.conn.vertices + i;
		if (vtx->count < 16) {
			hist[vtx->count]++;
		} else {
			hist[16]++;
		}

		lvFlatten(vtx->surfs, vtx->count);

// Too verbose
#if 0
		if (vtx->count) {
			DEBUG("Vertex %d linked count %d", i, vtx->count);
			for (int j = 0; j < vtx->count; ++j) {
				DEBUG("  %d: l=%d v=%d", j, vtx->surfs[j].link, vtx->surfs[j].value);
			}
		}
#endif
	}

	for (int i = 0; i < COUNTOF(hist); ++i) {
		DEBUG("VTX hist[%d] = %d", i, hist[i]);
	}
}

static qboolean getSmoothedNormalFor(const model_t* mod, int vertex_index, int surface_index, vec3_t out_normal) {
	const conn_vertex_t *v = g_brush.conn.vertices + vertex_index;
	const int index = lvFindValue(v->surfs, v->count, surface_index);
	if (index < 0)
		return false;
	const int base = lvFindBaseIndex(v->surfs, index);

	vec3_t normal = {0};
	for (int i = 0; i < v->count; ++i) {
		if (v->surfs[i].link == base) {
			const int surface = v->surfs[i].value;
			vec3_t surf_normal = {0};
			getSurfaceNormal(mod->surfaces + surface, surf_normal);
			VectorAdd(normal, surf_normal, normal);
		}
	}

	VectorNormalize(normal);
	VectorCopy(normal, out_normal);
	return true;
}

static const xvk_mapent_func_any_t *getModelFuncAnyPatch( const model_t *const mod ) {
	for (int i = 0; i < g_map_entities.func_any_count; ++i) {
		const xvk_mapent_func_any_t *const fw = g_map_entities.func_any + i;
		if (Q_strcmp(mod->name, fw->model) == 0) {
			return fw;
		}
	}

	return NULL;
}

static qboolean fillBrushSurfaces(fill_geometries_args_t args) {
	int vertex_offset = 0;
	int num_geometries = 0;
	int animated_count = 0;

	vk_vertex_t *p_vert = args.out_vertices;
	uint16_t *p_ind = args.out_indices;
	int index_offset = args.base_index_offset;

	const xvk_mapent_func_any_t *const entity_patch = getModelFuncAnyPatch(args.mod);
	connectVertices(args.mod, entity_patch ? entity_patch->smooth_entire_model : false);


	// Load sorted by gl_texturenum
	// TODO this does not make that much sense in vulkan (can sort later)
	for (int t = 0; t <= args.sizes.max_texture_id; ++t) {
		for( int i = 0; i < args.mod->nummodelsurfaces; ++i) {
			const int surface_index = args.mod->firstmodelsurface + i;
			msurface_t *surf = args.mod->surfaces + surface_index;
			const mextrasurf_t *info = surf->info;
			vk_render_geometry_t *model_geometry = args.out_geometries + num_geometries;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );
			int index_count = 0;
			vec3_t tangent;
			const int orig_tex_id = surf->texinfo->texture->gl_texturenum;
			if (t != orig_tex_id)
				continue;

			int tex_id = orig_tex_id;

			// TODO this patching should probably override entity patching below
			const xvk_patch_surface_t *const psurf = R_VkPatchGetSurface(surface_index);

			const brush_surface_type_e type = getSurfaceType(surf, surface_index, args.is_worldmodel);
			switch (type) {
			case BrushSurface_Water:
			case BrushSurface_WaterSide:
			case BrushSurface_Hidden:
				continue;
			case BrushSurface_Animated:
				args.bmodel->animated_indexes[animated_count++] = num_geometries;
			case BrushSurface_Regular:
			case BrushSurface_Sky:
				break;
			}

			args.bmodel->surface_to_geometry_index[i] = num_geometries;

			++num_geometries;

			//DEBUG( "surface %d: numverts=%d numedges=%d", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

			if (vertex_offset + surf->numedges >= UINT16_MAX) {
				// We might be able to handle it by adjusting base_vertex_offset, etc
				ERR("Model %s indices don't fit into 16 bits", args.mod->name);
				return false;
			}

			model_geometry->ye_olde_texture = orig_tex_id;
			qboolean material_assigned = false;

			if (psurf && (psurf->flags & Patch_Surface_Material)) {
				model_geometry->material = R_VkMaterialGetForRef(psurf->material_ref);
				material_assigned = true;
			}

			if (!material_assigned && entity_patch) {
				for (int i = 0; i < entity_patch->matmap_count; ++i) {
					if (entity_patch->matmap[i].from_tex == orig_tex_id) {
						model_geometry->material = R_VkMaterialGetForTexture(entity_patch->matmap[i].to_mat.index);
						material_assigned = true;
						break;
					}
				}

				if (!material_assigned && entity_patch->rendermode > 0) {
					material_assigned = R_VkMaterialGetEx(tex_id, entity_patch->rendermode, &model_geometry->material);
					if (!material_assigned && entity_patch->rendermode == kRenderTransColor) {
						// TransColor means ignore textures and draw just color
						model_geometry->material = R_VkMaterialGetForTexture(tglob.whiteTexture);
						model_geometry->ye_olde_texture = tglob.whiteTexture;
						material_assigned = true;
					}
				}
			}

			if (!material_assigned) {
				model_geometry->material = R_VkMaterialGetForTexture(tex_id);
				material_assigned = true;
			}

			VectorClear(model_geometry->emissive);

			model_geometry->surf_deprecate = surf;

			model_geometry->vertex_offset = args.base_vertex_offset;
			model_geometry->max_vertex = vertex_offset + surf->numedges;

			model_geometry->index_offset = index_offset;

			if ( type == BrushSurface_Sky ) {
#define TEX_BASE_SKYBOX 0x0f000000u // FIXME ray_interop.h
				model_geometry->material.tex_base_color = TEX_BASE_SKYBOX;
			} else {
				ASSERT(!FBitSet( surf->flags, SURF_DRAWTILED ));
				VK_CreateSurfaceLightmap( surf, args.mod );
			}

			if (FBitSet( surf->flags, SURF_CONVEYOR )) {
				// FIXME make an explicit list of dynamic-uv geometries
			}

			vec3_t surf_normal;
			getSurfaceNormal(surf, surf_normal);

			vk_vertex_t *const pvert_begin = p_vert;
			for( int k = 0; k < surf->numedges; k++ )
			{
				const int iedge_dir = args.mod->surfedges[surf->firstedge + k];
				const int iedge = iedge_dir >= 0 ? iedge_dir : -iedge_dir;
				const medge_t *edge = args.mod->edges + iedge;
				const int vertex_index = iedge_dir >= 0 ? edge->v[0] : edge->v[1];
				const mvertex_t *in_vertex = args.mod->vertexes + vertex_index;

				vk_vertex_t vertex = {
					{in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]},
				};

				vertex.prev_pos[0] = in_vertex->position[0];
				vertex.prev_pos[1] = in_vertex->position[1];
				vertex.prev_pos[2] = in_vertex->position[2];

				// Compute texture coordinates, process tangent
				{
					vec4_t svec, tvec;
					if (psurf && (psurf->flags & Patch_Surface_TexMatrix)) {
						svec[0] = surf->texinfo->vecs[0][0] * psurf->texmat_s[0] + surf->texinfo->vecs[1][0] * psurf->texmat_s[1];
						svec[1] = surf->texinfo->vecs[0][1] * psurf->texmat_s[0] + surf->texinfo->vecs[1][1] * psurf->texmat_s[1];
						svec[2] = surf->texinfo->vecs[0][2] * psurf->texmat_s[0] + surf->texinfo->vecs[1][2] * psurf->texmat_s[1];
						svec[3] = surf->texinfo->vecs[0][3] + psurf->texmat_s[2];

						tvec[0] = surf->texinfo->vecs[0][0] * psurf->texmat_t[0] + surf->texinfo->vecs[1][0] * psurf->texmat_t[1];
						tvec[1] = surf->texinfo->vecs[0][1] * psurf->texmat_t[0] + surf->texinfo->vecs[1][1] * psurf->texmat_t[1];
						tvec[2] = surf->texinfo->vecs[0][2] * psurf->texmat_t[0] + surf->texinfo->vecs[1][2] * psurf->texmat_t[1];
						tvec[3] = surf->texinfo->vecs[1][3] + psurf->texmat_t[2];
					} else {
						Vector4Copy(surf->texinfo->vecs[0], svec);
						Vector4Copy(surf->texinfo->vecs[1], tvec);
					}

					const float s = DotProduct( in_vertex->position, svec ) + svec[3];
					const float t = DotProduct( in_vertex->position, tvec ) + tvec[3];

					vertex.gl_tc[0] = s / surf->texinfo->texture->width;
					vertex.gl_tc[1] = t / surf->texinfo->texture->height;

					VectorCopy(svec, tangent);
					VectorNormalize(tangent);

					// "Inverted" texture mapping should not lead to inverted tangent/normal map
					// Make sure that orientation is preserved.
					{
						vec4_t stnorm;
						CrossProduct(tvec, svec, stnorm);
						if (DotProduct(stnorm, surf_normal) < 0.)
							VectorNegate(tangent, tangent);
					}
				}

				// lightmap texture coordinates
				{
					float s = DotProduct( in_vertex->position, info->lmvecs[0] ) + info->lmvecs[0][3];
					s -= info->lightmapmins[0];
					s += surf->light_s * sample_size;
					s += sample_size * 0.5f;
					s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

					float t = DotProduct( in_vertex->position, info->lmvecs[1] ) + info->lmvecs[1][3];
					t -= info->lightmapmins[1];
					t += surf->light_t * sample_size;
					t += sample_size * 0.5f;
					t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

					vertex.lm_tc[0] = s;
					vertex.lm_tc[1] = t;
				}

				// Compute smoothed normal if needed
				if (!getSmoothedNormalFor(args.mod, vertex_index, surface_index, vertex.normal)) {
					VectorCopy(surf_normal, vertex.normal);
				}

				VectorCopy(tangent, vertex.tangent);

				Vector4Set(vertex.color, 255, 255, 255, 255);

				*(p_vert++) = vertex;

				// Ray tracing apparently expects triangle list only (although spec is not very clear about this kekw)
				if (k > 1) {
					*(p_ind++) = (uint16_t)(vertex_offset + 0);
					*(p_ind++) = (uint16_t)(vertex_offset + k - 1);
					*(p_ind++) = (uint16_t)(vertex_offset + k);
					index_count += 3;
					index_offset += 3;
				}
			} // for surf->numedges

			model_geometry->element_count = index_count;
			vertex_offset += surf->numedges;
		} // for mod->nummodelsurfaces
	}

	ASSERT(args.sizes.num_surfaces == num_geometries);
	ASSERT(args.sizes.animated_count == animated_count);
	return true;
}

static qboolean createRenderModel( const model_t *mod, vk_brush_model_t *bmodel, const model_sizes_t sizes, qboolean is_worldmodel ) {
	bmodel->geometry = R_GeometryRangeAlloc(sizes.num_vertices, sizes.num_indices);
	if (!bmodel->geometry.block_handle.size) {
		ERR("Cannot allocate geometry for %s", mod->name );
		return false;
	}

	vk_render_geometry_t *const geometries = Mem_Malloc(vk_core.pool, sizeof(vk_render_geometry_t) * sizes.num_surfaces);
	bmodel->surface_to_geometry_index = Mem_Malloc(vk_core.pool, sizeof(int) * mod->nummodelsurfaces);
	bmodel->animated_indexes = Mem_Malloc(vk_core.pool, sizeof(int) * sizes.animated_count);
	bmodel->animated_indexes_count = sizes.animated_count;

	if (sizes.animated_count > MAX_ANIMATED_TEXTURES) {
		WARN("Too many animated textures %d for model \"%s\" some surfaces can be static", sizes.animated_count, mod->name);
	}

	const r_geometry_range_lock_t geom_lock = R_GeometryRangeLock(&bmodel->geometry);

	const qboolean fill_result = fillBrushSurfaces((fill_geometries_args_t){
			.mod = mod,
			.bmodel = bmodel,
			.sizes = sizes,
			.base_vertex_offset = bmodel->geometry.vertices.unit_offset,
			.base_index_offset = bmodel->geometry.indices.unit_offset,
			.out_geometries = geometries,
			.out_vertices = geom_lock.vertices,
			.out_indices = geom_lock.indices,
			.is_worldmodel = is_worldmodel,
		});

	R_GeometryRangeUnlock( &geom_lock );

	if (!fill_result) {
		// TODO unlock and free buffers if failed? Currently we can't free geometry range, as it is being implicitly referenced by staging queue. Flush staging and free?
		// This shouldn't really happen btw, kind of unrecoverable for now tbh.
		// Also, we might just handle it, as the only reason it can fail is 16 bit index overflow.
		// I. Split into smaller geometries sets.
		// II. Make indices 32 bit
		return false;
	}

	if (!R_RenderModelCreate(&bmodel->render_model, (vk_render_model_init_t){
		.name = mod->name,
		.geometries = geometries,
		.geometries_count = sizes.num_surfaces,
		.dynamic = false,
		})) {
		ERR("Could not create render model for brush model %s", mod->name);
		return false;
	}

	return true;
}

qboolean R_BrushModelLoad( model_t *mod, qboolean is_worldmodel ) {
	if (mod->cache.data) {
		WARN("Model %s was already loaded", mod->name );
		return true;
	}

	DEBUG("%s: %s flags=%08x", __FUNCTION__, mod->name, mod->flags);

	vk_brush_model_t *bmodel = Mem_Calloc(vk_core.pool, sizeof(*bmodel));
	ASSERT(g_brush.models_count < COUNTOF(g_brush.models));
	g_brush.models[g_brush.models_count++] = bmodel;

	bmodel->engine_model = mod;
	mod->cache.data = bmodel;

	Matrix4x4_LoadIdentity(bmodel->prev_transform);
	bmodel->prev_time = gpGlobals->time;

	const model_sizes_t sizes = computeSizes( mod, is_worldmodel );

	if (sizes.num_surfaces != 0) {
		if (!createRenderModel(mod, bmodel, sizes, is_worldmodel)) {
			ERR("Could not load brush model %s", mod->name);
			// FIXME Cannot deallocate bmodel as we might still have staging references to its memory
			return false;
		}
	}

	if (sizes.water.surfaces) {
		if (!brushCreateWaterModel(mod, &bmodel->water, sizes.water, BrushSurface_Water, is_worldmodel)) {
			ERR("Could not load brush water model %s", mod->name);
			// FIXME Cannot deallocate bmodel as we might still have staging references to its memory
			return false;
		}
	}

	if (sizes.side_water.surfaces) {
		if (!brushCreateWaterModel(mod, &bmodel->water_sides, sizes.side_water, BrushSurface_WaterSide, is_worldmodel)) {
			ERR("Could not load brush water_side model %s", mod->name);
			// FIXME Cannot deallocate bmodel as we might still have staging references to its memory
			return false;
		}
	}

	g_brush.stat.total_vertices += sizes.num_indices + sizes.water.vertices + sizes.side_water.vertices;
	g_brush.stat.total_indices += sizes.num_vertices + sizes.water.indices + sizes.side_water.indices;

	DEBUG("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u",
		mod->name, bmodel->render_model.num_geometries, mod->nummodelsurfaces, g_brush.stat.total_vertices, g_brush.stat.total_indices);

	return true;
}

static void R_BrushModelDestroy( vk_brush_model_t *bmodel ) {
	ASSERT(bmodel->engine_model);

	DEBUG("%s: %s", __FUNCTION__, bmodel->engine_model->name);

	ASSERT(bmodel->engine_model->cache.data == bmodel);
	ASSERT(bmodel->engine_model->type == mod_brush);

	if (bmodel->water.surfaces_count) {
		R_RenderModelDestroy(&bmodel->water.render_model);
		Mem_Free((int*)bmodel->water.surfaces_indices);
		Mem_Free(bmodel->water.render_model.geometries);
		R_GeometryRangeFree(&bmodel->water.geometry);
	}

	if (bmodel->water_sides.surfaces_count) {
		R_RenderModelDestroy(&bmodel->water_sides.render_model);
		Mem_Free((int*)bmodel->water_sides.surfaces_indices);
		Mem_Free(bmodel->water_sides.render_model.geometries);
		R_GeometryRangeFree(&bmodel->water_sides.geometry);
	}

	R_RenderModelDestroy(&bmodel->render_model);

	if (bmodel->animated_indexes)
		Mem_Free(bmodel->animated_indexes);

	if (bmodel->surface_to_geometry_index)
		Mem_Free(bmodel->surface_to_geometry_index);

	if (bmodel->render_model.geometries) {
		Mem_Free(bmodel->render_model.geometries);
		R_GeometryRangeFree(&bmodel->geometry);
	}

	bmodel->engine_model->cache.data = NULL;
	Mem_Free(bmodel);
}

void R_BrushModelDestroyAll( void ) {
	DEBUG("Destroying %d brush models", g_brush.models_count);
	for( int i = 0; i < g_brush.models_count; i++ )
		R_BrushModelDestroy(g_brush.models[i]);

	g_brush.stat.total_vertices = 0;
	g_brush.stat.total_indices = 0;
	g_brush.models_count = 0;

	memset(g_brush.conn.edges, 0, sizeof(*g_brush.conn.edges) * g_brush.conn.edges_capacity);

	memset(g_brush.conn.vertices, 0, sizeof(*g_brush.conn.vertices) * g_brush.conn.vertices_capacity);
}

static rt_light_add_polygon_t loadPolyLight(const model_t *mod, const int surface_index, const msurface_t *surf, const vec3_t emissive) {
	rt_light_add_polygon_t lpoly = {0};
	lpoly.num_vertices = Q_min(7, surf->numedges);

	// TODO split, don't clip
	if (surf->numedges > 7)
		WARN_THROTTLED(10, "emissive surface %d has %d vertices; clipping to 7", surface_index, surf->numedges);

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

void R_VkBrushModelCollectEmissiveSurfaces( const struct model_s *mod, qboolean is_worldmodel ) {
	vk_brush_model_t *const bmodel = mod->cache.data;
	ASSERT(bmodel);

	const xvk_mapent_func_any_t *func_any = getModelFuncAnyPatch(mod);
	const qboolean is_static = is_worldmodel || (func_any && func_any->origin_patched);

	typedef struct {
		int model_surface_index;
		int surface_index;
		const msurface_t *surf;
		vec3_t emissive;
	} emissive_surface_t;
	emissive_surface_t emissive_surfaces[MAX_SURFACE_LIGHTS];
	int geom_indices[MAX_SURFACE_LIGHTS];
	int emissive_surfaces_count = 0;

	// Load list of all emissive surfaces
	for( int i = 0; i < mod->nummodelsurfaces; ++i) {
		const int surface_index = mod->firstmodelsurface + i;
		const msurface_t *surf = mod->surfaces + surface_index;

		switch (getSurfaceType(surf, surface_index, is_worldmodel)) {
		case BrushSurface_Regular:
		case BrushSurface_Animated:
			break;
		default:
			continue;
		}

		const int tex_id = surf->texinfo->texture->gl_texturenum; // TODO animation?

		vec3_t emissive;
		const xvk_patch_surface_t *const psurf = R_VkPatchGetSurface(surface_index);
		if (psurf && (psurf->flags & Patch_Surface_Emissive)) {
			VectorCopy(psurf->emissive, emissive);
		} else if (RT_GetEmissiveForTexture(emissive, tex_id)) {
			// emissive
		} else {
			// not emissive, continue to the next
			continue;
		}

		DEBUG("%d: i=%d surf_index=%d tex_id=%d patch=%d(%#x) => emissive=(%f,%f,%f)", emissive_surfaces_count, i, surface_index, tex_id, !!psurf, psurf?psurf->flags:0, emissive[0], emissive[1], emissive[2]);

		if (emissive_surfaces_count == MAX_SURFACE_LIGHTS) {
			ERR("Too many emissive surfaces for model %s: max=%d", mod->name, MAX_SURFACE_LIGHTS);
			break;
		}

		emissive_surface_t* const surface = &emissive_surfaces[emissive_surfaces_count++];
		surface->model_surface_index = i;
		surface->surface_index = surface_index;
		surface->surf = surf;
		VectorCopy(emissive, surface->emissive);
	}

	// Clear old per-geometry emissive values. The new emissive values will be assigned by the loop below only to the relevant geoms
	for (int i = 0; i < bmodel->render_model.num_geometries; ++i) {
		vk_render_geometry_t *const geom = bmodel->render_model.geometries + i;
		VectorClear(geom->emissive);
	}

	// Non-static brush models may move around and so must have their emissive surfaces treated as dynamic
	if (!is_static) {
		if (bmodel->render_model.dynamic_polylights)
			Mem_Free(bmodel->render_model.dynamic_polylights);
		bmodel->render_model.dynamic_polylights_count = emissive_surfaces_count;
		bmodel->render_model.dynamic_polylights = Mem_Malloc(vk_core.pool, sizeof(bmodel->render_model.dynamic_polylights[0]) * emissive_surfaces_count);
	}

	// Apply all emissive surfaces found
	for (int i = 0; i < emissive_surfaces_count; ++i) {
		const emissive_surface_t* const s = emissive_surfaces + i;

		rt_light_add_polygon_t polylight = loadPolyLight(mod, s->surface_index, s->surf, s->emissive);

		// func_any surfaces do not really belong to BSP+PVS system, so they can't be used
		// for lights visibility calculation directly.
		if (func_any && func_any->origin_patched) {
			// TODO this is not really dynamic, but this flag signals using MovingSurface visibility calc
			polylight.dynamic = true;
			matrix3x4 m;
			Matrix3x4_LoadIdentity(m);
			Matrix3x4_SetOrigin(m, func_any->origin[0], func_any->origin[1], func_any->origin[2]);
			polylight.transform_row = &m;
		}

		// Static emissive surfaces are added immediately, as they are drawn all the time.
		// Non-static ones will be applied later when the model is actually rendered
		if (is_static) {
			RT_LightAddPolygon(&polylight);
		} else {
			bmodel->render_model.dynamic_polylights[i] = polylight;
		}

		// Assign the emissive value to the right geometry
		const int geom_index = bmodel->surface_to_geometry_index[s->model_surface_index];
		geom_indices[i] = geom_index;
		VectorCopy(polylight.emissive, bmodel->render_model.geometries[geom_index].emissive);
	}

	if (emissive_surfaces_count > 0) {
		// Update emissive values in kusochki. This is required because initial R_BrushModelLoad happens before we've read
		// RAD data in vk_light.c, so the emissive values are empty. This is the place and time where we actually get to
		// know them, so let's fixup things.
		// TODO minor optimization: sort geom_indices to have a better chance for them to be sequential

		{
			// Make sure that staging has been flushed.
			// Updating materials leads to staging an upload to the same memory that we've just staged an upload to.
			// This doesn't please the validator.
			// Ensure that these uploads are not mixed into the same unsynchronized stream.
			// TODO this might be not great for performance (extra waiting for GPU), so a better solution should be considered. E.g. tracking and barrier-syncing regions to-be-reuploaded.
			R_VkStagingFlushSync();
		}

		R_RenderModelUpdateMaterials(&bmodel->render_model, geom_indices, emissive_surfaces_count);
		INFO("Loaded %d polylights for %s model %s", emissive_surfaces_count, is_static ? "static" : "movable", mod->name);
	}
}

void R_BrushUnloadTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx || tx->gl_texturenum == tglob.defaultTexture )
			continue; // free slot

		R_TextureFree( tx->gl_texturenum );    // main texture
		R_TextureFree( tx->fb_texturenum );    // luma texture
	}
}
