// lambda/py/py_bigint.cpp — Python arbitrary-precision integers
// =============================================================
// Stores Python bigints as LMD_TYPE_DECIMAL Items with unlimited==PY_BIGINT_FLAG(2).
// Uses libmpdecimal with a dedicated 4000-digit context (~13,000 bits).
// Reuses Lambda's mpdecimal infrastructure (heap_alloc, lambda-decimal.hpp).

// Include mpdecimal.h first so its struct definitions precede any forward-decls
// that come from lambda-decimal.hpp (which only has typedef forward-decls).
#include <mpdecimal.h>

#include "py_bigint.h"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include "../lambda.h"
#include "../transpiler.hpp"
#include "py_runtime.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include "../../lib/mem.h"
#include <cstdio>
#include <cmath>
#include <cerrno>

// ─────────────────────────────────────────────────────────────────────
// Bigint context (4000-digit dedicated context, ~13,000 bits)
// ─────────────────────────────────────────────────────────────────────

static mpd_context_t g_bigint_ctx;
static bool g_bigint_initialized = false;

void py_bigint_init() {
    if (g_bigint_initialized) return;
    mpd_maxcontext(&g_bigint_ctx);
    g_bigint_ctx.prec = PY_BIGINT_PREC;
    g_bigint_initialized = true;
    log_debug("py-bigint: context initialized (prec=%d)", PY_BIGINT_PREC);
}

static mpd_context_t* bigint_ctx() {
    if (!g_bigint_initialized) py_bigint_init();
    return &g_bigint_ctx;
}

// ─────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────

// Wrap a finished mpd_t* into a Python bigint Item (takes ownership).
static Item bigint_wrap(mpd_t* mpd_val) {
    if (!mpd_val) return ItemNull;
    Decimal* dec = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!dec) { mpd_del(mpd_val); return ItemNull; }
    dec->unlimited = PY_BIGINT_FLAG;
    dec->dec_val   = mpd_val;
    Item result;
    result.item = c2it(dec);
    return result;
}

// Convert an Item (INT or BIGINT) to a freshly-allocated mpd_t* (caller must mpd_del).
static mpd_t* to_mpd(Item x) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* m = mpd_new(ctx);
    if (!m) return NULL;
    uint32_t status = 0;
    TypeId t = get_type_id(x);
    if (t == LMD_TYPE_INT) {
        mpd_qset_ssize(m, (mpd_ssize_t)x.get_int56(), ctx, &status);
    } else if (t == LMD_TYPE_DECIMAL) {
        Decimal* d = x.get_decimal();
        if (d && d->dec_val) {
            mpd_copy(m, d->dec_val, ctx);
        } else {
            mpd_qset_ssize(m, 0, ctx, &status);
        }
    } else if (t == LMD_TYPE_FLOAT) {
        // convert float to integer part (truncation)
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f", x.get_double());
        mpd_qset_string(m, buf, ctx, &status);
    } else {
        mpd_qset_ssize(m, 0, ctx, &status);
    }
    return m;
}

// ─────────────────────────────────────────────────────────────────────
// Type check
// ─────────────────────────────────────────────────────────────────────

bool py_is_bigint(Item x) {
    if (get_type_id(x) != LMD_TYPE_DECIMAL) return false;
    Decimal* d = x.get_decimal();
    return d && d->unlimited == PY_BIGINT_FLAG;
}

// ─────────────────────────────────────────────────────────────────────
// Creation
// ─────────────────────────────────────────────────────────────────────

Item py_bigint_from_int64(int64_t val) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* m = mpd_new(ctx);
    if (!m) return ItemNull;
    uint32_t status = 0;
    mpd_qset_ssize(m, (mpd_ssize_t)val, ctx, &status);
    return bigint_wrap(m);
}

