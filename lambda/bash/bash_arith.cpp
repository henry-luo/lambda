// bash_arith.cpp — Runtime String-Based Arithmetic Evaluator (Phase C — Module 6)
//
// Recursive descent parser/evaluator for Bash arithmetic expressions.
// Operator precedence (low to high):
//   , (comma)  →  = += -= *= /= %= <<= >>= &= ^= |=  →  ?: →
//   || → && → | → ^ → & → == != → < <= > >= → << >> → + - → * / % → ** → unary(- + ~ !) → ++ --

#include "bash_arith.h"
#include "bash_runtime.h"
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"

#include <cstring>
#include "../../lib/mem.h"
#include <cctype>

// ============================================================================
// Evaluator state
// ============================================================================

struct ArithParser {
    const char* src;     // source expression
    int pos;             // current position
    int len;             // total length
    int error;           // 0 = ok, 1 = error
    const char* errmsg;  // error message (static string or points into src)
};

static int arith_last_error = 0;
static const char* arith_last_errmsg = "";

// ============================================================================
// Lexer helpers
// ============================================================================

static void arith_skip_spaces(ArithParser* p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos]))
        p->pos++;
}

static bool arith_at_end(ArithParser* p) {
    arith_skip_spaces(p);
    return p->pos >= p->len;
}

