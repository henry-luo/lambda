/* Minimal MIR-gen stub for WASM builds */
#ifndef MIR_GEN_H_WASM_STUB
#define MIR_GEN_H_WASM_STUB

#include "mir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stub declarations for MIR generator functions */
static inline void MIR_gen_init(MIR_context_t ctx) { (void)ctx; }
static inline void MIR_gen_finish(MIR_context_t ctx) { (void)ctx; }

#ifdef __cplusplus
}
#endif

#endif /* MIR_GEN_H_WASM_STUB */
