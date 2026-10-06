#pragma once

// browser embeddings exchange text; tagged Items never cross the JS boundary.
#ifdef __cplusplus
extern "C" {
#endif

// one synchronous REPL per module instance; init/reset return 1 on success.
int lambda_wasm_init(void);
// UTF-8 Lambda value text, or NULL on initialization/argument failure.
// the result is borrowed until the next evaluation, reset or shutdown.
const char* lambda_wasm_eval(const char* source);
int lambda_wasm_reset(void);
void lambda_wasm_shutdown(void);

#ifdef __cplusplus
}
#endif
