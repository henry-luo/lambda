// Engine format images: TeX's \dump and undump (Lambda_Pkg_Latex3 §9.4, Phase IV M3).
//
// A format is the engine state after a preload (the Lambda kernel plus expl3), taken
// where no group, conditional or input is open. Reading expl3-code.tex and the Unicode
// tables it parses takes seconds even in pdfTeX, which is why LaTeX ships expl3 inside
// its format; documents that ask for expl3 start from this image instead. The image is
// built once per process, on first use, and never changes afterwards.
//
// Stored tokens keep their meaning but not their provenance: the files they were read
// from are not part of the image, so the reconstruction writes them canonically.

#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"
#include <pthread.h>
#include <string.h>

namespace tex {

static const char FORMAT_MAGIC[4] = {'L', 'T', 'X', 'F'};
static const uint32_t FORMAT_VERSION = 1;
static const uint16_t LEVEL_ONE = 1;
// expl3 and its Unicode tables need tens of millions of macro calls
static const uint64_t FORMAT_MAX_EXPANSIONS = 400000000;

// the expl3 preload, as latex.ltx makes it (ltexpl): expl3.ltx reads expl3-code.tex and
// fills in the kernel's file hooks, ending with \ExplSyntaxOff; latex.ltx reads it with @ a letter
static const char EXPL3_PRELOAD[] = "\\makeatletter\\input expl3.ltx\\makeatother\n";

// ---------------------------------------------------------------- writer

struct Writer {
    StrBuf* sb;
};

static void put(Writer* w, const void* p, size_t n) { strbuf_append_str_n(w->sb, (const char*)p, n); }
static void put_u8(Writer* w, uint8_t v) { put(w, &v, sizeof(v)); }
static void put_u16(Writer* w, uint16_t v) { put(w, &v, sizeof(v)); }
static void put_u32(Writer* w, uint32_t v) { put(w, &v, sizeof(v)); }
static void put_i32(Writer* w, int32_t v) { put(w, &v, sizeof(v)); }

static void put_bytes(Writer* w, const char* s, uint32_t n) {
    put_u32(w, n);
    if (n) put(w, s, n);
}

// a C string, or none
static void put_str(Writer* w, const char* s) {
    put_u8(w, s ? 1 : 0);
    if (s) put_bytes(w, s, (uint32_t)strlen(s));
}

static void put_token(Writer* w, const Token& t) {
    put_u32(w, t.value);
    put_u8(w, t.kind);
    put_u8(w, t.cat);
    put_u16(w, (uint16_t)(t.flags & ~TF_DIRECT));
}

static void put_span(Writer* w, TokSpan s) {
    put_u32(w, s.count);
    for (uint32_t i = 0; i < s.count; i++) put_token(w, s.data[i]);
}

static void put_glue(Writer* w, const Glue* g) {
    put_u8(w, g ? 1 : 0);
    if (!g) return;
    put_i32(w, g->width);
    put_i32(w, g->stretch);
    put_i32(w, g->shrink);
    put_u8(w, g->stretch_order);
    put_u8(w, g->shrink_order);
}

// ---------------------------------------------------------------- reader

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok;
};

static void get(Reader* r, void* out, size_t n) {
    if (!r->ok || (size_t)(r->end - r->p) < n) {
        r->ok = false;
        memset(out, 0, n);
        return;
    }
    memcpy(out, r->p, n);
    r->p += n;
}

static uint8_t get_u8(Reader* r) { uint8_t v; get(r, &v, sizeof(v)); return v; }
static uint16_t get_u16(Reader* r) { uint16_t v; get(r, &v, sizeof(v)); return v; }
static uint32_t get_u32(Reader* r) { uint32_t v; get(r, &v, sizeof(v)); return v; }
static int32_t get_i32(Reader* r) { int32_t v; get(r, &v, sizeof(v)); return v; }

// a count no larger than the bytes left could hold
static uint32_t get_count(Reader* r, size_t min_item) {
    uint32_t n = get_u32(r);
    if (r->ok && (size_t)n * min_item > (size_t)(r->end - r->p)) r->ok = false;
    return r->ok ? n : 0;
}