Item py_bigint_from_cstr(const char* s) {
    if (!s || !*s) return py_bigint_from_int64(0);
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* m = mpd_new(ctx);
    if (!m) return ItemNull;
    uint32_t status = 0;
    // strip underscores (Python int literal can have 1_000_000)
    // and handle hex/oct/bin prefixes
    char buf[PY_BIGINT_PREC + 32];
    int blen = 0;
    bool negative = false;
    int i = 0;
    if (s[0] == '-') { negative = true; i++; }
    else if (s[0] == '+') { i++; }
    // handle 0x, 0o, 0b (parse as a base string manually) — for literals,
    // just copy digits skipping underscores
    while (s[i] && blen < (int)sizeof(buf) - 2) {
        if (s[i] != '_') buf[blen++] = s[i];
        i++;
    }
    buf[blen] = '\0';
    if (negative) {
        // prepend minus to buf
        memmove(buf + 1, buf, blen + 1);
        buf[0] = '-';
    }
    mpd_qset_string(m, buf, ctx, &status);
    if (status & MPD_Conversion_syntax) {
        log_error("py-bigint: cannot parse '%s' as integer", s);
        mpd_del(m);
        return ItemNull;
    }
    return bigint_wrap(m);
}

// ─────────────────────────────────────────────────────────────────────
// Normalization: promote back to int56 when possible
// ─────────────────────────────────────────────────────────────────────

Item py_bigint_normalize(Item x) {
    if (!py_is_bigint(x)) return x;
    Decimal* d = x.get_decimal();
    if (!d || !d->dec_val) return x;
    mpd_context_t* ctx = bigint_ctx();
    // check if integer part fits in int56
    uint32_t status = 0;
    mpd_ssize_t v = mpd_qget_ssize(d->dec_val, &status);
    if (status == 0 && v >= (mpd_ssize_t)INT56_MIN && v <= (mpd_ssize_t)INT56_MAX) {
        return (Item){.item = i2it((int64_t)v)};
    }
    return x;
}

// ─────────────────────────────────────────────────────────────────────
// Arithmetic
// ─────────────────────────────────────────────────────────────────────

Item py_bigint_add(Item a, Item b) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return ItemNull; }
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qadd(r, ma, mb, ctx, &status);
    mpd_del(ma); mpd_del(mb);
    return py_bigint_normalize(bigint_wrap(r));
}

Item py_bigint_sub(Item a, Item b) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return ItemNull; }
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qsub(r, ma, mb, ctx, &status);
    mpd_del(ma); mpd_del(mb);
    return py_bigint_normalize(bigint_wrap(r));
}

Item py_bigint_mul(Item a, Item b) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return ItemNull; }
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qmul(r, ma, mb, ctx, &status);
    mpd_del(ma); mpd_del(mb);
    return py_bigint_normalize(bigint_wrap(r));
}

Item py_bigint_floordiv(Item a, Item b) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return ItemNull; }
    if (mpd_iszero(mb)) {
        mpd_del(ma); mpd_del(mb);
        log_error("py-bigint: ZeroDivisionError: integer division by zero");
        return ItemNull;
    }
    // Python floor division = truncation toward -inf.
    // mpd_qdivint truncates toward zero (divides and discards fractional part).
    // Adjust: if remainder != 0 and sign(r) != sign(b), subtract 1.
    mpd_t* q = mpd_new(ctx);
    mpd_t* rem = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qdivmod(q, rem, ma, mb, ctx, &status);
    // adjust for floor semantics
    if (!mpd_iszero(rem) && (mpd_isnegative(rem) != mpd_isnegative(mb))) {
        mpd_qsub_ssize(q, q, 1, ctx, &status);
    }
    mpd_del(rem); mpd_del(ma); mpd_del(mb);
    return py_bigint_normalize(bigint_wrap(q));
}

Item py_bigint_mod(Item a, Item b) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return ItemNull; }
    if (mpd_iszero(mb)) {
        mpd_del(ma); mpd_del(mb);
        log_error("py-bigint: ZeroDivisionError: integer modulo by zero");
        return ItemNull;
    }
    // Python modulo: result has same sign as b.
    mpd_t* q = mpd_new(ctx);
    mpd_t* rem = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qdivmod(q, rem, ma, mb, ctx, &status);
    mpd_del(q);
    // adjust remainder sign to match divisor
    if (!mpd_iszero(rem) && (mpd_isnegative(rem) != mpd_isnegative(mb))) {
        mpd_qadd(rem, rem, mb, ctx, &status);
    }
    mpd_del(ma); mpd_del(mb);
    return py_bigint_normalize(bigint_wrap(rem));
}

