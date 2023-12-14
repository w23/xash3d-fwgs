#include "vk_logs.h"
#include "vk_cvar.h"
#include "stringview.h"

uint32_t g_log_debug_bits = 0;

static const struct log_pair_t {
	const char *name;
	uint32_t bit;
} g_log_module_pairs[] = {
#define X(m) {LOG_NAME(m), LOG_BIT(m)},
	LIST_LOG_MODULES(X)
#undef X
};

void R_LogSetVerboseModules( const char *p ) {
	g_log_debug_bits = 0;
	while (*p) {
		const char *next = Q_strchrnul(p, ',');
		const const_string_view_t name = {p, next - p};
		uint32_t bit = 0;

		for (int i = 0; i < COUNTOF(g_log_module_pairs); ++i) {
			const struct log_pair_t *const pair = g_log_module_pairs + i;
			if (svCmp(name, pair->name) == 0) {
				gEngine.Con_Reportf("Enabling verbose logs for module \"%.*s\"\n", name.len, name.s);
				bit = pair->bit;
				break;
			}
		}

		if (!bit) {
			gEngine.Con_Reportf(S_ERROR "Unknown log module \"%.*s\"\n", name.len, name.s);
		}

		g_log_debug_bits |= bit;

		if (!*next)
			break;
		p = next + 1;
	}
}
