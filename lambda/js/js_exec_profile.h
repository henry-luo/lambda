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
    JS_EXEC_PROF_LOAD_IC_PROBE,
    JS_EXEC_PROF_LOAD_IC_HIT_MONO,
    JS_EXEC_PROF_LOAD_IC_HIT_POLY,
    JS_EXEC_PROF_LOAD_IC_MISS,
    JS_EXEC_PROF_LOAD_IC_INSTALL_MONO,
    JS_EXEC_PROF_LOAD_IC_INSTALL_POLY,
    JS_EXEC_PROF_LOAD_IC_MEGAMORPHIC,
    JS_EXEC_PROF_STORE_IC_PROBE,
    JS_EXEC_PROF_STORE_IC_HIT_MONO,
    JS_EXEC_PROF_STORE_IC_HIT_POLY,
    JS_EXEC_PROF_STORE_IC_MISS,
    JS_EXEC_PROF_STORE_IC_INSTALL_MONO,
    JS_EXEC_PROF_STORE_IC_INSTALL_POLY,
    JS_EXEC_PROF_STORE_IC_MEGAMORPHIC,
    JS_EXEC_PROF_BOX_FLOAT,
    JS_EXEC_PROF_UNBOX_INT,
    JS_EXEC_PROF_UNBOX_FLOAT,
    JS_EXEC_PROF_COERCE,
    JS_EXEC_PROF_OTHER_RUNTIME_CALL,
    JS_EXEC_PROF_EVENT_COUNT
} JsExecProfileEvent;

typedef enum JsLoadICProfileReason {
    JS_LOAD_IC_SITE_PROBE = 0,
    JS_LOAD_IC_SITE_HIT_MONO,
    JS_LOAD_IC_SITE_HIT_POLY,
    JS_LOAD_IC_SITE_MISS_KEY,
    JS_LOAD_IC_SITE_MISS_NOT_MAP,
    JS_LOAD_IC_SITE_MISS_NOT_PLAIN,
    JS_LOAD_IC_SITE_MISS_NO_DATA,
    JS_LOAD_IC_SITE_MISS_BAD_TYPEMAP,
    JS_LOAD_IC_SITE_MISS_NOT_FOUND,
    JS_LOAD_IC_SITE_MISS_NAME,
    JS_LOAD_IC_SITE_MISS_FLAGS,
    JS_LOAD_IC_SITE_MISS_OFFSET,
    JS_LOAD_IC_SITE_MISS_DELETED,
    JS_LOAD_IC_SITE_INSTALL_MONO,
    JS_LOAD_IC_SITE_INSTALL_POLY,
    JS_LOAD_IC_SITE_MEGAMORPHIC,
    JS_LOAD_IC_SITE_REASON_COUNT
} JsLoadICProfileReason;

typedef enum JsStoreICProfileReason {
    JS_STORE_IC_SITE_PROBE = 0,
    JS_STORE_IC_SITE_HIT_MONO,
    JS_STORE_IC_SITE_HIT_POLY,
    JS_STORE_IC_SITE_MISS_KEY,
    JS_STORE_IC_SITE_MISS_NOT_MAP,
    JS_STORE_IC_SITE_MISS_NOT_PLAIN,
    JS_STORE_IC_SITE_MISS_NO_DATA,
    JS_STORE_IC_SITE_MISS_BAD_TYPEMAP,
    JS_STORE_IC_SITE_MISS_NOT_FOUND,
    JS_STORE_IC_SITE_MISS_NAME,
    JS_STORE_IC_SITE_MISS_FLAGS,
    JS_STORE_IC_SITE_MISS_OFFSET,
    JS_STORE_IC_SITE_MISS_DELETED,
    JS_STORE_IC_SITE_MISS_TYPE,
    JS_STORE_IC_SITE_INSTALL_MONO,
    JS_STORE_IC_SITE_INSTALL_POLY,
    JS_STORE_IC_SITE_MEGAMORPHIC,
    JS_STORE_IC_SITE_REASON_COUNT
} JsStoreICProfileReason;

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
uint64_t js_profile_property_set_branch_enter(const char* label);
void js_profile_property_set_branch_leave(const char* label, uint64_t token);
void js_profile_property_set_branch_add_count(const char* label, uint64_t count);
void js_profile_load_ic_site(const char* label, JsLoadICProfileReason reason);
void js_profile_store_ic_site(const char* label, JsStoreICProfileReason reason);
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
static inline uint64_t js_profile_property_set_branch_enter(const char* label) {
    (void)label;
    return 0;
}
static inline void js_profile_property_set_branch_leave(const char* label, uint64_t token) {
    (void)label;
    (void)token;
}
static inline void js_profile_property_set_branch_add_count(const char* label, uint64_t count) {
    (void)label;
    (void)count;
}
static inline void js_profile_load_ic_site(const char* label, JsLoadICProfileReason reason) {
    (void)label;
    (void)reason;
}
static inline void js_profile_store_ic_site(const char* label, JsStoreICProfileReason reason) {
    (void)label;
    (void)reason;
}
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

struct JsPropertySetBranchScope {
    const char* label;
    uint64_t token;

    explicit JsPropertySetBranchScope(const char* branch_label) : label(branch_label), token(0) {
        int mode = g_js_exec_profile_mode;
        if (mode == 0) return;
        if (mode < 0) mode = js_exec_profile_mode();
        if (mode > 0) token = js_profile_property_set_branch_enter(label);
    }

    ~JsPropertySetBranchScope() {
        if (token != 0) js_profile_property_set_branch_leave(label, token);
    }
};

#define JS_PROPERTY_SET_BRANCH(label) \
    JsPropertySetBranchScope JS_EXEC_PROFILE_CONCAT(_js_property_set_branch_scope_, __LINE__)(label)
#else
#define JS_EXEC_PROFILE_SCOPE(event_id) ((void)0)
#define JS_PROPERTY_SET_BRANCH(label) ((void)0)
#endif

#endif