static const char* get_bytes(Engine* e, Reader* r, uint32_t* len) {
    uint32_t n = get_count(r, 1);
    if (len) *len = n;
    if (!r->ok) return "";
    const char* s = arena_copy(e, (const char*)r->p, n);
    r->p += n;
    return s;
}

static const char* get_str(Engine* e, Reader* r) {
    return get_u8(r) ? get_bytes(e, r, nullptr) : nullptr;
}

static Token get_token(Reader* r) {
    Token t = {};
    t.value = get_u32(r);
    t.kind = get_u8(r);
    t.cat = get_u8(r);
    t.flags = get_u16(r);
    t.file = -1;
    return t;
}

static TokSpan get_span(Engine* e, Reader* r) {
    uint32_t n = get_count(r, 8);
    if (!n) return TokSpan{nullptr, 0};
    Token* data = (Token*)arena_alloc(e->arena, sizeof(Token) * n);
    for (uint32_t i = 0; i < n; i++) data[i] = get_token(r);
    return TokSpan{data, n};
}

static const Glue* get_glue(Engine* e, Reader* r) {
    if (!get_u8(r)) return nullptr;
    Glue* g = (Glue*)arena_alloc(e->arena, sizeof(Glue));
    g->width = get_i32(r);
    g->stretch = get_i32(r);
    g->shrink = get_i32(r);
    g->stretch_order = get_u8(r);
    g->shrink_order = get_u8(r);
    return g;
}

static void put_list(Writer* w, const lam::ArrayList<uint32_t>& list) {
    put_u32(w, (uint32_t)list.size());
    for (size_t i = 0; i < list.size(); i++) put_u32(w, list[i]);
}

static void get_list(Reader* r, lam::ArrayList<uint32_t>* list) {
    list->clear();
    uint32_t n = get_count(r, 4);
    for (uint32_t i = 0; i < n; i++) list->append(get_u32(r));
}

// ---------------------------------------------------------------- dump

struct MacroRef {
    const Macro* macro;
    uint32_t index;
};

typedef TypedHashMap<MacroRef, HashMapPointerMemberKeyOps<MacroRef, &MacroRef::macro>> MacroIndex;

static void put_macro(Writer* w, const Macro* m) {
    put_span(w, m->params);
    put_span(w, m->body);
    put_span(w, m->opt_default);
    put_u32(w, m->env_name);
    put_u8(w, m->nargs);
    put_u8(w, m->flags);
    put_u8(w, m->xspec ? m->xcount : 0);
    for (uint8_t k = 0; m->xspec && k < m->xcount; k++) {
        const ArgSpec& a = m->xspec[k];
        put_u8(w, (uint8_t)a.type);
        put_u8(w, a.long_arg ? 1 : 0);
        put_token(w, a.open);
        put_token(w, a.close);
        put_span(w, a.dflt);
    }
}

// box registers hold captured output; a span of a source file keeps its bytes as text,
// since the files are not part of the image
static void put_box(Engine* e, Writer* w, const Box* b) {
    put_u8(w, b ? 1 : 0);
    if (!b) return;
    put_u8(w, b->kind);
    put_i32(w, b->width);
    put_i32(w, b->height);
    put_i32(w, b->depth);
    put_u32(w, b->count);
    for (uint32_t i = 0; i < b->count; i++) {
        const OutItem& it = b->items[i];
        if (it.kind == OUT_TOKEN) {
            put_u8(w, OUT_TOKEN);
            put_token(w, it.tok);
        } else if (it.kind == OUT_TEXT) {
            put_u8(w, OUT_TEXT);
            put_bytes(w, it.text, it.text_len);
        } else {
            const SourceFile& f = e->files[(size_t)it.file];
            uint32_t end = it.end < f.length ? it.end : f.length;
            put_u8(w, OUT_TEXT);
            put_bytes(w, f.data + it.start, end > it.start ? end - it.start : 0);
        }
    }
}

static const Box* get_box(Engine* e, Reader* r) {
    if (!get_u8(r)) return nullptr;
    Box* b = (Box*)arena_calloc(e->arena, sizeof(Box));
    b->kind = get_u8(r);
    b->width = get_i32(r);
    b->height = get_i32(r);
    b->depth = get_i32(r);
    b->count = get_count(r, 5);
    if (b->count) b->items = (OutItem*)arena_calloc(e->arena, sizeof(OutItem) * b->count);
    for (uint32_t i = 0; i < b->count && r->ok; i++) {
        OutItem& it = b->items[i];
        it.kind = get_u8(r);
        it.file = -1;
        if (it.kind == OUT_TOKEN) it.tok = get_token(r);
        else if (it.kind == OUT_TEXT) it.text = get_bytes(e, r, &it.text_len);
        else r->ok = false;
    }
    return b;
}

