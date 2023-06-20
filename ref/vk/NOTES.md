# Frame structure wrt calls from the engine
- (eng) SCR_UpdateScreen()
	- (eng) V_PreRender()
		- **(ref) R_BeginFrame()**
	- (eng) V_RenderView()
		- **(ref) GL_BackendStartFrame()** -- ref_gl only sets speeds string to empty here
			- (eng) loop over ref_params_t views
				- **(ref) GL_RenderFrame()**
			- (eng) ??? SV_DrawDebugTriangles()
		- **(ref) GL_BackendEndFrame()** -- ref_gl only produces speeds string here
	- (eng) V_PostRender()
		- **(ref) R_AllowFog(), R_Set2DMode(true)**
		- **(ref) R_DrawTileClear()** x N
		- (vgui) Paint() -> ???
		- (eng) SCR_RSpeeds()
			- **(ref) R_SpeedsMessage()**
			- (eng) CL_DrawString() ...
			  - **(ref) GL_SetRenderMode()**
				- **(ref) RefGetParm()** for texture resolution
				- **(ref) Color4ub()**
				- **(ref) R_DrawStretchPic()**
		- (eng) SRC_DrawNetGraph()
			- **(ref) many TriApi calls** -- 2D usage of triapi. we were not ready for this (maybe track R_Set2DMode()?)
		- **(ref) R_ShowTextures()** kekw
		- **(ref) VID_ScreenShot()**
		- **(ref) R_AllowFog(true)**
		- **(ref) R_EndFrame()**

# Staging and multiple command buffers
We want to get rid of extra command buffers used for staging (and building blases). That would mean tying any command-buffer related things in there to framectl.
However, there are several staging cmdbuf usages which are technically out-of-band wrt framectl:
0. Staging data can get full, which requires sync flush: filling cmdbuf outside of frame (or while still building a frame), submitting it and waiting on it.
1. Texture uploading. There's an explicit usage of staging cmdbuf in vk_texture to do layout transfer. This layout transfer can be moved to staging itself.
2. BLAS building. Creating a ray model uploads its geometry via staging and then immediately builds its BLAS on the same staging cmdbuf. Ideally(?), we'd like to split BLAS building to some later stage to do it in bulk.

# OpenGL-like immediate mode rendering, ~TriApi
## Functions:
	R_Set2DMode(bool) -- switches between 3D scene and 2D overlay modes; used in engine
	R_DrawStretchRaw,
	R_DrawStretchPic,
	R_DrawTileClear,
	CL_FillRGBA,
	CL_FillRGBABlend,

	R_AllowFog,
	GL_SetRenderMode,

	void		(*GL_Bind)( int tmu, unsigned int texnum );
	void		(*GL_SelectTexture)( int tmu );

	void		(*GL_LoadTextureMatrix)( const float *glmatrix ); -- exported to the game, not used in engine
	void		(*GL_TexMatrixIdentity)( void ); -- exported to the game, not used in engine

	void		(*GL_CleanUpTextureUnits)( int last );	// pass 0 for clear all the texture units
	void		(*GL_TexGen)( unsigned int coord, unsigned int mode );
	void		(*GL_TextureTarget)( unsigned int target ); // change texture unit mode without bind texture
	void		(*GL_TexCoordArrayMode)( unsigned int texmode );
	void		(*GL_UpdateTexSize)( int texnum, int width, int height, int depth ); // recalc statistics

	TriRenderMode,
	TriBegin,
	TriEnd,
	TriColor4f,
	TriColor4ub,
	TriTexCoord2f,
	TriVertex3fv,
	TriVertex3f,
	TriFog,
	TriGetMatrix,
	TriFogParams,
	TriCullFace,


# Better BLAS management API

~~
BLAS:
- geom_count => kusok.geom/material.size() == geom_count

Model types:
1. Fully static (brush model w/o animated textures; studio model w/o animations): singleton, fixed geoms and materials, uploaded only once
2. Semi-static (brush model w/ animated textures): singleton, fixed geoms, may update materials, inplace (e.g. animated textures)
3. Dynamic (beams, triapi, etc): singleton, may update both geoms and materials, inplace
4. Template (sprites): used by multiple instances, fixed geom, multiple materials (colors, textures etc) instances/copies
5. Update-from template (studo models): used by multiple dynamic models, deriving from it wvia BLAS UPDATE, dynamic geom+locations, fixed-ish materials.