Item py_bigint_pow(Item base_item, Item exp_item) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* mbase = to_mpd(base_item);
    mpd_t* mexp  = to_mpd(exp_item);
    if (!mbase || !mexp) {
        if (mbase) mpd_del(mbase); if (mexp) mpd_del(mexp); return ItemNull;
    }
    // negative exponents return float (Python semantics)
    if (mpd_isnegative(mexp)) {
        uint32_t st2 = 0;
        mpd_ssize_t bsz = mpd_qget_ssize(mbase, &st2);
        double b = (!st2 && bsz == 0) ? 0.0 : decimal_to_double(base_item);
        uint32_t st3 = 0;
        double e = (double)mpd_qget_ssize(mexp, &st3);
        mpd_del(mbase); mpd_del(mexp);
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!ptr) return ItemNull;
        *ptr = pow(b, e);
        return (Item){.item = d2it(ptr)};
    }
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qpow(r, mbase, mexp, ctx, &status);
    mpd_del(mbase); mpd_del(mexp);
    if (status & (MPD_Overflow | MPD_Invalid_operation)) {
        mpd_del(r);
        log_error("py-bigint: pow overflow or invalid");
        return ItemNull;
    }
    // quantize to integer (remove any fractional part from decimal representation)
    mpd_t* one = mpd_new(ctx);
    mpd_qset_ssize(one, 1, ctx, &status);
    mpd_t* r2 = mpd_new(ctx);
    mpd_qquantize(r2, r, one, ctx, &status);
    mpd_del(r); mpd_del(one);
    return py_bigint_normalize(bigint_wrap(r2));
}

Item py_bigint_neg(Item a) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    if (!ma) return ItemNull;
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qminus(r, ma, ctx, &status);
    mpd_del(ma);
    return py_bigint_normalize(bigint_wrap(r));
}

Item py_bigint_abs(Item a) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma = to_mpd(a);
    if (!ma) return ItemNull;
    mpd_t* r = mpd_new(ctx);
    uint32_t status = 0;
    mpd_qabs(r, ma, ctx, &status);
    mpd_del(ma);
    return py_bigint_normalize(bigint_wrap(r));
}

// ─────────────────────────────────────────────────────────────────────
// Bit shifts (via power-of-2 multiplication / floor division)
// ─────────────────────────────────────────────────────────────────────

Item py_bigint_lshift(Item a, Item n) {
    // a << n = a * 2^n
    int64_t shift = (get_type_id(n) == LMD_TYPE_INT) ? n.get_int56() : 0;
    if (shift < 0) {
        log_error("py-bigint: ValueError: negative shift count");
        return ItemNull;
    }
    if (shift == 0) return py_is_bigint(a) ? a : a;
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma   = to_mpd(a);
    mpd_t* two  = mpd_new(ctx);
    mpd_t* nmpd = mpd_new(ctx);
    mpd_t* pow2 = mpd_new(ctx);
    mpd_t* r    = mpd_new(ctx);
    if (!ma || !two || !nmpd || !pow2 || !r) {
        if (ma) mpd_del(ma); if (two) mpd_del(two);
        if (nmpd) mpd_del(nmpd); if (pow2) mpd_del(pow2); if (r) mpd_del(r);
        return ItemNull;
    }
    uint32_t status = 0;
    mpd_qset_ssize(two,  2,     ctx, &status);
    mpd_qset_ssize(nmpd, (mpd_ssize_t)shift, ctx, &status);
    mpd_qpow(pow2, two, nmpd,   ctx, &status);
    mpd_qmul(r, ma, pow2,       ctx, &status);
    mpd_del(ma); mpd_del(two); mpd_del(nmpd); mpd_del(pow2);
    return py_bigint_normalize(bigint_wrap(r));
}

