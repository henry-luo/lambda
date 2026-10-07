// TeX expansion engine: arithmetic, printing and scanning (TeX82 §§54-110,
// 402-480 and the e-TeX expression scanner).
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "../../lib/str.h"
#include <stdio.h>
#include <stdlib.h>

namespace tex {

static const int32_t UNITY = 65536;
static const int32_t MAX_DIMEN = 07777777777;     // 2^30 - 1
static const int32_t INFINITY_INT = 017777777777; // 2^31 - 1

const Glue ZERO_GLUE = {0, 0, 0, 0, 0};

const Glue* new_glue(Engine* e, int32_t width, int32_t stretch, uint8_t so, int32_t shrink, uint8_t sho) {
    Glue* g = (Glue*)arena_alloc(e->arena, sizeof(Glue));
    g->width = width;
    g->stretch = stretch;
    g->stretch_order = stretch ? so : 0;
    g->shrink = shrink;
    g->shrink_order = shrink ? sho : 0;
    return g;
}

// ======================================================================
// arithmetic (TeX §§99-108, e-TeX)
// ======================================================================

static int32_t round_decimals(const uint8_t* dig, int k) {
    int32_t a = 0;
    while (k > 0) {
        k--;
        a = (a + dig[k] * 131072) / 10;
    }
    return (a + 1) / 2;
}

int32_t xn_over_d(Engine* e, int32_t x, int32_t n, int32_t d, int32_t* remainder) {
    bool positive = x >= 0;
    int64_t ax = positive ? (int64_t)x : -(int64_t)x;
    int64_t prod = ax * (int64_t)n;
    int64_t q = prod / d;
    int64_t r = prod % d;
    // TeX §107: a quotient of 2^30 or more is an arithmetic error
    if (q >= ((int64_t)1 << 30)) {
        e->arith_error = true;
        q = 0;
    }
    if (remainder) *remainder = (int32_t)(positive ? r : -r);
    return (int32_t)(positive ? q : -q);
}

static int32_t mult_and_add(Engine* e, int32_t n, int32_t x, int32_t y, int32_t max_answer) {
    if (n < 0) { x = -x; n = -n; }
    if (n == 0) return y;
    if (x <= (max_answer - y) / n && -x <= (max_answer + y) / n) return n * x + y;
    e->arith_error = true;
    return 0;
}

static int32_t nx_plus_y(Engine* e, int32_t n, int32_t x, int32_t y) {
    return mult_and_add(e, n, x, y, MAX_DIMEN);
}

static int32_t mult_integers(Engine* e, int32_t n, int32_t x) {
    return mult_and_add(e, n, x, 0, INFINITY_INT);
}

// e-TeX quotient: n/d rounded, halves away from zero
static int32_t quotient(Engine* e, int32_t n, int32_t d) {
    if (d == 0) { e->arith_error = true; return 0; }
    bool negative = false;
    int64_t nn = n, dd = d;
    if (dd < 0) { dd = -dd; negative = true; }
    if (nn < 0) { nn = -nn; negative = !negative; }
    int64_t a = nn / dd;
    int64_t r = nn - a * dd;
    if (2 * r >= dd) a++;
    return (int32_t)(negative ? -a : a);
}

// e-TeX fract: floor(x*n/d + 1/2) with signs, or arith_error past max_answer
static int32_t fract(Engine* e, int32_t x, int32_t n, int32_t d, int32_t max_answer) {
    if (d == 0) { e->arith_error = true; return 0; }
    bool negative = false;
    uint64_t ax, an, ad;
    if (d < 0) { negative = true; ad = (uint64_t)(-(int64_t)d); } else ad = (uint64_t)d;
    if (x < 0) { negative = !negative; ax = (uint64_t)(-(int64_t)x); } else ax = (uint64_t)x;
    if (n < 0) { negative = !negative; an = (uint64_t)(-(int64_t)n); } else an = (uint64_t)n;
    if (ax == 0 || an == 0) return 0;
    uint64_t q = (2 * ax * an + ad) / (2 * ad);
    if (q > (uint64_t)max_answer) { e->arith_error = true; return 0; }
    return negative ? -(int32_t)q : (int32_t)q;
}

static int32_t add_or_sub(Engine* e, int32_t x, int32_t y, int32_t max_answer, bool negative) {
    if (negative) y = -y;
    if (x >= 0) {
        if (y <= max_answer - x) return x + y;
    } else if (y >= -max_answer - x) {
        return x + y;
    }
    e->arith_error = true;
    return 0;
}

// ======================================================================
// printing (TeX §§54-71, 103, 178, 262, 292-298)
// ======================================================================

void print_int(int32_t v, StrBuf* out) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    strbuf_append_str(out, buf);
}

void print_roman(int32_t n, StrBuf* out) {
    static const char* pool = "m2d5c2l5x2v5i";
    int j = 0;
    int32_t v = 1000;
    for (;;) {
        while (n >= v) {
            strbuf_append_char(out, pool[j]);
            n -= v;
        }
        if (n <= 0) return;
        int k = j + 2;
        int32_t u = v / (pool[k - 1] - '0');
        if (pool[k - 1] == '2') {
            k += 2;
            u = u / (pool[k - 1] - '0');
        }
        if (n + u >= v) {
            strbuf_append_char(out, pool[k]);
            n += u;
        } else {
            j += 2;
            v = v / (pool[j - 1] - '0');
        }
    }
}

void print_scaled(int32_t s, StrBuf* out) {
    if (s < 0) {
        strbuf_append_char(out, '-');
        s = -s;
    }
    print_int(s / UNITY, out);
    strbuf_append_char(out, '.');
    s = 10 * (s % UNITY) + 5;
    int32_t delta = 10;
    do {
        if (delta > UNITY) s = s + 0100000 - 50000;  // round the last digit
        strbuf_append_char(out, (char)('0' + s / UNITY));
        s = 10 * (s % UNITY);
        delta *= 10;
    } while (s > delta);
}

static void print_glue_part(int32_t d, uint8_t order, const char* unit, StrBuf* out) {
    print_scaled(d, out);
    if (order > 3) {
        strbuf_append_str(out, "foul");
    } else if (order > 0) {
        strbuf_append_str(out, "fil");
        for (uint8_t o = order; o > 1; o--) strbuf_append_char(out, 'l');
    } else if (unit) {
        strbuf_append_str(out, unit);
    }
}

void print_glue(const Glue* g, const char* unit, StrBuf* out) {
    print_glue_part(g->width, 0, unit, out);
    if (g->stretch != 0) {
        strbuf_append_str(out, " plus ");
        print_glue_part(g->stretch, g->stretch_order, unit, out);
    }
    if (g->shrink != 0) {
        strbuf_append_str(out, " minus ");
        print_glue_part(g->shrink, g->shrink_order, unit, out);
    }
}

