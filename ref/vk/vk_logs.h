#pragma once

#include "vk_common.h"

#define LIST_LOG_MODULES(X) \
	X(core) \
	X(misc) \
	X(tex) \
	X(brush) \
	X(light) \
	X(studio) \
	X(patch) \
	X(mat) \
	X(meat) \
	X(rt) \
	X(rmain) \
	X(sprite) \
	X(img) \
	X(staging) \
	X(buf) \
	X(fctl) \
	X(combuf) \

enum {
#define X(m) LogModule_##m,
LIST_LOG_MODULES(X)
#undef X
};

extern uint32_t g_log_debug_bits;

// TODO:
// - load bits early at startup somehow. cvar is empty at init for some reason
// - module name in message
// - file:line in message
// - consistent prefixes (see THROTTLED variant)

#define LOG_BIT(m) (1 << LogModule_##m)
#define LOG_NAME(m) #m

#define LOG_VERBOSE_IMPL(m) (g_log_debug_bits & LOG_BIT(m))
#define LOG_VERBOSE LOG_VERBOSE_IMPL(LOG_MODULE)

#define DEBUG_IMPL(module, msg, ...) \
	do { \
		if (g_log_debug_bits & LOG_BIT(module)) { \
			gEngine.Con_Reportf("vk/" LOG_NAME(module) ": " msg "\n", ##__VA_ARGS__); \
		} \
	} while(0)

#define WARN_IMPL(module, msg, ...) \
	do { \
		gEngine.Con_Printf(S_WARN "vk/" LOG_NAME(module) ": " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define ERR_IMPL(module, msg, ...) \
	do { \
		gEngine.Con_Printf(S_ERROR "vk/" LOG_NAME(module) ": " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define INFO_IMPL(module, msg, ...) \
	do { \
		gEngine.Con_Printf("vk/" LOG_NAME(module) ": " msg "\n", ##__VA_ARGS__); \
	} while(0)

#define PRINT_THROTTLED(delay, prefix, msg, ...) \
	do { \
		static int called = 0; \
		static double next_message_time = 0.; \
		if (gp_host->realtime > next_message_time) { \
			gEngine.Con_Printf( prefix "(x%d) " msg "\n", called, ##__VA_ARGS__ ); \
			next_message_time = gp_host->realtime + delay; \
		} \
		++called; \
	} while(0)

#define DEBUG(msg, ...) DEBUG_IMPL(LOG_MODULE, msg, ##__VA_ARGS__)
#define WARN(msg, ...) WARN_IMPL(LOG_MODULE, msg, ##__VA_ARGS__)
#define ERR(msg, ...) ERR_IMPL(LOG_MODULE, msg, ##__VA_ARGS__)
#define INFO(msg, ...) INFO_IMPL(LOG_MODULE, msg, ##__VA_ARGS__)

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
