#pragma once

#include <stdint.h>

#if defined(LAMBDA_JS_EXEC_PROFILE) && !defined(LAMBDA_STATIC)

#if defined(__APPLE__)
#define JS_EXEC_PROFILE_WEAK_IMPORT __attribute__((weak_import))
#elif defined(__GNUC__) || defined(__clang__)
#define JS_EXEC_PROFILE_WEAK_IMPORT __attribute__((weak))
#else
#define JS_EXEC_PROFILE_WEAK_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint64_t js_profile_property_set_branch_enter(const char* label) JS_EXEC_PROFILE_WEAK_IMPORT;
void js_profile_property_set_branch_leave(const char* label, uint64_t token) JS_EXEC_PROFILE_WEAK_IMPORT;
void js_profile_property_set_branch_add_count(const char* label, uint64_t count) JS_EXEC_PROFILE_WEAK_IMPORT;

#ifdef __cplusplus
}
#endif

static inline uint64_t js_weak_profile_property_set_branch_enter(const char* label) {
    return js_profile_property_set_branch_enter ?
        js_profile_property_set_branch_enter(label) : 0;
}

static inline void js_weak_profile_property_set_branch_leave(const char* label, uint64_t token) {
    if (js_profile_property_set_branch_leave) {
        js_profile_property_set_branch_leave(label, token);
    } else {
        (void)label;
        (void)token;
    }
}

static inline void js_weak_profile_property_set_branch_add_count(const char* label, uint64_t count) {
    if (js_profile_property_set_branch_add_count) {
        js_profile_property_set_branch_add_count(label, count);
    } else {
        (void)label;
        (void)count;
    }
}

#ifdef __cplusplus

#define JS_WEAK_PROFILE_CONCAT_INNER(a, b) a##b
#define JS_WEAK_PROFILE_CONCAT(a, b) JS_WEAK_PROFILE_CONCAT_INNER(a, b)

struct JsWeakPropertySetBranchScope {
    const char* label;
    uint64_t token;

    explicit JsWeakPropertySetBranchScope(const char* branch_label) :
        label(branch_label),
        token(js_weak_profile_property_set_branch_enter(branch_label)) {}

    ~JsWeakPropertySetBranchScope() {
        if (token != 0) js_weak_profile_property_set_branch_leave(label, token);
    }
};

#define JS_WEAK_PROPERTY_SET_BRANCH(label) \
    JsWeakPropertySetBranchScope JS_WEAK_PROFILE_CONCAT(_js_weak_property_set_branch_scope_, __LINE__)(label)

#endif

#else

static inline uint64_t js_weak_profile_property_set_branch_enter(const char* label) {
    (void)label;
    return 0;
}

static inline void js_weak_profile_property_set_branch_leave(const char* label, uint64_t token) {
    (void)label;
    (void)token;
}

static inline void js_weak_profile_property_set_branch_add_count(const char* label, uint64_t count) {
    (void)label;
    (void)count;
}

#ifdef __cplusplus
#define JS_WEAK_PROPERTY_SET_BRANCH(label) ((void)0)
#endif

#endif
