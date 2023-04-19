#include "vk_common.h"
#include "vk_mapents.h"
#include "vk_core.h" // TODO we need only pool from there, not the entire vulkan garbage
#include "vk_textures.h"

#include "eiface.h" // ARRAYSIZE
#include "xash3d_mathlib.h"
#include <string.h>
#include <ctype.h>

xvk_map_entities_t g_map_entities;

static struct {
	xvk_patch_surface_t *surfaces;
	int surfaces_count;
} g_patch;

static unsigned parseEntPropWadList(const char* value, string *out, unsigned bit) {
	int dst_left = sizeof(string) - 2; // ; \0
	char *dst = *out;
	*dst = '\0';
	gEngine.Con_Reportf("WADS: %s\n", value);

	for (; *value;) {
		const char *file_begin = value;

		for (; *value && *value != ';'; ++value) {
			if (*value == '\\' || *value == '/')
				file_begin = value + 1;
		}

		{
			const int len = value - file_begin;
			gEngine.Con_Reportf("WAD: %.*s\n", len, file_begin);

			if (len < dst_left) {
				Q_strncpy(dst, file_begin, len + 1);
				dst += len;
				dst[0] = ';';
				dst++;
				dst[0] = '\0';
				dst_left -= len;
			}
		}

		if (*value) value++;
	}

	gEngine.Con_Reportf("wad list: %s\n", *out);
	return bit;
}

static unsigned parseEntPropFloat(const char* value, float *out, unsigned bit) {
	return (1 == sscanf(value, "%f", out)) ? bit : 0;
}

static unsigned parseEntPropInt(const char* value, int *out, unsigned bit) {
	return (1 == sscanf(value, "%d", out)) ? bit : 0;
}

static unsigned parseEntPropIntArray(const char* value, int_array_t *out, unsigned bit) {
	unsigned retval = 0;
	out->num = 0;
	while (*value) {
		int i = 0;
		if (0 == sscanf(value, "%d", &i))
			break;

		if (out->num == MAX_INT_ARRAY_SIZE)
			break;

		retval |= bit;

		out->values[out->num++] = i;

		while (*value && isdigit(*value)) ++value;
		while (*value && isspace(*value)) ++value;
	}

	if (*value) {
		gEngine.Con_Printf(S_ERROR "Error parsing mapents patch IntArray (wrong format? too many entries (max=%d)), portion not parsed: %s\n", MAX_INT_ARRAY_SIZE, value);
	}
	return retval;
}

static unsigned parseEntPropString(const char* value, string *out, unsigned bit) {
	const int len = Q_strlen(value);
	if (len >= sizeof(string))
		gEngine.Con_Printf(S_ERROR "Map entity value '%s' is too long, max length is %d\n",
			value, (int)sizeof(string));
	Q_strncpy(*out, value, sizeof(*out));
	return bit;
}

static unsigned parseEntPropVec2(const char* value, vec2_t *out, unsigned bit) {
	return (2 == sscanf(value, "%f %f", &(*out)[0], &(*out)[1])) ? bit : 0;
}

static unsigned parseEntPropVec3(const char* value, vec3_t *out, unsigned bit) {
	return (3 == sscanf(value, "%f %f %f", &(*out)[0], &(*out)[1], &(*out)[2])) ? bit : 0;
}

static unsigned parseEntPropVec4(const char* value, vec4_t *out, unsigned bit) {
	return (4 == sscanf(value, "%f %f %f %f", &(*out)[0], &(*out)[1], &(*out)[2], &(*out)[3])) ? bit : 0;
}

static unsigned parseEntPropRgbav(const char* value, vec3_t *out, unsigned bit) {
	float scale = 1.f;
	const int components = sscanf(value, "%f %f %f %f", &(*out)[0], &(*out)[1], &(*out)[2], &scale);
	if (components == 1) {
		(*out)[2] = (*out)[1] = (*out)[0] = (*out)[0];
		return bit;
	} else if (components == 4) {
		scale /= 255.f;
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	} else if (components == 3) {
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	}

	return 0;
}

