#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_common.h"
#include "r_textures.h"
#include "vk_renderstate.h"
#include "vk_overlay.h"
#include "vk_scene.h"
#include "vk_framectl.h"
#include "vk_lightmap.h"
#include "vk_sprite.h"
#include "vk_studio.h"
#include "vk_beams.h"
#include "vk_brush.h"
#include "r_decals.h"
#include "vk_rpart.h"
#include "vk_triapi.h"
#include "r_speeds.h"
#include "vk_logs.h"

#include "xash3d_types.h"
#include "com_strings.h"

#include <memory.h>

#define LOG_MODULE rmain

r_globals_t globals = {0};
ref_api_t gEngine = {0};
ref_globals_t *gpGlobals = NULL;
ref_client_t  *gp_cl = NULL;
ref_host_t    *gp_host = NULL;

static const char *R_GetConfigName( void )
{
	return "vk";
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float scale_x, float scale_y )
{
	PRINT_NOT_IMPLEMENTED_ARGS("(%d, %d, %d, %f, %f)", rotate, x, y, scale_x, scale_y);

	return true;
}

static void GL_SetupAttributes( int safegl )
{
	// Nothing to do for Vulkan
}
static void GL_ClearExtensions( void )
{
	// Nothing to do for Vulkan
}
static void GL_BackendStartFrame_UNUSED( void )
{
	/* Unused in Vulkan renderer. GL renderer only uses this to clear the r_speeds_msg string */
}
static void GL_BackendEndFrame_UNUSED( void )
{
	/* Unused in Vulkan renderer. GL renderer only uses this to populate r_speeds_msg string. In Vulkan this is done naturally in R_EndFrame */
}

// debug
static void R_ShowTextures_UNUSED( void )
{
	/* Unused in Vulkan renderer. No need to debug textures this way */
}

// texture management
static const byte *R_GetTextureOriginalBuffer_UNUSED( unsigned int idx )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

static void GL_ProcessTexture_UNUSED( int texnum, float gamma, int topColor, int bottomColor )
{
	PRINT_NOT_IMPLEMENTED();
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	PRINT_NOT_IMPLEMENTED();
	return false;
}

// light
static colorVec R_LightPoint( const float *p )
{
	PRINT_NOT_IMPLEMENTED();
	return (colorVec){0};
}


extern void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa );

static void Mod_UnloadTextures( model_t *mod )
{
	ASSERT( mod != NULL );

	switch( mod->type )
	{
	case mod_studio:
		Mod_StudioUnloadTextures( mod->cache.data );
		break;
	case mod_alias:
		// FIXME Mod_AliasUnloadTextures( mod->cache.data );
		break;
	case mod_brush:
		R_BrushUnloadTextures( mod );
		break;
	case mod_sprite:
		// Sprite textures are managed by the engine (Mod_SpriteUnloadTextures)
		break;
	default:
		ASSERT( 0 );
		break;
	}
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer, size_t buffersize )
{
	qboolean loaded = true;

	DEBUG("%s(%s, create=%d)", __FUNCTION__, mod->name, create);

	// TODO does this ever happen?
	if (!create && mod->type == mod_brush)
		gEngine.Con_Printf( S_WARN "VK FIXME Trying to unload brush model %s\n", mod->name);

	if( create )
	{
		switch( mod->type )
		{
			case mod_studio:
				// This call happens before we get R_NewMap, which frees all current buffers
				// So we can't really load anything here
				// TODO we might benefit a tiny bit (a few ms loading time) from reusing studio models from previous map
				break;
			case mod_sprite:
				// Sprite textures are loaded by the engine (Mod_SpriteLoadTextures)
				// Nothing to do here
				break;
			case mod_alias:
				// TODO what ARE mod_alias? We just don't know.
				loaded = false;
				break;
			case mod_brush:
				// This call happens before we get R_NewMap, which frees all current buffers
				// So we can't really load anything here
				break;
			default:
				gEngine.Host_Error( "Mod_LoadModel: unsupported type %d\n", mod->type );
				loaded = false;
		}
	}

	if( loaded && gEngine.drawFuncs->Mod_ProcessUserData )
		gEngine.drawFuncs->Mod_ProcessUserData( mod, create, buffer );

	if( !create ) {
		Mod_UnloadTextures( mod );
		switch( mod->type ) {
			case mod_brush:
				// Empirically, this function only attempts to destroy the worldmodel before loading the next map.
				// However, all brush models need to be destroyed. Use this as a signal to destroy them too.
				// Assert that this observation is correct.
				// ASSERT(mod == gEngine.pfnGetModelByIndex(1)); not correct when closing the game. At this point model count is zero.

				R_SceneMapDestroy();
				break;
			default:
				PRINT_NOT_IMPLEMENTED_ARGS("destroy (%p, %d, %s)", mod, mod->type, mod->name);
		}
	}

	return loaded;
}

