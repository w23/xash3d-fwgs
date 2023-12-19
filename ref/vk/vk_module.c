#include "vk_module.h"

uint32_t RVkModule_InitAll( RVkModule* modules, uint32_t count ) {
	uint32_t module_index = 0;
	while ( module_index < count ) {
		RVkModule *const module = &modules[module_index];
		if ( !RVkModule_IsPreInitialized( module ) )
			break;

		if ( module->Init( RVkModuleLogLevels_All, RVkModuleArgs_Empty ) != RVkModuleResult_Success ) {
			module->Shutdown();
			break;
		}

		module_index += 1;
	}

	qboolean all_modules_inited = ( module_index == count );
	if ( !all_modules_inited ) {
		int32_t last_inited_module_index = module_index - 1;
		while ( last_inited_module_index >= 0 ) {
			RVkModule *const module = &modules[last_inited_module_index];
			module->Shutdown();
		}
	}

	return module_index;
}

const char *RVkModule_GetStateName( RVkModuleState state ) {
	switch ( state ) {
		case RVkModuleState_None:               return "None";
		case RVkModuleState_PreInitialized:     return "PreInitialized";
		case RVkModuleState_Initialized:        return "Initialize";
		case RVkModuleState_FailedToInitialize: return "FailedToInitialize";
		case RVkModuleState_ManualShutdown:     return "ManualShutdown";
		case RVkModuleState_ForcedShutdown:     return "ForcedShutdown";
		default:                                return "(invalid)";
	}
}

RVkModuleNameHash RVkModule_GetNameHash( const char *name ) {
	// TODO(nilsoncore): FIXME(nilsoncore): Implement real hashing function.
	// Quick hack for now:
	static RVkModuleNameHash g_module_counter = 0;
	return g_module_counter++;
}

qboolean RVkModule_IsPreInitialized( RVkModule *module ) {
	// TODO(nilsoncore): Also make sure it is null-terminated, not empty, etc.
	// This would probably require some other headers with functions
	// like `Q_strlen` and others.
	// ---
	// Check that the name is set.
	if ( !module->name )
		return false;

	// Check that the pointer to internal module data is set. 
	if ( !module->data )
		return false;

	// Check that the state is set properly.
	if ( module->state != RVkModuleState_PreInitialized)
		return false;

	// Check that all the function pointers are at least pointing somewhere
	// (we cannot staticly check that they are pointing to real functions).
	if ( !module->Init )
		return false;

	if ( !module->GetResultName )
		return false;

	if ( !module->Shutdown )
		return false;

	return true;
}

qboolean RVkModule_IsInitialized( RVkModule *module ) {
	// Check that the state is set properly.
	if ( module->state != RVkModuleState_Initialized )
		return false;
	
	// Check that the name's hash has at least some non-zero value
	// (we cannot check whether the value is `valid`
	// since hashing function should be unpredictable).
	if ( module->hash == 0 )
		return false;

	return true;
}
