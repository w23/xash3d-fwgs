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
- Transparency: positive material `refraction_ior` computes Fresnel once at the
  ray hit and writes it to ordinary surface alpha, before decal composition.
  The standard surface PBR response (explicitly mirrored by the water patch)
  blends with transmitted background through that alpha. There are no optical
  mode tags, special IOR branches in direct lighting, or secondary Fresnel weights.
  `lc_refraction` follows one continuing transmitted path, shading every accepted
  layer from the light cache and emission. Decals are applied at all these hits.
  Opaque decal coverage stops transmission and covers underlying emission.
  Roughness, metalness and RGB retain normal material/decal semantics.
  Primary shading uses the already resolved lighting lobes in the compositor.
  There is no reflection-branch stack or fixed layer limit; negligible accumulated
  transmission terminates the path. Rejected back faces are not alpha layers.
  Vanilla liquid surfaces remain visible from both sides; coincident BSP liquid
  back copies are removed before geometry generation. Physical entry/exit remains
  two-sided. Wave normals use independent material parameters on the geometric
  plane, filtered by ray footprint. There is no volumetric fog in this pass.
  File precedence and authoring examples are in `../ray_materials.md`.
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

## Procedural wave controls

Material `wave_scale` (0.01..100) multiplies wavelength and height together,
preserving normal slope. `wave_strength` (0..10) multiplies slope; zero disables
procedural waves while retaining any material normal map. These are authored in
`.mat` patches and passed directly to the shader, with no global cvar multipliers.
The refraction pass has no temporal history.
