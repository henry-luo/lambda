/**
 * JS Backtracking Regex Matcher — implementation of ECMA-262 §22.2.2 semantics.
 *
 * See js_bt_regex.h. The matcher uses the spec's Matcher/Continuation model: a
 * recursive matcher over an AST, threading a continuation and a direction (+1
 * normally, -1 inside a lookbehind body). Captures live in a flat array that is
 * saved/restored on backtracking. RepeatMatcher implements the nullable-quantifier
 * "discard empty optional iteration" rule. A global step budget bounds runtime.
 */
#include "js_bt_regex.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/utf.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// AST
// ---------------------------------------------------------------------------

enum RxType {
    RX_CHAR,        // single literal codepoint
    RX_CLASS,       // character class (ranges + negation)
    RX_ANY,         // .
    RX_BOL,         // ^
    RX_EOL,         // $
    RX_WORD_B,      // \b
    RX_NWORD_B,     // \B
    RX_GROUP,       // (capturing) / (?:) / (?<name>)  -> disjunction
    RX_LOOK,        // (?=) (?!) (?<=) (?<!)           -> disjunction
    RX_BACKREF,     // \N or \k<name>
    RX_QUANT,       // quantified atom
};

struct RxRange { uint32_t lo, hi; };

struct RxClass {
    RxRange* ranges;
    int range_count;
    bool negated;
};

struct RxSeq {
    struct RxNode** items;
    int count;
};

struct RxDisj {
    RxSeq** alts;
    int count;
};

struct RxNode {
    RxType type;
    // RX_CHAR
    uint32_t cp;
    // RX_CLASS
    RxClass* cls;
    // RX_GROUP / RX_LOOK
    RxDisj* disj;
    int group_index;     // >0 capturing, 0 non-capturing
    bool look_behind;
    bool look_negative;
    // RX_BACKREF
    int backref_index;
    // RX_QUANT
    RxNode* child;
    int qmin, qmax;      // qmax = -1 means unbounded
    bool greedy;
    int paren_lo, paren_hi; // capture indices contained in child (for per-iteration reset)
};

struct JsBtNamed { const char* name; int name_len; int index; };

struct JsBtRegex {
    Pool* pool;
    RxDisj* root;
    int group_count;
    JsBtFlags flags;
    JsBtNamed* named;
    int named_count;
};

// ---------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------

static int bt_utf8_decode(const char* s, int len, int pos, uint32_t* cp) {
    if (pos >= len) return 0;
    unsigned char c0 = (unsigned char)s[pos];
    if (c0 < 0x80) { *cp = c0; return 1; }
    if ((c0 & 0xE0) == 0xC0 && pos + 1 < len) {
        unsigned char c1 = (unsigned char)s[pos + 1];
        if ((c1 & 0xC0) == 0x80) { *cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F); return 2; }
    }
    if ((c0 & 0xF0) == 0xE0 && pos + 2 < len) {
        unsigned char c1 = (unsigned char)s[pos + 1], c2 = (unsigned char)s[pos + 2];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
            *cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F); return 3;
        }
    }
    if ((c0 & 0xF8) == 0xF0 && pos + 3 < len) {
        unsigned char c1 = (unsigned char)s[pos + 1], c2 = (unsigned char)s[pos + 2], c3 = (unsigned char)s[pos + 3];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
            *cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F); return 4;
        }
    }
    *cp = c0; return 1; // lone byte fallback
}

// Decode the character ending at byte offset `pos` (i.e. immediately to the left).
// Returns the byte length of that character, 0 if pos <= 0.
static int bt_utf8_decode_prev(const char* s, int pos, uint32_t* cp) {
    if (pos <= 0) return 0;
    int start = pos - 1;
    int back = 0;
    while (start > 0 && ((unsigned char)s[start] & 0xC0) == 0x80 && back < 3) { start--; back++; }
    uint32_t c; int adv = bt_utf8_decode(s, pos, start, &c);
    if (adv != pos - start) { // misaligned: treat the single byte before pos as a char
        *cp = (unsigned char)s[pos - 1]; return 1;
    }
    *cp = c; return adv;
}

// Forward/backward decoders that, under the unicode flag, combine a WTF-8 encoded
// surrogate pair into the corresponding astral code point (so `.`, character
// classes, and backreferences honour code-point semantics under /u).
static int bt_decode_fwd(const char* s, int len, int pos, uint32_t* cp, bool uni) {
    int adv = bt_utf8_decode(s, len, pos, cp);
    if (uni && adv && utf_is_high_surrogate(*cp)) {
        uint32_t lo; int adv2 = bt_utf8_decode(s, len, pos + adv, &lo);
        uint32_t combined = adv2 ? utf16_decode_pair((uint16_t)*cp, (uint16_t)lo) : 0;
        if (combined != 0) {
            *cp = combined;
            return adv + adv2;
        }
    }
    return adv;
}
static int bt_decode_bwd(const char* s, int pos, uint32_t* cp, bool uni) {
    int adv = bt_utf8_decode_prev(s, pos, cp);
    if (uni && adv && utf_is_low_surrogate(*cp) && pos - adv > 0) {
        uint32_t hi; int adv2 = bt_utf8_decode_prev(s, pos - adv, &hi);
        uint32_t combined = adv2 ? utf16_decode_pair((uint16_t)hi, (uint16_t)*cp) : 0;
        if (combined != 0) {
            *cp = combined;
            return adv + adv2;
        }
    }
    return adv;
}

// ---------------------------------------------------------------------------
// Character predicates / canonicalization
// ---------------------------------------------------------------------------

static inline bool bt_is_word(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9') || cp == '_';
}
static inline bool bt_is_line_term(uint32_t cp) {
    return cp == 0x0A || cp == 0x0D || cp == 0x2028 || cp == 0x2029;
}
static inline uint32_t bt_to_upper(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
}
static inline uint32_t bt_to_lower(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
}
// ECMA-262 Canonicalize for the comparison of single characters (ASCII fast path,
// sufficient for the routed tests which fold only ASCII letters under /i).
static inline uint32_t bt_canon(uint32_t cp, bool icase) {
    if (!icase) return cp;
    return bt_to_upper(cp);
}
static inline bool bt_char_eq(uint32_t a, uint32_t b, bool icase) {
    return a == b || (icase && bt_canon(a, icase) == bt_canon(b, icase));
}

