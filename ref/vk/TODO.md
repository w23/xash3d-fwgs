## Next

## Upcoming
- [ ] framectl frame tracking, e.g.:
	- [ ] wait for frame fence only really before actually starting to build combuf in R_BeginFrame()
		- why: there should be nothing to synchronize with
		- why: more straightforward dependency tracking
		- why not: waiting on frame fence allows freeing up staging and other temp memory
- [ ] Remove second semaphore from submit, replace it with explicit barriers for e.g. geom buffer
	- [x] why: best practice validation complains about too wide ALL_COMMANDS semaphore
	- why: explicit barriers are more clear, better perf possible too
	- [ ] Do not lose barrier-tracking state between frames
- [ ] Render graph
- [ ] performance profiling and comparison

## 2025-01-30 E389
- [ ] run render tests
- [ ] check diff wrt non-ref-vk changes
- [x] `PARM_TEX_FILTERING`
- [x] Fix some missing textures

## 2025-01-23 E387
- [x] (local) waf prefix shenanigans
- [x] merge resources branch into vulkan
  - [x] reverse-merge vulkan
  - [x] ghci upload s/v3/v4/
- [x] merge from upstream
- [ ] discuss further agenda

## 2024-12-17 E385
- [x] fix rendering on amdgpu+radv
### After stream
- [x] cleanup TLAS creation and building code

## 2024-12-12 E384
- [x] track image sync state with the image object itself (and not with vk_resource)

### After stream
- [x] Proper staging-vs-frame tracking, replace tag with something sensitive
	- currently assert fails because there's 1 frame latency, not one.
	- [x] comment for future: full staging might want to wait for previous frame to finish
- [x] zero vkCmdPipelineBarriers calls
	- [x] grep for anything else

## 2024-12-10 E383
- [x] Add transfer stage to submit semaphore separating command buffer: fixes sync for rt
- [x] Issue staging commit for a bunch of RT buffers (likely not all of them)
- [x] move destination buffer tracking to outside of staging:
	- [x] vk_geometry
	- [x] vk_light: grid, metadata
	- [x] vk_ray_accel: TLAS geometries
	- [x] vk_ray_model: kusochki
- [x] staging should not be aware of cmdbuf either
	- [x] `R_VkStagingCommit()`: -- removed
	- [x] `R_VkStagingGetCommandBuffer()` -- removed
- [x] Go through all staged buffers and make sure that they are committed
- [x] Commit staging in right places for right buffers
- [x] Add mode staging debug tracking/logs

### After stream
- [x] Fix glitch geometry
	- [x] Which specific models produce it? Use nsight btw

## 2024-05-24 E379
- [ ] refactor staging:
	- [ ] move destination image tracking to outside of staging
		- [x] vk_image ← vk_texture (E380)
		- [x] implement generic staging regions (E380)
		- [ ] implement stricter staging regions tracking

## 2024-05-07 E376
- [ ] resource manager
    - [x] extract all resource mgmt from vk_rtx into a designated file
    - [ ] register all resources in their modules
    - [ ] massage resource state tracking (read-write vs write-current; `value` field consistency, etc)

## 2024-04-12 E374
- [x] ~~`-vknort` arg to force-disable RT at init time~~ -- reverted on 2024-04-29

## 2024-03-21 E372: agonizig over agenda
### Player-visible essentials and blockers. Big projects.
- [ ] Light clusters, sampling, and performance -- 90fps HDR on a Steam Deck
- [ ] Transparency, refractions: glass, water, etc
- [ ] Moar and moar correct bounces
- [ ] Denoiser
- [ ] Decals
- [ ] Volumetrics and fog
- [ ] HDR and tonemapping

### Invisible blockers -- foundation/systems stuff
- [ ] Render graph and resource tracking -- track textures, buffers+regions ownership and usage, automatic barriers, etc.
- [ ] Modules and dependencies tracking
- [ ] Integrate rendertests into CI

### Small things
- [ ] Material patching refactoring: do not load any patched textures before they are referenced by the engine itself.
	Only load patched textures for the textures that are in fact used by something.

### Nice-to-have
- [ ] Split Vulkan+RT from xash specifics, start preparing it for being a standalone thing.
	- [ ] clang-format for it

# Previously
## 2024-02-05 E373
- [x] Skybox for traditional renderer
	- [x] Sky pipeline
	- [x] Submit SURF_DRAWSKY draw commands
	- [x] Use original skybox for trad renderer

## 2024-02-01 E371
- [x] tune A-Trous step widths for different channels
	- [x] multiple passes -- core of the paper lol
- [x] fix no-hit bounce absent legacy blending
- [x] update render tests
- [x] add new channels to render tests
- [x] add white furnace render test
- [ ] :x: temporal glitches with dontBlurSamples() and ATrous → no longer reproduces
- [x] add `-vkdbg_shaderprintf` arg to explicitly enable shader debug printfs

## 2024-01-29 E370
- [x] bounce > 1 brighness
- [ ] tune A-Trous step widths for different channels
	- [x] tune parameters
	- [x] "cone width"
	- [x] different parameters/radii for different channels
	- [ ] multiple passes -- core of the paper lol

## 2024-01-26 E369
- [x] white furnace test
	- [x] do it using display_only mode
	- [x] do it via separate flag
	- [x] too dark indirect: blurSamples() returns values too small (incorrect sigma|scale?)
		- [x] do a box blur test
		- [x] do A-trous wavelet denoiser
	- [x] diffuse and specular debug display modes
