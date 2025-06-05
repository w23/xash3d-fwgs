#pragma once

#include "vk_core.h"

#define MAX_GPU_SCOPES 64

typedef struct vk_combuf_s {
	VkCommandBuffer cmdbuf;
} vk_combuf_t;

qboolean R_VkCombuf_Init( void );
void R_VkCombuf_Destroy( void );

vk_combuf_t* R_VkCombufOpen( void );
void R_VkCombufClose( vk_combuf_t* );

void R_VkCombufBegin( vk_combuf_t* );
void R_VkCombufEnd( vk_combuf_t* );


// TODO rename consistently
int R_VkGpuScope_Register(const char *name);

enum {
	VCombufScopeFlag_None = 0,
	VCombufScopeFlag_PerfQuery = (1<<0),
};
int R_VkCombufScopeBegin(vk_combuf_t*, int scope_id, uint32_t flags);
void R_VkCombufScopeEnd(vk_combuf_t*, int begin_index, VkPipelineStageFlagBits pipeline_stage);

// Non-null counters enable perf query for the set of counters, NULL+0 -- disable.
// returns 0 if failed, 1 on success
// Counters then are reported for each gpu scope
int R_VkCombufPerfQueryEnable(const uint32_t *counters, uint32_t counters_count);

typedef struct {
	const char *name;
} vk_combuf_scope_t;

typedef struct {
	uint32_t counter; // Index into VK_KHR_performance_query counters
	uint64_t value; // raw value for everything, except %. % are in hundredths, i.e. 10000 is 100%, 2523 is 25.23%
} VPerfCounter;

typedef struct vk_combuf_scopes_s {
	const uint64_t *timestamps;
	const vk_combuf_scope_t *scopes;
	const int *entries; // index into scopes; each entry consumes 2 values from timestamps array sequentially
	int entries_count;

	// FIXME how to expose this properly
	int perf_counters_count;
	const VPerfCounter *perf_counters;
} vk_combuf_scopes_t;

// Reads all the scope timing data (timestamp queries) and returns a list of things happened this frame.
// Prerequisite: all relevant recorded command buffers should've been completed and waited on already.
// The returned pointer remains valid until any next R_VkGpu*() call.
vk_combuf_scopes_t R_VkCombufScopesGet( vk_combuf_t * );
