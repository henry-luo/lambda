/* Native C2MIR port of beng/pidigits.ls. */
/* Unbounded spigot algorithm (Gibbons 2004) over a small sign-magnitude
 * bignum: base 2^32 limbs in fixed-capacity structs. The r component of the
 * LFT state goes negative after the reduce step (r - digit*t can be < 0), so
 * signed add/sub/compare are needed; q, s, t stay non-negative, and the
 * digit-extraction division only ever sees positive operands with a tiny
 * quotient, so quotient-by-repeated-subtraction is enough. The small factors
 * (k, 2k+1, digit, 10) all fit in an unsigned int. */
extern int printf(const char *, ...);

#define NUM_DIGITS 30
/* 30 digits needs ~640 bits of state (q ~ k! * 10^30 with k <= ~100); 64
 * limbs of 32 bits gives ample headroom. */
#define BIG_LIMBS 64

typedef struct {
    int n;                       /* number of used limbs; 0 means zero */
    int neg;                     /* sign flag; zero is always neg==0 */
    unsigned int d[BIG_LIMBS];   /* little-endian base-2^32 magnitude limbs */
} Big;

static void big_set_small(Big *x, unsigned int v) {
    x->n = v != 0 ? 1 : 0;
    x->neg = 0;
    x->d[0] = v;
}

static void big_copy(Big *dst, const Big *src) {
    int i;
    dst->n = src->n;
    dst->neg = src->neg;
    for (i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}

/* -1, 0, 1 comparing magnitudes only */
static int big_mag_cmp(const Big *a, const Big *b) {
    int i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    }
    return 0;
}

/* signed -1, 0, 1 */
static int big_cmp(const Big *a, const Big *b) {
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    return a->neg ? -big_mag_cmp(a, b) : big_mag_cmp(a, b);
}

/* |dst| = |a| + |b|; safe when dst aliases a or b (low-to-high sweep) */
static void big_mag_add(Big *dst, const Big *a, const Big *b) {
    int i;
    int n = a->n > b->n ? a->n : b->n;
    unsigned long long carry = 0;
    for (i = 0; i < n; i++) {
        unsigned long long sum = carry;
        if (i < a->n) sum += a->d[i];
        if (i < b->n) sum += b->d[i];
        dst->d[i] = (unsigned int) sum;
        carry = sum >> 32;
    }
    if (carry != 0) dst->d[n++] = (unsigned int) carry;
    dst->n = n;
}

/* |dst| = |a| - |b|, requires |a| >= |b|; safe when dst aliases a or b */
static void big_mag_sub(Big *dst, const Big *a, const Big *b) {
    int i;
    long long borrow = 0;
    for (i = 0; i < a->n; i++) {
        long long diff = (long long) a->d[i] - borrow - (i < b->n ? (long long) b->d[i] : 0);
        if (diff < 0) { diff += 4294967296LL; borrow = 1; } else borrow = 0;
        dst->d[i] = (unsigned int) diff;
    }
    dst->n = a->n;
    while (dst->n > 0 && dst->d[dst->n - 1] == 0) dst->n--;
}

/* dst = a + b with signs; safe when dst aliases a or b */
static void big_add(Big *dst, const Big *a, const Big *b) {
    if (a->neg == b->neg) {
        int neg = a->neg;
        big_mag_add(dst, a, b);
        dst->neg = dst->n != 0 ? neg : 0;
    } else {
        int cmp = big_mag_cmp(a, b);
        if (cmp >= 0) {
            int neg = a->neg;
            big_mag_sub(dst, a, b);
            dst->neg = dst->n != 0 ? neg : 0;
        } else {
            int neg = b->neg;
            big_mag_sub(dst, b, a);
            dst->neg = dst->n != 0 ? neg : 0;
        }
    }
}

/* dst = a - b with signs; safe when dst aliases a or b */
static void big_sub(Big *dst, const Big *a, const Big *b) {
    Big nb;
    big_copy(&nb, b);
    if (nb.n != 0) nb.neg = !nb.neg;
    big_add(dst, a, &nb);
}

