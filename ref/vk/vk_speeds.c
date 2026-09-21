#include "vk_speeds.h"
#include "vulkan/VDevice.h"
#include "vulkan/VCombuf.h"
#include "vk_common.h"
#include "std/arrays.h"
#include "std/stringview.h"
#include "vk_logs.h"

#define MODULE_NAME "speeds"

static void listGpuPerfCounters(void) {
	vDevicePrintPerformanceCounters(&v_device_info);
}

static void enableGpuPerfCounters(void) {
	ARRAY_DYNAMIC_DECLARE(uint32_t, counters);
	arrayDynamicInitT(&counters);

	const int argc = gEngine.Cmd_Argc();
	for (int i = 1; i < argc; ++i) {
		const char *const arg = gEngine.Cmd_Argv(i);
		const const_string_view_t arg_sv = {arg, Q_strlen(arg) };
		const SVParseLongResult num = svParseLong(arg_sv);
		if (num.chars_converted != arg_sv.len) {
			ERR("Invalid perf query counter index \"%.*s\"", arg_sv.len, arg_sv.s);
			continue;
		}

		if (num.value < 0 || num.value >= v_device_info.perf_counters.count) {
			ERR("Perf query counter index %ld is out of bounds. Max %d", num.value, v_device_info.perf_counters.count);
			continue;
		}

		const uint32_t value = num.value;
		arrayDynamicAppendT(&counters, &value);
	}

	R_VkCombufPerfQueryEnable(counters.items, counters.count);
}

void VK_SpeedsInit(void) {
	gEngine.Cmd_AddCommand("vk_speeds_counters", listGpuPerfCounters, "List GPU performance query counters");
	gEngine.Cmd_AddCommand("vk_speeds_counters_enable", enableGpuPerfCounters, "Enable GPU performance query counters");

}