API ~
1. RT_ModelCreate(geometries_count dynamic?static?) -> rt_model + preallocated mem
2. RT_ModelBuild/Update(geometries[]) -> (blas + kusok.geom[])
3. RT_ModelUpdateMaterials(model, geometries/textures/materials[]); -> (kusok.material[])
4. RT_FrameAddModel(model + kusok.geom[] + kusok.material[] + render_type + xform + color)
~~


rt_instance_t/rt_blas_t:
- VkAS blas
	- VkASGeometry geom[] -> (vertex+index buffer address)
	- VkASBuildRangeInfo ranges[] -> (vtxidx buffer offsets)
	- ~~TODO: updateable: blas[2]? Ping-pong update, cannot do inplace?~~ Nope, can do inplace.
- kusochki
	- kusok[]
		- geometry -> (vtxidx buffer offsets)
			- TODO roughly the same data as VkASBuildRangeInfo, can reuse?
		- material (currently embedded in kusok)
			- static: tex[], scalar[]
			- semi-dynamic:
				- (a few) animated tex_base_color
				- emissive
					- animated with tex_base_color
					- individual per-surface patches
			- TODO: extract as a different modality not congruent with kusok data

Usage cases for the above:
1. (Fully+semi) static.
  - Accept geom[] from above with vtx+idx refernces. Consider them static.
	- Allocate static/fixed blas + kusok data once at map load.
	- Allocate geom+ranges[] temporarily. Fill them with vtx+idx refs.
	- Build BLAS (?: how does this work with lazy/deferred BLAS building wrt geom+ranges allocation)
		- Similar to staging: collect everything + temp data, then commit.
		- Needs BLAS manager, similar to vk_staging
	- Generate Kusok data with current geoms and materials
	- Free geom+ranges
	- Each frame:
		- (semi-static only) Update kusochki materials for animated textures
		- Add blas+kusochki_offset (+dynamic color/xform/mmode) to TLAS
2. Preallocated dynamic (triapi)
  - Preallocate for fixed N geoms:
		- geom+ranges[N].
		- BLAS for N geometries
		- kusochki[N]
	- Each frame:
		- Fill geom+ranges with geom data fed from outside
		- Fill kusochki --//--
		- Fast-Build BLAS as new
		- Add to TLAS
3. Dynamic with update (animated studio models, beams)
	- When a new studio model entity is encountered:
		- Allocate:
			- AT FIXED OFFSET: vtx+idx block
			- geom+ranges[N], BLAS for N, kusochki[N]
	- Each frame:
		- Fill geom+ranges with geom data
		- Fill kusochki --//--
		- First frame: BLAS as new
		- Next frames: UPDATE BLAS in-place (depends on fixed offsets for vtx+idx)
		- Add to TLAS
4. Instanced (sprites, studio models w/o animations).
	- Same as static, BUT potentially dynamic and different materials. I.e. have to have per-instance kusochki copies with slightly different material contents.
	- I.e. each frame
		- If modifying materials (e.g. different texture for sprites):
			- allocate temporary (for this frame only) kusochki block
			- fill geom+material kusochki data
		- Add to TLAS w/ correct kusochki offset.

Exposed ops:
- Create BLAS for N geoms
- Allocate kusochki[N]
	- static (fixed pos)
	- temporary (any location, single frame lifetime)
- Fill kusochki
	- All geoms[]
	- Subset of geoms[] (animated textures for static)
- Build BLAS
	- Allocate geom+ranges[N]
		- Single frame staging-like?
		- Needed only for BLAS BUILD/UPDATE
	- from geoms+ranges[N]
	- build vs update
- Add to TLAS w/ color/xform/mmode/...

- geometry_buffer -- vtx+idx static + multi-frame dynamic + single-frame dynamic
- kusochki_buffer -- kusok[] static + dynamic + clone_dynamic
- accel_buffer -- static + multiframe dynamic + single-frame dynamic
- scratch_buffer - single-frame dynamic
- model_buffer - single-frame dynamic