static bool bt_class_contains_raw(const RxClass* cls, uint32_t cp) {
    for (int i = 0; i < cls->range_count; i++) {
        if (cp >= cls->ranges[i].lo && cp <= cls->ranges[i].hi) return true;
    }
    return false;
}
static bool bt_class_match(const RxClass* cls, uint32_t cp, bool icase) {
    bool in = bt_class_contains_raw(cls, cp);
    if (!in && icase) {
        uint32_t u = bt_to_upper(cp), l = bt_to_lower(cp);
        if (u != cp) in = bt_class_contains_raw(cls, u);
        if (!in && l != cp) in = bt_class_contains_raw(cls, l);
    }
    return in != cls->negated;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

struct PtrVec { void** data; int count; int cap; };
static void pv_push(PtrVec* v, void* p) {
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 4;
        v->data = (void**)mem_realloc(v->data, (size_t)v->cap * sizeof(void*), MEM_CAT_PARSER);
    }
    v->data[v->count++] = p;
}

struct Parser {
    const char* p;
    int len;
    int pos;
    Pool* pool;
    JsBtFlags flags;
    int group_counter;       // groups assigned so far (capturing)
    JsBtNamed* named;        // name table from pre-pass
    int named_count;
    int total_groups;        // total capturing groups (from pre-pass)
    bool error;
};

static void* bt_alloc(Parser* ps, size_t n) { return pool_calloc(ps->pool, n); }

static RxDisj* parse_disjunction(Parser* ps);

static RxNode* new_node(Parser* ps, RxType t) {
    RxNode* n = (RxNode*)bt_alloc(ps, sizeof(RxNode));
    n->type = t;
    return n;
}

// parse a \x{HHHH} or \xHH after the backslash+x has been consumed; returns codepoint, sets ok
static uint32_t parse_hex_escape(Parser* ps, bool* ok) {
    *ok = false;
    uint32_t v = 0;
    if (ps->pos < ps->len && ps->p[ps->pos] == '{') {
        ps->pos++;
        int digits = 0;
        while (ps->pos < ps->len && ps->p[ps->pos] != '}') {
            char c = ps->p[ps->pos];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return 0;
            v = v * 16 + (uint32_t)d; digits++; ps->pos++;
            if (v > 0x10FFFF) return 0;
        }
        if (ps->pos >= ps->len || ps->p[ps->pos] != '}' || digits == 0) return 0;
        ps->pos++; // consume }
        *ok = true; return v;
    }
    // \xHH
    for (int i = 0; i < 2; i++) {
        if (ps->pos >= ps->len) return 0;
        char c = ps->p[ps->pos];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = v * 16 + (uint32_t)d; ps->pos++;
    }
    *ok = true; return v;
}

// Append the codepoint ranges for a class shorthand (\d \D \w \W \s \S) into vec.
// Returns whether the shorthand is itself "negated" (\D \W \S). For simplicity we
// represent negated shorthands by adding their positive ranges and a separate
// negated class node; but within a [...] we expand to explicit ranges.
static void add_shorthand_ranges(PtrVec* ranges, char kind, Parser* ps) {
    auto add = [&](uint32_t lo, uint32_t hi) {
        RxRange* r = (RxRange*)bt_alloc(ps, sizeof(RxRange));
        r->lo = lo; r->hi = hi; pv_push(ranges, r);
    };
    switch (kind) {
        case 'd': add('0','9'); break;
        case 'w': add('0','9'); add('A','Z'); add('a','z'); add('_','_'); break;
        case 's':
            add(0x09,0x0D); add(0x20,0x20); add(0xA0,0xA0); add(0x1680,0x1680);
            add(0x2000,0x200A); add(0x2028,0x2029); add(0x202F,0x202F);
            add(0x205F,0x205F); add(0x3000,0x3000); add(0xFEFF,0xFEFF); break;
        default: break;
    }
}

// Build a standalone class node for a top-level shorthand \d \D \w \W \s \S.
static RxNode* shorthand_class(Parser* ps, char kind) {
    PtrVec ranges = {0,0,0};
    char lower = (kind >= 'A' && kind <= 'Z') ? kind + 32 : kind;
    bool negated = (kind >= 'A' && kind <= 'Z');
    add_shorthand_ranges(&ranges, lower, ps);
    RxNode* n = new_node(ps, RX_CLASS);
    RxClass* cls = (RxClass*)bt_alloc(ps, sizeof(RxClass));
    cls->range_count = ranges.count;
    cls->ranges = (RxRange*)bt_alloc(ps, sizeof(RxRange) * (ranges.count ? ranges.count : 1));
    for (int i = 0; i < ranges.count; i++) cls->ranges[i] = *(RxRange*)ranges.data[i];
    cls->negated = negated;
    mem_free(ranges.data);
    n->cls = cls;
    return n;
}