static unsigned parseEntPropClassname(const char* value, class_name_e *out, unsigned bit) {
	if (Q_strcmp(value, "light") == 0) {
		*out = Light;
	} else if (Q_strcmp(value, "light_spot") == 0) {
		*out = LightSpot;
	} else if (Q_strcmp(value, "light_environment") == 0) {
		*out = LightEnvironment;
	} else if (Q_strcmp(value, "worldspawn") == 0) {
		*out = Worldspawn;
	} else if (Q_strcmp(value, "func_wall") == 0) {
		*out = FuncWall;
	} else {
		*out = Ignored;
	}

	return bit;
}

static void weirdGoldsrcLightScaling( vec3_t intensity ) {
	float l1 = Q_max( intensity[0], Q_max( intensity[1], intensity[2] ) );
	l1 = l1 * l1 / 10;
	VectorScale( intensity, l1, intensity );
}

static void parseAngles( const entity_props_t *props, vk_light_entity_t *le) {
	float angle = props->angle;
	VectorSet( le->dir, 0, 0, 0 );

	if (angle == -1) { // UP
		le->dir[0] = le->dir[1] = 0;
		le->dir[2] = 1;
	} else if (angle == -2) { // DOWN
		le->dir[0] = le->dir[1] = 0;
		le->dir[2] = -1;
	} else {
		if (angle == 0) {
			angle = props->angles[1];
		}

		angle *= M_PI / 180.f;

		le->dir[2] = 0;
		le->dir[0] = cosf(angle);
		le->dir[1] = sinf(angle);
	}

	angle = props->pitch ? props->pitch : props->angles[0];

	angle *= M_PI / 180.f;
	le->dir[2] = sinf(angle);
	le->dir[0] *= cosf(angle);
	le->dir[1] *= cosf(angle);
}

static void parseStopDot( const entity_props_t *props, vk_light_entity_t *le) {
	le->stopdot = props->_cone ? props->_cone : 10;
	le->stopdot2 = Q_max(le->stopdot, props->_cone2);

	le->stopdot = cosf(le->stopdot * M_PI / 180.f);
	le->stopdot2 = cosf(le->stopdot2 * M_PI / 180.f);
}

static void fillLightFromProps( vk_light_entity_t *le, const entity_props_t *props, unsigned have_fields, qboolean patch, int entity_index ) {
	switch (le->type) {
		case LightTypePoint:
			break;

		case LightTypeSpot:
		case LightTypeEnvironment:
			if (!patch || (have_fields & Field_pitch) || (have_fields & Field_angles) || (have_fields & Field_angle)) {
				parseAngles(props, le);
			}

			if (!patch || (have_fields & Field__cone) || (have_fields & Field__cone2)) {
				parseStopDot(props, le);
			}
			break;

		default:
			ASSERT(false);
	}

	if (have_fields & Field_target)
		Q_strcpy(le->target_entity, props->target);

	if (have_fields & Field_origin)
		VectorCopy(props->origin, le->origin);

	if (have_fields & Field__light)
	{
		VectorCopy(props->_light, le->color);
	} else if (!patch) {
		// same as qrad
		VectorSet(le->color, 300, 300, 300);
	}

	if (have_fields & Field__xvk_radius) {
		le->radius = props->_xvk_radius;
	}

	if (have_fields & Field_style) {
		le->style = props->style;
	}

	if (le->type != LightEnvironment && (!patch || (have_fields & Field__light))) {
		weirdGoldsrcLightScaling(le->color);
	}

	gEngine.Con_Reportf("%s light %d (ent=%d): %s targetname=%s color=(%f %f %f) origin=(%f %f %f) style=%d R=%f dir=(%f %f %f) stopdot=(%f %f)\n",
		patch ? "Patch" : "Added",
		g_map_entities.num_lights, entity_index,
		le->type == LightTypeEnvironment ? "environment" : le->type == LightTypeSpot ? "spot" : "point",
		props->targetname,
		le->color[0], le->color[1], le->color[2],
		le->origin[0], le->origin[1], le->origin[2],
		le->style,
		le->radius,
		le->dir[0], le->dir[1], le->dir[2],
		le->stopdot, le->stopdot2);
}

