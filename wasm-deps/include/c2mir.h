/* Minimal C2MIR stub for WASM builds */
#ifndef C2MIR_H_WASM_STUB
#define C2MIR_H_WASM_STUB

#include "mir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C2MIR stub functions */
static inline void c2mir_init(MIR_context_t ctx) { (void)ctx; }
static inline void c2mir_finish(MIR_context_t ctx) { (void)ctx; }
static inline int c2mir_compile(MIR_context_t ctx, const char *code, const char *name) { (void)ctx; (void)code; (void)name; return 0; }

#ifdef __cplusplus
}
#endif

#endif /* C2MIR_H_WASM_STUB */
