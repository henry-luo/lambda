// TeX expansion engine: main control, unexpandable primitives, the
// source reconstruction and the engine lifecycle.
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "input-latex-tables.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <stdio.h>
#include <new>

namespace tex {

static const uint16_t LEVEL_ONE = 1;
static const uint64_t DEFAULT_MAX_EXPANSIONS = 20000000;

// ======================================================================
// typesetting primitives in the main loop
// ======================================================================

static void emit_glue_command(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    bool mu = code == P_MSKIP;
    const Glue* g = scan_glue(e, mu);
    if (echo_finish(e, em)) return;
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    if (code == P_HSKIP) strbuf_append_str(sb, e->math_depth ? "\\hskip{" : "\\hspace{");
    else if (code == P_VSKIP) strbuf_append_str(sb, "\\vspace{");
    else strbuf_append_str(sb, "\\mskip{");
    print_glue(g, mu ? "mu" : "pt", sb);
    strbuf_append_char(sb, '}');
    emit_text(e, sb->str + mark, sb->length - mark);
    sb->length = mark;
    sb->str[mark] = '\0';
}

static void emit_kern_command(Engine* e, const Token& t, uint32_t code) {
    EchoMark em = echo_begin(e, t);
    bool mu = code == P_MKERN;
    int32_t d = scan_dimen(e, mu, false, false, 0);
    if (echo_finish(e, em)) return;
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    strbuf_append_str(sb, mu ? "\\mkern{" : e->math_depth ? "\\kern{" : "\\hspace{");
    print_scaled(d, sb);
    strbuf_append_str(sb, mu ? "mu}" : "pt}");
    emit_text(e, sb->str + mark, sb->length - mark);
    sb->length = mark;
    sb->str[mark] = '\0';
}

static void emit_rule(Engine* e, const Token& t, bool vrule) {
    EchoMark em = echo_begin(e, t);
    int32_t w, h, d;
    scan_rule_spec(e, vrule, &w, &h, &d);
    if (echo_finish(e, em)) return;
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    strbuf_append_str(sb, "\\rule{");
    if (w < 0) strbuf_append_str(sb, "\\linewidth");
    else append_dimen_text(sb, w);
    strbuf_append_str(sb, "}{");
    if (h < 0) strbuf_append_str(sb, "1em");
    else append_dimen_text(sb, h + (d > 0 ? d : 0));
    strbuf_append_char(sb, '}');
    emit_text(e, sb->str + mark, sb->length - mark);
    sb->length = mark;
    sb->str[mark] = '\0';
}

static void unskip(Engine* e) {
    if (e->out.empty()) return;
    const OutItem& last = e->out.back();
    if (last.kind == OUT_TOKEN && last.tok.kind == TK_CHAR && last.tok.cat == CAT_SPACE)
        e->out.remove(e->out.size() - 1);
}

static const char* accent_command(int32_t code) {
    switch (code) {
    case 18: return "`";
    case 19: return "'";
    case 20: return "v";
    case 21: return "u";
    case 22: return "=";
    case 23: return "r";
    case 24: return "c";
    case 94: return "^";
    case 95: return ".";
    case 125: return "H";
    case 126: return "~";
    case 127: return "\"";
    default: return nullptr;
    }
}

static void do_accent(Engine* e) {
    int32_t a = scan_char_num(e);
    Token n;
    int32_t c = -1;
    if (get_x_token(e, &n)) {
        Meaning mn = token_meaning(e, n);
        if (n.kind == TK_CHAR && (n.cat == CAT_LETTER || n.cat == CAT_OTHER)) c = (int32_t)n.value;
        else if (mn.type == MT_CHARDEF) c = (int32_t)mn.value;
        else if (mn.type == MT_PRIM && mn.value == P_CHAR) c = scan_char_num(e);
        else back_input(e, n);
    }
    const char* cmd = accent_command(a);
    StrBuf* sb = e->scratch;
    size_t mark = sb->length;
    if (cmd) {
        strbuf_append_char(sb, '\\');
        strbuf_append_str(sb, cmd);
        strbuf_append_char(sb, '{');
    }
    if (c >= 0) strbuf_append_utf8(sb, (uint32_t)c);
    if (cmd) strbuf_append_char(sb, '}');
    emit_text(e, sb->str + mark, sb->length - mark);
    sb->length = mark;
    sb->str[mark] = '\0';
}

static void drop_group_content(Engine* e, uint8_t type) {
    if (!scan_left_brace(e)) return;
    new_save_level(e, type);
    e->groups.back().box_target = -2;
}

static void do_typesetting(Engine* e, const Token& t, const Meaning& m) {
    switch (m.value) {
    case P_PAR: case P_INDENT: case P_NOINDENT: case P_HFIL: case P_HFILL: case P_HSS:
    case P_HFILNEG: case P_VFIL: case P_VFILL: case P_VSS: case P_VFILNEG: case P_NOALIGN:
    case P_OMIT: case P_SPAN: case P_CR: case P_CRCR: case P_DISC_HYPHEN: case P_ITAL_CORR:
    case P_CONTROL_SPACE: case P_NOBOUNDARY: case P_MATH_EMIT:
        emit_token(e, t);
        return;
    case P_HSKIP: case P_VSKIP: case P_MSKIP: emit_glue_command(e, t, m.value); return;
    case P_KERN: case P_MKERN: emit_kern_command(e, t, m.value); return;
    case P_PENALTY: scan_int(e); return;
    case P_UNPENALTY: case P_UNKERN: return;
    case P_UNSKIP: unskip(e); return;
    case P_HBOX: begin_box(e, t, m, -1, false, "\\mbox{", "}"); return;
    case P_VBOX: case P_VTOP: begin_box(e, t, m, -1, false, "{", "}"); return;
    case P_BOX: case P_COPY: case P_LASTBOX: case P_VSPLIT:
        begin_box(e, t, m, -1, false, nullptr, nullptr);
        return;
    case P_UNHBOX: case P_UNHCOPY: case P_UNVBOX: case P_UNVCOPY: {
        int32_t n = scan_register_num(e);
        emit_box_items(e, take_box(e, n, m.value == P_UNHCOPY || m.value == P_UNVCOPY));
        return;
    }
    case P_RAISE: case P_LOWER: case P_MOVELEFT: case P_MOVERIGHT: {
        int32_t d = scan_dimen(e, false, false, false, 0);
        Token b;
        skip_spaces_and_relax(e, &b);
        Meaning mb = token_meaning(e, b);
        if (m.value == P_RAISE || m.value == P_LOWER) {
            StrBuf* sb = strbuf_new();
            strbuf_append_str(sb, "\\raisebox{");
            append_dimen_text(sb, m.value == P_LOWER ? -d : d);
            strbuf_append_str(sb, "}{");
            const char* open = arena_copy(e, sb->str, sb->length);
            strbuf_free(sb);
            begin_box(e, b, mb, -1, false, open, "}");
        } else {
            begin_box(e, b, mb, -1, false, nullptr, nullptr);
        }
        return;
    }
    case P_HRULE: case P_VRULE: emit_rule(e, t, m.value == P_VRULE); return;
    case P_LEADERS: case P_CLEADERS: case P_XLEADERS: {
        Token b;
        skip_spaces_and_relax(e, &b);
        Meaning mb = token_meaning(e, b);
        if (mb.type == MT_PRIM && (mb.value == P_HRULE || mb.value == P_VRULE)) {
            int32_t w, h, d;
            scan_rule_spec(e, mb.value == P_VRULE, &w, &h, &d);
        } else {
            begin_box(e, b, mb, -2, false, nullptr, nullptr);
        }
        return;
    }
    case P_MARK: skip_text(e, true); return;
    case P_MARKS: scan_int(e); skip_text(e, true); return;
    case P_INSERT: scan_int(e); drop_group_content(e, GT_INSERT); return;
    case P_VADJUST: drop_group_content(e, GT_INSERT); return;
    case P_HALIGN: case P_VALIGN:
        diag(e, "tex-unsupported-alignment", "\\halign and \\valign are not reproduced");
        scan_box_spec(e);
        skip_text(e, false);
        return;
    case P_CHAR: {
        int32_t c = scan_char_num(e);
        uint8_t cat = (c < 128 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) ? CAT_LETTER : CAT_OTHER;
        emit_token(e, make_char((uint32_t)c, cat));
        return;
    }
    case P_ACCENT: do_accent(e); return;
    case P_DISCRETIONARY: {
        skip_text(e, false);
        skip_text(e, false);
        lam::ArrayList<Token> nobreak(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, false, &nobreak);
        back_list_copy(e, nobreak);
        return;
    }
    case P_SHIPOUT: {
        Token b;
        skip_spaces_and_relax(e, &b);
        begin_box(e, b, token_meaning(e, b), -2, false, nullptr, nullptr);
        return;
    }
    case P_PAGEDISCARDS: return;
    case P_VCENTER: {
        scan_box_spec(e);
        if (!scan_left_brace(e)) return;
        new_save_level(e, GT_VCENTER);
        e->groups.back().emit_close = true;
        emit_text(e, "\\vcenter{", 9);
        return;
    }
    case P_MATH_DELIM_EMIT:
        scan_int(e);
        diag(e, "tex-unsupported-mathchar", "numeric math characters are not reproduced");
        return;
    default:
        return;
    }
}

// ======================================================================
// messages, streams and other unexpandable primitives
// ======================================================================

static void text_of(Engine* e, TokSpan list, StrBuf* out) {
    tokens_to_str(e, list, out, false);
}

static void show_line(Engine* e, const char* prefix, StrBuf* body) {
    term_flush(e);
    term_print(e, prefix, strlen(prefix));
    term_print(e, body->str ? body->str : "", body->length);
    term_print(e, ".", 1);
    term_flush(e);
}

static void do_write(Engine* e) {
    int32_t j = scan_int(e);
    lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
    scan_text_into(e, true, &text);
    if (j >= 0 && j <= 15 && e->out_open[j]) return;   // written files are not kept
    StrBuf* sb = strbuf_new();
    text_of(e, span_of(text), sb);
    term_flush(e);
    term_print(e, sb->str ? sb->str : "", sb->length);
    term_flush(e);
    strbuf_free(sb);
}

static void do_open_stream(Engine* e, bool input) {
    int32_t n = scan_int(e) & 15;
    scan_optional_equals(e);
    StrBuf* name = strbuf_new();
    scan_file_name(e, name);
    if (input) {
        static const char* exts[] = {"", ".tex"};
        int32_t file = -1;
        e->in_streams[n].open = false;
        if (open_input_file(e, name->str ? name->str : "", exts, 2, &file, false)) {
            e->in_streams[n].file = file;
            e->in_streams[n].pos = 0;
            e->in_streams[n].open = true;
        }
    } else {
        e->out_open[n] = true;
    }
    strbuf_free(name);
}

static void case_shift(Engine* e, bool upper) {
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
    scan_text_into(e, false, &list);
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i].kind != TK_CHAR) continue;
        int32_t mapped = get_code(e, upper ? CT_UC : CT_LC, list[i].value);
        if (mapped != 0) list[i].value = (uint32_t)mapped;
    }
    back_list_copy(e, list);
}