// Xash3D Render Interface
// Get renderer info (doesn't changes engine state at all)

static const char *getParmName(int parm)
{
	switch(parm){
	case PARM_TEX_WIDTH: return "PARM_TEX_WIDTH";
	case PARM_TEX_HEIGHT: return "PARM_TEX_HEIGHT";
	case PARM_TEX_SRC_WIDTH: return "PARM_TEX_SRC_WIDTH";
	case PARM_TEX_SRC_HEIGHT: return "PARM_TEX_SRC_HEIGHT";
	case PARM_TEX_SKYBOX: return "PARM_TEX_SKYBOX";
	case PARM_TEX_SKYTEXNUM: return "PARM_TEX_SKYTEXNUM";
	case PARM_TEX_LIGHTMAP: return "PARM_TEX_LIGHTMAP";
	case PARM_TEX_TARGET: return "PARM_TEX_TARGET";
	case PARM_TEX_TEXNUM: return "PARM_TEX_TEXNUM";
	case PARM_TEX_FLAGS: return "PARM_TEX_FLAGS";
	case PARM_TEX_DEPTH: return "PARM_TEX_DEPTH";
	case PARM_TEX_GLFORMAT: return "PARM_TEX_GLFORMAT";
	case PARM_TEX_ENCODE: return "PARM_TEX_ENCODE";
	case PARM_TEX_MIPCOUNT: return "PARM_TEX_MIPCOUNT";
	case PARM_BSP2_SUPPORTED: return "PARM_BSP2_SUPPORTED";
	case PARM_SKY_SPHERE: return "PARM_SKY_SPHERE";
	case PARAM_GAMEPAUSED: return "PARAM_GAMEPAUSED";
	case PARM_MAP_HAS_DELUXE: return "PARM_MAP_HAS_DELUXE";
	case PARM_MAX_ENTITIES: return "PARM_MAX_ENTITIES";
	case PARM_WIDESCREEN: return "PARM_WIDESCREEN";
	case PARM_FULLSCREEN: return "PARM_FULLSCREEN";
	case PARM_SCREEN_WIDTH: return "PARM_SCREEN_WIDTH";
	case PARM_SCREEN_HEIGHT: return "PARM_SCREEN_HEIGHT";
	case PARM_CLIENT_INGAME: return "PARM_CLIENT_INGAME";
	case PARM_FEATURES: return "PARM_FEATURES";
	case PARM_ACTIVE_TMU: return "PARM_ACTIVE_TMU";
	case PARM_LIGHTSTYLEVALUE: return "PARM_LIGHTSTYLEVALUE";
	case PARM_MAX_IMAGE_UNITS: return "PARM_MAX_IMAGE_UNITS";
	case PARM_CLIENT_ACTIVE: return "PARM_CLIENT_ACTIVE";
	case PARM_REBUILD_GAMMA: return "PARM_REBUILD_GAMMA";
	case PARM_DEDICATED_SERVER: return "PARM_DEDICATED_SERVER";
	case PARM_SURF_SAMPLESIZE: return "PARM_SURF_SAMPLESIZE";
	case PARM_GL_CONTEXT_TYPE: return "PARM_GL_CONTEXT_TYPE";
	case PARM_GLES_WRAPPER: return "PARM_GLES_WRAPPER";
	case PARM_STENCIL_ACTIVE: return "PARM_STENCIL_ACTIVE";
	case PARM_WATER_ALPHA: return "PARM_WATER_ALPHA";
	case PARM_TEX_MEMORY: return "PARM_TEX_MEMORY";
	case PARM_DELUXEDATA: return "PARM_DELUXEDATA";
	case PARM_SHADOWDATA: return "PARM_SHADOWDATA";
	case PARM_MODERNFLASHLIGHT: return "PARM_MODERNFLASHLIGHT";
	case PARM_TEX_FILTERING: return "PARM_TEX_FILTERING";
	default: return "UNKNOWN";
	}
}

