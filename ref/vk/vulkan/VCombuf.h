#pragma once

#include "vk_core.h"
#include "std/arrays.h"
#include "std/profiler.h"

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
// Counters then are reported for each GPU scope with VCombufScopeFlag_PerfQuery flag.
int R_VkCombufPerfQueryEnable(const uint32_t *counters, uint32_t counters_count);

typedef struct VCombufProfilingResult {
	// Command buffer execution lifetime
	uint64_t begin_ns, end_ns;

	// All registered GPU profiler scopes and counters
	VIEW_DECLARE_CONST(aprof_scope_t, scopes);

	// Same indexes as into `v_device_info.perf_counters` arrays
	VIEW_DECLARE_CONST(aprof_counter_desc_t, counters);

	// All events during this command buffer submission
	VIEW_DECLARE_CONST(aprof_event_t, events);
} VCombufProfilingResult;

// Reads all the scope timing data (timestamp queries) and returns a list of things happened this frame.
// Prerequisite: all relevant recorded command buffers should've been completed and waited on already.
// The returned pointer remains valid until any next R_VkGpu*() call.
VCombufProfilingResult R_VkCombufProfilingGetResult(vk_combuf_t *);