static void drop_pdf_strings(Engine* e, uint8_t count) {
    for (uint8_t k = 0; k < count; k++) {
        Token t;
        int guard = 0;
        for (;;) {
            if (!get_x_token(e, &t)) return;
            Meaning m = token_meaning(e, t);
            bool brace = (t.kind == TK_CHAR && t.cat == CAT_BEGIN) || (m.type == MT_CHAR && m.cat == CAT_BEGIN);
            if (brace) {
                back_input(e, t);
                skip_text(e, true);
                break;
            }
            if (++guard > 64) {
                back_input(e, t);
                return;
            }
        }
    }
}

void do_unexpandable_prim(Engine* e, const Token& t, const Meaning& m) {
    uint32_t code = m.value;
    if (code >= P_LATEX_FIRST) {
        latex_do(e, t, code);
        return;
    }
    if (code >= P_PAR) {
        do_typesetting(e, t, m);
        return;
    }
    switch (code) {
    case P_RELAX: return;
    case P_ENDCSNAME: diag(e, "tex-extra-endcsname", "Extra \\endcsname"); return;
    case P_BEGINGROUP: new_save_level(e, GT_SEMI_SIMPLE); return;
    case P_ENDGROUP:
        if (!e->groups.empty() && e->groups.back().type == GT_SEMI_SIMPLE) unsave(e);
        else diag(e, "tex-extra-endgroup", "Extra \\endgroup");
        return;
    case P_AFTERGROUP: {
        Token x;
        if (get_token(e, &x) && !e->groups.empty()) {
            x.flags &= (uint16_t)~TF_DIRECT;
            e->after_group_tokens.append(x);
        }
        return;
    }
    case P_AFTERASSIGNMENT: {
        Token x;
        if (get_token(e, &x)) {
            e->after_assignment = x;
            e->has_after_assignment = true;
        }
        return;
    }
    case P_IGNORESPACES: {
        Token x;
        do {
            if (!get_x_token(e, &x)) return;
        } while (is_spacer(e, x));
        back_input(e, x);
        return;
    }
    case P_UPPERCASE: case P_LOWERCASE: case_shift(e, code == P_UPPERCASE); return;
    case P_MESSAGE: case P_ERRMESSAGE: {
        lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, true, &text);
        StrBuf* sb = strbuf_new();
        text_of(e, span_of(text), sb);
        if (code == P_MESSAGE) {
            if (e->term_line->length) term_print(e, " ", 1);
            term_print(e, sb->str ? sb->str : "", sb->length);
        } else {
            diag(e, "tex-errmessage", "%s", sb->str ? sb->str : "");
            show_line(e, "! ", sb);
        }
        strbuf_free(sb);
        return;
    }
    case P_SHOW: {
        Token x;
        if (!get_token(e, &x)) return;
        StrBuf* sb = strbuf_new();
        if (x.kind == TK_CS) {
            print_cs(e, x.value, sb, false);
            strbuf_append_char(sb, '=');
        }
        print_meaning(e, token_meaning(e, x), sb, &x);
        show_line(e, "> ", sb);
        strbuf_free(sb);
        return;
    }
    case P_SHOWTHE: case P_SHOWTOKENS: {
        lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
        if (code == P_SHOWTHE) {
            the_toks(e, &list);
        } else {
            scan_text_into(e, false, &list);
        }
        StrBuf* sb = strbuf_new();
        text_of(e, span_of(list), sb);
        show_line(e, "> ", sb);
        strbuf_free(sb);
        return;
    }
    case P_SHOWBOX: {
        int32_t n = scan_register_num(e);
        StrBuf* sb = strbuf_new();
        strbuf_append_str(sb, "\\box");
        print_int(n, sb);
        strbuf_append_str(sb, reg_slot(e, RK_BOX, (uint32_t)n)->v.b ? "=box" : "=void");
        show_line(e, "> ", sb);
        strbuf_free(sb);
        return;
    }
    case P_SHOWLISTS: case P_SHOWGROUPS: case P_SHOWIFS: return;
    case P_IMMEDIATE: {
        Token x;
        if (!get_x_token(e, &x)) return;
        Meaning mx = token_meaning(e, x);
        if (mx.type == MT_PRIM && (mx.value == P_WRITE || mx.value == P_OPENOUT || mx.value == P_CLOSEOUT)) {
            do_unexpandable_prim(e, x, mx);
            return;
        }
        back_input(e, x);
        return;
    }
    case P_WRITE: do_write(e); return;
    case P_OPENOUT: do_open_stream(e, false); return;
    case P_CLOSEOUT: e->out_open[scan_int(e) & 15] = false; return;
    case P_OPENIN: do_open_stream(e, true); return;
    case P_CLOSEIN: e->in_streams[scan_int(e) & 15].open = false; return;
    case P_SPECIAL: skip_text(e, true); return;
    case P_INTERACTION: return;
    case P_END: case P_DUMP: e->stop_requested = true; return;
    case P_PDFSTRING_DROP: drop_pdf_strings(e, m.sub); return;
    case P_PDFRESETTIMER: return;
    case P_SETRANDOMSEED: e->random_seed = (uint32_t)scan_int(e); return;
    default:
        prefixed_command(e, t, m, 0);
        return;
    }
}

