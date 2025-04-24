#include "camera.h"
#include "vk_math.h"
#include "vk_common.h"
#include "r_textures.h"
#include "vk_brush.h"
#include "vk_light.h"

#include "pm_defs.h"
#include "pmtrace.h"

static const char *renderModeName( int rendermode ) {
	switch (rendermode) {
		case kRenderNormal: return "kRenderNormal";
		case kRenderTransColor: return "kRenderTransColor";
		case kRenderTransTexture: return "kRenderTransTexture";
		case kRenderGlow: return "kRenderGlow";
		case kRenderTransAlpha: return "kRenderTransAlpha";
		case kRenderTransAdd: return "kRenderTransAdd";
		default: return "UNKNOWN";
	}
}

void XVK_CameraDebugPrintCenterEntity( void ) {
	vec3_t vec_end;
	pmtrace_t trace;
	const msurface_t *surf;
	char buf[1024], *p = buf, *const end = buf + sizeof(buf);
	const physent_t *physent = NULL;
	const cl_entity_t *ent = NULL;

	VectorMA(g_camera.vieworg, 1e6, g_camera.vforward, vec_end);

	trace = gEngine.CL_TraceLine( g_camera.vieworg, vec_end, PM_NORMAL );
	surf = gEngine.EV_TraceSurface( Q_max(trace.ent, 0), g_camera.vieworg, vec_end );

	if (trace.ent > 0) {
		physent = gEngine.EV_GetPhysent( trace.ent );
	}

	ent = globals.entities + ((physent && physent->info > 0) ? physent->info : 0);

	p += Q_snprintf(p, end - p,
		"^\n"
		"cam.origin: %.03f %.03f %.03f"
		" hit: %.03f %.03f %.03f"
		// TODO cam dir
		"\n",
		g_camera.vieworg[0], g_camera.vieworg[1], g_camera.vieworg[2],
		trace.endpos[0], trace.endpos[1], trace.endpos[2]
	);

	p += Q_snprintf(p, end - p,
		"entity (dynamic index: %d, info: %d), name: %s\n",
		ent ? ent->index : -1,
		(physent && physent->info > 0) ? physent->info : -1,
		(ent && ent->model) ? ent->model->name : "N/A");

	if (ent) {
		p += Q_snprintf(p, end - p,
			"ent type: %d, rendermode: %d(%s)\n",
			ent->curstate.entityType,
			ent->curstate.rendermode,
			renderModeName(ent->curstate.rendermode));
	}

	if (surf && ent && ent->model && ent->model->surfaces) {
		const int surface_index = surf - ent->model->surfaces;
		const texture_t *current_tex = R_TextureAnimation(ent, surf);
		const int tex_id = current_tex->gl_texturenum;
		const char *const tex_name = R_TextureGetNameByIndex( tex_id );
		const texture_t *tex = surf->texinfo->texture;

		const texture_t* const alt = tex->alternate_anims;

		p += Q_snprintf(p, end - p,
			"surface index: [[ %d ]];\ntexture: %s(%d)\n"
			"alternate_texture: %s(%d)\n",
			surface_index, tex_name ? tex_name : "NONE", tex_id,
			alt ? alt->name : "N/A", alt ? alt->gl_texturenum : -1
		);

		if (tex->anim_total > 0 && tex->anim_next) {
			tex = tex->anim_next;
			p += Q_snprintf(p, end - p,
				"anim textures chain (%d):\n", tex->anim_total);
			for (int i = 0; i < tex->anim_total && tex; ++i) {
				const char* const texname = R_TextureGetNameByIndex(tex->gl_texturenum);
				p += Q_snprintf(p, end - p,
					"%d: %s(%d)%s\n", i, texname ? texname : "NONE", tex->gl_texturenum, tex == current_tex ? " <-" : "   ");
				tex = tex->anim_next;
			}
		}
	}

	p = RT_LightPrintCellInfo(p, end, trace.endpos);

	gEngine.CL_CenterPrint(buf, 0.5f);
}