static void addLightEntity( const entity_props_t *props, unsigned have_fields ) {
	const int index = g_map_entities.num_lights;
	vk_light_entity_t *le = g_map_entities.lights + index;
	unsigned expected_fields = 0;

	if (g_map_entities.num_lights == ARRAYSIZE(g_map_entities.lights)) {
		gEngine.Con_Printf(S_ERROR "Too many lights entities in map\n");
		return;
	}

	*le = (vk_light_entity_t){0};

	switch (props->classname) {
		case Light:
			le->type = LightTypePoint;
			expected_fields = Field_origin;
			break;

		case LightSpot:
			if ((have_fields & Field__sky) && props->_sky != 0) {
				le->type = LightTypeEnvironment;
				expected_fields = Field__cone | Field__cone2;
			} else {
				le->type = LightTypeSpot;
				expected_fields = Field_origin | Field__cone | Field__cone2;
			}
			break;

		case LightEnvironment:
			le->type = LightTypeEnvironment;
			break;

		default:
			ASSERT(false);
	}

	if ((have_fields & expected_fields) != expected_fields) {
		gEngine.Con_Printf(S_ERROR "Missing some fields for light entity\n");
		return;
	}

	if (le->type == LightTypeEnvironment) {
		if (g_map_entities.single_environment_index == NoEnvironmentLights) {
			g_map_entities.single_environment_index = index;
		} else {
			g_map_entities.single_environment_index = MoreThanOneEnvironmentLight;
		}
	}

	fillLightFromProps(le, props, have_fields, false, g_map_entities.entity_count);

	le->entity_index = g_map_entities.entity_count;
	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = props->classname,
		.index = g_map_entities.num_lights,
	};
	g_map_entities.num_lights++;
}

static void addTargetEntity( const entity_props_t *props ) {
	xvk_mapent_target_t *target = g_map_entities.targets + g_map_entities.num_targets;

	gEngine.Con_Reportf("Adding target entity %s at (%f, %f, %f)\n",
		props->targetname, props->origin[0], props->origin[1], props->origin[2]);

	if (g_map_entities.num_targets == MAX_MAPENT_TARGETS) {
		gEngine.Con_Printf(S_ERROR "Too many map target entities\n");
		return;
	}

	Q_strcpy(target->targetname, props->targetname);
	VectorCopy(props->origin, target->origin);

	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = Xvk_Target,
		.index = g_map_entities.num_targets,
	};

	++g_map_entities.num_targets;
}

static void readWorldspawn( const entity_props_t *props ) {
	Q_strcpy(g_map_entities.wadlist, props->wad);
	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = Worldspawn,
		.index = -1,
	};
}

static void readFuncWall( const entity_props_t *const props, uint32_t have_fields, int props_count ) {
	gEngine.Con_Reportf("func_wall entity=%d model=\"%s\", props_count=%d\n", g_map_entities.entity_count, (have_fields & Field_model) ? props->model : "N/A", props_count);

	if (g_map_entities.func_walls_count >= MAX_FUNC_WALL_ENTITIES) {
		gEngine.Con_Printf(S_ERROR "Too many func_wall entities, max supported = %d\n", MAX_FUNC_WALL_ENTITIES);
		return;
	}

	xvk_mapent_func_wall_t *const e = g_map_entities.func_walls + g_map_entities.func_walls_count;

	*e = (xvk_mapent_func_wall_t){0};

	/* NOTE: not used
	e->rendercolor.r = 255;
	e->rendercolor.g = 255;
	e->rendercolor.b = 255;

	Q_strcpy(e->model, props->model);

	if (have_fields & Field_renderamt)
		e->renderamt = props->renderamt;

	if (have_fields & Field_renderfx)
		e->renderfx = props->renderfx;

	if (have_fields & Field_rendermode)
		e->rendermode = props->rendermode;

	if (have_fields & Field_rendercolor) {
		e->rendercolor.r = props->rendercolor[0];
		e->rendercolor.g = props->rendercolor[1];
		e->rendercolor.b = props->rendercolor[2];
	}
	*/

	e->entity_index = g_map_entities.entity_count;
	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = FuncWall,
		.index = g_map_entities.func_walls_count,
	};
	++g_map_entities.func_walls_count;
}