static void print_esc_char(Engine* e, StrBuf* out) {
    int32_t esc = int_par(e, IP_ESCAPE_CHAR);
    if (esc >= 0 && esc <= 0x10ffff) strbuf_append_utf8(out, (uint32_t)esc);
}

void print_cs(Engine* e, uint32_t cs, StrBuf* out, bool trailing_space) {
    const NameRec& rec = name_of(e, cs);
    uint32_t code = 0;
    if (name_is_active(rec, &code)) {
        strbuf_append_utf8(out, code);
        return;
    }
    if (rec.len == 0) {
        print_esc_char(e, out);
        strbuf_append_str(out, "csname");
        print_esc_char(e, out);
        strbuf_append_str(out, "endcsname");
        if (trailing_space) strbuf_append_char(out, ' ');
        return;
    }
    print_esc_char(e, out);
    strbuf_append_str_n(out, rec.chars, rec.len);
    if (!trailing_space) return;
    uint32_t cp = 0;
    int n = str_utf8_decode(rec.chars, rec.len, &cp);
    bool single = n > 0 && (uint32_t)n == rec.len;
    // TeX's print_cs: a space follows a control word, or a letter control symbol
    if (!single || get_code(e, CT_CAT, cp) == CAT_LETTER) strbuf_append_char(out, ' ');
}

void tokens_to_str(Engine* e, TokSpan list, StrBuf* out, bool show_params) {
    (void)show_params;
    uint32_t match_chr = '#';
    for (uint32_t i = 0; i < list.count; i++) {
        const Token& t = list.data[i];
        switch (t.kind) {
        case TK_CS: print_cs(e, t.value, out, true); break;
        case TK_CHAR:
            strbuf_append_utf8(out, t.value);
            if (t.cat == CAT_PARAM) strbuf_append_utf8(out, t.value);
            break;
        case TK_PARAM:
            strbuf_append_utf8(out, match_chr);
            strbuf_append_char(out, (char)('0' + t.value));
            break;
        case TK_MATCH:
            strbuf_append_utf8(out, match_chr);
            strbuf_append_char(out, (char)('0' + t.value));
            break;
        case TK_END_MATCH:
            if (t.value == 1) strbuf_append_char(out, '{');
            strbuf_append_str(out, "->");
            break;
        }
    }
}

static void print_hex(int32_t v, StrBuf* out) {
    char buf[16];
    snprintf(buf, sizeof(buf), "\"%X", (unsigned)v);
    strbuf_append_str(out, buf);
}

static const char* REG_NAMES[] = {"count", "dimen", "skip", "muskip", "toks", "box"};

void print_meaning(Engine* e, const Meaning& m, StrBuf* out, const Token* tok) {
    (void)tok;
    switch (m.type) {
    case MT_UNDEFINED: strbuf_append_str(out, "undefined"); return;
    case MT_CONSTRUCTOR:
        print_esc_char(e, out);
        strbuf_append_str(out, "protected macro:->");
        print_cs(e, m.value, out, true);
        return;
    case MT_CHAR: {
        static const char* prefixes[16] = {
            "", "begin-group character ", "end-group character ", "math shift character ",
            "alignment tab character ", "", "macro parameter character ", "superscript character ",
            "subscript character ", "", "blank space ", "the letter ", "the character ", "", "", ""};
        strbuf_append_str(out, prefixes[m.cat & 15]);
        strbuf_append_utf8(out, m.value);
        return;
    }
    case MT_CHARDEF:
        print_esc_char(e, out);
        strbuf_append_str(out, "char");
        print_hex((int32_t)m.value, out);
        return;
    case MT_MATHCHARDEF:
        print_esc_char(e, out);
        strbuf_append_str(out, "mathchar");
        print_hex((int32_t)m.value, out);
        return;
    case MT_REGDEF:
        print_esc_char(e, out);
        strbuf_append_str(out, REG_NAMES[m.sub % 6]);
        print_int((int32_t)m.value, out);
        return;
    case MT_FONT: {
        const Font& f = e->fonts[m.value];
        strbuf_append_str(out, "select font ");
        strbuf_append_str(out, f.name);
        if (m.value > 0 && f.size != 10 * UNITY) {
            strbuf_append_str(out, " at ");
            print_scaled(f.size, out);
            strbuf_append_str(out, "pt");
        }
        return;
    }
    case MT_MACRO: {
        const Macro* mac = m.macro;
        if (mac->flags & MF_PROTECTED) { print_esc_char(e, out); strbuf_append_str(out, "protected"); }
        if (mac->flags & MF_LONG) { print_esc_char(e, out); strbuf_append_str(out, "long"); }
        if (mac->flags & MF_OUTER) { print_esc_char(e, out); strbuf_append_str(out, "outer"); }
        if (mac->flags & (MF_PROTECTED | MF_LONG | MF_OUTER)) strbuf_append_char(out, ' ');
        strbuf_append_str(out, "macro:");
        if (mac->flags & MF_LATEX_OPT) {
            strbuf_append_str(out, "[#1]");
            if (mac->opt_default.count) {
                strbuf_append_char(out, '{');
                tokens_to_str(e, mac->opt_default, out, true);
                strbuf_append_char(out, '}');
            }
        }
        tokens_to_str(e, mac->params, out, true);
        if (mac->params.count == 0 || mac->params.data[mac->params.count - 1].kind != TK_END_MATCH)
            strbuf_append_str(out, "->");
        tokens_to_str(e, mac->body, out, true);
        return;
    }
    case MT_PRIM:
        print_esc_char(e, out);
        strbuf_append_str(out, prim_name(e, m.value, m.sub));
        return;
    }
}

// ======================================================================
// scanning helpers (TeX §§403-409)
// ======================================================================

// TeX's cur_cmd=spacer: a space token or a cs \let to one (e.g. \@sptoken)
bool is_spacer(Engine* e, const Token& t) {
    if (t.kind == TK_CHAR) return t.cat == CAT_SPACE;
    if (t.flags & TF_NOEXPAND) return false;
    const Meaning& m = meaning_of(e, t.value);
    return m.type == MT_CHAR && m.cat == CAT_SPACE;
}

static bool is_other(const Token& t, uint32_t c) {
    return t.kind == TK_CHAR && t.cat == CAT_OTHER && t.value == c;
}

void skip_spaces_x(Engine* e, Token* t) {
    do {
        if (!get_x_token(e, t)) { *t = make_cs(e->cs_relax); return; }
    } while (is_spacer(e, *t));
}

void skip_spaces_and_relax(Engine* e, Token* t) {
    for (;;) {
        if (!get_x_token(e, t)) { *t = make_cs(e->cs_relax); return; }
        if (is_spacer(e, *t)) continue;
        Meaning m = token_meaning(e, *t);
        if (m.type == MT_PRIM && m.value == P_RELAX && t->kind == TK_CS) continue;
        return;
    }
}

