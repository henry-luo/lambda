// TeX expansion engine: names, meanings, save stack, tokenizer, input stack,
// expansion, macro calls and conditionals (TeX82 §§256-510, e-TeX additions).
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include "../../lib/digest.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

namespace tex {

static const uint16_t LEVEL_ONE = 1;
static const uint32_t MAX_INPUT_DEPTH = 6000;

// conditional limits, as in TeX: if_code < fi_code < else_code < or_code
enum { LIM_IF = 1, LIM_FI = 2, LIM_ELSE = 3, LIM_OR = 4 };

const char* arena_copy(Engine* e, const char* s, size_t n) {
    char* out = (char*)arena_alloc(e->arena, n + 1);
    if (n) memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

// ======================================================================
// names
// ======================================================================

uint32_t intern(Engine* e, const char* chars, uint32_t len) {
    NameEntry key = {chars, len, 0};
    NameEntry* found = e->names.get(key);
    if (found) return found->id;
    const char* stored = arena_copy(e, chars, len);
    uint32_t id = (uint32_t)e->name_list.size();
    NameRec rec = {stored, len};
    e->name_list.append(rec);
    EqSlot slot = {};
    slot.m.type = MT_UNDEFINED;
    slot.m.value = id;
    slot.level = LEVEL_ONE;
    e->eqtb.append(slot);
    NameEntry entry = {stored, len, id};
    e->names.set(entry);
    return id;
}

uint32_t intern_cstr(Engine* e, const char* chars) {
    return intern(e, chars, (uint32_t)strlen(chars));
}

// active characters live in their own namespace: "\x01" + UTF-8
uint32_t intern_active(Engine* e, uint32_t code) {
    char buf[8];
    buf[0] = '\x01';
    size_t n = str_utf8_encode(code, buf + 1, sizeof(buf) - 1);
    if (n == 0) { buf[1] = (char)(code & 0x7f); n = 1; }
    return intern(e, buf, (uint32_t)(n + 1));
}

const NameRec& name_of(Engine* e, uint32_t id) {
    return e->name_list[id];
}

bool name_is_active(const NameRec& rec, uint32_t* code) {
    if (rec.len < 2 || rec.chars[0] != '\x01') return false;
    uint32_t cp = 0;
    if (str_utf8_decode(rec.chars + 1, rec.len - 1, &cp) <= 0) cp = (unsigned char)rec.chars[1];
    if (code) *code = cp;
    return true;
}

// ======================================================================
// meanings and eqtb
// ======================================================================

const Meaning& meaning_of(Engine* e, uint32_t cs) {
    return e->eqtb[cs].m;
}

static Meaning relax_meaning() {
    Meaning m = {};
    m.type = MT_PRIM;
    m.value = P_RELAX;
    return m;
}

Meaning token_meaning(Engine* e, const Token& t) {
    if (t.flags & TF_NOEXPAND) return relax_meaning();
    if (t.kind == TK_CS) return e->eqtb[t.value].m;
    Meaning m = {};
    m.type = MT_CHAR;
    m.value = t.value;
    m.cat = t.cat;
    return m;
}

Meaning make_prim(uint32_t code, uint8_t sub) {
    Meaning m = {};
    m.type = MT_PRIM;
    m.value = code;
    m.sub = sub;
    return m;
}

bool is_expandable(const Meaning& m) {
    if (m.type == MT_MACRO) return true;
    if (m.type == MT_PRIM) return m.value < P_EXPANDABLE_END || latex_expandable(m.value);
    return false;
}

static bool token_lists_equal(TokSpan a, TokSpan b) {
    if (a.count != b.count) return false;
    for (uint32_t i = 0; i < a.count; i++) {
        const Token& x = a.data[i];
        const Token& y = b.data[i];
        if (x.kind != y.kind || x.value != y.value) return false;
        if (x.kind == TK_CHAR && x.cat != y.cat) return false;
    }
    return true;
}

bool meanings_equal(Engine* e, const Meaning& a, const Meaning& b) {
    (void)e;
    if (a.type != b.type) return false;
    switch (a.type) {
    case MT_UNDEFINED: return true;
    case MT_CONSTRUCTOR: return a.value == b.value;
    case MT_MACRO: {
        const Macro* x = a.macro;
        const Macro* y = b.macro;
        if (x == y) return true;
        if ((x->flags & (MF_LONG | MF_OUTER | MF_PROTECTED)) !=
            (y->flags & (MF_LONG | MF_OUTER | MF_PROTECTED))) return false;
        if ((x->flags & MF_LATEX_OPT) != (y->flags & MF_LATEX_OPT)) return false;
        return x->nargs == y->nargs && token_lists_equal(x->params, y->params) &&
            token_lists_equal(x->body, y->body) && token_lists_equal(x->opt_default, y->opt_default);
    }
    case MT_CHAR: return a.value == b.value && a.cat == b.cat;
    default: return a.value == b.value && a.sub == b.sub;
    }
}

static void save_push(Engine* e, const SaveEntry& entry) {
    e->save.append(entry);
}

void define_cs(Engine* e, uint32_t cs, const Meaning& m, bool global) {
    EqSlot& slot = e->eqtb[cs];
    if (global) {
        slot.m = m;
        slot.level = LEVEL_ONE;
        return;
    }
    if (slot.level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = SV_EQ;
        entry.index = cs;
        entry.level = slot.level;
        entry.old.m = slot.m;
        save_push(e, entry);
        slot.level = e->cur_level;
    }
    slot.m = m;
}

void define_primitive(Engine* e, const char* name, uint32_t code, uint8_t sub) {
    uint32_t id = intern_cstr(e, name);
    EqSlot& slot = e->eqtb[id];
    slot.m = make_prim(code, sub);
    slot.level = LEVEL_ONE;
    PrimName pn = {code, sub, name_of(e, id).chars};
    e->prim_names.append(pn);
}

const char* prim_name(Engine* e, uint32_t code, uint8_t sub) {
    for (size_t i = 0; i < e->prim_names.size(); i++) {
        if (e->prim_names[i].code == code && e->prim_names[i].sub == sub) return e->prim_names[i].name;
    }
    return "relax";
}

// ======================================================================
// codes, parameters, registers
// ======================================================================

static int32_t code_default(uint8_t table, uint32_t cp) {
    switch (table) {
    case CT_CAT: return cp >= 128 ? CAT_OTHER : CAT_OTHER;
    case CT_SF: return 1000;
    case CT_MATH: return (int32_t)cp;
    case CT_DEL: return -1;
    default: return 0;
    }
}

int32_t get_code(Engine* e, uint8_t table, uint32_t cp) {
    if (cp < 256) return e->codes[table][cp].v;
    CodeEntry key = {((uint32_t)table << 24) | (cp & 0xffffff), 0, 0};
    CodeEntry* found = e->wide_codes.get(key);
    return found ? found->value : code_default(table, cp);
}

void set_code(Engine* e, uint8_t table, uint32_t cp, int32_t value, bool global) {
    IntSlot* slot = nullptr;
    IntSlot wide = {};
    CodeEntry key = {((uint32_t)table << 24) | (cp & 0xffffff), 0, 0};
    if (cp < 256) {
        slot = &e->codes[table][cp];
    } else {
        CodeEntry* found = e->wide_codes.get(key);
        wide.v = found ? found->value : code_default(table, cp);
        wide.level = found ? found->level : LEVEL_ONE;
        slot = &wide;
    }
    if (global) {
        slot->v = value;
        slot->level = LEVEL_ONE;
    } else {
        if (slot->level != e->cur_level) {
            SaveEntry entry = {};
            entry.kind = SV_CODE;
            entry.sub = table;
            entry.index = cp;
            entry.level = slot->level;
            entry.old.i = slot->v;
            save_push(e, entry);
            slot->level = e->cur_level;
        }
        slot->v = value;
    }
    if (cp >= 256) {
        CodeEntry stored = {key.key, slot->v, slot->level};
        e->wide_codes.set(stored);
    }
}

int32_t int_par(Engine* e, uint32_t which) {
    return e->int_pars[which].v;
}

static void set_int_slot(Engine* e, IntSlot* slot, uint8_t kind, uint32_t index, int32_t value, bool global) {
    if (global) {
        slot->v = value;
        slot->level = LEVEL_ONE;
        return;
    }
    if (slot->level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = kind;
        entry.index = index;
        entry.level = slot->level;
        entry.old.i = slot->v;
        save_push(e, entry);
        slot->level = e->cur_level;
    }
    slot->v = value;
}

void set_int_par(Engine* e, uint32_t which, int32_t value, bool global) {
    set_int_slot(e, &e->int_pars[which], SV_INT_PAR, which, value, global);
}

void set_dimen_par(Engine* e, uint32_t which, int32_t value, bool global) {
    set_int_slot(e, &e->dimen_pars[which], SV_DIMEN_PAR, which, value, global);
}

void set_glue_par(Engine* e, uint32_t which, const Glue* value, bool global) {
    GlueSlot* slot = &e->glue_pars[which];
    if (global) {
        slot->v = value;
        slot->level = LEVEL_ONE;
        return;
    }
    if (slot->level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = SV_GLUE_PAR;
        entry.index = which;
        entry.level = slot->level;
        entry.old_glue = slot->v;
        save_push(e, entry);
        slot->level = e->cur_level;
    }
    slot->v = value;
}

void set_toks_par(Engine* e, uint32_t which, TokSpan value, bool global) {
    ToksSlot* slot = &e->toks_pars[which];
    if (global) {
        slot->v = value;
        slot->level = LEVEL_ONE;
        return;
    }
    if (slot->level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = SV_TOKS_PAR;
        entry.index = which;
        entry.level = slot->level;
        entry.old.r.t = slot->v;
        save_push(e, entry);
        slot->level = e->cur_level;
    }
    slot->v = value;
}

RegSlot* reg_slot(Engine* e, uint8_t kind, uint32_t index) {
    if (index >= REGISTER_LIMIT) index = REGISTER_LIMIT - 1;
    if (!e->regs[kind]) {
        e->regs[kind] = (RegSlot*)arena_calloc(e->arena, sizeof(RegSlot) * REGISTER_LIMIT);
        for (uint32_t i = 0; i < REGISTER_LIMIT; i++) e->regs[kind][i].level = LEVEL_ONE;
    }
    return &e->regs[kind][index];
}

void set_reg(Engine* e, uint8_t kind, uint32_t index, RegValue v, bool global) {
    RegSlot* slot = reg_slot(e, kind, index);
    if (global) {
        slot->v = v;
        slot->level = LEVEL_ONE;
        return;
    }
    if (slot->level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = SV_REG;
        entry.sub = kind;
        entry.index = index;
        entry.level = slot->level;
        entry.old.r = slot->v;
        save_push(e, entry);
        slot->level = e->cur_level;
    }
    slot->v = v;
}

// ======================================================================
// groups and the save stack (TeX §§268-284)
// ======================================================================

void new_save_level(Engine* e, uint8_t type) {
    GroupRecord g = {};
    g.type = type;
    g.save_base = (uint32_t)e->save.size();
    g.origin = e->last_main_offset;
    g.after_start = (uint32_t)e->after_group_tokens.size();
    g.out_start = (uint32_t)e->out.size();
    g.box_target = -1;
    e->groups.append(g);
    if (e->cur_level == 0xffff) {
        diag(e, "tex-capacity", "grouping levels exceeded");
        return;
    }
    e->cur_level++;
}

void unsave(Engine* e) {
    if (e->groups.empty()) return;
    GroupRecord g = e->groups.back();
    e->groups.remove(e->groups.size() - 1);
    if (e->cur_level > LEVEL_ONE) e->cur_level--;
    while (e->save.size() > g.save_base) {
        SaveEntry entry = e->save.back();
        e->save.remove(e->save.size() - 1);
        switch (entry.kind) {
        case SV_EQ: {
            EqSlot& slot = e->eqtb[entry.index];
            if (slot.level == LEVEL_ONE) break;  // a global definition wins
            slot.m = entry.old.m;
            slot.level = entry.level;
            break;
        }
        case SV_REG: {
            RegSlot* slot = reg_slot(e, entry.sub, entry.index);
            if (slot->level == LEVEL_ONE) break;
            slot->v = entry.old.r;
            slot->level = entry.level;
            break;
        }
        case SV_INT_PAR:
        case SV_DIMEN_PAR: {
            IntSlot* slot = entry.kind == SV_INT_PAR ? &e->int_pars[entry.index]
                                                     : &e->dimen_pars[entry.index];
            if (slot->level == LEVEL_ONE) break;
            slot->v = entry.old.i;
            slot->level = entry.level;
            break;
        }
        case SV_GLUE_PAR: {
            GlueSlot* slot = &e->glue_pars[entry.index];
            if (slot->level == LEVEL_ONE) break;
            slot->v = entry.old_glue;
            slot->level = entry.level;
            break;
        }
        case SV_TOKS_PAR: {
            ToksSlot* slot = &e->toks_pars[entry.index];
            if (slot->level == LEVEL_ONE) break;
            slot->v = entry.old.r.t;
            slot->level = entry.level;
            break;
        }
        case SV_CODE: {
            if (entry.index < 256) {
                IntSlot* slot = &e->codes[entry.sub][entry.index];
                if (slot->level == LEVEL_ONE) break;
                slot->v = entry.old.i;
                slot->level = entry.level;
            } else {
                CodeEntry key = {((uint32_t)entry.sub << 24) | (entry.index & 0xffffff), 0, 0};
                CodeEntry* found = e->wide_codes.get(key);
                if (found && found->level == LEVEL_ONE) break;
                CodeEntry stored = {key.key, entry.old.i, entry.level};
                e->wide_codes.set(stored);
            }
            break;
        }
        case SV_FONT: {
            if (e->cur_font.level == LEVEL_ONE) break;
            e->cur_font.v = entry.old.i;
            e->cur_font.level = entry.level;
            break;
        }
        default: break;
        }
    }
    // \aftergroup tokens come back in the order they were given
    uint32_t n = (uint32_t)e->after_group_tokens.size();
    if (n > g.after_start) {
        TokSpan list = freeze_range(e, e->after_group_tokens.data() + g.after_start, n - g.after_start);
        e->after_group_tokens.remove_range(g.after_start, n - g.after_start);
        back_list(e, list);
    }
}

// ======================================================================
// tokens
// ======================================================================

Token make_char(uint32_t code, uint8_t cat) {
    Token t = {};
    t.kind = TK_CHAR;
    t.value = code;
    t.cat = cat;
    t.file = -1;
    return t;
}

Token make_cs(uint32_t id) {
    Token t = {};
    t.kind = TK_CS;
    t.value = id;
    t.file = -1;
    return t;
}

// stored tokens no longer stand where the reader produced them
static void copy_stored(Token* to, const Token* from, uint32_t count) {
    memcpy(to, from, sizeof(Token) * count);
    for (uint32_t i = 0; i < count; i++) to[i].flags &= (uint16_t)~(TF_DIRECT | TF_NOEXPAND);
}

TokSpan freeze_range(Engine* e, const Token* data, uint32_t count) {
    TokSpan span = {nullptr, count};
    if (count == 0) return span;
    Token* copy = (Token*)arena_alloc(e->arena, sizeof(Token) * count);
    copy_stored(copy, data, count);
    span.data = copy;
    return span;
}

TokSpan freeze(Engine* e, const lam::ArrayList<Token>& list) {
    return freeze_range(e, list.data(), (uint32_t)list.size());
}

void str_to_tokens(Engine* e, const char* s, size_t n, lam::ArrayList<Token>* out) {
    (void)e;
    size_t i = 0;
    while (i < n) {
        uint32_t cp = 0;
        int len = str_utf8_decode(s + i, n - i, &cp);
        if (len <= 0) { cp = (unsigned char)s[i]; len = 1; }
        out->append(make_char(cp, cp == ' ' ? CAT_SPACE : CAT_OTHER));
        i += (size_t)len;
    }
}

// ======================================================================
// diagnostics and terminal
// ======================================================================

void diag(Engine* e, const char* code, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Diagnostic d = {};
    d.code = code;
    d.message = arena_copy(e, buf, strlen(buf));
    d.offset = e->last_main_offset;
    FileReader* r = current_reader(e);
    if (r && r->file != e->main_file && r->file >= 0)
        d.file = e->files[(size_t)r->file].name;
    if (r) d.line = r->line_no;
    e->diagnostics.append(d);
    log_debug("tex-engine: %s: %s", code, buf);
}

void term_flush(Engine* e) {
    if (e->term_line->length == 0) return;
    e->messages.append(arena_copy(e, e->term_line->str, e->term_line->length));
    strbuf_reset(e->term_line);
}

void term_print(Engine* e, const char* text, size_t len) {
    // \newlinechar splits terminal output into lines, as TeX's print does
    int32_t nl = int_par(e, IP_NEW_LINE_CHAR);
    for (size_t i = 0; i < len; i++) {
        if (nl >= 0 && nl < 128 && (unsigned char)text[i] == (unsigned char)nl) {
            e->messages.append(arena_copy(e, e->term_line->str, e->term_line->length));
            strbuf_reset(e->term_line);
            continue;
        }
        strbuf_append_char(e->term_line, text[i]);
    }
}

// ======================================================================
// input: sources, readers, the input stack
// ======================================================================

int32_t add_source(Engine* e, const char* name, const char* data, uint32_t length) {
    SourceFile f = {};
    f.name = name;
    f.data = data;
    f.length = length;
    e->files.append(f);
    return (int32_t)e->files.size() - 1;
}

FileReader* current_reader(Engine* e) {
    for (size_t i = e->input.size(); i > 0; i--) {
        if (e->input[i - 1].kind == IN_FILE) return e->input[i - 1].reader;
    }
    return nullptr;
}

void push_file(Engine* e, int32_t file, bool scantokens) {
    if (e->input.size() >= MAX_INPUT_DEPTH) {
        diag(e, "tex-capacity", "input stack size exceeded");
        e->aborted = true;
        return;
    }
    FileReader* r = (FileReader*)arena_calloc(e->arena, sizeof(FileReader));
    const SourceFile& f = e->files[(size_t)file];
    r->file = file;
    r->buf = f.data;
    r->len = f.length;
    r->state = RS_NEW_LINE;
    r->scantokens = scantokens;
    InputLevel level = {};
    level.kind = IN_FILE;
    level.reader = r;
    e->input.append(level);
}

// Transient token buffers (macro expansions, backed-up tokens) are pooled by power-of-two
// capacity and reused once their input level is read, as TeX recycles its token memory;
// without this, every macro call would keep its expansion for the whole run.
// TeX Live's main memory is 5M words; a single list this long is a runaway expansion
static const size_t MAX_LIST_TOKENS = (size_t)1 << 22;

bool list_fits(Engine* e, size_t count) {
    if (count <= MAX_LIST_TOKENS) return true;
    if (!e->aborted) diag(e, "tex-capacity", "main memory size exceeded (a token list of %zu tokens)", count);
    e->aborted = true;
    return false;
}

static Token* pool_tokens(Engine* e, uint32_t count, uint8_t* cls) {
    uint8_t c = 0;
    while (c < 31 && (1u << c) < count) c++;
    *cls = c;
    Token* buf = e->token_pool[c];
    if (buf) {
        memcpy(&e->token_pool[c], buf, sizeof(Token*));   // the free list links through the buffer
        return buf;
    }
    return (Token*)arena_alloc(e->arena, sizeof(Token) << c);
}

static void release_tokens(Engine* e, Token* buf, uint8_t cls) {
    memcpy(buf, &e->token_pool[cls], sizeof(Token*));
    e->token_pool[cls] = buf;
}

// push a pooled buffer as a level that owns it; nothing else may point into the buffer
static void push_owned(Engine* e, Token* buf, uint8_t cls, uint32_t count, bool macro_body) {
    size_t depth = e->input.size();
    push_tokens(e, buf, count, macro_body);
    if (e->input.size() == depth) {
        release_tokens(e, buf, cls);
        return;
    }
    e->input.back().owned = buf;
    e->input.back().pool_class = cls;
}

static void pop_input_level(Engine* e) {
    InputLevel& top = e->input.back();
    if (top.owned) release_tokens(e, top.owned, top.pool_class);
    e->input.remove(e->input.size() - 1);
}

void back_list_copy(Engine* e, const Token* data, uint32_t count) {
    if (count == 0 || !list_fits(e, count)) return;
    uint8_t cls = 0;
    Token* buf = pool_tokens(e, count, &cls);
    copy_stored(buf, data, count);
    push_owned(e, buf, cls, count, false);
}

void back_list_copy(Engine* e, const lam::ArrayList<Token>& list) {
    back_list_copy(e, list.data(), (uint32_t)list.size());
}

void push_tokens(Engine* e, const Token* toks, uint32_t count, bool macro_body) {
    if (count == 0) return;
    if (e->input.size() >= MAX_INPUT_DEPTH) {
        diag(e, "tex-capacity", "input stack size exceeded (infinite recursion?)");
        e->aborted = true;
        return;
    }
    InputLevel level = {};
    level.kind = IN_TOKENS;
    level.toks = toks;
    level.count = count;
    level.is_macro_body = macro_body;
    e->input.append(level);
}

// drop exhausted token levels so back-up and tail calls do not grow the stack (TeX §325, §390)
static void pop_exhausted_levels(Engine* e) {
    while (!e->input.empty()) {
        InputLevel& top = e->input.back();
        if (top.kind == IN_TOKENS && top.index >= top.count) pop_input_level(e);
        else break;
    }
}

void back_input(Engine* e, const Token& t) {
    pop_exhausted_levels(e);
    uint8_t cls = 0;
    Token* copy = pool_tokens(e, 1, &cls);
    *copy = t;
    // TeX backs up cur_tok, which no longer carries a \noexpand marker
    copy->flags &= (uint16_t)~TF_NOEXPAND;
    push_owned(e, copy, cls, 1, false);
}

void back_list(Engine* e, TokSpan list) {
    push_tokens(e, list.data, list.count, false);
}

static bool load_line(FileReader* r) {
    if (r->done) return false;
    uint32_t start = 0;
    if (r->line_loaded) {
        // \endinput stops reading after the current line
        if (r->end_input) { r->done = true; return false; }
        start = r->next_line;
    }
    if (start >= r->len) { r->done = true; return false; }
    uint32_t i = start;
    while (i < r->len && r->buf[i] != '\n' && r->buf[i] != '\r') i++;
    uint32_t end = i;
    uint32_t next = i;
    if (next < r->len) {
        if (r->buf[next] == '\r' && next + 1 < r->len && r->buf[next + 1] == '\n') next += 2;
        else next += 1;
    }
    // TeX removes trailing spaces before appending \endlinechar
    while (end > start && r->buf[end - 1] == ' ') end--;
    r->line_start = start;
    r->line_end = end;
    r->next_line = next;
    r->pos = start;
    r->line_loaded = true;
    r->eol_done = false;
    r->state = RS_NEW_LINE;
    r->line_no++;
    return true;
}

static bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static uint32_t hex_value(char c) {
    return (c >= '0' && c <= '9') ? (uint32_t)(c - '0') : (uint32_t)(c - 'a' + 10);
}

// decode one character at r->pos inside the current line, reducing ^^ forms
// (TeX §355); returns false at the line end
static bool line_char(Engine* e, FileReader* r, uint32_t* cp_out, uint32_t* next_out) {
    uint32_t pos = r->pos;
    if (pos >= r->line_end) return false;
    uint32_t cp = 0;
    int n = str_utf8_decode(r->buf + pos, r->line_end - pos, &cp);
    if (n <= 0) { cp = (unsigned char)r->buf[pos]; n = 1; }
    uint32_t next = pos + (uint32_t)n;
    for (;;) {
        if (get_code(e, CT_CAT, cp) != CAT_SUPER || next >= r->line_end) break;
        if ((unsigned char)r->buf[next] != cp || cp > 127) break;
        if (next + 1 >= r->line_end) break;
        char c = r->buf[next + 1];
        if (next + 2 < r->line_end && is_lower_hex(c) && is_lower_hex(r->buf[next + 2])) {
            cp = hex_value(c) * 16 + hex_value(r->buf[next + 2]);
            next += 3;
        } else if ((unsigned char)c < 128) {
            cp = (unsigned char)c < 64 ? (unsigned char)c + 64 : (unsigned char)c - 64;
            next += 2;
        } else {
            break;
        }
    }
    *cp_out = cp;
    *next_out = next;
    return true;
}

static void finish_token(Engine* e, FileReader* r, Token* t, uint32_t start, uint32_t end) {
    t->file = r->file;
    t->start = start;
    t->end = end;
    t->seq = ++r->seq;
    t->flags |= TF_DIRECT;
    if (r->file == e->main_file && !r->scantokens) e->last_main_offset = start;
}

// TeX §343: get the next token from a file reader
static bool reader_next(Engine* e, FileReader* r, Token* t) {
    for (;;) {
        if (!r->line_loaded || (r->pos >= r->line_end && r->eol_done)) {
            if (!load_line(r)) return false;
        }
        uint32_t cp = 0;
        uint32_t start = r->pos;
        uint32_t next = 0;
        bool at_eol = false;
        if (!line_char(e, r, &cp, &next)) {
            // deliver \endlinechar once per line
            int32_t elc = int_par(e, IP_END_LINE_CHAR);
            r->eol_done = true;
            if (elc < 0 || elc > 0x10ffff) continue;
            cp = (uint32_t)elc;
            start = r->line_end;
            next = r->line_end;
            at_eol = true;
        }
        uint32_t span_end = at_eol ? r->next_line : next;
        r->pos = next;
        uint8_t cat = (uint8_t)get_code(e, CT_CAT, cp);
        *t = Token{};
        switch (cat) {
        case CAT_ESCAPE: {
            uint32_t c2 = 0, after = 0;
            if (at_eol || !line_char(e, r, &c2, &after)) {
                // escape at the end of a line: a control symbol for \endlinechar
                int32_t elc = int_par(e, IP_END_LINE_CHAR);
                char name[8];
                size_t nl = 0;
                if (elc >= 0 && elc <= 0x10ffff) nl = str_utf8_encode((uint32_t)elc, name, sizeof(name));
                t->kind = TK_CS;
                t->value = intern(e, name, (uint32_t)nl);
                r->eol_done = true;
                r->state = RS_MID_LINE;
                finish_token(e, r, t, start, r->next_line);
                return true;
            }
            uint8_t cat2 = (uint8_t)get_code(e, CT_CAT, c2);
            StrBuf* sb = e->scratch;
            size_t mark = sb->length;
            if (cat2 == CAT_LETTER) {
                uint32_t pos = r->pos;
                for (;;) {
                    uint32_t c3 = 0, after3 = 0;
                    r->pos = pos;
                    if (!line_char(e, r, &c3, &after3)) break;
                    if (get_code(e, CT_CAT, c3) != CAT_LETTER) break;
                    strbuf_append_utf8(sb, c3);
                    pos = after3;
                }
                r->pos = pos;
                r->state = RS_SKIP_BLANKS;
            } else {
                strbuf_append_utf8(sb, c2);
                r->pos = after;
                r->state = cat2 == CAT_SPACE ? RS_SKIP_BLANKS : RS_MID_LINE;
            }
            t->kind = TK_CS;
            t->value = intern(e, sb->str + mark, (uint32_t)(sb->length - mark));
            sb->length = mark;
            sb->str[mark] = '\0';
            finish_token(e, r, t, start, r->pos);
            return true;
        }
        case CAT_ACTIVE:
            t->kind = TK_CS;
            t->value = intern_active(e, cp);
            r->state = RS_MID_LINE;
            finish_token(e, r, t, start, span_end);
            return true;
        case CAT_EOL: {
            // end of line: the rest of the line is discarded
            uint8_t state = r->state;
            r->pos = r->line_end;
            r->eol_done = true;
            if (state == RS_NEW_LINE) {
                t->kind = TK_CS;
                t->value = e->cs_par;
                t->flags = TF_PARLINE;
                finish_token(e, r, t, start, r->next_line);
                return true;
            }
            if (state == RS_MID_LINE) {
                *t = make_char(' ', CAT_SPACE);
                finish_token(e, r, t, start, r->next_line);
                return true;
            }
            continue;
        }
        case CAT_IGNORE:
            continue;
        case CAT_SPACE:
            if (r->state == RS_MID_LINE) {
                r->state = RS_SKIP_BLANKS;
                *t = make_char(' ', CAT_SPACE);
                finish_token(e, r, t, start, span_end);
                return true;
            }
            continue;
        case CAT_COMMENT:
            r->pos = r->line_end;
            r->eol_done = true;
            continue;
        case CAT_INVALID:
            diag(e, "tex-invalid-char", "text line contains an invalid character");
            continue;
        default:
            *t = make_char(cp, cat);
            r->state = RS_MID_LINE;
            finish_token(e, r, t, start, span_end);
            return true;
        }
    }
}

// one \read record (TeX §483): the line and its \endlinechar under the current catcodes,
// run by its own reader so the end of the line never reaches the enclosing input. Returns the
// brace balance after the line; an unmatched `}' ends the record and drops the rest of the line.
int32_t tokenize_read_line(Engine* e, const char* text, size_t len, int32_t balance, lam::ArrayList<Token>* out) {
    // the reader needs a line terminator to see an empty line
    char* data = (char*)arena_alloc(e->arena, len + 1);
    memcpy(data, text, len);
    data[len] = '\n';
    if (e->read_source < 0) {
        e->read_source = add_source(e, "\\read", data, (uint32_t)len + 1);
    } else {
        e->files[(size_t)e->read_source].data = data;
        e->files[(size_t)e->read_source].length = (uint32_t)len + 1;
    }
    FileReader r = {};
    r.file = e->read_source;
    r.buf = data;
    r.len = (uint32_t)len + 1;
    r.state = RS_NEW_LINE;
    r.scantokens = true;
    Token t;
    while (reader_next(e, &r, &t)) {
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) balance++;
        else if (t.kind == TK_CHAR && t.cat == CAT_END && --balance < 0) return 0;
        out->append(t);
    }
    return balance;
}

// echo bookkeeping: which direct main-file tokens a command consumed
static void note_read(Engine* e, const Token& t) {
    if ((t.flags & TF_DIRECT) && t.file == e->main_file) {
        if (t.seq < e->echo_min_seq) e->echo_min_seq = t.seq;
        if (t.seq > e->echo_max_seq) e->echo_max_seq = t.seq;
        e->recent_seq[t.seq & 63] = t.seq;
        e->recent_end[t.seq & 63] = t.end;
    } else {
        e->foreign_reads++;
    }
}

bool get_next(Engine* e, Token* t) {
    for (;;) {
        if (e->aborted || e->input.empty()) return false;
        InputLevel& top = e->input.back();
        if (top.kind == IN_TOKENS) {
            if (top.index < top.count) {
                *t = top.toks[top.index++];
                note_read(e, *t);
                return true;
            }
            pop_input_level(e);
            continue;
        }
        FileReader* r = top.reader;
        if (reader_next(e, r, t)) {
            note_read(e, *t);
            return true;
        }
        // end of file: the bottom file ends all input; others get \everyeof
        pop_input_level(e);
        if (e->input.empty()) return false;
        TokSpan eof = e->toks_pars[TP_EVERY_EOF].v;
        if (eof.count) back_list(e, eof);
    }
}

bool get_token(Engine* e, Token* t) {
    return get_next(e, t);
}

bool get_x_token(Engine* e, Token* t) {
    for (;;) {
        if (!get_next(e, t)) return false;
        if (t->flags & TF_NOEXPAND) return true;
        Meaning m = token_meaning(e, *t);
        if (is_expandable(m)) {
            expand(e, *t, m);
            if (e->aborted) return false;
            continue;
        }
        return true;
    }
}

// like get_x_token, but leaves \protected macros unexpanded (edef contexts)
bool get_x_or_protected(Engine* e, Token* t) {
    for (;;) {
        if (!get_next(e, t)) return false;
        if (t->flags & TF_NOEXPAND) return true;
        Meaning m = token_meaning(e, *t);
        if (m.type == MT_MACRO && (m.macro->flags & MF_PROTECTED)) return true;
        if (is_expandable(m)) {
            expand(e, *t, m);
            if (e->aborted) return false;
            continue;
        }
        return true;
    }
}

// ======================================================================
// macro definitions and calls (TeX §§389-399)
// ======================================================================

const Macro* make_macro(Engine* e, TokSpan params, TokSpan body, uint8_t nargs, uint8_t flags) {
    Macro* m = (Macro*)arena_calloc(e->arena, sizeof(Macro));
    m->params = params;
    m->body = body;
    m->nargs = nargs;
    m->flags = flags;
    return m;
}

static bool same_token(const Token& a, const Token& b) {
    if (a.kind != b.kind || a.value != b.value) return false;
    return a.kind != TK_CHAR || a.cat == b.cat;
}

static void print_cs_name(Engine* e, const Token& t, char* buf, size_t cap) {
    if (t.kind != TK_CS) { snprintf(buf, cap, "?"); return; }
    const NameRec& rec = name_of(e, t.value);
    uint32_t code = 0;
    if (name_is_active(rec, &code)) {
        char u[8];
        size_t n = str_utf8_encode(code, u, sizeof(u));
        snprintf(buf, cap, "%.*s", (int)n, u);
    } else {
        snprintf(buf, cap, "\\%.*s", (int)rec.len, rec.chars);
    }
}

// collect one undelimited argument (TeX §392)
static bool scan_undelimited(Engine* e, const Token& call, bool long_ok, lam::ArrayList<Token>* arg) {
    Token t;
    do {
        if (!get_token(e, &t)) return false;
    } while (t.kind == TK_CHAR && t.cat == CAT_SPACE);
    if (t.kind == TK_CS && t.value == e->cs_par && !long_ok) {
        char name[128];
        print_cs_name(e, call, name, sizeof(name));
        diag(e, "tex-runaway-argument", "Paragraph ended before %s was complete", name);
        back_input(e, t);
        return false;
    }
    if (t.kind == TK_CHAR && t.cat == CAT_END) {
        char name[128];
        print_cs_name(e, call, name, sizeof(name));
        diag(e, "tex-extra-brace", "Argument of %s has an extra }", name);
        back_input(e, t);
        return false;
    }
    if (!(t.kind == TK_CHAR && t.cat == CAT_BEGIN)) {
        arg->append(t);
        return true;
    }
    int depth = 1;
    for (;;) {
        if (!get_token(e, &t)) {
            char name[128];
            print_cs_name(e, call, name, sizeof(name));
            diag(e, "tex-runaway-argument", "File ended while scanning use of %s", name);
            return false;
        }
        if (t.kind == TK_CS && t.value == e->cs_par && !long_ok) {
            char name[128];
            print_cs_name(e, call, name, sizeof(name));
            diag(e, "tex-runaway-argument", "Paragraph ended before %s was complete", name);
            back_input(e, t);
            return false;
        }
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) depth++;
        else if (t.kind == TK_CHAR && t.cat == CAT_END && --depth == 0) return true;
        arg->append(t);
    }
}