static bool reg_is_default(uint8_t kind, const RegSlot& s) {
    if (s.level != LEVEL_ONE) return false;
    switch (kind) {
    case RK_COUNT: case RK_DIMEN: return s.v.i == 0;
    case RK_SKIP: case RK_MUSKIP: return s.v.g == nullptr;
    case RK_TOKS: return s.v.t.count == 0;
    default: return s.v.b == nullptr;
    }
}

// TeX's \dump: false when the engine is not between commands at the outer level
static bool format_dump(Engine* e, StrBuf* out) {
    if (e->save.size() || e->groups.size() || e->conds.size() || e->input.size() ||
        e->package_frames.size() || e->after_group_tokens.size() || e->cur_level != LEVEL_ONE ||
        e->in_document || e->current_package) {
        log_error("tex-format: cannot dump inside a group, conditional, input or package");
        return false;
    }
    Writer w = {out};
    put(&w, FORMAT_MAGIC, sizeof(FORMAT_MAGIC));
    put_u32(&w, FORMAT_VERSION);

    put_u32(&w, (uint32_t)e->name_list.size());
    for (size_t i = 0; i < e->name_list.size(); i++)
        put_bytes(&w, e->name_list[i].chars, e->name_list[i].len);

    // macros shared through \let are stored once
    MacroIndex index;
    index.init(4096);
    lam::ArrayList<const Macro*> macros(MEM_CAT_INPUT_OTHER, 4096);
    for (size_t i = 0; i < e->eqtb.size(); i++) {
        const Meaning& m = e->eqtb[i].m;
        if (m.type != MT_MACRO || !m.macro) continue;
        MacroRef key = {m.macro, 0};
        if (index.get(key)) continue;
        MacroRef ref = {m.macro, (uint32_t)macros.size()};
        index.set(ref);
        macros.append(m.macro);
    }
    put_u32(&w, (uint32_t)macros.size());
    for (size_t i = 0; i < macros.size(); i++) put_macro(&w, macros[i]);
    for (size_t i = 0; i < e->eqtb.size(); i++) {
        const EqSlot& s = e->eqtb[i];
        put_u8(&w, s.m.type);
        put_u8(&w, s.m.sub);
        put_u8(&w, s.m.cat);
        put_u32(&w, s.m.value);
        put_u16(&w, s.level);
        if (s.m.type == MT_MACRO) {
            MacroRef key = {s.m.macro, 0};
            const MacroRef* found = s.m.macro ? index.get(key) : nullptr;
            put_u32(&w, found ? found->index : UINT32_MAX);
        }
    }
    index.destroy();

    for (uint8_t kind = 0; kind < RK_COUNT_KINDS; kind++) {
        const RegSlot* slots = e->regs[kind];
        uint32_t used = 0;
        for (uint32_t i = 0; slots && i < REGISTER_LIMIT; i++) used += reg_is_default(kind, slots[i]) ? 0 : 1;
        put_u32(&w, used);
        for (uint32_t i = 0; slots && i < REGISTER_LIMIT; i++) {
            const RegSlot& s = slots[i];
            if (reg_is_default(kind, s)) continue;
            put_u32(&w, i);
            put_u16(&w, s.level);
            if (kind == RK_COUNT || kind == RK_DIMEN) put_i32(&w, s.v.i);
            else if (kind == RK_SKIP || kind == RK_MUSKIP) put_glue(&w, s.v.g);
            else if (kind == RK_TOKS) put_span(&w, s.v.t);
            else put_box(e, &w, s.v.b);
        }
    }

    for (uint32_t i = 0; i < IP_COUNT; i++) { put_i32(&w, e->int_pars[i].v); put_u16(&w, e->int_pars[i].level); }
    for (uint32_t i = 0; i < DP_COUNT; i++) { put_i32(&w, e->dimen_pars[i].v); put_u16(&w, e->dimen_pars[i].level); }
    for (uint32_t i = 0; i < GP_COUNT; i++) { put_glue(&w, e->glue_pars[i].v); put_u16(&w, e->glue_pars[i].level); }
    for (uint32_t i = 0; i < TP_COUNT; i++) { put_span(&w, e->toks_pars[i].v); put_u16(&w, e->toks_pars[i].level); }
    for (int t = 0; t < CT_TABLES; t++) {
        for (int c = 0; c < 256; c++) { put_i32(&w, e->codes[t][c].v); put_u16(&w, e->codes[t][c].level); }
    }
    uint32_t wide = 0;
    size_t cursor = 0;
    CodeEntry* entry = nullptr;
    while (e->wide_codes.next(&cursor, &entry)) wide++;
    put_u32(&w, wide);
    cursor = 0;
    while (e->wide_codes.next(&cursor, &entry)) {
        put_u32(&w, entry->key);
        put_i32(&w, entry->value);
        put_u16(&w, entry->level);
    }

    put_u32(&w, (uint32_t)e->fonts.size());
    for (size_t i = 0; i < e->fonts.size(); i++) {
        const Font& f = e->fonts[i];
        put_str(&w, f.name);
        put_u32(&w, f.ident);
        put_i32(&w, f.size);
        put_u32(&w, f.param_base);
        put_i32(&w, f.param_count);
        put_i32(&w, f.hyphen_char);
        put_i32(&w, f.skew_char);
    }
    put_u32(&w, (uint32_t)e->font_params.size());
    for (size_t i = 0; i < e->font_params.size(); i++) put_i32(&w, e->font_params[i]);
    put_i32(&w, e->cur_font.v);
    put_u16(&w, e->cur_font.level);
    for (int s = 0; s < 3; s++) {
        for (int f = 0; f < 16; f++) { put_i32(&w, e->fam_fonts[s][f].v); put_u16(&w, e->fam_fonts[s][f].level); }
    }

    put_u32(&w, (uint32_t)e->packages.size());
    for (size_t i = 0; i < e->packages.size(); i++) {
        const PackageRecord& p = e->packages[i];
        put_str(&w, p.name);
        put_str(&w, p.options);
        put_u8(&w, p.is_class ? 1 : 0);
        put_u8(&w, p.kind);
    }
    put_u32(&w, (uint32_t)e->option_decls.size());
    for (size_t i = 0; i < e->option_decls.size(); i++) {
        put_str(&w, e->option_decls[i].package);
        put_str(&w, e->option_decls[i].name);
        put_span(&w, e->option_decls[i].code);
    }
    put_span(&w, span_of(e->begin_document_hook));
    put_span(&w, span_of(e->end_document_hook));
    put_u32(&w, (uint32_t)e->pass_options.size());
    for (size_t i = 0; i < e->pass_options.size(); i++) put_str(&w, e->pass_options[i]);
    put_list(&w, e->engine_counters);
    put_list(&w, e->counter_resets);
    put_u32(&w, e->random_seed);
    return true;
}

