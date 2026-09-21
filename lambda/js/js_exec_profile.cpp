#include "js_exec_profile.h"
#include "../lambda-data.hpp"
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
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
static bool g_js_opt_trace_dumped = false;

struct JsOptNamedFastNoEntryCounter {
    char* name;
    uint32_t length;
    uint64_t count;
};

static HashMap* g_js_opt_named_fast_no_entry = NULL;

static uint64_t js_opt_named_fast_no_entry_hash(const void* item,
        uint64_t seed0, uint64_t seed1) {
    const JsOptNamedFastNoEntryCounter* entry =
        (const JsOptNamedFastNoEntryCounter*)item;
    return entry && entry->name
        ? hashmap_hash_bytes(entry->name, entry->length, seed0, seed1) : 0;
}

static int js_opt_named_fast_no_entry_compare(const void* left,
        const void* right, void*) {
    const JsOptNamedFastNoEntryCounter* a =
        (const JsOptNamedFastNoEntryCounter*)left;
    const JsOptNamedFastNoEntryCounter* b =
        (const JsOptNamedFastNoEntryCounter*)right;
    if (!a || !b || !a->name || !b->name || a->length != b->length) return 1;
    return a->length == 0 || memcmp(a->name, b->name, a->length) == 0 ? 0 : 1;
}

static void js_opt_named_fast_no_entry_free(void* item) {
    JsOptNamedFastNoEntryCounter* entry =
        (JsOptNamedFastNoEntryCounter*)item;
    if (entry) mem_free(entry->name);
}

static void js_opt_trace_cleanup(void) {
    hashmap_free(g_js_opt_named_fast_no_entry);
    g_js_opt_named_fast_no_entry = NULL;
}

static const char* g_js_opt_event_names[JS_OPT_EVENT_COUNT] = {
    "scope_lookup_cache_hit", "scope_lookup_cache_miss",
    "regex_compile_cache_hit", "regex_compile_cache_miss",
    "regex_permanent_cache_hit", "regex_fresh_wrapper",
    "regex_keyless_reject", "regex_cache_invalidate",
    "regex_bulk_match_hit", "regex_bulk_match_fallback",
    "regex_bulk_replace_hit", "regex_bulk_replace_fallback",
    "array_set_fast_hit", "array_set_guard_fail",
    "array_own_element_get_hit", "array_own_element_get_fallback",
    "array_reduce_dense_element",
    "array_reduce_prerooted_args",
    "array_runtime_items_install",
    "array_runtime_items_release",
    "array_gc_items_alloc",
    "dynamic_function_fastpath", "dynamic_function_cache_hit",
    "dynamic_function_cache_miss", "mir_direct_destination",
    "mir_discard_elision", "mir_branch_direct", "mir_generic_fallback",
    "mir_box_value", "mir_unbox_value", "mir_root_store",
    "module_cache_hit", "module_cache_miss", "tla_deferred_body",
    "tla_drain", "uri_error_cache_hit", "uri_error_cache_miss",
    "named_fast_probe", "named_fast_hit", "named_fast_miss",
    "named_fast_string_length",
    "named_fast_data_descriptor",
    "named_fast_no_receiver_string",
    "named_fast_no_receiver_function",
    "named_fast_no_receiver_other",
    "named_fast_function_data",
    "runtime_number_head_hit", "runtime_number_head_fallback",
    "runtime_number_compare_head",
    "runtime_string_concat_head",
    "mir_number_admitted", "mir_number_fallback",
    "mir_native_index_admitted", "mir_native_index_fallback",
    "mir_dense_index_admitted", "mir_packed_strict_equal",
    "mir_loop_stable_name_id",
    "mir_light_call",
    "mir_light_direct_activation",
    "mir_this_call",
    "mir_this_direct_activation",
    "bound_call_forward_args",
    "mir_deferred_function_finalize",
    "mir_lazy_function_metadata",
    "ordinary_number_store",
    "typed_number_store",
    "typed_number_read",
    "own_enumerability_inspect",
    "mir_literal_field_admitted",
    "static_numeric_array_initializer",
    "static_object_initializer",
    "string_search_ascii", "string_search_unicode",
    "string_split_ascii", "string_split_unicode",
    "string_slice_ascii", "string_slice_unicode",
    "string_char_access_ascii", "string_char_access_unicode",
    "string_concat_ascii", "string_concat_unicode",
    "ascii_substring_cache_hit", "ascii_substring_cache_miss",
    "runtime_to_numeric_call",
    "runtime_increment_call",
    "runtime_decrement_call",
    "runtime_boxed_compare_call",
    "runtime_number_index_get_call",
    "runtime_number_index_numeric_array",
    "runtime_number_index_tagged_array",
    "runtime_number_index_tagged_packed_plain",
    "runtime_number_index_tagged_holey_plain",
    "runtime_number_index_tagged_with_props",
    "runtime_number_index_tagged_other",
    "runtime_number_index_typed_array",
    "runtime_number_index_other"
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

void js_opt_trace_named_fast_no_entry(const char* name, uint32_t length) {
    if (!js_opt_trace_is_enabled() || !name || length == 0) return;
    if (!g_js_opt_named_fast_no_entry) {
        g_js_opt_named_fast_no_entry = hashmap_new(
            sizeof(JsOptNamedFastNoEntryCounter), 16, 0, 0,
            js_opt_named_fast_no_entry_hash, js_opt_named_fast_no_entry_compare,
            js_opt_named_fast_no_entry_free, NULL);
        if (!g_js_opt_named_fast_no_entry) return;
    }
    JsOptNamedFastNoEntryCounter probe = {const_cast<char*>(name), length, 0};
    JsOptNamedFastNoEntryCounter* found =
        (JsOptNamedFastNoEntryCounter*)hashmap_get(g_js_opt_named_fast_no_entry,
            &probe);
    if (found) {
        if (found->count != UINT64_MAX) found->count++;
        return;
    }
    char* copied_name = mem_dup_n(name, length, MEM_CAT_JS_RUNTIME);
    if (!copied_name) return;
    JsOptNamedFastNoEntryCounter entry = {copied_name, length, 1};
    (void)hashmap_set(g_js_opt_named_fast_no_entry, &entry);
    if (hashmap_oom(g_js_opt_named_fast_no_entry)) mem_free(copied_name);
}

static void js_opt_trace_append_escaped_name(StrBuf* buf, const char* name,
        uint32_t length) {
    static const char* hex = "0123456789ABCDEF";
    if (!buf || !name) return;
    for (uint32_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch >= 0x20 && ch <= 0x7e && ch != '%' && ch != ',' &&
                ch != '=') {
            strbuf_append_char(buf, (char)ch);
            continue;
        }
        strbuf_append_char(buf, '%');
        strbuf_append_char(buf, hex[(ch >> 4) & 0x0f]);
        strbuf_append_char(buf, hex[ch & 0x0f]);
    }
}