// Parse a character class [...]; ps->pos points at '['.
static RxNode* parse_class(Parser* ps) {
    ps->pos++; // [
    bool negated = false;
    if (ps->pos < ps->len && ps->p[ps->pos] == '^') { negated = true; ps->pos++; }
    PtrVec ranges = {0,0,0};
    auto add_range = [&](uint32_t lo, uint32_t hi) {
        RxRange* r = (RxRange*)bt_alloc(ps, sizeof(RxRange));
        r->lo = lo; r->hi = hi; pv_push(&ranges, r);
    };
    // returns codepoint, sets is_class if it was a shorthand (already added to ranges)
    while (ps->pos < ps->len && ps->p[ps->pos] != ']') {
        uint32_t lo; bool lo_is_shorthand = false;
        if (ps->p[ps->pos] == '\\' && ps->pos + 1 < ps->len) {
            ps->pos++;
            char e = ps->p[ps->pos];
            switch (e) {
                case 'd': case 'w': case 's': add_shorthand_ranges(&ranges, e, ps); ps->pos++; lo_is_shorthand = true; break;
                case 'D': add_range(0, '0'-1); add_range('9'+1, 0x10FFFF); ps->pos++; lo_is_shorthand = true; break;
                case 'W': add_range(0, '0'-1); add_range('9'+1,'A'-1); add_range('Z'+1,'_'-1); add_range('_'+1,'a'-1); add_range('z'+1,0x10FFFF); ps->pos++; lo_is_shorthand = true; break;
                case 'S': add_range(0,0x08); add_range(0x0E,0x1F); add_range(0x21,0x9F); add_range(0xA1,0x167F); add_range(0x1681,0x1FFF); add_range(0x200B,0x2027); add_range(0x202A,0x202E); add_range(0x2030,0x205E); add_range(0x2060,0x2FFF); add_range(0x3001,0xFEFE); add_range(0xFF00,0x10FFFF); ps->pos++; lo_is_shorthand = true; break;
                case 'b': lo = 0x08; ps->pos++; break; // backspace
                case 'n': lo = '\n'; ps->pos++; break;
                case 'r': lo = '\r'; ps->pos++; break;
                case 't': lo = '\t'; ps->pos++; break;
                case 'f': lo = '\f'; ps->pos++; break;
                case 'v': lo = '\v'; ps->pos++; break;
                case '0': lo = 0; ps->pos++; break;
                case 'x': { ps->pos++; bool ok; lo = parse_hex_escape(ps, &ok); if (!ok) { ps->error = true; mem_free(ranges.data); return NULL; } break; }
                default: lo = (unsigned char)e; ps->pos++; break;
            }
        } else {
            int adv = bt_utf8_decode(ps->p, ps->len, ps->pos, &lo);
            ps->pos += adv;
        }
        if (lo_is_shorthand) continue;
        // possible range a-b
        if (ps->pos < ps->len && ps->p[ps->pos] == '-' &&
            ps->pos + 1 < ps->len && ps->p[ps->pos + 1] != ']') {
            ps->pos++; // -
            uint32_t hi;
            if (ps->p[ps->pos] == '\\' && ps->pos + 1 < ps->len) {
                ps->pos++;
                char e = ps->p[ps->pos];
                switch (e) {
                    case 'n': hi='\n'; ps->pos++; break;
                    case 'r': hi='\r'; ps->pos++; break;
                    case 't': hi='\t'; ps->pos++; break;
                    case 'f': hi='\f'; ps->pos++; break;
                    case 'v': hi='\v'; ps->pos++; break;
                    case '0': hi=0; ps->pos++; break;
                    case 'x': { ps->pos++; bool ok; hi = parse_hex_escape(ps,&ok); if(!ok){ps->error=true;mem_free(ranges.data);return NULL;} break; }
                    default: hi=(unsigned char)e; ps->pos++; break;
                }
            } else {
                int adv = bt_utf8_decode(ps->p, ps->len, ps->pos, &hi); ps->pos += adv;
            }
            if (hi < lo) { ps->error = true; mem_free(ranges.data); return NULL; }
            add_range(lo, hi);
        } else {
            add_range(lo, lo);
        }
    }
    if (ps->pos >= ps->len || ps->p[ps->pos] != ']') { ps->error = true; mem_free(ranges.data); return NULL; }
    ps->pos++; // ]
    RxNode* n = new_node(ps, RX_CLASS);
    RxClass* cls = (RxClass*)bt_alloc(ps, sizeof(RxClass));
    cls->range_count = ranges.count;
    cls->ranges = (RxRange*)bt_alloc(ps, sizeof(RxRange) * (ranges.count ? ranges.count : 1));
    for (int i = 0; i < ranges.count; i++) cls->ranges[i] = *(RxRange*)ranges.data[i];
    cls->negated = negated;
    mem_free(ranges.data);
    n->cls = cls;
    return n;
}

static int lookup_named(Parser* ps, const char* name, int name_len) {
    for (int i = 0; i < ps->named_count; i++) {
        if (ps->named[i].name_len == name_len &&
            memcmp(ps->named[i].name, name, name_len) == 0) return ps->named[i].index;
    }
    return -1;
}

