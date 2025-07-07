/* Minimal MIR stub header */
#ifndef MIR_H
#define MIR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MIR_context *MIR_context_t;
typedef struct MIR_module *MIR_module_t;
typedef struct MIR_func *MIR_func_t;

/* Stub function declarations */
void MIR_init(void);
void MIR_finish(void);
MIR_context_t MIR_init_context(void);
void MIR_finish_context(MIR_context_t ctx);

#ifdef __cplusplus
}
#endif

#endif /* MIR_H */
