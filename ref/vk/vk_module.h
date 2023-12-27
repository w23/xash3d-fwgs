/*
	vk_module.h - ref/vk modules' API

	TODO(nilsoncore): Explain what this is.
*/

#pragma once
#ifndef VK_MODULE_H
#define VK_MODULE_H

#include "vk_common.h"

typedef uint8_t RVkModuleState;
enum {
	RVkModuleState_NotInitialized      = 0,
	RVkModuleState_IsInitializing      = 1,
	RVkModuleState_Initialized         = 2,
	RVkModuleState_Shutdown            = 3,

	RVkModuleState_COUNT
};

/* NOTE(nilsoncore): Unused for now. We should decide whether we keep this or not.
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
*/

// Forward declare it so we can use it right below before we actually declare its' body.
typedef struct RVkModule RVkModule; 

typedef struct RVkModuleDependencies {
	RVkModule **modules;
	uint32_t count;
} RVkModuleDependencies;

#define RVkModuleDependencies_Empty { .modules = NULL, .count = 0 }
#define RVkModuleDependencies_FromStaticArray( array ) { .modules = array, .count = ( uint32_t ) COUNTOF( array ) }

typedef qboolean ( *RVkModulePfn_Init )( void );
typedef     void ( *RVkModulePfn_Shutdown )( void );

#define XRVkModule_OnInitStart( module ) do { \
	if ( module.state == RVkModuleState_Initialized ) { \
		return true; \
	} \
	if ( module.init_caller != NULL ) { \
		gEngine.Con_Printf( "RVkModule: Initializing '%s' from '%s'...\n", module.name, module.init_caller->name ); \
	} else { \
		gEngine.Con_Printf( "RVkModule: Initializing '%s'...\n", module.name ); \
	} \
	module.state = RVkModuleState_IsInitializing; \
	qboolean deps_inited = RVkModule_InitDependencies( &module ); \
	if ( !deps_inited ) { \
		gEngine.Con_Printf( S_ERROR "RVkModule: Failed to init dependencies for '%s'.\n", module.name ); \
		module.state = RVkModuleState_NotInitialized; \
		return false; \
	} \
} while(0)

#define XRVkModule_OnInitEnd( module ) do { \
	module.state = RVkModuleState_Initialized; \
	if ( module.init_caller != NULL ) { \
		gEngine.Con_Printf( "RVkModule: '%s' from '%s' has been initialized.\n", module.name, module.init_caller->name ); \
	} else { \
		gEngine.Con_Printf( "RVkModule: '%s' has been initialized.\n", module.name ); \
	} \
} while(0)

#define XRVkModule_OnShutdownStart( module ) do { \
	if ( module.state != RVkModuleState_Initialized ) { \
		return; \
	} \
	gEngine.Con_Printf( "RVkModule: Shutting down '%s'...\n", module.name ); \
} while(0)

#define XRVkModule_OnShutdownEnd( module ) do { \
	module.state = RVkModuleState_Shutdown; \
	gEngine.Con_Printf( "RVkModule: '%s' has been shut down.\n", module.name ); \
} while(0)

typedef struct RVkModule {
	const char *name;
	RVkModuleState state;

	RVkModuleDependencies dependencies;
	uint32_t reference_count;

	RVkModule *init_caller;

	RVkModulePfn_Init     Init;
	RVkModulePfn_Shutdown Shutdown;

	// NOTE(nilsoncore): Unused for now. See comment above.
	// RVkModuleLogLevels log_levels;
} RVkModule;

qboolean    RVkModule_OnInitStart( RVkModule *module );
void        RVkModule_OnInitEnd( RVkModule *module);
qboolean    RVkModule_OnShutdownStart( RVkModule *module );
void        RVkModule_OnShutdownEnd( RVkModule *module );

qboolean    RVkModule_InitDependencies( RVkModule *module );
void        RVkModule_PrintDependencyList( RVkModule *module, qboolean print_states );
void        RVkModule_PrintDependencyTree( RVkModule *module ); // TODO(nilsoncore)
const char *RVkModule_GetStateName( RVkModuleState state );

#endif /* VK_MODULE_H */