bool scan_keyword(Engine* e, const char* keyword) {
    lam::ArrayList<Token> backup(MEM_CAT_INPUT_OTHER, 8);
    size_t k = 0;
    size_t len = strlen(keyword);
    Token t;
    while (k < len) {
        if (!get_x_token(e, &t)) break;
        char c = keyword[k];
        bool match = t.kind == TK_CHAR && (t.value == (uint32_t)(unsigned char)c ||
            t.value == (uint32_t)(unsigned char)(c - 'a' + 'A'));
        if (match) {
            backup.append(t);
            k++;
            continue;
        }
        if (!is_spacer(e, t) || backup.size() > 0) {
            back_input(e, t);
            if (backup.size()) back_list_copy(e, backup);
            return false;
        }
    }
    if (k < len) {
        if (backup.size()) back_list_copy(e, backup);
        return false;
    }
    return true;
}

void scan_optional_equals(Engine* e) {
    Token t;
    skip_spaces_x(e, &t);
    if (!is_other(t, '=')) back_input(e, t);
}

bool scan_left_brace(Engine* e) {
    Token t;
    skip_spaces_and_relax(e, &t);
    Meaning m = token_meaning(e, t);
    if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) return true;
    if (m.type == MT_CHAR && m.cat == CAT_BEGIN) return true;
    diag(e, "tex-missing-brace", "Missing { inserted");
    back_input(e, t);
    return false;
}

// ======================================================================
// internal quantities (TeX §§410-427)
// ======================================================================

int internal_level(Engine* e, const Meaning& m) {
    (void)e;
    switch (m.type) {
    case MT_CHARDEF: case MT_MATHCHARDEF: return LV_INT;
    case MT_FONT: return LV_IDENT;
    case MT_REGDEF:
        switch (m.sub) {
        case RK_COUNT: return LV_INT;
        case RK_DIMEN: return LV_DIMEN;
        case RK_SKIP: return LV_GLUE;
        case RK_MUSKIP: return LV_MU;
        case RK_TOKS: return LV_TOK;
        default: return -1;
        }
    case MT_PRIM: break;
    default: return -1;
    }
    switch (m.value) {
    case P_COUNT: case P_INT_PARAM: case P_CATCODE: case P_LCCODE: case P_UCCODE:
    case P_SFCODE: case P_MATHCODE: case P_DELCODE: case P_NUMEXPR: case P_GLUESTRETCHORDER:
    case P_GLUESHRINKORDER: case P_HYPHENCHAR: case P_SKEWCHAR: case P_PARSHAPE:
    case P_INTERLINEPENALTIES: case P_CLUBPENALTIES: case P_WIDOWPENALTIES:
    case P_DISPLAYWIDOWPENALTIES: case P_INTERACTIONMODE:
        return LV_INT;
    case P_READONLY_INT:
        switch (m.sub) {
        case RO_LAST_KERN: case RO_PREV_DEPTH: case RO_PAGE_GOAL: case RO_PARSHAPE_INDENT:
        case RO_PARSHAPE_DIMEN: return LV_DIMEN;
        case RO_LAST_SKIP: return LV_GLUE;
        default: return LV_INT;
        }
    case P_DIMEN: case P_DIMEN_PARAM: case P_DIMEXPR: case P_GLUESTRETCH: case P_GLUESHRINK:
    case P_FONTCHARWD: case P_FONTCHARHT: case P_FONTCHARDP: case P_FONTCHARIC:
    case P_WD: case P_HT: case P_DP: case P_FONTDIMEN: case P_PAGEDIMEN:
        return LV_DIMEN;
    case P_SKIP: case P_GLUE_PARAM: case P_GLUEEXPR: case P_MUTOGLUE: return LV_GLUE;
    case P_MUSKIP: case P_MU_GLUE_PARAM: case P_MUEXPR: case P_GLUETOMU: return LV_MU;
    case P_TOKS: case P_TOKS_PARAM: return LV_TOK;
    case P_FONT: case P_NULLFONT: case P_TEXTFONT: case P_SCRIPTFONT: case P_SCRIPTSCRIPTFONT:
        return LV_IDENT;
    default: return -1;
    }
}

int32_t scan_register_num(Engine* e) {
    int32_t n = scan_int(e);
    if (n < 0 || n >= (int32_t)REGISTER_LIMIT) {
        diag(e, "tex-bad-register", "Bad register code (%d)", n);
        return 0;
    }
    return n;
}

int32_t scan_char_num(Engine* e) {
    int32_t n = scan_int(e);
    if (n < 0 || n > 0x10ffff) {
        diag(e, "tex-bad-char", "Bad character code (%d)", n);
        return 0;
    }
    return n;
}

int32_t scan_font_ident(Engine* e) {
    Token t;
    skip_spaces_x(e, &t);
    Meaning m = token_meaning(e, t);
    if (m.type == MT_FONT) return (int32_t)m.value;
    if (m.type == MT_PRIM) {
        if (m.value == P_NULLFONT) return 0;
        if (m.value == P_FONT) return e->cur_font.v;
        if (m.value == P_TEXTFONT || m.value == P_SCRIPTFONT || m.value == P_SCRIPTSCRIPTFONT) {
            int32_t fam = scan_int(e) & 15;
            int size = m.value == P_TEXTFONT ? 0 : m.value == P_SCRIPTFONT ? 1 : 2;
            return e->fam_fonts[size][fam].v;
        }
    }
    diag(e, "tex-missing-font", "Missing font identifier");
    back_input(e, t);
    return 0;
}

// TeX's font_mem_size in TeX Live; expl3 stores its int and fp arrays as font parameters
static const uint32_t FONT_MEM_LIMIT = 8000000;

// TeX's find_font_dimen: the index of \fontdimen n in font_params, or -1 after an error;
// reading or writing past the end grows the most recently loaded font only
int64_t font_dimen_index(Engine* e, int32_t f, int32_t n) {
    Font& font = e->fonts[(size_t)f];
    if (n > font.param_count && n > 0 && (size_t)f == e->fonts.size() - 1) {
        if (font.param_base + (uint32_t)n > FONT_MEM_LIMIT) {
            diag(e, "tex-capacity", "font memory exceeded");
            return -1;
        }
        while (font.param_count < n) { e->font_params.append(0); font.param_count++; }
    }
    if (n <= 0 || n > font.param_count) {
        diag(e, "tex-font-params", "Font %s has only %d fontdimen parameters", font.name, font.param_count);
        return -1;
    }
    return (int64_t)font.param_base + n - 1;
}

