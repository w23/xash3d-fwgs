#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_mapents.h"
#include "vk_const.h"
#include "profiler.h"

#include <stdio.h>

#define MAX_INCLUDE_DEPTH 4

static xvk_material_t k_default_material = {
		.tex_base_color = -1,
		.tex_metalness = 0,
		.tex_roughness = 0,
		.tex_normalmap = 0,

		.metalness = 0.f,
		.roughness = 1.f,
		.normal_scale = 1.f,
		.base_color = { 1.f, 1.f, 1.f, 1.f },

		.set = false,
};

static struct {
	xvk_material_t materials[MAX_TEXTURES];

	qboolean verbose_logs;
} g_materials;

static struct {
	int mat_files_read;
	int texture_lookups;
	int texture_loads;
	uint64_t material_file_read_duration_ns;
	uint64_t texture_lookup_duration_ns;
	uint64_t texture_load_duration_ns;
} g_stats;

static int loadTexture( const char *filename, qboolean force_reload ) {
	const uint64_t load_begin_ns = aprof_time_now_ns();
	const int tex_id = force_reload ? XVK_LoadTextureReplace( filename, NULL, 0, 0 ) : VK_LoadTexture( filename, NULL, 0, 0 );
	if (g_materials.verbose_logs)
		gEngine.Con_Reportf("Loaded texture %s => %d\n", filename, tex_id);
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

static void loadMaterialsFromFile( const char *filename, int depth ) {
	fs_offset_t size;
	const char *const path_begin = filename;
	const char *path_end = Q_strrchr(filename, '/');

	const uint64_t load_file_begin_ns = aprof_time_now_ns();
	byte *data = gEngine.fsapi->LoadFile( filename, 0, false );
	g_stats.material_file_read_duration_ns +=  aprof_time_now_ns() - load_file_begin_ns;

	char *pos = (char*)data;
	xvk_material_t current_material = k_default_material;
	int current_material_index = -1;
	qboolean force_reload = false;
	qboolean create = false;
	qboolean metalness_set = false;

	string basecolor_map, normal_map, metal_map, roughness_map;

	if (g_materials.verbose_logs)
		gEngine.Con_Reportf("Loading materials from %s\n", filename);

	if ( !data )
		return;

	if ( !path_end )
		path_end = path_begin;
	else
		path_end++;

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
			current_material_index = -1;
			force_reload = false;
			create = false;
			metalness_set = false;
			basecolor_map[0] = normal_map[0] = metal_map[0] = roughness_map[0] = '\0';
			continue;
		}

		if (key[0] == '}') {
			if (current_material_index < 0)
				continue;

#define LOAD_TEXTURE_FOR(name, field) do { \
			if (name[0] != '\0') { \
				char texture_path[256]; \
				MAKE_PATH(texture_path, name); \
				const int tex_id = loadTexture(texture_path, force_reload); \
				if (tex_id < 0) { \
					gEngine.Con_Printf(S_ERROR "Failed to load texture \"%s\" for "#name"\n", name); \
				} else { \
					current_material.field = tex_id; \
				} \
			}} while(0)

			LOAD_TEXTURE_FOR(basecolor_map, tex_base_color);
			LOAD_TEXTURE_FOR(normal_map, tex_normalmap);
			LOAD_TEXTURE_FOR(metal_map, tex_metalness);
			LOAD_TEXTURE_FOR(roughness_map, tex_roughness);

			// If there's no explicit basecolor_map value, use the "for" target texture
			if (current_material.tex_base_color == -1)
				current_material.tex_base_color = current_material_index;

			if (metalness_set && current_material.tex_metalness == tglob.blackTexture) {
				// Set metalness texture to white to accommodate explicitly set metalness value
				current_material.tex_metalness = tglob.whiteTexture;
			}

			if (!metalness_set && current_material.tex_metalness != tglob.blackTexture) {
				// If metalness factor wasn't set explicitly, but texture was specified, set it to match the texture value.
				current_material.metalness = 1.f;
			}

			if (g_materials.verbose_logs)
				gEngine.Con_Reportf("Creating%s material for texture %s(%d)\n", create?" new":"",
					findTexture(current_material_index)->name, current_material_index);

			g_materials.materials[current_material_index] = current_material;
			g_materials.materials[current_material_index].set = true;
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		if (!pos)
			break;

		if (Q_stricmp(key, "for") == 0) {
			const uint64_t lookup_begin_ns = aprof_time_now_ns();
			current_material_index = XVK_FindTextureNamedLike(value);
			g_stats.texture_lookup_duration_ns += aprof_time_now_ns() - lookup_begin_ns;
			g_stats.texture_lookups++;
			create = false;
		} else if (Q_stricmp(key, "new") == 0) {
			current_material_index = XVK_CreateDummyTexture(value);
			create = true;
		} else if (Q_stricmp(key, "force_reload") == 0) {
			force_reload = Q_atoi(value) != 0;
		} else if (Q_stricmp(key, "include") == 0) {
			if (depth > 0) {
				char include_path[256];
				MAKE_PATH(include_path, value);
				loadMaterialsFromFile( include_path, depth - 1);
			} else {
				gEngine.Con_Printf(S_ERROR "material: max include depth %d reached when including '%s' from '%s'\n", MAX_INCLUDE_DEPTH, value, filename);
			}
		} else {
			int *tex_id_dest = NULL;
			if (Q_stricmp(key, "basecolor_map") == 0) {
				Q_strncpy(basecolor_map, value, sizeof(basecolor_map));
			} else if (Q_stricmp(key, "normal_map") == 0) {
				Q_strncpy(normal_map, value, sizeof(normal_map));
			} else if (Q_stricmp(key, "metal_map") == 0) {
				Q_strncpy(metal_map, value, sizeof(metal_map));
			} else if (Q_stricmp(key, "roughness_map") == 0) {
				Q_strncpy(roughness_map, value, sizeof(roughness_map));
			} else if (Q_stricmp(key, "roughness") == 0) {
				sscanf(value, "%f", &current_material.roughness);
			} else if (Q_stricmp(key, "metalness") == 0) {
				sscanf(value, "%f", &current_material.metalness);
				metalness_set = true;
			} else if (Q_stricmp(key, "normal_scale") == 0) {
				sscanf(value, "%f", &current_material.normal_scale);
			} else if (Q_stricmp(key, "base_color") == 0) {
				sscanf(value, "%f %f %f %f", &current_material.base_color[0], &current_material.base_color[1], &current_material.base_color[2], &current_material.base_color[3]);
			} else {
				gEngine.Con_Printf(S_ERROR "Unknown material key \"%s\" on line `%.*s`\n", key, (int)(pos - line_begin), line_begin);
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

void XVK_ReloadMaterials( void ) {
	memset(&g_stats, 0, sizeof(g_stats));
	const uint64_t begin_time_ns = aprof_time_now_ns();

	k_default_material.tex_metalness = tglob.blackTexture;
	k_default_material.tex_roughness = tglob.whiteTexture;

	for (int i = 0; i < MAX_TEXTURES; ++i) {
		xvk_material_t *const mat = g_materials.materials + i;
		const vk_texture_t *const tex = findTexture( i );
		*mat = k_default_material;

		if (tex)
			mat->tex_base_color = i;
	}

	loadMaterialsFromFile( "pbr/materials.mat", MAX_INCLUDE_DEPTH );
	loadMaterialsFromFile( "pbr/models/models.mat", MAX_INCLUDE_DEPTH );
	loadMaterialsFromFile( "pbr/sprites/sprites.mat", MAX_INCLUDE_DEPTH );

	{
		const char *wad = g_map_entities.wadlist;
		for (; *wad;) {
			const char *const wad_end = Q_strchr(wad, ';');
			loadMaterialsFromFileF("pbr/%.*s/%.*s.mat", wad_end - wad, wad, wad_end - wad, wad);
			wad = wad_end + 1;
		}
	}

	{
		const model_t *map = gEngine.pfnGetModelByIndex( 1 );
		loadMaterialsFromFileF("pbr/%s/%s.mat", map->name, COM_FileWithoutPath(map->name));
	}

	// Print out statistics
	{
		const int duration_ms = (aprof_time_now_ns() - begin_time_ns) / 1000000ull;
		gEngine.Con_Printf("Loading materials took %dms, .mat files parsed: %d (fread: %dms). Texture lookups: %d (%dms). Texture loads: %d (%dms).\n",
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

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index ) {
	xvk_material_t *mat = NULL;
	ASSERT(tex_index >= 0);
	ASSERT(tex_index < MAX_TEXTURES);

	mat = g_materials.materials + tex_index;
	if (mat->base_color >= 0)
		return mat;

	return NULL;
}
