#include "vk_core.h"

#include "vk_common.h"
#include "r_textures.h"
#include "vk_overlay.h"
#include "vulkan/VImage.h"
#include "vulkan/VStaging.h"
#include "vk_framectl.h"
#include "vk_brush.h"
#include "vk_scene.h"
#include "vk_cvar.h"
#include "vulkan/VPipeline.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_studio.h"
#include "vk_rtx.h"
#include "vulkan/VDescriptor.h"
#include "vulkan/VResource.h"
#include "vulkan/VNvAftermath.h"
#include "vulkan/VDevmem.h"
#include "r_speeds.h"
#include "vk_speeds.h"
#include "vk_sprite.h"
#include "vk_beams.h"
#include "r_decals.h"
#include "vulkan/VCombuf.h"
#include "vk_entity_data.h"
#include "vk_logs.h"
#include "std/arrays.h"

// FIXME move this rt-specific stuff out
#include "vk_light.h"

#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h" // required for ref_api.h
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"
#include "eiface.h"

#include "std/debugbreak.h"

#include <string.h>

#define LOG_MODULE core

#define NULLINST_FUNCS(X) \
	X(vkEnumerateInstanceVersion) \
	X(vkCreateInstance) \

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define X(f) PFN_##f f = NULL;
	NULLINST_FUNCS(X)
	INSTANCE_FUNCS(X)
	INSTANCE_DEBUG_FUNCS(X)
#undef X

static dllfunc_t nullinst_funcs[] = {
#define X(f) {#f, (void**)&f},
	NULLINST_FUNCS(X)
#undef X
};

static dllfunc_t instance_funcs[] = {
#define X(f) {#f, (void**)&f},
	INSTANCE_FUNCS(X)
#undef X
};

static dllfunc_t instance_debug_funcs[] = {
#define X(f) {#f, (void**)&f},
	INSTANCE_DEBUG_FUNCS(X)
#undef X
};

static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

static VkBool32 VKAPI_PTR debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
	void *pUserData) {
	(void)(pUserData);
	(void)(messageTypes);
	(void)(messageSeverity);

	// TODO better messages, not only errors, what are other arguments for, ...
	if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		gEngine.Con_Printf(S_ERROR "vk/dbg: %s\n", pCallbackData->pMessage);
#ifdef _MSC_VER
		__debugbreak();
#else
		debug_break();
#endif
	} else {
		if (Q_strcmp(pCallbackData->pMessageIdName, "UNASSIGNED-DEBUG-PRINTF") == 0) {
			gEngine.Con_Printf(S_ERROR "vk/dbg: %s\n", pCallbackData->pMessage);
		} else {
			gEngine.Con_Printf(S_WARN "vk/dbg: %s\n", pCallbackData->pMessage);
		}
	}

	return VK_FALSE;
}

vulkan_core_t vk_core = {0};

static void loadInstanceFunctions(dllfunc_t *funcs, int count)
{
	for (int i = 0; i < count; ++i)
	{
		*funcs[i].func = vkGetInstanceProcAddr(vk_core.instance, funcs[i].name);
		if (!*funcs[i].func)
		{
			gEngine.Con_Printf( S_WARN "Function %s was not loaded\n", funcs[i].name);
		}
	}
}

