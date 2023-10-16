#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_mapents.h"
#include "vk_const.h"
#include "profiler.h"
#include "vk_logs.h"

#include <stdio.h>

#define LOG_MODULE LogModule_Material

#define MAX_INCLUDE_DEPTH 4

#define MAX_MATERIALS 2048

static r_vk_material_t k_default_material = {
	.tex_base_color = -1,
	.tex_metalness = 0,
	.tex_roughness = 0,
	.tex_normalmap = 0,

	.metalness = 0.f,
	.roughness = 1.f,
	.normal_scale = 1.f,
	.base_color = { 1.f, 1.f, 1.f, 1.f },
};

/* TODO
enum {
#define X(bit, type, name, key, func) kMatField_##key = (1 << (bit)),
MATERIAL_FIELDS_LIST(X)
#undef X
};
*/

#define MAX_RENDERMODE_MATERIALS 32
typedef struct {
		struct {
			int tex_id;
			r_vk_material_ref_t mat;
		} map[MAX_RENDERMODE_MATERIALS];
		int count;
} r_vk_material_per_mode_t;

enum {
	kMaterialNotChecked = 0,
	kMaterialNoReplacement = -1,
};

typedef struct {
	int mat_id;

	// TODO rendermode chain
} texture_to_material_t;

typedef struct {
	//int for_tex_id;
	string name;

	r_vk_material_t material;
} material_entry_t;

static struct {
	int count;
	material_entry_t table[MAX_MATERIALS];

	texture_to_material_t tex_to_mat[MAX_TEXTURES];

	// TODO embed into tex_to_mat
	r_vk_material_per_mode_t for_rendermode[kRenderTransAdd+1];

	// TODO for name
} g_materials;

static struct {
	int mat_files_read;
	int texture_lookups;
	int texture_loads;
	uint64_t material_file_read_duration_ns;
	uint64_t texture_lookup_duration_ns;
	uint64_t texture_load_duration_ns;
} g_stats;

static int loadTexture( const char *filename, qboolean force_reload, colorspace_hint_e colorspace ) {
	const uint64_t load_begin_ns = aprof_time_now_ns();
	const int tex_id = R_VkLoadTexture( filename, colorspace, force_reload);
	DEBUG("Loaded texture %s => %d", filename, tex_id);
	g_stats.texture_loads++;
	g_stats.texture_load_duration_ns += aprof_time_now_ns() - load_begin_ns;
	return tex_id ? tex_id : -1;
}

static void makePath(char *out, size_t out_size, const char *value, const char *path_begin, const char *path_end) {
	if (value[0] == '/') {
		// Path relative to valve/pbr dir
		Q_snprintf(out, out_size, "pbr%s", value);
	} else {
		// Path relative to current material.mat file
		Q_snprintf(out, out_size, "%.*s%s", (int)(path_end - path_begin), path_begin, value);
	}
}

#define MAKE_PATH(out, value) \
	makePath(out, sizeof(out), value, path_begin, path_end)

static void printMaterial(int index) {
	const char* const name = g_materials.table[index].name;
	const r_vk_material_t* const mat = &g_materials.table[index].material;

	DEBUG("material[%d] \"%s\" (tbc=%d, tr=%d, tm=%d, tn=%d bc=(%.03f,%.03f,%.03f,%.03f) r=%.03f m=%.03f ns=%.03f",
		index, name,
		mat->tex_base_color, mat->tex_roughness, mat->tex_metalness, mat->tex_normalmap,
		mat->base_color[0], mat->base_color[1], mat->base_color[2], mat->base_color[3],
		mat->roughness, mat->metalness, mat->normal_scale
		);
}

static int addMaterial(const char *name, const r_vk_material_t* mat) {
	if (g_materials.count == MAX_MATERIALS) {
		ERR("Max count of materials %d reached", MAX_MATERIALS);
		return -1;
	}

	Q_strncpy(g_materials.table[g_materials.count].name, name, sizeof g_materials.table[g_materials.count].name);
	g_materials.table[g_materials.count].material = *mat;

	printMaterial(g_materials.count);

	return g_materials.count++;
}