- [x] why did direct lighting became brighter on c2a5?

## 2024-01-23 E368
- [ ] specular bounce
    - [x] specular-vs-diffuse choice based on metalness+frensel
		- [x] better spec-vs-diff bounce type estimation
			- [x] better: also include fresnel
			- [ ] best: see literature
				- [ ] brdf.h
				- [x] rt gems 1/2
				- [ ] papers please?
	- [ ] BRDF material params attenuation
		- [x] try improving, multiply specular by fresnel

## 2024-01-22 E367
- [ ] specular bounce
    - [ ] specular-vs-diffuse choice based on metalness+frensel
		- [x] simple: metalness only
    - [ ] VNDF? sampling
		- [x] mindlessly copypasted from some paper
		- [ ] figure out what all of that means
	- [ ] BRDF material params attenuation
	- [ ] decide on the diffuse-vs-specular out channel based on the first bounce

## 2024-01-19 E366
- [x] investigate more shading nans
	- found zero normals in studio models, see #731
- [x] guns and transparency → added legacy transparency overshoot threshold
- [x] cvar to force culling
- [x] flashlight is too bright
- [x] bounce diffuse is still way darker than before
    → it shouldn't have been multiplied by diffuse value

## 2024-01-18 E365
- [-] flashlight far circular glitches
	- This is due to f32 precision not being enough when working with small (light radius ~=1) and large (light
	  distance ~=1e4) numbers.
- [x] patchable sun angle
	- [ ] :x: does qrad already have something for that? → no it doesn't
- [x] cleanup this TODO

## 2024-01-16 E364
- [x] P NaNs
	- [x] need to remove degenerate triangles
- [x] light_environment is too dark
- [ ] add direct_{diff,spec} to rendertests → only can do for this handmade-brdfs branch
	- [ ] :x: and rerun tests for vulkan to get new gold images → imuposshiburu, see above

## 2024-01-15 E363
- [x] filter out invalid (r=0, etc) lights in native
	- [-] :o: already do; it seems that clusters are not getting updates → see #730
- [x] pass point lights r² directly?
- [x] move empirical scaling to native code
- [x] modify point light radius in entity patches → already done
	- [x] adjust brightness based on radius? → already done
- [ ] :x: ~~common intersection-local-normal-oriented basis~~ → point light construct light-oriented frames, not reusable

## 2024-01-12 E362
- [x] point→spherical light sampling
	- [x] 1/pdf → pdf *= 2π
	- [x] disk sampling

## 2024-01-11 E361
- [x] fix zero-area polygon lights nanites, fixes #461
	- [x] c1a1a NaNs are still there
- [x] fix point light computation instabilites
	- [x] need proper sampling asap, as different instabilities approaches are visually different, and it's impossible to reason which one is preferable
- [x] add material debug display mode
- [ ] vulkan validation layers crashes on too many `debugPrintfEXT` messages

## 2024-01-09 E360
- [x] validate all intermediate and final outputs against invalid values, complain into log
- [ ] brdf math surprising edge cases
    - [ ] alpha^2 == 0 ???
    - [ ] various N,L,V collinearities, zero denoms and infinities
        - [ ] h_dot_l | h_dot_v < .0 because of numerical precision
        - [ ] ggxV|ggxG denoms->0

## 2024-01-08 E359
- [-] find and fix MORE NaNs
    - [x] add debugPrintfEXT to shaders
    - [x] fix black dots on glass surfaces
    - [ ] fix polygon light nans in logs
    - [x] magenta gliches -- dot(N,L) < 0.
    - [x] disableable NaN debugging with macro
    - [ ] enable NaN debugging with -vkvalidate

## 2024-01-04 E357
- [x] Black metals: https://github.com/w23/xash3d-fwgs/issues/666
    - [x] fix missing dot(N,L) term
- [x] try bespoke diffuse term -- yes, mine seems to be more correct
    - [ ] PR against glTF
- [ ] Bounces
    - [x] idiotic sampling
    - [ ] sampling functions
        - [x] diffuse
        - [ ] specular
    - [ ] how to mix properly with brdf itself
    - [x] find and fix NaNs
- [ ] Better PBR math, e.g.:
	- [ ] Fresnel issues (esp. with skybox)
	- [ ] Just make sure that all the BRDF math is correct

## 2023-12-29 E354
- [x] Figure out why additive transparency differs visibly from raster
- [x] Implement special legacy-blending in sRGB-γ colorspace

## 2023-12-28 E353
- [x] track color spaces when passing colors into shaders
- [-] validation failure at startup, #723 -- seems like memory corruption

Longer-term agenda for current season:
- [ ] Transparency/translucency:
	- [ ] Proper material mode for translucency, with reflections, refraction (index), fresnel, etc.
	- [ ] Extract and specialize effects, e.g.
		- [ ] Rays -> volumetrics
		- [ ] Glow -> bloom
		- [ ] Smoke -> volumetrics
		- [ ] Sprites/portals -> emissive volumetrics
		- [x] Holo models -> emissive additive
		- [ ] Some additive -> translucent
		- [ ] what else
