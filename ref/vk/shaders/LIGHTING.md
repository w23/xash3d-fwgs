# Lighting passes

`rt.json` contains a single lighting pipeline; experimental alternative paths are
not compiled into this graph.

- Direct: LTC lookup + static joint-UV blue noise, followed by shadow preparation,
  Heitz noise, noise smoothing, horizontal and vertical shadow filters. Preparation
  encodes the correlated estimates once per pixel, outside the filter tap loops.
  The noise estimator reads separate diffuse/specular lighting signals with
  analytic illumination, diffuse material color, revealage, the compositor's
  previous-frame exposure and sRGB output curve applied. Values are clamped to
  the visible [0,1] range for noise estimation only. Thus shadow noise revealed
  by increased exposure increases the filter footprint. Other lighting/emissive
  contributions are omitted from this visibility approximation; shading changes
  can also increase the estimated noise, but analytic shading is never blurred.
  A 3x3 RMS noise smoothing step and square-root noise-to-sigma response keep
  sparse visible edge texels from collapsing to an ineffective subpixel blur.
  The filters process
  correlated shadowed/unshadowed estimates. Analytic lighting stays at the center
  pixel. There is no direct-light history or final lighting blur.
- Cache: CPU uploads surface ranges on initialization and brush transforms each
  frame. GPU clear/raster passes prepare the lightmap geometry, then point and
  polygon passes refresh its RGB lighting. This retains the source experiment's
  512x512 cache for lightmap page zero and its studio-model lightmap lookup.
  Spotlights use the uploaded surface-to-light direction, matching direct lighting.
  Environment lights (sun) use sky visibility; on a miss, alpha-tested occluders
  are checked across the full ray interval.
- Diffuse GI: half-resolution `lc_bounce` → firefly rejection → Poisson
  reconstruction and SH formation → temporal reprojection of SH coefficients →
  two switchable bilateral SH blur passes → SH projection onto the shading normal
  from the normal map. The compositor reads that resolved irradiance directly.
  There is no temporal accumulation of resolved diffuse RGB.
  Paths ending at a sky brush or missing geometry sample the skybox, weighted
  by the accumulated path throughput, so the sky also supplies diffuse GI.
  SH history uses a bilinear previous-frame footprint validated against geometric
  normals and previous surface positions, with explicit age and frame stamps.
  Normal maps do not gate or modify the coefficient history. The 8-frame
  accumulation is stored before spatial blur, so blur never feeds back into it.
  A separate unaccumulated direct-diffuse luminance history follows the same
  validated SH footprint. Strong absolute and relative changes shorten SH
  history, reaching an immediate reset for large flashes/on-off transitions.
  This perceptual anti-lag gate is symmetric for brightening and darkening;
  small changes near black are suppressed by an absolute threshold. Thresholds
  are defined in `diffuse_gi_sh_temporal.comp` in linear, unexposed lighting units.
- Reflections: full-resolution `lc_reflection` → firefly rejection → spatial
  reuse → reflection radiance history with parallax validation. This separate
  lobe retains the source-branch ASVGF accumulation/variance code. Its defaults
  live in `indirect_temporal_config.glsl`, keeping the CPU UBO small.
  Sky brushes and misses return skybox radiance with the same exposure as the
  primary sky. They retain zero hit distance/identity for environment history.
- Refraction: full-resolution `lc_refraction` traces deterministic smooth optical
  paths through translucent brush glass and engine liquid/turbulent surfaces.
  Opaque hit lighting comes from the light cache (plus material emission
  and sky). Glass RGB filters transmitted light and never adds surface radiance.
  There is no radiance history or denoising. Water uses IOR 1.333 and
  animated world-space directional wave normals written into the G-buffer.
  The wave shape and analytic slope follow GPU Gems chapter 1, equations 8a/8b
  (sharpened sine waves); short waves fade with the pixel footprint. Wave phases
  use the geometric plane, independent of the material normal map. The liquid
  flag is derived directly from engine SURF_DRAWTURB/SURF_DRAWTURB_QUADS flags.
  Flat glass preserves direction; normal-mapped glass keeps only the perturbation
  relative to a flat interface, retaining the water-IOR bending cap. All non-liquid
  interfaces in this pass use glass Fresnel (IOR 1.5, F0=0.04); liquid uses water
  Fresnel (IOR 1.333, F0 about 0.02). Both smoothly reach 100% reflection between
  65 and 80 degrees from the shading normal. Primary glass also retains the
  existing direct-specular signal, including its Fresnel and shadow visibility,
  without multiplying it by surface alpha or applying Fresnel a second time.
  Liquid composition is unchanged.
  There is no intersection/depth limit. A four-branch budget includes reflected
  continuations at total internal reflection. On exhaustion, further reflected
  branches are omitted while transmission continues. Reflection-only exhaustion
  resolves the liquid surface or discards the exhausted glass reflection branch.
  Negligible path weights are discarded. Alpha is texture alpha multiplied by
  material and entity alpha. Glass tint and liquid coverage apply only on the
  entering face. The exit retains Fresnel/refraction but does not apply another
  texture layer. In particular, legacy water texture alpha = 1 must not block
  sky transmission from below; the incident underwater segment still receives fog.
  Liquid uses `F * reflected + (1-F) * (alpha * surface + (1-alpha) * transmitted)`,
  with cached diffuse surface lighting. Glass instead uses
  `F * reflected + (1-F) * mix(1, tint, alpha) * transmitted`.
  Tint is the sampled linear texture RGB times material/entity RGB, clamped to
  [0,1]. White is the identity multiplier for every alpha; alpha = 0 is fully
  clear before Fresnel. There is no additive glass texture or cache-dependent
  absorption. Only explicitly emissive materials add glass emission.
  Refraction texture samples use the
  traced ray footprint rather than a fixed mip level, preserving texture detail.
  The primary pass stores lightmap UV for cached shading of the first interface,
  without tracing the camera ray again. There is no fixed liquid opacity or tint.
  Tuning lives in `refraction_config.glsl`.
  Liquid segments use Beer-Lambert fog, with 50% transmittance at 200 world units
  (approximately 2 metres). Fog asymptotically approaches the boundary's linear
  emission color, or black for a non-emissive liquid. Each branch retains its own
  medium/color across glass and internal reflections; transmission into air stops
  fog integration. The liquid-facing dot threshold is 0.01, using the unflipped
  geometric normal. An initial back-facing liquid boundary also fogs the segment
  from the camera to that exit. Medium detection relies on visible boundaries;
  camera-underwater pixels with no liquid boundary on their primary path and
  missing liquid side faces are not classified by a volume query.
  Sky brushes and misses terminate at the environment before fog integration:
  neither the sky distance nor the ray's maximum range defines a water segment.
  Fog is applied to the camera-to-liquid-exit segment and finite geometry hits
  while inside liquid. Open water volumes therefore do not receive an invented
  infinite fog segment on environment rays.
