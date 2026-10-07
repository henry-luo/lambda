// TeX expansion engine: the native LaTeX layer (Lambda_Pkg_Latex3 §9.3-9.4).
//
// Definitions, environments, counters, lengths, package/class resolution and
// the package-author interface. Document-level commands stay constructors
// for the script adapters; package files run only from beside the document.
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "input-latex-tables.h"
#include "input-latex-scanner.h"
#include "../../lib/str.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <stdio.h>
#include <stdlib.h>

namespace tex {

static const uint32_t MAX_LOCAL_FILES = 128;
static const uint64_t MAX_LOCAL_BYTES = 16u * 1024u * 1024u;

// ======================================================================
// primitive names
// ======================================================================

struct LatexPrim {
    const char* name;
    uint32_t code;
    uint8_t sub;
};

static const LatexPrim LATEX_PRIMS[] = {
    {"newcommand", P_NEWCOMMAND, 0}, {"renewcommand", P_NEWCOMMAND, 1},
    {"providecommand", P_NEWCOMMAND, 2}, {"DeclareRobustCommand", P_NEWCOMMAND, 3},
    {"newenvironment", P_NEWENVIRONMENT, 0}, {"renewenvironment", P_NEWENVIRONMENT, 1},
    {"begin", P_BEGIN, 0}, {"end", P_END_ENV, 0}, {"@@end", P_END, 0},
    {"newcounter", P_NEWCOUNTER, 0}, {"setcounter", P_SETCOUNTER, 0},
    {"addtocounter", P_ADDTOCOUNTER, 0}, {"stepcounter", P_STEPCOUNTER, 0},
    {"refstepcounter", P_REFSTEPCOUNTER, 0}, {"newlength", P_NEWLENGTH, 0},
    {"setlength", P_SETLENGTH, 0}, {"addtolength", P_ADDTOLENGTH, 0},
    {"settowidth", P_SETTOBOX, 0}, {"settoheight", P_SETTOBOX, 1}, {"settodepth", P_SETTOBOX, 2},
    {"documentclass", P_DOCUMENTCLASS, 0}, {"LoadClass", P_LOADCLASS, 0},
    {"LoadClassWithOptions", P_LOADCLASSWITHOPTIONS, 0},
    {"usepackage", P_USEPACKAGE, 0}, {"RequirePackage", P_REQUIREPACKAGE, 0},
    {"RequirePackageWithOptions", P_REQUIREPACKAGE, 1},
    {"DeclareOption", P_DECLAREOPTION, 0}, {"ExecuteOptions", P_EXECUTEOPTIONS, 0},
    {"ProcessOptions", P_PROCESSOPTIONS, 0}, {"PassOptionsToPackage", P_PASSOPTIONS, 0},
    {"PassOptionsToClass", P_PASSOPTIONS, 1},
    {"@yargdef", P_YARGDEF, 0}, {"filename@parse", P_FILENAME_PARSE, 0},
    {"@ifpackagewith", P_IFPACKAGEWITH, 0}, {"@ifclasswith", P_IFPACKAGEWITH, 1},
    {"IfFileExists", P_IFFILEEXISTS, 0}, {"InputIfFileExists", P_INPUTIFFILEEXISTS, 0},
    {"AtBeginDocument", P_ATBEGINDOCUMENT, 0}, {"AtEndDocument", P_ATENDDOCUMENT, 0},
    {"AtEndOfPackage", P_ATENDOFPACKAGE, 0}, {"AtEndOfClass", P_ATENDOFPACKAGE, 1},
    {"verb", P_VERB, 0}, {"lstinline", P_RAW_GROUP_CMD, 0},
    {"lambdaconstructor", P_LAMBDA_CONSTRUCTOR, 0},
    {"ProvidesPackage", P_PROVIDESPACKAGE, 0}, {"ProvidesClass", P_PROVIDESPACKAGE, 1},
    {"ProvidesFile", P_PROVIDESPACKAGE, 2}, {"NeedsTeXFormat", P_NEEDSTEXFORMAT, 0},
    {"include", P_INCLUDE, 0},
    {"newif", P_NEWIF, 0},
    {"@ifnextchar", P_IFNEXTCHAR, 0}, {"kernel@ifnextchar", P_IFNEXTCHAR, 0}, {"@ifstar", P_IFSTAR, 0},
    {"@for", P_FOR, 0}, {"@tfor", P_TFOR, 0},
    {"arabic", P_COUNTER_FMT, 0}, {"roman", P_COUNTER_FMT, 1}, {"Roman", P_COUNTER_FMT, 2},
    {"alph", P_COUNTER_FMT, 3}, {"Alph", P_COUNTER_FMT, 4}, {"fnsymbol", P_COUNTER_FMT, 5},
    {"value", P_VALUE, 0},
    {"(", P_MATH_SWITCH, 0}, {")", P_MATH_SWITCH, 1}, {"[", P_MATH_SWITCH, 2}, {"]", P_MATH_SWITCH, 3},
    {"lambda@Roman", P_ROMAN_UPPER, 0},
    {"\x02packageend", P_PACKAGE_END, 0}, {"\x02packagepop", P_PACKAGE_POP, 0},
    {"\x02envend", P_ENV_END_GROUP, 0},
    {"\x02documentend", P_DOCUMENT_END, 0},
    {"NewDocumentCommand", P_NEWDOCUMENTCOMMAND, 0}, {"RenewDocumentCommand", P_NEWDOCUMENTCOMMAND, 1},
    {"ProvideDocumentCommand", P_NEWDOCUMENTCOMMAND, 2}, {"DeclareDocumentCommand", P_NEWDOCUMENTCOMMAND, 3},
    {"NewDocumentEnvironment", P_NEWDOCUMENTCOMMAND, 4},
    {"RenewDocumentEnvironment", P_NEWDOCUMENTCOMMAND, 5},
    {"ProvideDocumentEnvironment", P_NEWDOCUMENTCOMMAND, 6},
    {"DeclareDocumentEnvironment", P_NEWDOCUMENTCOMMAND, 7},
    {"IfNoValueTF", P_IFNOVALUE, 0}, {"IfNoValueT", P_IFNOVALUE, 1}, {"IfNoValueF", P_IFNOVALUE, 2},
    {"IfValueTF", P_IFNOVALUE, 3}, {"IfValueT", P_IFNOVALUE, 4}, {"IfValueF", P_IFNOVALUE, 5},
    {"IfBooleanTF", P_IFBOOLEAN, 0}, {"IfBooleanT", P_IFBOOLEAN, 1}, {"IfBooleanF", P_IFBOOLEAN, 2},
    {"AddToHook", P_ADDTOHOOK, 0}, {"IfFormatAtLeastTF", P_IFFORMATATLEAST, 0},
};

bool latex_expandable(uint32_t code) {
    switch (code) {
    case P_IFPACKAGEWITH: case P_COUNTER_FMT: case P_VALUE:
    case P_ROMAN_UPPER: case P_PACKAGE_END: case P_PACKAGE_POP: case P_IFNOVALUE: case P_IFBOOLEAN:
    case P_IFFORMATATLEAST:
        return true;
    default:
        return false;
    }
}

void latex_init(Engine* e) {
    // LaTeX's catcode regime (latex.ltx sets these before loading anything)
    set_code(e, CT_CAT, '{', CAT_BEGIN, true);
    set_code(e, CT_CAT, '}', CAT_END, true);
    set_code(e, CT_CAT, '$', CAT_MATH, true);
    set_code(e, CT_CAT, '&', CAT_ALIGN, true);
    set_code(e, CT_CAT, '#', CAT_PARAM, true);
    set_code(e, CT_CAT, '^', CAT_SUPER, true);
    set_code(e, CT_CAT, '_', CAT_SUB, true);
    set_code(e, CT_CAT, '\t', CAT_SPACE, true);
    set_code(e, CT_CAT, '~', CAT_ACTIVE, true);
    set_int_par(e, IP_NEW_LINE_CHAR, -1, true);
    for (size_t i = 0; i < sizeof(LATEX_PRIMS) / sizeof(LATEX_PRIMS[0]); i++)
        define_primitive(e, LATEX_PRIMS[i].name, LATEX_PRIMS[i].code, LATEX_PRIMS[i].sub);
}

// ======================================================================
// helpers
// ======================================================================

static uint8_t sub_of(Engine* e, const Token& t) {
    return t.kind == TK_CS ? meaning_of(e, t.value).sub : 0;
}

static bool peek_char(Engine* e, uint32_t c, bool consume) {
    Token t;
    for (;;) {
        if (!get_token(e, &t)) return false;
        if (!(t.kind == TK_CHAR && t.cat == CAT_SPACE)) break;
    }
    bool match = t.kind == TK_CHAR && t.value == c && t.cat == CAT_OTHER;
    if (!match || !consume) back_input(e, t);
    return match;
}

static TokSpan read_arg(Engine* e, bool long_ok = true) {
    lam::ArrayList<Token> arg(MEM_CAT_INPUT_OTHER, 16);
    scan_macro_arg(e, &arg, long_ok);
    return freeze(e, arg);
}

static TokSpan read_optional(Engine* e, bool* present) {
    lam::ArrayList<Token> arg(MEM_CAT_INPUT_OTHER, 8);
    scan_bracket_arg(e, &arg, present);
    return freeze(e, arg);
}

// expand a token list as \edef would and return its characters
static const char* expand_to_string(Engine* e, TokSpan list) {
    // plain characters need no expansion; re-reading them would also hide
    // their source span from the echo tracker
    bool plain = true;
    for (uint32_t i = 0; i < list.count && plain; i++) plain = list.data[i].kind == TK_CHAR;
    if (plain) {
        StrBuf* sb = strbuf_new();
        for (uint32_t i = 0; i < list.count; i++) strbuf_append_utf8(sb, list.data[i].value);
        const char* out = arena_copy(e, sb->str ? sb->str : "", sb->length);
        strbuf_free(sb);
        return out;
    }
    lam::ArrayList<Token> braced(MEM_CAT_INPUT_OTHER, list.count + 2);
    braced.append(make_char('{', CAT_BEGIN));
    for (uint32_t i = 0; i < list.count; i++) braced.append(list.data[i]);
    braced.append(make_char('}', CAT_END));
    back_list_copy(e, braced);
    lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
    scan_text_into(e, true, &text);
    StrBuf* sb = strbuf_new();
    for (size_t i = 0; i < text.size(); i++) {
        const Token& t = text[i];
        if (t.kind == TK_CHAR) strbuf_append_utf8(sb, t.value);
        else {
            const NameRec& rec = name_of(e, t.value);
            strbuf_append_char(sb, '\\');
            strbuf_append_str_n(sb, rec.chars, rec.len);
        }
    }
    const char* out = arena_copy(e, sb->str ? sb->str : "", sb->length);
    strbuf_free(sb);
    return out;
}

static const char* raw_string(Engine* e, TokSpan list) {
    StrBuf* sb = strbuf_new();
    tokens_to_str(e, list, sb, false);
    const char* out = arena_copy(e, sb->str ? sb->str : "", sb->length);
    strbuf_free(sb);
    return out;
}

static void push_string_tokens(Engine* e, const char* s) {
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
    str_to_tokens(e, s, strlen(s), &list);
    if (list.size()) back_list_copy(e, list);
}

// characters as tokens with letter/other catcodes (for building macro bodies)
static void chars_to_tokens(Engine* e, const char* s, lam::ArrayList<Token>* out) {
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        uint32_t cp = 0;
        int len = str_utf8_decode(s + i, n - i, &cp);
        if (len <= 0) { cp = (unsigned char)s[i]; len = 1; }
        uint8_t cat = (uint8_t)get_code(e, CT_CAT, cp);
        if (cat != CAT_LETTER && cat != CAT_SPACE) cat = CAT_OTHER;
        out->append(make_char(cp, cat == CAT_SPACE ? CAT_SPACE : cat));
        i += (size_t)len;
    }
}

