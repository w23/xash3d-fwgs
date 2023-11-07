#pragma once

#include "vk_common.h"

enum {
	LogModule_Misc = (1<<0),
	LogModule_Textures = (1<<1),
	LogModule_Brush = (1<<2),
	LogModule_Lights = (1<<3),
	LogModule_Studio = (1<<4),
	LogModule_Patch = (1<<5),
	LogModule_Material = (1<<6),
	LogModule_Meatpipe = (1<<7),
	LogModule_RT = (1<<8),
	LogModule_RMain = (1<<9),
	LogModule_Sprite = (1<<10),
};

extern uint32_t g_log_debug_bits;

// TODO:
// - load bits early at startup somehow. cvar is empty at init for some reason
// - module name in message
// - file:line in message
// - consistent prefixes (see THROTTLED variant)

#define DEBUG(msg, ...) \
	do { \
		if (g_log_debug_bits & (LOG_MODULE)) { \
			gEngine.Con_Reportf("vk: " msg "\n", ##__VA_ARGS__); \
		} \
	} while(0)

#define WARN(msg, ...) \
	do { \
		gEngine.Con_Printf(S_WARN "vk: " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define ERR(msg, ...) \
	do { \
		gEngine.Con_Printf(S_ERROR "vk: " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define INFO(msg, ...) \
	do { \
		gEngine.Con_Printf("vk: " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define PRINT_THROTTLED(delay, prefix, msg, ...) \
	do { \
		static int called = 0; \
		static double next_message_time = 0.; \
		if (gpGlobals->realtime > next_message_time) { \
			gEngine.Con_Printf( prefix "(x%d) " msg "\n", called, ##__VA_ARGS__ ); \
			next_message_time = gpGlobals->realtime + delay; \
		} \
		++called; \
	} while(0)

#define ERROR_THROTTLED(delay, msg, ...) PRINT_THROTTLED(delay, S_ERROR "vk: ", msg, ##__VA_ARGS__)

#define WARN_THROTTLED(delay, msg, ...) PRINT_THROTTLED(delay, S_WARN "vk: ", msg, ##__VA_ARGS__)

#define PRINT_NOT_IMPLEMENTED_ARGS(msg, ...) do { \
		static int called = 0; \
		if ((called&1023) == 0) { \
			gEngine.Con_Printf( S_ERROR "VK NOT_IMPLEMENTED(x%d): %s " msg "\n", called, __FUNCTION__, ##__VA_ARGS__ ); \
		} \
		++called; \
	} while(0)

#define PRINT_NOT_IMPLEMENTED() do { \
		static int called = 0; \
		if ((called&1023) == 0) { \
			gEngine.Con_Printf( S_ERROR "VK NOT_IMPLEMENTED(x%d): %s\n", called, __FUNCTION__ ); \
		} \
		++called; \
	} while(0)

void R_LogSetVerboseModules( const char *modules );