static qboolean createInstance( void )
{
	const char ** instance_extensions = NULL;
	unsigned int num_instance_extensions = vk_core.debug ? 1 : 0;
	const VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		// TODO support versions 1.0 and 1.1 for simple traditional rendering
		// This would require using older physical device features and props query structures
		// .apiVersion = vk_core.rtx ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1,
		.apiVersion = VK_API_VERSION_1_3,
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0), // TODO
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.pApplicationName = "",
		.pEngineName = "xash3d-fwgs",
	};

	BOUNDED_ARRAY(VkValidationFeatureEnableEXT, validation_features, 8);
	BOUNDED_ARRAY_APPEND_ITEM(validation_features, VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
	BOUNDED_ARRAY_APPEND_ITEM(validation_features, VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
	BOUNDED_ARRAY_APPEND_ITEM(validation_features, VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);

	if (!!gEngine.Sys_CheckParm("-vkdbg_shaderprintf"))
		BOUNDED_ARRAY_APPEND_ITEM(validation_features, VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);

	const VkValidationFeaturesEXT validation_ext = {
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.pEnabledValidationFeatures = validation_features.items,
		.enabledValidationFeatureCount = validation_features.count,
	};

	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.pNext = vk_core.validate ? &validation_ext : NULL,
	};

	int vid_extensions = gEngine.XVK_GetInstanceExtensions(0, NULL);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		return false;
	}

	num_instance_extensions += vid_extensions;

	instance_extensions = Mem_Malloc(vk_core.pool, sizeof(const char*) * num_instance_extensions);
	vid_extensions = gEngine.XVK_GetInstanceExtensions(vid_extensions, instance_extensions);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		Mem_Free((void*)instance_extensions);
		return false;
	}

	if (vk_core.debug)
	{
		instance_extensions[vid_extensions] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

	gEngine.Con_Reportf("Requesting instance extensions: %d\n", num_instance_extensions);
	for (int i = 0; i < num_instance_extensions; ++i)
	{
		gEngine.Con_Reportf("\t%d: %s\n", i, instance_extensions[i]);
	}

	create_info.enabledExtensionCount = num_instance_extensions;
	create_info.ppEnabledExtensionNames = instance_extensions;

	if (vk_core.validate)
	{
		create_info.enabledLayerCount = ARRAYSIZE(validation_layers);
		create_info.ppEnabledLayerNames = validation_layers;

		gEngine.Con_Printf(S_WARN "Using Vulkan validation layers, expect severely degraded performance\n");
	}

	// TODO handle errors gracefully -- let it try next renderer
	XVK_CHECK(vkCreateInstance(&create_info, NULL, &vk_core.instance));

	loadInstanceFunctions(instance_funcs, ARRAYSIZE(instance_funcs));

	if (vk_core.debug || vk_core.validate)
	{
		loadInstanceFunctions(instance_debug_funcs, ARRAYSIZE(instance_debug_funcs));

 		if (vk_core.validate) {
			if (vkCreateDebugUtilsMessengerEXT) {
				VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
					.messageSeverity = 0x1111, //:vovka: VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
					.messageType = 0x07,
					.pfnUserCallback = debugCallback,
				};
				XVK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_core.instance, &debug_create_info, NULL, &vk_core.debug_messenger));
			} else {
				gEngine.Con_Printf(S_WARN "Vulkan debug utils messenger is not available\n");
			}
		}
	}

	Mem_Free((void*)instance_extensions);
	return true;
}

static qboolean initSurface( void )
{
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(v_device_info.physical_device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));
	vk_core.surface.present_modes = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.present_modes) * vk_core.surface.num_present_modes);
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(v_device_info.physical_device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));

	gEngine.Con_Printf("Supported surface present modes: %u\n", vk_core.surface.num_present_modes);
	for (uint32_t i = 0; i < vk_core.surface.num_present_modes; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s (%u)\n", i, R_VkPresentModeName(vk_core.surface.present_modes[i]), vk_core.surface.present_modes[i]);
	}

	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(v_device_info.physical_device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));
	vk_core.surface.surface_formats = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.surface_formats) * vk_core.surface.num_surface_formats);
	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(v_device_info.physical_device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));

	gEngine.Con_Reportf("Supported surface formats: %u\n", vk_core.surface.num_surface_formats);
	for (uint32_t i = 0; i < vk_core.surface.num_surface_formats; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s(%u) %s(%u)\n", i,
			R_VkFormatName(vk_core.surface.surface_formats[i].format), vk_core.surface.surface_formats[i].format,
			R_VkColorSpaceName(vk_core.surface.surface_formats[i].colorSpace), vk_core.surface.surface_formats[i].colorSpace);
	}

	return true;
}

// TODO modules
/*
typedef struct r_vk_module_s {
	qboolean (*init)(void);
	void (*destroy)(void);

	// TODO next: dependecies, refcounts, ...
} r_vk_module_t;

#define LIST_MODULES(X) ...

=>
extern const r_vk_module_t vk_instance_module;
...
extern const r_vk_module_t vk_rtx_module;
...

=>
static const r_vk_module_t *const modules[] = {
	&vk_instance_module,
	&vk_device_module,
	&vk_aftermath_module,
	&vk_texture_module,
	...
	&vk_rtx_module,
	...
};
*/