static uint32_t cs_from_arg(Engine* e) {
    Token t;
    for (;;) {
        if (!get_token(e, &t)) return intern_cstr(e, "\x02inaccessible");
        if (!(t.kind == TK_CHAR && t.cat == CAT_SPACE)) break;
    }
    if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) {
        uint32_t cs = 0;
        bool found = false;
        for (;;) {
            Token x;
            if (!get_token(e, &x)) break;
            if (x.kind == TK_CHAR && x.cat == CAT_END) break;
            if (x.kind == TK_CHAR && x.cat == CAT_SPACE) continue;
            if (x.kind == TK_CS && !found) { cs = x.value; found = true; }
        }
        if (found) return cs;
        diag(e, "latex-missing-cs", "Missing control sequence in definition");
        return intern_cstr(e, "\x02inaccessible");
    }
    if (t.kind == TK_CS) return t.value;
    diag(e, "latex-missing-cs", "Missing control sequence in definition");
    back_input(e, t);
    return intern_cstr(e, "\x02inaccessible");
}

static bool is_definable(const Meaning& m) {
    return m.type == MT_UNDEFINED || (m.type == MT_PRIM && m.value == P_RELAX);
}

// LaTeX bodies use #1..#n; ## stands for one parameter character
static TokSpan convert_params(Engine* e, TokSpan body, uint8_t nargs) {
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, body.count);
    for (uint32_t i = 0; i < body.count; i++) {
        const Token& t = body.data[i];
        if (t.kind == TK_CHAR && t.cat == CAT_PARAM && i + 1 < body.count) {
            const Token& n = body.data[i + 1];
            if (n.kind == TK_CHAR && n.cat == CAT_PARAM) {
                out.append(t);
                i++;
                continue;
            }
            if (n.kind == TK_CHAR && n.value >= '1' && n.value <= '9' && n.value - '0' <= nargs) {
                Token p = {};
                p.kind = TK_PARAM;
                p.value = n.value - '0';
                out.append(p);
                i++;
                continue;
            }
            diag(e, "latex-illegal-param", "Illegal parameter number in definition");
        }
        out.append(t);
    }
    return freeze(e, out);
}

static TokSpan match_params(Engine* e, uint8_t first, uint8_t nargs) {
    lam::ArrayList<Token> params(MEM_CAT_INPUT_OTHER, 10);
    for (uint8_t k = first; k <= nargs; k++) {
        Token mt = {};
        mt.kind = TK_MATCH;
        mt.value = k;
        params.append(mt);
    }
    Token em = {};
    em.kind = TK_END_MATCH;
    params.append(em);
    return freeze(e, params);
}

static Meaning macro_meaning(Engine* e, TokSpan params, TokSpan body, uint8_t nargs, uint8_t flags) {
    Meaning m = {};
    m.type = MT_MACRO;
    m.macro = make_macro(e, params, body, nargs, flags);
    return m;
}

static void split_list(Engine* e, const char* list, lam::ArrayList<const char*>* out) {
    const char* p = list;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ',') p++;
        const char* end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\n' || end[-1] == '\t')) end--;
        if (end > start) out->append(arena_copy(e, start, (size_t)(end - start)));
    }
}

static bool list_contains(Engine* e, const char* list, const char* item) {
    lam::ArrayList<const char*> items(MEM_CAT_INPUT_OTHER, 8);
    split_list(e, list ? list : "", &items);
    for (size_t i = 0; i < items.size(); i++) {
        if (strcmp(items[i], item) == 0) return true;
    }
    return false;
}

// ======================================================================
// local files (§9.7: read-only, beside the document)
// ======================================================================


static bool ensure_base_dir(Engine* e) {
    if (e->base_dir) return true;
    const char* base = e->opts.base_path;
    if (!base || !*base) return false;
    if (strncmp(base, "file://", 7) == 0) base += 7;
    char* canonical = file_realpath(base);
    char* dir = nullptr;
    if (canonical && file_is_dir(canonical)) {
        dir = canonical;
    } else {
        dir = canonical ? file_path_dirname(canonical) : file_path_dirname(base);
        if (canonical) mem_free(canonical);
    }
    if (!dir) return false;
    e->base_dir = (char*)arena_copy(e, dir, strlen(dir));
    mem_free(dir);
    return true;
}

static bool path_inside(const char* root, const char* path) {
    size_t n = strlen(root);
    return strncmp(root, path, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

// a file this run already read from beside the document (or from the bundle), or -1
static int32_t find_loaded_file(Engine* e, const char* label, bool bundled) {
    for (size_t i = 0; i < e->files.size(); i++) {
        const SourceFile& f = e->files[i];
        if ((bundled ? f.bundled : f.local) && strcmp(f.name, label) == 0) return (int32_t)i;
    }
    return -1;
}

bool open_local_file(Engine* e, const char* name, const char* const* exts, int ext_count,
                     int32_t* file_out, bool report_missing) {
    *file_out = -1;
    if (!name || !*name) return false;
    if (!ensure_base_dir(e)) {
        if (report_missing) diag(e, "tex-no-base", "file `%s' needs a document location (base)", name);
        return false;
    }
    if (name[0] == '/' || strstr(name, "..")) {
        // a probe (\openin, \pdffilesize) just finds nothing; a required file is reported
        if (report_missing) diag(e, "tex-file-outside", "file `%s' is outside the document directory", name);
        return false;
    }
    for (int k = 0; k < ext_count; k++) {
        StrBuf* rel = strbuf_new();
        strbuf_append_str(rel, name);
        strbuf_append_str(rel, exts[k]);
        char* joined = file_path_join(e->base_dir, rel->str);
        char* canonical = joined ? file_realpath(joined) : nullptr;
        if (joined) mem_free(joined);
        bool ok = canonical && !file_is_dir(canonical) && path_inside(e->base_dir, canonical);
        if (ok) {
            // a run sees each file once: reopening reuses the loaded text
            int32_t loaded = find_loaded_file(e, rel->str, false);
            if (loaded >= 0) {
                *file_out = loaded;
                mem_free(canonical);
                strbuf_free(rel);
                return true;
            }
            // the budget covers files from beside the document, not bundled ones or token sources
            uint32_t local_count = 0;
            uint64_t total = 0;
            for (size_t i = 0; i < e->files.size(); i++) {
                if (!e->files[i].local) continue;
                local_count++;
                total += e->files[i].length;
            }
            if (local_count >= MAX_LOCAL_FILES) {
                diag(e, "tex-budget", "too many beside-document files");
                mem_free(canonical);
                strbuf_free(rel);
                return false;
            }
            int64_t size = file_size(canonical);
            if (size < 0 || total + (uint64_t)size > MAX_LOCAL_BYTES) {
                diag(e, "tex-budget", "beside-document files exceed the size budget");
                mem_free(canonical);
                strbuf_free(rel);
                return false;
            }
            char* data = read_text_file(canonical);
            mem_free(canonical);
            if (!data) { strbuf_free(rel); continue; }
            size_t len = strlen(data);
            const char* stored = arena_copy(e, data, len);
            mem_free(data);
            *file_out = add_source(e, arena_copy(e, rel->str, rel->length), stored, (uint32_t)len);
            e->files[(size_t)*file_out].local = true;
            strbuf_free(rel);
            return true;
        }
        if (canonical) mem_free(canonical);
        strbuf_free(rel);
    }
    if (report_missing) diag(e, "tex-file-not-found", "File `%s' not found beside the document", name);
    return false;
}

static char g_resource_dir[1024];

void set_resource_dir(const char* dir) {
    if (!dir) { g_resource_dir[0] = '\0'; return; }
    char* canonical = file_realpath(dir);
    const char* use = canonical ? canonical : dir;
    size_t n = strlen(use);
    if (n >= sizeof(g_resource_dir)) n = sizeof(g_resource_dir) - 1;
    memcpy(g_resource_dir, use, n);
    g_resource_dir[n] = '\0';
    if (canonical) mem_free(canonical);
}

// a bundled resource: Lambda's own copy, never a TeX installation (§9.4)
static bool bundled_path(const char* name, const char* ext, StrBuf* out) {
    if (!g_resource_dir[0] || !name || !*name || name[0] == '/' || strstr(name, "..")) return false;
    strbuf_reset(out);
    strbuf_append_str(out, g_resource_dir);
    strbuf_append_char(out, '/');
    strbuf_append_str(out, name);
    strbuf_append_str(out, ext);
    return file_exists(out->str) && !file_is_dir(out->str);
}

static bool bundled_exists(const char* name, const char* ext) {
    StrBuf* sb = strbuf_new();
    bool ok = bundled_path(name, ext, sb);
    strbuf_free(sb);
    return ok;
}

static bool open_bundled_file(Engine* e, const char* name, const char* ext, int32_t* file_out) {
    StrBuf* sb = strbuf_new();
    bool ok = bundled_path(name, ext, sb);
    if (ok) {
        strbuf_reset(sb);
        strbuf_append_str(sb, name);
        strbuf_append_str(sb, ext);
        int32_t loaded = find_loaded_file(e, sb->str, true);
        if (loaded >= 0) {
            *file_out = loaded;
            strbuf_free(sb);
            return true;
        }
        bundled_path(name, ext, sb);
    }
    char* data = ok ? read_text_file(sb->str) : nullptr;
    if (data) {
        size_t len = strlen(data);
        StrBuf* label = strbuf_new();
        strbuf_append_str(label, name);
        strbuf_append_str(label, ext);
        *file_out = add_source(e, arena_copy(e, label->str, label->length), arena_copy(e, data, len),
                               (uint32_t)len);
        e->files[(size_t)*file_out].bundled = true;
        strbuf_free(label);
        mem_free(data);
    }
    strbuf_free(sb);
    return data != nullptr;
}

// \input from a bundled file stays among the bundled resources
bool open_input_file(Engine* e, const char* name, const char* const* exts, int ext_count,
                     int32_t* file_out, bool report_missing) {
    FileReader* r = current_reader(e);
    if (r && r->file >= 0 && e->files[(size_t)r->file].bundled) {
        for (int k = 0; k < ext_count; k++) {
            if (open_bundled_file(e, name, exts[k], file_out)) return true;
        }
        if (report_missing) diag(e, "tex-file-not-found", "bundled file `%s' not found", name);
        return false;
    }
    return open_local_file(e, name, exts, ext_count, file_out, report_missing);
}

static bool local_file_exists(Engine* e, const char* name, const char* ext) {
    if (!ensure_base_dir(e) || !name || !*name || name[0] == '/' || strstr(name, "..")) return false;
    StrBuf* rel = strbuf_new();
    strbuf_append_str(rel, name);
    strbuf_append_str(rel, ext);
    char* joined = file_path_join(e->base_dir, rel->str);
    char* canonical = joined ? file_realpath(joined) : nullptr;
    bool ok = canonical && !file_is_dir(canonical) && path_inside(e->base_dir, canonical);
    if (joined) mem_free(joined);
    if (canonical) mem_free(canonical);
    strbuf_free(rel);
    return ok;
}

// ======================================================================
// definitions
// ======================================================================

static void do_newcommand(Engine* e, const Token& t, uint8_t sub) {
    EchoMark em = echo_begin(e, t);
    bool star = peek_char(e, '*', true);
    uint32_t cs = cs_from_arg(e);
    bool present = false;
    TokSpan count = read_optional(e, &present);
    uint8_t nargs = 0;
    if (present) {
        const char* s = raw_string(e, count);
        int n = atoi(s);
        if (n < 0 || n > 9) diag(e, "latex-bad-arg-count", "Illegal number of arguments %d", n);
        else nargs = (uint8_t)n;
    }
    bool has_default = false;
    TokSpan dflt = read_optional(e, &has_default);
    TokSpan body = read_arg(e);
    const Meaning& cur = meaning_of(e, cs);
    bool definable = is_definable(cur);
    bool define = true;
    char name[128];
    const NameRec& rec = name_of(e, cs);
    snprintf(name, sizeof(name), "\\%.*s", (int)(rec.len > 100 ? 100 : rec.len), rec.chars);
    if (sub == 0 && !definable) {
        diag(e, "latex-already-defined", "LaTeX Error: Command %s already defined", name);
        define = false;
    } else if (sub == 1 && definable) {
        diag(e, "latex-undefined", "LaTeX Error: Command %s undefined", name);
    } else if (sub == 2 && !definable) {
        define = false;
    }
    if (define) {
        uint8_t flags = (uint8_t)((star ? 0 : MF_LONG) | (has_default ? MF_LATEX_OPT : 0) |
                                  (sub == 3 ? MF_PROTECTED : 0));
        TokSpan params = match_params(e, has_default ? 2 : 1, nargs);
        Meaning m = macro_meaning(e, params, convert_params(e, body, nargs), nargs, flags);
        if (has_default) ((Macro*)m.macro)->opt_default = dflt;
        define_cs(e, cs, m, false);
    }
    echo_finish(e, em);
}

static void do_newenvironment(Engine* e, const Token& t, uint8_t sub) {
    EchoMark em = echo_begin(e, t);
    bool star = peek_char(e, '*', true);
    const char* name = expand_to_string(e, read_arg(e));
    bool present = false;
    TokSpan count = read_optional(e, &present);
    uint8_t nargs = 0;
    if (present) {
        int n = atoi(raw_string(e, count));
        if (n >= 0 && n <= 9) nargs = (uint8_t)n;
    }
    bool has_default = false;
    TokSpan dflt = read_optional(e, &has_default);
    TokSpan begin_body = read_arg(e);
    TokSpan end_body = read_arg(e);
    uint32_t cs_begin = intern_cstr(e, name);
    StrBuf* end_name = strbuf_new();
    strbuf_append_str(end_name, "end");
    strbuf_append_str(end_name, name);
    uint32_t cs_end = intern(e, end_name->str, (uint32_t)end_name->length);
    strbuf_free(end_name);
    bool definable = is_definable(meaning_of(e, cs_begin));
    if (sub == 0 && !definable) {
        diag(e, "latex-already-defined", "LaTeX Error: Environment %s already defined", name);
    } else {
        if (sub == 1 && definable) diag(e, "latex-undefined", "LaTeX Error: Environment %s undefined", name);
        uint8_t flags = (uint8_t)((star ? 0 : MF_LONG) | (has_default ? MF_LATEX_OPT : 0));
        Meaning mb = macro_meaning(e, match_params(e, has_default ? 2 : 1, nargs),
                                   convert_params(e, begin_body, nargs), nargs, flags);
        if (has_default) ((Macro*)mb.macro)->opt_default = dflt;
        define_cs(e, cs_begin, mb, false);
        define_cs(e, cs_end, macro_meaning(e, match_params(e, 1, 0), convert_params(e, end_body, 0), 0, flags & MF_LONG), false);
    }
    echo_finish(e, em);
}

// ======================================================================
// environments and raw captures
// ======================================================================

static bool is_math_env_name(const char* name) {
    if (is_math_environment(name)) return true;
    size_t n = strlen(name);
    if (n < 2 || n >= 96 || name[n - 1] != '*') return false;
    char base[96];
    memcpy(base, name, n - 1);
    base[n - 1] = '\0';
    return is_math_environment(base);
}

// move a reader to an absolute offset of its buffer
static void reader_seek(FileReader* r, uint32_t offset) {
    uint32_t start = offset;
    while (start > 0 && r->buf[start - 1] != '\n' && r->buf[start - 1] != '\r') start--;
    uint32_t i = offset;
    while (i < r->len && r->buf[i] != '\n' && r->buf[i] != '\r') i++;
    uint32_t end = i;
    uint32_t next = i;
    if (next < r->len) {
        if (r->buf[next] == '\r' && next + 1 < r->len && r->buf[next + 1] == '\n') next += 2;
        else next += 1;
    }
    while (end > start && r->buf[end - 1] == ' ') end--;
    r->line_start = start;
    r->line_end = end;
    r->next_line = next;
    r->pos = offset < end ? offset : end;
    r->line_loaded = true;
    r->eol_done = false;
    r->state = RS_MID_LINE;
}

// the reader whose bytes a raw capture continues from, if nothing is backed up
static FileReader* raw_reader(Engine* e, const Token& t) {
    for (size_t i = e->input.size(); i > 0; i--) {
        const InputLevel& level = e->input[i - 1];
        if (level.kind == IN_TOKENS) {
            if (level.index < level.count) return nullptr;  // backed-up tokens come first
            continue;
        }
        if (!(t.flags & TF_DIRECT) || t.file != level.reader->file) return nullptr;
        return level.reader;
    }
    return nullptr;
}

// byte position just after the last token the reader delivered
static uint32_t reader_resume(FileReader* r) {
    return r->pos;
}

static bool raw_environment(Engine* e, const Token& t, const char* name, const EchoMark& em) {
    FileReader* r = raw_reader(e, t);
    if (!r) return false;
    uint32_t from = reader_resume(r);
    size_t name_len = strlen(name);
    uint32_t after = r->len;
    bool found = false;
    for (uint32_t i = from; i < r->len; i++) {
        if (r->buf[i] != '\\') continue;
        if (i + 5 + name_len + 1 <= r->len && memcmp(r->buf + i, "\\end{", 5) == 0 &&
            memcmp(r->buf + i + 5, name, name_len) == 0 && r->buf[i + 5 + name_len] == '}') {
            after = i + 5 + (uint32_t)name_len + 1;
            found = true;
            break;
        }
    }
    if (!found) diag(e, "latex-missing-end", "\\begin{%s} has no matching \\end{%s}", name, name);
    echo_cancel(e, em);
    emit_span(e, r->file, t.start, after, t.seq, r->seq);
    reader_seek(r, after);
    return true;
}

static void begin_env_group(Engine* e, uint32_t id, bool passthrough, bool math) {
    new_save_level(e, GT_ENV);
    GroupRecord& g = e->groups.back();
    g.env_name = id;
    g.env_passthrough = passthrough;
    g.box_kind = math ? 1 : 0;
}

static void set_currenvir(Engine* e, const char* name) {
    lam::ArrayList<Token> body(MEM_CAT_INPUT_OTHER, 16);
    chars_to_tokens(e, name, &body);
    define_cs(e, intern_cstr(e, "@currenvir"),
              macro_meaning(e, match_params(e, 1, 0), freeze(e, body), 0, 0), false);
}

static void emit_env_text(Engine* e, const char* which, const char* name) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '\\');
    strbuf_append_str(sb, which);
    strbuf_append_char(sb, '{');
    strbuf_append_str(sb, name);
    strbuf_append_char(sb, '}');
    emit_text(e, sb->str, sb->length);
    strbuf_free(sb);
}

