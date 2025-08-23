#include "r_speeds.h"
#include "vk_overlay.h"
#include "vk_framectl.h"
#include "vk_cvar.h"
#include "vk_logs.h"
#include "vulkan/VCombuf.h"
#include "std/stringview.h"

#include "std/profiler.h"

#include "crclib.h" // CRC32 for stable random colors
#include "xash3d_mathlib.h" // Q_min
#include <limits.h>

#define MAX_GPU_SCOPES 64

#define MAX_SPEEDS_MESSAGE (1024)
#define MAX_SPEEDS_METRICS (512)
#define TARGET_FRAME_TIME (1000.f / 60.f)
#define MAX_GRAPHS 8
#define MAX_COUNTERS_PER_SCOPE (32)

#define MODULE_NAME "speeds"
#define LOG_MODULE speeds

// Valid bits for `r_speeds` argument:
enum {
	SPEEDS_BIT_OFF = 0,       // `r_speeds 0` turns off all performance stats display
	SPEEDS_BIT_SIMPLE = 1,    // `r_speeds 1` displays only basic info about frame time
	SPEEDS_BIT_STATS = 2,     // `r_speeds 2` displays additional metrics, i.e. lights counts, dynamic geometry upload sizes, etc
	SPEEDS_BIT_GRAPHS = 4,     // `r_speeds 4` display instrumental metrics graphs, controlled by r_speeds_graphs var
	SPEEDS_BIT_FRAME = 8,     // `r_speeds 8` diplays details instrumental profiler flame graph
	// TODO SPEEDS_BIT_GPU_USAGE = 16, // `r_speeds 16` displays overall GPU usage stats

	// These bits can be combined, e.g. `r_speeds 9`, 8+1, will display 1: basic timing info and 8: frame graphs
};

typedef struct {
	int *p_value;
	qboolean reset;
	char name[64];
	const char *var_name;
	const char *src_file;
	int src_line;
	r_speeds_metric_type_t type;
	int low_watermark, high_watermark, max_value;
	int graph_index;
} r_speeds_metric_t;

typedef struct {
	char name[64];
	float *data;
	int data_count;
	int data_write;
	int source_metric; // can be -1 for missing metrics

	int height;
	int max_value; // Computed automatically every frame
	rgba_t color;
} r_speeds_graph_t;

typedef enum {
	kSpeedsMprintNone,
	kSpeedsMprintList,
	kSpeedsMprintTable
} r_speeds_mprint_mode_t;

typedef struct {
	int initialized;
	int value;
} Metacounter;

typedef struct {
	int initialized;
	int time_us; // automatically zeroed by metrics each frame
	Metacounter counters[MAX_COUNTERS_PER_SCOPE];
} Metascope;

static struct {
	cvar_t *r_speeds_graphs;
	cvar_t *r_speeds_graphs_width;

	aprof_event_t *paused_events;
	int paused_events_count;
	int pause_requested;

	struct {
		int glyph_width, glyph_height;
		float scale;
	} font_metrics;

	r_speeds_metric_t metrics[MAX_SPEEDS_METRICS];
	int metrics_count;

	r_speeds_graph_t graphs[MAX_GRAPHS];
	int graphs_count;

	struct {
		int frame_time_us, cpu_time_us, cpu_wait_time_us, gpu_time_us;
		Metascope scopes[APROF_MAX_SCOPES];
		Metascope gpu_scopes[MAX_GPU_SCOPES];
		char message[MAX_SPEEDS_MESSAGE];

		r_speeds_mprint_mode_t metrics_print_mode;
		string metrics_print_filter;
	} frame;

	// Mask g_speeds_graphs cvar writes
	char graphs_list[1024];
} g_speeds;

static void speedsStrcat( const char *msg ) {
	Q_strncat( g_speeds.frame.message, msg, sizeof( g_speeds.frame.message ));
}

static void speedsPrintf( const char *msg, ... ) FORMAT_CHECK(1);
static void speedsPrintf( const char *msg, ... ) {
	va_list argptr;
	char text[MAX_SPEEDS_MESSAGE];

	va_start( argptr, msg );
	Q_vsnprintf( text, sizeof( text ), msg, argptr );
	va_end( argptr );

	speedsStrcat(text);
}

static void metricTypeSnprintf(char *buf, int buf_size, int value, r_speeds_metric_type_t type) {
	switch (type) {
		case kSpeedsMetricCount:
			Q_snprintf( buf, buf_size, "%d", value );
			break;
		case kSpeedsMetricBytes:
			Q_strncpy( buf, Q_memprint( (float) value ), buf_size );
			break;
		case kSpeedsMetricMicroseconds: {
			const float msecs = value * 1e-3f; // us -> ms
			Q_snprintf( buf, buf_size, "%.03fms", msecs );
			break;
		}
		case kSpeedsMetricPermyriad: {
			const float percent = value * 1e-2f;
			Q_snprintf( buf, buf_size, "%.02f%%", percent );
			break;
		}
	}
}

static float linearstep(float min, float max, float v) {
	if (v <= min) return 0;
	if (v >= max) return 1;
	return (v - min) / (max - min);
}

// TODO better "random" colors for scope bars
static uint32_t getHash(const char *s) {
	dword crc;
	CRC32_Init(&crc);
	CRC32_ProcessBuffer(&crc, s, Q_strlen(s));
	return CRC32_Final(crc);
}

static void getColorForString( const char *s, rgba_t out_color ) {
	const uint32_t hash = getHash(s);
	out_color[0] = (hash >> 16) & 0xff;
	out_color[1] = (hash >> 8) & 0xff;
	out_color[2] = hash & 0xff;
}

