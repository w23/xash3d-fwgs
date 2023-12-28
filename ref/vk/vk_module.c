#include "vk_module.h"
#include "debugbreak.h"

// NOTE(nilsoncore):
// These `replica` functions are broken for now because I didn't update them to be exact as `XRVkModule_Onxxx` macros.
// The reason why these functions exist (for now) is that I don't know whether we should use macros or functions like these.
// For now we just wrap around every `Impl_Init` module function with `XRVkModule_OnxxxStart` and `XRVkModule_OnxxxEnd`.
// 
// This approach is quite simple and clean: you just write this construction passing it module and that's it.
// You don't have to do anything else and know how it works inside (for the most part).
// Although, it has a downside: these macros are CHUNKY. We don't just call some function, we paste the whole code inline
// for every module out there.  It probably grows the binary size a lot (didn't measure).
// 
// The second approach is to use regular functions.  It is probably more conventional and rational way, but it has downsides too.
// You can't just write it and expect it to work.  The placing is the same as with the macros, but things start to become tricky.
// We can't use `qboolean` as a result value because of the `already initialized?` check.
// If module is already initialized we return `true` from the module `Impl_Init` function.  Here inside `OnInitStart` it's too ambigious -
// does that mean that module is already initialized and we are done or module has loaded dependencies and ready to initialize?
// In this case we should probably declare some enum with states like `_AlreadyInitialized`, `_DependenciesLoaded`, `_FailedToLoadDependecies`, etc.
// In the end we will end up not just with the boilerplate code that we have to paste everywhere, but also we can't make change in one place --
// every change must be then made manually in every file.
// 
// Whether we want it or not -- we must decide which approach we will use. Don't be afraid to comment on this if I missed something.

qboolean RVkModule_OnInitStart( RVkModule *module ) {
	if ( module->state == RVkModuleState_Initialized )
		return true;

	gEngine.Con_Printf( "RVkModule: Initializing '%s'...\n", module->name );
	module->state = RVkModuleState_IsInitializing;
	
	qboolean deps_inited = RVkModule_InitDependencies( module );
	if ( !deps_inited ) {
		module->state = RVkModuleState_NotInitialized;
		return false;
	}

	return true;
}

void RVkModule_OnInitEnd( RVkModule *module ) {
	module->state = RVkModuleState_Initialized;
	gEngine.Con_Printf( "RVkModule: '%s' has been initialized.\n", module->name );
}

qboolean RVkModule_OnShutdownStart( RVkModule *module ) {
	if ( module->state != RVkModuleState_Initialized )
		return false;

	gEngine.Con_Printf( "RVkModule: Shutting down '%s'...\n", module->name );
	return true;
}

void RVkModule_OnShutdownEnd( RVkModule *module ) {
	module->state = RVkModuleState_Shutdown;
	gEngine.Con_Printf( "RVkModule: '%s' has been shut down.\n", module->name );
}

qboolean RVkModule_InitDependencies( RVkModule *module ) {
	ASSERT( module && "Module pointer must be valid" );

	const uint32_t deps_count = module->dependencies.count;
	qboolean second_try = false;
	uint32_t deps_in_progress_count = 0;

init_dependencies:
	for ( uint32_t dep_module_index = 0; dep_module_index < deps_count; dep_module_index += 1 ) {
		RVkModule *const dep_module = module->dependencies.modules[dep_module_index];

		if ( dep_module == module ) {
			// NOTE(nilsoncore): Just exit with error because module dependencies are
			// set statically, so this must be fixed in source code.
			gEngine.Host_Error( "RVkModule: Failed to init dependencies for '%s' - module depends on itself.\n", module->name );
		}

		if ( dep_module->state == RVkModuleState_IsInitializing ) {
			if ( second_try ) {
				RVkModule_PrintDependencyList( module, true );
				RVkModule_PrintDependencyList( dep_module, true );
				// NOTE(nilsoncore): Just exit with error because module dependencies are
				// set statically, so this must be fixed in source code.
				gEngine.Host_Error( "RVkModule: Failed to init dependencies for '%s' - module is stuck on '%s' (it is a circular dependency chain).\n", module->name, dep_module->name );
			} else {
				deps_in_progress_count += 1;
				continue;
			}
		}

		if ( dep_module->init_caller == NULL &&
			 dep_module->state != RVkModuleState_Initialized &&
			 dep_module->state != RVkModuleState_IsInitializing )
		{
			dep_module->init_caller = module;
		}

		if ( !dep_module->Init() ) {
			gEngine.Con_Printf( S_ERROR "Module '%s' failed to init dependency '%s'.\n", module->name, dep_module->name );
			return false;
		}
	}

	second_try = ( deps_in_progress_count > 0 );
	if ( second_try )
		goto init_dependencies;
	
	return true;
}

void RVkModule_PrintDependencyList( RVkModule *module, qboolean print_states ) {
	ASSERT( module && "Module pointer must be valid" );

	const uint32_t deps_count = module->dependencies.count;
	if ( deps_count == 0 ) {
		gEngine.Con_Printf( "Module '%s' has 0 dependencies.\n", module->name );
	} else {
		gEngine.Con_Printf( "Module '%s' has %u dependenc%s: [", module->name, deps_count, ( deps_count == 1 ) ? "y" : "ies" );

		// #0
		uint32_t dep_module_index = 0;
		RVkModule *const dep_module = module->dependencies.modules[dep_module_index];
		if ( print_states )  gEngine.Con_Printf( "%s (%s)", dep_module->name, RVkModule_GetStateName( dep_module->state ) );
		else                 gEngine.Con_Printf( "%s", dep_module->name );

		// #1..deps_count
		for ( dep_module_index = 1; dep_module_index < deps_count; dep_module_index += 1 ) {
			RVkModule *const dep_module = module->dependencies.modules[dep_module_index];
			if ( print_states )  gEngine.Con_Printf( ", %s (%s)", dep_module->name, RVkModule_GetStateName( dep_module->state ) );
			else                 gEngine.Con_Printf( ", %s", dep_module->name );
		}
	
		gEngine.Con_Printf( "]\n" );
	}
}

void RVkModule_PrintDependencyTree( RVkModule *module ) {
	ASSERT( module && "Module pointer must be valid" );

	gEngine.Con_Printf( "Dependency tree of module '%s':\n", module->name );
	// TODO(nilsoncore)
}

const char *RVkModule_GetStateName( RVkModuleState state ) {
	switch ( state ) {
		case RVkModuleState_NotInitialized:  return "NotInitialized";
		case RVkModuleState_IsInitializing:  return "IsInitializing";
		case RVkModuleState_Initialized:     return "Initialized";
		case RVkModuleState_Shutdown:        return "Shutdown";
		default:                             return "(invalid)";
	}
}