Item py_bigint_rshift(Item a, Item n) {
    // a >> n = floor(a / 2^n) — arithmetic shift
    int64_t shift = (get_type_id(n) == LMD_TYPE_INT) ? n.get_int56() : 0;
    if (shift < 0) {
        log_error("py-bigint: ValueError: negative shift count");
        return ItemNull;
    }
    if (shift == 0) return a;
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* ma   = to_mpd(a);
    mpd_t* two  = mpd_new(ctx);
    mpd_t* nmpd = mpd_new(ctx);
    mpd_t* pow2 = mpd_new(ctx);
    mpd_t* q    = mpd_new(ctx);
    mpd_t* rem  = mpd_new(ctx);
    if (!ma || !two || !nmpd || !pow2 || !q || !rem) {
        if (ma) mpd_del(ma); if (two) mpd_del(two); if (nmpd) mpd_del(nmpd);
        if (pow2) mpd_del(pow2); if (q) mpd_del(q); if (rem) mpd_del(rem);
        return ItemNull;
    }
    uint32_t status = 0;
    mpd_qset_ssize(two,  2,     ctx, &status);
    mpd_qset_ssize(nmpd, (mpd_ssize_t)shift, ctx, &status);
    mpd_qpow(pow2, two, nmpd,   ctx, &status);
    // floor division ma / pow2
    mpd_qdivmod(q, rem, ma, pow2, ctx, &status);
    if (!mpd_iszero(rem) && (mpd_isnegative(rem) != mpd_isnegative(pow2))) {
        mpd_qsub_ssize(q, q, 1, ctx, &status);
    }
    mpd_del(rem); mpd_del(ma); mpd_del(two); mpd_del(nmpd); mpd_del(pow2);
    return py_bigint_normalize(bigint_wrap(q));
}

// ─────────────────────────────────────────────────────────────────────
// Comparison
// ─────────────────────────────────────────────────────────────────────

int py_bigint_cmp(Item a, Item b) {
    mpd_t* ma = to_mpd(a);
    mpd_t* mb = to_mpd(b);
    if (!ma || !mb) { if (ma) mpd_del(ma); if (mb) mpd_del(mb); return 0; }
    uint32_t status = 0;
    int result = mpd_qcmp(ma, mb, &status);
    mpd_del(ma); mpd_del(mb);
    return result;  // -1, 0, or +1
}

// ─────────────────────────────────────────────────────────────────────
// Value extraction
// ─────────────────────────────────────────────────────────────────────

int64_t py_bigint_to_int64(Item x) {
    mpd_context_t* ctx = bigint_ctx();
    mpd_t* m = to_mpd(x);
    if (!m) return 0;
    uint32_t status = 0;
    mpd_ssize_t v = mpd_qget_ssize(m, &status);
    mpd_del(m);
    return (int64_t)v;
}

double py_bigint_to_double(Item x) {
    Decimal* d = x.get_decimal();
    if (!d || !d->dec_val) return 0.0;
    return decimal_to_double(x);
}

// ─────────────────────────────────────────────────────────────────────
// String conversion helpers
// ─────────────────────────────────────────────────────────────────────