// Parse a single atom (without trailing quantifier). Returns NULL on error or
// when nothing parseable (e.g. at '|' or ')').
static RxNode* parse_atom(Parser* ps) {
    if (ps->pos >= ps->len) return NULL;
    char c = ps->p[ps->pos];
    if (c == '|' || c == ')') return NULL;

    if (c == '(') {
        // group / lookaround
        ps->pos++; // (
        bool capturing = true;
        bool look = false, look_behind = false, look_negative = false;
        int gi = 0;
        const char* gname = NULL; int gname_len = 0;
        if (ps->pos < ps->len && ps->p[ps->pos] == '?') {
            ps->pos++;
            if (ps->pos >= ps->len) { ps->error = true; return NULL; }
            char k = ps->p[ps->pos];
            if (k == ':') { capturing = false; ps->pos++; }
            else if (k == '=') { look = true; look_negative = false; capturing = false; ps->pos++; }
            else if (k == '!') { look = true; look_negative = true; capturing = false; ps->pos++; }
            else if (k == '<') {
                // (?<= , (?<! , or (?<name>
                if (ps->pos + 1 < ps->len && (ps->p[ps->pos+1] == '=' || ps->p[ps->pos+1] == '!')) {
                    look = true; look_behind = true; look_negative = (ps->p[ps->pos+1] == '!');
                    capturing = false; ps->pos += 2;
                } else {
                    // named group
                    ps->pos++; // <
                    int ns = ps->pos;
                    while (ps->pos < ps->len && ps->p[ps->pos] != '>') ps->pos++;
                    if (ps->pos >= ps->len) { ps->error = true; return NULL; }
                    gname = ps->p + ns; gname_len = ps->pos - ns;
                    ps->pos++; // >
                }
            } else if (k == 'P' && ps->pos + 1 < ps->len && ps->p[ps->pos+1] == '<') {
                ps->pos += 2; // P<
                int ns = ps->pos;
                while (ps->pos < ps->len && ps->p[ps->pos] != '>') ps->pos++;
                if (ps->pos >= ps->len) { ps->error = true; return NULL; }
                gname = ps->p + ns; gname_len = ps->pos - ns;
                ps->pos++; // >
            } else {
                ps->error = true; return NULL; // unsupported (?...) construct
            }
        }
        if (capturing) { gi = ++ps->group_counter; }
        RxDisj* d = parse_disjunction(ps);
        if (ps->error) return NULL;
        if (ps->pos >= ps->len || ps->p[ps->pos] != ')') { ps->error = true; return NULL; }
        ps->pos++; // )
        RxNode* n;
        if (look) {
            n = new_node(ps, RX_LOOK);
            n->disj = d; n->look_behind = look_behind; n->look_negative = look_negative;
        } else {
            n = new_node(ps, RX_GROUP);
            n->disj = d; n->group_index = gi;
        }
        (void)gname; (void)gname_len;
        return n;
    }

    if (c == '[') return parse_class(ps);

    if (c == '.') { ps->pos++; return new_node(ps, RX_ANY); }
    if (c == '^') { ps->pos++; return new_node(ps, RX_BOL); }
    if (c == '$') { ps->pos++; return new_node(ps, RX_EOL); }

    if (c == '\\') {
        if (ps->pos + 1 >= ps->len) { ps->error = true; return NULL; }
        char e = ps->p[ps->pos + 1];
        if (e >= '1' && e <= '9') {
            // numeric backreference: consume digits greedily while <= total groups
            ps->pos++; // backslash
            int val = 0; int start = ps->pos;
            while (ps->pos < ps->len && ps->p[ps->pos] >= '0' && ps->p[ps->pos] <= '9') {
                int next = val * 10 + (ps->p[ps->pos] - '0');
                if (next > ps->total_groups) break;
                val = next; ps->pos++;
            }
            if (ps->pos == start) { ps->error = true; return NULL; }
            RxNode* n = new_node(ps, RX_BACKREF);
            n->backref_index = val;
            return n;
        }
        if (e == 'k') {
            // \k<name>
            ps->pos += 2; // \k
            if (ps->pos >= ps->len || ps->p[ps->pos] != '<') { ps->error = true; return NULL; }
            ps->pos++; // <
            int ns = ps->pos;
            while (ps->pos < ps->len && ps->p[ps->pos] != '>') ps->pos++;
            if (ps->pos >= ps->len) { ps->error = true; return NULL; }
            int nl = ps->pos - ns;
            int idx = lookup_named(ps, ps->p + ns, nl);
            ps->pos++; // >
            if (idx < 0) { ps->error = true; return NULL; }
            RxNode* n = new_node(ps, RX_BACKREF);
            n->backref_index = idx;
            return n;
        }
        if (e == 'b') { ps->pos += 2; return new_node(ps, RX_WORD_B); }
        if (e == 'B') { ps->pos += 2; return new_node(ps, RX_NWORD_B); }
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') {
            ps->pos += 2; return shorthand_class(ps, e);
        }
        // single-char escapes
        uint32_t cp;
        switch (e) {
            case 'n': cp = '\n'; ps->pos += 2; break;
            case 'r': cp = '\r'; ps->pos += 2; break;
            case 't': cp = '\t'; ps->pos += 2; break;
            case 'f': cp = '\f'; ps->pos += 2; break;
            case 'v': cp = '\v'; ps->pos += 2; break;
            case '0': cp = 0; ps->pos += 2; break;
            case 'x': { ps->pos += 2; bool ok; cp = parse_hex_escape(ps, &ok); if (!ok) { ps->error = true; return NULL; } break; }
            default: cp = (unsigned char)e; ps->pos += 2; break;
        }
        RxNode* n = new_node(ps, RX_CHAR); n->cp = cp; return n;
    }

    // literal codepoint
    uint32_t cp; int adv = bt_utf8_decode(ps->p, ps->len, ps->pos, &cp);
    ps->pos += adv;
    RxNode* n = new_node(ps, RX_CHAR); n->cp = cp; return n;
}

// Parse {n}, {n,}, {n,m}. ps->pos at '{'. Returns false (not a quantifier) if
// malformed — '{' is then treated as a literal.
static bool parse_brace_quant(Parser* ps, int* outmin, int* outmax) {
    int save = ps->pos;
    ps->pos++; // {
    int lo = 0; bool has_lo = false;
    while (ps->pos < ps->len && ps->p[ps->pos] >= '0' && ps->p[ps->pos] <= '9') { lo = lo*10 + (ps->p[ps->pos]-'0'); has_lo = true; ps->pos++; }
    if (!has_lo) { ps->pos = save; return false; }
    int hi;
    if (ps->pos < ps->len && ps->p[ps->pos] == '}') { hi = lo; ps->pos++; }
    else if (ps->pos < ps->len && ps->p[ps->pos] == ',') {
        ps->pos++;
        if (ps->pos < ps->len && ps->p[ps->pos] == '}') { hi = -1; ps->pos++; }
        else {
            int h = 0; bool has_hi = false;
            while (ps->pos < ps->len && ps->p[ps->pos] >= '0' && ps->p[ps->pos] <= '9') { h = h*10 + (ps->p[ps->pos]-'0'); has_hi = true; ps->pos++; }
            if (!has_hi || ps->pos >= ps->len || ps->p[ps->pos] != '}') { ps->pos = save; return false; }
            ps->pos++; hi = h;
        }
    } else { ps->pos = save; return false; }
    *outmin = lo; *outmax = hi;
    return true;
}

// Parse one quantified term: atom + optional quantifier.
static RxNode* parse_term(Parser* ps) {
    int paren_lo = ps->group_counter + 1;
    RxNode* atom = parse_atom(ps);
    if (!atom) return NULL;
    int paren_hi = ps->group_counter;
    if (ps->pos >= ps->len) return atom;
    char c = ps->p[ps->pos];
    int qmin, qmax;
    bool has_q = false;
    if (c == '*') { qmin = 0; qmax = -1; has_q = true; ps->pos++; }
    else if (c == '+') { qmin = 1; qmax = -1; has_q = true; ps->pos++; }
    else if (c == '?') { qmin = 0; qmax = 1; has_q = true; ps->pos++; }
    else if (c == '{') { if (parse_brace_quant(ps, &qmin, &qmax)) has_q = true; }
    if (!has_q) return atom;
    bool greedy = true;
    if (ps->pos < ps->len && ps->p[ps->pos] == '?') { greedy = false; ps->pos++; }
    RxNode* q = new_node(ps, RX_QUANT);
    q->child = atom; q->qmin = qmin; q->qmax = qmax; q->greedy = greedy;
    q->paren_lo = paren_lo; q->paren_hi = paren_hi;
    return q;
}