static void js_opt_trace_append_named_fast_no_entry_details(StrBuf* buf) {
    if (!buf || !g_js_opt_named_fast_no_entry) return;
    enum { JS_OPT_TRACE_TOP_NAMED_FAST_NO_ENTRY = 16 };
    JsOptNamedFastNoEntryCounter* top[JS_OPT_TRACE_TOP_NAMED_FAST_NO_ENTRY] = {};
    size_t cursor = 0;
    void* raw = NULL;
    while (hashmap_iter(g_js_opt_named_fast_no_entry, &cursor, &raw)) {
        JsOptNamedFastNoEntryCounter* entry =
            (JsOptNamedFastNoEntryCounter*)raw;
        if (!entry || !entry->name || entry->count == 0) continue;
        for (int i = 0; i < JS_OPT_TRACE_TOP_NAMED_FAST_NO_ENTRY; i++) {
            if (top[i] && top[i]->count >= entry->count) continue;
            for (int j = JS_OPT_TRACE_TOP_NAMED_FAST_NO_ENTRY - 1; j > i; j--) {
                top[j] = top[j - 1];
            }
            top[i] = entry;
            break;
        }
    }
    strbuf_append_format(buf, "JS_OPT_TRACE_DETAILS schema=1 named_fast_no_entry_distinct=%llu top=",
        (unsigned long long)hashmap_count(g_js_opt_named_fast_no_entry));
    for (int i = 0; i < JS_OPT_TRACE_TOP_NAMED_FAST_NO_ENTRY && top[i]; i++) {
        if (i != 0) strbuf_append_char(buf, ',');
        strbuf_append_uint64(buf, top[i]->count);
        strbuf_append_char(buf, ':');
        js_opt_trace_append_escaped_name(buf, top[i]->name, top[i]->length);
    }
    strbuf_append_char(buf, '\n');
}

void js_opt_trace_dump(void) {
    if (!js_opt_trace_is_enabled() || g_js_opt_trace_dumped) return;
    create_dir_recursive("temp");
    char default_path[128];
    snprintf(default_path, sizeof(default_path), "temp/js_opt_trace_%ld.tsv",
        (long)js_profile_getpid());
    const char* out_path = getenv("JS_OPT_TRACE_OUT");
    if (!out_path || !out_path[0]) out_path = default_path;

    StrBuf* buf = strbuf_new();
    if (!buf) {
        js_opt_trace_cleanup();
        g_js_opt_trace_dumped = true;
        return;
    }
    // Keep the strict schema record last: legacy readers tokenize every line
    // after its `reasons=` field, while companion diagnostics are optional.
    js_opt_trace_append_named_fast_no_entry_details(buf);
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
        strbuf_append_all(buf, 2, i != 0 ? "," : "", g_js_opt_reason_names[i]);
        strbuf_append_format(buf, "=%llu",
            (unsigned long long)g_js_opt_trace_reason_counts[i]);
    }
    strbuf_append_char(buf, '\n');
    if (write_text_file_atomic(out_path, buf->str ? buf->str : "") != 0) {
        log_error("js-opt-trace: failed to write '%s'", out_path);
    }
    strbuf_free(buf);
    // The normal runner dumps before memtrack_shutdown().  Release copied
    // source spellings here so the diagnostic does not outlive that boundary;
    // the atexit hook is then an idempotent no-op.
    js_opt_trace_cleanup();
    g_js_opt_trace_dumped = true;
}

#endif // LAMBDA_JS_EXEC_PROFILE