static void strip_enclosing_braces(lam::ArrayList<Token>* arg) {
    size_t n = arg->size();
    if (n < 2 || !((*arg)[0].kind == TK_CHAR && (*arg)[0].cat == CAT_BEGIN) ||
        !((*arg)[n - 1].kind == TK_CHAR && (*arg)[n - 1].cat == CAT_END)) return;
    int d = 0;
    for (size_t i = 0; i < n; i++) {
        const Token& x = (*arg)[i];
        if (x.kind == TK_CHAR && x.cat == CAT_BEGIN) d++;
        else if (x.kind == TK_CHAR && x.cat == CAT_END && --d == 0 && i != n - 1) return;
    }
    arg->remove(n - 1);
    arg->remove(0);
}

// collect a delimited argument (TeX §§394-397); with brace_delim the
// delimiter is `delim` followed by a left brace that stays in the input
static bool scan_delimited(Engine* e, const Token& call, bool long_ok, const Token* delim,
                           uint32_t dlen, bool brace_delim, lam::ArrayList<Token>* arg) {
    lam::ArrayList<uint8_t> top_level(MEM_CAT_INPUT_OTHER, 16);
    int depth = 0;
    Token t;
    for (;;) {
        if (!get_token(e, &t)) {
            char name[128];
            print_cs_name(e, call, name, sizeof(name));
            diag(e, "tex-runaway-argument", "File ended while scanning use of %s", name);
            return false;
        }
        if (t.kind == TK_CS && t.value == e->cs_par && !long_ok) {
            char name[128];
            print_cs_name(e, call, name, sizeof(name));
            diag(e, "tex-runaway-argument", "Paragraph ended before %s was complete", name);
            back_input(e, t);
            return false;
        }
        if (depth == 0 && brace_delim && t.kind == TK_CHAR && t.cat == CAT_BEGIN) {
            bool match = arg->size() >= dlen;
            size_t base = match ? arg->size() - dlen : 0;
            for (uint32_t k = 0; k < dlen && match; k++) {
                if (!top_level[base + k] || !same_token((*arg)[base + k], delim[k])) match = false;
            }
            if (match) {
                // the brace is consumed here; the macro body re-inserts it (TeX §473)
                arg->remove_range(base, dlen);
                break;
            }
        }
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) depth++;
        if (t.kind == TK_CHAR && t.cat == CAT_END) {
            if (depth == 0) {
                char name[128];
                print_cs_name(e, call, name, sizeof(name));
                diag(e, "tex-extra-brace", "Argument of %s has an extra }", name);
                back_input(e, t);
                return false;
            }
            depth--;
        }
        bool at_top = depth == 0 && !(t.kind == TK_CHAR && t.cat == CAT_END);
        arg->append(t);
        top_level.append(at_top ? 1 : 0);
        if (!brace_delim && depth == 0 && dlen && arg->size() >= dlen) {
            size_t base = arg->size() - dlen;
            bool match = true;
            for (uint32_t k = 0; k < dlen && match; k++) {
                if (!top_level[base + k] || !same_token((*arg)[base + k], delim[k])) match = false;
            }
            if (match) {
                arg->remove_range(base, dlen);
                break;
            }
        }
    }
    // TeX strips one pair of braces enclosing the whole argument
    strip_enclosing_braces(arg);
    return true;
}