- [ ] Render-graph-ish approach to resources.
- [ ] Performance tools -- needed for perf and lighting work below:
	- [ ] Needs: render-graph-ish things for fast iterations when exporting custom situational metrics for the shader.
	- [ ] Purpose: shader profiling. Measure impact of changes. Regressions.
	- [ ] WIP shader clocks: https://github.com/w23/xash3d-fwgs/pull/692
	- [ ] WIP perf query: https://github.com/w23/xash3d-fwgs/pull/500
- [ ] Lighting
	- [x] Point spheres sampling
	- [ ] Increase limits
	- [ ] s/poly/triangle/ -- simpler sampling, universal
	- [ ] Better and dynamically sized clusters
	- [ ] Cache rays -- do not cast shadow rays for everything, do a separate ray-only pass for visibility caching
- [ ] Bounces
	- [ ] Moar bounces
	- [ ] MIS
	- [ ] Cache directions for strong indirect light


## 2023-12-19 E350
- [x] fixup skybox reflections
- [x] improve logs "vk/tex: Loaded skybox pbr/env/%.*s"
- [x] add skybox test

## 2023-12-18 E349
- [x] KTX2 cubemaps
- [x] variable cubemap exposure (in .mat file)

## 2023-12-15 E348
- [x] fix ktx2 sides corruption

## 2023-12-14 E346-E347
- [x] Optimize skybox loading, #706
    - [x] Do not load skybox when there are no SURF_DRAWSKY, #579
    - [x] Do not reload the same skybox
    - [-] Load skyboxes from KTX2 sides
        → doesn't work as easily, as there's no way to rotate compressed images.
          KTX2 sides should be pre-rotated
    - [x] do not generate mips for skybox
    - [x] support imagelib cubemaps
    - [x] use imagelib skybox loader
- [x] Hide all SURF_DRAWSKY while retaining skybox, #579
- [x] possible issues with TF_NOMIPMAP
    - [x] used incorrectly when loading blue noise textures
    - [x] what about regular usage?

## 2023-12-11 E345
- [x] fix black dielectrics, #666
    - [x] fix incorrect basecolor brdf multiplication, #666
    - [x] fixup skybox glitches caused by #666 fix
