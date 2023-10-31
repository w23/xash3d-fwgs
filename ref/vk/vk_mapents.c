#include "vk_common.h"
#include "vk_mapents.h"
#include "vk_core.h" // TODO we need only pool from there, not the entire vulkan garbage
#include "r_textures.h"
#include "vk_logs.h"

#include "eiface.h" // ARRAYSIZE
#include "xash3d_mathlib.h"
#include <string.h>
#include <ctype.h>

#define LOG_MODULE LogModule_Patch

xvk_map_entities_t g_map_entities;

static struct {
	xvk_patch_surface_t *surfaces;
	int surfaces_count;
} g_patch;

static unsigned parseEntPropWadList(const char* value, string *out, unsigned bit) {
	int dst_left = sizeof(string) - 2; // ; \0
	char *dst = *out;
	*dst = '\0';
	DEBUG("WADS: %s", value);

	for (; *value;) {
		const char *file_begin = value;

		for (; *value && *value != ';'; ++value) {
			if (*value == '\\' || *value == '/')
				file_begin = value + 1;
		}

		{
			const int len = value - file_begin;
			DEBUG("WAD: %.*s", len, file_begin);

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

	DEBUG("wad list: %s", *out);
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
		ERR("Error parsing mapents patch IntArray (wrong format? too many entries (max=%d)), portion not parsed: %s", MAX_INT_ARRAY_SIZE, value);
	}
	return retval;
}

static unsigned parseEntPropString(const char* value, string *out, unsigned bit) {
	const int len = Q_strlen(value);
	if (len >= sizeof(string))
		ERR("Map entity value '%s' is too long, max length is %d",
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
	} else if (Q_strncmp(value, "func_", 5) == 0) {
		*out = FuncAny;
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
		Q_strncpy( le->target_entity, props->target, sizeof( le->target_entity ));

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

	DEBUG("%s light %d (ent=%d): %s targetname=%s color=(%f %f %f) origin=(%f %f %f) style=%d R=%f dir=(%f %f %f) stopdot=(%f %f)",
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
		ERR("Too many lights entities in map");
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
		ERR("Missing some fields for light entity");
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

	DEBUG("Adding target entity %s at (%f, %f, %f)",
		props->targetname, props->origin[0], props->origin[1], props->origin[2]);

	if (g_map_entities.num_targets == MAX_MAPENT_TARGETS) {
		ERR("Too many map target entities");
		return;
	}

	Q_strncpy( target->targetname, props->targetname, sizeof( target->targetname ));
	VectorCopy(props->origin, target->origin);

	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = Xvk_Target,
		.index = g_map_entities.num_targets,
	};

	++g_map_entities.num_targets;
}

static void readWorldspawn( const entity_props_t *props ) {
	Q_strncpy( g_map_entities.wadlist, props->wad, sizeof( g_map_entities.wadlist ));
	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = Worldspawn,
		.index = -1,
	};
}

int R_VkRenderModeFromString( const char *s ) {
#define CHECK_IF_MODE(mode) if (Q_strcmp(s, #mode) == 0) { return mode; }
		CHECK_IF_MODE(kRenderNormal)
		else CHECK_IF_MODE(kRenderTransColor)
		else CHECK_IF_MODE(kRenderTransTexture)
		else CHECK_IF_MODE(kRenderGlow)
		else CHECK_IF_MODE(kRenderTransAlpha)
		else CHECK_IF_MODE(kRenderTransAdd)
		return -1;
}

static void readFuncAny( const entity_props_t *const props, uint32_t have_fields, int props_count ) {
	DEBUG("func_any entity=%d model=\"%s\", props_count=%d", g_map_entities.entity_count, (have_fields & Field_model) ? props->model : "N/A", props_count);

	if (g_map_entities.func_any_count >= MAX_FUNC_ANY_ENTITIES) {
		ERR("Too many func_any entities, max supported = %d", MAX_FUNC_ANY_ENTITIES);
		return;
	}

	xvk_mapent_func_any_t *const e = g_map_entities.func_any + g_map_entities.func_any_count;

	*e = (xvk_mapent_func_any_t){0};
	e->rendermode = -1;

	Q_strncpy( e->model, props->model, sizeof( e->model ));

	if (have_fields & Field_rendermode)
		e->rendermode = props->rendermode;

	/* NOTE: not used
	e->rendercolor.r = 255;
	e->rendercolor.g = 255;
	e->rendercolor.b = 255;

	if (have_fields & Field_renderamt)
		e->renderamt = props->renderamt;

	if (have_fields & Field_renderfx)
		e->renderfx = props->renderfx;

	if (have_fields & Field_rendercolor) {
		e->rendercolor.r = props->rendercolor[0];
		e->rendercolor.g = props->rendercolor[1];
		e->rendercolor.b = props->rendercolor[2];
	}
	*/

	e->entity_index = g_map_entities.entity_count;
	g_map_entities.refs[g_map_entities.entity_count] = (xvk_mapent_ref_t){
		.class = FuncAny,
		.index = g_map_entities.func_any_count,
	};
	++g_map_entities.func_any_count;
}

static void addPatchSurface( const entity_props_t *props, uint32_t have_fields ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );
	const int num_surfaces = map->numsurfaces;
	const qboolean should_remove = (have_fields == Field__xvk_surface_id) || (have_fields & Field__xvk_material && props->_xvk_material[0] == '\0');

	for (int i = 0; i < props->_xvk_surface_id.num; ++i) {
		const int index = props->_xvk_surface_id.values[i];
		xvk_patch_surface_t *psurf = NULL;
		if (index < 0 || index >= num_surfaces) {
			ERR("Incorrect patch for surface_index %d where numsurfaces=%d", index, num_surfaces);
			continue;
		}

		if (!g_patch.surfaces) {
			g_patch.surfaces = Mem_Malloc(vk_core.pool, num_surfaces * sizeof(xvk_patch_surface_t));
			g_patch.surfaces_count = num_surfaces;
			for (int i = 0; i < num_surfaces; ++i) {
				g_patch.surfaces[i].flags = Patch_Surface_NoPatch;
				g_patch.surfaces[i].material_ref.index = -1;
			}
		}

		psurf = g_patch.surfaces + index;

		if (should_remove) {
			DEBUG("Patch: surface %d removed", index);
			psurf->flags = Patch_Surface_Delete;
			continue;
		}

		if (have_fields & Field__xvk_material) {
			const r_vk_material_ref_t mat = R_VkMaterialGetForName( props->_xvk_material );
			if (mat.index >= 0) {
				DEBUG("Patch for surface %d with material \"%s\" -> %d", index, props->_xvk_material, mat.index);
				psurf->material_ref = mat;
				psurf->flags |= Patch_Surface_Material;
			} else {
				ERR("Cannot patch surface %d with material \"%s\": material not found", index, props->_xvk_material);
			}
		}

		if (have_fields & Field__light) {
			VectorScale(props->_light, 0.1f, psurf->emissive);
			psurf->flags |= Patch_Surface_Emissive;
			DEBUG("Patch for surface %d: assign emissive %f %f %f", index,
				psurf->emissive[0],
				psurf->emissive[1],
				psurf->emissive[2]
			);
		}

		if (have_fields & (Field__xvk_svec | Field__xvk_tvec)) {
			Vector4Copy(props->_xvk_svec, psurf->s_vec);
			Vector4Copy(props->_xvk_tvec, psurf->t_vec);
			psurf->flags |= Patch_Surface_STvecs;
			DEBUG("Patch for surface %d: assign st_vec", index);
		}

		if (have_fields & Field__xvk_tex_scale) {
			Vector2Copy(props->_xvk_tex_scale, psurf->tex_scale);
			psurf->flags |= Patch_Surface_TexScale;
			DEBUG("Patch for surface %d: assign tex_scale %f %f",
				index, psurf->tex_scale[0], psurf->tex_scale[1]
			);
		}

		if (have_fields & Field__xvk_tex_offset) {
			Vector2Copy(props->_xvk_tex_offset, psurf->tex_offset);
			psurf->flags |= Patch_Surface_TexOffset;
			DEBUG("Patch for surface %d: assign tex_offset %f %f",
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
		DEBUG("Deleting light entity (%d of %d) with index=%d", index, g_map_entities.num_lights, ent_id);

		// Mark it as deleted
		light->entity_index = -1;
		return;
	}

	fillLightFromProps(light, props, have_fields, true, ent_id);
}

static void patchFuncAnyEntity( const entity_props_t *props, uint32_t have_fields, int index ) {
	ASSERT(index >= 0);
	ASSERT(index < g_map_entities.func_any_count);
	xvk_mapent_func_any_t *const e = g_map_entities.func_any + index;

	if (have_fields & Field_origin) {
		VectorCopy(props->origin, e->origin);
		e->origin_patched = true;

		DEBUG("Patching ent=%d func_any=%d %f %f %f", e->entity_index, index, e->origin[0], e->origin[1], e->origin[2]);
	}

	if (have_fields & Field__xvk_smooth_entire_model) {
		DEBUG("Patching ent=%d func_any=%d smooth_entire_model =%d", e->entity_index, index, props->_xvk_smooth_entire_model);
		e->smooth_entire_model = props->_xvk_smooth_entire_model;
	}

	if (have_fields & Field__xvk_map_material) {
		const char *s = props->_xvk_map_material;
		while (*s) {
			while (*s && isspace(*s)) ++s; // skip space
			const char *from_begin = s;
			while (*s && !isspace(*s)) ++s; // find first space or end
			const int from_len = s - from_begin;
			if (!from_len)
				break;

			while (*s && isspace(*s)) ++s; // skip space
			const char *to_begin = s;
			while (*s && !isspace(*s)) ++s; // find first space or end
			const int to_len = s - to_begin;
			if (!to_len)
				break;

			string from_tex, to_mat;
			Q_strncpy(from_tex, from_begin, Q_min(sizeof from_tex, from_len + 1));
			Q_strncpy(to_mat, to_begin, Q_min(sizeof to_mat, to_len + 1));

			const int from_tex_index = R_TextureFindByNameLike(from_tex);
			const r_vk_material_ref_t to_mat_ref = R_VkMaterialGetForName(to_mat);

			DEBUG("Adding mapping from tex \"%s\"(%d) to mat \"%s\"(%d) for entity=%d",
				from_tex, from_tex_index, to_mat, to_mat_ref.index, e->entity_index);

			if (from_tex_index <= 0) {
				ERR("When patching entity=%d couldn't find map-from texture \"%s\"", e->entity_index, from_tex);
				continue;
			}

			if (to_mat_ref.index <= 0) {
				ERR("When patching entity=%d couldn't find map-to material \"%s\"", e->entity_index, to_mat);
				continue;
			}

			if (e->matmap_count == MAX_MATERIAL_MAPPINGS) {
				ERR("Cannot map tex \"%s\"(%d) to mat \"%s\"(%d) for entity=%d: too many mappings, "
						"consider increasing MAX_MATERIAL_MAPPINGS",
					from_tex, from_tex_index, to_mat, to_mat_ref.index, e->entity_index);
				continue;
			}

			e->matmap[e->matmap_count].from_tex = from_tex_index;
			e->matmap[e->matmap_count].to_mat = to_mat_ref;
			++e->matmap_count;
		}
	}
}

static void patchEntity( const entity_props_t *props, uint32_t have_fields ) {
	ASSERT(have_fields & Field__xvk_ent_id);

	for (int i = 0; i < props->_xvk_ent_id.num; ++i) {
		const int ei = props->_xvk_ent_id.values[i];
		if (ei < 0 || ei >= g_map_entities.entity_count) {
			ERR("_xvk_ent_id value %d is out of bounds, max=%d", ei, g_map_entities.entity_count);
			continue;
		}

		const xvk_mapent_ref_t *const ref = g_map_entities.refs + ei;
		switch (ref->class) {
			case Light:
			case LightSpot:
			case LightEnvironment:
				patchLightEntity(props, ei, have_fields, ref->index);
				break;
			case FuncAny:
				patchFuncAnyEntity(props, have_fields, ref->index);
				break;
			default:
				WARN("vk_mapents: trying to patch unsupported entity %d class %d", ei, ref->class);
		}
	}
}

static void appendExcludedPairs(const entity_props_t *props) {
	if (props->_xvk_smoothing_excluded_pairs.num % 2 != 0) {
		ERR("vk_mapents: smoothing group exclusion pairs list should be list of pairs -- divisible by 2; cutting the tail");
	}

	int count = props->_xvk_smoothing_excluded_pairs.num & ~1;
	if (g_map_entities.smoothing.excluded_pairs_count + count > COUNTOF(g_map_entities.smoothing.excluded_pairs)) {
		ERR("vk_mapents: smoothing exclusion pairs capacity exceeded, go complain in github issues");
		count = COUNTOF(g_map_entities.smoothing.excluded_pairs) - g_map_entities.smoothing.excluded_pairs_count;
	}

	memcpy(g_map_entities.smoothing.excluded_pairs + g_map_entities.smoothing.excluded_pairs_count, props->_xvk_smoothing_excluded_pairs.values, count * sizeof(int));

	g_map_entities.smoothing.excluded_pairs_count += count;
}

static void appendExcludedSingles(const entity_props_t *props) {
	int count = props->_xvk_smoothing_excluded.num;

	if (g_map_entities.smoothing.excluded_count + count > COUNTOF(g_map_entities.smoothing.excluded)) {
		ERR("vk_mapents: smoothing exclusion group capacity exceeded, go complain in github issues");
		count = COUNTOF(g_map_entities.smoothing.excluded) - g_map_entities.smoothing.excluded_count;
	}

	memcpy(g_map_entities.smoothing.excluded + g_map_entities.smoothing.excluded_count, props->_xvk_smoothing_excluded.values, count * sizeof(int));

	if (g_log_debug_bits & LOG_MODULE) {
		DEBUG("Adding %d smoothing-excluded surfaces", props->_xvk_smoothing_excluded.num);
		for (int i = 0; i < props->_xvk_smoothing_excluded.num; ++i) {
			DEBUG("%d", props->_xvk_smoothing_excluded.values[i]);
		}
	}

	g_map_entities.smoothing.excluded_count += count;
}

static void addSmoothingGroup(const entity_props_t *props) {
	if (g_map_entities.smoothing.groups_count == MAX_INCLUDED_SMOOTHING_GROUPS) {
		ERR("vk_mapents: limit of %d smoothing groups reached", MAX_INCLUDED_SMOOTHING_GROUPS);
		return;
	}

	xvk_smoothing_group_t *g = g_map_entities.smoothing.groups + (g_map_entities.smoothing.groups_count++);

	int count = props->_xvk_smoothing_group.num;
	if (count > MAX_INCLUDED_SMOOTHING_SURFACES_IN_A_GROUP) {
		ERR("vk_mapents: too many surfaces in a smoothing group. Max %d, got %d. Culling", MAX_INCLUDED_SMOOTHING_SURFACES_IN_A_GROUP, props->_xvk_smoothing_group.num);
		count = MAX_INCLUDED_SMOOTHING_SURFACES_IN_A_GROUP;
	}

	memcpy(g->surfaces, props->_xvk_smoothing_group.values, sizeof(int) * count);
	g->count = count;
}

static void parseEntities( char *string, qboolean is_patch ) {
	unsigned have_fields = 0;
	int props_count = 0;
	entity_props_t values;
	char *pos = string;
	//DEBUG("ENTITIES: %s", pos);
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

				case FuncAny:
					readFuncAny( &values, have_fields, props_count );
					break;

				case Unknown:
					if (is_patch) {
						if (have_fields & Field__xvk_surface_id) {
							addPatchSurface( &values, have_fields );
						} else if (have_fields & Field__xvk_ent_id) {
							patchEntity( &values, have_fields );
						} else {
							if (have_fields & Field__xvk_smoothing_threshold) {
								g_map_entities.smoothing.threshold = cosf(DEG2RAD(values._xvk_smoothing_threshold));
							}

							if (have_fields & Field__xvk_smoothing_excluded_pairs) {
								appendExcludedPairs(&values);
							}

							if (have_fields & Field__xvk_smoothing_excluded) {
								appendExcludedSingles(&values);
							}

							if (have_fields & Field__xvk_smoothing_group) {
								addSmoothingGroup(&values);
							}
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
				ERR("vk_mapents: too many entities, skipping the rest");\
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
				ERR("Error parsing entity property " #name ", invalid value: %s", value); \
			} else have_fields |= bit; \
		} else
		ENT_PROP_LIST(READ_FIELD)
		{
			//DEBUG("Unknown field %s with value %s", key, value);
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
			ERR("Couldn't find target entity '%s' for spot light %d", light->target_entity, i);
			continue;
		}

		VectorSubtract(target->origin, light->origin, light->dir);
		VectorNormalize(light->dir);

		DEBUG("Light %d patched direction towards '%s': %f %f %f", i, target->targetname,
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

	{
		const char *ext = NULL;

		// Find extension (if any)
		{
			const char *p = map->name;
			for(; *p; ++p)
				if (*p == '.')
					ext = p;
			if (!ext)
				ext = p;
		}

		Q_snprintf(filename, sizeof(filename), "luchiki/%.*s.patch", (int)(ext - map->name), map->name);
	}

	DEBUG("Loading patches from file \"%s\"", filename);
	data = gEngine.fsapi->LoadFile( filename, 0, false );
	if (!data) {
		DEBUG("No patch file \"%s\"", filename);
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
	g_map_entities.func_any_count = 0;
	g_map_entities.smoothing.threshold = cosf(DEG2RAD(45.f));
	g_map_entities.smoothing.excluded_pairs_count = 0;
	g_map_entities.smoothing.excluded_count = 0;
	for (int i = 0; i < g_map_entities.smoothing.groups_count; ++i)
		g_map_entities.smoothing.groups[i].count = 0;
	g_map_entities.smoothing.groups_count = 0;

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