static void do_begin(Engine* e, const Token& t) {
    EchoMark em = echo_begin(e, t);
    const char* name = expand_to_string(e, read_arg(e));
    uint32_t id = intern_cstr(e, name);
    if (strcmp(name, "document") == 0) {
        e->in_document = true;
        begin_env_group(e, id, true, false);
        if (!echo_finish(e, em)) emit_env_text(e, "begin", name);
        if (e->begin_document_hook.size()) {
            TokSpan hook = freeze(e, e->begin_document_hook);
            e->begin_document_hook.clear();
            back_list(e, hook);
        }
        return;
    }
    if (is_raw_text_environment(name) || strcmp(name, "tikzpicture") == 0) {
        if (raw_environment(e, t, name, em)) return;
        diag(e, "latex-raw-environment", "environment %s needs its own source text", name);
    }
    const Meaning& mb = meaning_of(e, id);
    if (mb.type == MT_MACRO) {
        echo_cancel(e, em);
        begin_env_group(e, id, false, false);
        set_currenvir(e, name);
        back_input(e, make_cs(id));
        return;
    }
    bool math = is_math_env_name(name);
    begin_env_group(e, id, true, math);
    if (!echo_finish(e, em)) emit_env_text(e, "begin", name);
    if (math) e->math_depth++;
    // environment arguments ([placement], {width}, {columns}) are adapter data
    pass_constructor_args(e, false);
}

// \begin and \end inside a constructor argument: engine environments still
// run; everything else passes through as written
bool latex_arg_mode_env(Engine* e, const Token& t, bool begin) {
    lam::ArrayList<Token> raw(MEM_CAT_INPUT_OTHER, 16);
    Token b;
    do {
        if (!get_token(e, &b)) { emit_token(e, t); return false; }
    } while (b.kind == TK_CHAR && b.cat == CAT_SPACE);
    raw.append(b);
    lam::ArrayList<Token> name_tokens(MEM_CAT_INPUT_OTHER, 16);
    if (b.kind == TK_CHAR && b.cat == CAT_BEGIN) {
        int depth = 1;
        while (depth > 0 && get_token(e, &b)) {
            raw.append(b);
            if (b.kind == TK_CHAR && b.cat == CAT_BEGIN) depth++;
            else if (b.kind == TK_CHAR && b.cat == CAT_END && --depth == 0) break;
            name_tokens.append(b);
        }
    } else {
        name_tokens.append(b);
    }
    const char* name = expand_to_string(e, freeze(e, name_tokens));
    uint32_t id = intern_cstr(e, name);
    bool engine_env = meaning_of(e, id).type == MT_MACRO;
    if (engine_env) {
        if (begin) {
            begin_env_group(e, id, false, false);
            set_currenvir(e, name);
            back_input(e, make_cs(id));
        } else if (!e->groups.empty() && e->groups.back().type == GT_ENV && e->groups.back().env_name == id) {
            StrBuf* end_name = strbuf_new();
            strbuf_append_str(end_name, "end");
            strbuf_append_str(end_name, name);
            uint32_t cs_end = intern(e, end_name->str, (uint32_t)end_name->length);
            strbuf_free(end_name);
            Token seq[2] = {make_cs(cs_end), make_cs(intern_cstr(e, "\x02envend"))};
            back_list_copy(e, seq, 2);
        }
        return true;
    }
    emit_token(e, t);
    for (size_t i = 0; i < raw.size(); i++) emit_token(e, raw[i]);
    return false;
}

static void finish_document(Engine* e) {
    // text after \end{document} reaches the digester unchanged
    FileReader* r = e->main_reader;
    if (r && !e->input.empty() && e->input.back().kind == IN_FILE && e->input.back().reader == r) {
        uint32_t from = reader_resume(r);
        emit_span(e, e->main_file, from, r->len, r->seq + 1, r->seq + 1);
    }
    e->document_ended = true;
    e->stop_requested = true;
}

static void do_end_env(Engine* e, const Token& t) {
    EchoMark em = echo_begin(e, t);
    const char* name = expand_to_string(e, read_arg(e));
    uint32_t id = intern_cstr(e, name);
    if (strcmp(name, "document") == 0) {
        if (e->end_document_hook.size()) {
            echo_cancel(e, em);
            lam::ArrayList<Token> hook(MEM_CAT_INPUT_OTHER, e->end_document_hook.size() + 1);
            for (size_t i = 0; i < e->end_document_hook.size(); i++) hook.append(e->end_document_hook[i]);
            hook.append(make_cs(intern_cstr(e, "\x02documentend")));
            e->end_document_hook.clear();
            back_list_copy(e, hook);
            return;
        }
        if (!echo_finish(e, em)) emit_env_text(e, "end", name);
        finish_document(e);
        return;
    }
    bool matches = !e->groups.empty() && e->groups.back().type == GT_ENV && e->groups.back().env_name == id;
    if (!matches) {
        const char* open = "?";
        if (!e->groups.empty() && e->groups.back().type == GT_ENV) open = name_of(e, e->groups.back().env_name).chars;
        diag(e, "latex-env-mismatch", "LaTeX Error: \\begin{%s} ended by \\end{%s}", open, name);
        if (!echo_finish(e, em)) emit_env_text(e, "end", name);
        return;
    }
    GroupRecord g = e->groups.back();
    if (g.env_passthrough) {
        if (!echo_finish(e, em)) emit_env_text(e, "end", name);
        if (g.box_kind == 1 && e->math_depth) e->math_depth--;
        unsave(e);
        return;
    }
    echo_cancel(e, em);
    StrBuf* end_name = strbuf_new();
    strbuf_append_str(end_name, "end");
    strbuf_append_str(end_name, name);
    uint32_t cs_end = intern(e, end_name->str, (uint32_t)end_name->length);
    strbuf_free(end_name);
    Token seq[2] = {make_cs(cs_end), make_cs(intern_cstr(e, "\x02envend"))};
    back_list_copy(e, seq, 2);
}

static void do_verb(Engine* e, const Token& t) {
    FileReader* r = raw_reader(e, t);
    if (!r) {
        diag(e, "latex-verb-in-argument", "\\verb is illegal in a macro argument");
        emit_token(e, t);
        return;
    }
    uint32_t p = reader_resume(r);
    if (p < r->line_end && r->buf[p] == '*') p++;
    if (p >= r->line_end) {
        diag(e, "latex-verb-delimiter", "missing \\verb delimiter");
        emit_token(e, t);
        return;
    }
    char delim = r->buf[p];
    uint32_t q = p + 1;
    while (q < r->line_end && r->buf[q] != delim) q++;
    uint32_t after = q < r->line_end ? q + 1 : r->line_end;
    if (q >= r->line_end) diag(e, "latex-verb-delimiter", "\\verb ended by end of line");
    emit_span(e, r->file, t.start, after, t.seq, r->seq);
    reader_seek(r, after);
}