static void drawTimeBar(uint64_t begin_time_ns, float time_scale_ms, int64_t begin_ns, int64_t end_ns, int y, int height, const char *label, const rgba_t color) {
	const float delta_ms = (end_ns - begin_ns) * 1e-6;
	const int width = delta_ms  * time_scale_ms;
	const int x = (begin_ns - begin_time_ns) * 1e-6 * time_scale_ms;

	rgba_t text_color = {191 + color[0]/4, 191 + color[1]/4, 191 + color[2]/4, 255};
	CL_FillRGBA(kRenderTransAdd, x, y, width, height, color[0], color[1], color[2], color[3]);

	// Tweak this if scope names escape the block boundaries
	char tmp[64];
	tmp[0] = '\0';
	const int glyph_width = g_speeds.font_metrics.glyph_width;
	const int box_capped_length = Q_min(sizeof(tmp), width / glyph_width);
	if (box_capped_length > 0) {
		Q_snprintf(tmp, box_capped_length, "%s %.3fms", label, delta_ms);
		gEngine.Con_DrawString(x, y, tmp, text_color);
	}
}

static void updateMetascope(Metascope *metascope, const char* prefix, const aprof_scope_t* scope, uint64_t delta_ns) {
	if (!metascope->initialized) {
		const qboolean reset = true;
		R_SpeedsRegisterMetric(&metascope->time_us, prefix, scope->name, kSpeedsMetricMicroseconds, reset,
			scope->name, scope->source_file, scope->source_line);
		metascope->initialized = 1;
	}

	metascope->time_us += delta_ns / 1000;
}

typedef struct {
	int draw;
	const aprof_event_t *events;
	uint64_t begin_time;
	float time_scale_ms;
	uint32_t begin;
	uint32_t end;
	int y;
	Metascope *scopes;
	const aprof_scope_t *aprof_scopes;
	VIEW_DECLARE_CONST(aprof_counter_desc_t, aprof_counters);
	const char *scope_name_prefix;
	int *out_active_time_us;
	int *out_wait_time_us;
} ProcessAndDrawAprofEvents;