static void assignMaterialForTexture(const char *name, int for_tex_id, int mat_id) {
	const char* const tex_name = findTexture(for_tex_id)->name;
	DEBUG("Assigning material \"%s\" for_tex_id=\"%s\"(%d)", name, tex_name, for_tex_id);

	ASSERT(mat_id >= 0);
	ASSERT(mat_id < g_materials.count);

	ASSERT(for_tex_id < COUNTOF(g_materials.tex_to_mat));
	texture_to_material_t* const t2m = g_materials.tex_to_mat + for_tex_id;

	if (t2m->mat_id == kMaterialNoReplacement) {
		ERR("Texture \"%s\"(%d) has been already queried by something. Only future queries will get the new material", tex_name, for_tex_id);
	} else if (t2m->mat_id != kMaterialNotChecked) {
		ERR("Texture \"%s\"(%d) already has material assigned, will replace", tex_name, for_tex_id);
	}

	t2m->mat_id = mat_id;
}

static void loadMaterialsFromFile( const char *filename, int depth ) {
	const uint64_t load_file_begin_ns = aprof_time_now_ns();
	byte *data = gEngine.fsapi->LoadFile( filename, 0, false );
	g_stats.material_file_read_duration_ns +=  aprof_time_now_ns() - load_file_begin_ns;

	r_vk_material_t current_material = k_default_material;
	int for_tex_id = -1;
	int dummy_named_texture_fixme = -1;
	qboolean force_reload = false;
	qboolean create = false;
	qboolean metalness_set = false;

	string name;
	string basecolor_map, normal_map, metal_map, roughness_map;
	//uint32_t fields;

	int rendermode = 0;

	DEBUG("Loading materials from %s (exists=%d)", filename, data != 0);

	if ( !data )
		return;

	const char *const path_begin = filename;
	const char *path_end = Q_strrchr(filename, '/');
	if ( !path_end )
		path_end = path_begin;
	else
		path_end++;

	char *pos = (char*)data;
	for (;;) {
		char key[1024];
		char value[1024];

		const char *const line_begin = pos;
		pos = COM_ParseFile(pos, key, sizeof(key));
		ASSERT(Q_strlen(key) < sizeof(key));
		if (!pos)
			break;

		if (key[0] == '{') {
			current_material = k_default_material;
			for_tex_id = -1;
			dummy_named_texture_fixme = -1;
			force_reload = false;
			create = false;
			metalness_set = false;
			name[0] = basecolor_map[0] = normal_map[0] = metal_map[0] = roughness_map[0] = '\0';
			rendermode = 0;
			//fields = 0;
			continue;
		}

		if (key[0] == '}') {
			if (for_tex_id < 0 && !create) {
				// Skip this material, as its texture hasn't been loaded
				// NOTE: might want to check whether it makes sense wrt late-loading stuff
				continue;
			}

			if (!name[0]) {
				WARN("Unreferenceable (no \"for_texture\", no \"new\") material found in %s", filename);
				continue;
			}

#define LOAD_TEXTURE_FOR(name, field, colorspace) do { \
			if (name[0] != '\0') { \
				char texture_path[256]; \
				MAKE_PATH(texture_path, name); \
				const int tex_id = loadTexture(texture_path, force_reload, colorspace); \
				if (tex_id < 0) { \
					ERR("Failed to load texture \"%s\" for "#name"", name); \
				} else { \
					current_material.field = tex_id; \
				} \
			}} while(0)

			LOAD_TEXTURE_FOR(basecolor_map, tex_base_color, kColorspaceNative);
			LOAD_TEXTURE_FOR(normal_map, tex_normalmap, kColorspaceLinear);
			LOAD_TEXTURE_FOR(metal_map, tex_metalness, kColorspaceLinear);
			LOAD_TEXTURE_FOR(roughness_map, tex_roughness, kColorspaceLinear);

			// If there's no explicit basecolor_map value, use the "for" target texture
			if (current_material.tex_base_color == -1)
				current_material.tex_base_color = for_tex_id >= 0 ? for_tex_id : 0;

			if (!metalness_set && current_material.tex_metalness != tglob.whiteTexture) {
				// If metalness factor wasn't set explicitly, but texture was specified, set it to match the texture value.
				current_material.metalness = 1.f;
			}

			const int mat_id = addMaterial(name, &current_material);

			if (mat_id < 0) {
				ERR("Cannot add material \"%s\" for_tex_id=\"%s\"(%d)", name, for_tex_id >= 0 ? findTexture(for_tex_id)->name : "N/A", for_tex_id);
				continue;
			}

			// FIXME have a personal hash map, don't use texture
			if (dummy_named_texture_fixme > 0) {
				assignMaterialForTexture(name, dummy_named_texture_fixme, mat_id);
			}

			// Assign from-texture mapping if there's a texture
			if (for_tex_id >= 0) {
				// Assign rendermode-specific materials
				if (rendermode > 0) {
					const char* const tex_name = findTexture(for_tex_id)->name;
					DEBUG("Adding material \"%s\" for_tex_id=\"%s\"(%d) for rendermode %d", name, tex_name, for_tex_id, rendermode);

					r_vk_material_per_mode_t* const rm = g_materials.for_rendermode + rendermode;
					if (rm->count == COUNTOF(rm->map)) {
						ERR("Too many rendermode/tex_id mappings");
						continue;
					}

					rm->map[rm->count].tex_id = for_tex_id;
					rm->map[rm->count].mat.index = mat_id;
					rm->count++;
				} else {
					assignMaterialForTexture(name, for_tex_id, mat_id);
				}
			}

			continue;
		} // if (key[0] == '}') -- closing material block

		pos = COM_ParseFile(pos, value, sizeof(value));
		if (!pos)
			break;

		if (Q_stricmp(key, "for") == 0) {
			if (name[0] != '\0')
				WARN("Material already has \"new\" or \"for_texture\" old=\"%s\" new=\"%s\"", name, value);

			const uint64_t lookup_begin_ns = aprof_time_now_ns();
			for_tex_id = XVK_FindTextureNamedLike(value);
			g_stats.texture_lookup_duration_ns += aprof_time_now_ns() - lookup_begin_ns;
			g_stats.texture_lookups++;
			Q_strncpy(name, value, sizeof name);
		} else if (Q_stricmp(key, "new") == 0) {
			if (name[0] != '\0')
				WARN("Material already has \"new\" or \"for_texture\" old=\"%s\" new=\"%s\"", name, value);

			// TODO hash map here, don't depend on textures
			dummy_named_texture_fixme = XVK_CreateDummyTexture(value);
			Q_strncpy(name, value, sizeof name);
			create = true;
		} else if (Q_stricmp(key, "force_reload") == 0) {
			force_reload = Q_atoi(value) != 0;
		} else if (Q_stricmp(key, "include") == 0) {
			if (depth > 0) {
				char include_path[256];
				MAKE_PATH(include_path, value);
				loadMaterialsFromFile( include_path, depth - 1);
			} else {
				ERR("material: max include depth %d reached when including '%s' from '%s'", MAX_INCLUDE_DEPTH, value, filename);
			}
		} else {
			int *tex_id_dest = NULL;
			if (Q_stricmp(key, "basecolor_map") == 0) {
				Q_strncpy(basecolor_map, value, sizeof(basecolor_map));
				//fields |= kMatField_basecolor_map;
			} else if (Q_stricmp(key, "normal_map") == 0) {
				Q_strncpy(normal_map, value, sizeof(normal_map));
				//fields |= kMatField_normal_map;
			} else if (Q_stricmp(key, "metal_map") == 0) {
				Q_strncpy(metal_map, value, sizeof(metal_map));
				//fields |= kMatField_metal_map;
			} else if (Q_stricmp(key, "roughness_map") == 0) {
				Q_strncpy(roughness_map, value, sizeof(roughness_map));
				//fields |= kMatField_roughness_map;
			} else if (Q_stricmp(key, "inherit") == 0 || Q_stricmp(key, "use") == 0) {
				const r_vk_material_ref_t ref = R_VkMaterialGetForName(value);
				if (ref.index < 0) {
					ERR("In material \"%s\" cannot find material \"%s\" to inherit", name, value);
					continue;
				}
				const r_vk_material_t inherited = R_VkMaterialGetForRef(ref);
				current_material = inherited;
			} else if (Q_stricmp(key, "roughness") == 0) {
				sscanf(value, "%f", &current_material.roughness);
				//fields |= kMatField_roughness;
			} else if (Q_stricmp(key, "metalness") == 0) {
				sscanf(value, "%f", &current_material.metalness);
				//fields |= kMatField_metalness;
				metalness_set = true;
			} else if (Q_stricmp(key, "normal_scale") == 0) {
				sscanf(value, "%f", &current_material.normal_scale);
				//fields |= kMatField_normal_scale;
			} else if (Q_stricmp(key, "base_color") == 0) {
				sscanf(value, "%f %f %f %f", &current_material.base_color[0], &current_material.base_color[1], &current_material.base_color[2], &current_material.base_color[3]);
				//fields |= kMatField_base_color;
			} else if (Q_stricmp(key, "for_rendermode") == 0) {
				rendermode = R_VkRenderModeFromString(value);
				if (rendermode < 0)
					ERR("Invalid rendermode \"%s\"", value);
				ASSERT(rendermode < COUNTOF(g_materials.for_rendermode[0].map));
				//fields |= kMatField_rendermode;
			} else {
				ERR("Unknown material key \"%s\" on line `%.*s`", key, (int)(pos - line_begin), line_begin);
				continue;
			}
		}
	}

	Mem_Free( data );
	g_stats.mat_files_read++;
}