// a parameter without growing or reporting; 0 when the font lacks it
int32_t font_param(Engine* e, int32_t f, int32_t n) {
    const Font& font = e->fonts[(size_t)f];
    return n >= 1 && n <= font.param_count ? e->font_params[font.param_base + (uint32_t)n - 1] : 0;
}

static void report_box_metrics(Engine* e) {
    if (e->box_metrics_reported) return;
    e->box_metrics_reported = true;
    diag(e, "approximate-box-metrics",
         "box dimensions are approximate: TeX's typesetting is not reproduced");
}

static int32_t scan_expr_value(Engine* e, int level, const Glue** gout);

Quantity scan_something_internal(Engine* e, const Token& t, const Meaning& m, int level, bool negative) {
    Quantity q = {};
    q.level = LV_INT;
    int found = internal_level(e, m);
    if (found < 0) {
        diag(e, "tex-cant-use", "You can't use that token after \\the or in a number");
        q.level = level != LV_TOK ? LV_DIMEN : LV_INT;
        q.i = 0;
        (void)t;
        return q;
    }
    if (found >= LV_IDENT && level != LV_TOK) {
        // TeX §415: a token list or font where a number is wanted
        diag(e, "tex-missing-number", "Missing number, treated as zero");
        back_input(e, t);
        q.level = level < LV_DIMEN ? level : LV_DIMEN;
        q.i = 0;
        return q;
    }
    q.level = found;
    switch (m.type) {
    case MT_CHARDEF: case MT_MATHCHARDEF: q.i = (int32_t)m.value; break;
    case MT_FONT: q.i = (int32_t)m.value; break;
    case MT_REGDEF: {
        RegSlot* slot = reg_slot(e, m.sub, m.value);
        if (m.sub == RK_COUNT || m.sub == RK_DIMEN) q.i = slot->v.i;
        else if (m.sub == RK_TOKS) q.t = slot->v.t;
        else q.g = slot->v.g ? slot->v.g : &ZERO_GLUE;
        break;
    }
    case MT_PRIM:
        switch (m.value) {
        case P_COUNT: case P_DIMEN: case P_SKIP: case P_MUSKIP: case P_TOKS: {
            uint8_t kind = m.value == P_COUNT ? RK_COUNT : m.value == P_DIMEN ? RK_DIMEN
                : m.value == P_SKIP ? RK_SKIP : m.value == P_MUSKIP ? RK_MUSKIP : RK_TOKS;
            RegSlot* slot = reg_slot(e, kind, (uint32_t)scan_register_num(e));
            if (kind == RK_COUNT || kind == RK_DIMEN) q.i = slot->v.i;
            else if (kind == RK_TOKS) q.t = slot->v.t;
            else q.g = slot->v.g ? slot->v.g : &ZERO_GLUE;
            break;
        }
        case P_INT_PARAM: q.i = e->int_pars[m.sub].v; break;
        case P_DIMEN_PARAM: q.i = e->dimen_pars[m.sub].v; break;
        case P_GLUE_PARAM: case P_MU_GLUE_PARAM:
            q.g = e->glue_pars[m.sub].v ? e->glue_pars[m.sub].v : &ZERO_GLUE;
            break;
        case P_TOKS_PARAM: q.t = e->toks_pars[m.sub].v; break;
        case P_CATCODE: q.i = get_code(e, CT_CAT, (uint32_t)scan_char_num(e)); break;
        case P_LCCODE: q.i = get_code(e, CT_LC, (uint32_t)scan_char_num(e)); break;
        case P_UCCODE: q.i = get_code(e, CT_UC, (uint32_t)scan_char_num(e)); break;
        case P_SFCODE: q.i = get_code(e, CT_SF, (uint32_t)scan_char_num(e)); break;
        case P_MATHCODE: q.i = get_code(e, CT_MATH, (uint32_t)scan_char_num(e)); break;
        case P_DELCODE: q.i = get_code(e, CT_DEL, (uint32_t)scan_char_num(e)); break;
        case P_READONLY_INT:
            switch (m.sub) {
            case RO_INPUT_LINE_NO: {
                FileReader* r = current_reader(e);
                q.i = r ? (int32_t)r->line_no : 0;
                break;
            }
            case RO_ETEX_VERSION: q.i = 2; break;
            case RO_CURRENT_GROUP_LEVEL: q.i = (int32_t)e->groups.size(); break;
            case RO_CURRENT_GROUP_TYPE: {
                uint8_t type = e->groups.empty() ? 0 : e->groups.back().type;
                q.i = type == GT_ENV ? GT_SEMI_SIMPLE : type;
                break;
            }
            case RO_CURRENT_IF_LEVEL: q.i = (int32_t)e->conds.size(); break;
            case RO_CURRENT_IF_TYPE: q.i = e->conds.empty() ? 0 : e->cur_if; break;
            case RO_CURRENT_IF_BRANCH: q.i = e->conds.empty() ? 0 : (e->if_limit == 3 ? 1 : 0); break;
            case RO_LAST_NODE_TYPE: q.i = -1; break;
            case RO_PDFTEX_VERSION: q.i = 140; break;
            case RO_RANDOM_SEED: q.i = (int32_t)(e->random_seed & 0x7fffffff); break;
            case RO_SPACE_FACTOR: q.i = 1000; break;
            case RO_PREV_DEPTH: q.i = -65536000; break;
            case RO_PAGE_GOAL: q.i = MAX_DIMEN; break;
            case RO_LAST_SKIP: q.g = &ZERO_GLUE; break;
            case RO_PARSHAPE_LENGTH: case RO_PARSHAPE_INDENT: case RO_PARSHAPE_DIMEN:
                scan_int(e);
                q.i = 0;
                break;
            default: q.i = 0; break;
            }
            break;
        case P_NUMEXPR: q.i = scan_expr_value(e, LV_INT, nullptr); break;
        case P_DIMEXPR: q.i = scan_expr_value(e, LV_DIMEN, nullptr); break;
        case P_GLUEEXPR: scan_expr_value(e, LV_GLUE, &q.g); break;
        case P_MUEXPR: scan_expr_value(e, LV_MU, &q.g); break;
        case P_GLUESTRETCH: case P_GLUESHRINK: case P_GLUESTRETCHORDER: case P_GLUESHRINKORDER: {
            const Glue* g = scan_glue(e, false);
            q.i = m.value == P_GLUESTRETCH ? g->stretch : m.value == P_GLUESHRINK ? g->shrink
                : m.value == P_GLUESTRETCHORDER ? g->stretch_order : g->shrink_order;
            break;
        }
        case P_GLUETOMU: q.g = scan_glue(e, false); break;
        case P_MUTOGLUE: q.g = scan_glue(e, true); break;
        case P_FONTCHARWD: case P_FONTCHARHT: case P_FONTCHARDP: case P_FONTCHARIC: {
            int32_t f = scan_font_ident(e);
            scan_char_num(e);
            int32_t quad = font_param(e, f, 6);
            q.i = m.value == P_FONTCHARWD ? quad / 2 : m.value == P_FONTCHARHT ? (quad * 7) / 10 : 0;
            report_box_metrics(e);
            break;
        }
        case P_WD: case P_HT: case P_DP: {
            const Box* box = reg_slot(e, RK_BOX, (uint32_t)scan_register_num(e))->v.b;
            q.i = !box ? 0 : m.value == P_WD ? box->width : m.value == P_HT ? box->height : box->depth;
            if (box) report_box_metrics(e);
            break;
        }
        case P_FONTDIMEN: {
            int32_t n = scan_int(e);
            int64_t k = font_dimen_index(e, scan_font_ident(e), n);
            q.i = k >= 0 ? e->font_params[(size_t)k] : 0;
            break;
        }
        case P_HYPHENCHAR: q.i = e->fonts[(size_t)scan_font_ident(e)].hyphen_char; break;
        case P_SKEWCHAR: q.i = e->fonts[(size_t)scan_font_ident(e)].skew_char; break;
        case P_FONT: q.i = e->cur_font.v; break;
        case P_NULLFONT: q.i = 0; break;
        case P_TEXTFONT: case P_SCRIPTFONT: case P_SCRIPTSCRIPTFONT: {
            int32_t fam = scan_int(e) & 15;
            int size = m.value == P_TEXTFONT ? 0 : m.value == P_SCRIPTFONT ? 1 : 2;
            q.i = e->fam_fonts[size][fam].v;
            break;
        }
        case P_PARSHAPE: q.i = 0; break;
        case P_INTERLINEPENALTIES: case P_CLUBPENALTIES: case P_WIDOWPENALTIES:
        case P_DISPLAYWIDOWPENALTIES:
            scan_int(e);
            q.i = 0;
            break;
        case P_INTERACTIONMODE: q.i = 1; break;
        case P_PAGEDIMEN: q.i = 0; break;
        default: q.i = 0; break;
        }
        break;
    default: break;
    }
    // coerce down to the wanted level (TeX §429)
    while (q.level > level) {
        if (q.level == LV_GLUE) q.i = q.g->width;
        else if (q.level == LV_MU) diag(e, "tex-mu-error", "Incompatible glue units");
        q.level--;
    }
    if (negative) {
        if (q.level == LV_GLUE || q.level == LV_MU) {
            q.g = new_glue(e, -q.g->width, -q.g->stretch, q.g->stretch_order, -q.g->shrink,
                           q.g->shrink_order);
        } else if (q.level <= LV_DIMEN) {
            q.i = -q.i;
        }
    }
    return q;
}