// ======================================================================
// constructor arguments
// ======================================================================

// The groups right after a passed-through command are that command's
// arguments, read by a script adapter rather than typeset: macros expand
// (as LaTeX's \protected@edef would), but registers, primitives and
// assignments pass through untouched (Lambda_Impl_Latex_Phase4, "argument mode").
static void arg_mode(Engine* e, bool bracket) {
    int depth = bracket ? 0 : 1;
    Token t;
    while (!e->aborted && get_x_token(e, &t)) {
        if (t.flags & TF_NOEXPAND) {
            t.flags &= (uint16_t)~TF_NOEXPAND;
            emit_token(e, t);
            continue;
        }
        if (t.kind == TK_CHAR) {
            emit_token(e, t);
            if (t.cat == CAT_BEGIN) depth++;
            else if (t.cat == CAT_END) {
                if (--depth == 0 && !bracket) return;
                if (depth < 0) return;
            } else if (bracket && depth == 0 && t.cat == CAT_OTHER && t.value == ']') {
                return;
            }
            continue;
        }
        const Meaning& m = meaning_of(e, t.value);
        if (m.type == MT_PRIM && (m.value == P_BEGIN || m.value == P_END_ENV)) {
            if (latex_arg_mode_env(e, t, m.value == P_BEGIN)) continue;
            continue;
        }
        if (m.type == MT_PRIM && (m.value == P_VERB || m.value == P_RAW_GROUP_CMD)) {
            latex_do(e, t, m.value);
            continue;
        }
        if (m.type == MT_UNDEFINED || m.type == MT_CONSTRUCTOR) {
            emit_passthrough(e, t, m.value);
            continue;
        }
        emit_token(e, t);
    }
}

