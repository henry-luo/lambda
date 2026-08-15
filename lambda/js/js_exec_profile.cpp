#include "js_exec_profile.h"
#include "../lambda-data.hpp"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define js_profile_getpid _getpid
#else
#include <unistd.h>
#define js_profile_getpid getpid
#endif

#ifdef LAMBDA_JS_EXEC_PROFILE

static JsOptTraceCounter g_js_opt_trace_events[JS_OPT_EVENT_COUNT] = {};
static uint64_t g_js_opt_trace_reason_counts[JS_OPT_REASON_COUNT] = {};
static int g_js_opt_trace_mode = -1;
static bool g_js_opt_trace_registered = false;

static const char* g_js_opt_event_names[JS_OPT_EVENT_COUNT] = {
    "scope_lookup_cache_hit", "scope_lookup_cache_miss",
    "regex_compile_cache_hit", "regex_compile_cache_miss",
    "regex_permanent_cache_hit", "regex_fresh_wrapper",
    "regex_keyless_reject", "regex_cache_invalidate",
    "array_set_fast_hit", "array_set_guard_fail",
    "dynamic_function_fastpath", "dynamic_function_cache_hit",
    "dynamic_function_cache_miss", "mir_direct_destination",
    "mir_discard_elision", "mir_branch_direct", "mir_generic_fallback",
    "mir_box_value", "mir_unbox_value", "mir_root_store",
    "module_cache_hit", "module_cache_miss", "tla_deferred_body",
    "tla_drain", "uri_error_cache_hit", "uri_error_cache_miss",
    "named_fast_probe", "named_fast_hit", "named_fast_miss"
};

static const char* g_js_opt_reason_names[JS_OPT_REASON_COUNT] = {
    "none", "hole_or_sparse", "prototype_accessor", "not_extensible",
    "length_not_writable", "capture_bearing_short_regex",
    "keyless_cache_entry", "shape_changed", "named_fast_no_key",
    "named_fast_host_dynamic", "named_fast_no_receiver", "named_fast_no_entry",
    "named_fast_attributes", "named_fast_bounds", "named_fast_reserved",
    "named_fast_deleted", "named_fast_value_type"
};

static int js_exec_profile_truthy(const char* value) {
    return value && value[0] && strcmp(value, "0") != 0 &&
        strcmp(value, "false") != 0 && strcmp(value, "off") != 0 &&
        strcmp(value, "no") != 0;
}

static void js_opt_trace_register(void) {
    if (!g_js_opt_trace_registered) {
        atexit(js_opt_trace_dump);
        g_js_opt_trace_registered = true;
    }
}

int js_opt_trace_is_enabled(void) {
    if (g_js_opt_trace_mode >= 0) return g_js_opt_trace_mode;
    g_js_opt_trace_mode = js_exec_profile_truthy(getenv("JS_OPT_TRACE")) ? 1 : 0;
    if (g_js_opt_trace_mode) js_opt_trace_register();
    return g_js_opt_trace_mode;
}

void js_opt_trace_record(JsOptEvent event, JsOptReason reason,
        JsOptTraceOutcome outcome) {
    if (!js_opt_trace_is_enabled() || event < 0 ||
            event >= JS_OPT_EVENT_COUNT) return;
    JsOptTraceCounter* counter = &g_js_opt_trace_events[event];
    counter->attempts++;
    switch (outcome) {
    case JS_OPT_OUTCOME_TAKEN: counter->taken++; break;
    case JS_OPT_OUTCOME_FALLBACK: counter->fallback++; break;
    }
    if (reason >= 0 && reason < JS_OPT_REASON_COUNT) {
        g_js_opt_trace_reason_counts[reason]++;
    }
}

void js_opt_trace_dump(void) {
    if (!js_opt_trace_is_enabled()) return;
    create_dir_recursive("temp");
    char default_path[128];
    snprintf(default_path, sizeof(default_path), "temp/js_opt_trace_%ld.tsv",
        (long)js_profile_getpid());
    const char* out_path = getenv("JS_OPT_TRACE_OUT");
    if (!out_path || !out_path[0]) out_path = default_path;

    StrBuf* buf = strbuf_new();
    if (!buf) return;
    strbuf_append_str(buf, "JS_OPT_TRACE schema=1 events=");
    for (int i = 0; i < JS_OPT_EVENT_COUNT; i++) {
        if (i != 0) strbuf_append_char(buf, ',');
        JsOptTraceCounter* counter = &g_js_opt_trace_events[i];
        strbuf_append_str(buf, g_js_opt_event_names[i]);
        strbuf_append_format(buf, "=%llu/%llu/%llu/%llu",
            (unsigned long long)counter->attempts,
            (unsigned long long)counter->taken,
            (unsigned long long)counter->fallback,
            (unsigned long long)counter->invalidated);
    }
    strbuf_append_str(buf, " reasons=");
    for (int i = 0; i < JS_OPT_REASON_COUNT; i++) {
        if (i != 0) strbuf_append_char(buf, ',');
        strbuf_append_str(buf, g_js_opt_reason_names[i]);
        strbuf_append_format(buf, "=%llu",
            (unsigned long long)g_js_opt_trace_reason_counts[i]);
    }
    strbuf_append_char(buf, '\n');
    if (write_text_file_atomic(out_path, buf->str ? buf->str : "") != 0) {
        log_error("js-opt-trace: failed to write '%s'", out_path);
    }
    strbuf_free(buf);
}

#endif // LAMBDA_JS_EXEC_PROFILE