// ======================================================================
// numbers, dimensions, glue (TeX §§440-463)
// ======================================================================

static bool sign_and_first(Engine* e, Token* t) {
    bool negative = false;
    for (;;) {
        skip_spaces_x(e, t);
        if (is_other(*t, '-')) { negative = !negative; continue; }
        if (is_other(*t, '+')) continue;
        return negative;
    }
}

int32_t scan_int(Engine* e) {
    Token t;
    e->radix = 0;
    bool negative = sign_and_first(e, &t);
    int32_t val = 0;
    if (is_other(t, '`')) {
        Token c;
        if (!get_token(e, &c)) return 0;
        if (c.kind == TK_CHAR) {
            val = (int32_t)c.value;
        } else {
            const NameRec& rec = name_of(e, c.value);
            uint32_t code = 0;
            if (!name_is_active(rec, &code)) {
                int n = str_utf8_decode(rec.chars, rec.len, &code);
                if (n <= 0 || (uint32_t)n != rec.len) {
                    diag(e, "tex-improper-alphabetic", "Improper alphabetic constant");
                    back_input(e, c);
                    code = '0';
                    e->cur_tok = c;
                    return negative ? -(int32_t)code : (int32_t)code;
                }
            }
            val = (int32_t)code;
        }
        // scan an optional space
        Token s;
        if (get_x_token(e, &s) && !is_spacer(e, s)) back_input(e, s);
        e->cur_tok = s;
    } else {
        Meaning m = token_meaning(e, t);
        if (internal_level(e, m) >= 0) {
            Quantity q = scan_something_internal(e, t, m, LV_INT, false);
            val = q.i;
            e->cur_tok = t;
        } else {
            int radix = 10;
            int32_t limit = 214748364;
            if (is_other(t, '\'')) { radix = 8; limit = 02000000000; get_x_token(e, &t); }
            else if (is_other(t, '"')) { radix = 16; limit = 01000000000; get_x_token(e, &t); }
            e->radix = radix;
            bool vacuous = true;
            bool ok_so_far = true;
            for (;;) {
                int d;
                if (t.kind == TK_CHAR && t.cat == CAT_OTHER && t.value >= '0' && t.value <= '9' &&
                    (int)(t.value - '0') < radix) {
                    d = (int)(t.value - '0');
                } else if (radix == 16 && t.kind == TK_CHAR && (t.cat == CAT_LETTER || t.cat == CAT_OTHER) &&
                           t.value >= 'A' && t.value <= 'F') {
                    d = (int)(t.value - 'A' + 10);
                } else {
                    break;
                }
                vacuous = false;
                if (val >= limit && (val > limit || d > 7 || radix != 10)) {
                    if (ok_so_far) {
                        diag(e, "tex-number-too-big", "Number too big");
                        val = INFINITY_INT;
                        ok_so_far = false;
                    }
                } else {
                    val = val * radix + d;
                }
                if (!get_x_token(e, &t)) { t = make_cs(e->cs_relax); break; }
            }
            e->cur_tok = t;
            if (vacuous) {
                diag(e, "tex-missing-number", "Missing number, treated as zero");
                back_input(e, t);
            } else if (!is_spacer(e, t)) {
                back_input(e, t);
            }
        }
    }
    return negative ? -val : val;
}