# E268: explicit kusochki management
Kusochki buffer has a similar lifetime rules to geometry buffer
Funcs:
- Allocate kusochki[N] w/ static/long lifetime
- Allocate dynamic (single-frame) kusochki[N]
- Upload geom[N] -> kusochki[N]
- Upload subset geom[ind[M] -> kusochki[M]

# E269

RT model alloc:
- blas -- fixed
	- accel buffer region -- fixed
	- (scratch: once for build)
	- (geoms: once for build)
- -> geometry buffer -- fixed
- kusochki[G]: geometry data -- fixed
- materials[G]: -- fixed

RT model update:
- lives in the same statically allocated blas + accel_buffer
-

RT model draw:
- mmode
- materials[G] -- can be fully static, partially dynamic, fully dynamic
	- update inplace for most of dynamic things
	- clone for instanced
- color
- transforms

## Blocks
### Layer 0: abstract, not backing-dependent
	handle = R_BlockAlloc(int size, lifetime);
	- block possible users: {accel buffer, geometry, kusochki, materials};
	- lifetime
		- long: map, N frames: basically everything)
		- once = this frame only: sprite materials, triapi geometry/kusochki/materials
	- handle: offset, size
	- R_BlockAcquire/Release(handle);
	- R_BlocksClearOnce(); -- frees "once" regions, checking that they are not referenced
	- R_blocksClearFull(); -- clears everything, checking that there are not external references

### Layer 1: backed by buffer
- lock = R_SmthLock(handle, size, offset)
	- marks region/block as dirty (cannot be used by anything yet, prevents release, clear, etc.),
	- opens staging regiong for filling and uploading
- R_SmthUnlock(lock)
	- remembers dirty region (for barriers)
	- submits into staging queue
- ?? R_SmthBarrier -- somehow ask for the barrier struct given pipelines, etc

# E271

## Map loading sequence
1. For a bunch of sprites:
	1. Load their textures
	2. Mod_ProcessRenderData(spr, create=1)
2. "loading maps/c1a0.bsp" message
	1. Load a bunch of `#maps/c1a0.bsp:*.mip` textures
	2. Mod_ProcessRenderData(maps/c1a0.bsp, create=1)
3. For studio models:
	1. Load their textures
	2. Mod_ProcessRenderData(mdl, create=1)