static char arith_peek(ArithParser* p) {
    arith_skip_spaces(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char arith_peek2(ArithParser* p) {
    arith_skip_spaces(p);
    if (p->pos + 1 >= p->len) return '\0';
    return p->src[p->pos + 1];
}

static bool arith_match(ArithParser* p, char c) {
    arith_skip_spaces(p);
    if (p->pos < p->len && p->src[p->pos] == c) {
        p->pos++;
        return true;
    }
    return false;
}

static bool arith_match2(ArithParser* p, char c1, char c2) {
    arith_skip_spaces(p);
    if (p->pos + 1 < p->len && p->src[p->pos] == c1 && p->src[p->pos + 1] == c2) {
        p->pos += 2;
        return true;
    }
    return false;
}

static void arith_error(ArithParser* p, const char* msg) {
    if (!p->error) {
        p->error = 1;
        p->errmsg = msg;
        log_debug("bash_arith: error: %s at pos %d in '%s'", msg, p->pos, p->src);
    }
}

// ============================================================================
// Variable resolution
// ============================================================================

static long long arith_get_var(const char* name, int name_len) {
    // look up variable value and coerce to integer
    char buf[256];
    int n = name_len < (int)sizeof(buf) - 1 ? name_len : (int)sizeof(buf) - 1;
    memcpy(buf, name, n);
    buf[n] = '\0';

    Item name_item = (Item){.item = s2it(heap_create_name(buf))};
    Item val = bash_get_var(name_item);
    return (long long)bash_to_int_val(val);
}

static void arith_set_var(const char* name, int name_len, long long value) {
    char buf[256];
    int n = name_len < (int)sizeof(buf) - 1 ? name_len : (int)sizeof(buf) - 1;
    memcpy(buf, name, n);
    buf[n] = '\0';

    Item name_item = (Item){.item = s2it(heap_create_name(buf))};
    Item val_item = (Item){.item = i2it(value)};
    bash_set_var(name_item, val_item);
}

// ============================================================================
// Forward declarations for recursive descent
// ============================================================================

static long long arith_expr(ArithParser* p);
static long long arith_assign(ArithParser* p);
static long long arith_ternary(ArithParser* p);
static long long arith_logical_or(ArithParser* p);
static long long arith_logical_and(ArithParser* p);
static long long arith_bitwise_or(ArithParser* p);
static long long arith_bitwise_xor(ArithParser* p);
static long long arith_bitwise_and(ArithParser* p);
static long long arith_equality(ArithParser* p);
static long long arith_relational(ArithParser* p);
static long long arith_shift(ArithParser* p);
static long long arith_additive(ArithParser* p);
static long long arith_multiplicative(ArithParser* p);
static long long arith_exponent(ArithParser* p);
static long long arith_unary(ArithParser* p);
static long long arith_postfix(ArithParser* p);
static long long arith_primary(ArithParser* p);

// ============================================================================
// Parse a variable name, return start and length
// ============================================================================

static bool arith_parse_varname(ArithParser* p, int* out_start, int* out_len) {
    arith_skip_spaces(p);
    if (p->pos >= p->len) return false;
    // skip optional $ prefix
    int start = p->pos;
    if (p->src[p->pos] == '$') p->pos++;
    if (p->pos >= p->len) { p->pos = start; return false; }
    char c = p->src[p->pos];
    if (!isalpha((unsigned char)c) && c != '_') { p->pos = start; return false; }
    int name_start = p->pos;
    while (p->pos < p->len && (isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_'))
        p->pos++;
    *out_start = name_start;
    *out_len = p->pos - name_start;
    return true;
}

// ============================================================================
// Primary: number, variable, (expr)
// ============================================================================

static long long arith_primary(ArithParser* p) {
    if (p->error) return 0;
    arith_skip_spaces(p);
    if (p->pos >= p->len) { arith_error(p, "unexpected end of expression"); return 0; }

    char c = p->src[p->pos];

    // parenthesized expression
    if (c == '(') {
        p->pos++;
        long long val = arith_expr(p);
        if (!arith_match(p, ')')) arith_error(p, "expected ')'");
        return val;
    }

    // number: decimal, hex (0x), octal (0), or base#val
    if (isdigit((unsigned char)c)) {
        char* end = NULL;
        long long val;
        // check for base#val: e.g., 16#ff
        int saved_pos = p->pos;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
        if (p->pos < p->len && p->src[p->pos] == '#') {
            // base#value notation
            int base = (int)strtol(p->src + saved_pos, NULL, 10);
            if (base < 2 || base > 64) base = 10;
            p->pos++; // skip '#'
            int val_start = p->pos;
            while (p->pos < p->len && (isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '@' || p->src[p->pos] == '_'))
                p->pos++;
            // parse value in given base
            char buf[128];
            int n = p->pos - val_start;
            if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
            memcpy(buf, p->src + val_start, n);
            buf[n] = '\0';
            val = strtoll(buf, NULL, base <= 36 ? base : 36);
            return val;
        }
        // restore and parse normally
        p->pos = saved_pos;
        if (c == '0' && p->pos + 1 < p->len &&
            (p->src[p->pos + 1] == 'x' || p->src[p->pos + 1] == 'X')) {
            val = strtoll(p->src + p->pos, &end, 16);
        } else if (c == '0' && p->pos + 1 < p->len && isdigit((unsigned char)p->src[p->pos + 1])) {
            val = strtoll(p->src + p->pos, &end, 8);
        } else {
            val = strtoll(p->src + p->pos, &end, 10);
        }
        if (end) p->pos = (int)(end - p->src);
        return val;
    }

    // variable or $variable
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        int name_start, name_len;
        if (arith_parse_varname(p, &name_start, &name_len)) {
            return arith_get_var(p->src + name_start, name_len);
        }
    }

    arith_error(p, "unexpected character in arithmetic expression");
    return 0;
}

// ============================================================================
// Postfix: primary (++ | --)
// ============================================================================

static long long arith_postfix(ArithParser* p) {
    if (p->error) return 0;
    arith_skip_spaces(p);

    // check if this is a variable followed by ++ or --
    int saved_pos = p->pos;
    int name_start, name_len;
    if (arith_parse_varname(p, &name_start, &name_len)) {
        arith_skip_spaces(p);
        if (arith_match2(p, '+', '+')) {
            long long old_val = arith_get_var(p->src + name_start, name_len);
            arith_set_var(p->src + name_start, name_len, old_val + 1);
            return old_val; // postfix: return old value
        }
        if (arith_match2(p, '-', '-')) {
            long long old_val = arith_get_var(p->src + name_start, name_len);
            arith_set_var(p->src + name_start, name_len, old_val - 1);
            return old_val; // postfix: return old value
        }
        // not postfix — backtrack and let primary handle it
        p->pos = saved_pos;
    } else {
        p->pos = saved_pos;
    }

    return arith_primary(p);
}

// ============================================================================
// Unary: (- | + | ~ | ! | ++ | --) unary
// ============================================================================

static long long arith_unary(ArithParser* p) {
    if (p->error) return 0;
    arith_skip_spaces(p);

    // prefix ++ and --
    if (arith_match2(p, '+', '+')) {
        int name_start, name_len;
        int saved = p->pos;
        if (arith_parse_varname(p, &name_start, &name_len)) {
            long long val = arith_get_var(p->src + name_start, name_len) + 1;
            arith_set_var(p->src + name_start, name_len, val);
            return val; // prefix: return new value
        }
        p->pos = saved;
        arith_error(p, "++ requires variable operand");
        return 0;
    }
    if (arith_match2(p, '-', '-')) {
        int name_start, name_len;
        int saved = p->pos;
        if (arith_parse_varname(p, &name_start, &name_len)) {
            long long val = arith_get_var(p->src + name_start, name_len) - 1;
            arith_set_var(p->src + name_start, name_len, val);
            return val;
        }
        p->pos = saved;
        arith_error(p, "-- requires variable operand");
        return 0;
    }

    char c = arith_peek(p);
    if (c == '-' && arith_peek2(p) != '-') {
        p->pos++; return -arith_unary(p);
    }
    if (c == '+' && arith_peek2(p) != '+') {
        p->pos++; return arith_unary(p);
    }
    if (c == '~') { p->pos++; return ~arith_unary(p); }
    if (c == '!') {
        // make sure it's not != 
        if (p->pos + 1 < p->len && p->src[p->pos + 1] == '=') {
            // not logical not — fall through to postfix
        } else {
            p->pos++;
            return arith_unary(p) == 0 ? 1 : 0;
        }
    }

    return arith_postfix(p);
}

// ============================================================================
// Exponentiation: unary (** unary)*  (right-associative)
// ============================================================================

static long long arith_exponent(ArithParser* p) {
    long long left = arith_unary(p);
    if (p->error) return left;
    arith_skip_spaces(p);
    if (arith_match2(p, '*', '*')) {
        long long exp = arith_exponent(p); // right-associative
        if (exp < 0) return 0;
        long long result = 1;
        for (long long i = 0; i < exp; i++) result *= left;
        return result;
    }
    return left;
}

// ============================================================================
// Multiplicative: exponent ((* | / | %) exponent)*
// ============================================================================

static long long arith_multiplicative(ArithParser* p) {
    long long left = arith_exponent(p);
    while (!p->error) {
        arith_skip_spaces(p);
        char c = arith_peek(p);
        if (c == '*' && arith_peek2(p) != '*' && arith_peek2(p) != '=') {
            p->pos++; left *= arith_exponent(p);
        } else if (c == '/' && arith_peek2(p) != '=') {
            p->pos++;
            long long right = arith_exponent(p);
            if (right == 0) {
                arith_error(p, "division by 0");
                bash_err_division_by_zero(p->src, 0);
                return 0;
            }
            left /= right;
        } else if (c == '%' && arith_peek2(p) != '=') {
            p->pos++;
            long long right = arith_exponent(p);
            if (right == 0) {
                arith_error(p, "division by 0");
                bash_err_division_by_zero(p->src, 0);
                return 0;
            }
            left %= right;
        } else break;
    }
    return left;
}

// ============================================================================
// Additive: multiplicative ((+ | -) multiplicative)*
// ============================================================================

static long long arith_additive(ArithParser* p) {
    long long left = arith_multiplicative(p);
    while (!p->error) {
        arith_skip_spaces(p);
        char c = arith_peek(p);
        if (c == '+' && arith_peek2(p) != '+' && arith_peek2(p) != '=') {
            p->pos++; left += arith_multiplicative(p);
        } else if (c == '-' && arith_peek2(p) != '-' && arith_peek2(p) != '=') {
            p->pos++; left -= arith_multiplicative(p);
        } else break;
    }
    return left;
}

// ============================================================================
// Shift: additive ((<< | >>) additive)*
// ============================================================================

static long long arith_shift(ArithParser* p) {
    long long left = arith_additive(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match2(p, '<', '<')) {
            // check it's not  <<=
            if (arith_peek(p) == '=') { p->pos -= 2; break; }
            left <<= arith_additive(p);
        } else if (arith_match2(p, '>', '>')) {
            if (arith_peek(p) == '=') { p->pos -= 2; break; }
            left >>= arith_additive(p);
        } else break;
    }
    return left;
}

// ============================================================================
// Relational: shift ((< | <= | > | >=) shift)*
// ============================================================================

static long long arith_relational(ArithParser* p) {
    long long left = arith_shift(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match2(p, '<', '=')) { left = (left <= arith_shift(p)) ? 1 : 0; }
        else if (arith_match2(p, '>', '=')) { left = (left >= arith_shift(p)) ? 1 : 0; }
        else if (arith_peek(p) == '<' && arith_peek2(p) != '<') {
            p->pos++; left = (left < arith_shift(p)) ? 1 : 0;
        }
        else if (arith_peek(p) == '>' && arith_peek2(p) != '>') {
            p->pos++; left = (left > arith_shift(p)) ? 1 : 0;
        }
        else break;
    }
    return left;
}

// ============================================================================
// Equality: relational ((== | !=) relational)*
// ============================================================================

static long long arith_equality(ArithParser* p) {
    long long left = arith_relational(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match2(p, '=', '=')) { left = (left == arith_relational(p)) ? 1 : 0; }
        else if (arith_match2(p, '!', '=')) { left = (left != arith_relational(p)) ? 1 : 0; }
        else break;
    }
    return left;
}

