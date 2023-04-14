#pragma once
#include "xash3d_types.h"

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
	X(14, string, _xvk_texture, String) \
	X(15, int_array_t, _xvk_ent_id, IntArray) \
	X(16, float, _xvk_radius, Float) \
	X(17, vec4_t, _xvk_svec, Vec4) \
	X(18, vec4_t, _xvk_tvec, Vec4) \
	X(19, vec2_t, _xvk_tex_offset, Vec2) \
	X(20, vec2_t, _xvk_tex_scale, Vec2) \
	X(21, string, model, String) \
	X(22, int, rendermode, Int) \
	X(23, int, renderamt, Int) \
	X(24, vec3_t, rendercolor, Vec3) \
	X(25, int, renderfx, Int) \
	X(26, vec3_t, _xvk_offset, Vec3) \

typedef enum {
	Unknown = 0,
	Light,
	LightSpot,
	LightEnvironment,
	Worldspawn,
	FuncWall,
	Ignored,
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
	string model;
	int rendermode, renderamt, renderfx;
	color24 rendercolor;

	struct cl_entity_s *ent;
	vec3_t offset;
} xvk_mapent_func_wall_t;

typedef struct {
	int num_lights;
	vk_light_entity_t lights[256];

	int single_environment_index;
	int entity_count;

	string wadlist;

	int num_targets;
	xvk_mapent_target_t targets[MAX_MAPENT_TARGETS];

#define MAX_FUNC_WALL_ENTITIES 64
	int func_walls_count;
	xvk_mapent_func_wall_t func_walls[MAX_FUNC_WALL_ENTITIES];
} xvk_map_entities_t;

extern xvk_map_entities_t g_map_entities;

enum { NoEnvironmentLights = -1, MoreThanOneEnvironmentLight = -2 };

void XVK_ParseMapEntities( void );
void XVK_ParseMapPatches( void );

enum {
	Patch_Surface_NoPatch = 0,
	Patch_Surface_Delete = (1<<0),
	Patch_Surface_Texture = (1<<1),
	Patch_Surface_Emissive = (1<<2),
	Patch_Surface_STvecs = (1<<3),
	Patch_Surface_TexOffset = (1<<4),
	Patch_Surface_TexScale = (1<<5),
};

struct texture_s;

typedef struct {
	uint32_t flags;

	// Static texture index in case there's no texture_s pointer
	int tex_id;

	// Pointer to texture_s data (which also may include animation)
	const struct texture_s *tex;

	vec3_t emissive;

	// Texture coordinate patches
	vec4_t s_vec, t_vec;
	vec2_t tex_offset, tex_scale;
} xvk_patch_surface_t;

const xvk_patch_surface_t* R_VkPatchGetSurface( int surface_index );