static RxSeq* parse_alternative(Parser* ps) {
    PtrVec items = {0,0,0};
    while (ps->pos < ps->len && ps->p[ps->pos] != '|' && ps->p[ps->pos] != ')') {
        RxNode* t = parse_term(ps);
        if (ps->error) { mem_free(items.data); return NULL; }
        if (!t) break;
        pv_push(&items, t);
    }
    RxSeq* seq = (RxSeq*)bt_alloc(ps, sizeof(RxSeq));
    seq->count = items.count;
    seq->items = (RxNode**)bt_alloc(ps, sizeof(RxNode*) * (items.count ? items.count : 1));
    for (int i = 0; i < items.count; i++) seq->items[i] = (RxNode*)items.data[i];
    mem_free(items.data);
    return seq;
}

static RxDisj* parse_disjunction(Parser* ps) {
    PtrVec alts = {0,0,0};
    RxSeq* a = parse_alternative(ps);
    if (ps->error) { mem_free(alts.data); return NULL; }
    pv_push(&alts, a);
    while (ps->pos < ps->len && ps->p[ps->pos] == '|') {
        ps->pos++; // |
        RxSeq* b = parse_alternative(ps);
        if (ps->error) { mem_free(alts.data); return NULL; }
        pv_push(&alts, b);
    }
    RxDisj* d = (RxDisj*)bt_alloc(ps, sizeof(RxDisj));
    d->count = alts.count;
    d->alts = (RxSeq**)bt_alloc(ps, sizeof(RxSeq*) * alts.count);
    for (int i = 0; i < alts.count; i++) d->alts[i] = (RxSeq*)alts.data[i];
    mem_free(alts.data);
    return d;
}

// Pre-pass: count capturing groups and collect named-group indices.
static int collect_groups(const char* p, int len, Pool* pool, JsBtNamed** out_named, int* out_named_count) {
    PtrVec names = {0,0,0};
    int count = 0;
    bool in_class = false;
    for (int i = 0; i < len; i++) {
        char c = p[i];
        if (c == '\\') { i++; continue; }
        if (in_class) { if (c == ']') in_class = false; continue; }
        if (c == '[') { in_class = true; continue; }
        if (c == '(') {
            if (i + 1 < len && p[i+1] == '?') {
                // (?: (?= (?! (?<= (?<! (?<name>  (?P<name>
                if (i + 2 < len && p[i+2] == '<' &&
                    i + 3 < len && p[i+3] != '=' && p[i+3] != '!') {
                    // named group (?<name>
                    int ns = i + 3;
                    int j = ns; while (j < len && p[j] != '>') j++;
                    count++;
                    int nl = j - ns;
                    char* nc = (char*)pool_calloc(pool, nl + 1);
                    memcpy(nc, p + ns, nl);
                    JsBtNamed* nm = (JsBtNamed*)pool_calloc(pool, sizeof(JsBtNamed));
                    nm->name = nc; nm->name_len = nl; nm->index = count;
                    pv_push(&names, nm);
                } else if (i + 2 < len && p[i+2] == 'P' && i + 3 < len && p[i+3] == '<') {
                    int ns = i + 4;
                    int j = ns; while (j < len && p[j] != '>') j++;
                    count++;
                    int nl = j - ns;
                    char* nc = (char*)pool_calloc(pool, nl + 1);
                    memcpy(nc, p + ns, nl);
                    JsBtNamed* nm = (JsBtNamed*)pool_calloc(pool, sizeof(JsBtNamed));
                    nm->name = nc; nm->name_len = nl; nm->index = count;
                    pv_push(&names, nm);
                }
                // else non-capturing / lookaround
            } else {
                count++; // plain capturing group
            }
        }
    }
    JsBtNamed* arr = NULL;
    if (names.count) {
        arr = (JsBtNamed*)pool_calloc(pool, sizeof(JsBtNamed) * names.count);
        for (int i = 0; i < names.count; i++) arr[i] = *(JsBtNamed*)names.data[i];
    }
    mem_free(names.data);
    *out_named = arr; *out_named_count = names.count;
    return count;
}

// ---------------------------------------------------------------------------
// Matcher (CPS)
// ---------------------------------------------------------------------------

struct MatchCtx {
    const char* input;
    int input_len;
    int* cap_start;   // size ngroups+1
    int* cap_end;
    int ngroups;
    bool icase, multiline, dot_all, unicode;
    long steps;
    long budget;
    int depth;        // current recursion depth (bounds stack usage)
    int depth_budget; // bail like the step budget rather than overflow the stack
    bool overflow;
};

struct Cont {
    enum Kind { C_SEQ, C_CAP, C_REPEAT } kind;
    const Cont* up;
    // C_SEQ
    const RxSeq* seq; int idx;
    // C_CAP
    int cap_group; int cap_anchor;
    // C_REPEAT
    const RxNode* rep_node; int rep_min, rep_max; int rep_start;
};

static int bt_run(MatchCtx& ctx, const Cont* k, int pos, int dir);
static int bt_match(MatchCtx& ctx, const RxNode* node, int pos, const Cont* k, int dir);
static int bt_match_disj(MatchCtx& ctx, const RxDisj* d, int pos, const Cont* k, int dir);
static int bt_repeat(MatchCtx& ctx, const RxNode* node, int min, int max, int pos, const Cont* k, int dir);