- [ ] Patch overlay textures (#696) → turned out to be much more difficult than expected.
- [x] Do not patch sprite textures for traditional raster, #695

## 2023-12-05 E342
- [x] tone down the specular indirect blur
- [-] try func_wall static light opt, #687
	→ decided to postpone, a lot more logic changes are needed
- [x] increase rendertest wait by 1 -- increased scroll speed instead
- [x] update rendertest images
- [x] Discuss shader profiling
- [-] Discuss Env-based verbose log control

## 2023-12-04 E341
- [-] investigate envlight missing #680
	- couldn't reproduce more than once
- [x] add more logs for the above
- [x] double switchable lights, #679

-- season cut --

## 2023-12-01 E340
- [x] Better resolution changes:
    - [x] Dynamic max resolution (start with current one, then grow by some growth factor)

## 2023-11-30 E339
- [x] rendermode patch
	- [x] track patch by boolean, not another field
- [x] missing polylight on c2a1b
	- [x] "proper" slow fix: make func_water emissive surfaces dynamic
		- [x] extract dynamic polylights from render/rt models
- [x] Reuse GPU scope names
- [x] Support changing screen resolution up to UHD
    - [x] Increase devmem count.

## 2023-11-28 E338
- [x] rendertest
    - [x] read imagecompare results
    - [x] html report

## 2023-11-27 E337
- [x] make rendetest.py the central script
    - [x] parallelize/make gifs
    - [x] diff/convert in parallel
- [-] backside transparency
     - [x] added to rendertest
     - [ ] consider passing a special flag for single-sided blended surfaces (i.e. brush surfaces)
- [x] fix per-entity material mapping, #669
    - [x] add to rendertest

## 2023-11-24 E336
- reproducible rendering:
    - [x] make sure it's reproducible -- given carefully spaced `wait N`s and `playersonly` it gets pretty reproducible
    - [x] difference heatmap
    - [x] contemplate infrastructure: scripts, repo, etc.

## 2023-11-23 E335
- [x] spec for profiler dumper
- reproducible rendering:
    - [ ] write fixed resolution internal images -- only need this because i'm stupid and using tiling window manager
        - [ ] how to synchronize with frames
        - [ ] how to extract vk images
        - [ ] how to blit/copy various image pixel formats
        - [ ] what file format to choose for non-rgba8 formats? do we even need them?
    - [x] script for running and comparing results
    - [-] extras:
        - [x] difference gif
        - [ ] difference summary table
        - [ ] summary html
- [x] consolidate all binding in shaders

## 2023-11-21 E334
- [ ] reproducible rendering
    - [ ] dump all components
        - [x] script
        - [-] ~~try also dumping in native code~~ -- no need, it's fast enough
    - [x] command for random seed fixation

## 2023-11-20 E333
- [ ] contemplate testing rendered images
    - [x] try making a rendertest script: load multiple save, make multiple screenshots
    - [x] compare screenshots
    - [ ] Other infrastructure:
        - tracking golden states
        - testing script

## 2023-11-17 E332
- [-] backside emissive water polygons:
    - adding them makes things worse in other parts of the level
- [x] water normalmap support -- added missing tangents
- [x] discuss integration test strategies

## 2023-11-16 E331
- [x] Emissive waters
    - [x] add emissive water surface to polygon lights
    - [x] update emissive color for water surfaces
- [x] trihash option
- [x] dynamic UVs
    - [x] update UVs for conveyors
    - [ ] pls don't aggravate validation on changelevel -- cannot reproduce

## 2023-11-14 E330
- [x] culling worldmodel waters
     - [-] try simple flag culling (probably won't work)
     - [-] try detecting glpoly normals -> consistent with SURF_PLANEBACK, doesn't help
     - [x] SURF_UNDERWATER seems to get us a SINLE surface looking outwards
- [x] investigate gl backface culling for transparent surfaces:
    - [ ] glass -- seems to have 2nd face (brush backside)
    - [x] water -- doesn't seem to have 2nd face
        - [x] glpoly_t winding order is reversed when camera origin is opposite to (SURF_PLANEBACK-aware) surface normal
- [x] discuss culling transparent surfaces strategies

## 2023-11-13 E329
- [-] culling -> need to cull everything except opaque and blend. Alpha-mask is culled.
- [-] waters:
     - [-] No water surface visible from underneath -- hidden by enabling culling
     - [-] No coplanar issues visible? -- hidden by culling. Disabling culling makes glitches reappear

## 2023-11-10 E328
- [ ] woditschka
     - [-] potentially collinear planes vs ray tracing #264
         - not super clear how exactly it works, and what it does. And how to cull things
         - leaning towards making our own tesselator, as it might be universally usable for other things, e.g. detail mapping
         - [ ] (A) try producing simple surfaces w/o tesselation, similar to regular brush surfaces
         - [x] (C) print out all surfaces and polys to see where are they looking
         - [-] (B) try filtering surfaces looking down

## 2023-11-09 E327
- [x] update animated textures is now super slow: some static map surfaces have alternate anims (e.g. light post on c2a5)
- [-] woditschka
     - [x] height not switching to negative underwater -- decided that we don't need it for now
     - [x] do not draw water sides when not requested.

## 2023-11-07 E326
- [x] list supported arguments for `rt_debug_display_only` cvar
- [x] make vk_debug_log a command
- [x] remove stvecs from patches -- not used, inconvenient
- [x] patch texture coordinates by matrices
- [x] add `_xvk_tex_rotate`
- [x] ASSERT in c2a5 -- skybox sentinel

## 2023-11-06 E325
- [x] fix material asserts and inherit
- [x] fixup -vkverboselogs
- [x] changing textures on buttons, etc
- [x] fix unpatched chrome surfaces brightness glitches

## 2023-11-03 E324
- [x] add cvar for displaying only specified channel
- [x] r_lightmap
- [x] highlight all surfaces with random colors
- [ ] highlight selected surfaces -- decided to postpone
- [ ] massage shaders: consolidate all bindings explicitly
- [ ] skip sorting-by-texture when loading brush models ~~(=> geometry count explosion; i.e. kusochki count will explode too)~~
- [ ] kusochki-vs-materials structures
- [x] -vkverbose arg for turning all debug logs before detailed cvars are read

## 2023-11-02 E323
- [x] lol meta: read and sort issues
- [x] merge from upstream
- [x] hevsuit glitches
- [x] inverted normal map orientation

## 2023-10-31 E322
- [x] load png blue noise files
- [-] translucent animated thing -> needs shader rework
- [x] massage texture code
    - [x] single return/goto cleanup
    - [-] pass args via structs? -> not necessary
    - [-] collapse texture uploading into a single function -> not necessary, they are different enough
- [x] merge materials PR
- [x] studio gibs translucency
- [x] smoothing exclusion

## 2023-10-30 E321
- [x] missing skybox
- [x] explicitly free default textures; and complain about any leftovers
- [x] use the new hash table in materials too, remove dummy textures
- [x] why are there references to \*unused
- [ ] restore blue noise
    - [x] vk_texture_t blue_noise; 3d texture
	- [x] separate binding similar to skybox in vk_rtx.c and shaders
	- [x] patch shader function
	- [ ] load 64xpngs into a single big pic

## 2023-10-27 E320
- [x] fix windows build
- [x] track texture visibility for ref_api via flag and refcounts
- [ ] devmem assert, not all textures are destroyed in wagonchik
    - [ ] new material names+fixme => move to material hash table
    - [x] preallocated default textures
- [x] check urmom stats after a few different changelevels
    - [x] COUNT(IS_DELETED)
    - [x] clusters size histogram
- [x] silence logs
    - [x] "accessing empty texture"
    - [x] "found existing texture"
- [x] check mips

## 2023-10-26 E319
- [x] fix pbr materials disappearing
- [x] fix surface lights
- [ ] pbr/material refcount leaks
    - [ ] track texture visibility for ref_api
- [x] handle existing image on texture upload
    - [x] sanely recreate
    - [x] reuse if possible
- [x] case insensitive hash table

## 2023-10-24 E318
- [ ] use new hashmap for textures
    - [x] use vk_texure array directly as open addressing hash table
        - [x] Completely hide `struct vk_texture`
        - [x] just try
        - [x] texture indexes are no longer consecutive
    - [ ] blue noise texture breaks => make it a separate (3d) thing
    - [ ] index=0 is now valid
        - [x] I. mark 0 as occupied to avoid allocating it
        - [ ] II. Increase all returned indexes by 1. Then dec it back wherever it is passed back
    - (SAD): cannot make builtin textures have stable indexes anymore

# E313
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

# Programmable render
- [ ] implicit dependency tracking. pass defines:
	- [ ] imports: list of things it needs
	- [ ] exports: list of things it produces. those get created and registered with this pass as a producer
- [ ] resource management refactoring:
	- [ ] register existing resources (tlas, buffers, temp images, ...) in their producers
	- [ ] resource automatic resolution: prducing, barriers, etc
	- [ ] resource destruction
- [ ] ? resource object: name, metadata(type, etc.), producer, status (ready, barriers, etc)

# Multipass + Sampling
- [ ] better simple sampling
	- [x] all triangles
	- [x] area based on triangles
	- [ ] clipping?
	- [ ] can we pack polygon lights better? e.g.:
		- each light is strictly a triangle
		- index is offset into triangles
		- layout:
			- vec4(plane) // is it really needed? is early culling important? can we shove area into there too? e.g plane_n.xy,plane_d, area
			- vec4(v0xyz, e_r)
			- vec4(v1xyz, e_g)
			- vec4(v2xyz, e_b)

# Next
- [ ] remove surface visibility cache
- [ ] rtx: rename point lights to lampochki
- [ ] rtx: rename emissive surface to surface lights
- [ ] rtx: dynamically sized light clusters
	Split into 2 buffers:
		struct LightCluster { uint16 offset, length; }
		uint8_t data[];

# Planned
- [ ] improve nonuniformEXT usage: https://github.com/KhronosGroup/Vulkan-Samples/pull/243/files#diff-262568ff21d7a618c0069d6a4ddf78e715fe5326c71dd2f5cdf8fc8da929bc4eR31
- [ ] rtx: experiment with refraction index and "refraction roughness"
- [ ] emissive beams
- [ ] emissive particles/sprites
- [ ] issue: transparent brushes are too transparent (train ride)
	- [ ] (test_shaders_basic.bsp) shows that for brushes at least there are the following discrepancies with gl renderer:
		- [ ] traditional:
			- [ ] anything textured transparent is slightly darker in ref_vk
			- [ ] "Color" render mode should not sample texture at all and use just color
			- [ ] "Texture" looks mostly correct, but ~2x darker than it should be
			- [ ] "Glow" looks totally incorrect, it should be the same as "Texture" (as in ref_gl)
			- [ ] "Additive" is way too dark in ref_vk
	- [ ] rtx:
			- [ ] "Color" should use solid color instead of texture
			- [ ] "Color", "Texture", ("Glow"?) should be able to reflect and refract, likely not universally though, as they might be used for different intended effects in game. figure this out on case-by-case basis. maybe we could control it based on texture names and such.
			- [ ] "Additive" should just be emissive and not reflective/refractive
- [ ] rtx: filter things to render, e.g.: some sprites are there to fake bloom, we don't need to draw them in rtx mode
- [ ] possibly split vk_render into (a) rendering/pipeline, (b) buffer management/allocation, (c) render state
- [ ] studio models: fix lighting: should have white texture instead of lightmap OR we could write nearest surface lightmap coords to fake light
	- [ ] make it look correct lol
- [ ] studio model types:
	- [x] normal
	- [ ] float
	- [x] chrome
- [ ] rtx: sky light/emissive skybox:
	- [ ] consider baking it into a single (or a few localized) kusok that has one entry in light cluster
	- [x] just ignore sky surfaces and treat not hitting anything as hitting sky. importance-sample by sun direction
	- [ ] pre-compute importance sampling direction by searching for ray-miss directions
- [ ] rtx: importance-sample sky light; there are sky surfaces that we can consider light sources
- [ ] cull water surfaces (see c3a2a)
- [ ] consider doing per-geometry rendermode: brushes can be built only once; late transparency depth sorting for vk render;
- [ ] rtx: too many emissive lights in c3a1b
- [ ] rtx: denoise
	- [ ] non local means ?
	- [x] reprojection
	- [ ] SVG+
	- [ ] ...
- [ ] rtx: bake light visibility in compute shader
- [ ] make 2nd commad buffer for resource upload
- [ ] :x: bad condition for temp vs map-permanent buffer error message
- [ ] fix brush blending
- [ ] sprite depth offset
- [ ] fix incorrect viewport sprite culling
- [ ] improve g_camera handling; trace SetViewPass vs RenderScene ...
- [ ] studio model lighting
- [ ] :x: move all consts to vk_const
- [ ] decals
- [ ] lightmap dynamic styles
- [ ] fog
- [ ] studio models survive NewMap; need to compactify buffers after removing all brushes
- [ ] sometimes it gets very slow (1fps) when ran under lldb (only on stream?)
- [ ] rtx: non-realtime unbiased mode: make "ground truth" screenshots that take 1e5 samples per pixels and seconds to produce. what for: semi-interactive material tuning, comparison w/ denoise, etc.

# Someday
- [ ] more than one lightmap texture. E.g. sponza ends up having 3 lightmaps
- [ ] better 2d renderer: fill DRAWQUAD(texture, color, ...) command into storage buffer instead of 4 vertices
- [ ] brush geometry is not watertight
- [ ] collect render_draw_t w/o submitting them to cmdbuf, then sort by render_mode, trans depth, and other parameters, trying to batch as much stuff as possible; only then submit

## 2021-02-06
- [x] alpha test
- [x] compare w/ gl R_SetRendeMode
	- [x] raster state
	- [x] color constants
- [x] culling
- [x] shaders s/map/brush/
- [x] pipeline cache
- [x] swapchain getting stale
- [x] HUD sprites
- [x] issue: lightmap sometimes gets corrupted on map load

## 2021-02-08
- [x] move entity rendering-enumeration into vk_scene

## 2021-02-10
- [x] refactor brush into brushes and separate rendering/buffer management
- [x] animated textures (accept PR)

## 2021-02-13
- [x] move pipelines from brush to render
- [x] render temp buffer api
- [x] draw studio models somehow
- [x] studio models vk debug markers
- [x] studio models white texture as lightmap
- [x] studio models fixes

## 2021-02-15
- [x] weapon models -- viewmodel
- [x] coalesce studio model draw calls
- [x] initual sprite support

## 2021-02-17
- [x] draw some beams

## 2021-02-20
- [x] refactor vk_render interface:
	- [x] move uniform_data_t to global render state ~inside render_draw_t, remove any mentions of uniform/slots from api; alt: global render state?~
	- [x] rename RenderDraw to SubmitDraw
	- [x] ~add debug label to render_draw_t?;~ alt: VK_RenderDebugNameBegin/End
	- [x] perform 3d rendering on corresponding refapi calls, not endframe
- [x] fix sprite blending

## 2021-02-22
- [x] RTX: load extensions with -rtx arg
- [x] vk_render: buffer-alloc-centric upload and draw api

## 2021-03-06
- [x] (RTX; common) Staging vs on-GPU buffers
- [x] rtx: BLAS construction on buffer unlock
- [x] rtx: ray trace compute shader
- [x] dlight test

## 2021-03-08
- [x] studio models normals
- [x] rtx: geometry indexing

## 2021-03-10
- [x] rtx: dlights
- [x] rtx: dlight shadows
- [x] rtx: dlight soft shadows

## 2021-03-13
- [x] rtx: blend normals according to barycentrics
- [x] rtx: (debug/dev) shader reload
- [x] rtx: make projection matrix independent render global/current/static state
- [x] rtx: model matrices
- [x] rtx: light entities -- still not enough to enlight maps :(
- [x] rtx: path tracing

## 2021-03-15
- [x] rtx: control bounces with cvars
- [x] rtx: device-local buffers -- doesn't affect perf noticeably :(
- [x] rtx: emissive materials
	- [x] rtx: emissive textures
	- [x] rtx: emissive beams

## 2021-03-17..20
- [x] rtx: lower resolution framebuffer + upscale
- [x] rtx: importance sample emissive surface
- [x] rtx: remove entnity-parsed lights
- [x] rtx: naive temporal denoise: mix with previous frame

## 2021-03-22
- [x] rtx: traverse bsp for science!

## 2021-03-28
- [x] bake s/d-lights visibility data into bsp leaves

## 2021-04-06..08
- [x] persistent models
	- [x] load brushes into render model
	- [x] destroy brushes when time comes (when?)
	- [x] rasterize models in renderer

## 2021-04-09
- [x] rtx: build AS for model
- [x] rtx: include pre-built models in TLAS

## 2021-04-10
- [x] rtx: fix tlas rebuild
- [x] rtx: upload kusochki metadata ~~w/ leaves~~
- [x] rtx: add fps
	- [x] rtx: don't group brush draws by texture
	- [x] better AS structure (fewer blases, etc)

## 2021-04-11
- [x] vscode build and debug

## 2021-04-12
- [x] rtx: fix surface-kusok index mismatch
- [x] rtx: try to use light visibility data
	- too few slots for light sources
	- some areas have too many naively visible lights
- [x] rtx: fix light shadow artefacts

## 2021-04-13
- [x] rtx: "toilet error": attempting to get AS device address crashes the driver
- [x] rtx: fix blas destruction on exit
- [x] rtx: sometimes we get uninitialized models

## 2021-04-14..16
- [x] rtx: grid-based light clusters

## 2021-04-17
- [x] rtx: read rad file data

## 2021-04-19
- [x] rtx: light intensity-based light clusters visibility
- [x] rtx: check multiple variants of texture name (wad and non-wad)
- [x] rtx: rad liquids/xeno/... textures

## 2021-04-22
- [x] rtx: fix backlight glitch
- [x] rtx: textures

## 2021-04-24, E86
- [x] rtx: restore studio models

## 2021-05-01, E89
- [x] make a wrapper for descriptor sets/layouts

## 2021-05-03, E90
- [x] make map/frame lifetime aware allocator and use it everywhere: render, rtx buffers, etc

## 2021-05-08, E92
- [x] rtx: weird purple bbox-like glitches on dynamic geometry (tlas vs blas memory corruption/aliasing)
- [x] rtx: some studio models have glitchy geometry

## 2021-05-10, E93
- [x] rtx: don't recreate tlas each frame
- [x] rtx: dynamic models AS caching

## 2021-05-..-17, E93, E94
- [x] rtx: improve AS lifetime/management; i.e. pre-cache them, etc
- [x] add debug names to all of the buffers

## 2021-05-22, E97
- [x] add nvidia aftermath sdk

## 2021-05-24, E98
- [x] rtx: simplify AS tracking

## 2021-05-26, E99
- [x] rtx: fix device lost after map load

## 2021-05-28, E100
- [x] rtx: build acceleration structures in a single queue/cmdbuf

## 2021-06-05, E103
- [x] rtx: dynamic surface lights / dynamic light clusters
- [x] rtx: animated textures
- [x] rtx: attenuate surface lights by normal

## 2021-06-07, E104..
- [x] fix CI for vulkan branch

## 2021-06-09..12, E105..106
- [x] c3a2a: no water surfaces in vk (transparent in gl: *45,*24,*19-21)
- [x] water surfaces

## 2021-06-14, E107
- [x] rtx: optimize water normals. now they're very slow because we R/W gpu mem? yes
- [x] cull bottom water surfaces (they're PLANE_Z looking down)
- [x] fix water normals

## 2021-06-23, E109
- [x] rtx: ray tracing shaders specialization, e.g. for light clusters constants
- [x] rtx: restore dynamic stuff like particles, beams, etc
- [x] rtx: c3a1b: assert model->size >= build_size.accelerationStructureSize failed at vk_rtx.c:347

## 2021-07-17, E110..120
- [x] rtx: ray tracing pipeline
- [x] rtx: fix rendering on AMD
- [x] rtx: split models into a separate module
- [x] rtx: alpha test

## 2021-07-31, E121
- [x] rtx: alpha blending -- did a PoC

## 2021-08-02..04, E122-123
- [x] mipmaps
- [x] rtx: better random

## 2021-08-07, E124
- [x] anisotropic texture sampling
- [x] studio model lighting prep
	- [x] copy over R_LightVec from GL renderer
	- [x] add per-vertex color attribute
	- [x] support per-vertex colors
	- [x] disable lightmaps, or use white texture for it instead

## 2021-08-11, E125
- [x] simplify buffer api: do alloc+lock as a single op

## 2021-08-15, E126
- [x] restore render debug labels
- [x] restore draw call concatenation; brush geoms are generated in a way that makes concatenating them impossible

## 2021-08-16, E127
- [x] better device enumeration

## 2021-08-18, E128
- [x] rtx: fix maxVertex for brushes

## 2021-08-22, E129
- [x] fix depth test for glow render mode
- [x] screenshots

## 2021-08-26, E131
- [x] rtx: material flags for kusochki

## 2021-09-01, E132
- [x] rtx: ingest brdfs from ray tracing gems 2
- [x] rtx: directly select a triangle for light sampling

## 2021-09-04, E133
- [x] rtx: different sbts for opaque and alpha mask
- [x] include common headers with struct definitions from both shaders and c code

## 2021-09-06, E134
- [x] rtx: pass alpha for transparency
- [x] rtx: remove additive/refractive flags in favor or probability of ray continuing further instead of bouncing off
- [x] make a list of all possible materials, categorize them and figure out what to do

# E149
- [x] rtx: remove sun
- [x] rtx: point lights:
	- [x] static lights
		- [x] intensity "fix"
	- [x] dlights
	- [ ] elights
	- [x] intensity fix for d/elights?
	- [x] point light clusters
	- [x] bsp:
		- [x] leaf culling
		- [x] pvs

- [x] rtx: better light culling: normal, bsp visibility, (~light volumes and intensity, sort by intensity, etc~)
- [x] rtx: cluster dlights

## 2021-10-24 E155
- [x] rtx: static lights
	- [x] point lights
	- [x] surface lights

## 2021-10-26 E156
- [x] enable entity-parsed lights by lightstyles

## 2021-12-21 DONE SOMEWHEN
- [x] rtx: dynamic rtx/non-rtx switching breaks dynamic models (haven't seen this in a while)
- [x] run under asan
- [x] rtx: map name to rad files mapping
- [x] rtx: live rad file reloading (or other solution for tuning lights)
- [x] rtx: move entity parsing to its own module
- [x] rtx: configuration that includes texture name -> pbr params mapping, etc. Global, per-map, ...
- [x] rtx: simple convolution denoise (bilateral?)
- [x] rtx: cull light sources (dlights and light textures) using bsp
- [-] crash in PM_RecursiveHullCheck. havent seen this in a while
- [x] rtx: remove lbsp

## 2022-09-17 E207 Parallel frames
- [x] allocate for N frames:
	- [x] geometries
	- [x] rt models
		- [x] kusochki
		  - [x] same ring buffer alloc as for geometries
		    - [x] extract as a unit
		- [x] tlas geom --//--
	- [-] lights
		- [x] make metadata buffer in lights
		- [-] join lights grid+meta into a single buffer => pipeline loading issues
		- [x] put lights data into a cpu-side vk buffer
		- [-] sync+barrier upload => TOO BIG AND TOO SLOW, need to e.g. track dirty regions, compactify stuff (many clusters are the same), etc
- [x] scratch buffer:
  - should be fine (assuming intra-cmdbuf sync), contents lifetime is single frame only
- [x] accels_buffer:
  - ~~[ ] lifetime: multiple frames; dynamic: some b/tlases get rebuilt every frame~~
  - ~~[ ] opt 1: double buffering~~
  - [x] opt 2: intra-cmdbuf sync (can't write unless previous frame is done)
- [x] uniform_buffer:
  - lifetime: single frame
- [x] tlas_geom_buffer:
  - similar to scratch_buffer
  - BUT: filled on CPU, so it's not properly synchronsized
  - fix: upload using staging?
	- [x] double/ring buffering

- [x] E213:
	- [x] parse binding types
	- [x] remove types from resources FIXME
- [x] E214: ~tentative~
	- [x] integrate sebastian into waf
- [x] E215:
	- [x] serialize binding image format

## 2022-11-26 E216 rake yuri
	- [x] validate meatpipe image formats
	- [x] begin Rake Yuri migration
		- [x] direct lights

## 2023-01-21 E217-E221
- [x] meatpipe resource tracking
	- [x] name -> index mapping
	- [x] create images on meatpipe load
	- [x] automatic resource creation
	- [x] serialize all resources with in/out and formats for images
	- [x] create resources on demand
- [x] parse spirv -> get bindings with names
  - [x] spirv needs to be compiled with -g, otherwise there are no OpName entries. Need a custom strip util that strips the rest?
	- [x] unnamed uniform blocks are uncomfortable to parse.
- [x] passes "export" their bindings as detailed resource descriptions:
  - [x] images: name, r/w, format, resolution (? not found in spv, needs to be externally supplied)
	- [-] buffers: name, r/w, size, type name (?) -- can't really do, too hard for now
- [x] name -> index resolver (hashmap kekw)
- [x] automatic creation of resources
	- [x] images
	- [-] buffers -- no immediate need for that

## 2023-01-22 E222
- [x] refcount meatpipe created images
- [x] rake yuri primary ray

## 2023-01-28 E223
- [x] previous frame resources reference
		- specification:
			- [x] I: prev_ -> resource flag + pair index
			- [ ] II: new section in json
		- internals:
			- [x] I: create a new image for prev_, track its source; swap them each frame
						Result is meh: too much indirection, hard to follow, many things need manual fragile updates.
			- [ ] II: create tightly coupled image pair[2], read from [frame%2] write to [frame%2+1]
			- [ ] III: like (I) but with more general resource management: i.e. resource object for prev_ points to its source

## 2023-01-28-02-08 E224-229
- [x] light_grid_buffer (+ small lights_buffer):
  - lifetime: single frame
  - BUT: populated by CPU, needs sync; can't just ring-buffer it
  - fixes: double-buffering?
    - staging + sync upload? staging needs to be huge or done in chunks. also, cpu needs to wait on staging upload
	- 2x size + wait: won't fit into device-local-host-visible mem
	- decrease size first?
- [x] additive transparency
- [x] bounces
- [x] skybox shadows
- [-] rtx: shrink payload between shaders
- [x] rtx: split ray tracing into modules: pipeline mgmt, buffer mgmt
- [x] nvnsight into buffer memory and stuff
- [x] multiple frames in flight (#nd cmdbuf, ...)
- [x] embed shaders into binary
- [x] verify resources lifetime: make sure we don't leak and delete all textures, brushes, models, etc between maps
- [x] custom allocator for vulkan
- [x] rtx: better mip lods: there's a weird math that operates on fov degrees (not radians) that we copypasted from ray tracing gems 2 chapter 7. When the book is available, get through the math and figure this out.
- [x] render skybox
- [x] better flashlight: spotlight instead of dlight point
- [x] rtx: add fps: rasterize into G-buffer, and only then compute lighting with rtx

# Done somewhen
- [x] create water surfaces once in vk_brush
- [x] loading to the same map breaks geometry
- [x] (helps with RTX?) unified rendering (brush/studio models/...), each model is instance, instance data is read from storage buffers, gives info about vertex format, texture bindings, etc; which are read from another set of storage buffers, ..
- [x] waf shader build step -- get from upstream

## Collected on 2024-01-18
- [x] what if new meatpipe has different image format for a creatable image?
- [x] rtx: light styles: need static lights data, not clear how and what to do
- [x] more beams types
- [x] more particle types
- [x] sane texture memory management: do not allocate VKDeviceMemory for every texture
- [x] rtx: transparency layering issue, possible approaches:
	- [x]  trace a special transparent-only ray separately from opaque. This can at least be used to remove black texture areas
- [x] rtx: better memory handling
	- [x] robust tracking of memory hierarchies: global/static, map, frame
	- or just do a generic allocator with compaction?
- [x] rtx: coalesce all these buffers
- [x] rtx: entity lights
- [x] rtx: do not rebuild static studio models (most of them). BLAS building takes most of the frame time (~12ms where ray tracing itself is just 3ms)
- [x] studio models: pre-compute buffer sizes and allocate them at once
- [x] dlight for flashlight seems to be broken
- [x] fix sprite blending; there are commented out functions that we really need (see tunnel before the helicopter in the very beginning)
- [x] fix projection matrix differences w/ gl render
- [x] what is GL_Backend*/GL_RenderFrame ???
- [x] particles
- [x] optimize perf: cmdbuf managements and semaphores, upload to gpu, ...
- [x] rtx: studio models should not pre-transform vertices with modelView matrix
- [x] start building command buffers in beginframe
- [x] cleanup unused stuff in vk_studio.c
- [x] stats
- [-] auto-atlas lots of smol textures: most of model texture are tiny (64x64 or less), can we not rebind them all the time? alt: bindless texture array
- [x] can we also try to coalesce sprite draw calls?
