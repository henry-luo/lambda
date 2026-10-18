#pragma once

#include "js_transpiler.hpp"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_source_module.hpp"
#include "js_interp.hpp"
#include "../mir/mir.h"

extern MIR_error_func_t g_batch_mir_error_handler;
extern unsigned int g_js_mir_optimize_level;
extern int g_js_force_document_interp;
extern "C" Item js_process_emit_before_exit(int code);
extern "C" void js_process_emit_exit(int code);
bool js_module_ast_prebuild_imports(const char* filename, const char* source,
    size_t length);
bool js_module_ast_prebuild_await_import(const char* path);

struct ActiveJsTranspileOwner {
    JsTranspiler* tp;
    char* owned_source;
};
struct JsCompileRecoveryState {
    JsTranspiler* active_js_transpiler;
    char* active_js_owned_source;
    ArrayList* stack;
};
JsCompileRecoveryState* js_compile_recovery_state_ensure(void);
JsCompileRecoveryState* js_compile_recovery_state_current(void);
void js_compile_recovery_state_destroy_context(JsRuntimeState* state);
#define g_active_js_transpiler (js_compile_recovery_state_ensure()->active_js_transpiler)
#define g_active_js_owned_source (js_compile_recovery_state_ensure()->active_js_owned_source)
void js_track_active_compile(JsTranspiler* tp, char* owned_source);
void js_clear_active_compile(JsTranspiler* tp, char* owned_source);
void js_cleanup_active_compile(void);
void js_abandon_active_compile_after_signal(void);

static inline bool js_path_is_http_url(const char* path) {
    return path && (!strncmp(path, "http://", 7) || !strncmp(path, "https://", 8));
}
bool js_activate_runtime_name_pool(void);
Item transpile_js_module_to_mir(Runtime*, const char* source, const char* filename);