static void processAndDrawAprofEvents(ProcessAndDrawAprofEvents args) {
#define MAX_STACK_DEPTH 8
	struct StackFrame {
		int scope_id;
		uint64_t begin_ns;
		uint64_t latest_child_end_ns;
		int overlaps;
	} stack[MAX_STACK_DEPTH];
	int depth = 0;
	int max_depth = 0;

	const int bar_height = g_speeds.font_metrics.glyph_height;

	int under_waiting = 0;
	uint64_t active_time_ns = 0;
	uint64_t wait_time_ns = 0;

	for (uint32_t begin = args.begin; begin != args.end; begin = (begin + 1) % APROF_EVENT_BUFFER_SIZE) {
		const aprof_event_t event = args.events[begin];
		const int event_type = APROF_EVENT_TYPE(event);
		const uint64_t timestamp_ns = APROF_EVENT_TIMESTAMP(event);
		const int scope_id = APROF_EVENT_SCOPE_ID(event);
		switch (event_type) {
			case APROF_EVENT_FRAME_BOUNDARY:
				active_time_ns = 0;
				wait_time_ns = 0;
				under_waiting = 0;
				break;

			case APROF_EVENT_SCOPE_BEGIN: {
					if (depth < MAX_STACK_DEPTH) {
						stack[depth] = (struct StackFrame) {
							.scope_id = scope_id,
							.begin_ns = timestamp_ns,
							.latest_child_end_ns = timestamp_ns,
							.overlaps = 0,
						};
					}
					++depth;
					if (max_depth < depth)
						max_depth = depth;

					const aprof_scope_t *const scope = args.aprof_scopes + scope_id;
					if (scope->flags & APROF_SCOPE_FLAG_WAIT)
						under_waiting++;

					break;
				}

			case APROF_EVENT_SCOPE_END: {
					ASSERT(depth > 0);
					--depth;

					ASSERT(scope_id >= 0);
					ASSERT(scope_id < APROF_MAX_SCOPES);

					struct StackFrame *const stack_frame = stack + depth;
					struct StackFrame *const parent_frame = depth > 0 ? stack + depth - 1 : NULL;

					if (stack_frame->scope_id != scope_id) {
						ERR("scope_id mismatch at stack depth=%d: found %d(%s), expected %d(%s)",
							depth,
							scope_id, args.aprof_scopes[scope_id].name,
							stack_frame->scope_id, args.aprof_scopes[stack_frame->scope_id].name);

						ERR("Full stack:");
						for (int i = depth; i >= 0; --i) {
							ERR("  %d: scope_id=%d(%s)", i,
								stack[i].scope_id, args.aprof_scopes[stack[i].scope_id].name);
						}

						return;
					}

					const aprof_scope_t *const scope = args.aprof_scopes + scope_id;
					const uint64_t delta_ns = timestamp_ns - stack_frame->begin_ns;

					updateMetascope(args.scopes + scope_id, args.scope_name_prefix, scope, delta_ns);

					// This is a top level scope that should be counted towards active usage
					const int is_top_level = ((scope->flags & APROF_SCOPE_FLAG_DECOR) == 0)
						&& (depth == 0 || (args.aprof_scopes[stack[depth-1].scope_id].flags & APROF_SCOPE_FLAG_DECOR));

					const int64_t latest_end_delta_ns = parent_frame
						? (int64_t)parent_frame->latest_child_end_ns - (int64_t)stack_frame->begin_ns
						: 0ll;
					const int overlap_pixels = (float)(latest_end_delta_ns / 1000000) * args.time_scale_ms;
					const int OVERLAP_PIXELS_THRESHOLD = 2;
					const int overlaps_with_siblings = overlap_pixels > OVERLAP_PIXELS_THRESHOLD;

					int y_overlap_offset = 0;
					if (overlaps_with_siblings) {
						parent_frame->overlaps += 1;
						y_overlap_offset = parent_frame->overlaps * bar_height / 2;
					} else if (parent_frame) {
						parent_frame->overlaps = 0;
					}

					// Updated parent's latest child end timestamp
					if (parent_frame) {
						parent_frame->latest_child_end_ns = Q_max(parent_frame->latest_child_end_ns, timestamp_ns);
					}

					// Only count top level scopes towards active time, and only if it's not waiting
					if (is_top_level && under_waiting == 0)
						active_time_ns += delta_ns;

					// If this is a top level waiting scope (under any depth)
					if (under_waiting == 1) {
						// Count it towards waiting time
						wait_time_ns += delta_ns;

						// If this is not a top level scope, then we might count its top level parent
						// towards cpu usage time, which is not correct. Subtract this waiting time from it.
						if (!is_top_level)
							active_time_ns -= delta_ns;
					}

					if (scope->flags & APROF_SCOPE_FLAG_WAIT)
						under_waiting--;

					if (args.draw) {
						rgba_t color = {0, 0, 0, 127};
						getColorForString(scope->name, color);
						drawTimeBar(args.begin_time, args.time_scale_ms, stack_frame->begin_ns, timestamp_ns,
							args.y + y_overlap_offset + depth * bar_height, bar_height, scope->name, color);
					}
					break;
				}

			case APROF_EVENT_COUNTER: {
					struct StackFrame *const frame = depth > 0 ? stack + depth - 1 : NULL;
					if (!frame || !args.aprof_counters.items)
						break;

					const uint32_t counter_index = APROF_EVENT_COUNTER_INDEX(event);
					const uint64_t counter_value = APROF_EVENT_COUNTER_VALUE(event);

					ASSERT(counter_index < args.aprof_counters.count);

					if (counter_index >= MAX_COUNTERS_PER_SCOPE) {
						// TODO throttled error
						break;
					}

					const aprof_counter_desc_t *const counter_desc = &args.aprof_counters.items[counter_index];
					Metascope *const metascope = &args.scopes[frame->scope_id];
					Metacounter *const metacounter = &metascope->counters[counter_index];

					metacounter->value = counter_value;

					r_speeds_metric_type_t metric = kSpeedsMetricCount;
					switch (counter_desc->unit) {
						case AprofCounterUnit_Bytes:
							metric = kSpeedsMetricBytes;
							break;
						case AprofCounterUnit_Nanoseconds:
							metacounter->value = counter_value / 1000;
							metric = kSpeedsMetricMicroseconds;
							break;
						case AprofCounterUnit_Permyriad:
							metric = kSpeedsMetricPermyriad;
							break;
						case AprofCounterUnit_Generic:
							break;
					}

					if (!metacounter->initialized) {
						const qboolean reset = true;
						R_SpeedsRegisterMetric(&metacounter->value,
							args.aprof_scopes[frame->scope_id].name, counter_desc->name,
							metric, reset, counter_desc->name, __FILE__, __LINE__);
						metacounter->initialized = 1;
					}

					// gEngine.Con_Reportf("%s.%s (%d) = %llu\n",
					// 	args.aprof_scopes[frame->scope_id].name,
					// 	args.aprof_counters.items[counter_index].name,
					// 	args.aprof_counters.items[counter_index].unit,
					// 	(unsigned long long)counter_value);

					break;
				}


			default:
				break;
		}
	}

	if (args.out_wait_time_us)
		*args.out_wait_time_us += wait_time_ns / 1000;

	if (args.out_active_time_us)
		*args.out_active_time_us += active_time_ns / 1000;

	if (max_depth > MAX_STACK_DEPTH)
		gEngine.Con_NPrintf(4, S_ERROR "Profiler stack overflow: reached %d, max available %d\n", max_depth, MAX_STACK_DEPTH);
}

static void handlePause( uint32_t prev_frame_index ) {
	if (!g_speeds.pause_requested || g_speeds.paused_events)
		return;

	const uint32_t frame_begin = prev_frame_index;
	const uint32_t frame_end = g_aprof.events_last_frame + 1;

	g_speeds.paused_events_count = frame_end >= frame_begin ? frame_end - frame_begin : (frame_end + APROF_EVENT_BUFFER_SIZE - frame_begin);
	g_speeds.paused_events = Mem_Malloc(vk_core.pool, g_speeds.paused_events_count * sizeof(g_speeds.paused_events[0]));

	if (frame_end >= frame_begin) {
		memcpy(g_speeds.paused_events, g_aprof.events + frame_begin, g_speeds.paused_events_count * sizeof(g_speeds.paused_events[0]));
	} else {
		const int first_chunk = (APROF_EVENT_BUFFER_SIZE - frame_begin) * sizeof(g_speeds.paused_events[0]);
		memcpy(g_speeds.paused_events, g_aprof.events + frame_begin, first_chunk);
		memcpy(g_speeds.paused_events + first_chunk, g_aprof.events, frame_end * sizeof(g_speeds.paused_events[0]));
	}
}

static int findMetricIndexByName( const_string_view_t name) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		if (svCmp(name, g_speeds.metrics[i].name) == 0)
			return i;
	}
	return -1;
}

