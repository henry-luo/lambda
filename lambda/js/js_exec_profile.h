#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable optimization decisions consumed by focused JS contract tests. The
// broad execution profiler below remains the source for elapsed-time and
// aggregate optimization reports.
typedef enum JsOptEvent {
    JS_OPT_SCOPE_LOOKUP_CACHE_HIT = 0,
    JS_OPT_SCOPE_LOOKUP_CACHE_MISS,
    JS_OPT_REGEX_COMPILE_CACHE_HIT,
    JS_OPT_REGEX_COMPILE_CACHE_MISS,
    JS_OPT_REGEX_PERMANENT_CACHE_HIT,
    JS_OPT_REGEX_FRESH_WRAPPER,
    JS_OPT_REGEX_KEYLESS_REJECT,
    JS_OPT_REGEX_CACHE_INVALIDATE,
    JS_OPT_ARRAY_SET_FAST_HIT,
    JS_OPT_ARRAY_SET_GUARD_FAIL,
    JS_OPT_DYNAMIC_FUNCTION_FASTPATH,
    JS_OPT_DYNAMIC_FUNCTION_CACHE_HIT,
    JS_OPT_DYNAMIC_FUNCTION_CACHE_MISS,
    JS_OPT_MIR_DIRECT_DESTINATION,
    JS_OPT_MIR_DISCARD_ELISION,
    JS_OPT_MIR_BRANCH_DIRECT,
    JS_OPT_MIR_GENERIC_FALLBACK,
    JS_OPT_MIR_BOX_VALUE,
    JS_OPT_MIR_UNBOX_VALUE,
    JS_OPT_MIR_ROOT_STORE,
    JS_OPT_MODULE_CACHE_HIT,
    JS_OPT_MODULE_CACHE_MISS,
    JS_OPT_TLA_DEFERRED_BODY,
    JS_OPT_TLA_DRAIN,
    JS_OPT_URI_ERROR_CACHE_HIT,
    JS_OPT_URI_ERROR_CACHE_MISS,
    JS_OPT_NAMED_FAST_PROBE,
    JS_OPT_NAMED_FAST_HIT,
    JS_OPT_NAMED_FAST_MISS,
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
#define JS_PROFILED_PUSH_D_NAME "js_profiled_push_d"
#define JS_PROFILED_IT2D_NAME "js_profiled_it2d"
#define JS_PROFILED_IT2I_NAME "js_profiled_it2i"

int js_opt_trace_is_enabled(void);
void js_opt_trace_record(JsOptEvent event, JsOptReason reason,
                         JsOptTraceOutcome outcome);
void js_opt_trace_dump(void);

#else
#define JS_PROFILED_PUSH_D_NAME "push_d"
#define JS_PROFILED_IT2D_NAME "it2d"
#define JS_PROFILED_IT2I_NAME "it2i"

#define JS_PROFILE_NOOP(name, return_type, args, body) \
    static inline return_type name args body
JS_PROFILE_NOOP(js_opt_trace_is_enabled, int, (void), { return 0; })
JS_PROFILE_NOOP(js_opt_trace_record, void, (JsOptEvent event, JsOptReason reason, JsOptTraceOutcome outcome), { (void)event; (void)reason; (void)outcome; })
JS_PROFILE_NOOP(js_opt_trace_dump, void, (void), {})
#undef JS_PROFILE_NOOP
#endif

#ifdef __cplusplus
}

#endif