- `denoiser.comp` reads the finished lobes and handles material composition,
  emissive/translucent composition, exposure and debug views. Normal output remains
  linear HDR until `bloom_composite.comp` applies bloom and the output curve.

`rt_bounces` controls diffuse path depth. `rt_denoise_gi_by_sh` and
`rt_spatial_reconstruction` default to 1; existing saved values are respected.
Set both to 1 when testing the complete spatial denoising chain with an old
configuration.

- `vk_gi_sh_blur 0` bypasses both rotated SH blur
  dispatches; irradiance resolve reads the pre-blur SH signal instead.
- `rt_gi_poisson 0` independently disables spatial Poisson reconstruction.
- `rt_gi_sh_temporal` defaults to 1 and controls coefficient history before blur.
  Existing saved values are respected; 0 copies current SH through this stage.
- `rt_disable_reprojection 1` bypasses SH reprojection and reflection radiance
  accumulation. Path reservoir reuse remains separate.

Temporal histories reset when the resolution or signal settings change.
Direct lighting and the final compositor share previous-frame auto exposure,
but direct lighting does not accumulate a shadow or lighting history. Final
a-trous and alternative NRD/AMD paths are not part of this pipeline.

Build from the repository root with `python waf build`.

## HDR bloom

`rt_bloom` defaults to 1; 0 preserves the original display conversion and bypasses
bloom contribution. `rt_debug_display_only BLOOM_SOURCE` shows the thresholded
source, and `BLOOM` shows the halo alone. Existing debug views bypass bloom.

All tuning lives in `bloom_config.glsl`: threshold, soft knee, intensity, tent
radius, active level count (1..5), normalized level weights, and separate emission,
lighting and sky weights. Defaults are 1.0 for emission and 0.5 for lighting/sky.
The soft bright pass operates separately on exposed emission and material-shaded
lighting, after revealage. Sky is identified by nonpositive primary hit distance.
Legacy additive sprites remain composited in sRGB and do not feed bloom.

The built-in resource manager creates two full-resolution RGBA16F bloom images.
The compositor initially writes the full-resolution source to `bloom_a`.
Five 13-tap downsample passes pack floor-sized levels (1/2 through 1/32) vertically
into the two ping-pong images. Four tent upsample passes reconstruct level zero.
Each pass copies earlier levels needed by later passes to the opposite image;
there is no in-dispatch read/write aliasing. Sampling clamps to each rectangle,
including at odd resolutions. Tiny render targets automatically reduce the level
count. Inactive levels are skipped, with earlier rectangles copied through so
the final result always resides in `bloom_b`. Dispatch sizes and allocation remain
under the existing resource manager; invocations outside useful rectangles exit.

The final pass adds bloom to exposed linear HDR, converts to sRGB, then performs
legacy additive blending. Auto-exposure continues metering the unexposed scene,
without bloom feedback. There is no new tone mapper or temporal bloom history.

## Liquid controls

These archived cvars update immediately; all default to 1:

- `rt_water_wave_scale` (0.01..100): wavelength multiplier. 2 makes waves twice
  as large; height scales with wavelength to preserve normal strength.
- `rt_water_wave_strength` (0..10): procedural normal slope multiplier. 0 disables
  procedural waves while retaining any material normal map.
- `rt_water_fog_density` (0..100): extinction multiplier. 0 disables liquid fog;
  1 gives 50% visibility at 200 units (2 m), 2 at 100 units (1 m), 0.5 at 400
  units (4 m). Fog emission/color is unchanged.

Values are clamped when uploading shader uniforms. Wave setting changes reset
lighting histories; the refraction pass itself has no temporal history.