// ============================================================================
// Bitwise AND: equality (& equality)*
// ============================================================================

static long long arith_bitwise_and(ArithParser* p) {
    long long left = arith_equality(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_peek(p) == '&' && arith_peek2(p) != '&' && arith_peek2(p) != '=') {
            p->pos++; left &= arith_equality(p);
        } else break;
    }
    return left;
}

// ============================================================================
// Bitwise XOR: bitwise_and (^ bitwise_and)*
// ============================================================================

static long long arith_bitwise_xor(ArithParser* p) {
    long long left = arith_bitwise_and(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_peek(p) == '^' && arith_peek2(p) != '=') {
            p->pos++; left ^= arith_bitwise_and(p);
        } else break;
    }
    return left;
}

// ============================================================================
// Bitwise OR: bitwise_xor (| bitwise_xor)*
// ============================================================================

static long long arith_bitwise_or(ArithParser* p) {
    long long left = arith_bitwise_xor(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_peek(p) == '|' && arith_peek2(p) != '|' && arith_peek2(p) != '=') {
            p->pos++; left |= arith_bitwise_xor(p);
        } else break;
    }
    return left;
}

// ============================================================================
// Logical AND: bitwise_or (&& bitwise_or)*
// ============================================================================

static long long arith_logical_and(ArithParser* p) {
    long long left = arith_bitwise_or(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match2(p, '&', '&')) {
            if (left == 0) {
                // short-circuit: skip right, result is 0
                arith_bitwise_or(p); // still parse for side effects
                left = 0;
            } else {
                left = arith_bitwise_or(p) != 0 ? 1 : 0;
            }
        } else break;
    }
    return left;
}