static int findMetricIndexByIndexOrName( const_string_view_t name) {
	// try to read it as metric index first
	const SVParseLongResult parsed = svParseLong(name);
	if (parsed.chars_converted == name.len) {
		if (parsed.value < 0 || parsed.value > g_speeds.metrics_count) {
			return -1;
		}
		return parsed.value;
	}

	return findMetricIndexByName(name);
}

static int findGraphIndexByName( const_string_view_t name) {
	// TODO also delete by index. But need to have the active graph list first
	for (int i = 0; i < g_speeds.graphs_count; ++i) {
		if (svCmp(name, g_speeds.graphs[i].name) == 0)
			return i;
	}

	return -1;
}

static int drawGraph( r_speeds_graph_t *const graph, int frame_bar_y ) {
	const int min_width = 100 * g_speeds.font_metrics.scale;
	const int graph_width = clampi32(
		g_speeds.r_speeds_graphs_width->value < 1
		? vk_frame.width / 2 // half frame width if invalid
		: (int)(g_speeds.r_speeds_graphs_width->value * g_speeds.font_metrics.scale), // scaled value if valid
		min_width, vk_frame.width); // clamp to min_width..frame_width
	const int graph_height = graph->height * g_speeds.font_metrics.scale;

	if (graph->source_metric < 0) {
		// Check whether this metric has been registered
		const int metric_index = findMetricIndexByName((const_string_view_t){graph->name, Q_strlen(graph->name)});

		if (metric_index >= 0) {
			graph->source_metric = metric_index;
			g_speeds.metrics[metric_index].graph_index = graph - g_speeds.graphs;
		} else {
			const char *name = graph->name;
			rgba_t text_color = {0xff, 0x00, 0x00, 0xff};
			gEngine.Con_DrawString(0, frame_bar_y, name, text_color);
			frame_bar_y += g_speeds.font_metrics.glyph_height;
			return frame_bar_y;
		}
	}

	const r_speeds_metric_t *const metric = g_speeds.metrics + graph->source_metric;
	const int graph_max_value = metric->max_value ? Q_max(metric->max_value, graph->max_value) : graph->max_value;

	const float width_factor = (float)graph_width / graph->data_count;
	const float height_scale = (float)graph_height / graph_max_value;

	rgba_t text_color = {0xed, 0x9f, 0x01, 0xff};

	// Draw graph name
	const char *name = metric->name;
	gEngine.Con_DrawString(0, frame_bar_y, name, text_color);
	frame_bar_y += g_speeds.font_metrics.glyph_height;

	// Draw background that podcherkivaet graph area
	CL_FillRGBA(kRenderTransColor, 0, frame_bar_y, graph_width, graph_height, graph->color[0], graph->color[1], graph->color[2], 32);

	// Draw max value
	{
		char buf[16];
		metricTypeSnprintf(buf, sizeof(buf), graph_max_value, metric->type);
		gEngine.Con_DrawString(0, frame_bar_y, buf, text_color);
	}

	// Draw zero
	gEngine.Con_DrawString(0, frame_bar_y + graph->height - g_speeds.font_metrics.glyph_height, "0", text_color);
	frame_bar_y += graph_height;

	if (metric->low_watermark && metric->low_watermark < graph_max_value) {
		const int y = frame_bar_y - metric->low_watermark * height_scale;
		CL_FillRGBA(kRenderTransAdd, 0, y, graph_width, 1, 0, 255, 0, 50);
	}

	if (metric->high_watermark && metric->high_watermark < graph_max_value) {
		const int y = frame_bar_y - metric->high_watermark * height_scale;
		CL_FillRGBA(kRenderTransAdd, 0, y, graph_width, 1, 255, 0, 0, 50);
	}

	// Invert graph origin for better math below
	int max_value = INT_MIN;
	const qboolean watermarks = metric->low_watermark && metric->high_watermark;
	for (int i = 0; i < graph->data_count; ++i) {
		const int raw_value = Q_max(0, graph->data[(graph->data_write + i) % graph->data_count]);
		max_value = Q_max(max_value, raw_value);
		const int value = Q_min(graph_max_value, raw_value);

		int red = 0xed, green = 0x9f, blue = 0x01;
		if (watermarks) {
			// E.g: > 60 fps => 0, 30..60 fps -> 1..0, <30fps => 1
			const float k = linearstep(metric->low_watermark, metric->high_watermark, value);
			red = value < metric->low_watermark ? 0 : 255;
			green = 255 * (1 - k);
		}

		const int x0 = (float)i * width_factor;
		const int x1 = (float)(i+1) * width_factor;
		const int y_pos = value * height_scale;
		const int height = watermarks ? y_pos : 2 * g_speeds.font_metrics.scale;
		const int y = frame_bar_y - y_pos;

		// TODO lines
		CL_FillRGBA(kRenderTransAdd, x0, y, x1-x0, height, red, green, blue, 127);

		if (i == graph->data_count - 1) {
			char buf[16];
			metricTypeSnprintf(buf, sizeof(buf), raw_value, metric->type);
			gEngine.Con_DrawString(x1, y - g_speeds.font_metrics.glyph_height / 2, buf, text_color);
		}
	}

	graph->max_value = max_value ? max_value : 1;

	return frame_bar_y;
}