static void do_raw_group_cmd(Engine* e, const Token& t, uint8_t sub) {
    FileReader* r = raw_reader(e, t);
    if (!r) {
        emit_token(e, t);
        return;
    }
    uint32_t p = reader_resume(r);
    while (p < r->len && (r->buf[p] == ' ' || r->buf[p] == '\t')) p++;
    if (sub == 0 && p < r->len && r->buf[p] == '[') {
        size_t cs = 0, ce = 0;
        size_t after = latex_scan_group_end(r->buf, r->len, p, '[', ']', &cs, &ce);
        if (after) p = (uint32_t)after;
    }
    uint32_t after = p;
    if (p < r->len && r->buf[p] == '{') {
        size_t cs = 0, ce = 0;
        size_t end = latex_scan_group_end(r->buf, r->len, p, '{', '}', &cs, &ce);
        after = end ? (uint32_t)end : r->len;
    } else if (sub == 0 && p < r->len) {
        char delim = r->buf[p];
        uint32_t q = p + 1;
        while (q < r->len && r->buf[q] != delim && r->buf[q] != '\n') q++;
        after = q < r->len && r->buf[q] == delim ? q + 1 : q;
    }
    emit_span(e, r->file, t.start, after, t.seq, r->seq);
    reader_seek(r, after);
}

// ======================================================================
// counters and lengths
// ======================================================================

static uint32_t counter_cs(Engine* e, const char* name) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "c@");
    strbuf_append_str(sb, name);
    uint32_t id = intern(e, sb->str, (uint32_t)sb->length);
    strbuf_free(sb);
    return id;
}

static bool engine_counter(Engine* e, uint32_t cs) {
    const Meaning& m = meaning_of(e, cs);
    return m.type == MT_REGDEF && m.sub == RK_COUNT;
}

// allocate as the kernel's \alloc@ does: \count10/11/12 track counts, dimens
// and skips, below the \insc@unt (\count20) limit, so native and TeX-level
// allocations never hand out the same register
static uint32_t alloc_register(Engine* e, uint8_t kind) {
    uint32_t tracker = kind == RK_COUNT ? 10 : kind == RK_DIMEN ? 11 : 12;
    int32_t next = reg_slot(e, RK_COUNT, tracker)->v.i + 1;
    if (next >= reg_slot(e, RK_COUNT, 20)->v.i) {
        diag(e, "tex-no-room", "No room for a new register");
        return REGISTER_LIMIT - 1;
    }
    RegValue v = {};
    v.i = next;
    set_reg(e, RK_COUNT, tracker, v, true);
    return (uint32_t)next;
}

static void reset_children(Engine* e, uint32_t parent) {
    for (size_t i = 0; i + 1 < e->counter_resets.size(); i += 2) {
        if (e->counter_resets[i] != parent) continue;
        uint32_t child = e->counter_resets[i + 1];
        if (!engine_counter(e, child)) continue;
        const Meaning& m = meaning_of(e, child);
        RegValue v = {};
        v.i = 0;
        set_reg(e, RK_COUNT, m.value, v, true);
        reset_children(e, child);
    }
}

static void do_newcounter(Engine* e, const Token& t) {
    EchoMark em = echo_begin(e, t);
    const char* name = expand_to_string(e, read_arg(e));
    bool present = false;
    TokSpan within = read_optional(e, &present);
    uint32_t cs = counter_cs(e, name);
    bool echoed = false;
    if (engine_counter(e, cs)) {
        diag(e, "latex-counter-defined", "LaTeX Error: Counter `%s' already defined", name);
    } else {
        Meaning m = {};
        m.type = MT_REGDEF;
        m.sub = RK_COUNT;
        m.value = alloc_register(e, RK_COUNT);
        define_cs(e, cs, m, true);
        // \thename typesets the counter in Arabic numerals
        lam::ArrayList<Token> body(MEM_CAT_INPUT_OTHER, 16);
        body.append(make_cs(intern_cstr(e, "arabic")));
        body.append(make_char('{', CAT_BEGIN));
        chars_to_tokens(e, name, &body);
        body.append(make_char('}', CAT_END));
        StrBuf* the = strbuf_new();
        strbuf_append_str(the, "the");
        strbuf_append_str(the, name);
        define_cs(e, intern(e, the->str, (uint32_t)the->length),
                  macro_meaning(e, match_params(e, 1, 0), freeze(e, body), 0, 0), true);
        strbuf_free(the);
        if (present) {
            uint32_t parent = counter_cs(e, expand_to_string(e, within));
            e->counter_resets.append(parent);
            e->counter_resets.append(cs);
        }
        echoed = echo_finish(e, em);
        // a counter declared outside the document is unknown to the script:
        // its commands stay in the engine
        if (!echoed) e->engine_counters.append(cs);
        return;
    }
    echo_finish(e, em);
}

static bool package_counter(Engine* e, uint32_t cs) {
    for (size_t i = 0; i < e->engine_counters.size(); i++)
        if (e->engine_counters[i] == cs) return true;
    return false;
}

static int32_t eval_counter_value(Engine* e, TokSpan value) {
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, value.count + 1);
    for (uint32_t i = 0; i < value.count; i++) list.append(value.data[i]);
    list.append(make_cs(e->cs_frozen_relax));
    back_list_copy(e, list);
    int32_t v = scan_int(e);
    Token x;
    if (get_x_token(e, &x)) {
        if (!(x.kind == TK_CS && x.value == e->cs_frozen_relax)) {
            back_input(e, x);
            diag(e, "latex-counter-value", "counter value has trailing material");
        }
    }
    return v;
}

static void counter_command(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    TokSpan name_tokens = read_arg(e);
    TokSpan value = {};
    if (code == P_SETCOUNTER || code == P_ADDTOCOUNTER) value = read_arg(e);
    const char* name = expand_to_string(e, name_tokens);
    uint32_t cs = counter_cs(e, name);
    bool echoed = echo_finish(e, em);
    if (engine_counter(e, cs)) {
        const Meaning& m = meaning_of(e, cs);
        RegSlot* slot = reg_slot(e, RK_COUNT, m.value);
        RegValue v = {};
        if (code == P_SETCOUNTER) v.i = eval_counter_value(e, value);
        else if (code == P_ADDTOCOUNTER) v.i = slot->v.i + eval_counter_value(e, value);
        else v.i = slot->v.i + 1;
        set_reg(e, RK_COUNT, m.value, v, true);
        if (code == P_STEPCOUNTER || code == P_REFSTEPCOUNTER) reset_children(e, cs);
    }
    if (echoed || package_counter(e, cs)) return;
    // a synthesized counter command still reaches the script adapters
    StrBuf* sb = strbuf_new();
    const NameRec& rec = name_of(e, t.value);
    strbuf_append_char(sb, '\\');
    strbuf_append_str_n(sb, rec.chars, rec.len);
    strbuf_append_char(sb, '{');
    strbuf_append_str(sb, name);
    strbuf_append_char(sb, '}');
    if (value.count) {
        strbuf_append_char(sb, '{');
        if (engine_counter(e, cs)) print_int(reg_slot(e, RK_COUNT, meaning_of(e, cs).value)->v.i, sb);
        else tokens_to_str(e, value, sb, false);
        strbuf_append_char(sb, '}');
    }
    emit_text(e, sb->str, sb->length);
    strbuf_free(sb);
}

static void length_command(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    uint32_t cs = cs_from_arg(e);
    if (code == P_NEWLENGTH) {
        if (!is_definable(meaning_of(e, cs))) {
            diag(e, "latex-already-defined", "LaTeX Error: Command already defined (\\newlength)");
        } else {
            Meaning m = {};
            m.type = MT_REGDEF;
            m.sub = RK_SKIP;
            m.value = alloc_register(e, RK_SKIP);
            define_cs(e, cs, m, true);
        }
        echo_finish(e, em);
        return;
    }
    TokSpan value = read_arg(e);
    bool echoed = echo_finish(e, em);
    const Meaning& m = meaning_of(e, cs);
    if (internal_level(e, m) >= 0 && internal_level(e, m) <= LV_MU) {
        // \setlength{\x}{v} is the assignment \x v\relax (and \advance for \addtolength)
        lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, value.count + 3);
        if (code == P_ADDTOLENGTH) list.append(make_cs(intern_cstr(e, "advance")));
        list.append(make_cs(cs));
        for (uint32_t i = 0; i < value.count; i++) list.append(value.data[i]);
        list.append(make_cs(e->cs_frozen_relax));
        back_list_copy(e, list);
        Token x;
        if (get_x_token(e, &x)) prefixed_command(e, x, token_meaning(e, x), 0);
    }
    if (echoed) return;
    StrBuf* sb = strbuf_new();
    const NameRec& rec = name_of(e, t.value);
    strbuf_append_char(sb, '\\');
    strbuf_append_str_n(sb, rec.chars, rec.len);
    strbuf_append_str(sb, "{\\");
    const NameRec& target = name_of(e, cs);
    strbuf_append_str_n(sb, target.chars, target.len);
    strbuf_append_str(sb, "}{");
    tokens_to_str(e, value, sb, false);
    strbuf_append_char(sb, '}');
    emit_text(e, sb->str, sb->length);
    strbuf_free(sb);
}

static void do_settobox(Engine* e, const Token& t, uint8_t sub) {
    EchoMark em = echo_begin(e, t);
    uint32_t cs = cs_from_arg(e);
    TokSpan text = read_arg(e);
    echo_finish(e, em);
    // without TeX's typesetter, width is estimated from the text length
    uint32_t chars = 0;
    for (uint32_t i = 0; i < text.count; i++) if (text.data[i].kind == TK_CHAR) chars++;
    int32_t quad = e->fonts[(size_t)e->cur_font.v].param_count >= 6 ? font_param(e, e->cur_font.v, 6) : 10 * 65536;
    int32_t v = sub == 0 ? (int32_t)((int64_t)chars * quad / 2) : sub == 1 ? (quad * 7) / 10 : 0;
    if (!e->box_metrics_reported) {
        e->box_metrics_reported = true;
        diag(e, "approximate-box-metrics", "box dimensions are approximate: TeX's typesetting is not reproduced");
    }
    const Meaning& m = meaning_of(e, cs);
    if (m.type == MT_REGDEF && (m.sub == RK_DIMEN || m.sub == RK_SKIP)) {
        RegValue rv = {};
        if (m.sub == RK_DIMEN) rv.i = v;
        else rv.g = new_glue(e, v, 0, 0, 0, 0);
        set_reg(e, m.sub, m.value, rv, false);
    }
}

// ======================================================================
// packages and classes
// ======================================================================

static bool is_adapter(Engine* e, const char* name) {
    for (int i = 0; i < e->opts.adapter_count; i++) {
        if (strcmp(e->opts.adapters[i], name) == 0) return true;
    }
    return false;
}

static PackageRecord* find_package(Engine* e, const char* name, bool is_class) {
    for (size_t i = 0; i < e->packages.size(); i++) {
        PackageRecord& p = e->packages[i];
        if (p.is_class == is_class && strcmp(p.name, name) == 0) return &p;
    }
    return nullptr;
}

// a parameterless macro whose body is the given characters
static void define_text_macro(Engine* e, const char* csname, const char* text, bool global) {
    lam::ArrayList<Token> body(MEM_CAT_INPUT_OTHER, 16);
    chars_to_tokens(e, text, &body);
    define_cs(e, intern_cstr(e, csname), macro_meaning(e, match_params(e, 1, 0), freeze(e, body), 0, 0), global);
}

static void define_named_text(Engine* e, const char* prefix, const char* name, const char* ext,
                              const char* text) {
    StrBuf* cs = strbuf_new();
    strbuf_append_str(cs, prefix);
    strbuf_append_str(cs, name);
    strbuf_append_char(cs, '.');
    strbuf_append_str(cs, ext);
    define_text_macro(e, cs->str, text, true);
    strbuf_free(cs);
}

