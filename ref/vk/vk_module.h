/*
	vk_module.h - ref/vk modules' API

	TODO(nilsoncore): Explain what this is.
*/

#pragma once
#ifndef VK_MODULE_H
#define VK_MODULE_H

#include "vk_core.h"

typedef enum {
	RVkModuleResult_Success = 0
} RVkModuleResult;

typedef uint8_t RVkModuleState;
enum {
	RVkModuleState_None               = 0,
	RVkModuleState_PreInitialized     = 1,
	RVkModuleState_Initialized        = 2,
	RVkModuleState_FailedToInitialize = 3,
	RVkModuleState_ManualShutdown     = 4,
	RVkModuleState_ForcedShutdown     = 5,

	RVkModuleState_COUNT
};

typedef uint8_t RVkModuleLogLevels;
enum {
	RVkModuleLogLevels_None    = 0,
	RVkModuleLogLevels_Error   = (1 << 0),
	RVkModuleLogLevels_Warning = (1 << 1),
	RVkModuleLogLevels_Info    = (1 << 2),
	RVkModuleLogLevels_Debug   = (1 << 3),

	// Shortcuts:
	RVkModuleLogLevels_AllExceptDebug = RVkModuleLogLevels_Error   |
	                                    RVkModuleLogLevels_Warning |
	                                    RVkModuleLogLevels_Info,
	
	RVkModuleLogLevels_All = RVkModuleLogLevels_AllExceptDebug |
	                         RVkModuleLogLevels_Debug
};

// Forward declare it so we can use it right below before we actually declare its' body.
typedef struct RVkModule RVkModule; 

typedef struct RVkModuleDependencies {
	const char **required_module_names;
	struct RVkModule *modules;
	uint32_t count;
} RVkModuleDependencies;

typedef struct RVkModuleArgs {
	char **args;
	uint32_t count;
} RVkModuleArgs;

// Shortcut to inline-create empty argument list.
// Usage:  module.Init( log_levels, RVkModuleArgs_Empty );
#define RVkModuleArgs_Empty ( RVkModuleArgs ){ .args = NULL, .count = 0 }

typedef uint64_t RVkModuleNameHash;

typedef RVkModuleResult ( *RVkModulePfn_Init )( RVkModuleLogLevels /*log_levels*/, RVkModuleArgs /*args*/ );
typedef    const char * ( *RVkModulePfn_GetResultName )( RVkModuleResult /*result*/ );
typedef            void ( *RVkModulePfn_Shutdown )( void );

typedef struct RVkModule {
	// `PreInitialization`-filled fields.
	// These fields are should be set in `vk_xxx.c` files by declaring global variable `g_module_xxx`
	// and manually setting proper values and function pointers.
	const char *name;
	void *data;
	RVkModuleState state;

	RVkModuleDependencies dependencies;
	uint32_t reference_count;

	RVkModulePfn_Init          Init;
	RVkModulePfn_GetResultName GetResultName;
	RVkModulePfn_Shutdown      Shutdown;

	// `Initialization`-filled fields.
	// These fields are should be set after successful `Init` function completion.
	RVkModuleNameHash hash;
	RVkModuleLogLevels log_levels;
} RVkModule;

uint32_t RVkModule_InitAll( RVkModule* modules, uint32_t count );
const char *RVkModule_GetStateName( RVkModuleState state );
RVkModuleNameHash RVkModule_GetNameHash( const char *name ); // Not implemented yet. (stub)
qboolean RVkModule_IsPreInitialized( RVkModule *module );
qboolean RVkModule_IsInitialized( RVkModule *module );

#endif /* VK_MODULE_H */