static void loadMaterialsFromFileF( const char *fmt, ... ) {
	char buffer[256];
	va_list argptr;

	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	loadMaterialsFromFile( buffer, MAX_INCLUDE_DEPTH );
}

static int findFilenameExtension(const char *s, int len) {
	if (len < 0)
		len = Q_strlen(s);

	for (int i = len - 1; i >= 0; --i) {
		if (s[i] == '.')
			return i;
	}

	return len;
}

void R_VkMaterialsReload( void ) {
	const uint64_t begin_time_ns = aprof_time_now_ns();
	memset(&g_stats, 0, sizeof(g_stats));

	g_materials.count = 1;

	memset(g_materials.tex_to_mat, 0, sizeof g_materials.tex_to_mat);

	for (int i = 0; i < COUNTOF(g_materials.for_rendermode); ++i)
		g_materials.for_rendermode[i].count = 0;

	// TODO make these texture constants static constants
	k_default_material.tex_metalness = tglob.whiteTexture;
	k_default_material.tex_roughness = tglob.whiteTexture;

	// TODO name?
	g_materials.table[0].material = k_default_material;
	g_materials.table[0].material.tex_base_color = 0;

	loadMaterialsFromFile( "pbr/materials.mat", MAX_INCLUDE_DEPTH );

	// Load materials by WAD files
	{
		for(const char *wad = g_map_entities.wadlist; *wad;) {
			const char *wad_end = wad;
			const char *ext = NULL;
			while (*wad_end && *wad_end != ';') {
				if (*wad_end == '.')
					ext = wad_end;
				++wad_end;
			}

			const int full_length = wad_end - wad;

			// Length without extension
			const int short_length = ext ? ext - wad : full_length;

			loadMaterialsFromFileF("pbr/%.*s/%.*s.mat", full_length, wad, short_length, wad);
			wad = wad_end + 1;
		}
	}

	// Load materials by map/BSP file
	{
		const model_t *map = gEngine.pfnGetModelByIndex( 1 );
		const char *filename = COM_FileWithoutPath(map->name);
		const int no_ext_len = findFilenameExtension(filename, -1);
		loadMaterialsFromFileF("pbr/%s/%.*s.mat", map->name, no_ext_len, filename);
	}

	// Print out statistics
	{
		const int duration_ms = (aprof_time_now_ns() - begin_time_ns) / 1000000ull;
		INFO("Loading materials took %dms, .mat files parsed: %d (fread: %dms). Texture lookups: %d (%dms). Texture loads: %d (%dms).",
			duration_ms,
			g_stats.mat_files_read,
			(int)(g_stats.material_file_read_duration_ns / 1000000ull),
			g_stats.texture_lookups,
			(int)(g_stats.texture_lookup_duration_ns / 1000000ull),
			g_stats.texture_loads,
			(int)(g_stats.texture_load_duration_ns / 1000000ull)
			);
	}
}

