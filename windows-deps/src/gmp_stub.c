/* Minimal GMP stub implementation */
#include "../include/gmp.h"
#include <stdlib.h>
#include <string.h>

void mpz_init(mpz_t x) {
    x->_mp_alloc = 0;
    x->_mp_size = 0;
    x->_mp_d = NULL;
}

void mpz_clear(mpz_t x) {
    if (x->_mp_d) free(x->_mp_d);
    x->_mp_alloc = 0;
    x->_mp_size = 0;
    x->_mp_d = NULL;
}

void mpz_set_str(mpz_t rop, const char *str, int base) {
    (void)rop; (void)str; (void)base;
}

char* mpz_get_str(char *str, int base, const mpz_t op) {
    (void)base; (void)op;
    if (!str) str = malloc(32);
    strcpy(str, "0");
    return str;
}

void mpz_add(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    (void)rop; (void)op1; (void)op2;
}

void mpz_sub(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    (void)rop; (void)op1; (void)op2;
}

void mpz_mul(mpz_t rop, const mpz_t op1, const mpz_t op2) {
    (void)rop; (void)op1; (void)op2;
}

void mpf_init(mpf_t x) {
    x->_mp_prec = 0;
    x->_mp_size = 0;
    x->_mp_exp = 0;
    x->_mp_d = NULL;
}

void mpf_clear(mpf_t x) {
    if (x->_mp_d) free(x->_mp_d);
    x->_mp_prec = 0;
    x->_mp_size = 0;
    x->_mp_exp = 0;
    x->_mp_d = NULL;
}

void mpf_set_str(mpf_t rop, const char *str, int base) {
    (void)rop; (void)str; (void)base;
}