qboolean R_VkInit( void )
{
	// FIXME !!!! handle initialization errors properly: destroy what has already been created
	INFO("R_VkInit");

	vk_core.validate = !!gEngine.Sys_CheckParm("-vkvalidate");
	vk_core.debug = vk_core.validate || !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));
	vk_core.rtx = false;

	VK_LoadCvars();

	// Force extremely verbose logs at startup.
	// This is instrumental in some investigations, because the usual "vk_debug_log" cvar is not set
	// at this point and cannot be used to selectively swith things on.
	if (gEngine.Sys_CheckParm("-vkverboselogs"))
		g_log_debug_bits = 0xffffffffu;

	R_SpeedsInit();
	VK_SpeedsInit();

	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video\n" );
		return false;
	}

	vkGetInstanceProcAddr = gEngine.XVK_GetVkGetInstanceProcAddr();
	if (!vkGetInstanceProcAddr)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get vkGetInstanceProcAddr address\n" );
		return false;
	}

	vk_core.pool = Mem_AllocPool("Vulkan pool");

	loadInstanceFunctions(nullinst_funcs, ARRAYSIZE(nullinst_funcs));

	if (vkEnumerateInstanceVersion)
	{
		vkEnumerateInstanceVersion(&vk_core.vulkan_version);
	}
	else
	{
		vk_core.vulkan_version = VK_MAKE_VERSION(1, 0, 0);
	}

	gEngine.Con_Printf( "Vulkan version %u.%u.%u\n", XVK_PARSE_VERSION(vk_core.vulkan_version));

	if (!createInstance())
		return false;

	vk_core.surface.surface = gEngine.XVK_CreateSurface(vk_core.instance);
	if (!vk_core.surface.surface)
	{
		gEngine.Con_Printf( S_ERROR "Cannot create Vulkan surface\n" );
		return false;
	}

#if USE_AFTERMATH
	if (!VK_AftermathInit()) {
		gEngine.Con_Printf( S_ERROR "Cannot initialize Nvidia Nsight Aftermath SDK\n" );
	}
#endif

	if (!vDeviceInit((VDeviceInitArgs){
				.force_disable_rt = CVAR_TO_BOOL(rt_force_disable),
				.enable_perf_query = gEngine.Sys_CheckParm("-vkperfquery"),
			}))
		return false;

	VK_LoadCvarsAfterInit();

	R_VkResourcesInit();

	if (!R_VkImageInit())
		return false;

	if (!R_VkCombuf_Init())
		return false;

	if (!initSurface())
		return false;

	if (!VK_DevMemInit())
		return false;

	if (!R_VkStagingInit())
		return false;

	if (!VK_PipelineInit())
		return false;

	// TODO ...
	if (!VK_DescriptorInit())
		return false;

	if (!VK_FrameCtlInit())
		return false;

	if (!R_GeometryBuffer_Init())
		return false;

	if (!VK_RenderInit())
		return false;

	VK_StudioInit();

	VK_SceneInit();

	R_TexturesInit();

	// All below need render_pass

	if (!R_VkOverlay_Init())
		return false;

	if (!R_BrushInit())
		return false;

	if (vk_core.rtx)
	{
		// FIXME move all this to rt-specific modules
		if (!VK_LightsInit())
			return false;

		if (!VK_RayInit())
			return false;
	}

	R_SpriteInit();
	R_BeamInit();

	R_ClearDecals();

	INFO("R_VkInit done");
	return true;
}

void R_VkShutdown( void ) {
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	VK_EntityDataClear();

	R_SpriteShutdown();

	if (vk_core.rtx)
	{
		VK_LightsShutdown();
		VK_RayShutdown();
	}

	R_BrushShutdown();
	VK_StudioShutdown();
	R_VkOverlay_Shutdown();

	VK_RenderShutdown();
	R_GeometryBuffer_Shutdown();

	VK_FrameCtlShutdown();

	R_VkMaterialsShutdown();

	R_TexturesShutdown();

	VK_PipelineShutdown();

	VK_DescriptorShutdown();

	R_VkStagingShutdown();

	R_VkCombuf_Destroy();

	VK_DevMemDestroy();

	vDeviceShutdown();

#if USE_AFTERMATH
	VK_AftermathShutdown();
#endif

	if (vk_core.debug_messenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
	}

	Mem_Free(vk_core.surface.present_modes);
	Mem_Free(vk_core.surface.surface_formats);
	vkDestroySurfaceKHR(vk_core.instance, vk_core.surface.surface, NULL);
	vkDestroyInstance(vk_core.instance, NULL);
	Mem_FreePool(&vk_core.pool);

	gEngine.R_Free_Video();
}

VkSemaphore R_VkSemaphoreCreate( void ) {
	VkSemaphore sema;
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.flags = 0,
	};
	XVK_CHECK(vkCreateSemaphore(vk_core.device, &sci, NULL, &sema));
	return sema;
}

void R_VkSemaphoreDestroy(VkSemaphore sema) {
	vkDestroySemaphore(vk_core.device, sema, NULL);
}

VkFence R_VkFenceCreate( qboolean signaled ) {
	VkFence fence;
	const VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
	};
	XVK_CHECK(vkCreateFence(vk_core.device, &fci, NULL, &fence));
	return fence;
}

void R_VkFenceDestroy(VkFence fence) {
	vkDestroyFence(vk_core.device, fence, NULL);
}