int32_t scan_dimen(Engine* e, bool mu, bool inf, bool shortcut, int32_t shortcut_value) {
    int32_t f = 0;
    e->arith_error = false;
    e->cur_order = 0;
    bool negative = false;
    int32_t cur_val = 0;
    Token t;
    if (shortcut) {
        cur_val = shortcut_value;
    } else {
        negative = sign_and_first(e, &t);
        Meaning m = token_meaning(e, t);
        int lvl = internal_level(e, m);
        if (lvl >= 0) {
            if (mu) {
                Quantity q = scan_something_internal(e, t, m, LV_MU, false);
                cur_val = q.level >= LV_GLUE ? q.g->width : q.i;
                if (q.level == LV_MU) goto attach_sign;
                if (q.level != LV_INT) diag(e, "tex-mu-error", "Incompatible glue units");
            } else {
                Quantity q = scan_something_internal(e, t, m, LV_DIMEN, false);
                cur_val = q.i;
                if (q.level == LV_DIMEN) goto attach_sign;
            }
        } else {
            back_input(e, t);
            if (is_other(t, ',')) t = make_char('.', CAT_OTHER);
            if (!is_other(t, '.')) {
                cur_val = scan_int(e);
                t = e->cur_tok;
                if (is_other(t, ',')) t = make_char('.', CAT_OTHER);
            } else {
                e->radix = 10;
                cur_val = 0;
            }
            if (e->radix == 10 && is_other(t, '.')) {
                uint8_t dig[17];
                int k = 0;
                Token p;
                get_token(e, &p);  // the point being re-scanned
                for (;;) {
                    if (!get_x_token(e, &p)) { p = make_cs(e->cs_relax); break; }
                    if (!(p.kind == TK_CHAR && p.cat == CAT_OTHER && p.value >= '0' && p.value <= '9')) break;
                    if (k < 17) dig[k++] = (uint8_t)(p.value - '0');
                }
                f = round_decimals(dig, k);
                if (!is_spacer(e, p)) back_input(e, p);
            }
        }
    }
    if (cur_val < 0) {
        negative = !negative;
        cur_val = -cur_val;
    }
    if (inf && scan_keyword(e, "fil")) {
        e->cur_order = 1;
        while (scan_keyword(e, "l")) {
            if (e->cur_order == 3) diag(e, "tex-illegal-unit", "Illegal unit of measure (replaced by filll)");
            else e->cur_order++;
        }
        goto attach_fraction;
    }
    {
        int32_t save_cur_val = cur_val;
        int32_t v = 0;
        skip_spaces_x(e, &t);
        Meaning m = token_meaning(e, t);
        if (internal_level(e, m) < 0) {
            back_input(e, t);
            if (mu) goto not_found;
            if (scan_keyword(e, "em")) v = font_param(e, e->cur_font.v, 6);
            else if (scan_keyword(e, "ex")) v = font_param(e, e->cur_font.v, 5);
            else if (scan_keyword(e, "px")) v = e->dimen_pars[DP_PDF_PX_DIMEN].v;
            else goto not_found;
            {
                Token s;
                if (get_x_token(e, &s) && !is_spacer(e, s)) back_input(e, s);
            }
        } else {
            if (mu) {
                Quantity q = scan_something_internal(e, t, m, LV_MU, false);
                v = q.level >= LV_GLUE ? q.g->width : q.i;
                if (q.level != LV_MU) diag(e, "tex-mu-error", "Incompatible glue units");
            } else {
                Quantity q = scan_something_internal(e, t, m, LV_DIMEN, false);
                v = q.i;
            }
        }
        cur_val = nx_plus_y(e, save_cur_val, v, xn_over_d(e, v, f, UNITY, nullptr));
        goto attach_sign;
    }
not_found:
    if (mu) {
        if (!scan_keyword(e, "mu")) diag(e, "tex-illegal-unit", "Illegal unit of measure (mu inserted)");
        goto attach_fraction;
    }
    if (scan_keyword(e, "true")) {
        int32_t mag = int_par(e, IP_MAG);
        if (mag <= 0 || mag > 32768) mag = 1000;
        if (mag != 1000) {
            int32_t rem = 0;
            cur_val = xn_over_d(e, cur_val, 1000, mag, &rem);
            f = (int32_t)(((int64_t)1000 * f + (int64_t)UNITY * rem) / mag);
            cur_val += f / UNITY;
            f = f % UNITY;
        }
    }
    if (scan_keyword(e, "pt")) goto attach_fraction;
    {
        int32_t num = 0, denom = 0;
        if (scan_keyword(e, "in")) { num = 7227; denom = 100; }
        else if (scan_keyword(e, "pc")) { num = 12; denom = 1; }
        else if (scan_keyword(e, "cm")) { num = 7227; denom = 254; }
        else if (scan_keyword(e, "mm")) { num = 7227; denom = 2540; }
        else if (scan_keyword(e, "bp")) { num = 7227; denom = 7200; }
        else if (scan_keyword(e, "dd")) { num = 1238; denom = 1157; }
        else if (scan_keyword(e, "cc")) { num = 14856; denom = 1157; }
        else if (scan_keyword(e, "nd")) { num = 685; denom = 642; }
        else if (scan_keyword(e, "nc")) { num = 1370; denom = 107; }
        else if (scan_keyword(e, "sp")) goto done;
        else {
            diag(e, "tex-illegal-unit", "Illegal unit of measure (pt inserted)");
            goto attach_fraction;
        }
        int32_t rem = 0;
        cur_val = xn_over_d(e, cur_val, num, denom, &rem);
        f = (int32_t)(((int64_t)num * f + (int64_t)UNITY * rem) / denom);
        cur_val += f / UNITY;
        f = f % UNITY;
    }
attach_fraction:
    if (cur_val >= 16384) e->arith_error = true;
    else cur_val = cur_val * UNITY + f;
done:
    {
        Token s;
        if (get_x_token(e, &s) && !is_spacer(e, s)) back_input(e, s);
    }
attach_sign:
    if (e->arith_error || cur_val >= 010000000000 || cur_val <= -010000000000) {
        diag(e, "tex-dimension-too-large", "Dimension too large");
        cur_val = MAX_DIMEN;
        e->arith_error = false;
    }
    return negative ? -cur_val : cur_val;
}

const Glue* scan_glue(Engine* e, bool mu) {
    Token t;
    bool negative = sign_and_first(e, &t);
    Meaning m = token_meaning(e, t);
    int32_t width = 0;
    if (internal_level(e, m) >= 0) {
        Quantity q = scan_something_internal(e, t, m, mu ? LV_MU : LV_GLUE, negative);
        if (q.level >= LV_GLUE) {
            if (q.level != (mu ? LV_MU : LV_GLUE)) diag(e, "tex-mu-error", "Incompatible glue units");
            return q.g;
        }
        if (q.level == LV_INT) width = scan_dimen(e, mu, false, true, q.i);
        else {
            if (mu) diag(e, "tex-mu-error", "Incompatible glue units");
            width = q.i;
        }
    } else {
        back_input(e, t);
        width = scan_dimen(e, mu, false, false, 0);
        if (negative) width = -width;
    }
    int32_t stretch = 0, shrink = 0;
    uint8_t so = 0, sho = 0;
    if (scan_keyword(e, "plus")) {
        stretch = scan_dimen(e, mu, true, false, 0);
        so = e->cur_order;
    }
    if (scan_keyword(e, "minus")) {
        shrink = scan_dimen(e, mu, true, false, 0);
        sho = e->cur_order;
    }
    return new_glue(e, width, stretch, so, shrink, sho);
}