// LaTeX optional argument: [ ... ] up to the matching ] at brace depth 0
static bool scan_latex_optional(Engine* e, lam::ArrayList<Token>* arg, bool* present) {
    Token t;
    *present = false;
    for (;;) {
        if (!get_token(e, &t)) return true;
        if (!(t.kind == TK_CHAR && t.cat == CAT_SPACE)) break;
    }
    if (!(t.kind == TK_CHAR && t.value == '[' && t.cat == CAT_OTHER)) {
        back_input(e, t);
        return true;
    }
    *present = true;
    Token close = make_char(']', CAT_OTHER);
    Token call = make_cs(e->cs_relax);
    return scan_delimited(e, call, true, &close, 1, false, arg);
}

bool latex_collect_xargs(Engine* e, const Token& call, const Macro* mac, lam::ArrayList<Token>* args);

// argument scanners shared with the LaTeX layer
bool scan_macro_arg(Engine* e, lam::ArrayList<Token>* out, bool long_ok) {
    Token call = make_cs(e->cs_relax);
    return scan_undelimited(e, call, long_ok, out);
}

bool scan_bracket_arg(Engine* e, lam::ArrayList<Token>* out, bool* present) {
    return scan_latex_optional(e, out, present);
}

bool scan_until_delimiter(Engine* e, const Token* delim, uint32_t dlen, lam::ArrayList<Token>* out) {
    Token call = make_cs(e->cs_relax);
    return scan_delimited(e, call, true, delim, dlen, false, out);
}