// raw arguments reach the adapter token for token, unexpanded
static void raw_arg(Engine* e, bool bracket) {
    int depth = bracket ? 0 : 1;
    Token t;
    while (!e->aborted && get_token(e, &t)) {
        emit_token(e, t);
        if (t.kind != TK_CHAR) continue;
        if (t.cat == CAT_BEGIN) depth++;
        else if (t.cat == CAT_END) {
            if (--depth == 0 && !bracket) return;
            if (depth < 0) return;
        } else if (bracket && depth == 0 && t.cat == CAT_OTHER && t.value == ']') {
            return;
        }
    }
}

void pass_constructor_args(Engine* e, bool raw) {
    bool first = true;
    Token t;
    while (!e->aborted && get_token(e, &t)) {
        if (first && tok_is_char(t, '*', CAT_OTHER)) {
            emit_token(e, t);
            first = false;
            continue;
        }
        first = false;
        if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) {
            emit_token(e, t);
            if (raw) raw_arg(e, false);
            else arg_mode(e, false);
            continue;
        }
        if (tok_is_char(t, '[', CAT_OTHER)) {
            emit_token(e, t);
            if (raw) raw_arg(e, true);
            else arg_mode(e, true);
            continue;
        }
        if (raw && t.kind == TK_CHAR && t.cat == CAT_MATH) {
            // bussproofs' \Axiom$...$ forms delimit their argument by math shifts
            emit_token(e, t);
            Token x;
            while (!e->aborted && get_token(e, &x)) {
                emit_token(e, x);
                if (x.kind == TK_CHAR && x.cat == CAT_MATH) break;
            }
            continue;
        }
        back_input(e, t);
        return;
    }
}

