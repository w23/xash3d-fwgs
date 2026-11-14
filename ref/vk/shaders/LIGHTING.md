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
- Diffuse GI: half-resolution `lc_bounce` → firefly rejection → Poisson
  reconstruction and SH formation → temporal reprojection of SH coefficients →
  two switchable bilateral SH blur passes → SH projection onto the shading normal
  from the normal map. The compositor reads that resolved irradiance directly.
  There is no temporal accumulation of resolved diffuse RGB.
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
- `denoiser.comp` reads the finished lobes and handles material composition,
  emissive/translucent blending, output color conversion and debug views only.

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
a-trous, alternative NRD/AMD paths and source-branch refraction experiments are
not part of this pipeline.

Build from the repository root with `python waf build`.