bool macro_call(Engine* e, const Token& call, const Meaning& m) {
    const Macro* mac = m.macro;
    if (++e->expansions > e->max_expansions) {
        diag(e, "tex-budget", "expansion budget exhausted (%llu macro calls)",
             (unsigned long long)e->max_expansions);
        e->aborted = true;
        return false;
    }
    bool long_ok = (mac->flags & MF_LONG) != 0;
    lam::ArrayList<Token> args[9] = {
        lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0), lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0),
        lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0), lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0),
        lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0), lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0),
        lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0), lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0),
        lam::ArrayList<Token>(MEM_CAT_INPUT_OTHER, 0)};
    uint32_t argi = 0;
    if (mac->xspec) {
        if (!latex_collect_xargs(e, call, mac, args)) return false;
        argi = mac->xcount;
    } else if (mac->flags & MF_LATEX_OPT) {
        bool present = false;
        if (!scan_latex_optional(e, &args[0], &present)) return false;
        if (!present) {
            for (uint32_t i = 0; i < mac->opt_default.count; i++) args[0].append(mac->opt_default.data[i]);
        }
        argi = 1;
    }
    const Token* p = mac->params.data;
    uint32_t np = mac->params.count;
    uint32_t i = 0;
    // leading delimiters must match exactly
    while (i < np && p[i].kind != TK_MATCH && p[i].kind != TK_END_MATCH) {
        Token t;
        if (!get_token(e, &t)) return false;
        if (!same_token(t, p[i])) {
            char name[128];
            print_cs_name(e, call, name, sizeof(name));
            diag(e, "tex-macro-mismatch", "Use of %s doesn't match its definition", name);
            back_input(e, t);
            return false;
        }
        i++;
    }
    while (i < np && p[i].kind == TK_MATCH) {
        uint32_t j = i + 1;
        while (j < np && p[j].kind != TK_MATCH && p[j].kind != TK_END_MATCH) j++;
        uint32_t dlen = j - i - 1;
        bool brace_delim = j < np && p[j].kind == TK_END_MATCH && p[j].value == 1;
        if (argi >= 9) break;
        bool ok = dlen == 0 && !brace_delim
            ? scan_undelimited(e, call, long_ok, &args[argi])
            : scan_delimited(e, call, long_ok, p + i + 1, dlen, brace_delim, &args[argi]);
        if (!ok) return false;
        argi++;
        i = j;
    }
    // substitute the arguments into the body
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, mac->body.count + 8);
    for (uint32_t k = 0; k < mac->body.count; k++) {
        const Token& b = mac->body.data[k];
        if (b.kind == TK_PARAM) {
            uint32_t idx = b.value - 1;
            if (idx < 9) {
                for (size_t a = 0; a < args[idx].size(); a++) out.append(args[idx][a]);
            }
        } else {
            out.append(b);
        }
    }
    if (out.size() && list_fits(e, out.size())) {
        uint8_t cls = 0;
        Token* data = pool_tokens(e, (uint32_t)out.size(), &cls);
        memcpy(data, out.data(), sizeof(Token) * out.size());
        pop_exhausted_levels(e);
        push_owned(e, data, cls, (uint32_t)out.size(), true);
    }
    return true;
}