static int analyzeScopesAndDrawFrames( int draw, uint32_t prev_frame_index, int y, const VCombufProfilingResult *gpurofls, int gpurofls_count) {
	// Draw latest 2 frames; find their boundaries
	uint32_t rewind_frame = prev_frame_index;
	const int max_frames_to_draw = 2;
	for (int frame = 1; frame < max_frames_to_draw;) {
		rewind_frame = (rewind_frame - 1) % APROF_EVENT_BUFFER_SIZE; // NOTE: only correct for power-of-2 buffer sizes
		const aprof_event_t event = g_aprof.events[rewind_frame];

		// Exhausted all events
		if (event == 0 || rewind_frame == g_aprof.events_write)
			break;

		// Note the frame
		if (APROF_EVENT_TYPE(event) == APROF_EVENT_FRAME_BOUNDARY) {
			++frame;
			prev_frame_index = rewind_frame;
		}
	}

	const aprof_event_t *const events = g_speeds.paused_events ? g_speeds.paused_events : g_aprof.events;
	const int event_begin = g_speeds.paused_events ? 0 : prev_frame_index;
	const int event_end = g_speeds.paused_events ? g_speeds.paused_events_count - 1 : g_aprof.events_last_frame;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(events[event_begin]);
	const uint64_t frame_end_time = APROF_EVENT_TIMESTAMP(events[event_end]);
	const uint64_t delta_ns = frame_end_time - frame_begin_time;
	const float time_scale_ms = (double)vk_frame.width / (delta_ns / 1e6);

	// TODO? manage y based on depths
	processAndDrawAprofEvents((ProcessAndDrawAprofEvents){
		.draw = draw,
		.events = events,
		.begin_time = frame_begin_time,
		.time_scale_ms = time_scale_ms,
		.begin = event_begin,
		.end = event_end,
		.y = y,
		.scopes = g_speeds.frame.scopes,
		.aprof_scopes = g_aprof.scopes,
		.scope_name_prefix = "scope",
		.out_active_time_us = &g_speeds.frame.cpu_time_us,
		.out_wait_time_us = &g_speeds.frame.cpu_wait_time_us,
	});

	for (int i = 0; i < gpurofls_count; ++i) {
		const VCombufProfilingResult *const gpurofl = &gpurofls[i];

		y += g_speeds.font_metrics.glyph_height * (MAX_STACK_DEPTH + 1);

		processAndDrawAprofEvents((ProcessAndDrawAprofEvents){
			.draw = draw,
			.events = gpurofl->events.items,
			.begin_time = frame_begin_time,
			.time_scale_ms = time_scale_ms,
			.begin = 0,
			.end = gpurofl->events.count,
			.y = y,
			.scopes = g_speeds.frame.gpu_scopes,
			.aprof_scopes = gpurofl->scopes.items,
			.aprof_counters = {
				.items = gpurofl->counters.items,
				.count = gpurofl->counters.count,
			},
			.scope_name_prefix = "gpuscope",
			.out_active_time_us = NULL, // GPU time is handled elsewhere for now
			.out_wait_time_us = NULL,
		});
	}

	return y + g_speeds.font_metrics.glyph_height * 6;
}

static void printMetrics( void ) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		const r_speeds_metric_t *const metric = g_speeds.metrics + i;
		if (Q_strncmp(metric->name, "speeds", 6) != 0)
			continue;

		char buf[32];
		metricTypeSnprintf(buf, sizeof(buf), (*metric->p_value), metric->type);
		speedsPrintf("%s: %s\n", metric->name, buf);
	}
}

static void resetMetrics( void ) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		const r_speeds_metric_t *const metric = g_speeds.metrics + i;
		if (metric->reset)
			*metric->p_value = 0;
	}
}

static void getCurrentFontMetrics(void) {
	// hidpi scaling
	float scale = gEngine.pfnGetCvarFloat("con_fontscale");
	if (scale <= 0.f)
		scale = 1.f;

	// TODO these numbers are mostly fine for the "default" font. Unfortunately
	// we don't have any access to real font metrics from here, ref_api_t doesn't give us anything about fonts. ;_;
	g_speeds.font_metrics.glyph_width = 7 * scale;
	g_speeds.font_metrics.glyph_height = 20 * scale;
	g_speeds.font_metrics.scale = scale;
}

static int drawGraphs( int y ) {
	for (int i = 0; i < g_speeds.graphs_count; ++i) {
		r_speeds_graph_t *const graph = g_speeds.graphs + i;

		if (graph->source_metric >= 0)
			graph->data[graph->data_write] = *g_speeds.metrics[graph->source_metric].p_value;

		graph->data_write = (graph->data_write + 1) % graph->data_count;
		y = drawGraph(graph, y) + 10;
	}

	return y;
}

static void togglePause( void ) {
	if (g_speeds.paused_events) {
		Mem_Free(g_speeds.paused_events);
		g_speeds.paused_events = NULL;
		g_speeds.paused_events_count = 0;
		g_speeds.pause_requested = 0;
	} else {
		g_speeds.pause_requested = 1;
	}
}

static void speedsGraphAdd(const_string_view_t name, int metric_index) {
	INFO("Adding profiler graph for metric %.*s(%d) at graph index %d", name.len, name.s, metric_index, g_speeds.graphs_count);

	if (g_speeds.graphs_count == MAX_GRAPHS) {
		ERR("Cannot add graph \"%.*s\", no free graphs slots (max=%d)", name.len, name.s, MAX_GRAPHS);
		return;
	}

	if (metric_index >= 0) {
		r_speeds_metric_t *const metric = g_speeds.metrics + metric_index;
		metric->graph_index = g_speeds.graphs_count;
	}

	r_speeds_graph_t *const graph = g_speeds.graphs + g_speeds.graphs_count++;

	// TODO make these customizable
	graph->data_count = 256;
	graph->height = 100;
	graph->max_value = 1; // Will be computed automatically on first frame
	graph->color[3] = 255;

	const int len = Q_min(name.len, sizeof(graph->name) - 1);
	memcpy(graph->name, name.s, len);
	graph->name[len] = '\0';
	getColorForString(graph->name, graph->color);

	ASSERT(!graph->data);
	graph->data = Mem_Calloc(vk_core.pool, graph->data_count * sizeof(float));
	graph->data_write = 0;
	graph->source_metric = metric_index;
}