// LaTeX's file bookkeeping: \ver@<name>.<ext> marks a loaded package or class
// (\@ifpackageloaded tests it), \opt@<name>.<ext> holds its options, and
// \@filelist lists the files
static void record_package(Engine* e, const char* name, const char* options, bool is_class, uint8_t kind) {
    PackageRecord rec = {};
    rec.name = arena_copy(e, name, strlen(name));
    rec.options = arena_copy(e, options ? options : "", options ? strlen(options) : 0);
    rec.is_class = is_class;
    rec.kind = kind;
    e->packages.append(rec);
    if (kind == PK_LOCAL) e->loaded.append(rec.name);
    if (kind == PK_UNKNOWN) return;
    const char* ext = is_class ? "cls" : "sty";
    define_named_text(e, "ver@", name, ext, "");
    define_named_text(e, "opt@", name, ext, rec.options);
    const Meaning& list = meaning_of(e, intern_cstr(e, "@filelist"));
    StrBuf* files = strbuf_new();
    if (list.type == MT_MACRO) tokens_to_str(e, list.macro->body, files, false);
    if (files->length) strbuf_append_char(files, ',');
    strbuf_append_str(files, name);
    strbuf_append_char(files, '.');
    strbuf_append_str(files, ext);
    define_text_macro(e, "@filelist", files->str, true);
    strbuf_free(files);
}

static const char* passed_options(Engine* e, const char* name, const char* options) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, options ? options : "");
    for (size_t i = 0; i + 1 < e->pass_options.size(); i += 2) {
        if (strcmp(e->pass_options[i], name) != 0) continue;
        if (sb->length) strbuf_append_char(sb, ',');
        strbuf_append_str(sb, e->pass_options[i + 1]);
    }
    const char* out = arena_copy(e, sb->str ? sb->str : "", sb->length);
    strbuf_free(sb);
    return out;
}

static void start_package_file(Engine* e, const char* name, const char* options, bool is_class,
                               const char** siblings, uint32_t sibling_count, uint32_t sibling_next,
                               const char* sibling_options, bool bundled) {
    static const char* sty[] = {".sty"};
    static const char* cls[] = {".cls"};
    int32_t file = -1;
    bool opened = bundled ? open_bundled_file(e, name, ".sty", &file)
                          : open_local_file(e, name, is_class ? cls : sty, 1, &file, true);
    if (!opened) return;
    e->files[(size_t)file].package = true;
    const char* opts = passed_options(e, name, options);
    record_package(e, name, opts, is_class, bundled ? PK_ADAPTER : PK_LOCAL);
    PackageFrame f = {};
    f.name = arena_copy(e, name, strlen(name));
    f.options = opts;
    f.is_class = is_class;
    f.saved_at_catcode = get_code(e, CT_CAT, '@');
    f.saved_package = e->current_package;
    f.saved_options = e->current_options;
    f.saved_is_class = e->current_is_class;
    f.siblings = siblings;
    f.sibling_count = sibling_count;
    f.sibling_next = sibling_next;
    f.sibling_options = sibling_options;
    e->package_frames.append(f);
    e->current_package = f.name;
    e->current_options = opts;
    e->current_is_class = is_class;
    if (is_class) {
        e->class_options = opts;
        define_text_macro(e, "@classoptionslist", opts, true);
    }
    // package files are read with @ as a letter, as LaTeX does
    set_code(e, CT_CAT, '@', CAT_LETTER, false);
    back_input(e, make_cs(intern_cstr(e, "\x02packageend")));
    push_file(e, file, false);
    // LaTeX's \@onefilewithoptions opens a file with exactly these tokens, and expl3's
    // \@pushfilename hook reads the three after it
    lam::ArrayList<Token> open(MEM_CAT_INPUT_OTHER, 32);
    open.append(make_cs(intern_cstr(e, "@pushfilename")));
    open.append(make_cs(intern_cstr(e, "xdef")));
    open.append(make_cs(intern_cstr(e, "@currname")));
    open.append(make_char('{', CAT_BEGIN));
    str_to_tokens(e, f.name, strlen(f.name), &open);
    open.append(make_char('}', CAT_END));
    open.append(make_cs(intern_cstr(e, "gdef")));
    open.append(make_cs(intern_cstr(e, "@currext")));
    open.append(make_char('{', CAT_BEGIN));
    str_to_tokens(e, is_class ? "cls" : "sty", 3, &open);
    open.append(make_char('}', CAT_END));
    back_list_copy(e, open);
}

static void emit_declaration(Engine* e, const char* cmd, const char* options, const char* names) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '\\');
    strbuf_append_str(sb, cmd);
    if (options && *options) {
        strbuf_append_char(sb, '[');
        strbuf_append_str(sb, options);
        strbuf_append_char(sb, ']');
    }
    strbuf_append_char(sb, '{');
    strbuf_append_str(sb, names);
    strbuf_append_char(sb, '}');
    emit_text(e, sb->str, sb->length);
    strbuf_free(sb);
}

static void load_package_list(Engine* e, const char** names, uint32_t count, uint32_t from,
                              const char* options);

static void finish_package_frame(Engine* e) {
    if (e->package_frames.empty()) return;
    PackageFrame f = e->package_frames.back();
    e->package_frames.remove(e->package_frames.size() - 1);
    set_code(e, CT_CAT, '@', f.saved_at_catcode, false);
    e->current_package = f.saved_package;
    e->current_options = f.saved_options;
    e->current_is_class = f.saved_is_class;
    define_text_macro(e, "@currname", f.saved_package ? f.saved_package : "", true);
    define_text_macro(e, "@currext", f.saved_package ? (f.saved_is_class ? "cls" : "sty") : "", true);
    if (f.sibling_next < f.sibling_count)
        load_package_list(e, f.siblings, f.sibling_count, f.sibling_next, f.sibling_options);
}

// load names[from..]: adapters are recorded; the first beside-document file
// starts, and its frame continues the list when it ends
static void load_package_list(Engine* e, const char** names, uint32_t count, uint32_t from,
                              const char* options) {
    for (uint32_t i = from; i < count; i++) {
        const char* name = names[i];
        if (find_package(e, name, false)) continue;
        if (bundled_exists(name, ".sty")) {
            start_package_file(e, name, options, false, names, count, i + 1, options, true);
            return;
        }
        if (!is_adapter(e, name) && local_file_exists(e, name, ".sty")) {
            start_package_file(e, name, options, false, names, count, i + 1, options, false);
            return;
        }
    }
}

static void do_usepackage(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    bool present = false;
    TokSpan opt_tokens = read_optional(e, &present);
    const char* options = present ? raw_string(e, opt_tokens) : "";
    const char* list = expand_to_string(e, read_arg(e));
    bool date_present = false;
    read_optional(e, &date_present);
    if (sub_of(e, t) == 1 && e->current_options) options = e->current_options;  // ...WithOptions
    lam::ArrayList<const char*> names(MEM_CAT_INPUT_OTHER, 8);
    split_list(e, list, &names);
    // expl3 comes from the format: stop, and the caller reruns from it (once)
    if (!e->expl3_format && !e->opts.expl3) {
        for (size_t i = 0; i < names.size(); i++) {
            if (strcmp(names[i], "expl3") != 0) continue;
            echo_cancel(e, em);
            e->wants_expl3 = true;
            e->aborted = true;
            return;
        }
    }
    StrBuf* passthrough = strbuf_new();
    bool any_local = false;
    for (size_t i = 0; i < names.size(); i++) {
        const char* name = names[i];
        if (find_package(e, name, false)) {
            if (!any_local) { /* duplicate loads are idempotent */ }
            if (passthrough->length) strbuf_append_char(passthrough, ',');
            strbuf_append_str(passthrough, name);
            continue;
        }
        if (bundled_exists(name, ".sty")) {
            // programming packages run on the engine; an adapter still sees its declaration
            any_local = true;
            if (is_adapter(e, name)) {
                if (passthrough->length) strbuf_append_char(passthrough, ',');
                strbuf_append_str(passthrough, name);
            }
            continue;
        }
        if (!is_adapter(e, name) && local_file_exists(e, name, ".sty")) {
            any_local = true;
            continue;
        }
        record_package(e, name, options, false, is_adapter(e, name) ? PK_ADAPTER : PK_UNKNOWN);
        if (passthrough->length) strbuf_append_char(passthrough, ',');
        strbuf_append_str(passthrough, name);
    }
    bool in_package = !e->package_frames.empty();
    if (!any_local) {
        if (!echo_finish(e, em) && passthrough->length)
            emit_declaration(e, "usepackage", options, passthrough->str);
    } else {
        echo_cancel(e, em);
        if (passthrough->length) emit_declaration(e, "usepackage", options, passthrough->str);
        const char** stored = (const char**)arena_alloc(e->arena, sizeof(char*) * (names.size() + 1));
        for (size_t i = 0; i < names.size(); i++) stored[i] = names[i];
        load_package_list(e, stored, (uint32_t)names.size(), 0, arena_copy(e, options, strlen(options)));
    }
    (void)in_package;
    (void)code;
    strbuf_free(passthrough);
}

static void do_documentclass(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    bool present = false;
    TokSpan opt_tokens = read_optional(e, &present);
    const char* options = present ? raw_string(e, opt_tokens) : "";
    const char* name = expand_to_string(e, read_arg(e));
    bool date_present = false;
    read_optional(e, &date_present);
    if (code == P_LOADCLASSWITHOPTIONS && e->current_options) options = e->current_options;
    if (code == P_DOCUMENTCLASS) {
        e->class_options = arena_copy(e, options, strlen(options));
        define_text_macro(e, "@classoptionslist", e->class_options, true);
    }
    if (!is_adapter(e, name) && local_file_exists(e, name, ".cls")) {
        echo_cancel(e, em);
        start_package_file(e, name, options, true, nullptr, 0, 0, nullptr, false);
        return;
    }
    record_package(e, name, options, true, is_adapter(e, name) ? PK_ADAPTER : PK_UNKNOWN);
    // a class's \LoadClass names the document's base class for the adapters
    if (!echo_finish(e, em)) emit_declaration(e, "documentclass", options, name);
    if (strcmp(name, "book") == 0 || strcmp(name, "report") == 0 || strcmp(name, "memoir") == 0 ||
        strcmp(name, "scrbook") == 0 || strcmp(name, "scrreprt") == 0) {
        Meaning m = {};
        m.type = MT_CONSTRUCTOR;
        uint32_t chapter = intern_cstr(e, "chapter");
        m.value = chapter;
        define_cs(e, chapter, m, true);
    }
}

static void do_declare_option(Engine* e) {
    bool star = peek_char(e, '*', true);
    const char* name = star ? "*" : expand_to_string(e, read_arg(e));
    TokSpan code = read_arg(e);
    OptionDecl d = {};
    d.package = e->current_package ? e->current_package : "";
    d.name = name;
    d.code = code;
    e->option_decls.append(d);
}

static const OptionDecl* find_option(Engine* e, const char* package, const char* name) {
    for (size_t i = 0; i < e->option_decls.size(); i++) {
        const OptionDecl& d = e->option_decls[i];
        if (strcmp(d.package, package) == 0 && strcmp(d.name, name) == 0) return &d;
    }
    return nullptr;
}

static void append_option_code(Engine* e, lam::ArrayList<Token>* out, const char* option, TokSpan code) {
    out->append(make_cs(intern_cstr(e, "def")));
    out->append(make_cs(intern_cstr(e, "CurrentOption")));
    out->append(make_char('{', CAT_BEGIN));
    chars_to_tokens(e, option, out);
    out->append(make_char('}', CAT_END));
    for (uint32_t i = 0; i < code.count; i++) out->append(code.data[i]);
}

static void do_execute_options(Engine* e) {
    const char* list = expand_to_string(e, read_arg(e));
    lam::ArrayList<const char*> items(MEM_CAT_INPUT_OTHER, 8);
    split_list(e, list, &items);
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, 32);
    const char* pkg = e->current_package ? e->current_package : "";
    for (size_t i = 0; i < items.size(); i++) {
        const OptionDecl* d = find_option(e, pkg, items[i]);
        if (d) append_option_code(e, &out, items[i], d->code);
    }
    if (out.size()) back_list_copy(e, out);
}

