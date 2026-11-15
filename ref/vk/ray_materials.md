## Transparency and procedural waves

Materials preserve the game's surface shading and alpha blending by default.
There is one switch for physical transparency: `refraction_ior`.

| Key | Values | Default |
| --- | --- | --- |
| `refraction_ior` | `0` keeps vanilla shading/alpha; `1`–`11` selects physical transparency | `0` |
| `wave_scale` | `0.01`–`100`, wavelength and height scale | `1` |
| `wave_strength` | `0`–`10`, procedural normal slope strength | `0` |

**Glass and toxins:** no optical override is required. With `refraction_ior 0`,
the ray continues straight and the complete shaded surface is blended with the
background using texture alpha × material alpha × entity alpha. World liquids
also retain the engine's `movevars.wateralpha`. A fully opaque surface stays
opaque; zero alpha reveals the background. Vanilla translucent faces follow the
raster winding/culling convention for both primary and continued rays; the
back face of a closed brush is not an additional alpha layer. Distinct panes
still blend independently. Liquids remain two-sided for vanilla views from below and physical entry/exit. Coincident reversed BSP water copies are removed before tessellation. Surface color, diffuse/specular
lighting, and emission all participate in the surface result. Existing `.rad`
and per-surface `_light` emission sources are preserved.

**Water:** explicitly set the following in a `.mat` block:

```
{
    "for" "!WATERBLUE"
    "inherit" "$current"
    "refraction_ior" "1.333"
    "metalness" "1"
    "roughness" "0"
    "wave_scale" "1"
    "wave_strength" "1"
}
```

A positive `refraction_ior` replaces texture/material/entity alpha with exact
Fresnel reflectance at the ray hit. The ordinary PBR surface is then blended with
the transmitted background through that alpha; no transparency-mode tag or
second Fresnel multiplication exists. The water patch explicitly sets
`metalness 1` and `roughness 0` to use the existing mirror surface response.
Texture/material RGB and emission remain ordinary surface properties. The
metallic color's grazing response is accepted as an artistic approximation.

Decals are applied after IOR-to-alpha conversion, on primary and subsequent
hits. A decal of coverage `D` produces `alpha = D + (1-D)*surface_alpha`.
Opaque stickers stop transmission, their material maps blend normally, and they
cover underlying emission. All shading inside the background tracing pass uses
the light cache. There is one continuing path, without a reflection-branch stack.

The exterior is assumed to have IOR 1; entry uses `1 / refraction_ior`, exit uses
`refraction_ior`. `refraction_ior 1` gives zero interface opacity and no bending,
whereas `0` preserves the original alpha. Total internal reflection gives alpha 1.
Values between 0 and 1, negative values, and values above 11 are rejected.

Waves are independent of transparency. They can be enabled on a vanilla material
or disabled on physical water with `wave_strength 0`. Wave scale multiplies both
wavelength and height, preserving slope. There are no global wave cvars.
The shipped water list enables waves initially. The final override file can tune
or disable them; it takes precedence over resource-pack and map materials. There is no underwater fog in this pass.

## Override files and precedence

The loader reads, in order:

1. `pbr/materials.mat`.
2. WAD-specific material files.
3. Map-specific `pbr/maps/<mapname>.bsp/<mapname>.mat`.
4. Optional renderer/user `water_physical_refraction.mat` (source: `data/valve/water_physical_refraction.mat`,
   alongside the `bluenoise2d` directory, independent of the PBR resource pack).
   This final layer takes precedence over all of the above.

Use `for` then `inherit "$current"` to preserve the previously loaded texture maps,
colors, roughness, and metalness, then set the fields to override. For a
render-mode-specific material, put `for_rendermode` before `inherit`.
Later mappings replace earlier ones. Missing map textures are skipped.
`new` and named `inherit`/`use` remain supported by the existing loader.

The shipped override explicitly lists 31 water textures from `halflife.wad` and
`liquids.wad`, including the `!C2A5*` color variants and `WATER4B` used by water entities.
It does not override glass, toxins, or Xen fluids.
Loading follows the explicit order above, not alphabetical filenames.
`inherit "$current"` retains unspecified fields, including normal maps. For example,
`roughness 0` overrides the inherited roughness while `normal_scale 0` explicitly
disables an inherited normal map's perturbation without disabling procedural waves. Shader behavior never depends on a texture's name.

## Material architecture notes

- orig:
- HL: (how does this map?)
	- Render: Normal (по умолчанию), Color, Texture, Glow, Solid, Additive
	- Render FX: разные пульсации, строб, плавные переходы, Constant Glow, Distort, Hologram (Distort + fade)
- brush:
	- opaque:
		- "diffuse": pbr (diffuse, metallic, roughness, ...)
		- reflective: specular, ...
		- emissive
	- semi-opaque: alpha mask + same as opaque
	- transparent:
		- glass:
			- reflective
			- translucent + refractions
		- additive:
			- ???
- studio:
	- normal: ~same as opaque
	- float: ???
	- chrome:
		- reflective
	- glow shell
		- transparent-additive
	- x rendermode ???
- sprite
	- transparent additive:
		- fake bloom (rtx: just disable)
		- misc: smoke, explosions (rtx: custom shader?)
	- can generally be in all "HL Render/FX" modes
- beams:
	- can have custom color?
	- transparent additive
		- rtx: needs custom shader
	- can generally be in all "HL Render/FX" modes
- decals: ???
- rtx proposal:
- kusok
	- bool alpha_mask -- whether need to check alpha mask for boolean anyhit transparency
	- render modes:
		- opaque
			- emissive
			- diffuse
			- specular/reflection
		- transparent additive -- render_mode
			- force emissive
			- no diffuse, specular, ...
		- translucent -- render_mode
			- ? diffuse
			- (? specular)/reflection
			- refraction
		- sky?
			- emissive cubemap
