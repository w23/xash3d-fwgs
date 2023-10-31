#pragma once
#include "vk_materials.h"

#include "xash3d_types.h"
#include "const.h" // typedef word, needed for bspfile.h
#include "bspfile.h" // MAX_MAP_ENTITIES

// TODO string_view instead of string. map entities string/buffer is supposed to be alive for the entire map duration
// NOTE that the above is not true for string in patches. but we can change that in parsePatches

#define ENT_PROP_LIST(X) \
	X(0, vec3_t, origin, Vec3) \
	X(1, vec3_t, angles, Vec3) \
	X(2, float, pitch, Float) \
	X(3, vec3_t, _light, Rgbav) \
	X(4, class_name_e, classname, Classname) \
	X(5, float, angle, Float) \
	X(6, float, _cone, Float) \
	X(7, float, _cone2, Float) \
	X(8, int, _sky, Int) \
	X(9, string, wad, WadList) \
	X(10, string, targetname, String) \
	X(11, string, target, String) \
	X(12, int, style, Int) \
	X(13, int_array_t, _xvk_surface_id, IntArray) \
	X(14, string, _xvk_material, String) \
	X(15, int_array_t, _xvk_ent_id, IntArray) \
	X(16, float, _xvk_radius, Float) \
	X(17, vec4_t, _xvk_svec, Vec4) \
	X(18, vec4_t, _xvk_tvec, Vec4) \
	X(19, vec2_t, _xvk_tex_offset, Vec2) \
	X(20, vec2_t, _xvk_tex_scale, Vec2) \
	X(21, string, model, String) \
	X(22, float, _xvk_smoothing_threshold, Float) \
	X(23, int_array_t, _xvk_smoothing_excluded_pairs, IntArray) \
	X(24, int_array_t, _xvk_smoothing_group, IntArray) \
	X(25, string, _xvk_map_material, String) \
	X(26, int, rendermode, Int) \
	X(27, int, _xvk_smooth_entire_model, Int) \
	X(28, int_array_t, _xvk_smoothing_excluded, IntArray) \

/* NOTE: not used
	X(23, int, renderamt, Int) \
	X(24, vec3_t, rendercolor, Vec3) \
	X(25, int, renderfx, Int) \
	X(26, vec3_t, _xvk_offset, Vec3) \
*/

typedef enum {
	Unknown = 0,
	Light,
	LightSpot,
	LightEnvironment,
	Worldspawn,
	FuncAny,
	Ignored,
	Xvk_Target,
} class_name_e;

#define MAX_INT_ARRAY_SIZE 64

typedef struct {
	int num;
	int values[MAX_INT_ARRAY_SIZE];
} int_array_t;

typedef struct {
#define DECLARE_FIELD(num, type, name, kind) type name;
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} entity_props_t;

typedef enum {
	None = 0,
#define DECLARE_FIELD(num, type, name, kind) Field_##name = (1<<num),
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} fields_read_e;

typedef enum { LightTypePoint, LightTypeSurface, LightTypeSpot, LightTypeEnvironment} LightType;

typedef struct {
	int entity_index;
	LightType type;

	vec3_t origin;
	vec3_t color;
	vec3_t dir;

	float radius;

	int style;
	float stopdot, stopdot2;

	string target_entity;
} vk_light_entity_t;

typedef struct {
	string targetname;
	vec3_t origin;
} xvk_mapent_target_t;

#define MAX_MAPENT_TARGETS 256

typedef struct {
	int entity_index;
	string model;
	vec3_t origin;

	qboolean origin_patched;

#define MAX_MATERIAL_MAPPINGS 8
	int matmap_count;
	struct {
		int from_tex;
		r_vk_material_ref_t to_mat;
	} matmap[MAX_MATERIAL_MAPPINGS];

	int rendermode;

	qboolean smooth_entire_model;

	/* NOTE: not used. Might be needed for #118 in the future.
	int renderamt, renderfx;
	color24 rendercolor;

	struct cl_entity_s *ent;
	*/
} xvk_mapent_func_any_t;

typedef struct {
	class_name_e class;
	int index;
} xvk_mapent_ref_t;

#define MAX_INCLUDED_SMOOTHING_SURFACES_IN_A_GROUP 16
typedef struct {
	int count;
	int surfaces[MAX_INCLUDED_SMOOTHING_SURFACES_IN_A_GROUP];
} xvk_smoothing_group_t;

typedef struct {
	int num_lights;
	vk_light_entity_t lights[256];

	int single_environment_index;
	int entity_count;

	string wadlist;

	int num_targets;
	xvk_mapent_target_t targets[MAX_MAPENT_TARGETS];

#define MAX_FUNC_ANY_ENTITIES 1024
	int func_any_count;
	xvk_mapent_func_any_t func_any[MAX_FUNC_ANY_ENTITIES];

	// TODO find out how to read this from the engine, or make its size dynamic
//#define MAX_MAP_ENTITIES 2048
	xvk_mapent_ref_t refs[MAX_MAP_ENTITIES];

	struct {
		float threshold;

#define MAX_EXCLUDED_SMOOTHING_SURFACES_PAIRS 32
		int excluded_pairs[MAX_EXCLUDED_SMOOTHING_SURFACES_PAIRS * 2];
		int excluded_pairs_count;

#define MAX_INCLUDED_SMOOTHING_GROUPS 32
		int groups_count;
		xvk_smoothing_group_t groups[MAX_INCLUDED_SMOOTHING_GROUPS];

#define MAX_EXCLUDED_SMOOTHING_SURFACES 256
		int excluded_count;
		int excluded[MAX_EXCLUDED_SMOOTHING_SURFACES];
	} smoothing;
} xvk_map_entities_t;

// TODO expose a bunch of things here as funtions, not as internal structures
extern xvk_map_entities_t g_map_entities;

enum { NoEnvironmentLights = -1, MoreThanOneEnvironmentLight = -2 };

void XVK_ParseMapEntities( void );
void XVK_ParseMapPatches( void );

enum {
	Patch_Surface_NoPatch = 0,
	Patch_Surface_Delete = (1<<0),
	Patch_Surface_Material = (1<<1),
	Patch_Surface_Emissive = (1<<2),
	Patch_Surface_STvecs = (1<<3),
	Patch_Surface_TexOffset = (1<<4),
	Patch_Surface_TexScale = (1<<5),
};

struct texture_s;

typedef struct {
	uint32_t flags;

	r_vk_material_ref_t material_ref;

	vec3_t emissive;

	// Texture coordinate patches
	vec4_t s_vec, t_vec;
	vec2_t tex_offset, tex_scale;
} xvk_patch_surface_t;

const xvk_patch_surface_t* R_VkPatchGetSurface( int surface_index );

// -1 if failed
int R_VkRenderModeFromString( const char *s );
