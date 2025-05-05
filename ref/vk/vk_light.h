#pragma once

#include "xash3d_types.h"

qboolean VK_LightsInit( void );
void VK_LightsShutdown( void );

// Allocate clusters and vis data for the new map
struct model_s;
void RT_LightsNewMap( const struct model_s *map );

// Clear light data and prepare for loading
// RT_LightsNewMap should have been already called for current map
void RT_LightsLoadBegin( const struct model_s *map );
// Finalize loading light data, i.e. mark everything loaded so far as static light data
void RT_LightsLoadEnd( void );

// TODO can we call it from produce/consumed?
void RT_LightsFrameBegin( void );

qboolean RT_GetEmissiveForTexture( vec3_t out, int texture_id );

int RT_LightCellIndex( const int light_cell[3] );

struct cl_entity_s;
void RT_LightAddFlashlight( const struct cl_entity_s *ent, qboolean local_player );

struct msurface_s;
typedef struct rt_light_add_polygon_s {
	int num_vertices;
	vec3_t vertices[7];

	vec3_t emissive;

	// Needed for BSP visibilty purposes
	// TODO can we layer light code? like:
	// - bsp/xash/rad/patch-specific stuff
	// - mostly engine-agnostic light clusters
	const struct msurface_s *surface;

	qboolean dynamic;
	const matrix3x4 *transform_row;
} rt_light_add_polygon_t;
int RT_LightAddPolygon(const rt_light_add_polygon_t *light);

char *RT_LightPrintCellInfo(char *p, char *const end, vec3_t pos);