// ---------------------------------------------------------------- undump

static bool format_load(Engine* e, const uint8_t* data, size_t size) {
    Reader r = {data, data + size, true};
    char magic[sizeof(FORMAT_MAGIC)];
    get(&r, magic, sizeof(magic));
    if (memcmp(magic, FORMAT_MAGIC, sizeof(magic)) != 0 || get_u32(&r) != FORMAT_VERSION) return false;

    // the image replaces the names and meanings init made, in the same order of ids
    e->names.destroy();
    e->names.init(2048);
    e->name_list.clear();
    e->eqtb.clear();
    uint32_t names = get_count(&r, 4);
    for (uint32_t i = 0; i < names && r.ok; i++) {
        uint32_t len = 0;
        const char* chars = get_bytes(e, &r, &len);
        if (intern(e, chars, len) != i) r.ok = false;
    }
    uint32_t macro_count = get_count(&r, 16);
    lam::ArrayList<const Macro*> macros(MEM_CAT_INPUT_OTHER, macro_count + 1);
    for (uint32_t i = 0; i < macro_count && r.ok; i++) {
        Macro* m = (Macro*)arena_calloc(e->arena, sizeof(Macro));
        m->params = get_span(e, &r);
        m->body = get_span(e, &r);
        m->opt_default = get_span(e, &r);
        m->env_name = get_u32(&r);
        m->nargs = get_u8(&r);
        m->flags = get_u8(&r);
        m->xcount = get_u8(&r);
        if (m->xcount) {
            ArgSpec* spec = (ArgSpec*)arena_calloc(e->arena, sizeof(ArgSpec) * m->xcount);
            for (uint8_t k = 0; k < m->xcount; k++) {
                spec[k].type = (char)get_u8(&r);
                spec[k].long_arg = get_u8(&r) != 0;
                spec[k].open = get_token(&r);
                spec[k].close = get_token(&r);
                spec[k].dflt = get_span(e, &r);
            }
            m->xspec = spec;
        }
        macros.append(m);
    }
    for (uint32_t i = 0; i < names && r.ok; i++) {
        EqSlot& s = e->eqtb[i];
        s.m = Meaning{};
        s.m.type = get_u8(&r);
        s.m.sub = get_u8(&r);
        s.m.cat = get_u8(&r);
        s.m.value = get_u32(&r);
        s.level = get_u16(&r);
        if (s.m.type == MT_MACRO) {
            uint32_t k = get_u32(&r);
            if (k < macros.size()) s.m.macro = macros[k];
            else r.ok = false;
        }
    }

    for (uint8_t kind = 0; kind < RK_COUNT_KINDS && r.ok; kind++) {
        uint32_t used = get_count(&r, 6);
        for (uint32_t n = 0; n < used && r.ok; n++) {
            uint32_t i = get_u32(&r);
            if (i >= REGISTER_LIMIT) { r.ok = false; break; }
            RegSlot* s = reg_slot(e, kind, i);
            s->level = get_u16(&r);
            if (kind == RK_COUNT || kind == RK_DIMEN) s->v.i = get_i32(&r);
            else if (kind == RK_SKIP || kind == RK_MUSKIP) s->v.g = get_glue(e, &r);
            else if (kind == RK_TOKS) s->v.t = get_span(e, &r);
            else s->v.b = get_box(e, &r);
        }
    }

    for (uint32_t i = 0; i < IP_COUNT; i++) { e->int_pars[i].v = get_i32(&r); e->int_pars[i].level = get_u16(&r); }
    for (uint32_t i = 0; i < DP_COUNT; i++) { e->dimen_pars[i].v = get_i32(&r); e->dimen_pars[i].level = get_u16(&r); }
    for (uint32_t i = 0; i < GP_COUNT; i++) { e->glue_pars[i].v = get_glue(e, &r); e->glue_pars[i].level = get_u16(&r); }
    for (uint32_t i = 0; i < TP_COUNT; i++) { e->toks_pars[i].v = get_span(e, &r); e->toks_pars[i].level = get_u16(&r); }
    for (int t = 0; t < CT_TABLES; t++) {
        for (int c = 0; c < 256; c++) { e->codes[t][c].v = get_i32(&r); e->codes[t][c].level = get_u16(&r); }
    }
    e->wide_codes.destroy();
    e->wide_codes.init(64);
    uint32_t wide = get_count(&r, 10);
    for (uint32_t i = 0; i < wide && r.ok; i++) {
        CodeEntry c = {};
        c.key = get_u32(&r);
        c.value = get_i32(&r);
        c.level = get_u16(&r);
        e->wide_codes.set(c);
    }

    e->fonts.clear();
    uint32_t fonts = get_count(&r, 21);
    for (uint32_t i = 0; i < fonts && r.ok; i++) {
        Font f = {};
        f.name = get_str(e, &r);
        if (!f.name) f.name = "";
        f.ident = get_u32(&r);
        f.size = get_i32(&r);
        f.param_base = get_u32(&r);
        f.param_count = get_i32(&r);
        f.hyphen_char = get_i32(&r);
        f.skew_char = get_i32(&r);
        e->fonts.append(f);
    }
    e->font_params.clear();
    uint32_t params = get_count(&r, 4);
    for (uint32_t i = 0; i < params && r.ok; i++) e->font_params.append(get_i32(&r));
    for (size_t i = 0; i < e->fonts.size(); i++) {
        if ((uint64_t)e->fonts[i].param_base + (uint64_t)e->fonts[i].param_count > e->font_params.size()) r.ok = false;
    }
    e->cur_font.v = get_i32(&r);
    e->cur_font.level = get_u16(&r);
    for (int s = 0; s < 3; s++) {
        for (int f = 0; f < 16; f++) { e->fam_fonts[s][f].v = get_i32(&r); e->fam_fonts[s][f].level = get_u16(&r); }
    }

    e->packages.clear();
    uint32_t packages = get_count(&r, 4);
    for (uint32_t i = 0; i < packages && r.ok; i++) {
        PackageRecord p = {};
        p.name = get_str(e, &r);
        p.options = get_str(e, &r);
        p.is_class = get_u8(&r) != 0;
        p.kind = get_u8(&r);
        e->packages.append(p);
    }
    e->option_decls.clear();
    uint32_t decls = get_count(&r, 6);
    for (uint32_t i = 0; i < decls && r.ok; i++) {
        OptionDecl d = {};
        d.package = get_str(e, &r);
        d.name = get_str(e, &r);
        d.code = get_span(e, &r);
        e->option_decls.append(d);
    }
    TokSpan begin_hook = get_span(e, &r);
    TokSpan end_hook = get_span(e, &r);
    e->begin_document_hook.clear();
    e->end_document_hook.clear();
    for (uint32_t i = 0; i < begin_hook.count; i++) e->begin_document_hook.append(begin_hook.data[i]);
    for (uint32_t i = 0; i < end_hook.count; i++) e->end_document_hook.append(end_hook.data[i]);
    e->pass_options.clear();
    uint32_t passes = get_count(&r, 1);
    for (uint32_t i = 0; i < passes && r.ok; i++) e->pass_options.append(get_str(e, &r));
    get_list(&r, &e->engine_counters);
    get_list(&r, &e->counter_resets);
    e->random_seed = get_u32(&r);
    if (!r.ok || r.p != r.end) return false;

    // ids of the names the engine keeps at hand
    e->cs_par = intern_cstr(e, "par");
    e->cs_relax = intern_cstr(e, "relax");
    e->cs_endcsname = intern_cstr(e, "endcsname");
    e->cs_frozen_relax = intern_cstr(e, "\x02relax");
    e->cs_begin = intern_cstr(e, "begin");
    e->cs_end = intern_cstr(e, "end");
    e->cs_document = intern_cstr(e, "document");
    return true;
}