// ======================================================================
// conditionals (TeX §§487-510)
// ======================================================================

static void push_cond(Engine* e, uint32_t code) {
    CondRecord rec = {};
    rec.if_limit = e->if_limit;
    rec.cur_if = e->cur_if;
    rec.if_line = e->if_line;
    rec.if_file = e->if_file;
    rec.if_line_no = e->if_line_no;
    e->conds.append(rec);
    e->cur_if = (uint8_t)(code - P_IF + 1);
    e->if_limit = LIM_IF;
    e->if_line = e->last_main_offset;
    FileReader* r = current_reader(e);
    e->if_file = r ? r->file : -1;
    e->if_line_no = r ? r->line_no : 0;
}

static void pop_cond(Engine* e) {
    if (e->conds.empty()) return;
    CondRecord rec = e->conds.back();
    e->conds.remove(e->conds.size() - 1);
    e->if_limit = rec.if_limit;
    e->cur_if = rec.cur_if;
    e->if_line = rec.if_line;
    e->if_file = rec.if_file;
    e->if_line_no = rec.if_line_no;
}

static void change_if_limit(Engine* e, uint8_t limit, size_t p) {
    if (p == e->conds.size()) {
        e->if_limit = limit;
        return;
    }
    if (p < e->conds.size()) e->conds[p].if_limit = limit;
}