static void speedsGraphAddByMetricName( const_string_view_t name ) {
	const int metric_index = findMetricIndexByIndexOrName(name);
	if (metric_index < 0) {
		ERR("Metric \"%.*s\" not found", name.len, name.s);
		return;
	}

	r_speeds_metric_t *const metric = g_speeds.metrics + metric_index;
	if (metric->graph_index >= 0) {
		WARN("Metric \"%.*s\" already has graph @%d", name.len, name.s, metric->graph_index);
		return;
	}

	speedsGraphAdd( svFromNullTerminated(metric->name), metric_index );
}

static void speedsGraphDelete( r_speeds_graph_t *graph ) {
	ASSERT(graph->data);
	Mem_Free(graph->data);
	graph->data = NULL;
	graph->name[0] = '\0';

	if (graph->source_metric >= 0) {
		ASSERT(graph->source_metric < g_speeds.metrics_count);
		r_speeds_metric_t *const metric = g_speeds.metrics + graph->source_metric;
		metric->graph_index = -1;
	}

	graph->source_metric = -1;
}

static void speedsGraphRemoveByName( const_string_view_t name ) {
	const int graph_index = findGraphIndexByName(name);
	if (graph_index < 0) {
		ERR("Graph \"%.*s\" not found", name.len, name.s);
		return;
	}

	r_speeds_graph_t *const graph = g_speeds.graphs + graph_index;
	speedsGraphDelete( graph );

	INFO("Removing profiler graph %.*s(%d) at graph index %d", name.len, name.s, graph->source_metric, graph_index);

	// Move all further graphs one slot back, also updating their indices
	for (int i = graph_index + 1; i < g_speeds.graphs_count; ++i) {
		r_speeds_graph_t *const dst = g_speeds.graphs + i - 1;
		const r_speeds_graph_t *const src = g_speeds.graphs + i;

		if (src->source_metric >= 0) {
			ASSERT(src->source_metric < g_speeds.metrics_count);
			g_speeds.metrics[src->source_metric].graph_index--;
		}

		memcpy(dst, src, sizeof(r_speeds_graph_t));
	}

	g_speeds.graphs_count--;
}

static void speedsGraphsRemoveAll( void ) {
	INFO("Removing all %d profiler graphs", g_speeds.graphs_count);
	for (int i = 0; i < g_speeds.graphs_count; ++i) {
		r_speeds_graph_t *const graph = g_speeds.graphs + i;
		speedsGraphDelete(graph);
	}

	g_speeds.graphs_count = 0;
}

static void processGraphCvar( void ) {
	if (!(g_speeds.r_speeds_graphs->flags & FCVAR_CHANGED))
		return;

	if (0 == Q_strcmp(g_speeds.r_speeds_graphs->string, g_speeds.graphs_list))
		return;

	// TODO only remove graphs that are not present in the new list
	speedsGraphsRemoveAll();

	const char *p = g_speeds.r_speeds_graphs->string;
	while (*p) {
		const char *next = Q_strchrnul(p, ',');
		const const_string_view_t name = {p, next - p};

		const int metric_index = findMetricIndexByName(name);
		if (metric_index < 0) {
			WARN("Metric \"%.*s\" not found (yet? can be registered later)", name.len, name.s);
		}

		speedsGraphAdd( name, metric_index );
		if (!*next)
			break;
		p = next + 1;
	}

	g_speeds.r_speeds_graphs->flags &= ~FCVAR_CHANGED;
}

/*
static const char *getMetricTypeName(r_speeds_metric_type_t type) {
	switch (type) {
		case kSpeedsMetricCount: return "count";
		case kSpeedsMetricMicroseconds: return "ms";
		case kSpeedsMetricBytes: return "bytes";
	}

	return "UNKNOWN";
}
*/

// Actually does the job of `r_speeds_mlist` and `r_speeds_mtable` commands.
// We can't just directly call this function from little command handler ones, because
// all the metrics calculations happen inside `R_SpeedsDisplayMore` function.
static void doPrintMetrics( void ) {
	if ( g_speeds.frame.metrics_print_mode == kSpeedsMprintNone )
		return;

	const char *header_format = NULL;
	const char *line_format = NULL;
	const char *row_format = NULL;
	char line[64];
	if ( g_speeds.frame.metrics_print_mode == kSpeedsMprintTable ) {
		// Note:
		// This table alignment method relies on monospace font
		// and will have its alignment completly broken without one.
		header_format = "  | %-3s | %-38s | %-10s | %-46s | %21s\n";
		line_format   = "  | %.3s | %.38s | %.10s | %.46s | %.21s\n";
		row_format    = "  | ^2%-3d^7 | ^2%-38s^7 | ^3%-10s^7 | ^5%-46s^7 | ^6%s:%d^7\n";

		size_t line_size = sizeof ( line );
		memset( line, '-', line_size - 1 );
		line[line_size - 1] = '\0';
	} else {
		header_format = " [%s] %s = %s  -->  (%s, %s)\n";
		line_format   = NULL;
		row_format    = " [^2%d^7] ^2%s^7 = ^3%s^7  -->  (^5%s^7, ^6%s:%d^7)\n";

		line[0] = '\0';
	}

	// Reset mode to print only this frame.
	g_speeds.frame.metrics_print_mode = kSpeedsMprintNone;

	gEngine.Con_Printf( header_format, "index", "module.metric_name", "value", "variable", "registration_location" );
	if ( line_format )  gEngine.Con_Printf( line_format, line, line, line, line, line );
	for ( int i = 0; i < g_speeds.metrics_count; ++i ) {
		const r_speeds_metric_t *metric = g_speeds.metrics + i;

		if ( g_speeds.frame.metrics_print_filter[0] && !Q_strstr( metric->name, g_speeds.frame.metrics_print_filter ) )
			continue;

		char value_with_unit[16];
		metricTypeSnprintf( value_with_unit, sizeof( value_with_unit ), *metric->p_value, metric->type );
		gEngine.Con_Printf( row_format, i, metric->name, value_with_unit, metric->var_name, COM_FileWithoutPath( metric->src_file ), metric->src_line );
	}
	if ( line_format )  gEngine.Con_Printf( line_format, line, line, line, line, line );
	gEngine.Con_Printf( header_format, "index", "module.metric_name", "value", "variable", "registration_location" );
}