static intptr_t VK_RefGetParm( int parm, int arg )
{
	// TODO all PARM_TEX handle in r_texture internally
	switch(parm){
	case PARM_TEX_WIDTH:
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_WIDTH: // TODO why is this separate?
	case PARM_TEX_SRC_HEIGHT:
	case PARM_TEX_FLAGS:
	case PARM_TEX_FILTERING:
	/* TODO
	case PARM_TEX_SKYBOX:
	case PARM_TEX_SKYTEXNUM:
	case PARM_TEX_LIGHTMAP:
	case PARM_TEX_TARGET:
	case PARM_TEX_TEXNUM:
	case PARM_TEX_DEPTH:
	case PARM_TEX_GLFORMAT:
	case PARM_TEX_ENCODE:
	case PARM_TEX_MIPCOUNT:
	case PARM_TEX_MEMORY:
	*/
		return R_TexturesGetParm( parm, arg );
	case PARM_MODERNFLASHLIGHT:
		if (CVAR_TO_BOOL( rt_enable )) {
			return true;
		}
		return false;
	case PARM_WIDESCREEN:
		return gpGlobals->wideScreen;
	case PARM_FULLSCREEN:
		return gpGlobals->window_mode != WINDOW_MODE_WINDOWED;
	case PARM_SCREEN_WIDTH:
		return gpGlobals->width;
	case PARM_SCREEN_HEIGHT:
		return gpGlobals->height;
	}

	PRINT_NOT_IMPLEMENTED_ARGS("(%s(%d), %d)", getParmName(parm), parm, arg);

	return 0;
}
static void		GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{
	PRINT_NOT_IMPLEMENTED();
}
static float		GetFrameTime( void )
{
	/* TODO as in gl R_RenderScene()
	// frametime is valid only for normal pass
	if( RP_NORMALPASS( ))
		tr.frametime = gp_cl->time -   gp_cl->oldtime;
	else tr.frametime = 0.0;
	*/

	PRINT_NOT_IMPLEMENTED();
	return 1.f;
}

// Set renderer info (tell engine about changes)
static void		R_SetCurrentEntity( struct cl_entity_s *ent )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		R_SetCurrentModel( struct model_s *mod )
{
	PRINT_NOT_IMPLEMENTED();
}



// AVI
static void		AVI_UploadRawFrame( int texture, int cols, int rows, int width, int height, const byte *data )
{
	PRINT_NOT_IMPLEMENTED();
}

// glState related calls (must use this instead of normal gl-calls to prevent de-synchornize local states between engine and the client)
static void GL_Bind( int tmu, unsigned int texnum )
{
	if (tmu != 0) {
		PRINT_NOT_IMPLEMENTED_ARGS("non-zero tmu=%d", tmu);
	}

	TriSetTexture(texnum);
}
static void		GL_SelectTexture( int tmu )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_LoadTextureMatrix( const float *glmatrix )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TexMatrixIdentity( void )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_CleanUpTextureUnits( int last )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TexGen( unsigned int coord, unsigned int mode )
{
	PRINT_NOT_IMPLEMENTED();
}
static void		GL_TextureTarget( unsigned int target )
{
	PRINT_NOT_IMPLEMENTED();
}
static void GL_TexCoordArrayMode( unsigned int texmode )
{
	PRINT_NOT_IMPLEMENTED();
}
static void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	PRINT_NOT_IMPLEMENTED();
}

// Misc renderer functions
static void GL_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	PRINT_NOT_IMPLEMENTED();
}

colorVec R_LightVec( const float *start, const float *end, float *lightspot, float *lightvec );

static struct mstudiotex_s *R_StudioGetTexture( struct cl_entity_s *e )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

// setup map bounds for ortho-projection when we in dev_overview mode
static void		GL_OrthoBounds( const float *mins, const float *maxs )
{
	PRINT_NOT_IMPLEMENTED();
}