// ======================================================================
// main control (TeX §1030, digestion into the output list)
// ======================================================================

void main_control(Engine* e) {
    Token t;
    while (!e->aborted && !e->stop_requested && get_x_token(e, &t)) {
        if (t.flags & TF_NOEXPAND) {
            t.flags &= (uint16_t)~TF_NOEXPAND;
            emit_token(e, t);
            continue;
        }
        if (t.kind == TK_CHAR) {
            // package files run in the preamble, where TeX ignores spaces
            if (t.cat == CAT_SPACE && (t.flags & TF_DIRECT) && t.file >= 0 &&
                e->files[(size_t)t.file].package) continue;
            handle_char(e, t, t.value, t.cat);
            continue;
        }
        if (t.value == e->cs_par && (t.flags & TF_DIRECT) && t.file >= 0 &&
            e->files[(size_t)t.file].package) continue;
        const Meaning& m = meaning_of(e, t.value);
        switch (m.type) {
        case MT_UNDEFINED:
        case MT_CONSTRUCTOR: {
            const NameRec& rec = name_of(e, m.value);
            if (m.type == MT_UNDEFINED && rec.len < 32) {
                char name[32];
                memcpy(name, rec.chars, rec.len);
                name[rec.len] = '\0';
                if (is_raw_group_command(name)) {
                    latex_do(e, t, P_RAW_GROUP_CMD);
                    break;
                }
            }
            bool raw = m.type == MT_CONSTRUCTOR && m.sub == 1;
            emit_passthrough(e, t, m.value);
            pass_constructor_args(e, raw);
            break;
        }
        case MT_CHAR:
            handle_char(e, t, m.value, m.cat);
            break;
        case MT_CHARDEF:
            emit_token(e, make_char(m.value, CAT_OTHER));
            break;
        case MT_MATHCHARDEF:
            emit_token(e, t);
            break;
        case MT_REGDEF:
        case MT_FONT:
            prefixed_command(e, t, m, 0);
            break;
        case MT_PRIM: {
            Meaning copy = m;
            do_unexpandable_prim(e, t, copy);
            break;
        }
        default:
            emit_token(e, t);
            break;
        }
    }
}

// ======================================================================
// reconstruction
// ======================================================================

static uint8_t digester_cat(uint32_t c) {
    switch (c) {
    case '\\': return CAT_ESCAPE;
    case '{': return CAT_BEGIN;
    case '}': return CAT_END;
    case '$': return CAT_MATH;
    case '&': return CAT_ALIGN;
    case '#': return CAT_PARAM;
    case '^': return CAT_SUPER;
    case '_': return CAT_SUB;
    case '%': return CAT_COMMENT;
    case '~': return CAT_ACTIVE;
    case ' ': case '\t': return CAT_SPACE;
    default:
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return CAT_LETTER;
        return CAT_OTHER;
    }
}

struct Rebuilder {
    Engine* e;
    StrBuf* text;
    bool prev_valid;
    int32_t prev_file;
    uint32_t prev_seq;
    uint32_t prev_end;
};

static void add_segment(Engine* e, uint32_t out_start, uint32_t length, uint32_t src, bool synthesized) {
    if (length == 0) return;
    if (!e->segments.empty()) {
        OffsetSegment& last = e->segments.back();
        if (last.synthesized == synthesized && last.out_start + last.length == out_start &&
            (synthesized ? last.src_start == src : last.src_start + last.length == src)) {
            last.length += length;
            return;
        }
    }
    OffsetSegment seg = {out_start, length, src, synthesized};
    e->segments.append(seg);
}

static void append_bytes(Rebuilder* rb, const char* data, size_t n, uint32_t src, bool synthesized) {
    uint32_t start = (uint32_t)rb->text->length;
    strbuf_append_str_n(rb->text, data, n);
    add_segment(rb->e, start, (uint32_t)n, src, synthesized);
}

static void append_file_range(Rebuilder* rb, int32_t file, uint32_t start, uint32_t end, uint32_t origin) {
    if (end <= start) return;
    const SourceFile& f = rb->e->files[(size_t)file];
    if (end > f.length) end = f.length;
    if (end <= start) return;
    bool main = file == rb->e->main_file;
    append_bytes(rb, f.data + start, end - start, main ? start : origin, !main);
}

static bool ends_with_control_word(StrBuf* sb) {
    size_t i = sb->length;
    size_t letters = 0;
    while (i > 0) {
        char c = sb->str[i - 1];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '@') { i--; letters++; }
        else break;
    }
    if (letters == 0 || i == 0 || sb->str[i - 1] != '\\') return false;
    size_t slashes = 0;
    while (i > 0 && sb->str[i - 1] == '\\') { i--; slashes++; }
    return (slashes & 1) == 1;
}

