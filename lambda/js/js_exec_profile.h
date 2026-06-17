#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum JsExecProfileEvent {
    JS_EXEC_PROF_PROPERTY_GET = 0,
    JS_EXEC_PROF_PROPERTY_SET,
    JS_EXEC_PROF_PROPERTY_ACCESS,
    JS_EXEC_PROF_ARRAY_GET_INT,
    JS_EXEC_PROF_ARRAY_SET_INT,
    JS_EXEC_PROF_ARRAY_PUSH,
    JS_EXEC_PROF_CALL_FUNCTION,
    JS_EXEC_PROF_DISPATCH_BUILTIN,
    JS_EXEC_PROF_NEW_OBJECT,
    JS_EXEC_PROF_NEW_OBJECT_SHAPE,
    JS_EXEC_PROF_GET_SLOT_F,
    JS_EXEC_PROF_GET_SLOT_I,
    JS_EXEC_PROF_SET_SLOT_F,
    JS_EXEC_PROF_SET_SLOT_I,
    JS_EXEC_PROF_SHAPE_SLOT_GUARD,
    JS_EXEC_PROF_SHAPE_GUARD_HIT,
    JS_EXEC_PROF_SHAPE_GUARD_MISS,
    JS_EXEC_PROF_BOX_FLOAT,
    JS_EXEC_PROF_UNBOX_INT,
    JS_EXEC_PROF_UNBOX_FLOAT,
    JS_EXEC_PROF_COERCE,
    JS_EXEC_PROF_OTHER_RUNTIME_CALL,
    JS_EXEC_PROF_EVENT_COUNT
} JsExecProfileEvent;

#ifdef LAMBDA_JS_EXEC_PROFILE
#define JS_EXEC_PROFILE_ENABLED 1
#define JS_PROFILED_PUSH_D_NAME "js_profiled_push_d"
#define JS_PROFILED_IT2D_NAME "js_profiled_it2d"
#define JS_PROFILED_IT2I_NAME "js_profiled_it2i"

extern int g_js_exec_profile_mode;

int js_exec_profile_mode(void);
void js_exec_profile_reset(void);
uint64_t js_exec_profile_enter(JsExecProfileEvent event);
void js_exec_profile_leave(JsExecProfileEvent event, uint64_t token);
void js_exec_profile_count(JsExecProfileEvent event);
void js_exec_profile_note_mir_call(const char* fn_name);
void js_exec_profile_dump(void);
void js_profile_property_set_site(const char* label);
#else
#define JS_EXEC_PROFILE_ENABLED 0
#define JS_PROFILED_PUSH_D_NAME "push_d"
#define JS_PROFILED_IT2D_NAME "it2d"
#define JS_PROFILED_IT2I_NAME "it2i"

static inline int js_exec_profile_mode(void) { return 0; }
static inline void js_exec_profile_reset(void) {}
static inline uint64_t js_exec_profile_enter(JsExecProfileEvent event) {
    (void)event;
    return 0;
}
static inline void js_exec_profile_leave(JsExecProfileEvent event, uint64_t token) {
    (void)event;
    (void)token;
}
static inline void js_exec_profile_count(JsExecProfileEvent event) { (void)event; }
static inline void js_exec_profile_note_mir_call(const char* fn_name) { (void)fn_name; }
static inline void js_exec_profile_dump(void) {}
static inline void js_profile_property_set_site(const char* label) { (void)label; }
#endif

#ifdef __cplusplus
}

#ifdef LAMBDA_JS_EXEC_PROFILE
struct JsExecProfileScope {
    JsExecProfileEvent event;
    uint64_t token;

    explicit JsExecProfileScope(JsExecProfileEvent ev) : event(ev), token(0) {
        int mode = g_js_exec_profile_mode;
        if (mode == 0) return;
        if (mode < 0) mode = js_exec_profile_mode();
        if (mode > 0) token = js_exec_profile_enter(event);
    }

    ~JsExecProfileScope() {
        if (token != 0) js_exec_profile_leave(event, token);
    }
};

#define JS_EXEC_PROFILE_CONCAT_INNER(a, b) a##b
#define JS_EXEC_PROFILE_CONCAT(a, b) JS_EXEC_PROFILE_CONCAT_INNER(a, b)
#define JS_EXEC_PROFILE_SCOPE(event_id) \
    JsExecProfileScope JS_EXEC_PROFILE_CONCAT(_js_exec_profile_scope_, __LINE__)(event_id)
#else
#define JS_EXEC_PROFILE_SCOPE(event_id) ((void)0)
#endif

#endif