// ---------------------------------------------------------------- the expl3 format

static pthread_mutex_t g_format_lock = PTHREAD_MUTEX_INITIALIZER;
static StrBuf* g_expl3_image;   // process lifetime once built
static bool g_expl3_failed;

static StrBuf* build_expl3_image() {
    EngineOptions o = {};
    o.max_expansions = FORMAT_MAX_EXPANSIONS;
    Engine* e = engine_create(&o);
    if (!e) return nullptr;
    e->expl3_format = true;   // this run is what reads expl3: no restart
    run_preload(e, "<expl3-format>", EXPL3_PRELOAD, sizeof(EXPL3_PRELOAD) - 1);
    if (e->aborted && e->diagnostics.empty()) log_error("tex-format: the expl3 preload stopped");
    for (size_t i = 0; i < e->diagnostics.size(); i++)
        log_error("tex-format: %s at %s:%u: %s", e->diagnostics[i].code, e->diagnostics[i].file ? e->diagnostics[i].file : "?", e->diagnostics[i].line, e->diagnostics[i].message);
    StrBuf* image = nullptr;
    if (!e->aborted && e->diagnostics.empty()) {
        image = strbuf_new();
        if (!format_dump(e, image)) {
            strbuf_free(image);
            image = nullptr;
        }
    }
    engine_destroy(e);
    if (image) log_info("tex-format: expl3 format built, %zu bytes", image->length);
    return image;
}

int load_expl3_format(Engine* e) {
    pthread_mutex_lock(&g_format_lock);
    if (!g_expl3_image && !g_expl3_failed) {
        g_expl3_image = build_expl3_image();
        g_expl3_failed = g_expl3_image == nullptr;
    }
    StrBuf* image = g_expl3_image;
    pthread_mutex_unlock(&g_format_lock);
    if (!image) return 0;
    if (format_load(e, (const uint8_t*)image->str, image->length)) return 1;
    log_error("tex-format: the expl3 format image does not load");
    return -1;
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