// get visdata for current frame from custom renderer
static byte* Mod_GetCurrentVis( void ) {
	// ref_soft just returns NULL here
	// Not sure if we need to copy what ref_gl does. What it does is:
	// - Setup camera and call R_MarkLeaves() in R_RenderScene()
	// - R_MarkLeaves() sets RI.visbytes
	//   will be eventually needed for culling in traditional renderer, see:
	//   - https://github.com/w23/xash3d-fwgs/pull/96
	//   - https://github.com/w23/xash3d-fwgs/issues/93
	// - Return RI.visbytes here (if not using custom rendering)
	return NULL;
}

// GL_GetProcAddress for client renderer
static void*		R_GetProcAddress( const char *name )
{
	PRINT_NOT_IMPLEMENTED();
	return NULL;
}

// TriAPI Interface
static void	TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	R_ScreenToWorld( const float *screen, float *world  )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriGetMatrix( const int pname, float *matrix )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriFogParams( float flDensity, int iFogSkybox )
{
	PRINT_NOT_IMPLEMENTED();
}
static void	TriCullFace( TRICULLSTYLE mode )
{
	PRINT_NOT_IMPLEMENTED();
}

static const byte* R_TextureData_UNUSED( unsigned int texnum )
{
	PRINT_NOT_IMPLEMENTED_ARGS("texnum=%d", texnum);
	// We don't store original texture data
	// TODO do we need to?
	return NULL;
}

static int R_CreateTexture_UNUSED( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	PRINT_NOT_IMPLEMENTED_ARGS("name=%s width=%d height=%d buffer=%p flags=%08x", name, width, height, buffer, flags);
	return 0;
}

static int R_LoadTextureArray_UNUSED( const char **names, int flags )
{
	PRINT_NOT_IMPLEMENTED();
	return 0;
}

static int R_CreateTextureArray_UNUSED( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	PRINT_NOT_IMPLEMENTED_ARGS("name=%s width=%d height=%d buffer=%p flags=%08x", name, width, height, buffer, flags);
	return 0;
}

static const ref_device_t *pfnGetRenderDevice( unsigned int idx )
{
	if( idx >= vk_core.num_devices )
		return NULL;

	return &vk_core.devices[idx];
}

static void R_GammaChanged( qboolean do_reset_gamma )
{
	PRINT_NOT_IMPLEMENTED_ARGS("do_reset_gamma=%d", do_reset_gamma);
}

static qboolean R_Init(void) {
	globals.world = (struct world_static_s *)ENGINE_GET_PARM( PARM_GET_WORLD_PTR );
	globals.movevars = (struct movevars_s *)ENGINE_GET_PARM( PARM_GET_MOVEVARS_PTR );
	globals.palette = (color24 *)ENGINE_GET_PARM( PARM_GET_PALETTE_PTR );
	globals.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );
	globals.texgammatable = (byte *)ENGINE_GET_PARM( PARM_GET_TEXGAMMATABLE_PTR );
	globals.lightgammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	globals.screengammatable = (uint *)ENGINE_GET_PARM( PARM_GET_SCREENGAMMATABLE_PTR );
	globals.lineargammatable = (uint *)ENGINE_GET_PARM( PARM_GET_LINEARGAMMATABLE_PTR );
	globals.dlights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_DLIGHTS_PTR );
	globals.elights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_ELIGHTS_PTR );

	return R_VkInit();
}

static void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture ) {
	PRINT_NOT_IMPLEMENTED_ARGS("solidskyTexture=%d alphaskyTexture=%d", solidskyTexture, alphaskyTexture);
}

static void R_OverrideTextureSourceSize( unsigned int texnum, unsigned int srcWidth, unsigned int srcHeight ) { // used to override decal size for texture replacement
	PRINT_NOT_IMPLEMENTED_ARGS("texnum=%u srcWidth=%u srcHeight=%u", texnum, srcWidth, srcHeight);
}

static void VGUI_SetupDrawing( qboolean rect ) {
	PRINT_NOT_IMPLEMENTED_ARGS("rect=%d", rect);
}