4. "level loaded at 0.31 sec" message
5. 1-2 frames drawn (judging by vk swapchain logs)
6. Do another bunch of sprites (as #1)
7. Lightstyles logs
8. "Setting up renderer..." message
9. R_NewMap() is called
	1. (vk) load skybox
	2. (vk) extract WADs, parse entities
	3. (vk) parse materials
	4. (vk) parse patches
	5. (vk) load models
		1. load brush models
		2. skip studio and sprite models
	6. (vk) load lights: parse rad files, etc
10. "loading model/scientist02.mdl"
11. Load 640_pain.spr ???, Mod_ProcessRenderData() first, then textures ??

## Map unloading sequence
1. Mod_ProcessRenderData(maps/c1a0.bps, create=0)
	- NO similar calls for `*` brush submodels.
2. For the rest of studio and sprite models:
	- Mod_ProcessRenderData(create=0)

# E274

rt_model:
	- kusok/geom
		- index_,vertex_offset (static, same as geom/blas lifetime)
		- ref to material (static or dynamic)
		- emissive (mostly static, independent to material)
	- instanceCustomIndex (24 bits) = offset to kusochki buffer
	- kusochki[G]
		- geom data (index, vertex offsets)
		- emissive
		- material
	- materials[M]
  - kusochki[N] <- iCI


# E275 studio models

- `R_StudioDrawPoints()`
	- `VK_RenderModelDynamicBegin()`
	- compute `g_studio.verts`
		- in:
			- `m_pSubModel`
			- `m_pStudioHeader`
			- `g_studio.worldtransform`
	- `R_StudioBuildNormalTable()` ...
	- `R_StudioGenerateNormals()`
		- in:
			- `m_pStudioHeader`
			- `m_pSubModel`
			- `g_studio.verts`
		- out:
			- `g_studio.norms`
			- `g_studio.tangents`
		- for all submodel meshes
			- compute normals+tangents
	- for all submodel meshes
		- `R_StudioDrawNormalMesh()`
			- `R_GeometryBufferAllocOnceAndLock()`
			- fills it with vertex/index data, reading `g_studio.verts/norms/tangents/...`
				- `R_StudioSetColorBegin()` ???
			- `R_GeometryBufferUnlock()`
			- `VK_RenderModelDynamicAddGeometry()`
	- `VK_RenderModelDynamicCommit()`

- `R_StudioDrawPoints()` callers:
	- external ???
	- `R_StudioRenderFinal()`

- `R_StudioRenderFinal()`
	- ... TBD
	- `VK_RenderDebugLabelBegin()`
	- for all `m_pStudioHeader->numbodyparts`
		- `R_StudioSetupModel()` -- also can be called externally
			- set `m_pBodyPart`
			- set `m_pSubModel`
		- `R_StudioDrawPoints()`
		- `GL_StudioDrawShadow()`
	- `VK_RenderDebugLabelEnd()`

- `R_StudioDrawModelInternal()`
	- called from:
		- `R_DrawStudioModel()` 3x
		- `R_DrawViewModel()`
		- `R_RunViewmodelEvents()`
	- `VK_RenderDebugLabelBegin()`
	- `R_StudioDrawModel()`
		- in:
			- `RI.currententity`
			- `RI.currentmodel`
		- `R_StudioSetHeader()`
			- sets `m_pStudioHeader`
		- `R_StudioSetUpTransform(entity = RI.currententity)`
			- `R_StudioLerpMovement(entity)`
				- updates entity internal state
			- `g_studio.rotationmatrix = Matrix3x4_CreateFromEntity()`
	- `VK_RenderDebugLabelEnd()`

- `VK_StudioDrawModel()` -- called from vk_scene.c
	- sets `RI.currententity`, `RI.currentmodel`
	- `R_DrawStudioModel()`
		- `R_StudioSetupTimings()` -- sets `g_studio.time/frametime`
		- `R_StudioDrawModelInternal()`

# E279
## Studio model animation
- studiohdr_t
	- int numseq -- number of "sequences"?
	- int seqindex -- offset to sequences:
			`pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + sequence;`
- mstudioseqdesc_t
	- int numframes
	- int fps
- mstudioanim_t
	- = gEngine.R_StudioGetAnim(studiohdr, model, seqdesc)

- cl_entity_t
	- sequence -- references studio model sequence
	- animtime/frame -- references animation state within sequence

# E282
## Studio model tracking
`m_pStudioHeader` is set from:
- `R_StudioSetHeader()` from:
	- EXTERNAL
	- `R_StudioDrawModel()`
	- `R_StudioDrawPlayer()`
- `R_StudioDrawPlayer()`

## Detecting static/unchanged studio submodels
### Parse `studiohdr_t` eagerly
Go deeply into sequences, animations, etc and figure out whether vertices will actually change.
Might not catch models which are not being animated right now, i.e. current frame is the same as previous one, altough it is not guaranteed to be so.
This potentially conflicts with game logic updating bonetransforms manually even though there are no recorded animations in studio file.

### Detect changes dynamically
Let it process vertices as usual, but then compute hash of vertices values.
Depends on floating point vertices coordinates being bit-perfect same every time, even for moving entities. This is not strictly speaking true because studio model rendering is organized in such a way that bone matrices are pre-multiplied by entity transform matrix. This is done outside of vk_studio.c, and in game dll,which we have no control over. We then undo this multiplication. Given floating point nature of all of this garbage, there will be precision errors and resulting coordinates are not guaranteed to be the same even for completely static models.

### Lazily detect static models, and draw the rest as fully dynamic with fast build
- Detect simple static cases (one sequence, one frame), and pre-build those.
- For everything else, just build it from scratch every frame w/o caching or anything.
If that is not fast enough, then we can proceed with more involved per-entity caching, BLAS updates, cache eviction, etc.

TODO: can we not have a BLAS/model for each submodel? Can it be per-model instead? This would need prior knowledge of submodel count, mesh count, vertices and indices counts. (Potentially conflicts with game dll doing weird things, e.g. skipping certain submodels based on whatever game specific logic)

### Action plan
- [ ] Try to pre-build static studio models. If fails (e.g. still need dynamic knowledge for the first build), then build it lazily, i.e. when the model is rendered for the first time.
	- [ ] Needs tracking of model cache entry whenever `m_pStudioHeader` is set.
- [ ] Add a cache for entities, store all prev_* stuff there.
	- [ ] Needs tracking of entity cache entry whenever `RI.currententity` is set.

- [ ] Alternative model/entity tracking: just check current ptrs in `R_StudioDrawPoints()` and update them if changed.
