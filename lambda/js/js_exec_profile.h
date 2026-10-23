#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable optimization decisions consumed by focused JS contract tests. The
// broad execution profiler below remains the source for elapsed-time and
// aggregate optimization reports.
typedef enum JsOptEvent {
#define JS_OPT_EVENT(id, name) id,
#include "js_exec_profile_events.def"
#undef JS_OPT_EVENT
    JS_OPT_EVENT_COUNT
} JsOptEvent;

typedef enum JsOptReason {
    JS_OPT_REASON_NONE = 0,
    JS_OPT_REASON_HOLE_OR_SPARSE,
    JS_OPT_REASON_PROTOTYPE_ACCESSOR,
    JS_OPT_REASON_NOT_EXTENSIBLE,
    JS_OPT_REASON_LENGTH_NOT_WRITABLE,
    JS_OPT_REASON_CAPTURE_BEARING_SHORT_REGEX,
    JS_OPT_REASON_KEYLESS_CACHE_ENTRY,
    JS_OPT_REASON_SHAPE_CHANGED,
    JS_OPT_REASON_NAMED_FAST_NO_KEY,
    JS_OPT_REASON_NAMED_FAST_HOST_DYNAMIC,
    JS_OPT_REASON_NAMED_FAST_NO_RECEIVER,
    JS_OPT_REASON_NAMED_FAST_NO_ENTRY,
    JS_OPT_REASON_NAMED_FAST_ATTRIBUTES,
    JS_OPT_REASON_NAMED_FAST_BOUNDS,
    JS_OPT_REASON_NAMED_FAST_RESERVED,
    JS_OPT_REASON_NAMED_FAST_DELETED,
    JS_OPT_REASON_NAMED_FAST_VALUE_TYPE,
    JS_OPT_REASON_COUNT
} JsOptReason;

typedef enum JsOptTraceOutcome {
    JS_OPT_OUTCOME_TAKEN = 0,
    JS_OPT_OUTCOME_FALLBACK
} JsOptTraceOutcome;

typedef struct JsOptTraceCounter {
    uint64_t attempts;
    uint64_t taken;
    uint64_t fallback;
    uint64_t invalidated;
} JsOptTraceCounter;

#ifdef LAMBDA_JS_EXEC_PROFILE
int js_opt_trace_is_enabled(void);
void js_opt_trace_record(JsOptEvent event, JsOptReason reason,
                         JsOptTraceOutcome outcome);
// Attribute the ordinary-slot miss to the stable source spelling while the
// profile build is active. The release stub erases this diagnostic entirely.
void js_opt_trace_named_fast_no_entry(const char* name, uint32_t length);
void js_opt_trace_dump(void);

#else
#define JS_PROFILE_NOOP(name, return_type, args, body) \
    static inline return_type name args body
JS_PROFILE_NOOP(js_opt_trace_is_enabled, int, (void), { return 0; })
JS_PROFILE_NOOP(js_opt_trace_record, void, (JsOptEvent event, JsOptReason reason, JsOptTraceOutcome outcome), { (void)event; (void)reason; (void)outcome; })
JS_PROFILE_NOOP(js_opt_trace_named_fast_no_entry, void, (const char* name, uint32_t length), { (void)name; (void)length; })
JS_PROFILE_NOOP(js_opt_trace_dump, void, (void), {})
#undef JS_PROFILE_NOOP
#endif

#ifdef __cplusplus
}

#endif