static void separate(Rebuilder* rb, const char* next, size_t n, uint32_t origin) {
    if (n == 0) return;
    char c = next[0];
    bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '@';
    if (letter && ends_with_control_word(rb->text)) append_bytes(rb, " ", 1, origin, true);
}

static void canonical_token(Engine* e, const Token& t, StrBuf* out) {
    if (t.kind == TK_CS) {
        const NameRec& rec = name_of(e, t.value);
        uint32_t code = 0;
        if (name_is_active(rec, &code)) {
            strbuf_append_utf8(out, code);
            return;
        }
        if (t.value == e->cs_par) {
            strbuf_append_str(out, "\n\n");
            return;
        }
        if (rec.len == 0) {
            strbuf_append_str(out, "\\csname\\endcsname");
            return;
        }
        strbuf_append_char(out, '\\');
        strbuf_append_str_n(out, rec.chars, rec.len);
        return;
    }
    switch (t.cat) {
    case CAT_BEGIN: strbuf_append_char(out, '{'); return;
    case CAT_END: strbuf_append_char(out, '}'); return;
    case CAT_MATH: strbuf_append_char(out, '$'); return;
    case CAT_ALIGN: strbuf_append_char(out, '&'); return;
    case CAT_PARAM: strbuf_append_char(out, '#'); return;
    case CAT_SUPER: strbuf_append_char(out, '^'); return;
    case CAT_SUB: strbuf_append_char(out, '_'); return;
    case CAT_SPACE: strbuf_append_char(out, ' '); return;
    default: break;
    }
    switch (t.value) {
    case '\\': strbuf_append_str(out, "\\textbackslash{}"); return;
    case '{': strbuf_append_str(out, "\\{"); return;
    case '}': strbuf_append_str(out, "\\}"); return;
    case '$': strbuf_append_str(out, "\\$"); return;
    case '&': strbuf_append_str(out, "\\&"); return;
    case '#': strbuf_append_str(out, "\\#"); return;
    case '^': strbuf_append_str(out, "\\textasciicircum{}"); return;
    case '_': strbuf_append_str(out, "\\_"); return;
    case '%': strbuf_append_str(out, "\\%"); return;
    case '~': strbuf_append_str(out, "\\textasciitilde{}"); return;
    default: strbuf_append_utf8(out, t.value); return;
    }
}

// a token's own source bytes are reusable when the digester reads them back
// as the same token
static bool span_is_faithful(Engine* e, const Token& t) {
    if (t.file < 0 || t.end <= t.start) return false;
    const SourceFile& f = e->files[(size_t)t.file];
    if (t.end > f.length) return false;
    const char* s = f.data + t.start;
    uint32_t n = t.end - t.start;
    if (t.kind == TK_CS) {
        const NameRec& rec = name_of(e, t.value);
        uint32_t code = 0;
        if (name_is_active(rec, &code)) {
            char u[8];
            size_t ul = str_utf8_encode(code, u, sizeof(u));
            return ul == n && memcmp(u, s, n) == 0;
        }
        return n == rec.len + 1 && s[0] == '\\' && memcmp(s + 1, rec.chars, rec.len) == 0;
    }
    if (t.kind != TK_CHAR) return false;
    if (t.cat == CAT_SPACE) {
        for (uint32_t i = 0; i < n; i++) {
            if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') return false;
        }
        return true;
    }
    char u[8];
    size_t ul = str_utf8_encode(t.value, u, sizeof(u));
    if (ul != n || memcmp(u, s, n) != 0) return false;
    if (t.value == '@') return t.cat == CAT_LETTER || t.cat == CAT_OTHER;
    if (t.value >= 128) return t.cat == CAT_LETTER || t.cat == CAT_OTHER;
    return digester_cat(t.value) == t.cat;
}