// ============================================================================
// Logical OR: logical_and (|| logical_and)*
// ============================================================================

static long long arith_logical_or(ArithParser* p) {
    long long left = arith_logical_and(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match2(p, '|', '|')) {
            if (left != 0) {
                arith_logical_and(p); // still parse for side effects
                left = 1;
            } else {
                left = arith_logical_and(p) != 0 ? 1 : 0;
            }
        } else break;
    }
    return left;
}

// ============================================================================
// Ternary: logical_or (? expr : ternary)?
// ============================================================================

static long long arith_ternary(ArithParser* p) {
    long long cond = arith_logical_or(p);
    if (p->error) return cond;
    arith_skip_spaces(p);
    if (arith_match(p, '?')) {
        long long then_val = arith_expr(p);
        if (!arith_match(p, ':')) {
            arith_error(p, "expected ':' in ternary expression");
            return 0;
        }
        long long else_val = arith_ternary(p);
        return cond != 0 ? then_val : else_val;
    }
    return cond;
}

// ============================================================================
// Assignment: (var (= | += | -= | *= | /= | %= | <<= | >>= | &= | ^= | |=) assign) | ternary
// ============================================================================

static long long arith_assign(ArithParser* p) {
    if (p->error) return 0;
    arith_skip_spaces(p);

    // check if this is an assignment: varname followed by assignment operator
    int saved_pos = p->pos;
    int name_start, name_len;
    if (arith_parse_varname(p, &name_start, &name_len)) {
        arith_skip_spaces(p);
        // simple assignment =  (but not ==)
        if (p->pos < p->len && p->src[p->pos] == '=' &&
            (p->pos + 1 >= p->len || p->src[p->pos + 1] != '=')) {
            p->pos++;
            long long val = arith_assign(p); // right-associative
            arith_set_var(p->src + name_start, name_len, val);
            return val;
        }
        // compound assignments
        char c1 = (p->pos < p->len) ? p->src[p->pos] : '\0';
        char c2 = (p->pos + 1 < p->len) ? p->src[p->pos + 1] : '\0';

        int compound_op = 0; // 1=+=, 2=-=, 3=*=, 4=/=, 5=%=, 6=<<=, 7=>>=, 8=&=, 9=^=, 10=|=
        if (c1 == '+' && c2 == '=') { compound_op = 1; p->pos += 2; }
        else if (c1 == '-' && c2 == '=') { compound_op = 2; p->pos += 2; }
        else if (c1 == '*' && c2 == '=') { compound_op = 3; p->pos += 2; }
        else if (c1 == '/' && c2 == '=') { compound_op = 4; p->pos += 2; }
        else if (c1 == '%' && c2 == '=') { compound_op = 5; p->pos += 2; }
        else if (c1 == '&' && c2 == '=') { compound_op = 8; p->pos += 2; }
        else if (c1 == '^' && c2 == '=') { compound_op = 9; p->pos += 2; }
        else if (c1 == '|' && c2 == '=') { compound_op = 10; p->pos += 2; }
        else if (c1 == '<' && c2 == '<' && p->pos + 2 < p->len && p->src[p->pos + 2] == '=') {
            compound_op = 6; p->pos += 3;
        }
        else if (c1 == '>' && c2 == '>' && p->pos + 2 < p->len && p->src[p->pos + 2] == '=') {
            compound_op = 7; p->pos += 3;
        }

        if (compound_op > 0) {
            long long cur = arith_get_var(p->src + name_start, name_len);
            long long rhs = arith_assign(p);
            long long result = cur;
            switch (compound_op) {
            case 1: result = cur + rhs; break;
            case 2: result = cur - rhs; break;
            case 3: result = cur * rhs; break;
            case 4: result = rhs != 0 ? cur / rhs : 0; break;
            case 5: result = rhs != 0 ? cur % rhs : 0; break;
            case 6: result = cur << rhs; break;
            case 7: result = cur >> rhs; break;
            case 8: result = cur & rhs; break;
            case 9: result = cur ^ rhs; break;
            case 10: result = cur | rhs; break;
            }
            arith_set_var(p->src + name_start, name_len, result);
            return result;
        }

        // not an assignment — backtrack
        p->pos = saved_pos;
    } else {
        p->pos = saved_pos;
    }

    return arith_ternary(p);
}