static void do_process_options(Engine* e) {
    bool star = peek_char(e, '*', true);
    const char* pkg = e->current_package ? e->current_package : "";
    lam::ArrayList<const char*> local(MEM_CAT_INPUT_OTHER, 8);
    lam::ArrayList<const char*> global(MEM_CAT_INPUT_OTHER, 8);
    split_list(e, e->current_options ? e->current_options : "", &local);
    if (!e->current_is_class) split_list(e, e->class_options ? e->class_options : "", &global);
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, 64);
    const OptionDecl* star_decl = find_option(e, pkg, "*");
    if (star) {
        for (size_t i = 0; i < global.size(); i++) {
            const OptionDecl* d = find_option(e, pkg, global[i]);
            if (d) append_option_code(e, &out, global[i], d->code);
        }
    } else {
        // declared options in declaration order, when given locally or globally
        for (size_t i = 0; i < e->option_decls.size(); i++) {
            const OptionDecl& d = e->option_decls[i];
            if (strcmp(d.package, pkg) != 0 || strcmp(d.name, "*") == 0) continue;
            bool given = false;
            for (size_t k = 0; k < local.size() && !given; k++) given = strcmp(local[k], d.name) == 0;
            for (size_t k = 0; k < global.size() && !given; k++) given = strcmp(global[k], d.name) == 0;
            if (given) append_option_code(e, &out, d.name, d.code);
        }
    }
    for (size_t i = 0; i < local.size(); i++) {
        const OptionDecl* d = find_option(e, pkg, local[i]);
        if (d) {
            if (star) append_option_code(e, &out, local[i], d->code);
            continue;
        }
        if (star_decl) append_option_code(e, &out, local[i], star_decl->code);
        else if (!e->current_is_class)
            diag(e, "latex-unknown-option", "LaTeX Error: Unknown option `%s' for package `%s'", local[i], pkg);
    }
    if (out.size()) back_list_copy(e, out);
}

static void push_branch(Engine* e, bool which, TokSpan yes, TokSpan no) {
    back_list(e, which ? yes : no);
}

// ======================================================================
// xparse-style commands
// ======================================================================

static const char* NO_VALUE = "-NoValue-";

static TokSpan no_value_tokens(Engine* e) {
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 9);
    str_to_tokens(e, NO_VALUE, strlen(NO_VALUE), &list);
    return freeze(e, list);
}

static bool is_no_value(Engine* e, TokSpan arg) {
    size_t n = strlen(NO_VALUE);
    if (arg.count != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (arg.data[i].kind != TK_CHAR || arg.data[i].value != (uint32_t)(unsigned char)NO_VALUE[i]) return false;
    }
    (void)e;
    return true;
}

static bool parse_arg_spec(Engine* e, TokSpan spec, lam::ArrayList<ArgSpec>* out) {
    uint32_t i = 0;
    while (i < spec.count) {
        const Token& t = spec.data[i];
        if (t.kind == TK_CHAR && t.cat == CAT_SPACE) { i++; continue; }
        ArgSpec a = {};
        while (i < spec.count && spec.data[i].kind == TK_CHAR &&
               (spec.data[i].value == '+' || spec.data[i].value == '!' || spec.data[i].value == '>')) {
            if (spec.data[i].value == '+') a.long_arg = true;
            if (spec.data[i].value == '>') {
                // argument processors are ignored: skip the following group
                i++;
                if (i < spec.count && spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_BEGIN) {
                    int depth = 0;
                    for (; i < spec.count; i++) {
                        if (spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_BEGIN) depth++;
                        else if (spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_END && --depth == 0) break;
                    }
                }
                diag(e, "latex-argument-processor", "xparse argument processors are not applied");
            }
            i++;
        }
        if (i >= spec.count || spec.data[i].kind != TK_CHAR) return false;
        a.type = (char)spec.data[i].value;
        i++;
        auto take_group = [&](TokSpan* out_span) {
            while (i < spec.count && spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_SPACE) i++;
            if (i < spec.count && spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_BEGIN) {
                uint32_t start = i + 1;
                int depth = 0;
                for (; i < spec.count; i++) {
                    if (spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_BEGIN) depth++;
                    else if (spec.data[i].kind == TK_CHAR && spec.data[i].cat == CAT_END && --depth == 0) break;
                }
                *out_span = freeze_range(e, spec.data + start, i - start);
                i++;
            } else if (i < spec.count) {
                *out_span = freeze_range(e, spec.data + i, 1);
                i++;
            }
        };
        switch (a.type) {
        case 'm': case 's': case 'g': case 'v': case 'b': case 'o': break;
        case 'O': case 'G': take_group(&a.dflt); break;
        case 't': if (i < spec.count) a.open = spec.data[i++]; break;
        case 'r': case 'd':
            if (i + 1 < spec.count) { a.open = spec.data[i]; a.close = spec.data[i + 1]; i += 2; }
            break;
        case 'R': case 'D':
            if (i + 1 < spec.count) { a.open = spec.data[i]; a.close = spec.data[i + 1]; i += 2; }
            take_group(&a.dflt);
            break;
        case 'e': case 'E': {
            TokSpan ignored = {};
            take_group(&ignored);
            if (a.type == 'E') take_group(&ignored);
            diag(e, "latex-embellishment", "xparse embellishment arguments are not supported");
            break;
        }
        default:
            diag(e, "latex-arg-spec", "unknown argument type `%c'", a.type);
            return false;
        }
        out->append(a);
    }
    return true;
}

static bool peek_token(Engine* e, const Token& want, bool skip_spaces) {
    Token t;
    for (;;) {
        if (!get_token(e, &t)) return false;
        if (skip_spaces && t.kind == TK_CHAR && t.cat == CAT_SPACE) continue;
        break;
    }
    bool match = t.kind == want.kind && t.value == want.value && (t.kind != TK_CHAR || t.cat == want.cat);
    if (!match) back_input(e, t);
    return match;
}

static void collect_until_close(Engine* e, const Token& open, const Token& close, lam::ArrayList<Token>* out) {
    int depth = 0;
    int nest = 0;
    Token t;
    for (;;) {
        if (!get_token(e, &t)) return;
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) depth++;
        if (t.kind == TK_CHAR && t.cat == CAT_END) depth--;
        if (depth == 0 && t.kind == open.kind && t.value == open.value && !(open.value == close.value)) nest++;
        if (depth == 0 && t.kind == close.kind && t.value == close.value) {
            if (nest == 0) return;
            nest--;
        }
        out->append(t);
    }
}

bool latex_collect_xargs(Engine* e, const Token& call, const Macro* mac, lam::ArrayList<Token>* args) {
    (void)call;
    for (uint8_t k = 0; k < mac->xcount && k < 9; k++) {
        const ArgSpec& a = mac->xspec[k];
        lam::ArrayList<Token>& arg = args[k];
        switch (a.type) {
        case 'm':
            if (!scan_macro_arg(e, &arg, true)) return false;
            break;
        case 'o': case 'O': {
            bool present = false;
            if (!scan_bracket_arg(e, &arg, &present)) return false;
            if (!present) {
                TokSpan d = a.type == 'o' ? no_value_tokens(e) : a.dflt;
                for (uint32_t i = 0; i < d.count; i++) arg.append(d.data[i]);
            }
            break;
        }
        case 's': case 't': {
            Token want = a.type == 's' ? make_char('*', CAT_OTHER) : a.open;
            bool hit = peek_token(e, want, true);
            arg.append(make_cs(intern_cstr(e, hit ? "BooleanTrue" : "BooleanFalse")));
            break;
        }
        case 'r': case 'R': case 'd': case 'D': {
            bool hit = peek_token(e, a.open, true);
            if (hit) {
                collect_until_close(e, a.open, a.close, &arg);
            } else {
                if (a.type == 'r' || a.type == 'R') diag(e, "latex-missing-arg", "missing required delimited argument");
                TokSpan d = (a.type == 'r' || a.type == 'd') ? no_value_tokens(e) : a.dflt;
                for (uint32_t i = 0; i < d.count; i++) arg.append(d.data[i]);
            }
            break;
        }
        case 'g': case 'G': {
            Token brace = make_char('{', CAT_BEGIN);
            Token t;
            bool hit = false;
            for (;;) {
                if (!get_token(e, &t)) break;
                if (t.kind == TK_CHAR && t.cat == CAT_SPACE) continue;
                hit = t.kind == TK_CHAR && t.cat == CAT_BEGIN;
                back_input(e, t);
                break;
            }
            (void)brace;
            if (hit) {
                if (!scan_macro_arg(e, &arg, true)) return false;
            } else {
                TokSpan d = a.type == 'g' ? no_value_tokens(e) : a.dflt;
                for (uint32_t i = 0; i < d.count; i++) arg.append(d.data[i]);
            }
            break;
        }
        case 'v': {
            // verbatim argument: the raw characters, as other tokens
            if (!scan_macro_arg(e, &arg, true)) return false;
            StrBuf* sb = strbuf_new();
            tokens_to_str(e, freeze(e, arg), sb, false);
            arg.clear();
            str_to_tokens(e, sb->str ? sb->str : "", sb->length, &arg);
            strbuf_free(sb);
            break;
        }
        case 'b': {
            // environment body up to the matching \end{name}
            const NameRec& rec = name_of(e, mac->env_name);
            int depth = 0;
            Token t;
            for (;;) {
                if (!get_token(e, &t)) break;
                if (t.kind == TK_CS && (t.value == e->cs_begin || t.value == e->cs_end)) {
                    lam::ArrayList<Token> name(MEM_CAT_INPUT_OTHER, 16);
                    Token b;
                    if (get_token(e, &b)) {
                        name.append(b);
                        if (b.kind == TK_CHAR && b.cat == CAT_BEGIN) {
                            int d = 1;
                            while (d > 0 && get_token(e, &b)) {
                                name.append(b);
                                if (b.kind == TK_CHAR && b.cat == CAT_BEGIN) d++;
                                else if (b.kind == TK_CHAR && b.cat == CAT_END) d--;
                            }
                        }
                    }
                    StrBuf* sb = strbuf_new();
                    for (size_t i = 0; i < name.size(); i++)
                        if (name[i].kind == TK_CHAR && name[i].cat != CAT_BEGIN && name[i].cat != CAT_END)
                            strbuf_append_utf8(sb, name[i].value);
                    bool same = sb->length == rec.len && memcmp(sb->str, rec.chars, rec.len) == 0;
                    strbuf_free(sb);
                    if (same && t.value == e->cs_end && depth == 0) {
                        // leave \end{name} in the input for the environment's end code
                        back_list_copy(e, name);
                        back_input(e, t);
                        break;
                    }
                    if (same) depth += t.value == e->cs_begin ? 1 : -1;
                    arg.append(t);
                    for (size_t i = 0; i < name.size(); i++) arg.append(name[i]);
                    continue;
                }
                arg.append(t);
            }
            break;
        }
        default:
            break;
        }
    }
    return true;
}

static void do_new_document_command(Engine* e, const Token& t, uint8_t sub) {
    EchoMark em = echo_begin(e, t);
    bool env = sub >= 4;
    uint8_t mode = sub & 3;   // 0 new, 1 renew, 2 provide, 3 declare
    uint32_t cs = 0;
    const char* env_name = nullptr;
    if (env) {
        env_name = expand_to_string(e, read_arg(e));
        cs = intern_cstr(e, env_name);
    } else {
        cs = cs_from_arg(e);
    }
    TokSpan spec = read_arg(e);
    TokSpan body = read_arg(e);
    TokSpan end_body = env ? read_arg(e) : TokSpan{};
    lam::ArrayList<ArgSpec> specs(MEM_CAT_INPUT_OTHER, 8);
    bool ok = parse_arg_spec(e, spec, &specs);
    bool definable = is_definable(meaning_of(e, cs));
    bool define = ok;
    if (mode == 0 && !definable) {
        diag(e, "latex-already-defined", "LaTeX Error: Command or environment already defined");
        define = false;
    } else if (mode == 1 && definable) {
        diag(e, "latex-undefined", "LaTeX Error: Command or environment undefined");
    } else if (mode == 2 && !definable) {
        define = false;
    }
    if (define && specs.size() > 9) {
        diag(e, "latex-bad-arg-count", "too many arguments in signature");
        define = false;
    }
    if (define) {
        uint8_t n = (uint8_t)specs.size();
        ArgSpec* stored = (ArgSpec*)arena_alloc(e->arena, sizeof(ArgSpec) * (n ? n : 1));
        for (uint8_t k = 0; k < n; k++) stored[k] = specs[k];
        Meaning m = macro_meaning(e, match_params(e, 1, 0), convert_params(e, body, n), n, MF_LONG | MF_PROTECTED);
        Macro* mac = (Macro*)m.macro;
        mac->xspec = stored;
        mac->xcount = n;
        mac->env_name = cs;
        define_cs(e, cs, m, false);
        if (env) {
            StrBuf* end_name = strbuf_new();
            strbuf_append_str(end_name, "end");
            strbuf_append_str(end_name, env_name);
            uint32_t cs_end = intern(e, end_name->str, (uint32_t)end_name->length);
            strbuf_free(end_name);
            define_cs(e, cs_end, macro_meaning(e, match_params(e, 1, 0), convert_params(e, end_body, 0), 0, MF_LONG), false);
        }
    }
    echo_finish(e, em);
}

