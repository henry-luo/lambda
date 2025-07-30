/* Minimal MIR stub for WASM builds */
#ifndef MIR_H_WASM_STUB
#define MIR_H_WASM_STUB

#ifdef __cplusplus
extern "C" {
#endif

/* Basic MIR types as stubs */
typedef struct {
    void *dummy;
} MIR_context_t;

typedef struct {
    void *dummy;
} MIR_module_t;

typedef struct {
    void *dummy;
} MIR_func_t;

typedef struct {
    void *dummy;
} MIR_item_t;

typedef void *MIR_val_t;

/* Stub function declarations */
static inline MIR_context_t MIR_init(void) { 
    MIR_context_t ctx = {0}; 
    return ctx; 
}

static inline void MIR_finish(MIR_context_t ctx) { (void)ctx; }
static inline MIR_module_t *MIR_new_module(MIR_context_t ctx, const char *name) { (void)ctx; (void)name; return NULL; }
static inline void MIR_finish_module(MIR_context_t ctx) { (void)ctx; }
static inline void MIR_load_module(MIR_context_t ctx, MIR_module_t *module) { (void)ctx; (void)module; }
static inline void MIR_link(MIR_context_t ctx, void (*resolved_addr_func)(const char *name, void *addr)) { (void)ctx; (void)resolved_addr_func; }
static inline void *MIR_gen_and_redirect(MIR_context_t ctx) { (void)ctx; return NULL; }

/* Additional commonly used MIR functions as stubs */
static inline void *MIR_gen(MIR_context_t ctx, MIR_func_t *func) { (void)ctx; (void)func; return NULL; }
static inline void MIR_gen_set_debug_file(MIR_context_t ctx, const char *file) { (void)ctx; (void)file; }
static inline void MIR_gen_set_optimize_level(MIR_context_t ctx, int level) { (void)ctx; (void)level; }

#ifdef __cplusplus
}
#endif

#endif /* MIR_H_WASM_STUB */