// Fill render_api_t with renderer-specific functions (called by engine)
static void R_FillRenderAPI( render_api_t *api )
{
	api->GetDetailScaleForTexture  = GetDetailScaleForTexture;
	api->GetExtraParmsForTexture   = GetExtraParmsForTexture;
	api->GetFrameTime              = GetFrameTime;
	api->R_SetCurrentEntity        = R_SetCurrentEntity;
	api->R_SetCurrentModel         = R_SetCurrentModel;

	// Texture tools
	api->GL_FindTexture            = R_TextureFindByName;
	api->GL_TextureName            = R_TextureGetNameByIndex;
	api->GL_TextureData            = R_TextureData_UNUSED;
	api->GL_LoadTexture            = R_TextureUploadFromFile;
	api->GL_CreateTexture          = R_CreateTexture_UNUSED;
	api->GL_LoadTextureArray       = R_LoadTextureArray_UNUSED;
	api->GL_CreateTextureArray     = R_CreateTextureArray_UNUSED;
	api->GL_FreeTexture            = R_TextureFree;

	// Decals manipulating (draw & remove)
	api->DrawSingleDecal           = R_DrawSingleDecal;
	api->R_DecalSetupVerts         = R_DecalSetupVerts;
	api->R_EntityRemoveDecals      = R_EntityRemoveDecals;

	// AVIkit support
	api->AVI_UploadRawFrame        = AVI_UploadRawFrame;

	// glState related calls
	api->GL_Bind                   = GL_Bind;
	api->GL_SelectTexture          = GL_SelectTexture;
	api->GL_LoadTextureMatrix      = GL_LoadTextureMatrix;
	api->GL_TexMatrixIdentity      = GL_TexMatrixIdentity;
	api->GL_CleanUpTextureUnits    = GL_CleanUpTextureUnits;
	api->GL_TexGen                 = GL_TexGen;
	api->GL_TextureTarget          = GL_TextureTarget;
	api->GL_TexCoordArrayMode      = GL_TexCoordArrayMode;
	api->GL_GetProcAddress         = R_GetProcAddress;
	api->GL_UpdateTexSize          = GL_UpdateTexSize;

	// Misc renderer functions
	api->GL_DrawParticles          = GL_DrawParticles;
	api->LightVec                  = R_LightVec;
	api->StudioGetTexture          = R_StudioGetTexture;
}

// Fill triangleapi_t with renderer-specific functions (called by engine)
static void R_FillTriAPI( triangleapi_t *api )
{
	api->RenderMode    = TriRenderMode;
	api->Begin         = TriBegin;
	api->End           = TriEnd;
	api->Color4f       = TriColor4f;
	api->Color4ub      = TriColor4ub;
	api->TexCoord2f    = TriTexCoord2f;
	api->Vertex3fv     = TriVertex3fv;
	api->Vertex3f      = TriVertex3f;
	api->Fog           = TriFog;
	api->ScreenToWorld = R_ScreenToWorld;
	api->GetMatrix     = TriGetMatrix;
	api->FogParams     = TriFogParams;
	api->CullFace      = TriCullFace;
}

