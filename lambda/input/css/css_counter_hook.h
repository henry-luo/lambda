#pragma once

#include <stddef.h>
#include <stdint.h>

// CSS owns the call site; layout installs the formatter for its opaque counter
// state. This keeps parsed content independent of Radiant's CounterContext.
typedef int (*CssCounterFormatFn)(void* counter_context, const char* name,
                                  uint32_t style, char* buffer, size_t buffer_size);
typedef int (*CssCountersFormatFn)(void* counter_context, const char* name,
                                   const char* separator, uint32_t style,
                                   char* buffer, size_t buffer_size);

void css_counter_format_register(CssCounterFormatFn counter_fn,
                                 CssCountersFormatFn counters_fn);
int css_counter_format(void* counter_context, const char* name, uint32_t style,
                       char* buffer, size_t buffer_size);
int css_counters_format(void* counter_context, const char* name,
                        const char* separator, uint32_t style,
                        char* buffer, size_t buffer_size);