static void addPatchSurface( const entity_props_t *props, uint32_t have_fields ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );
	const int num_surfaces = map->numsurfaces;
	const qboolean should_remove = (have_fields == Field__xvk_surface_id) || (have_fields & Field__xvk_texture && props->_xvk_texture[0] == '\0');

	for (int i = 0; i < props->_xvk_surface_id.num; ++i) {
		const int index = props->_xvk_surface_id.values[i];
		xvk_patch_surface_t *psurf = NULL;
		if (index < 0 || index >= num_surfaces) {
			gEngine.Con_Printf(S_ERROR "Incorrect patch for surface_index %d where numsurfaces=%d\n", index, num_surfaces);
			continue;
		}

		if (!g_patch.surfaces) {
			g_patch.surfaces = Mem_Malloc(vk_core.pool, num_surfaces * sizeof(xvk_patch_surface_t));
			g_patch.surfaces_count = num_surfaces;
			for (int i = 0; i < num_surfaces; ++i) {
				g_patch.surfaces[i].flags = Patch_Surface_NoPatch;
				g_patch.surfaces[i].tex_id = -1;
				g_patch.surfaces[i].tex = NULL;
			}
		}

		psurf = g_patch.surfaces + index;

		if (should_remove) {
			gEngine.Con_Reportf("Patch: surface %d removed\n", index);
			psurf->flags = Patch_Surface_Delete;
			continue;
		}

		if (have_fields & Field__xvk_texture) {
			const int tex_id = XVK_FindTextureNamedLike( props->_xvk_texture );
			gEngine.Con_Reportf("Patch for surface %d with texture \"%s\" -> %d\n", index, props->_xvk_texture, tex_id);
			psurf->tex_id = tex_id;

			// Find texture_t for this index
			for (int i = 0; i < map->numtextures; ++i) {
				const texture_t* const tex = map->textures[i];
				if (tex->gl_texturenum == tex_id) {
					psurf->tex = tex;
					psurf->tex_id = -1;
					break;
				}
			}

			psurf->flags |= Patch_Surface_Texture;
		}

		if (have_fields & Field__light) {
			VectorScale(props->_light, 0.1f, psurf->emissive);
			psurf->flags |= Patch_Surface_Emissive;
			gEngine.Con_Reportf("Patch for surface %d: assign emissive %f %f %f\n", index,
				psurf->emissive[0],
				psurf->emissive[1],
				psurf->emissive[2]
			);
		}

		if (have_fields & (Field__xvk_svec | Field__xvk_tvec)) {
			Vector4Copy(props->_xvk_svec, psurf->s_vec);
			Vector4Copy(props->_xvk_tvec, psurf->t_vec);
			psurf->flags |= Patch_Surface_STvecs;
			gEngine.Con_Reportf("Patch for surface %d: assign stvec\n", index);
		}

		if (have_fields & Field__xvk_tex_scale) {
			Vector2Copy(props->_xvk_tex_scale, psurf->tex_scale);
			psurf->flags |= Patch_Surface_TexScale;
			gEngine.Con_Reportf("Patch for surface %d: assign tex scale %f %f\n",
				index, psurf->tex_scale[0], psurf->tex_scale[1]
			);
		}

		if (have_fields & Field__xvk_tex_offset) {
			Vector2Copy(props->_xvk_tex_offset, psurf->tex_offset);
			psurf->flags |= Patch_Surface_TexOffset;
			gEngine.Con_Reportf("Patch for surface %d: assign tex offset %f %f\n",
				index, psurf->tex_offset[0], psurf->tex_offset[1]
			);
		}
	}
}