// ============================================================================
// Expression: comma-separated assignments
// ============================================================================

static long long arith_expr(ArithParser* p) {
    long long val = arith_assign(p);
    while (!p->error) {
        arith_skip_spaces(p);
        if (arith_match(p, ',')) {
            val = arith_assign(p); // comma: evaluate both, return last
        } else break;
    }
    return val;
}

// ============================================================================
// Public API
// ============================================================================

extern "C" Item bash_arith_eval_string(Item expr_item) {
    String* s = it2s(bash_to_string(expr_item));
    if (!s || s->len == 0) return (Item){.item = i2it(0)};

    ArithParser parser;
    parser.src = s->chars;
    parser.pos = 0;
    parser.len = s->len;
    parser.error = 0;
    parser.errmsg = "";

    long long result = arith_expr(&parser);

    arith_last_error = parser.error;
    arith_last_errmsg = parser.errmsg;

    if (parser.error) {
        bash_errmsg_at("%s: syntax error in expression (error token is \"%s\")",
                       s->chars, s->chars + parser.pos);
        bash_set_exit_code(1);
        return (Item){.item = i2it(0)};
    }

    return (Item){.item = i2it(result)};
}

extern "C" long long bash_arith_eval_to_int(const char* expr) {
    if (!expr || !*expr) return 0;

    ArithParser parser;
    parser.src = expr;
    parser.pos = 0;
    parser.len = (int)strlen(expr);
    parser.error = 0;
    parser.errmsg = "";

    long long result = arith_expr(&parser);

    arith_last_error = parser.error;
    arith_last_errmsg = parser.errmsg;

    return parser.error ? 0 : result;
}

extern "C" int bash_arith_get_error(void) {
    return arith_last_error;
}

extern "C" const char* bash_arith_get_error_msg(void) {
    return arith_last_errmsg;
}