static const ref_interface_t gReffuncs =
{
	.R_Init = R_Init,
	.R_Shutdown = R_VkShutdown,

	.R_GetConfigName = R_GetConfigName,
	.R_SetDisplayTransform = R_SetDisplayTransform,

	// only called for GL contexts
	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions = NULL, // Unused in Vulkan renderer
	.GL_ClearExtensions = GL_ClearExtensions,

	.R_GammaChanged = R_GammaChanged,

	.R_BeginFrame = R_BeginFrame,
	.R_RenderScene = R_RenderScene, // Not called ever?
	.R_EndFrame = R_EndFrame,
	.R_PushScene = R_PushScene,
	.R_PopScene = R_PopScene,
	.GL_BackendStartFrame = GL_BackendStartFrame_UNUSED,
	.GL_BackendEndFrame = GL_BackendEndFrame_UNUSED,

	.R_ClearScreen = R_ClearScreen,
	.R_AllowFog = R_AllowFog,
	.GL_SetRenderMode = GL_SetRenderMode,

	.R_AddEntity = R_AddEntity,
	.R_ProcessEntData = R_ProcessEntData,

	// debug
	.R_ShowTextures = R_ShowTextures_UNUSED,

	// texture management
	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer_UNUSED,
	.GL_LoadTextureFromBuffer = R_TextureUploadFromBuffer,
	.GL_ProcessTexture = GL_ProcessTexture_UNUSED,
	.R_SetupSky = R_TextureSetupCustomSky,

	// 2D
	.R_Set2DMode = R_Set2DMode,
	.R_DrawStretchPic = R_DrawStretchPic,
	.FillRGBA = CL_FillRGBA,
	.WorldToScreen = R_WorldToScreen,

	// screenshot, cubemapshot
	.VID_ScreenShot = VID_ScreenShot,
	.VID_CubemapShot = VID_CubemapShot,

	// light
	.R_LightPoint = R_LightPoint,

	// decals
	.R_DecalShoot = R_DecalShoot,
	.R_DecalRemoveAll = R_DecalRemoveAll,
	.R_CreateDecalList = R_CreateDecalList,
	.R_ClearAllDecals = R_ClearAllDecals,

	.R_StudioEstimateFrame = R_StudioEstimateFrame,
	.R_StudioLerpMovement = R_StudioLerpMovement,
	.R_StudioFillAPI = R_StudioFillAPI,
	.R_StudioSetDrawInterface = R_StudioSetDrawInterface,

	.R_SetSkyCloudsTextures = R_SetSkyCloudsTextures,
	.GL_SubdivideSurface = GL_SubdivideSurface,
	.CL_RunLightStyles = VK_RunLightStyles,

	.Mod_ProcessRenderData = Mod_ProcessRenderData,
	.Mod_StudioLoadTextures = Mod_StudioLoadTextures,

	.CL_DrawParticles = CL_DrawParticles,
	.CL_DrawTracers = CL_DrawTracers,
	.CL_DrawBeams = CL_DrawBeams,

	.RefGetParm = VK_RefGetParm,

	// detail texture scale
	.R_GetDetailScaleForTexture = GetDetailScaleForTexture,
	.R_SetDetailScaleForTexture = NULL, // not implemented

	// Texture tools (used by engine directly)
	.GL_CreateTexture = R_CreateTexture_UNUSED,
	.GL_FindTexture = R_TextureFindByName,
	.GL_TextureName = R_TextureGetNameByIndex,
	.GL_TextureData = R_TextureData_UNUSED,
	.GL_LoadTexture = R_TextureUploadFromFile,
	.GL_FreeTexture = R_TextureFree,
	.R_OverrideTextureSourceSize = R_OverrideTextureSourceSize,

	// glState related calls (used by engine directly)
	.GL_UpdateTexture = NULL, // not implemented
	.GL_Bind = GL_Bind,

	// passed through R_RenderFrame (0 - use engine renderer, 1 - use custom client renderer)
	.GL_RenderFrame = VK_RenderFrame,
	// setup map bounds for ortho-projection when we in dev_overview mode
	.GL_OrthoBounds = GL_OrthoBounds,
	// grab r_speeds message
	.R_SpeedsMessage = R_SpeedsMessage,
	// get visdata for current frame from custom renderer
	.Mod_GetCurrentVis = Mod_GetCurrentVis,
	// tell the renderer what new map is started
	.R_NewMap = R_NewMap,
	// clear the render entities before each frame
	.R_ClearScene = R_ClearScene,

	// TriAPI Interface (functions used by engine wrappers)
	.TriRenderMode = TriRenderMode,
	.Begin = TriBegin,
	.End = TriEnd,
	.Color4f = TriColor4f,
	.Color4ub = TriColor4ub,
	.Vertex3fv = TriVertex3fv,
	.Vertex3f = TriVertex3f,
	.CullFace = TriCullFace,

	// fill render_api_t and triangleapi_t with renderer-specific functions
	.R_FillRenderAPI = R_FillRenderAPI,
	.R_FillTriAPI = R_FillTriAPI,

	.VGUI_SetupDrawing = VGUI_SetupDrawing,

	.pfnGetVulkanRenderDevice = pfnGetRenderDevice,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );
int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	memcpy( funcs, &gReffuncs, sizeof( ref_interface_t ));
	memcpy( &gEngine, engfuncs, sizeof( ref_api_t ));
	gpGlobals = globals;
	gp_cl = (ref_client_t *)ENGINE_GET_PARM( PARM_GET_CLIENT_PTR );
	gp_host = (ref_host_t *)ENGINE_GET_PARM( PARM_GET_HOST_PTR );

	INFO("GetRefAPI version=%d (REF_API_VERSION=%d) funcs=%p engfuncs=%p globals=%p",
		version, REF_API_VERSION, funcs, engfuncs, globals);

	return REF_API_VERSION;
}