static bool is_if_test(const Meaning& m) {
    return m.type == MT_PRIM && m.value >= P_IF && m.value <= P_IFINCSNAME;
}

static bool is_fi_or_else(const Meaning& m) {
    return m.type == MT_PRIM && (m.value == P_FI || m.value == P_ELSE || m.value == P_OR);
}

static uint8_t limit_code(uint32_t prim) {
    return prim == P_FI ? LIM_FI : prim == P_ELSE ? LIM_ELSE : LIM_OR;
}

// skip tokens to the matching \else, \or or \fi at nesting level 0
static uint8_t pass_text(Engine* e) {
    int level = 0;
    Token t;
    for (;;) {
        if (!get_next(e, &t)) {
            diag(e, "tex-incomplete-if", "Incomplete conditional; all text was ignored after it");
            return LIM_FI;
        }
        if (t.kind != TK_CS) continue;
        const Meaning& m = meaning_of(e, t.value);
        if (is_fi_or_else(m)) {
            if (level == 0) return limit_code(m.value);
            if (m.value == P_FI) level--;
        } else if (is_if_test(m)) {
            level++;
        }
    }
}

// the comparison key used by \if and \ifcat (TeX §506)
static void if_char_key(Engine* e, const Token& t, uint32_t* code, uint32_t* cat) {
    Meaning m = token_meaning(e, t);
    if ((t.flags & TF_NOEXPAND) && t.kind == TK_CS) {
        uint32_t c = 0;
        if (name_is_active(name_of(e, t.value), &c)) {
            *code = c;
            *cat = CAT_ACTIVE;
            return;
        }
    }
    if (t.kind == TK_CHAR) {
        *code = t.value;
        *cat = t.cat;
        return;
    }
    if (m.type == MT_CHAR) {
        *code = m.value;
        *cat = m.cat;
        return;
    }
    *code = 0x110000;   // non-character: compares equal only to another
    *cat = 16;
}

static bool scan_relation(Engine* e, uint32_t* rel) {
    Token t;
    do {
        if (!get_x_token(e, &t)) return false;
    } while (t.kind == TK_CHAR && t.cat == CAT_SPACE);
    if (t.kind == TK_CHAR && t.cat == CAT_OTHER && (t.value == '<' || t.value == '=' || t.value == '>')) {
        *rel = t.value;
        return true;
    }
    diag(e, "tex-missing-relation", "Missing = inserted for \\ifnum");
    back_input(e, t);
    *rel = '=';
    return true;
}

static bool compare(int32_t a, int32_t b, uint32_t rel) {
    return rel == '<' ? a < b : rel == '>' ? a > b : a == b;
}

static bool test_ifx(Engine* e) {
    Token a, b;
    if (!get_next(e, &a) || !get_next(e, &b)) return false;
    Meaning ma = token_meaning(e, a);
    Meaning mb = token_meaning(e, b);
    if (a.kind == TK_CHAR && b.kind == TK_CHAR) return a.value == b.value && a.cat == b.cat;
    return meanings_equal(e, ma, mb);
}

static bool name_from_tokens(Engine* e, StrBuf* sb, bool* ok) {
    // expand until \endcsname, collecting character codes
    Token t;
    *ok = true;
    for (;;) {
        if (!get_x_token(e, &t)) { *ok = false; return false; }
        if (t.kind == TK_CHAR) {
            strbuf_append_utf8(sb, t.value);
            continue;
        }
        Meaning m = token_meaning(e, t);
        if (m.type == MT_PRIM && m.value == P_ENDCSNAME) return true;
        diag(e, "tex-missing-endcsname", "Missing \\endcsname inserted");
        back_input(e, t);
        return true;
    }
}