Item py_bigint_to_str_item(Item x) {
    Decimal* d = x.get_decimal();
    if (!d || !d->dec_val) return (Item){.item = s2it(heap_create_name("0"))};
    // mpd_to_sci gives decimal string (no scientific notation when fmt=0)
    char* str = mpd_to_sci(d->dec_val, 0);
    if (!str) return (Item){.item = s2it(heap_create_name("0"))};
    // remove any +/E notation — for pure integers mpd_to_sci gives exact string
    // we need to strip the exponent if present (e.g. "1E+100" → not ideal).
    // Use the engine context to get exact decimal string.
    mpd_context_t* ctx = bigint_ctx();
    char* exact = mpd_to_sci(d->dec_val, 1);  // eng notation flag=1 gives decimal
    if (!exact) {
        Item r = (Item){.item = s2it(heap_create_name(str))};
        decimal_free_string(str);
        return r;
    }
    // For large integers from mpd_qpow, result may use scientific notation.
    // To get exact digits: do a string-format via mpd_format with "%+u" style.
    // Simpler: use mpd_to_sci flag=0 which gives "1E+100" style... not ideal.
    // Better: since all bigints are integers, use mpd_to_sci which gives N digits.
    // mpd_to_sci(x, 0) produces "1E+100" for 10^100. We want "100...0" (101 digits).
    // Use snprintf approach: get the coefficient and exponent, build the string.
    // Cleanest: quantize to exponent=0 first (mpd_qrescale), then to_sci.
    mpd_t* quantized = mpd_new(ctx);
    mpd_t* zero_exp  = mpd_new(ctx);
    if (quantized && zero_exp) {
        uint32_t status = 0;
        mpd_qset_ssize(zero_exp, 1, ctx, &status);
        mpd_qquantize(quantized, d->dec_val, zero_exp, ctx, &status);
        decimal_free_string(exact);
        decimal_free_string(str);
        mpd_del(zero_exp);
        char* s2 = mpd_to_sci(quantized, 0);
        mpd_del(quantized);
        if (!s2) return (Item){.item = s2it(heap_create_name("0"))};
        Item r = (Item){.item = s2it(heap_create_name(s2))};
        decimal_free_string(s2);
        return r;
    }
    if (quantized) mpd_del(quantized);
    if (zero_exp)  mpd_del(zero_exp);
    Item r = (Item){.item = s2it(heap_create_name(exact))};
    decimal_free_string(exact);
    decimal_free_string(str);
    return r;
}

// Base-N conversion helper: converts absolute value of m to a digit string in given base.
// Digits are returned LSB-first in out[], returns count.
static int mpd_to_base(mpd_t* m, int base, char* out, int out_cap, mpd_context_t* ctx) {
    // if m == 0, return "0"
    if (mpd_iszero(m)) { out[0]='0'; return 1; }
    mpd_t* n     = mpd_new(ctx);
    mpd_t* bmpd  = mpd_new(ctx);
    mpd_t* q     = mpd_new(ctx);
    mpd_t* rem   = mpd_new(ctx);
    if (!n || !bmpd || !q || !rem) {
        if (n) mpd_del(n); if (bmpd) mpd_del(bmpd);
        if (q) mpd_del(q); if (rem) mpd_del(rem); return 0;
    }
    uint32_t status = 0;
    mpd_qabs(n, m, ctx, &status);
    mpd_qset_ssize(bmpd, base, ctx, &status);
    int count = 0;
    while (!mpd_iszero(n) && count < out_cap) {
        mpd_qdivmod(q, rem, n, bmpd, ctx, &status);
        int digit = (int)mpd_qget_ssize(rem, &status);
        if (digit < 0) digit = -digit;
        out[count++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        mpd_copy(n, q, ctx);
    }
    mpd_del(n); mpd_del(bmpd); mpd_del(q); mpd_del(rem);
    return count;
}

static Item bigint_to_base_item(Item x, int base, const char* prefix) {
    Decimal* d = x.get_decimal();
    if (!d || !d->dec_val) return (Item){.item = s2it(heap_create_name("0"))};
    mpd_context_t* ctx = bigint_ctx();
    bool neg = mpd_isnegative(d->dec_val) && !mpd_iszero(d->dec_val);
    char raw[PY_BIGINT_PREC + 64];
    int count = mpd_to_base(d->dec_val, base, raw, sizeof(raw) - 4, ctx);
    if (count == 0) return (Item){.item = s2it(heap_create_name("0"))};
    // build result: [-]prefix + reversed(raw)
    StrBuf* sb = strbuf_new();
    if (neg) strbuf_append_char(sb, '-');
    strbuf_append_str(sb, prefix);
    for (int i = count - 1; i >= 0; i--) strbuf_append_char(sb, raw[i]);
    Item r = (Item){.item = s2it(heap_create_name(sb->str ? sb->str : "0"))};
    strbuf_free(sb);
    return r;
}

Item py_bigint_to_hex_item(Item x) { return bigint_to_base_item(x, 16, "0x"); }
Item py_bigint_to_oct_item(Item x) { return bigint_to_base_item(x, 8,  "0o"); }
Item py_bigint_to_bin_item(Item x) { return bigint_to_base_item(x, 2,  "0b"); }
