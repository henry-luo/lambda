#include "lambda/jube/jube.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef JUBE_TEST_MODULE_NAME
#define JUBE_TEST_MODULE_NAME "lang-python"
#endif

static void jube_test_write_marker(const char* environment_name, const char* contents) {
    const char* marker_path = getenv(environment_name);
    if (!marker_path || !*marker_path) return;
    FILE* marker = fopen(marker_path, "wb");
    if (!marker) return;
    fputs(contents, marker);
    fclose(marker);
}

static int jube_test_init_failure(const JubeHostAPI* host) {
#if defined(JUBE_TEST_SUCCESS_INIT)
    jube_test_write_marker("JUBE_DEPENDENCY_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#elif defined(JUBE_TEST_STALE_CURSOR)
    if (!host || !host->hosted_language || !host->hosted_language->execution) return -72;
    const JubeGuestExecutionAPI* execution = host->hosted_language->execution;
    if (!execution->mir_context_create || !execution->mir_context_destroy ||
            !execution->mir_compiler_cursor_create ||
            !execution->mir_compiler_cursor_destroy ||
            !execution->mir_compiler_import_cache_init) {
        return -73;
    }
    void* mir_context = execution->mir_context_create(0);
    void* compiler_cursor = NULL;
    if (!mir_context || execution->mir_compiler_cursor_create(mir_context,
            &compiler_cursor) != 0 || !compiler_cursor) {
        if (mir_context) execution->mir_context_destroy(mir_context);
        return -74;
    }
    execution->mir_compiler_cursor_destroy(compiler_cursor);
    // A destroyed opaque handle and a manufactured token must be rejected
    // before the host can reach any private emitter storage.
    int stale_result = execution->mir_compiler_import_cache_init(compiler_cursor, 8);
    int invalid_result = execution->mir_compiler_import_cache_init((void*)1, 8);
    execution->mir_context_destroy(mir_context);
    void* context_owned_cursor = NULL;
    void* context_owned_mir = execution->mir_context_create(0);
    int context_result = -2;
    if (context_owned_mir && execution->mir_compiler_cursor_create(context_owned_mir,
            &context_owned_cursor) == 0 && context_owned_cursor) {
        context_result = 0;
    }
    if (context_result == 0) {
        execution->mir_context_destroy(context_owned_mir);
        context_result = execution->mir_compiler_import_cache_init(context_owned_cursor, 8);
    } else if (context_owned_mir) {
        execution->mir_context_destroy(context_owned_mir);
    }
    jube_test_write_marker("JUBE_STALE_CURSOR_MARKER", stale_result == -1 &&
        invalid_result == -1 && context_result == -1 ? "rejected\n" : "accepted\n");
    return stale_result == -1 && invalid_result == -1 && context_result == -1 ? -71 : -75;
#elif defined(JUBE_TEST_WRONG_OWNER)
    if (!host || !host->hosted_language || !host->hosted_language->execution) return -72;
    const JubeGuestExecutionAPI* execution = host->hosted_language->execution;
    if (!execution->mir_context_create || !execution->mir_context_destroy ||
            !execution->mir_module_create || !execution->mir_compiler_cursor_create ||
            !execution->mir_compiler_cursor_destroy ||
            !execution->mir_function_state_suspend || !execution->mir_function_state_restore ||
            !execution->mir_item_function_create_typed_current ||
            !execution->mir_function_finish_current ||
            !execution->mir_module_finalize_and_load_current ||
            !execution->mir_function_select || !execution->mir_label_create_current ||
            !execution->mir_label_emit_current ||
            !execution->mir_compiler_import_cache_init ||
            !execution->mir_local_direct_call_prototype_get_or_create ||
            !execution->mir_local_direct_call_emit) return -73;
    void* first_context = execution->mir_context_create(0);
    void* second_context = execution->mir_context_create(0);
    void* first_cursor = NULL;
    void* second_cursor = NULL;
    void* first_state = NULL;
    void* first_item = NULL;
    void* first_function = NULL;
    void* second_item = NULL;
    void* second_function = NULL;
    void* second_label = NULL;
    void* second_prototype = NULL;
    void* first_module = NULL;
    void* second_module = NULL;
    bool prepared = first_context && second_context &&
        (first_module = execution->mir_module_create(first_context, "first")) &&
        (second_module = execution->mir_module_create(second_context, "second")) &&
        execution->mir_compiler_cursor_create(first_context, &first_cursor) == 0 &&
        execution->mir_compiler_cursor_create(second_context, &second_cursor) == 0 &&
        first_cursor && second_cursor &&
        execution->mir_item_function_create_typed_current(first_cursor,
            "first_owned", 0, NULL, NULL, &first_item, &first_function) == 0 &&
        execution->mir_item_function_create_typed_current(second_cursor,
            "second_owned", 0, NULL, NULL, &second_item, &second_function) == 0;
    int select_first_result = prepared
        ? execution->mir_function_select(first_cursor, first_item, first_function) : -2;
    int wrong_state_result = prepared
        ? execution->mir_function_state_suspend(first_cursor, &first_state) : -2;
    if (wrong_state_result == 0) {
        wrong_state_result = execution->mir_function_state_restore(second_cursor, first_state);
    }
    int restore_owner_result = wrong_state_result == -1
        ? execution->mir_function_state_restore(first_cursor, first_state) : -2;
    int forged_state_result = prepared
        ? execution->mir_function_state_restore(first_cursor, (void*)1) : -2;
    int wrong_owner_result = prepared
        ? execution->mir_function_select(first_cursor, second_item, second_function) : -2;
    int wrong_label_result = select_first_result == 0 &&
        execution->mir_label_create_current(second_cursor, &second_label) == 0
        ? execution->mir_label_emit_current(first_cursor, second_label) : -2;
    int wrong_prototype_result = execution->mir_compiler_import_cache_init(second_cursor, 8) == 0 &&
        execution->mir_local_direct_call_prototype_get_or_create(second_cursor,
            "second_proto", "second_proto", 0, NULL, &second_prototype) == 0
        ? execution->mir_local_direct_call_emit(first_cursor, "first_owned",
            second_prototype, first_item, 1, 0, NULL) : -2;
    int wrong_module_result = prepared
        ? execution->mir_module_finalize_and_load_current(first_cursor, second_module) : -2;
    if (prepared) {
        execution->mir_function_finish_current(first_cursor);
        execution->mir_function_finish_current(second_cursor);
    }
    if (prepared) {
        execution->mir_module_finalize_and_load_current(first_cursor, first_module);
        execution->mir_module_finalize_and_load_current(second_cursor, second_module);
    }
    int stale_module_result = prepared
        ? execution->mir_module_finalize_and_load_current(first_cursor, first_module) : -2;
    void* stale_state = NULL;
    int stale_state_prepare = prepared
        ? execution->mir_function_state_suspend(first_cursor, &stale_state) : -2;
    if (first_cursor) execution->mir_compiler_cursor_destroy(first_cursor);
    int stale_state_result = stale_state_prepare == 0
        ? execution->mir_function_state_restore(first_cursor, stale_state) : -2;
    if (second_cursor) execution->mir_compiler_cursor_destroy(second_cursor);
    if (first_context) execution->mir_context_destroy(first_context);
    if (second_context) execution->mir_context_destroy(second_context);
    jube_test_write_marker("JUBE_WRONG_OWNER_MARKER", wrong_owner_result == -1 &&
        wrong_label_result == -1 && wrong_state_result == -1 && restore_owner_result == 0
        && forged_state_result == -1 && wrong_prototype_result == -1 &&
        wrong_module_result == -1 && stale_module_result == -1 &&
        stale_state_result == -1 ? "rejected\n" : "accepted\n");
    return wrong_owner_result == -1 && wrong_state_result == -1 &&
        wrong_label_result == -1 && wrong_prototype_result == -1 &&
        restore_owner_result == 0 && forged_state_result == -1 &&
        wrong_module_result == -1 && stale_module_result == -1 &&
        stale_state_result == -1 ? -71 : -75;
#elif defined(JUBE_TEST_REQUIRES_MISSING_CAPABILITY)
    jube_test_write_marker("JUBE_CAPABILITY_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#elif defined(JUBE_TEST_UNSUPPORTED_ABI) || defined(JUBE_TEST_UNDERSIZED_DESCRIPTOR)
    jube_test_write_marker("JUBE_DESCRIPTOR_INIT_MARKER", "init\n");
    return host ? 0 : -72;
#else
    return host ? -71 : -72;
#endif
}

static void jube_test_init_failure_shutdown(void) {
#if defined(JUBE_TEST_SUCCESS_INIT)
    jube_test_write_marker("JUBE_DEPENDENCY_SHUTDOWN_MARKER", "shutdown\n");
#else
    jube_test_write_marker("JUBE_INIT_FAILURE_MARKER", "shutdown\n");
#endif
}

#if defined(JUBE_TEST_REQUIRES_MISSING_CAPABILITY)
static const char* const jube_test_capability_aliases[] = {"py"};
static const char* const jube_test_capability_extensions[] = {".py"};

static int jube_test_capability_create_session(const JubeLanguageSessionConfig* config,
                                               JubeLanguageSession** out_session) {
    (void)config;
    if (out_session) *out_session = NULL;
    return -1;
}

static void jube_test_capability_destroy_session(JubeLanguageSession* session) {
    (void)session;
}

static int jube_test_capability_run(JubeLanguageSession* session,
                                    const JubeLanguageRunRequest* request) {
    (void)session;
    (void)request;
    return -1;
}

static const JubeLanguageDef jube_test_missing_capability_language = {
    JUBE_LANGUAGE_ABI_VERSION,
    sizeof(JubeLanguageDef),
    "python",
    jube_test_capability_aliases,
    1,
    jube_test_capability_extensions,
    1,
    jube_test_capability_create_session,
    jube_test_capability_destroy_session,
    jube_test_capability_run,
    NULL,
    JUBE_HOST_CAP_COMPILER,
    JUBE_HOSTED_LANG_CAP_NONE,
    JUBE_HOST_BUILD_ID,
};
#define JUBE_TEST_LANGUAGE_DESCRIPTOR &jube_test_missing_capability_language
#else
#define JUBE_TEST_LANGUAGE_DESCRIPTOR NULL
#endif

#if defined(JUBE_TEST_UNSUPPORTED_ABI)
#define JUBE_TEST_DESCRIPTOR_ABI (JUBE_ABI_VERSION + 1)
#else
#define JUBE_TEST_DESCRIPTOR_ABI JUBE_ABI_VERSION
#endif

#if defined(JUBE_TEST_UNDERSIZED_DESCRIPTOR)
#define JUBE_TEST_DESCRIPTOR_SIZE (JUBE_MODULE_DEF_V1_SIZE - 1)
#else
#define JUBE_TEST_DESCRIPTOR_SIZE sizeof(JubeModuleDef)
#endif

static const JubeModuleDef jube_test_init_failure_module = {
    JUBE_TEST_DESCRIPTOR_ABI,
    JUBE_TEST_DESCRIPTOR_SIZE,
    JUBE_TEST_MODULE_NAME,
    "0.1.0",
    "Test-only hosted module whose initializer fails after descriptor validation",
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    jube_test_init_failure,
    jube_test_init_failure_shutdown,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    JUBE_TEST_LANGUAGE_DESCRIPTOR,
};

extern "C" const JubeModuleDef* jube_module(void) {
    return &jube_test_init_failure_module;
}