/* dst = a * m for small non-negative m; safe when dst aliases a */
static void big_mul_small(Big *dst, const Big *a, unsigned int m) {
    int i;
    unsigned long long carry = 0;
    if (m == 0 || a->n == 0) { dst->n = 0; dst->neg = 0; return; }
    for (i = 0; i < a->n; i++) {
        unsigned long long prod = (unsigned long long) a->d[i] * m + carry;
        dst->d[i] = (unsigned int) prod;
        carry = prod >> 32;
    }
    dst->n = a->n;
    dst->neg = a->neg;
    if (carry != 0) dst->d[dst->n++] = (unsigned int) carry;
}

/* floor(a / b) for positive a, b where the quotient is known to be tiny (a
 * spigot digit candidate), so repeated subtraction is enough. Only reached
 * under the q <= r guard, which guarantees both operands are positive. */
static unsigned int big_div_small_quot(const Big *a, const Big *b) {
    Big rem;
    unsigned int q = 0;
    big_copy(&rem, a);
    while (big_mag_cmp(&rem, b) >= 0) {
        big_mag_sub(&rem, &rem, b);
        q++;
    }
    return q;
}

/* dst = (base * mult + add) * k2 (the LFT composition term) */
static void big_muladd(Big *dst, const Big *base, unsigned int mult, const Big *add, unsigned int k2) {
    big_mul_small(dst, base, mult);
    big_add(dst, dst, add);
    big_mul_small(dst, dst, k2);
}

int main(void) {
    Big q, r, s, t, nq, nr, ns, nt, a, b;
    unsigned int k = 0;
    int i = 0;
    int line_len = 0;
    int total_len = 0;
    char line[16];
    char all_digits[NUM_DIGITS + 1];
    static const char expected[] = "314159265358979323846264338327";
    int ok;
    int j;

    big_set_small(&q, 1);
    big_set_small(&r, 0);
    big_set_small(&s, 0);
    big_set_small(&t, 1);

    while (i < NUM_DIGITS) {
        unsigned int k2;
        k = k + 1;
        k2 = k * 2 + 1;

        /* compose: multiply LFT by next term */
        big_mul_small(&nq, &q, k);
        big_muladd(&nr, &q, 2, &r, k2);
        big_mul_small(&ns, &s, k);
        big_muladd(&nt, &s, 2, &t, k2);
        big_copy(&q, &nq);
        big_copy(&r, &nr);
        big_copy(&s, &ns);
        big_copy(&t, &nt);

        /* can we extract a digit? */
        if (big_cmp(&q, &r) <= 0) {
            unsigned int fd3, fd4;
            big_mul_small(&a, &q, 3);
            big_add(&a, &a, &r);
            big_mul_small(&b, &s, 3);
            big_add(&b, &b, &t);
            fd3 = big_div_small_quot(&a, &b);
            big_mul_small(&a, &q, 4);
            big_add(&a, &a, &r);
            big_mul_small(&b, &s, 4);
            big_add(&b, &b, &t);
            fd4 = big_div_small_quot(&a, &b);
            if (fd3 == fd4) {
                line[line_len++] = (char) ('0' + fd3);
                all_digits[total_len++] = (char) ('0' + fd3);
                i = i + 1;

                /* output 10 digits per line */
                if (i % 10 == 0) {
                    line[line_len] = 0;
                    printf("%s\t:%d\n", line, i);
                    line_len = 0;
                }

                /* reduce: eliminate the extracted digit (r may go negative) */
                big_mul_small(&a, &t, fd3);
                big_sub(&r, &r, &a);
                big_mul_small(&r, &r, 10);
                big_mul_small(&q, &q, 10);
            }
        }
    }

    /* handle last partial line (fewer than 10 digits) */
    if (line_len > 0) {
        while (line_len < 10) line[line_len++] = ' ';
        line[line_len] = 0;
        printf("%s\t:%d\n", line, i);
    }

    all_digits[total_len] = 0;
    ok = total_len == NUM_DIGITS;
    for (j = 0; j < NUM_DIGITS; j++) {
        if (all_digits[j] != expected[j]) ok = 0;
    }
    printf(ok ? "pidigits: PASS\n" : "pidigits: FAIL\n");
    return ok ? 0 : 1;
}
