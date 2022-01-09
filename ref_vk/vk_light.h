#pragma once

#include "vk_const.h"

#include "xash3d_types.h"
#include "protocol.h"
#include "const.h"
#include "bspfile.h"

typedef struct {
	vec3_t emissive;
	qboolean set;
} vk_emissive_texture_t;

typedef struct {
	uint8_t num_point_lights;
	uint8_t num_emissive_surfaces;
	uint8_t point_lights[MAX_VISIBLE_POINT_LIGHTS];
	uint8_t emissive_surfaces[MAX_VISIBLE_SURFACE_LIGHTS];

	struct {
		uint8_t point_lights;
		uint8_t emissive_surfaces;
	} num_static;
} vk_lights_cell_t;

typedef struct {
	vec3_t emissive;
	uint32_t kusok_index;
	matrix3x4 transform;
} vk_emissive_surface_t;

enum {
	LightFlag_Environment = 0x1,
};

typedef struct {
	vec3_t origin;
	vec3_t color;
	vec3_t dir;
	float stopdot, stopdot2;
	float radius;
	int flags;

	int lightstyle;
	vec3_t base_color;
} vk_point_light_t;

// TODO spotlight

typedef struct {
	struct {
		int grid_min_cell[3];
		int grid_size[3];
		int grid_cells;

		vk_emissive_texture_t emissive_textures[MAX_TEXTURES];
	} map;

	int num_emissive_surfaces;
	vk_emissive_surface_t emissive_surfaces[MAX_SURFACE_LIGHTS];

	int num_point_lights;
	vk_point_light_t point_lights[MAX_POINT_LIGHTS];

	struct {
		int emissive_surfaces;
		int point_lights;
	} num_static;

	vk_lights_cell_t cells[MAX_LIGHT_CLUSTERS];
} vk_lights_t;

extern vk_lights_t g_lights;

void VK_LightsInit( void );
void VK_LightsShutdown( void );

void VK_LightsNewMap( void );
void VK_LightsLoadMapStaticLights( void );

void VK_LightsFrameInit( void );

// TODO there is an arguably better way to organize this.
// a. this only belongs to ray tracing mode
// b. kusochki now have emissive color, so it probably makes more sense to not store emissive
//    separately in emissive surfaces.
struct vk_render_geometry_s;
void VK_LightsAddEmissiveSurface( const struct vk_render_geometry_s *geom, const matrix3x4 *transform_row, qboolean static_map );
void XVK_GetEmissiveForTexture( vec3_t out, int texture_id );

void VK_LightsFrameFinalize( void );

int R_LightCellIndex( const int light_cell[3] );

struct cl_entity_s;
void R_LightAddFlashlight( const struct cl_entity_s *ent, qboolean local_player );