void conditional(Engine* e, const Token& t, uint32_t code, bool unless) {
    (void)t;
    push_cond(e, code);
    size_t save_cond = e->conds.size();
    bool b = false;
    if (code == P_IFCASE) {
        int32_t n = scan_int(e);
        while (n != 0) {
            uint8_t c = pass_text(e);
            if (e->conds.size() == save_cond) {
                if (c == LIM_OR) n--;
                else {
                    if (c == LIM_FI) pop_cond(e);
                    else e->if_limit = LIM_FI;
                    return;
                }
            } else if (c == LIM_FI) {
                pop_cond(e);
            }
        }
        change_if_limit(e, LIM_OR, save_cond);
        return;
    }
    switch (code) {
    case P_IF:
    case P_IFCAT: {
        Token a, c;
        if (!get_x_token(e, &a) || !get_x_token(e, &c)) break;
        uint32_t ac, acat, cc, ccat;
        if_char_key(e, a, &ac, &acat);
        if_char_key(e, c, &cc, &ccat);
        b = code == P_IF ? ac == cc : acat == ccat;
        break;
    }
    case P_IFNUM: {
        int32_t x = scan_int(e);
        uint32_t rel = '=';
        scan_relation(e, &rel);
        int32_t y = scan_int(e);
        b = compare(x, y, rel);
        break;
    }
    case P_IFDIM: {
        int32_t x = scan_dimen(e, false, false, false, 0);
        uint32_t rel = '=';
        scan_relation(e, &rel);
        int32_t y = scan_dimen(e, false, false, false, 0);
        b = compare(x, y, rel);
        break;
    }
    case P_IFODD: b = (scan_int(e) & 1) != 0; break;
    case P_IFVMODE: b = false; break;
    case P_IFHMODE: b = e->math_depth == 0; break;
    case P_IFMMODE: b = e->math_depth > 0; break;
    case P_IFINNER: b = e->math_depth > 0 && !e->display_math; break;
    case P_IFVOID:
    case P_IFHBOX:
    case P_IFVBOX: {
        int32_t n = scan_register_num(e);
        const Box* box = reg_slot(e, RK_BOX, (uint32_t)n)->v.b;
        b = code == P_IFVOID ? box == nullptr
          : code == P_IFHBOX ? box != nullptr && box->kind == 0
          : box != nullptr && box->kind == 1;
        break;
    }
    case P_IFX: b = test_ifx(e); break;
    case P_IFEOF: {
        int32_t n = scan_int(e);
        if (n < 0 || n > 15) {
            diag(e, "tex-bad-number", "Bad number (%d)", n);
            n = 0;
        }
        // a stream is closed until \openin finds its file, and again once \read passes its end
        b = !e->in_streams[n].open;
        break;
    }
    case P_IFTRUE: b = true; break;
    case P_IFFALSE: b = false; break;
    case P_IFDEFINED: {
        Token a;
        if (!get_next(e, &a)) break;
        b = a.kind == TK_CHAR || token_meaning(e, a).type != MT_UNDEFINED;
        break;
    }
    case P_IFCSNAME: {
        StrBuf* sb = strbuf_new();
        bool ok = true;
        name_from_tokens(e, sb, &ok);
        NameEntry key = {sb->str, (uint32_t)sb->length, 0};
        NameEntry* found = e->names.get(key);
        b = found && meaning_of(e, found->id).type != MT_UNDEFINED;
        strbuf_free(sb);
        break;
    }
    case P_IFFONTCHAR: {
        scan_font_ident(e);
        int32_t c = scan_char_num(e);
        b = c >= 0;
        break;
    }
    case P_IFPDFPRIMITIVE: {
        Token a;
        if (!get_next(e, &a)) break;
        b = a.kind == TK_CS && meaning_of(e, a.value).type == MT_PRIM;
        break;
    }
    case P_IFINCSNAME: b = false; break;
    default: break;
    }
    if (unless) b = !b;
    if (b) {
        change_if_limit(e, LIM_ELSE, save_cond);
        return;
    }
    // skip to \else or \fi
    for (;;) {
        uint8_t c = pass_text(e);
        if (e->conds.size() == save_cond) {
            if (c != LIM_OR) {
                if (c == LIM_FI) pop_cond(e);
                else e->if_limit = LIM_FI;
                return;
            }
            diag(e, "tex-extra-or", "Extra \\or");
        } else if (c == LIM_FI) {
            pop_cond(e);
        }
    }
}

void fi_or_else(Engine* e, const Token& t, uint32_t code) {
    uint8_t limit = limit_code(code);
    if (limit > e->if_limit) {
        if (e->if_limit == LIM_IF) {
            // the test is still being scanned: insert \relax (TeX §510)
            back_input(e, t);
            Token relax = make_cs(e->cs_frozen_relax);
            back_input(e, relax);
            return;
        }
        diag(e, "tex-extra-fi", "Extra %s", code == P_FI ? "\\fi" : code == P_ELSE ? "\\else" : "\\or");
        return;
    }
    while (limit != LIM_FI) limit = pass_text(e);
    pop_cond(e);
}

// ======================================================================
// expansion (TeX §366)
// ======================================================================

static void push_string(Engine* e, const char* s, size_t n) {
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, n + 1);
    str_to_tokens(e, s, n, &list);
    if (list.size()) back_list_copy(e, list);
}

static void do_csname(Engine* e) {
    StrBuf* sb = strbuf_new();
    bool ok = true;
    name_from_tokens(e, sb, &ok);
    uint32_t id = intern(e, sb->str, (uint32_t)sb->length);
    strbuf_free(sb);
    if (meaning_of(e, id).type == MT_UNDEFINED) define_cs(e, id, relax_meaning(), false);
    Token t = make_cs(id);
    back_input(e, t);
}