// ======================================================================
// conditionals and loops of the package-author interface
// ======================================================================

// LaTeX's \@ifnextchar stores its branches with \def, which turns each ## into #
static TokSpan halve_params(Engine* e, TokSpan list) {
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, list.count);
    for (uint32_t i = 0; i < list.count; i++) {
        const Token& t = list.data[i];
        out.append(t);
        if (t.kind == TK_CHAR && t.cat == CAT_PARAM && i + 1 < list.count &&
            list.data[i + 1].kind == TK_CHAR && list.data[i + 1].cat == CAT_PARAM) i++;
    }
    return freeze(e, out);
}

static void do_ifnextchar(Engine* e) {
    TokSpan want = read_arg(e);
    TokSpan yes = halve_params(e, read_arg(e));
    TokSpan no = halve_params(e, read_arg(e));
    Token n;
    for (;;) {
        if (!get_token(e, &n)) { back_list(e, no); return; }
        if (!(n.kind == TK_CHAR && n.cat == CAT_SPACE)) break;
    }
    bool match = false;
    if (want.count >= 1) {
        Meaning a = token_meaning(e, want.data[0]);
        Meaning b = token_meaning(e, n);
        if (want.data[0].kind == TK_CHAR && n.kind == TK_CHAR)
            match = want.data[0].value == n.value && want.data[0].cat == n.cat;
        else
            match = meanings_equal(e, a, b);
    }
    back_input(e, n);
    back_list(e, match ? yes : no);
}

static void do_ifstar(Engine* e) {
    TokSpan yes = halve_params(e, read_arg(e));
    TokSpan no = halve_params(e, read_arg(e));
    bool star = peek_char(e, '*', true);
    back_list(e, star ? yes : no);
}

static void strip_item_braces(lam::ArrayList<Token>* item) {
    // strip a pair of braces that encloses the whole item, as \@for does
    size_t n = item->size();
    if (n < 2) return;
    if (!((*item)[0].kind == TK_CHAR && (*item)[0].cat == CAT_BEGIN)) return;
    if (!((*item)[n - 1].kind == TK_CHAR && (*item)[n - 1].cat == CAT_END)) return;
    int d = 0;
    for (size_t i = 0; i < n; i++) {
        if ((*item)[i].kind == TK_CHAR && (*item)[i].cat == CAT_BEGIN) d++;
        else if ((*item)[i].kind == TK_CHAR && (*item)[i].cat == CAT_END && --d == 0 && i != n - 1) return;
    }
    item->remove(n - 1);
    item->remove(0);
}

static void append_loop_step(Engine* e, lam::ArrayList<Token>* out, uint32_t var,
                             const lam::ArrayList<Token>& item, TokSpan body) {
    out->append(make_cs(intern_cstr(e, "def")));
    out->append(make_cs(var));
    out->append(make_char('{', CAT_BEGIN));
    for (size_t i = 0; i < item.size(); i++) out->append(item[i]);
    out->append(make_char('}', CAT_END));
    for (uint32_t i = 0; i < body.count; i++) out->append(body.data[i]);
}

static void do_for(Engine* e, bool tokens_loop) {
    uint32_t var = get_r_token(e);
    Token colon, eq;
    if (!get_token(e, &colon) || !get_token(e, &eq) || !tok_is_char(colon, ':', CAT_OTHER) ||
        !tok_is_char(eq, '=', CAT_OTHER)) {
        diag(e, "latex-for-syntax", "\\@for expects \\var:=list\\do{body}");
        return;
    }
    Token do_tok = make_cs(intern_cstr(e, "do"));
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
    scan_until_delimiter(e, &do_tok, 1, &list);
    TokSpan body = read_arg(e);
    // the list is expanded once, as \@for's \expandafter does
    if (!tokens_loop && list.size() >= 1 && list[0].kind == TK_CS) {
        const Meaning& m = meaning_of(e, list[0].value);
        if (m.type == MT_MACRO && m.macro->nargs == 0 && !m.macro->xspec) {
            lam::ArrayList<Token> expanded(MEM_CAT_INPUT_OTHER, m.macro->body.count + list.size());
            for (uint32_t i = 0; i < m.macro->body.count; i++) expanded.append(m.macro->body.data[i]);
            for (size_t i = 1; i < list.size(); i++) expanded.append(list[i]);
            list = static_cast<lam::ArrayList<Token>&&>(expanded);
        }
    }
    lam::ArrayList<Token> out(MEM_CAT_INPUT_OTHER, 64);
    lam::ArrayList<Token> item(MEM_CAT_INPUT_OTHER, 16);
    if (tokens_loop) {
        size_t i = 0;
        while (i < list.size()) {
            item.clear();
            const Token& t = list[i];
            if (t.kind == TK_CHAR && t.cat == CAT_SPACE) { i++; continue; }
            if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) {
                int d = 0;
                for (; i < list.size(); i++) {
                    if (list[i].kind == TK_CHAR && list[i].cat == CAT_BEGIN) { if (d++) item.append(list[i]); continue; }
                    if (list[i].kind == TK_CHAR && list[i].cat == CAT_END) { if (--d == 0) { i++; break; } }
                    item.append(list[i]);
                }
            } else {
                item.append(t);
                i++;
            }
            append_loop_step(e, &out, var, item, body);
        }
    } else if (list.size()) {
        int depth = 0;
        for (size_t i = 0; i <= list.size(); i++) {
            bool end = i == list.size();
            if (!end) {
                const Token& t = list[i];
                if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) depth++;
                else if (t.kind == TK_CHAR && t.cat == CAT_END) depth--;
                if (!(depth == 0 && tok_is_char(t, ',', CAT_OTHER))) {
                    item.append(t);
                    continue;
                }
            }
            strip_item_braces(&item);
            append_loop_step(e, &out, var, item, body);
            item.clear();
        }
    }
    if (out.size()) back_list_copy(e, out);
}

// ======================================================================
// LaTeX's definition internals
// ======================================================================

// \@yargdef<cs><mode>{<n>}{<body>}: define <cs> with n arguments; mode 2 makes
// the first one a [ ]-delimited optional argument (the \@xargdef form).
// \l@ngrel@x supplies the prefixes: \relax, \long, \protected, or a macro of them.
static uint8_t prefix_flags(Engine* e) {
    const Meaning& m = meaning_of(e, intern_cstr(e, "l@ngrel@x"));
    uint8_t flags = 0;
    if (m.type == MT_PRIM) {
        if (m.value == P_LONG) flags |= MF_LONG;
        if (m.value == P_PROTECTED) flags |= MF_PROTECTED;
    } else if (m.type == MT_MACRO) {
        for (uint32_t i = 0; i < m.macro->body.count; i++) {
            const Token& t = m.macro->body.data[i];
            if (t.kind != TK_CS) continue;
            const Meaning& p = meaning_of(e, t.value);
            if (p.type == MT_PRIM && p.value == P_LONG) flags |= MF_LONG;
            if (p.type == MT_PRIM && p.value == P_PROTECTED) flags |= MF_PROTECTED;
        }
    }
    return flags;
}

static void do_yargdef(Engine* e) {
    uint32_t cs = get_r_token(e);
    int32_t mode = scan_int(e);
    TokSpan count = read_arg(e);
    TokSpan body = read_arg(e);
    int32_t nargs = eval_counter_value(e, count);
    if (nargs < 0 || nargs > 9) {
        diag(e, "latex-bad-arg-count", "Illegal number of arguments %d", nargs);
        nargs = 0;
    }
    lam::ArrayList<Token> params(MEM_CAT_INPUT_OTHER, 12);
    for (int32_t k = 1; k <= nargs; k++) {
        if (k == 1 && mode == 2) params.append(make_char('[', CAT_OTHER));
        Token mt = {};
        mt.kind = TK_MATCH;
        mt.value = (uint32_t)k;
        params.append(mt);
        if (k == 1 && mode == 2) params.append(make_char(']', CAT_OTHER));
    }
    Token em = {};
    em.kind = TK_END_MATCH;
    params.append(em);
    Meaning m = macro_meaning(e, freeze(e, params), convert_params(e, body, (uint8_t)nargs),
                              (uint8_t)nargs, prefix_flags(e));
    define_cs(e, cs, m, false);
}

// \filename@parse{path} sets \filename@area, \filename@base and \filename@ext
// (\relax when the name has no extension)
static void do_filename_parse(Engine* e) {
    const char* path = expand_to_string(e, read_arg(e));
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    const char* dot = strrchr(base, '.');
    define_text_macro(e, "filename@area", slash ? arena_copy(e, path, (size_t)(base - path)) : "", false);
    define_text_macro(e, "filename@base", dot ? arena_copy(e, base, (size_t)(dot - base)) : base, false);
    if (dot) define_text_macro(e, "filename@ext", dot + 1, false);
    else define_cs(e, intern_cstr(e, "filename@ext"), make_prim(P_RELAX), false);
}

// ======================================================================
// dispatch
// ======================================================================

static void format_counter(Engine* e, uint8_t sub, int32_t v, StrBuf* sb) {
    switch (sub) {
    case 0: print_int(v, sb); return;
    case 1: print_roman(v, sb); return;
    case 2: {
        size_t mark = sb->length;
        print_roman(v, sb);
        for (size_t i = mark; i < sb->length; i++) sb->str[i] = (char)(sb->str[i] - 'a' + 'A');
        return;
    }
    case 3: case 4:
        if (v >= 1 && v <= 26) strbuf_append_char(sb, (char)((sub == 3 ? 'a' : 'A') + v - 1));
        else diag(e, "latex-counter-too-large", "LaTeX Error: Counter too large");
        return;
    default: {
        static const char* SYMBOLS[] = {"", "*", "\\dag", "\\ddag", "\\S", "\\P", "\\textbardbl",
                                        "**", "\\dag\\dag", "\\ddag\\ddag"};
        if (v >= 1 && v <= 9) strbuf_append_str(sb, SYMBOLS[v]);
        else diag(e, "latex-counter-too-large", "LaTeX Error: Counter too large");
        return;
    }
    }
}

// one undelimited argument, keeping its braces in `raw` for an exact hand-back
static TokSpan read_arg_raw(Engine* e, lam::ArrayList<Token>* raw) {
    lam::ArrayList<Token> arg(MEM_CAT_INPUT_OTHER, 16);
    Token t;
    do {
        if (!get_token(e, &t)) return freeze(e, arg);
        raw->append(t);
    } while (t.kind == TK_CHAR && t.cat == CAT_SPACE);
    if (!(t.kind == TK_CHAR && t.cat == CAT_BEGIN)) {
        arg.append(t);
        return freeze(e, arg);
    }
    int depth = 1;
    while (get_token(e, &t)) {
        raw->append(t);
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) depth++;
        else if (t.kind == TK_CHAR && t.cat == CAT_END && --depth == 0) break;
        arg.append(t);
    }
    return freeze(e, arg);
}