static void patchLightEntity( const entity_props_t *props, int ent_id, uint32_t have_fields, int index ) {
	ASSERT(index >= 0);
	ASSERT(index < g_map_entities.num_lights);

	vk_light_entity_t *const light = g_map_entities.lights + index;

	if (have_fields == Field__xvk_ent_id) {
		gEngine.Con_Reportf("Deleting light entity (%d of %d) with index=%d\n", index, g_map_entities.num_lights, ent_id);

		// Mark it as deleted
		light->entity_index = -1;
		return;
	}

	fillLightFromProps(light, props, have_fields, true, ent_id);
}

static void patchFuncWallEntity( const entity_props_t *props, uint32_t have_fields, int index ) {
	ASSERT(index >= 0);
	ASSERT(index < g_map_entities.func_walls_count);
	xvk_mapent_func_wall_t *const fw = g_map_entities.func_walls + index;

	if (have_fields & Field_origin)
		VectorCopy(props->origin, fw->origin);

	gEngine.Con_Reportf("Patching ent=%d func_wall=%d %f %f %f\n", fw->entity_index, index, fw->origin[0], fw->origin[1], fw->origin[2]);
}

static void patchEntity( const entity_props_t *props, uint32_t have_fields ) {
	ASSERT(have_fields & Field__xvk_ent_id);

	for (int i = 0; i < props->_xvk_ent_id.num; ++i) {
		const int ei = props->_xvk_ent_id.values[i];
		if (ei < 0 || ei >= g_map_entities.entity_count) {
			gEngine.Con_Printf(S_ERROR "_xvk_ent_id value %d is out of bounds, max=%d\n", ei, g_map_entities.entity_count);
			continue;
		}

		const xvk_mapent_ref_t *const ref = g_map_entities.refs + ei;
		switch (ref->class) {
			case Light:
			case LightSpot:
			case LightEnvironment:
				patchLightEntity(props, ei, have_fields, ref->index);
				break;
			case FuncWall:
				patchFuncWallEntity(props, have_fields, ref->index);
				break;
			default:
				gEngine.Con_Printf(S_WARN "vk_mapents: trying to patch unsupported entity %d class %d\n", ei, ref->class);
		}
	}

}