// Handles optional filter argument for `r_speeds_mlist` and `r_speeds_mtable` commands.
static void handlePrintFilterArg( void ) {
	if ( gEngine.Cmd_Argc() > 1 ) {
		Q_strncpy( g_speeds.frame.metrics_print_filter, gEngine.Cmd_Argv( 1 ), sizeof( g_speeds.frame.metrics_print_filter ) );
	} else {
		g_speeds.frame.metrics_print_filter[0] = '\0';
	}
}

// Ideally, we'd just autocomplete the r_speeds_graphs cvar/cmd.
// However, autocompletion is not exposed to the renderer. It is completely internal to the engine, see con_utils.c, var cmd_list.
// -------
// Handles `r_speeds_mlist` command.
static void printMetricsList( void ) {
	handlePrintFilterArg();
	g_speeds.frame.metrics_print_mode = kSpeedsMprintList;
}

// Handles `r_speeds_mtable` command.
static void printMetricsTable( void ) {
	handlePrintFilterArg();
	g_speeds.frame.metrics_print_mode = kSpeedsMprintTable;
}

static void graphCmd( void ) {
	enum { Unknown, Add, Remove, Clear } action = Unknown;

	const int argc = gEngine.Cmd_Argc();

	if (argc > 1) {
		const char *const cmd = gEngine.Cmd_Argv(1);
		if (0 == Q_strcmp("add", cmd) && argc > 2)
			action = Add;
		else if (0 == Q_strcmp("del", cmd) && argc > 2)
			action = Remove;
		else if (0 == Q_strcmp("clear", cmd))
			action = Clear;
	}


	switch (action) {
		case Add:
			for (int i = 2; i < argc; ++i) {
				const char *const arg = gEngine.Cmd_Argv(i);
				const const_string_view_t name = {arg, Q_strlen(arg) };
				speedsGraphAddByMetricName( name );
			}
			break;
		case Remove:
			for (int i = 2; i < argc; ++i) {
				const char *const arg = gEngine.Cmd_Argv(i);
				const const_string_view_t name = {arg, Q_strlen(arg) };
				speedsGraphRemoveByName( name );
			}
			break;
		case Clear:
			speedsGraphsRemoveAll();
			break;
		case Unknown:
			INFO("Usage:\n%s <add/del> metric0 metric1 ...", gEngine.Cmd_Argv(0));
			INFO("\t%s <add/del> metric0 metric1 ...", gEngine.Cmd_Argv(0));
			INFO("\t%s clear", gEngine.Cmd_Argv(0));
			return;
	}

	// update cvar
	{
		const int len = sizeof(g_speeds.graphs_list) - 1;
		char *const buf = g_speeds.graphs_list;

		buf[0] = '\0';
		int off = 0;
		for (int i = 0; i < g_speeds.graphs_count; ++i) {
			const r_speeds_graph_t *const graph = g_speeds.graphs + i;

			if (off)
				buf[off++] = ',';

			//gEngine.Con_Reportf("buf='%s' off=%d %s(%d)\n", buf, off, graph->name, (int)Q_strlen(graph->name));

			const char *s = graph->name;
			while (off < len && *s)
				buf[off++] = *s++;

			buf[off] = '\0';

			if (off >= len - 1)
				break;
		}

		gEngine.Cvar_Set("r_speeds_graphs", buf);
	}
}

void R_SpeedsInit( void ) {
	g_speeds.r_speeds_graphs = gEngine.Cvar_Get("r_speeds_graphs", "", FCVAR_GLCONFIG, "List of metrics to plot as graphs, separated by commas");
	g_speeds.r_speeds_graphs_width = gEngine.Cvar_Get("r_speeds_graphs_width", "", FCVAR_GLCONFIG, "Graphs width in pixels");

	gEngine.Cmd_AddCommand("r_speeds_toggle_pause", togglePause, "Toggle frame profiler pause");
	gEngine.Cmd_AddCommand("r_speeds_mlist", printMetricsList, "Print all registered metrics as a list");
	gEngine.Cmd_AddCommand("r_speeds_mtable", printMetricsTable, "Print all registered metrics as a table");
	gEngine.Cmd_AddCommand("r_speeds_graph", graphCmd, "Manipulate add/remove metrics graphs");

	R_SPEEDS_COUNTER(g_speeds.frame.frame_time_us, "frame", kSpeedsMetricMicroseconds);
	R_SPEEDS_COUNTER(g_speeds.frame.cpu_time_us, "cpu", kSpeedsMetricMicroseconds);
	R_SPEEDS_COUNTER(g_speeds.frame.cpu_wait_time_us, "cpu_wait", kSpeedsMetricMicroseconds);
	R_SPEEDS_COUNTER(g_speeds.frame.gpu_time_us, "gpu", kSpeedsMetricMicroseconds);
}