void R_VkMaterialsLoadForModel( const struct model_s* mod ) {
	// Brush models are loaded separately
	if (mod->type == mod_brush)
		return;

	// TODO add stats

	const char *filename = COM_FileWithoutPath(mod->name);
	const int no_ext_len = findFilenameExtension(filename, -1);
	loadMaterialsFromFileF("pbr/%s/%.*s.mat", mod->name, no_ext_len, filename);
}

r_vk_material_t R_VkMaterialGetForTexture( int tex_index ) {
	return R_VkMaterialGetForTextureWithFlags( tex_index, kVkMaterialFlagNone );
}

r_vk_material_t R_VkMaterialGetForTextureWithFlags( int tex_index, uint32_t flags ) {
	//DEBUG("Getting material for tex_id=%d", tex_index);
	ASSERT(tex_index >= 0);
	ASSERT(tex_index < MAX_TEXTURES);

	texture_to_material_t* const t2m = g_materials.tex_to_mat + tex_index;

	if (t2m->mat_id > 0) {
		ASSERT(t2m->mat_id < g_materials.count);
		//DEBUG("Getting material for tex_id=%d", tex_index);
		//printMaterial(t2m->mat_id);
		return g_materials.table[t2m->mat_id].material;
	}

	if (t2m->mat_id == kMaterialNotChecked) {
		// TODO check for replacement textures named in a predictable way
		// If there are, create a new material and assign it here

		const char* texname = findTexture(tex_index)->name;
		DEBUG("Would try to load texture files by default names of \"%s\"", texname);

		// If no PBR textures found, continue using legacy+default ones
		t2m->mat_id = kMaterialNoReplacement;
	}

	r_vk_material_t ret = k_default_material;
	ret.tex_base_color = tex_index;

	if ( flags & kVkMaterialFlagChrome )
		ret.roughness = tglob.grayTexture;

	//DEBUG("Returning default material with tex_base_color=%d", tex_index);
	return ret;
}