void reconstruct(Engine* e) {
    Rebuilder rb = {};
    rb.e = e;
    rb.text = e->text;
    strbuf_reset(rb.text);
    StrBuf* tmp = strbuf_new();
    for (size_t i = 0; i < e->out.size(); i++) {
        const OutItem& it = e->out[i];
        switch (it.kind) {
        case OUT_SPAN: {
            bool adjacent = rb.prev_valid && rb.prev_file == it.file && it.tok.seq == rb.prev_seq + 1;
            if (adjacent) {
                append_file_range(&rb, it.file, rb.prev_end, it.start, it.origin);
            } else {
                const SourceFile& f = e->files[(size_t)it.file];
                separate(&rb, f.data + it.start, it.end - it.start, it.origin);
            }
            append_file_range(&rb, it.file, it.start, it.end, it.origin);
            rb.prev_valid = true;
            rb.prev_file = it.file;
            rb.prev_seq = it.last_seq;
            rb.prev_end = it.end;
            break;
        }
        case OUT_TEXT:
            separate(&rb, it.text, it.text_len, it.origin);
            append_bytes(&rb, it.text, it.text_len, it.origin, true);
            rb.prev_valid = false;
            break;
        case OUT_TOKEN: {
            const Token& t = it.tok;
            bool direct = (t.flags & TF_DIRECT) && t.file >= 0;
            bool adjacent = direct && rb.prev_valid && rb.prev_file == t.file && t.seq == rb.prev_seq + 1;
            bool main = t.file == e->main_file && direct;
            if (adjacent) append_file_range(&rb, t.file, rb.prev_end, t.start, it.origin);
            if (t.flags & TF_PARLINE) {
                // a blank line: its own bytes when adjacent, else a paragraph break
                if (adjacent) append_file_range(&rb, t.file, t.start, t.end, it.origin);
                else append_bytes(&rb, "\n\n", 2, main ? t.start : it.origin, !main);
            } else if (span_is_faithful(e, t) && (direct || t.kind == TK_CS || t.cat != CAT_SPACE)) {
                const SourceFile& f = e->files[(size_t)t.file];
                if (!adjacent) separate(&rb, f.data + t.start, t.end - t.start, it.origin);
                if (main) append_file_range(&rb, t.file, t.start, t.end, it.origin);
                else append_bytes(&rb, f.data + t.start, t.end - t.start, it.origin, true);
            } else {
                strbuf_reset(tmp);
                canonical_token(e, t, tmp);
                if (!adjacent) separate(&rb, tmp->str, tmp->length, it.origin);
                append_bytes(&rb, tmp->str, tmp->length, main ? t.start : it.origin, !main);
            }
            rb.prev_valid = direct;
            rb.prev_file = t.file;
            rb.prev_seq = t.seq;
            rb.prev_end = t.end;
            break;
        }
        }
    }
    strbuf_free(tmp);
}

size_t result_map_offset(const Result* result, size_t out_offset) {
    size_t lo = 0, hi = result->segment_count;
    if (hi == 0) return out_offset;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (result->segments[mid].out_start <= out_offset) lo = mid;
        else hi = mid;
    }
    const OffsetSegment& s = result->segments[lo];
    if (out_offset < s.out_start) return s.src_start;
    if (s.synthesized) return s.src_start;
    size_t delta = out_offset - s.out_start;
    if (delta > s.length) delta = s.length;
    return s.src_start + delta;
}

// ======================================================================
// lifecycle
// ======================================================================

static void init_tables(Engine* e) {
    e->cur_level = LEVEL_ONE;
    for (uint32_t c = 0; c < 256; c++) {
        for (int t = 0; t < CT_TABLES; t++) e->codes[t][c].level = LEVEL_ONE;
        e->codes[CT_CAT][c].v = CAT_OTHER;
        e->codes[CT_SF][c].v = 1000;
        e->codes[CT_MATH][c].v = (int32_t)c;
        e->codes[CT_DEL][c].v = -1;
    }
    // IniTeX catcodes (TeX §232)
    e->codes[CT_CAT]['\\'].v = CAT_ESCAPE;
    e->codes[CT_CAT]['%'].v = CAT_COMMENT;
    e->codes[CT_CAT][0].v = CAT_IGNORE;
    e->codes[CT_CAT][13].v = CAT_EOL;
    e->codes[CT_CAT][' '].v = CAT_SPACE;
    e->codes[CT_CAT][127].v = CAT_INVALID;
    for (uint32_t c = 'A'; c <= 'Z'; c++) {
        uint32_t l = c + 32;
        e->codes[CT_CAT][c].v = CAT_LETTER;
        e->codes[CT_CAT][l].v = CAT_LETTER;
        e->codes[CT_LC][c].v = (int32_t)l;
        e->codes[CT_LC][l].v = (int32_t)l;
        e->codes[CT_UC][c].v = (int32_t)c;
        e->codes[CT_UC][l].v = (int32_t)c;
        e->codes[CT_SF][c].v = 999;
        e->codes[CT_MATH][c].v = (int32_t)(0x7100 + c);
        e->codes[CT_MATH][l].v = (int32_t)(0x7100 + l);
    }
    for (uint32_t c = '0'; c <= '9'; c++) e->codes[CT_MATH][c].v = (int32_t)(0x7000 + c);
    e->codes[CT_DEL]['.'].v = 0;
    for (uint32_t i = 0; i < IP_COUNT; i++) e->int_pars[i].level = LEVEL_ONE;
    for (uint32_t i = 0; i < DP_COUNT; i++) e->dimen_pars[i].level = LEVEL_ONE;
    for (uint32_t i = 0; i < GP_COUNT; i++) e->glue_pars[i].level = LEVEL_ONE;
    for (uint32_t i = 0; i < TP_COUNT; i++) e->toks_pars[i].level = LEVEL_ONE;
    e->int_pars[IP_MAG].v = 1000;
    e->int_pars[IP_TOLERANCE].v = 10000;
    e->int_pars[IP_HANG_AFTER].v = 1;
    e->int_pars[IP_MAX_DEAD_CYCLES].v = 25;
    e->int_pars[IP_ESCAPE_CHAR].v = '\\';
    e->int_pars[IP_END_LINE_CHAR].v = 13;
    // fixed date: output depends only on the inputs (S12.1.1v2)
    e->int_pars[IP_TIME].v = 720;
    e->int_pars[IP_DAY].v = 1;
    e->int_pars[IP_MONTH].v = 1;
    e->int_pars[IP_YEAR].v = 2000;
    e->cur_font.level = LEVEL_ONE;
    for (int s = 0; s < 3; s++)
        for (int f = 0; f < 16; f++) e->fam_fonts[s][f].level = LEVEL_ONE;
    Font null_font = {};
    null_font.name = "nullfont";
    null_font.param_count = 7;   // all zero, at the start of font memory
    for (int k = 0; k < 7; k++) e->font_params.append(0);
    null_font.hyphen_char = '-';
    null_font.skew_char = -1;
    e->fonts.append(null_font);
    e->random_seed = 12345;
}