// grab r_speeds message
qboolean R_SpeedsMessage( char *out, size_t size )
{
	if( gEngine.drawFuncs->R_SpeedsMessage != NULL )
	{
		if( gEngine.drawFuncs->R_SpeedsMessage( out, size ))
			return true;
		// otherwise pass to default handler
	}

	if( r_speeds->value <= 0 ) return false;
	if( !out || !size ) return false;

	Q_strncpy( out, g_speeds.frame.message, size );

	return true;
}

void R_SpeedsRegisterMetric(int* p_value, const char *module, const char *name, r_speeds_metric_type_t type, qboolean reset, const char *var_name, const char *file, int line) {
	ASSERT(g_speeds.metrics_count < MAX_SPEEDS_METRICS);

	r_speeds_metric_t *metric = g_speeds.metrics + (g_speeds.metrics_count++);
	metric->p_value = p_value;
	metric->reset = reset;

	Q_snprintf(metric->name, sizeof(metric->name), "%s.%s", module, name);

	metric->type = type;
	metric->src_file = file;
	metric->src_line = line;
	metric->var_name = var_name;
	metric->graph_index = -1;

	// TODO how to make universally adjustable?
	if (Q_strcmp("frame", name) == 0) {
		metric->low_watermark = TARGET_FRAME_TIME * 1000;
		metric->high_watermark = TARGET_FRAME_TIME * 2000;
		metric->max_value = TARGET_FRAME_TIME * 3000;
	} else {
		metric->low_watermark = metric->high_watermark = metric->max_value;
	}
}

void R_SpeedsDisplayMore(uint32_t prev_frame_index, const struct VCombufProfilingResult *gpurofl, int gpurofl_count) {
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);

	// Reads current font/DPI scale, many functions below use it
	getCurrentFontMetrics();

	g_speeds.frame.message[0] = '\0';

	const uint32_t speeds_bits = r_speeds->value;

	if (speeds_bits) {
		speedsPrintf( "Renderer: ^1Vulkan%s^7\n", vk_frame.rtx_enabled ? " RT" : "" );
		int color_index = 7; // default color
		switch (v_device_info.properties.vendorID) {
			case 0x1002: /* AMD */ color_index = 1; break;
			case 0x10DE: /* NVIDIA */ color_index = 2; break;
			case 0x8086: /* INTEL */ color_index = 4; break;
		}
		speedsPrintf( "^%d%s^7\n", color_index, v_device_info.properties.deviceName);
		speedsPrintf( "Driver: %u.%u.%u, Vulkan: %u.%u.%u\n",
			XVK_PARSE_VERSION(v_device_info.properties.driverVersion),
			XVK_PARSE_VERSION(v_device_info.properties.apiVersion));
		speedsPrintf( "Resolution: %ux%u\n", vk_frame.width, vk_frame.height);
	}

	const uint32_t events = g_aprof.events_last_frame - prev_frame_index;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(g_aprof.events[prev_frame_index]);
	const unsigned long long delta_ns = APROF_EVENT_TIMESTAMP(g_aprof.events[g_aprof.events_last_frame]) - frame_begin_time;

	g_speeds.frame.frame_time_us = delta_ns / 1000;

	{
		// TODO this is not strictly correct, just and approximation
		// E.g. it won't give correct result for multiple combufs with gaps between them.
		uint64_t gpu_frame_begin_ns = UINT64_MAX, gpu_frame_end_ns = 0;
		for (int i = 0; i < gpurofl_count; ++i) {
			gpu_frame_begin_ns = Q_min(gpu_frame_begin_ns, gpurofl[i].begin_ns);
			gpu_frame_end_ns = Q_max(gpu_frame_end_ns, gpurofl[i].end_ns);
		}
		g_speeds.frame.gpu_time_us = (gpu_frame_end_ns - gpu_frame_begin_ns) / 1000;
	}

	handlePause( prev_frame_index );

	if (speeds_bits != 0)
	{
		int y = 100;
		const int draw_frame = speeds_bits & SPEEDS_BIT_FRAME;
		y = analyzeScopesAndDrawFrames( draw_frame, prev_frame_index, y, gpurofl, gpurofl_count );

		const int draw_graphs = speeds_bits & SPEEDS_BIT_GRAPHS;
		if (draw_graphs)
			y = drawGraphs(y + 10);
	}

	if (speeds_bits & SPEEDS_BIT_SIMPLE) {
		speedsPrintf("frame: %.03fms GPU: %.03fms\n", g_speeds.frame.frame_time_us * 1e-3f, g_speeds.frame.gpu_time_us * 1e-3);
		speedsPrintf("  (ref) CPU: %.03fms wait: %.03fms\n", g_speeds.frame.cpu_time_us * 1e-3, g_speeds.frame.cpu_wait_time_us * 1e-3);
	}

	if (speeds_bits & SPEEDS_BIT_STATS) {
		speedsPrintf("profiler events: %u, wraps: %d\n", events, g_aprof.current_frame_wraparounds);
		printMetrics();
	}

	processGraphCvar();

	doPrintMetrics();

	resetMetrics();

	APROF_SCOPE_END(function);
}
