/* Minimal GMP stub for cross-compilation testing */
#ifndef __GMP_H__
#define __GMP_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int _mp_alloc;
    int _mp_size;
    unsigned long *_mp_d;
} __mpz_struct;

typedef __mpz_struct mpz_t[1];

/* Basic function declarations */
void mpz_init(mpz_t x);
void mpz_clear(mpz_t x);
void mpz_set_str(mpz_t rop, const char *str, int base);
char* mpz_get_str(char *str, int base, const mpz_t op);
void mpz_add(mpz_t rop, const mpz_t op1, const mpz_t op2);
void mpz_sub(mpz_t rop, const mpz_t op1, const mpz_t op2);
void mpz_mul(mpz_t rop, const mpz_t op1, const mpz_t op2);

#ifdef __cplusplus
}
#endif

#endif /* __GMP_H__ */