Engine* engine_create(const EngineOptions* options) {
    void* mem = mem_calloc(1, sizeof(Engine), MEM_CAT_INPUT_OTHER);
    if (!mem) return nullptr;
    Engine* e = new (mem) Engine();  // NEW_DELETE_OK: single audited construction boundary for the TeX engine.
    e->opts = *options;
    e->arena = arena_create(64 * 1024, 4 * 1024 * 1024);
    e->names.init(2048);
    e->wide_codes.init(64);
    e->term_line = strbuf_new();
    e->text = strbuf_new();
    e->scratch = strbuf_new();
    e->max_expansions = options->max_expansions ? options->max_expansions : DEFAULT_MAX_EXPANSIONS;
    e->main_file = -1;
    e->read_source = -1;
    e->echo_min_seq = UINT32_MAX;
    init_tables(e);
    init_primitives(e);
    e->cs_par = intern_cstr(e, "par");
    e->cs_relax = intern_cstr(e, "relax");
    e->cs_endcsname = intern_cstr(e, "endcsname");
    e->cs_frozen_relax = intern_cstr(e, "\x02relax");
    e->eqtb[e->cs_frozen_relax].m = make_prim(P_RELAX);
    e->fonts[0].ident = intern_cstr(e, "nullfont");
    e->cs_begin = intern_cstr(e, "begin");
    e->cs_end = intern_cstr(e, "end");
    e->cs_document = intern_cstr(e, "document");
    if (!options->ini) {
        latex_init(e);
        int format = options->expl3 ? load_expl3_format(e) : 0;
        if (format < 0) {
            // a half-loaded image leaves nothing usable: start over from the kernel
            engine_destroy(e);
            EngineOptions plain = *options;
            plain.expl3 = false;
            return engine_create(&plain);
        }
        e->expl3_format = format > 0;
        if (!e->expl3_format) load_kernel(e);
        // §9.2: adapter commands that read their arguments as tokens
        for (int i = 0; i < options->raw_command_count; i++) {
            uint32_t cs = intern_cstr(e, options->raw_commands[i]);
            Meaning m = {};
            m.type = MT_CONSTRUCTOR;
            m.sub = 1;
            m.value = cs;
            e->eqtb[cs].m = m;
        }
    }
    return e;
}

bool engine_wants_expl3(const Engine* e) {
    return e && e->wants_expl3;
}

bool engine_run(Engine* e, const char* source, size_t length, Result* result) {
    const char* name = e->opts.base_path ? e->opts.base_path : "<main>";
    e->main_file = add_source(e, name, source, (uint32_t)length);
    push_file(e, e->main_file, false);
    e->main_reader = e->input.back().reader;
    main_control(e);
    // TeX's "(\end occurred when \if... on line N was incomplete)", innermost first
    for (size_t k = e->conds.size(); k > 0; k--) {
        int32_t file = k == e->conds.size() ? e->if_file : e->conds[k].if_file;
        uint32_t line = k == e->conds.size() ? e->if_line_no : e->conds[k].if_line_no;
        const char* name = file >= 0 ? e->files[(size_t)file].name : "?";
        diag(e, "tex-incomplete-if", "(\\end occurred when a conditional on line %u of %s was incomplete)",
             line, name);
    }
    term_flush(e);
    reconstruct(e);
    Result r = {};
    r.text = e->text->str ? e->text->str : "";
    r.length = e->text->length;
    r.segments = e->segments.data();
    r.segment_count = e->segments.size();
    r.diagnostics = e->diagnostics.data();
    r.diagnostic_count = e->diagnostics.size();
    r.messages = e->messages.data();
    r.message_count = e->messages.size();
    r.loaded_packages = e->loaded.data();
    r.loaded_package_count = e->loaded.size();
    e->result = r;
    if (result) *result = r;
    return !e->aborted;
}

void engine_destroy(Engine* e) {
    if (!e) return;
    strbuf_free(e->term_line);
    strbuf_free(e->text);
    strbuf_free(e->scratch);
    e->names.destroy();
    e->wide_codes.destroy();
    if (e->arena) arena_destroy(e->arena);
    e->~Engine();
    mem_free(e);
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
