#include "css_counter_hook.h"

static CssCounterFormatFn g_counter_format = nullptr;
static CssCountersFormatFn g_counters_format = nullptr;

void css_counter_format_register(CssCounterFormatFn counter_fn,
                                 CssCountersFormatFn counters_fn) {
    g_counter_format = counter_fn;
    g_counters_format = counters_fn;
}

int css_counter_format(void* counter_context, const char* name, uint32_t style,
                       char* buffer, size_t buffer_size) {
    if (!g_counter_format) return 0;
    return g_counter_format(counter_context, name, style, buffer, buffer_size);
}

int css_counters_format(void* counter_context, const char* name,
                        const char* separator, uint32_t style,
                        char* buffer, size_t buffer_size) {
    if (!g_counters_format) return 0;
    return g_counters_format(counter_context, name, separator, style, buffer, buffer_size);
}