static int bt_run(MatchCtx& ctx, const Cont* k, int pos, int dir) {
    if (++ctx.steps > ctx.budget) { ctx.overflow = true; return -1; }
    if (!k) return pos;  // accept
    if (k->kind == Cont::C_SEQ) {
        if (k->idx >= k->seq->count) return bt_run(ctx, k->up, pos, dir);
        int real = (dir >= 0) ? k->idx : (k->seq->count - 1 - k->idx);
        const RxNode* node = k->seq->items[real];
        Cont next; next.kind = Cont::C_SEQ; next.up = k->up; next.seq = k->seq; next.idx = k->idx + 1;
        return bt_match(ctx, node, pos, &next, dir);
    }
    if (k->kind == Cont::C_CAP) {
        int g = k->cap_group;
        int os = ctx.cap_start[g], oe = ctx.cap_end[g];
        int s = k->cap_anchor < pos ? k->cap_anchor : pos;
        int e = k->cap_anchor < pos ? pos : k->cap_anchor;
        ctx.cap_start[g] = s; ctx.cap_end[g] = e;
        int r = bt_run(ctx, k->up, pos, dir);
        if (r < 0) { ctx.cap_start[g] = os; ctx.cap_end[g] = oe; }
        return r;
    }
    // C_REPEAT
    if (k->rep_min == 0 && pos == k->rep_start) return -1; // discard empty optional iteration
    int min2 = (k->rep_min == 0) ? 0 : k->rep_min - 1;
    int max2 = (k->rep_max < 0) ? -1 : k->rep_max - 1;
    return bt_repeat(ctx, k->rep_node, min2, max2, pos, k->up, dir);
}

// True for atoms that always consume exactly one code point and contain no
// captures/sub-structure — these can be repeated iteratively (O(1) stack)
// instead of recursing once per iteration (which overflows on long inputs).
static inline bool bt_simple_atom(const RxNode* n) {
    return n->type == RX_CHAR || n->type == RX_CLASS || n->type == RX_ANY;
}

// Match a single simple atom at `pos` in direction `dir`; return its byte width
// (>0) on a match, 0 otherwise.
static int bt_simple_step(MatchCtx& ctx, const RxNode* n, int pos, int dir) {
    uint32_t cp;
    int adv = (dir >= 0) ? bt_decode_fwd(ctx.input, ctx.input_len, pos, &cp, ctx.unicode)
                         : bt_decode_bwd(ctx.input, pos, &cp, ctx.unicode);
    if (adv == 0) return 0;
    bool ok;
    switch (n->type) {
        case RX_CHAR:  ok = bt_char_eq(cp, n->cp, ctx.icase); break;
        case RX_CLASS: ok = bt_class_match(n->cls, cp, ctx.icase); break;
        case RX_ANY:   ok = ctx.dot_all || !bt_is_line_term(cp); break;
        default:       ok = false; break;
    }
    return ok ? adv : 0;
}

// Iterative RepeatMatcher for a simple consuming atom: no per-iteration recursion,
// so it handles arbitrarily long matches without growing the C stack. Each simple
// atom consumes >=1 char, so the empty-iteration discard rule cannot apply.
static int bt_repeat_simple(MatchCtx& ctx, const RxNode* node, int pos, const Cont* k, int dir) {
    const RxNode* child = node->child;
    int min = node->qmin, max = node->qmax;
    if (node->greedy) {
        // consume greedily up to max, then back off one atom at a time
        int cur = pos, cnt = 0;
        while (max < 0 || cnt < max) {
            if (++ctx.steps > ctx.budget) { ctx.overflow = true; return -1; }
            int adv = bt_simple_step(ctx, child, cur, dir);
            if (adv == 0) break;
            cur += (dir >= 0) ? adv : -adv;
            cnt++;
        }
        if (cnt < min) return -1;
        for (;;) {
            int r = bt_run(ctx, k, cur, dir);
            if (r >= 0) return r;
            if (ctx.overflow || cnt == min) return -1;
            uint32_t cp;
            int w = (dir >= 0) ? bt_decode_bwd(ctx.input, cur, &cp, ctx.unicode)
                               : bt_decode_fwd(ctx.input, ctx.input_len, cur, &cp, ctx.unicode);
            if (w == 0) return -1;
            cur += (dir >= 0) ? -w : w;
            cnt--;
        }
    } else {
        // lazy: take the fewest atoms first, add one at a time up to max
        int cur = pos, cnt = 0;
        while (cnt < min) {
            int adv = bt_simple_step(ctx, child, cur, dir);
            if (adv == 0) return -1;
            cur += (dir >= 0) ? adv : -adv;
            cnt++;
        }
        for (;;) {
            int r = bt_run(ctx, k, cur, dir);
            if (r >= 0) return r;
            if (ctx.overflow || (max >= 0 && cnt >= max)) return -1;
            if (++ctx.steps > ctx.budget) { ctx.overflow = true; return -1; }
            int adv = bt_simple_step(ctx, child, cur, dir);
            if (adv == 0) return -1;
            cur += (dir >= 0) ? adv : -adv;
            cnt++;
        }
    }
}

static int bt_repeat_inner(MatchCtx& ctx, const RxNode* node, int min, int max, int pos, const Cont* k, int dir) {
    // reset captures of parens contained in the quantified child for this iteration
    int lo = node->paren_lo, hi = node->paren_hi;
    int saved_s[256], saved_e[256]; int nsaved = 0;
    if (hi >= lo) {
        for (int g = lo; g <= hi && g <= ctx.ngroups; g++) {
            saved_s[nsaved] = ctx.cap_start[g]; saved_e[nsaved] = ctx.cap_end[g]; nsaved++;
            ctx.cap_start[g] = -1; ctx.cap_end[g] = -1;
        }
    }
    auto restore = [&]() {
        int idx = 0;
        for (int g = lo; g <= hi && g <= ctx.ngroups; g++) { ctx.cap_start[g] = saved_s[idx]; ctx.cap_end[g] = saved_e[idx]; idx++; }
    };
    Cont d; d.kind = Cont::C_REPEAT; d.up = k; d.rep_node = node;
    d.rep_min = min; d.rep_max = max; d.rep_start = pos;
    int r;
    if (min != 0) {
        r = bt_match(ctx, node->child, pos, &d, dir);
        if (r < 0) restore();
        return r;
    }
    if (node->greedy) {
        r = bt_match(ctx, node->child, pos, &d, dir);
        if (r >= 0) return r;
        restore();
        return bt_run(ctx, k, pos, dir);
    } else {
        r = bt_run(ctx, k, pos, dir);
        if (r >= 0) return r;
        r = bt_match(ctx, node->child, pos, &d, dir);
        if (r < 0) restore();
        return r;
    }
}