static void counter_format(Engine* e, const Token& t, uint8_t sub, bool value_only) {
    lam::ArrayList<Token> raw(MEM_CAT_INPUT_OTHER, 8);
    TokSpan name_tokens = read_arg_raw(e, &raw);
    const char* name = expand_to_string(e, name_tokens);
    uint32_t cs = counter_cs(e, name);
    if (engine_counter(e, cs)) {
        if (value_only) {
            back_input(e, make_cs(cs));
            return;
        }
        StrBuf* sb = strbuf_new();
        format_counter(e, sub, reg_slot(e, RK_COUNT, meaning_of(e, cs).value)->v.i, sb);
        if (sub == 5) {
            // symbols include control words; tokenize them through a pseudo file
            int32_t file = add_source(e, "\\fnsymbol", arena_copy(e, sb->str ? sb->str : "", sb->length),
                                      (uint32_t)sb->length);
            push_file(e, file, true);
        } else {
            push_string_tokens(e, sb->str ? sb->str : "");
        }
        strbuf_free(sb);
        return;
    }
    // a counter the script owns: hand the command to the adapters exactly as written
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, raw.size() + 1);
    Token self = t;
    self.flags |= TF_NOEXPAND;
    list.append(self);
    for (size_t i = 0; i < raw.size(); i++) list.append(raw[i]);
    Token* data = (Token*)arena_alloc(e->arena, sizeof(Token) * list.size());
    memcpy(data, list.data(), sizeof(Token) * list.size());
    push_tokens(e, data, (uint32_t)list.size(), false);
}

static void append_hook(Engine* e, lam::ArrayList<Token>* hook, TokSpan code) {
    for (uint32_t i = 0; i < code.count; i++) {
        Token t = code.data[i];
        t.flags &= (uint16_t)~TF_DIRECT;
        hook->append(t);
    }
}

bool latex_do(Engine* e, const Token& t, uint32_t code) {
    uint8_t sub = sub_of(e, t);
    switch (code) {
    case P_NEWCOMMAND: do_newcommand(e, t, sub); return true;
    case P_NEWENVIRONMENT: do_newenvironment(e, t, sub); return true;
    case P_BEGIN: do_begin(e, t); return true;
    case P_END_ENV: do_end_env(e, t); return true;
    case P_NEWCOUNTER: do_newcounter(e, t); return true;
    case P_SETCOUNTER: case P_ADDTOCOUNTER: case P_STEPCOUNTER: case P_REFSTEPCOUNTER:
        counter_command(e, t, code);
        return true;
    case P_NEWLENGTH: case P_SETLENGTH: case P_ADDTOLENGTH: length_command(e, t, code); return true;
    case P_SETTOBOX: do_settobox(e, t, sub); return true;
    case P_DOCUMENTCLASS: case P_LOADCLASS: case P_LOADCLASSWITHOPTIONS:
        do_documentclass(e, t, code);
        return true;
    case P_USEPACKAGE: case P_REQUIREPACKAGE: do_usepackage(e, t, code); return true;
    case P_DECLAREOPTION: do_declare_option(e); return true;
    case P_EXECUTEOPTIONS: do_execute_options(e); return true;
    case P_PROCESSOPTIONS: do_process_options(e); return true;
    case P_PASSOPTIONS: {
        const char* options = expand_to_string(e, read_arg(e));
        const char* names = expand_to_string(e, read_arg(e));
        lam::ArrayList<const char*> items(MEM_CAT_INPUT_OTHER, 4);
        split_list(e, names, &items);
        for (size_t i = 0; i < items.size(); i++) {
            e->pass_options.append(items[i]);
            e->pass_options.append(options);
        }
        return true;
    }
    case P_IFPACKAGEWITH: {
        const char* name = expand_to_string(e, read_arg(e));
        const char* opts = expand_to_string(e, read_arg(e));
        TokSpan yes = read_arg(e);
        TokSpan no = read_arg(e);
        PackageRecord* p = find_package(e, name, sub == 1);
        bool all = p != nullptr;
        if (all) {
            lam::ArrayList<const char*> want(MEM_CAT_INPUT_OTHER, 4);
            split_list(e, opts, &want);
            for (size_t i = 0; i < want.size() && all; i++) all = list_contains(e, p->options, want[i]);
        }
        push_branch(e, all, yes, no);
        return true;
    }
    case P_IFFILEEXISTS: case P_INPUTIFFILEEXISTS: {
        const char* name = expand_to_string(e, read_arg(e));
        TokSpan yes = read_arg(e);
        TokSpan no = read_arg(e);
        bool exists = local_file_exists(e, name, "") || local_file_exists(e, name, ".tex");
        if (exists && code == P_INPUTIFFILEEXISTS) {
            static const char* exts[] = {"", ".tex"};
            int32_t file = -1;
            if (open_local_file(e, name, exts, 2, &file, true)) push_file(e, file, false);
        }
        push_branch(e, exists, yes, no);
        return true;
    }
    case P_ATBEGINDOCUMENT: {
        TokSpan code_tokens = read_arg(e);
        if (e->in_document) back_list(e, code_tokens);
        else append_hook(e, &e->begin_document_hook, code_tokens);
        return true;
    }
    case P_ATENDDOCUMENT: append_hook(e, &e->end_document_hook, read_arg(e)); return true;
    case P_ATENDOFPACKAGE: {
        TokSpan code_tokens = read_arg(e);
        if (e->package_frames.empty()) {
            back_list(e, code_tokens);
        } else {
            PackageFrame& f = e->package_frames.back();
            lam::ArrayList<Token> hook(MEM_CAT_INPUT_OTHER, f.end_hook.count + code_tokens.count);
            for (uint32_t i = 0; i < f.end_hook.count; i++) hook.append(f.end_hook.data[i]);
            append_hook(e, &hook, code_tokens);
            f.end_hook = freeze(e, hook);
        }
        return true;
    }
    case P_VERB: do_verb(e, t); return true;
    case P_RAW_GROUP_CMD:
        // \lstinline is a primitive (sub 0); \directlua arrives undefined (Lua group form)
        do_raw_group_cmd(e, t, meaning_of(e, t.value).type == MT_PRIM ? sub : 1);
        return true;
    case P_LAMBDA_CONSTRUCTOR: {
        uint32_t cs = get_r_token(e);
        Meaning m = {};
        m.type = MT_CONSTRUCTOR;
        m.value = cs;
        define_cs(e, cs, m, true);
        return true;
    }
    case P_PROVIDESPACKAGE: {
        // \ProvidesPackage{name}[info] sets \ver@name.sty, as LaTeX's \@ifpackagelater reads it
        const char* name = expand_to_string(e, read_arg(e));
        bool present = false;
        TokSpan info = read_optional(e, &present);
        const char* text = present ? raw_string(e, info) : "";
        if (sub == 2) {
            StrBuf* cs = strbuf_new();
            strbuf_append_str(cs, "ver@");
            strbuf_append_str(cs, name);
            define_text_macro(e, cs->str, text, true);
            strbuf_free(cs);
        } else {
            define_named_text(e, "ver@", name, sub == 1 ? "cls" : "sty", text);
        }
        return true;
    }
    case P_YARGDEF: do_yargdef(e); return true;
    case P_FILENAME_PARSE: do_filename_parse(e); return true;
    case P_NEEDSTEXFORMAT: {
        read_arg(e);
        bool present = false;
        read_optional(e, &present);
        return true;
    }
    case P_INCLUDE: {
        const char* name = expand_to_string(e, read_arg(e));
        static const char* exts[] = {".tex", ""};
        int32_t file = -1;
        if (open_local_file(e, name, exts, 2, &file, true)) push_file(e, file, false);
        return true;
    }
    case P_NEWIF: {
        uint32_t cs = get_r_token(e);
        const NameRec& rec = name_of(e, cs);
        if (rec.len < 3 || memcmp(rec.chars, "if", 2) != 0) {
            diag(e, "latex-newif", "\\newif needs a name starting with `if'");
            return true;
        }
        StrBuf* base = strbuf_new();
        strbuf_append_str_n(base, rec.chars + 2, rec.len - 2);
        size_t base_len = base->length;
        uint32_t if_false = intern_cstr(e, "iffalse");
        uint32_t if_true = intern_cstr(e, "iftrue");
        define_cs(e, cs, meaning_of(e, if_false), false);
        for (int k = 0; k < 2; k++) {
            base->length = base_len;
            base->str[base_len] = '\0';
            strbuf_append_str(base, k == 0 ? "true" : "false");
            Token body[3] = {make_cs(intern_cstr(e, "let")), make_cs(cs), make_cs(k == 0 ? if_true : if_false)};
            define_cs(e, intern(e, base->str, (uint32_t)base->length),
                      macro_meaning(e, match_params(e, 1, 0), freeze_range(e, body, 3), 0, 0), false);
        }
        strbuf_free(base);
        return true;
    }
    case P_IFNEXTCHAR: do_ifnextchar(e); return true;
    case P_IFSTAR: do_ifstar(e); return true;
    case P_FOR: do_for(e, false); return true;
    case P_TFOR: do_for(e, true); return true;
    case P_COUNTER_FMT: counter_format(e, t, sub, false); return true;
    case P_VALUE: counter_format(e, t, 0, true); return true;
    case P_MATH_SWITCH: {
        emit_token(e, t);
        if (sub == 0 || sub == 2) {
            new_save_level(e, GT_MATH_SHIFT);
            e->groups.back().box_kind = sub == 2 ? 2 : 0;
            e->math_depth++;
        } else if (!e->groups.empty() && e->groups.back().type == GT_MATH_SHIFT) {
            unsave(e);
            if (e->math_depth) e->math_depth--;
        }
        return true;
    }
    case P_ROMAN_UPPER: {
        StrBuf* sb = strbuf_new();
        format_counter(e, 2, scan_int(e), sb);
        push_string_tokens(e, sb->str ? sb->str : "");
        strbuf_free(sb);
        return true;
    }
    case P_PACKAGE_END: {
        // LaTeX's order: the \AtEndOfPackage hook, \@popfilename, then the next file
        if (e->package_frames.empty()) return true;
        PackageFrame& f = e->package_frames.back();
        back_input(e, make_cs(intern_cstr(e, "\x02packagepop")));
        back_input(e, make_cs(intern_cstr(e, "@popfilename")));
        TokSpan hook = f.end_hook;
        f.end_hook = TokSpan{nullptr, 0};
        if (hook.count) back_list(e, hook);
        return true;
    }
    case P_PACKAGE_POP: finish_package_frame(e); return true;
    case P_ENV_END_GROUP:
        if (!e->groups.empty() && e->groups.back().type == GT_ENV) unsave(e);
        return true;
    case P_DOCUMENT_END:
        emit_env_text(e, "end", "document");
        finish_document(e);
        return true;
    case P_NEWDOCUMENTCOMMAND: do_new_document_command(e, t, sub); return true;
    case P_IFNOVALUE: {
        TokSpan arg = read_arg(e);
        bool no_value = is_no_value(e, arg);
        bool test = sub < 3 ? no_value : !no_value;
        uint8_t form = sub % 3;   // 0 TF, 1 T, 2 F
        TokSpan first = read_arg(e);
        TokSpan second = form == 0 ? read_arg(e) : TokSpan{};
        if (form == 0) push_branch(e, test, first, second);
        else if (form == 1 && test) back_list(e, first);
        else if (form == 2 && !test) back_list(e, first);
        return true;
    }
    case P_IFBOOLEAN: {
        TokSpan arg = read_arg(e);
        bool test = arg.count == 1 && arg.data[0].kind == TK_CS &&
            arg.data[0].value == intern_cstr(e, "BooleanTrue");
        TokSpan first = read_arg(e);
        TokSpan second = sub == 0 ? read_arg(e) : TokSpan{};
        if (sub == 0) push_branch(e, test, first, second);
        else if (sub == 1 && test) back_list(e, first);
        else if (sub == 2 && !test) back_list(e, first);
        return true;
    }
    case P_ADDTOHOOK: {
        const char* hook = expand_to_string(e, read_arg(e));
        bool present = false;
        read_optional(e, &present);
        TokSpan code_tokens = read_arg(e);
        if (strcmp(hook, "begindocument") == 0 || strcmp(hook, "begindocument/before") == 0 ||
            strcmp(hook, "begindocument/end") == 0) {
            if (e->in_document) back_list(e, code_tokens);
            else append_hook(e, &e->begin_document_hook, code_tokens);
        } else if (strcmp(hook, "enddocument") == 0 || strcmp(hook, "enddocument/afterlastpage") == 0) {
            append_hook(e, &e->end_document_hook, code_tokens);
        } else {
            diag(e, "latex-unsupported-hook", "hook `%s' is not available", hook);
        }
        return true;
    }
    case P_IFFORMATATLEAST: {
        read_arg(e);
        TokSpan yes = read_arg(e);
        read_arg(e);
        back_list(e, yes);
        return true;
    }
    default:
        diag(e, "tex-unsupported-primitive", "unsupported LaTeX-layer command");
        return false;
    }
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