// ======================================================================
// e-TeX expressions (\numexpr, \dimexpr, \glueexpr, \muexpr)
// ======================================================================

enum { EX_NONE = 0, EX_ADD = 1, EX_SUB = 2, EX_MULT = 3, EX_DIV = 4, EX_SCALE = 5 };

struct ExprFrame {
    int l, r, s;
    int32_t e, t, n;
    Glue eg, tg;
};

static Glue glue_scale(Engine* e, Glue g, int op, int32_t f, int32_t n) {
    int32_t* parts[3] = {&g.width, &g.stretch, &g.shrink};
    for (int i = 0; i < 3; i++) {
        if (op == EX_MULT) *parts[i] = nx_plus_y(e, *parts[i], f, 0);
        else if (op == EX_DIV) *parts[i] = quotient(e, *parts[i], f);
        else *parts[i] = fract(e, *parts[i], n, f, MAX_DIMEN);
    }
    if (g.stretch == 0) g.stretch_order = 0;
    if (g.shrink == 0) g.shrink_order = 0;
    return g;
}

static Glue glue_add(Engine* e, Glue a, Glue b, bool negative) {
    a.width = add_or_sub(e, a.width, b.width, MAX_DIMEN, negative);
    if (a.stretch_order == b.stretch_order) a.stretch = add_or_sub(e, a.stretch, b.stretch, MAX_DIMEN, negative);
    else if (a.stretch_order < b.stretch_order && b.stretch != 0) {
        a.stretch = negative ? -b.stretch : b.stretch;
        a.stretch_order = b.stretch_order;
    }
    if (a.shrink_order == b.shrink_order) a.shrink = add_or_sub(e, a.shrink, b.shrink, MAX_DIMEN, negative);
    else if (a.shrink_order < b.shrink_order && b.shrink != 0) {
        a.shrink = negative ? -b.shrink : b.shrink;
        a.shrink_order = b.shrink_order;
    }
    if (a.stretch == 0) a.stretch_order = 0;
    if (a.shrink == 0) a.shrink_order = 0;
    return a;
}

static int32_t scan_expr_value(Engine* e, int level, const Glue** gout) {
    bool a = e->arith_error;
    bool b = false;
    lam::ArrayList<ExprFrame> stack(MEM_CAT_INPUT_OTHER, 4);
    int l = level;
    int r, s, o;
    int32_t ev = 0, tv = 0, n = 0, f = 0;
    Glue eg = ZERO_GLUE, tg = ZERO_GLUE, fg = ZERO_GLUE;
    Token t;
restart:
    r = EX_NONE; ev = 0; s = EX_NONE; tv = 0; n = 0;
    eg = ZERO_GLUE; tg = ZERO_GLUE;
cont:
    o = s == EX_NONE ? l : LV_INT;
    skip_spaces_x(e, &t);
    if (is_other(t, '(')) {
        ExprFrame fr = {l, r, s, ev, tv, n, eg, tg};
        stack.append(fr);
        l = o;
        goto restart;
    }
    back_input(e, t);
    if (o == LV_INT) f = scan_int(e);
    else if (o == LV_DIMEN) f = scan_dimen(e, false, false, false, 0);
    else fg = *scan_glue(e, o == LV_MU);
found:
    skip_spaces_x(e, &t);
    if (is_other(t, '+')) o = EX_ADD;
    else if (is_other(t, '-')) o = EX_SUB;
    else if (is_other(t, '*')) o = EX_MULT;
    else if (is_other(t, '/')) o = EX_DIV;
    else {
        o = EX_NONE;
        if (stack.empty()) {
            Meaning m = token_meaning(e, t);
            if (!(t.kind == TK_CS && m.type == MT_PRIM && m.value == P_RELAX)) back_input(e, t);
        } else if (!is_other(t, ')')) {
            diag(e, "tex-missing-paren", "Missing ) inserted for expression");
            back_input(e, t);
        }
    }
    e->arith_error = b;
    if (l == LV_INT || s > EX_SUB) {
        if (f > INFINITY_INT || f < -INFINITY_INT) { e->arith_error = true; f = 0; }
    } else if (l == LV_DIMEN) {
        if (f > MAX_DIMEN || f < -MAX_DIMEN) { e->arith_error = true; f = 0; }
    } else if (fg.width > MAX_DIMEN || fg.width < -MAX_DIMEN || fg.stretch > MAX_DIMEN ||
               fg.stretch < -MAX_DIMEN || fg.shrink > MAX_DIMEN || fg.shrink < -MAX_DIMEN) {
        e->arith_error = true;
        fg = ZERO_GLUE;
    }
    switch (s) {
    case EX_NONE:
        if (l >= LV_GLUE) tg = fg;
        else tv = f;
        break;
    case EX_MULT:
        if (o == EX_DIV) { n = f; o = EX_SCALE; }
        else if (l == LV_INT) tv = mult_integers(e, tv, f);
        else if (l == LV_DIMEN) tv = nx_plus_y(e, tv, f, 0);
        else tg = glue_scale(e, tg, EX_MULT, f, 0);
        break;
    case EX_DIV:
        if (l < LV_GLUE) tv = quotient(e, tv, f);
        else tg = glue_scale(e, tg, EX_DIV, f, 0);
        break;
    case EX_SCALE:
        if (l == LV_INT) tv = fract(e, tv, n, f, INFINITY_INT);
        else if (l == LV_DIMEN) tv = fract(e, tv, n, f, MAX_DIMEN);
        else tg = glue_scale(e, tg, EX_SCALE, f, n);
        break;
    }
    if (o > EX_SUB) {
        s = o;
    } else {
        s = EX_NONE;
        if (r == EX_NONE) {
            ev = tv;
            eg = tg;
        } else if (l == LV_INT) {
            ev = add_or_sub(e, ev, tv, INFINITY_INT, r == EX_SUB);
        } else if (l == LV_DIMEN) {
            ev = add_or_sub(e, ev, tv, MAX_DIMEN, r == EX_SUB);
        } else {
            eg = glue_add(e, eg, tg, r == EX_SUB);
        }
        r = o;
    }
    b = e->arith_error;
    if (o != EX_NONE) goto cont;
    if (!stack.empty()) {
        ExprFrame fr = stack.back();
        stack.remove(stack.size() - 1);
        f = ev;
        fg = eg;
        ev = fr.e; tv = fr.t; n = fr.n; s = fr.s; r = fr.r; l = fr.l;
        eg = fr.eg; tg = fr.tg;
        goto found;
    }
    if (b) {
        diag(e, "tex-arith-overflow", "Arithmetic overflow");
        ev = 0;
        eg = ZERO_GLUE;
    }
    e->arith_error = a;
    if (gout) *gout = new_glue(e, eg.width, eg.stretch, eg.stretch_order, eg.shrink, eg.shrink_order);
    return ev;
}