static void parseEntities( char *string, qboolean is_patch ) {
	unsigned have_fields = 0;
	int props_count = 0;
	entity_props_t values;
	char *pos = string;
	//gEngine.Con_Reportf("ENTITIES: %s\n", pos);
	for (;;) {
		char key[1024];
		char value[1024];

		pos = COM_ParseFile(pos, key, sizeof(key));
		ASSERT(Q_strlen(key) < sizeof(key));
		if (!pos)
			break;

		if (key[0] == '{') {
			have_fields = None;
			values = (entity_props_t){0};
			props_count = 0;
			g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
				.class = Unknown,
				.index = -1,
			};
			continue;
		} else if (key[0] == '}') {
			const int target_fields = Field_targetname | Field_origin;
			if ((have_fields & target_fields) == target_fields)
				addTargetEntity( &values );
			switch (values.classname) {
				case Light:
				case LightSpot:
				case LightEnvironment:
					addLightEntity( &values, have_fields );
					break;

				case Worldspawn:
					readWorldspawn( &values );
					break;

				case FuncWall:
					readFuncWall( &values, have_fields, props_count );
					break;

				case Unknown:
					if (is_patch) {
						if (have_fields & Field__xvk_surface_id) {
							addPatchSurface( &values, have_fields );
						} else if (have_fields & Field__xvk_ent_id) {
							patchEntity( &values, have_fields );
						}
					}
					break;
				case Ignored:
				case Xvk_Target:
					// Skip
					break;
			}

			g_map_entities.entity_count++;
			if (g_map_entities.entity_count == MAX_MAP_ENTITIES) {
				gEngine.Con_Printf(S_ERROR "vk_mapents: too many entities, skipping the rest");\
				break;
			}
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		ASSERT(Q_strlen(value) < sizeof(value));
		if (!pos)
			break;

#define READ_FIELD(num, type, name, kind) \
		if (Q_strcmp(key, #name) == 0) { \
			const unsigned bit = parseEntProp##kind(value, &values.name, Field_##name); \
			if (bit == 0) { \
				gEngine.Con_Printf( S_ERROR "Error parsing entity property " #name ", invalid value: %s\n", value); \
			} else have_fields |= bit; \
		} else
		ENT_PROP_LIST(READ_FIELD)
		{
			//gEngine.Con_Reportf("Unknown field %s with value %s\n", key, value);
		}
		++props_count;
#undef CHECK_FIELD
	}
}

const xvk_mapent_target_t *findTargetByName(const char *name) {
	for (int i = 0; i < g_map_entities.num_targets; ++i) {
		const xvk_mapent_target_t *target = g_map_entities.targets + i;
		if (Q_strcmp(name, target->targetname) == 0)
			return target;
	}

	return NULL;
}

static void orientSpotlights( void ) {
	// Patch spotlight directions based on target entities
	for (int i = 0; i < g_map_entities.num_lights; ++i) {
		vk_light_entity_t *const light = g_map_entities.lights + i;
		const xvk_mapent_target_t *target;

		if (light->type != LightSpot && light->type != LightTypeEnvironment)
			continue;

		if (light->target_entity[0] == '\0')
			continue;

		target = findTargetByName(light->target_entity);
		if (!target) {
			gEngine.Con_Printf(S_ERROR "Couldn't find target entity '%s' for spot light %d\n", light->target_entity, i);
			continue;
		}

		VectorSubtract(target->origin, light->origin, light->dir);
		VectorNormalize(light->dir);

		gEngine.Con_Reportf("Light %d patched direction towards '%s': %f %f %f\n", i, target->targetname,
			light->dir[0], light->dir[1], light->dir[2]);
	}
}

static void parsePatches( const model_t *const map ) {
	char filename[256];
	byte *data;

	if (g_patch.surfaces) {
		Mem_Free(g_patch.surfaces);
		g_patch.surfaces = NULL;
		g_patch.surfaces_count = 0;
	}

	Q_snprintf(filename, sizeof(filename), "luchiki/%s.patch", map->name);
	gEngine.Con_Reportf("Loading patches from file \"%s\"\n", filename);
	data = gEngine.fsapi->LoadFile( filename, 0, false );
	if (!data) {
		gEngine.Con_Reportf("No patch file \"%s\"\n", filename);
		return;
	}

	parseEntities( (char*)data, true );
	Mem_Free(data);
}

void XVK_ParseMapEntities( void ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );

	ASSERT(map);

	g_map_entities.num_targets = 0;
	g_map_entities.num_lights = 0;
	g_map_entities.single_environment_index = NoEnvironmentLights;
	g_map_entities.entity_count = 0;
	g_map_entities.func_walls_count = 0;

	parseEntities( map->entities, false );
	orientSpotlights();
}

void XVK_ParseMapPatches( void ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );

	parsePatches( map );

	// Perform light deletion and compaction
	{
		int w = 0;
		for (int r = 0; r < g_map_entities.num_lights; ++r) {
			// Deleted
			if (g_map_entities.lights[r].entity_index < 0) {
				continue;
			}

			if (r != w)
				memcpy(g_map_entities.lights + w, g_map_entities.lights + r, sizeof(vk_light_entity_t));
			++w;
		}

		g_map_entities.num_lights = w;
	}

	orientSpotlights();
}

const xvk_patch_surface_t* R_VkPatchGetSurface( int surface_index ) {
	if (!g_patch.surfaces_count)
		return NULL;

	ASSERT(g_patch.surfaces);
	ASSERT(surface_index >= 0);
	ASSERT(surface_index < g_patch.surfaces_count);

	return g_patch.surfaces + surface_index;
}