r_vk_material_ref_t R_VkMaterialGetForName( const char *name ) {
	// FIXME proper hash table here, don't depend on textures
	const int dummy_tex_id_fixme = VK_FindTexture(name);
	if (dummy_tex_id_fixme == 0) {
		ERR("Material with name \"%s\" not found", name);
		return (r_vk_material_ref_t){.index = -1,};
	}

	ASSERT(dummy_tex_id_fixme >= 0);
	ASSERT(dummy_tex_id_fixme < MAX_TEXTURES);

	return (r_vk_material_ref_t){.index = g_materials.tex_to_mat[dummy_tex_id_fixme].mat_id};
}

r_vk_material_t R_VkMaterialGetForRef( r_vk_material_ref_t ref ) {
	if (ref.index < 0) {
		r_vk_material_t ret = k_default_material;
		ret.tex_base_color = 0; // Default/error texture
		return ret;
	}

	ASSERT(ref.index < g_materials.count);
	return g_materials.table[ref.index].material;
}

qboolean R_VkMaterialGetEx( int tex_id, int rendermode, r_vk_material_t *out_material ) {
	DEBUG("Getting material for tex_id=%d rendermode=%d", tex_id, rendermode);

	if (rendermode == 0) {
		WARN("rendermode==0: fallback to regular tex_id=%d", tex_id);
		*out_material = R_VkMaterialGetForTexture(tex_id);
		return true;
	}

	// TODO move rendermode-specifit things to by-texid-chains
	ASSERT(rendermode < COUNTOF(g_materials.for_rendermode));
	const r_vk_material_per_mode_t* const mode = &g_materials.for_rendermode[rendermode];
	for (int i = 0; i < mode->count; ++i) {
		if (mode->map[i].tex_id == tex_id) {
			const int index = mode->map[i].mat.index;
			ASSERT(index >= 0);
			ASSERT(index < g_materials.count);
			*out_material = g_materials.table[index].material;
			return true;
		}
	}

	return false;
}
