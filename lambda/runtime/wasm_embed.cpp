#ifdef LAMBDA_WASM

#include "../lambda-wasm.h"
#include "interp.hpp"
#include "lambda-root-frame.hpp"
#include "../core/print.h"
#include "../core/lambda-decimal.hpp"

// each WASM module instance owns one evaluator and one persistent REPL.
static Runtime wasm_runtime = {};
static InterpReplSession wasm_repl = {};
static StrBuf* wasm_result = NULL;
static bool wasm_initialized = false;

extern "C" int lambda_wasm_init(void) {
    if (wasm_initialized) return 1;
    decimal_init();
    runtime_init(&wasm_runtime);
    lambda_tier_set(LAMBDA_TIER_INTERP);
    wasm_result = strbuf_new();
    if (!wasm_result || !interp_repl_session_init(&wasm_repl, &wasm_runtime)) {
        lambda_wasm_shutdown();
        return 0;
    }
    wasm_initialized = true;
    return 1;
}

extern "C" const char* lambda_wasm_eval(const char* source) {
    if (!source || !lambda_wasm_init()) return NULL;
    strbuf_reset(wasm_result);
    // printing may allocate, so retain the returned value in a precise home.
    RootFrame roots(1);
    Rooted<Item> result(roots, interp_repl_session_eval(&wasm_repl, source));
    print_item(wasm_result, result.get());
    return wasm_result->str;
}

extern "C" int lambda_wasm_reset(void) {
    if (!wasm_initialized) return lambda_wasm_init();
    strbuf_reset(wasm_result);
    return interp_repl_session_init(&wasm_repl, &wasm_runtime) ? 1 : 0;
}

extern "C" void lambda_wasm_shutdown(void) {
    interp_repl_session_destroy(&wasm_repl);
    runtime_cleanup(&wasm_runtime);
    strbuf_free(wasm_result);
    wasm_result = NULL;
    wasm_initialized = false;
}

#endif
