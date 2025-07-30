/* Minimal GMP stub for WASM builds */
#ifndef GMP_H_WASM_STUB
#define GMP_H_WASM_STUB

#ifdef __cplusplus
extern "C" {
#endif

/* Basic types for WASM stub */
typedef struct {
    long _mp_d; /* minimal stub - not functional */
} __mpf_struct;

typedef __mpf_struct mpf_t[1];

/* Stub function declarations */
static inline double mpf_get_d(const mpf_t op) {
    (void)op;
    return 0.0; /* stub implementation */
}

static inline int gmp_sprintf(char *buf, const char *fmt, ...) {
    (void)buf; (void)fmt;
    return 0; /* stub implementation */
}

/* Additional commonly used GMP functions as stubs */
static inline void mpf_init(mpf_t x) { (void)x; }
static inline void mpf_clear(mpf_t x) { (void)x; }
static inline void mpf_set_d(mpf_t rop, double op) { (void)rop; (void)op; }
static inline void mpf_set_str(mpf_t rop, const char *str, int base) { (void)rop; (void)str; (void)base; }

#ifdef __cplusplus
}
#endif

#endif /* GMP_H_WASM_STUB */