// ======================================================================
// \the and token-list scanning (TeX §§464-482)
// ======================================================================

bool the_toks(Engine* e, lam::ArrayList<Token>* out) {
    Token t;
    if (!get_x_token(e, &t)) return false;
    Meaning m = token_meaning(e, t);
    Quantity q = scan_something_internal(e, t, m, LV_TOK, false);
    if (q.level == LV_IDENT) {
        const Font& f = e->fonts[(size_t)q.i];
        out->append(make_cs(f.ident));
        return true;
    }
    if (q.level == LV_TOK) {
        for (uint32_t i = 0; i < q.t.count; i++) out->append(q.t.data[i]);
        return true;
    }
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    if (q.level == LV_INT) print_int(q.i, sb);
    else if (q.level == LV_DIMEN) { print_scaled(q.i, sb); strbuf_append_str(sb, "pt"); }
    else print_glue(q.g, q.level == LV_MU ? "mu" : "pt", sb);
    str_to_tokens(e, sb->str + mark, sb->length - mark, out);
    sb->length = mark;
    sb->str[mark] = '\0';
    return true;
}

// one token for an \edef-style body: \the, \unexpanded and \detokenize
// results go straight to `out`; returns false at end of input
static bool xpand_next(Engine* e, Token* t, lam::ArrayList<Token>* out) {
    for (;;) {
        if (!get_next(e, t)) return false;
        if (t->flags & TF_NOEXPAND) {
            t->flags &= (uint16_t)~TF_NOEXPAND;
            return true;
        }
        Meaning m = token_meaning(e, *t);
        if (m.type == MT_MACRO && (m.macro->flags & MF_PROTECTED)) return true;
        if (m.type == MT_PRIM && (m.value == P_THE || m.value == P_UNEXPANDED || m.value == P_DETOKENIZE)) {
            if (m.value == P_THE) {
                the_toks(e, out);
            } else if (m.value == P_UNEXPANDED) {
                scan_text_into(e, false, out);
            } else {
                lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
                scan_text_into(e, false, &text);
                StrBuf* tmp = strbuf_new();
                tokens_to_str(e, span_of(text), tmp, false);
                str_to_tokens(e, tmp->str, tmp->length, out);
                strbuf_free(tmp);
            }
            continue;
        }
        if (is_expandable(m)) {
            expand(e, *t, m);
            if (e->aborted) return false;
            continue;
        }
        return true;
    }
}

TokSpan scan_toks(Engine* e, bool macro_def, bool xpand, lam::ArrayList<Token>* params, uint8_t* nargs) {
    lam::ArrayList<Token> body(MEM_CAT_INPUT_OTHER, 32);
    scan_toks_into(e, macro_def, xpand, params, nargs, &body);
    return freeze(e, body);
}

void scan_text_into(Engine* e, bool xpand, lam::ArrayList<Token>* out) {
    scan_toks_into(e, false, xpand, nullptr, nullptr, out);
}

void skip_text(Engine* e, bool xpand) {
    lam::ArrayList<Token> dropped(MEM_CAT_INPUT_OTHER, 16);
    scan_toks_into(e, false, xpand, nullptr, nullptr, &dropped);
}

void scan_toks_into(Engine* e, bool macro_def, bool xpand, lam::ArrayList<Token>* params, uint8_t* nargs,
                    lam::ArrayList<Token>* out) {
    lam::ArrayList<Token>& body = *out;
    uint8_t next_param = 1;
    bool hash_brace = false;
    Token t;
    if (macro_def) {
        for (;;) {
            if (!get_token(e, &t)) goto finish;
            if (t.kind == TK_CHAR && (t.cat == CAT_BEGIN || t.cat == CAT_END)) break;
            if (t.kind == TK_CHAR && t.cat == CAT_PARAM) {
                Token s;
                if (!get_token(e, &s)) goto finish;
                if (s.kind == TK_CHAR && s.cat == CAT_BEGIN) {
                    hash_brace = true;
                    Token em = {};
                    em.kind = TK_END_MATCH;
                    em.value = 1;
                    if (params) params->append(em);
                    t = s;
                    break;
                }
                if (next_param > 9) {
                    diag(e, "tex-nine-params", "You already have nine parameters");
                    continue;
                }
                if (!(s.kind == TK_CHAR && s.cat == CAT_OTHER && s.value == (uint32_t)('0' + next_param))) {
                    diag(e, "tex-param-order", "Parameters must be numbered consecutively");
                    back_input(e, s);
                }
                Token mt = {};
                mt.kind = TK_MATCH;
                mt.value = next_param++;
                if (params) params->append(mt);
                continue;
            }
            Token stored = t;
            stored.flags &= (uint16_t)~TF_DIRECT;
            if (params) params->append(stored);
        }
        if (!hash_brace) {
            Token em = {};
            em.kind = TK_END_MATCH;
            if (params) params->append(em);
        }
        if (t.cat == CAT_END) {
            diag(e, "tex-missing-brace", "Missing { inserted");
            goto finish;
        }
    } else {
        scan_left_brace(e);
    }
    {
        int unbalance = 1;
        for (;;) {
            bool ok = (xpand ? xpand_next(e, &t, &body) : get_token(e, &t)) && list_fits(e, body.size());
            if (!ok && e->aborted) break;
            if (!ok) {
                diag(e, "tex-runaway", "File ended while scanning a text");
                break;
            }
            if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) unbalance++;
            else if (t.kind == TK_CHAR && t.cat == CAT_END) {
                if (--unbalance == 0) break;
            } else if (macro_def && t.kind == TK_CHAR && t.cat == CAT_PARAM) {
                Token s;
                bool got = xpand ? get_x_token(e, &s) : get_token(e, &s);
                if (!got) break;
                if (s.kind == TK_CHAR && s.cat == CAT_PARAM) {
                    // ## stands for one macro parameter character
                } else if (s.kind == TK_CHAR && s.cat == CAT_OTHER && s.value >= '1' &&
                           s.value < (uint32_t)('0' + next_param)) {
                    Token pt = {};
                    pt.kind = TK_PARAM;
                    pt.value = s.value - '0';
                    body.append(pt);
                    continue;
                } else {
                    diag(e, "tex-illegal-param", "Illegal parameter number in definition");
                    back_input(e, s);
                }
            }
            body.append(t);
        }
    }
finish:
    if (hash_brace) body.append(make_char('{', CAT_BEGIN));
    if (nargs) *nargs = (uint8_t)(next_param - 1);
}

TokSpan scan_general_text(Engine* e) {
    return scan_toks(e, false, false, nullptr, nullptr);
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