static int bt_repeat(MatchCtx& ctx, const RxNode* node, int min, int max, int pos, const Cont* k, int dir) {
    if (ctx.overflow) return -1;
    if (max == 0) return bt_run(ctx, k, pos, dir);
    // Simple consuming atoms repeat iteratively (no per-iteration recursion).
    if (bt_simple_atom(node->child)) return bt_repeat_simple(ctx, node, pos, k, dir);
    // Complex children recurse once per iteration; bound the depth so a pathological
    // repeat count bails to "no match" (anti-DoS) instead of overflowing the stack.
    if (++ctx.depth > ctx.depth_budget) { ctx.overflow = true; ctx.depth--; return -1; }
    int r = bt_repeat_inner(ctx, node, min, max, pos, k, dir);
    ctx.depth--;
    return r;
}

static int bt_match_disj(MatchCtx& ctx, const RxDisj* d, int pos, const Cont* k, int dir) {
    for (int i = 0; i < d->count; i++) {
        Cont seqk; seqk.kind = Cont::C_SEQ; seqk.up = k; seqk.seq = d->alts[i]; seqk.idx = 0;
        int r = bt_run(ctx, &seqk, pos, dir);
        if (r >= 0) return r;
        if (ctx.overflow) return -1;
    }
    return -1;
}