void expand(Engine* e, const Token& t, const Meaning& m) {
    if (m.type == MT_MACRO) {
        macro_call(e, t, m);
        return;
    }
    if (++e->expansions > e->max_expansions) {
        diag(e, "tex-budget", "expansion budget exhausted");
        e->aborted = true;
        return;
    }
    uint32_t code = m.value;
    if (code >= P_IF && code <= P_IFINCSNAME) {
        conditional(e, t, code, false);
        return;
    }
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    switch (code) {
    case P_EXPANDAFTER: {
        Token a, b;
        if (!get_token(e, &a)) return;
        if (!get_token(e, &b)) { back_input(e, a); return; }
        Meaning mb = token_meaning(e, b);
        if (!(b.flags & TF_NOEXPAND) && is_expandable(mb)) expand(e, b, mb);
        else back_input(e, b);
        back_input(e, a);
        return;
    }
    case P_UNLESS: {
        Token b;
        if (!get_token(e, &b)) return;
        Meaning mb = token_meaning(e, b);
        if (is_if_test(mb) && mb.value != P_IFCASE) {
            conditional(e, b, mb.value, true);
            return;
        }
        diag(e, "tex-bad-unless", "You can't use \\unless before that token");
        back_input(e, b);
        return;
    }
    case P_NOEXPAND: {
        Token b;
        if (!get_token(e, &b)) return;
        Meaning mb = token_meaning(e, b);
        if (is_expandable(mb) || (b.kind == TK_CS && mb.type == MT_UNDEFINED)) b.flags |= TF_NOEXPAND;
        Token* copy = (Token*)arena_alloc(e->arena, sizeof(Token));
        *copy = b;
        push_tokens(e, copy, 1, false);
        return;
    }
    case P_CSNAME: do_csname(e); return;
    case P_FI: case P_ELSE: case P_OR: fi_or_else(e, t, code); return;
    case P_STRING: {
        Token b;
        if (!get_token(e, &b)) return;
        if (b.kind == TK_CS) print_cs(e, b.value, sb, false);
        else strbuf_append_utf8(sb, b.value);
        break;
    }
    case P_NUMBER: print_int(scan_int(e), sb); break;
    case P_ROMANNUMERAL: print_roman(scan_int(e), sb); break;
    case P_MEANING: {
        Token b;
        if (!get_token(e, &b)) return;
        print_meaning(e, token_meaning(e, b), sb, &b);
        break;
    }
    case P_FONTNAME: {
        int32_t f = scan_font_ident(e);
        const Font& font = e->fonts[(size_t)f];
        strbuf_append_str(sb, font.name);
        if (f > 0 && font.size != 10 * 65536) {
            strbuf_append_str(sb, " at ");
            print_scaled(font.size, sb);
            strbuf_append_str(sb, "pt");
        }
        break;
    }
    case P_JOBNAME: {
        const char* base = e->opts.base_path;
        if (!base || !*base) { strbuf_append_str(sb, "texput"); break; }
        const char* slash = strrchr(base, '/');
        const char* stem = slash ? slash + 1 : base;
        const char* dot = strrchr(stem, '.');
        strbuf_append_str_n(sb, stem, dot ? (size_t)(dot - stem) : strlen(stem));
        break;
    }
    case P_ETEXREVISION: strbuf_append_str(sb, ".6"); break;
    case P_PDFTEXREVISION: strbuf_append_str(sb, "27"); break;
    case P_THE:
    case P_UNEXPANDED:
    case P_DETOKENIZE: {
        lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
        if (code == P_THE) {
            the_toks(e, &list);
        } else if (code == P_UNEXPANDED) {
            scan_text_into(e, false, &list);
        } else {
            lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
            scan_text_into(e, false, &text);
            StrBuf* tmp = strbuf_new();
            tokens_to_str(e, span_of(text), tmp, false);
            str_to_tokens(e, tmp->str, tmp->length, &list);
            strbuf_free(tmp);
        }
        if (list.size()) back_list_copy(e, list);
        return;
    }
    case P_EXPANDED: {
        lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, true, &text);
        back_list_copy(e, text);
        return;
    }
    case P_SCANTOKENS: {
        lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, false, &text);
        StrBuf* tmp = strbuf_new();
        tokens_to_str(e, span_of(text), tmp, false);
        const char* data = arena_copy(e, tmp->str, tmp->length);
        int32_t file = add_source(e, "\\scantokens", data, (uint32_t)tmp->length);
        strbuf_free(tmp);
        push_file(e, file, true);
        return;
    }
    case P_STRCMP: {
        lam::ArrayList<Token> a(MEM_CAT_INPUT_OTHER, 16);
        lam::ArrayList<Token> b(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, true, &a);
        scan_text_into(e, true, &b);
        StrBuf* sa = strbuf_new();
        StrBuf* sbb = strbuf_new();
        tokens_to_str(e, span_of(a), sa, false);
        tokens_to_str(e, span_of(b), sbb, false);
        int c = strcmp(sa->str ? sa->str : "", sbb->str ? sbb->str : "");
        strbuf_free(sa);
        strbuf_free(sbb);
        strbuf_append_str(sb, c < 0 ? "-1" : c > 0 ? "1" : "0");
        break;
    }
    case P_UNIFORMDEVIATE: {
        int32_t n = scan_int(e);
        // deterministic linear congruential sequence (S12.1.1v2: output depends only on input)
        e->random_seed = e->random_seed * 1103515245u + 12345u;
        int32_t r = n > 0 ? (int32_t)((e->random_seed >> 1) % (uint32_t)n) : 0;
        print_int(r, sb);
        break;
    }
    case P_NORMALDEVIATE: print_int(0, sb); break;
    case P_ESCAPESTRING:
    case P_ESCAPENAME:
    case P_ESCAPEHEX:
    case P_UNESCAPEHEX:
    case P_MDFIVESUM:
    case P_FILESIZE:
    case P_FILEMODDATE:
    case P_CREATIONDATE: {
        static const char* hex = "0123456789ABCDEF";
        // pdfTeX's \pdfmdfivesum file{name} digests a file rather than the text
        bool of_file = code == P_FILESIZE || code == P_FILEMODDATE ||
                       (code == P_MDFIVESUM && scan_keyword(e, "file"));
        lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
        if (code != P_CREATIONDATE) scan_text_into(e, true, &text);
        StrBuf* tmp = strbuf_new();
        tokens_to_str(e, span_of(text), tmp, false);
        const unsigned char* bytes = (const unsigned char*)(tmp->str ? tmp->str : "");
        size_t count = tmp->length;
        bool found = true;
        if (of_file) {
            // only what \input could read is visible (D7.5.2); anything else reads as a missing file
            static const char* exact[] = {""};
            int32_t file = -1;
            StrBuf* fname = strbuf_new();
            append_file_name(fname, tmp->str ? tmp->str : "", tmp->length);
            found = open_input_file(e, fname->str ? fname->str : "", exact, 1, &file, false);
            strbuf_free(fname);
            if (found) {
                bytes = (const unsigned char*)e->files[(size_t)file].data;
                count = e->files[(size_t)file].length;
            }
        }
        if (!found) {
            // pdfTeX expands a query about a missing file to nothing
        } else if (code == P_FILESIZE) {
            print_int((int32_t)count, sb);
        } else if (code == P_FILEMODDATE || code == P_CREATIONDATE) {
            strbuf_append_str(sb, FIXED_PDF_DATE);   // S12.1.1v2: no clock or file-system time
        } else if (code == P_ESCAPEHEX) {
            for (size_t i = 0; i < count; i++) {
                strbuf_append_char(sb, hex[bytes[i] >> 4]);
                strbuf_append_char(sb, hex[bytes[i] & 15]);
            }
        } else if (code == P_ESCAPESTRING) {
            // PDF string syntax: delimiters escaped, bytes outside '!'..'~' in octal
            for (size_t i = 0; i < count; i++) {
                unsigned char c = bytes[i];
                if (c < '!' || c > '~') {
                    strbuf_append_char(sb, '\\');
                    for (int shift = 6; shift >= 0; shift -= 3) strbuf_append_char(sb, (char)('0' + ((c >> shift) & 7)));
                    continue;
                }
                if (c == '(' || c == ')' || c == '\\') strbuf_append_char(sb, '\\');
                strbuf_append_char(sb, (char)c);
            }
        } else if (code == P_ESCAPENAME) {
            // PDF name syntax: delimiters and bytes outside '!'..'~' as #XX
            for (size_t i = 0; i < count; i++) {
                unsigned char c = bytes[i];
                if (c < '!' || c > '~' || strchr("#%()<>[]{}/", c)) {
                    strbuf_append_char(sb, '#');
                    strbuf_append_char(sb, hex[c >> 4]);
                    strbuf_append_char(sb, hex[c & 15]);
                } else {
                    strbuf_append_char(sb, (char)c);
                }
            }
        } else if (code == P_UNESCAPEHEX) {
            // non-hex characters are skipped; an odd final digit is padded with 0
            int pending = -1;
            for (size_t i = 0; i < count; i++) {
                int c = toupper(bytes[i]);
                const char* d = c ? strchr(hex, c) : nullptr;
                if (!d) continue;
                int v = (int)(d - hex);
                if (pending < 0) { pending = v; continue; }
                strbuf_append_utf8(sb, (uint32_t)(pending * 16 + v));
                pending = -1;
            }
            if (pending >= 0) strbuf_append_utf8(sb, (uint32_t)(pending * 16));
        } else if (code == P_MDFIVESUM) {
            uint8_t digest[16];
            if (digest_compute_bits(DIGEST_MD5, bytes, count, digest, sizeof(digest))) {
                for (int i = 0; i < 16; i++) {
                    strbuf_append_char(sb, hex[digest[i] >> 4]);
                    strbuf_append_char(sb, hex[digest[i] & 15]);
                }
            }
        }
        strbuf_free(tmp);
        break;
    }
    case P_INPUT: {
        StrBuf* name = strbuf_new();
        scan_file_name(e, name);
        static const char* exts[] = {"", ".tex"};
        int32_t file = -1;
        if (open_input_file(e, name->str ? name->str : "", exts, 2, &file, true)) push_file(e, file, false);
        strbuf_free(name);
        return;
    }
    case P_ENDINPUT: {
        FileReader* r = current_reader(e);
        if (r) r->end_input = true;
        return;
    }
    case P_TOPMARK: case P_FIRSTMARK: case P_BOTMARK: case P_SPLITFIRSTMARK:
    case P_SPLITBOTMARK:
        return;
    case P_TOPMARKS: case P_FIRSTMARKS: case P_BOTMARKS: case P_SPLITFIRSTMARKS:
    case P_SPLITBOTMARKS:
        scan_int(e);
        return;
    case P_PRIMITIVE_EXP: {
        Token b;
        if (get_token(e, &b)) back_input(e, b);
        return;
    }
    default:
        if (latex_expandable(code)) {
            latex_do(e, t, code);
            return;
        }
        diag(e, "tex-unsupported-primitive", "unsupported expandable primitive");
        return;
    }
    size_t n = sb->length - mark;
    char* copy = (char*)arena_alloc(e->arena, n + 1);
    memcpy(copy, sb->str + mark, n);
    copy[n] = '\0';
    sb->length = mark;
    sb->str[mark] = '\0';
    push_string(e, copy, n);
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
