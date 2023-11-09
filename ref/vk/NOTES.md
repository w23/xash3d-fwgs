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

# 2023-07-30
- ~~R_DrawStudioModel is the main func for drawing studio model. Called from scene code for each studio entity, with everything current (RI and stuff) set up~~
- `R_StudioDrawModelInternal()` is the main one. It is where it splits into renderer-vs-game rendering functions.

# 2023-09-11 E293
- light shaders include structure
- ray_light_direct_{poly,point}.comp
	- ray_light_direct.glsl
		- utils.glsl
		- noise.glsl
		- ray_interop.h
		- ray_kusochki.glsl
		- light.glsl
			- brdf.h
			- light_common.glsl
			- LIGHT_POLYGON: light_polygon.glsl

# 2023-09-19 E298
## SURF_DRAWSKY
- (context: want to remove kXVkMaterialSky, #474)
- qrad:
    - uses textue name "sky" or "SKY" to check `IsSky()`. `IsSky()` surfaces do not get patches and do not participate in radiosity.
    - uses CONTENTS_SKY node flag to detect whether a ray has hit skybox and can contribute sky light.
- xash/gl:
    - CONTENTS_SKY is not used in any meaningful way
    - sets SURF_DRAWSKY for surfaces with "sky" texture.
    - uses SURF_DRAWSKY:
        - to build skychain, and then draw it in Quake mode (the other branch does a bunch of math, which seemingly isn't used for anything at all).
        - for dynamic lighting: if sky ray has hit sky surface then sky is contributing light

# 2023-09-25 #301
## Materials format
Define new material, independently of any existing textures, etc
This can be .vmat compatible, primext compatbile, etc.
The important parts:
- It has a unique name that we can reference it with
- It has all the fields that we use for our PBR shading model
- (? Material mode can be specified)
```
{
	"material" "MAT_NAME"
	"map_base_color" "base.png"
	"map_normal" "irregular.ktx2"
	"base_color" "1 .5 0"
	// ...
}

{
	"material" "mirror"
    "map_base_color" "white"
    "base_color" "1 1 1"
    "roughness" "0"
    "metalness" "1"
    // ...
}
```

Then, we can map existing textures to new materials:
```
{
	"for_texture" "+EXIT"
    "use" "MAT_NAME"
}
```

Or, with more context:
```
{
    "for_model_type" "brush"
    "for_rendermode" "kRenderTransAlpha"
    "for_texture" "wood"
    "use" "mat_glass"
    "mode" "translucent"
    "map_base_color" "glass2.ktx"
}

// ??? meh, see the better _xvk_ example below
{
    "for_model_type" "brush"
    "for_surface_id" "584"
    "use" "mirror"
}

// This example: use previously specified material (e.g. via _xvk stuff below)
// (Depends on applying multiple matching rules, see questions below)
{
    "for_model_type" "brush"
    "for_rendermode" "kRenderTransAlpha"
    "mode" "translucent"
    "map_normal" "glass2.ktx"
}

// We also want this (for maps, not globally ofc), see https://github.com/w23/xash3d-fwgs/issues/526
{
    "for_entity_id" "39"
    "for_texture" "generic028"
    "use" "generic_metal1"
}

{
    "for_entity_id" "39"
    "for_texture" "generic029"
    "use" "generic_metal2"
}
```

What it does is:
1. If all `"for_"` fields match, apply values from `"use"` material (in this case `"wood"`)
2. Additionally, override any extra fields/values with ones specified in this block

As we already have surface-patching ability, can just use that for patching materials directly for brush surfaces:
```
// mirror in toilet
{
    "_xvk_surface_id" "2057"
    "_xvk_material" "mirror"
}
```

Questions:
- Should it apply the first found rule that matches a given geometry and stop?
  Or should it apply updates to the material using all the rules that matched in their specified order? Doing the first rule and stopping is more readable and perofrmant, but also might be verbose in some cases.
- Should we do "automatic" materials? I.e. if there's no manually specified material for a texture named `"<TEX>"`, then we try to load `"<TEX>_basecolor.ktx"`, `"<TEX>_normalmap.ktx"`, etc automatically.

# 2023-09-26 E302
Map loading sequence
```
[2023:09:26|11:30:31] Couldn't open file overviews/c1a0d.txt. Using default values for overiew mode.
[2023:09:26|11:30:31] CL_SignonReply: 2
[2023:09:26|11:30:31] Signon network traffic:  10.380 Kb from server, 349 bytes to server
[2023:09:26|11:30:31] client connected at 0.07 sec
[2023:09:26|11:30:31] Error: SDL_GL_SetSwapInterval: No OpenGL context has been made current
[2023:09:26|11:30:31] vk: Mod_ProcessRenderData(sprites/640_pain.spr, create=1)

[2023:09:26|11:30:43] Loading game from save/autosave01.sav...
[2023:09:26|11:30:43] Spawn Server: c2a5
[2023:09:26|11:30:43] vk: Mod_ProcessRenderData(maps/c1a0d.bsp, create=0)

[2023:09:26|11:30:43] Warning: VK FIXME Trying to unload brush model maps/c1a0d.bsp
[2023:09:26|11:30:43] Error: VK NOT_IMPLEMENTED(x0): RT_KusochkiFree
[2023:09:26|11:30:43] loading maps/c2a5.bsp
[2023:09:26|11:30:43] Warning: FS_LoadImage: couldn't load "alpha_sky"
[2023:09:26|11:30:43] Warning: FS_LoadImage: couldn't load "solid_sky"
[2023:09:26|11:30:43] lighting: colored
[2023:09:26|11:30:43] Wad files required to run the map: "halflife.wad; liquids.wad; xeno.wad"
[2023:09:26|11:30:43] vk: Mod_ProcessRenderData(maps/c2a5.bsp, create=1)

[2023:09:26|11:30:43] Loading game from save/c2a5.HL1...
[2023:09:26|11:30:43]
GAME SKILL LEVEL:1
[2023:09:26|11:30:43] Loading CGraph in GRAPH_VERSION 16 compatibility mode
[2023:09:26|11:30:43] Loading CLink array in GRAPH_VERSION 16 compatibility mode
[2023:09:26|11:30:43]
*Graph Loaded!
[2023:09:26|11:30:43] **Graph Pointers Set!
[2023:09:26|11:30:43] loading sprites/flare1.spr
[2023:09:26|11:30:43] vk: Mod_ProcessRenderData(sprites/flare1.spr, create=1)
.. more Mod_ProcessRenderData
.. and only then R_NewMap
```

# 2023-09-28 E303
## #526
Replace textures for specific brush entities.
For a single texture it might be as easy as:
```
{
	"_xvk_ent_id" "39"
	"_xvk_texture" "generic028"
	"_xvk_material" "generic_metal1"
}
```

For multiple replacements:
0. Multiple entries
```
{
	"_xvk_ent_id" "39"
	"_xvk_texture" "generic028"
	"_xvk_material" "generic_metal1"
}

{
	"_xvk_ent_id" "39"
	"_xvk_texture" "generic029"
	"_xvk_material" "generic_metal2"
}
```

1. Pairwise
```
{
	"_xvk_ent_id" "39"
	"_xvk_texture" "generic028 generic029 ..."
	"_xvk_material" "generic_metal1 generic_metal2 ..."
}
```

2. Pair list <-- preferred
```
{
	"_xvk_ent_id" "39"
	"_xvk_texture_material" "generic028 generic_metal1 generic029 generic_metal2 ... ..."
}
```

# 2023-10-02 E305
## Materials table

### Operations
- Clean
- load materials from file
	- current format (mixes materials and selection rules a bit)
	- other formats (can only support named materials w/o any selection rules)
	- inherit/use from previously defined materials
		- needs index/value by name below
- Get materials by:
	- value by tex_id
	- value by tex_id + rendermode
	- value by tex_id + chrome
		- ~~(do we need to specialize "for_chrome"? were there any cases where it would be useful?)~~ It seems not.
	- index by name (currently works by having a dummy texture with this name; reuses vk_textures hash search)
	- Lazy: Getting by value performs loading, getting by index does not.

### Data structures overview
- materials[mat_id] -- indexed by indexes independent of tex_id, referenced externally, requires stable index.
	- (typical material fields)
		- possibly lazily loaded
			- arg `-vknolazymaterials` for development. To immediately recognize missing textues, not until they are requested for the first time.
			- fallback onto default/error texture on lazy loading errors
- tex_to_material[tex_id] table (hash table, array, whatever)
	- state -- {NotChecked, NoReplacement, ReplacementExists}
		- NotChecked -- means there was no explicit replacement, but we still can check for auto replacement (TEXNAME_roughness.png, etc)
		- NoReplacement -- there's nothing to replace with, use original texture with default material parameters.
	- mat_id -- index into materials[] table if there's a replacement
    - rendermodes-specific overrides (fixed array? too expensive; linked list?)
        - rendermode
        - mat_id
- name_to_material[] -- string "name" to mat_id
    - hash table of some sorts

# 2023-10-16 E313
## Pre-next:
- validation crash
## Next:
- KTX2 PR against upstream
- texture leaks
	- better texture storage
		- hash map
	- texture lifetimes/refcounts
	- texture deletion
		- mass (for single device wait idle)

# 2023-10-17 E314
- [x] imagelib/ktx2 PR to upstream
	1. [x] Make a vulkan branch with the latest upstream merged in
	2. [x] Make another branch `upstream-ktx2` from upstream/master with imagelib changes hand-picked
	3. [x] Make a PR against upstream with ktx2
	4. [x] Make a PR against vulkan with recentmost upstream

- [x] Contemplate texture storage

# 2023-10-19 E315
Tried refcounts. They only break things, many textures get released prematurely.
Hunch: need to split external/refapi refcount-unaware functionality and hash map into two things:
- old name->gl_texturenum table that is refcount-unaware
- new name->vk.image refcount-aware table and api

# 2023-10-20 E316
## Texture mgmt refcount impedance mismatch: losing textures on changelevel
1. ref_interface_t api is refcount-oblivious: it creates and destroys textures directly. Can't really update refcounts in these calls because they are not balanced at all. Can potentially call Load on the same texture twice, and then delete it only once. (Can it delete the same texture multiple times? Probably not -- need index, which *SHOULD* be inaccessible after the first delte by the API logic -- this is broken now with refcounts)
2. Sequence of events:
    1. Changlevel initiated
    2. Textures for the old map are deleted using ref_interface api
        - mostly us in ref_vk (so we can adjust), but there are a few possible calls from the engine and game.dll
        - brings refcount down to "1" (many textures are still referenced from materials)
    3. Texture for the new map are created using ref_interface api
        - There are common textures that weren't deleted due to being referenced by materials, so they are considered being uploaded already.
        - ref_interface_t is refcount-oblivious, so refcounts are not updated, i.e. remaining =1.
    4. ref_vk finally notices the changelevel, and starts reloading materials.
        - goes through the list of all old materials and releases all the textures
        - old textures (but which should've been loaded for the new map too) with refcount=1 are deleted
        - ;_;

# 2023-11-07 E326
Water :|

## Overview
- water is `msurface_t` with `SURF_DRAWTURB` (?)
- Engine calls `GL_SubdivideSurface()` to produce `glpoly_t` chain of subdivided polygons for a given `msurface_t`
	- When? How? Is it correct?
	- What does it do exactly?
- rendering uses `glpoly_t` to generate and submit heightmap-animated triangles
- animated height depends on current camera position. Height is inverted if camera is underwater.
- there are "water sides" with `PLANE_Z` flag. These are drawn only when `cl_entity_t.curstate.effects` has `EF_WATERSIDES` bit
	- water sides can be found in test_brush2

# 2023-11-09 E327
GL water vs test_brush2
- `EmitWaterPolys()`
    - is called by `R_RenderBrushPoly()`
        - if commented out, the inner sphere disappears, as all other water
        - is called from `R_DrawTextureChains()`
            - is called from `R_DrawWorld()`
    - is called by `R_DrawWaterSurfaces()`
        - commenting out doesn affect anything ?! -- doesn't seem to be called at all

- `PLANE_Z` check is in `R_DrawBrushModel()`

GL:
- default:          sphere=1 side=0
- no PLANE_Z check: sphere=1 side=0.5 (?? two sides missing)

VK:
- default:          sphere=0 side=0
- no PLANE_Z check: sphere=1 side=1

EXPLANATION: `!=PLANE_Z` is only culled for non-worldmodel entities. Worldmodel doesn't cull by != PLANE_Z.