static int bt_match(MatchCtx& ctx, const RxNode* node, int pos, const Cont* k, int dir) {
    if (++ctx.steps > ctx.budget) { ctx.overflow = true; return -1; }
    switch (node->type) {
    case RX_CHAR: {
        if (dir >= 0) {
            uint32_t cp; int adv = bt_decode_fwd(ctx.input, ctx.input_len, pos, &cp, ctx.unicode);
            if (adv == 0 || !bt_char_eq(cp, node->cp, ctx.icase)) return -1;
            return bt_run(ctx, k, pos + adv, dir);
        } else {
            uint32_t cp; int adv = bt_decode_bwd(ctx.input, pos, &cp, ctx.unicode);
            if (adv == 0 || !bt_char_eq(cp, node->cp, ctx.icase)) return -1;
            return bt_run(ctx, k, pos - adv, dir);
        }
    }
    case RX_CLASS: {
        if (dir >= 0) {
            uint32_t cp; int adv = bt_decode_fwd(ctx.input, ctx.input_len, pos, &cp, ctx.unicode);
            if (adv == 0 || !bt_class_match(node->cls, cp, ctx.icase)) return -1;
            return bt_run(ctx, k, pos + adv, dir);
        } else {
            uint32_t cp; int adv = bt_decode_bwd(ctx.input, pos, &cp, ctx.unicode);
            if (adv == 0 || !bt_class_match(node->cls, cp, ctx.icase)) return -1;
            return bt_run(ctx, k, pos - adv, dir);
        }
    }
    case RX_ANY: {
        if (dir >= 0) {
            uint32_t cp; int adv = bt_decode_fwd(ctx.input, ctx.input_len, pos, &cp, ctx.unicode);
            if (adv == 0) return -1;
            if (!ctx.dot_all && bt_is_line_term(cp)) return -1;
            return bt_run(ctx, k, pos + adv, dir);
        } else {
            uint32_t cp; int adv = bt_decode_bwd(ctx.input, pos, &cp, ctx.unicode);
            if (adv == 0) return -1;
            if (!ctx.dot_all && bt_is_line_term(cp)) return -1;
            return bt_run(ctx, k, pos - adv, dir);
        }
    }
    case RX_BOL: {
        bool ok;
        if (pos == 0) ok = true;
        else if (ctx.multiline) { uint32_t cp; bt_utf8_decode_prev(ctx.input, pos, &cp); ok = bt_is_line_term(cp); }
        else ok = false;
        return ok ? bt_run(ctx, k, pos, dir) : -1;
    }
    case RX_EOL: {
        bool ok;
        if (pos == ctx.input_len) ok = true;
        else if (ctx.multiline) { uint32_t cp; bt_utf8_decode(ctx.input, ctx.input_len, pos, &cp); ok = bt_is_line_term(cp); }
        else ok = false;
        return ok ? bt_run(ctx, k, pos, dir) : -1;
    }
    case RX_WORD_B:
    case RX_NWORD_B: {
        bool before = false, after = false;
        if (pos > 0) { uint32_t cp; bt_utf8_decode_prev(ctx.input, pos, &cp); before = bt_is_word(cp); }
        if (pos < ctx.input_len) { uint32_t cp; bt_utf8_decode(ctx.input, ctx.input_len, pos, &cp); after = bt_is_word(cp); }
        bool boundary = (before != after);
        bool ok = (node->type == RX_WORD_B) ? boundary : !boundary;
        return ok ? bt_run(ctx, k, pos, dir) : -1;
    }
    case RX_GROUP: {
        if (node->group_index > 0) {
            int g = node->group_index;
            Cont capk; capk.kind = Cont::C_CAP; capk.up = k; capk.cap_group = g; capk.cap_anchor = pos;
            return bt_match_disj(ctx, node->disj, pos, &capk, dir);
        }
        return bt_match_disj(ctx, node->disj, pos, k, dir);
    }
    case RX_LOOK: {
        int subdir = node->look_behind ? -1 : 1;
        // save all captures
        int n = ctx.ngroups + 1;
        int sv_s[256], sv_e[256];
        for (int i = 0; i < n; i++) { sv_s[i] = ctx.cap_start[i]; sv_e[i] = ctx.cap_end[i]; }
        int r = bt_match_disj(ctx, node->disj, pos, NULL, subdir);
        bool matched = (r >= 0);
        if (node->look_negative) {
            // negative lookaround keeps no captures
            for (int i = 0; i < n; i++) { ctx.cap_start[i] = sv_s[i]; ctx.cap_end[i] = sv_e[i]; }
            if (matched) return -1;
            return bt_run(ctx, k, pos, dir);
        } else {
            if (!matched) {
                for (int i = 0; i < n; i++) { ctx.cap_start[i] = sv_s[i]; ctx.cap_end[i] = sv_e[i]; }
                return -1;
            }
            // positive: keep captures from the first successful match, do not
            // backtrack into the assertion. Continue at the original position.
            int cr = bt_run(ctx, k, pos, dir);
            if (cr < 0) { for (int i = 0; i < n; i++) { ctx.cap_start[i] = sv_s[i]; ctx.cap_end[i] = sv_e[i]; } }
            return cr;
        }
    }
    case RX_BACKREF: {
        int g = node->backref_index;
        if (g <= 0 || g > ctx.ngroups) return bt_run(ctx, k, pos, dir);
        int s = ctx.cap_start[g], e = ctx.cap_end[g];
        if (s < 0 || e < 0 || e == s) return bt_run(ctx, k, pos, dir); // non-participating/empty -> empty match
        int caplen = e - s;
        if (dir >= 0) {
            if (pos + caplen > ctx.input_len) return -1;
            // compare codepoint-by-codepoint with Canonicalize under /i
            int a = s, b = pos;
            while (a < e) {
                uint32_t ca, cb; int aa = bt_utf8_decode(ctx.input, ctx.input_len, a, &ca);
                int bb = bt_utf8_decode(ctx.input, ctx.input_len, b, &cb);
                if (aa == 0 || bb == 0 || !bt_char_eq(ca, cb, ctx.icase)) return -1;
                a += aa; b += bb;
            }
            return bt_run(ctx, k, b, dir);
        } else {
            if (pos - caplen < 0) return -1;
            int a = s, b = pos - caplen;
            while (a < e) {
                uint32_t ca, cb; int aa = bt_utf8_decode(ctx.input, ctx.input_len, a, &ca);
                int bb = bt_utf8_decode(ctx.input, ctx.input_len, b, &cb);
                if (aa == 0 || bb == 0 || !bt_char_eq(ca, cb, ctx.icase)) return -1;
                a += aa; b += bb;
            }
            if (b != pos) return -1;
            return bt_run(ctx, k, pos - caplen, dir);
        }
    }
    case RX_QUANT:
        return bt_repeat(ctx, node, node->qmin, node->qmax, pos, k, dir);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" JsBtRegex* js_bt_compile(const char* pattern, int pattern_len, JsBtFlags flags, Pool* pool) {
    if (!pattern || pattern_len < 0 || !pool) return NULL;
    JsBtRegex* bt = (JsBtRegex*)pool_calloc(pool, sizeof(JsBtRegex));
    bt->pool = pool;
    bt->flags = flags;
    bt->group_count = collect_groups(pattern, pattern_len, pool, &bt->named, &bt->named_count);

    Parser ps; memset(&ps, 0, sizeof(ps));
    ps.p = pattern; ps.len = pattern_len; ps.pos = 0; ps.pool = pool; ps.flags = flags;
    ps.group_counter = 0; ps.named = bt->named; ps.named_count = bt->named_count;
    ps.total_groups = bt->group_count; ps.error = false;

    bt->root = parse_disjunction(&ps);
    if (ps.error || !bt->root || ps.pos != pattern_len) {
        return NULL; // caller falls back to RE2
    }
    return bt;
}

extern "C" int js_bt_group_count(JsBtRegex* bt) { return bt ? bt->group_count : 0; }

extern "C" int js_bt_named_count(JsBtRegex* bt) { return bt ? bt->named_count : 0; }
extern "C" const char* js_bt_named_name(JsBtRegex* bt, int i, int* out_len) {
    if (!bt || i < 0 || i >= bt->named_count) { if (out_len) *out_len = 0; return NULL; }
    if (out_len) *out_len = bt->named[i].name_len;
    return bt->named[i].name;
}
extern "C" int js_bt_named_index(JsBtRegex* bt, int i) {
    if (!bt || i < 0 || i >= bt->named_count) return -1;
    return bt->named[i].index;
}

extern "C" int js_bt_exec(JsBtRegex* bt, const char* input, int input_len, int start_pos,
                          bool anchor_start, int* match_starts, int* match_ends, int max_groups) {
    if (!bt || !bt->root) return 0;
    int ng = bt->group_count;
    if (ng + 1 > 256) return 0; // beyond our fixed capture buffers — fall back
    int cap_start[256], cap_end[256];
    MatchCtx ctx;
    ctx.input = input; ctx.input_len = input_len;
    ctx.cap_start = cap_start; ctx.cap_end = cap_end; ctx.ngroups = ng;
    ctx.icase = bt->flags.ignore_case; ctx.multiline = bt->flags.multiline;
    ctx.dot_all = bt->flags.dot_all; ctx.unicode = bt->flags.unicode;
    ctx.steps = 0; ctx.budget = 8000000;
    ctx.depth = 0; ctx.depth_budget = 5000;
    ctx.overflow = false;

    bool only_at_start = anchor_start || bt->flags.sticky;
    if (start_pos < 0) start_pos = 0;
    for (int sp = start_pos; sp <= input_len; sp++) {
        // only begin a match at a UTF-8 codepoint boundary (never mid-sequence)
        if (sp < input_len && ((unsigned char)input[sp] & 0xC0) == 0x80) {
            if (only_at_start) break;
            continue;
        }
        for (int i = 0; i <= ng; i++) { cap_start[i] = -1; cap_end[i] = -1; }
        int endpos = bt_match_disj(ctx, bt->root, sp, NULL, 1);
        if (endpos >= 0) {
            cap_start[0] = sp; cap_end[0] = endpos;
            int n = ng + 1; if (n > max_groups) n = max_groups;
            for (int i = 0; i < n; i++) { match_starts[i] = cap_start[i]; match_ends[i] = cap_end[i]; }
            return 1;
        }
        if (ctx.overflow) { log_debug("js bt regex: step budget exhausted, bailing to no-match"); return 0; }
        if (only_at_start) break;
    }
    return 0;
}
