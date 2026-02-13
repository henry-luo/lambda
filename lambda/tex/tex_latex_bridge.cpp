// tex_latex_bridge.cpp - Implementation of LaTeX-to-TeX document conversion
//
// This file implements the bridge between LaTeX source files (parsed via
// tree-sitter-latex) and the TeX typesetting pipeline.

#include "tex_latex_bridge.hpp"
#include "tex_hlist.hpp"
#include "tex_linebreak.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "tex_math_bridge.hpp"
#include "tex_math_ts.hpp"
#include "tex_hyphen.hpp"
#include "../../lib/log.h"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/str.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace tex {

// ============================================================================
// LaTeXContext Implementation
// ============================================================================

LaTeXContext LaTeXContext::create(Arena* arena, TFMFontManager* fonts) {
    return create(arena, fonts, "article");
}

LaTeXContext LaTeXContext::create(Arena* arena, TFMFontManager* fonts, const char* doc_class) {
    LaTeXContext ctx = {};
    ctx.doc_ctx = DocumentContext::create(arena, fonts);

    ctx.document_class = doc_class;
    ctx.two_column = false;
    ctx.twosided = (strcmp(doc_class, "book") == 0);

    ctx.in_preamble = true;
    ctx.in_verbatim = false;

    // Initialize counters
    ctx.chapter_num = 0;
    ctx.section_num = 0;
    ctx.subsection_num = 0;
    ctx.subsubsection_num = 0;
    ctx.paragraph_num = 0;
    ctx.figure_num = 0;
    ctx.table_num = 0;
    ctx.equation_num = 0;
    ctx.page_num = 1;

    // Initialize labels
    ctx.labels = nullptr;
    ctx.label_count = 0;
    ctx.label_capacity = 0;

    return ctx;
}

void LaTeXContext::reset_chapter_counters() {
    section_num = 0;
    subsection_num = 0;
    subsubsection_num = 0;
    paragraph_num = 0;
    figure_num = 0;
    table_num = 0;
    equation_num = 0;
}

const char* LaTeXContext::format_section_number(int level, Arena* arena) {
    char* buf = (char*)arena_alloc(arena, 64);

    bool has_chapters = (strcmp(document_class, "book") == 0 ||
                         strcmp(document_class, "report") == 0);

    if (has_chapters) {
        switch (level) {
            case 0:  // chapter
                snprintf(buf, 64, "%d", chapter_num);
                break;
            case 1:  // section
                snprintf(buf, 64, "%d.%d", chapter_num, section_num);
                break;
            case 2:  // subsection
                snprintf(buf, 64, "%d.%d.%d", chapter_num, section_num, subsection_num);
                break;
            case 3:  // subsubsection
                snprintf(buf, 64, "%d.%d.%d.%d", chapter_num, section_num, subsection_num, subsubsection_num);
                break;
            default:
                buf[0] = '\0';
        }
    } else {
        // article class - no chapters
        switch (level) {
            case 1:  // section
                snprintf(buf, 64, "%d", section_num);
                break;
            case 2:  // subsection
                snprintf(buf, 64, "%d.%d", section_num, subsection_num);
                break;
            case 3:  // subsubsection
                snprintf(buf, 64, "%d.%d.%d", section_num, subsection_num, subsubsection_num);
                break;
            default:
                buf[0] = '\0';
        }
    }

    return buf;
}

void LaTeXContext::add_label(const char* label, const char* ref_text, int page) {
    if (label_count >= label_capacity) {
        int new_cap = label_capacity == 0 ? 16 : label_capacity * 2;
        LabelEntry* new_labels = (LabelEntry*)arena_alloc(doc_ctx.arena,
                                                           sizeof(LabelEntry) * new_cap);
        if (labels && label_count > 0) {
            memcpy(new_labels, labels, sizeof(LabelEntry) * label_count);
        }
        labels = new_labels;
        label_capacity = new_cap;
    }

    labels[label_count].label = label;
    labels[label_count].ref_text = ref_text;
    labels[label_count].page = page;
    label_count++;
}

const char* LaTeXContext::resolve_ref(const char* label) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].label, label) == 0) {
            return labels[i].ref_text;
        }
    }
    return "??";  // Undefined reference
}

// ============================================================================
// Helper Functions
// ============================================================================

// Check if tag matches (case-insensitive)
static bool tag_eq(const char* tag, const char* expected) {
    if (!tag || !expected) return false;
    return str_ieq(tag, strlen(tag), expected, strlen(expected));
}

// Get tag name from element
static const char* get_tag(const ElementReader& elem) {
    return elem.tagName();
}

// Create HListContext from LaTeXContext
static HListContext make_hlist_ctx(LaTeXContext& ctx) {
    DocumentContext& doc = ctx.doc_ctx;
    HListContext hctx(doc.arena, doc.fonts);
    hctx.current_tfm = doc.current_tfm();
    hctx.current_font = doc.current_font();
    hctx.apply_ligatures = true;
    hctx.apply_kerning = true;
    return hctx;
}

// Transfer nodes from source list to target
static void transfer_nodes(TexNode* target, TexNode* source) {
    if (!target || !source || !source->first_child) return;

    for (TexNode* n = source->first_child; n; ) {
        TexNode* next = n->next_sibling;
        n->prev_sibling = nullptr;
        n->next_sibling = nullptr;
        n->parent = nullptr;
        target->append_child(n);
        n = next;
    }
    source->first_child = nullptr;
    source->last_child = nullptr;
}

// ============================================================================
// Command Classification
// ============================================================================

bool is_section_command(const char* cmd) {
    if (!cmd) return false;
    // Note: LaTeX \paragraph{} and \subparagraph{} commands create section-like headings,
    // but tree-sitter-latex uses "paragraph" tag for text paragraph elements.
    // The \paragraph{} command appears as "section" node type with command name containing "paragraph".
    // We only match explicit section/chapter commands here to avoid confusion with paragraph content.
    return tag_eq(cmd, "part") || tag_eq(cmd, "chapter") ||
           tag_eq(cmd, "section") || tag_eq(cmd, "subsection") ||
           tag_eq(cmd, "subsubsection");
}

int get_section_level(const char* cmd) {
    if (!cmd) return -1;
    if (tag_eq(cmd, "part")) return -1;
    if (tag_eq(cmd, "chapter")) return 0;
    if (tag_eq(cmd, "section")) return 1;
    if (tag_eq(cmd, "subsection")) return 2;
    if (tag_eq(cmd, "subsubsection")) return 3;
    if (tag_eq(cmd, "paragraph")) return 4;
    if (tag_eq(cmd, "subparagraph")) return 5;
    return -1;
}

bool is_text_format_command(const char* cmd) {
    if (!cmd) return false;
    return tag_eq(cmd, "textbf") || tag_eq(cmd, "textit") ||
           tag_eq(cmd, "texttt") || tag_eq(cmd, "textrm") ||
           tag_eq(cmd, "textsf") || tag_eq(cmd, "textsc") ||
           tag_eq(cmd, "emph") || tag_eq(cmd, "underline");
}

bool is_font_declaration(const char* cmd) {
    if (!cmd) return false;
    return tag_eq(cmd, "bf") || tag_eq(cmd, "it") ||
           tag_eq(cmd, "tt") || tag_eq(cmd, "rm") ||
           tag_eq(cmd, "sf") || tag_eq(cmd, "sc") ||
           tag_eq(cmd, "bfseries") || tag_eq(cmd, "itshape") ||
           tag_eq(cmd, "ttfamily") || tag_eq(cmd, "rmfamily") ||
           tag_eq(cmd, "sffamily") || tag_eq(cmd, "scshape");
}

// ============================================================================
// Environment Classification
// ============================================================================

const char* get_environment_name(const ElementReader& elem) {
    // Environment name is stored in first child (begin_env) or as attribute
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag_eq(tag, "begin_env") || tag_eq(tag, "env_name")) {
                // Get environment name from child
                auto child_iter = child_elem.children();
                ItemReader name_item;
                while (child_iter.next(&name_item)) {
                    if (name_item.isString()) {
                        return name_item.cstring();
                    }
                    if (name_item.isElement()) {
                        ElementReader name_elem = name_item.asElement();
                        if (tag_eq(name_elem.tagName(), "env_name")) {
                            auto name_iter = name_elem.children();
                            ItemReader actual_name;
                            if (name_iter.next(&actual_name) && actual_name.isString()) {
                                return actual_name.cstring();
                            }
                        }
                    }
                }
            }
        }
    }

    // Fallback: check tag name of element itself
    const char* tag = elem.tagName();
    if (tag && strncmp(tag, "generic_environment", 19) != 0) {
        return tag;
    }

    return nullptr;
}

bool is_list_environment(const char* env) {
    if (!env) return false;
    return tag_eq(env, "itemize") || tag_eq(env, "enumerate") ||
           tag_eq(env, "description");
}

bool is_math_environment(const char* env) {
    if (!env) return false;
    return tag_eq(env, "equation") || tag_eq(env, "equation*") ||
           tag_eq(env, "align") || tag_eq(env, "align*") ||
           tag_eq(env, "gather") || tag_eq(env, "gather*") ||
           tag_eq(env, "multline") || tag_eq(env, "multline*") ||
           tag_eq(env, "displaymath") || tag_eq(env, "math");
}

// ============================================================================
// Utility Functions
// ============================================================================

bool latex_tag_is(const ElementReader& elem, const char* tag) {
    return tag_eq(elem.tagName(), tag);
}

const char* latex_get_attr(const ElementReader& elem, const char* attr) {
    return elem.get_attr_string(attr);
}

void extract_latex_text(const ItemReader& item, char* buffer, size_t buffer_size, size_t* out_len) {
    size_t pos = 0;

    if (item.isString()) {
        const char* str = item.cstring();
        if (str) {
            size_t len = strlen(str);
            if (len > buffer_size - 1) len = buffer_size - 1;
            memcpy(buffer, str, len);
            pos = len;
        }
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child) && pos < buffer_size - 1) {
            size_t child_len = 0;
            extract_latex_text(child, buffer + pos, buffer_size - pos, &child_len);
            pos += child_len;
        }
    }

    buffer[pos] = '\0';
    if (out_len) *out_len = pos;
}

// ============================================================================
// Text Processing
// ============================================================================

// Build HList from plain text
static TexNode* build_text_hlist(const char* text, size_t len, LaTeXContext& ctx) {
    if (!text || len == 0) {
        return make_hlist(ctx.doc_ctx.arena);
    }

    TFMFont* tfm = ctx.doc_ctx.current_tfm();
    if (!tfm) {
        log_error("latex_bridge: no TFM font available");
        return make_hlist(ctx.doc_ctx.arena);
    }

    HListContext hctx = make_hlist_ctx(ctx);
    return text_to_hlist(text, len, hctx);
}

// ============================================================================
// Inline Content Conversion
// ============================================================================

// Forward declaration
static TexNode* convert_inline_item(const ItemReader& item, LaTeXContext& ctx, Pool* pool);

// Append text to HList
static void append_text_to_hlist(TexNode* hlist, const char* text, size_t len, LaTeXContext& ctx) {
    if (!hlist || !text || len == 0) return;

    TexNode* text_nodes = build_text_hlist(text, len, ctx);
    if (text_nodes) {
        transfer_nodes(hlist, text_nodes);
    }
}

// Convert emphasis (\emph, \textit)
static void append_emphasis(TexNode* hlist, const ElementReader& elem, LaTeXContext& ctx, Pool* pool) {
    TextStyle saved = ctx.doc_ctx.format.style;

    if (ctx.doc_ctx.format.style == TextStyle::Bold) {
        ctx.doc_ctx.format.style = TextStyle::BoldItalic;
    } else {
        ctx.doc_ctx.format.style = TextStyle::Italic;
    }

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_item(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    ctx.doc_ctx.format.style = saved;
}

// Convert bold (\textbf)
static void append_bold(TexNode* hlist, const ElementReader& elem, LaTeXContext& ctx, Pool* pool) {
    TextStyle saved = ctx.doc_ctx.format.style;

    if (ctx.doc_ctx.format.style == TextStyle::Italic) {
        ctx.doc_ctx.format.style = TextStyle::BoldItalic;
    } else {
        ctx.doc_ctx.format.style = TextStyle::Bold;
    }

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_item(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    ctx.doc_ctx.format.style = saved;
}

// Convert monospace (\texttt, verbatim)
static void append_monospace(TexNode* hlist, const ElementReader& elem, LaTeXContext& ctx, Pool* pool) {
    TextStyle saved = ctx.doc_ctx.format.style;
    ctx.doc_ctx.format.style = TextStyle::Monospace;

    // Get text content
    Pool* local_pool = pool ? pool : pool_create();
    StringBuf* sb = stringbuf_new(local_pool);
    elem.textContent(sb);

    if (sb->length > 0 && sb->str) {
        append_text_to_hlist(hlist, sb->str->chars, sb->length, ctx);
    }

    if (!pool) {
        stringbuf_free(sb);
        pool_destroy(local_pool);
    }

    ctx.doc_ctx.format.style = saved;
}

// Convert inline math
static void append_inline_math(TexNode* hlist, const ElementReader& elem, LaTeXContext& ctx, Pool* pool) {
    // Check if this is display math - either by element tag or by child element
    const char* elem_tag = elem.tagName();
    bool is_display = tag_eq(elem_tag, "display_math") || tag_eq(elem_tag, "displaymath");

    // Also check children for nested display_math (legacy structure)
    if (!is_display) {
        auto check_iter = elem.children();
        ItemReader check_child;
        while (check_iter.next(&check_child)) {
            if (check_child.isElement()) {
                ElementReader child_elem = check_child.asElement();
                const char* child_tag = child_elem.tagName();
                if (tag_eq(child_tag, "display_math") || tag_eq(child_tag, "displaymath")) {
                    is_display = true;
                    break;
                }
            }
        }
    }

    MathContext math_ctx = ctx.doc_ctx.math_context();
    math_ctx.style = is_display ? MathStyle::Display : MathStyle::Text;

    // Use pre-parsed AST from input-latex-ts.cpp
    ItemReader ast_attr = elem.get_attr("ast");
    if (ast_attr.isNull() || !ast_attr.isElement()) {
        log_debug("latex_bridge: inline math missing ast attribute");
        return;
    }

    TexNode* math_hbox = typeset_math_from_ast(ast_attr, math_ctx);
    if (math_hbox) {
        hlist->append_child(math_hbox);
    }
}

// Convert spacing command
TexNode* make_latex_hspace(const char* command, LaTeXContext& ctx) {
    Arena* arena = ctx.doc_ctx.arena;
    float em = ctx.doc_ctx.base_size_pt;  // 1em = base font size

    Glue glue;

    if (tag_eq(command, "quad") || tag_eq(command, "\\quad")) {
        glue = Glue::fixed(em);
    } else if (tag_eq(command, "qquad") || tag_eq(command, "\\qquad")) {
        glue = Glue::fixed(2 * em);
    } else if (tag_eq(command, ",") || tag_eq(command, "\\,") || tag_eq(command, "thinspace")) {
        glue = Glue::fixed(em / 6.0f);  // thin space
    } else if (tag_eq(command, ";") || tag_eq(command, "\\;") || tag_eq(command, "thickspace")) {
        glue = Glue::fixed(em * 5.0f / 18.0f);  // thick space
    } else if (tag_eq(command, ":") || tag_eq(command, "\\:") || tag_eq(command, "medspace")) {
        glue = Glue::fixed(em * 4.0f / 18.0f);  // medium space
    } else if (tag_eq(command, "!") || tag_eq(command, "\\!") || tag_eq(command, "negthinspace")) {
        glue = Glue::fixed(-em / 6.0f);  // negative thin space
    } else if (tag_eq(command, "enspace") || tag_eq(command, "\\enspace")) {
        glue = Glue::fixed(em / 2.0f);
    } else if (tag_eq(command, "hfill") || tag_eq(command, "\\hfill")) {
        glue = Glue::fil(0);  // hfill is infinite stretch glue
    } else {
        // Default to normal space
        TFMFont* tfm = ctx.doc_ctx.current_tfm();
        if (tfm) {
            glue = Glue::flexible(
                tfm->params[TFM_PARAM_SPACE],
                tfm->params[TFM_PARAM_SPACE_STRETCH],
                tfm->params[TFM_PARAM_SPACE_SHRINK]
            );
        } else {
            glue = Glue::fixed(em / 3.0f);
        }
    }

    return make_glue(arena, glue);
}

// Handle special escaped characters
void append_latex_special_char(TexNode* hlist, char ch, LaTeXContext& ctx) {
    char buf[2] = {ch, '\0'};
    append_text_to_hlist(hlist, buf, 1, ctx);
}

// Convert inline content item
static TexNode* convert_inline_item(const ItemReader& item, LaTeXContext& ctx, Pool* pool) {
    TexNode* hlist = make_hlist(ctx.doc_ctx.arena);

    if (item.isString()) {
        const char* str = item.cstring();
        if (str) {
            append_text_to_hlist(hlist, str, strlen(str), ctx);
        }
    } else if (item.isSymbol()) {
        // Handle symbols like parbreak, space commands, etc.
        Symbol* sym_str = item.asSymbol();
        const char* sym = sym_str ? sym_str->chars : nullptr;
        if (sym) {
            if (tag_eq(sym, "parbreak")) {
                // Paragraph break - return empty, caller handles
            } else if (tag_eq(sym, "thinspace") || tag_eq(sym, "thickspace") ||
                       tag_eq(sym, "medspace") || tag_eq(sym, "negthinspace")) {
                TexNode* space = make_latex_hspace(sym, ctx);
                if (space) hlist->append_child(space);
            }
        }
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        const char* tag = elem.tagName();

        // Skip preamble-only commands and structural elements
        if (tag && (tag_eq(tag, "documentclass") || tag_eq(tag, "pagestyle") ||
                    tag_eq(tag, "usepackage") || tag_eq(tag, "title") ||
                    tag_eq(tag, "author") || tag_eq(tag, "date") ||
                    tag_eq(tag, "newcommand") || tag_eq(tag, "renewcommand") ||
                    tag_eq(tag, "setlength") || tag_eq(tag, "setcounter") ||
                    tag_eq(tag, "newenvironment") || tag_eq(tag, "renewenvironment") ||
                    tag_eq(tag, "newtheorem") || tag_eq(tag, "DeclareMathOperator") ||
                    tag_eq(tag, "bibliographystyle") || tag_eq(tag, "makeatletter") ||
                    tag_eq(tag, "makeatother") || tag_eq(tag, "input") ||
                    tag_eq(tag, "include") || tag_eq(tag, "includeonly") ||
                    tag_eq(tag, "begin") || tag_eq(tag, "end"))) {
            return nullptr;
        }

        if (!tag) {
            // Process children directly
            auto iter = elem.children();
            ItemReader child;
            while (iter.next(&child)) {
                TexNode* child_nodes = convert_inline_item(child, ctx, pool);
                if (child_nodes) {
                    transfer_nodes(hlist, child_nodes);
                }
            }
        } else if (tag_eq(tag, "command")) {
            // Generic command - check first child for command name
            auto iter = elem.children();
            ItemReader first;
            if (iter.next(&first)) {
                const char* cmd_name = nullptr;
                if (first.isString()) {
                    cmd_name = first.cstring();
                } else if (first.isElement()) {
                    ElementReader cmd_elem = first.asElement();
                    if (tag_eq(cmd_elem.tagName(), "command_name")) {
                        auto cmd_iter = cmd_elem.children();
                        ItemReader name_item;
                        if (cmd_iter.next(&name_item) && name_item.isString()) {
                            cmd_name = name_item.cstring();
                        }
                    }
                }

                if (cmd_name) {
                    // Skip backslash if present
                    if (cmd_name[0] == '\\') cmd_name++;

                    if (tag_eq(cmd_name, "textbf") || tag_eq(cmd_name, "bf")) {
                        append_bold(hlist, elem, ctx, pool);
                    } else if (tag_eq(cmd_name, "textit") || tag_eq(cmd_name, "it") ||
                               tag_eq(cmd_name, "emph")) {
                        append_emphasis(hlist, elem, ctx, pool);
                    } else if (tag_eq(cmd_name, "texttt") || tag_eq(cmd_name, "tt") ||
                               tag_eq(cmd_name, "verb")) {
                        append_monospace(hlist, elem, ctx, pool);
                    } else if (tag_eq(cmd_name, "quad") || tag_eq(cmd_name, "qquad") ||
                               tag_eq(cmd_name, "hspace") || tag_eq(cmd_name, "hfill")) {
                        TexNode* space = make_latex_hspace(cmd_name, ctx);
                        if (space) hlist->append_child(space);
                    } else {
                        // Unknown command - process children
                        auto child_iter = elem.children();
                        ItemReader child;
                        while (child_iter.next(&child)) {
                            TexNode* child_nodes = convert_inline_item(child, ctx, pool);
                            if (child_nodes) {
                                transfer_nodes(hlist, child_nodes);
                            }
                        }
                    }
                }
            }
        } else if (tag_eq(tag, "textbf")) {
            append_bold(hlist, elem, ctx, pool);
        } else if (tag_eq(tag, "textit") || tag_eq(tag, "emph")) {
            append_emphasis(hlist, elem, ctx, pool);
        } else if (tag_eq(tag, "texttt") || tag_eq(tag, "verb") || tag_eq(tag, "verb_command")) {
            append_monospace(hlist, elem, ctx, pool);
        } else if (tag_eq(tag, "inline_math") || tag_eq(tag, "math") || tag_eq(tag, "display_math")) {
            // Both inline and display math can appear in paragraphs
            // append_inline_math checks for display_math to set style appropriately
            append_inline_math(hlist, elem, ctx, pool);
        } else if (tag_eq(tag, "space_cmd")) {
            // Get the space command content
            auto iter = elem.children();
            ItemReader cmd;
            if (iter.next(&cmd) && cmd.isString()) {
                const char* cmd_str = cmd.cstring();
                if (cmd_str) {
                    // Skip backslash
                    if (cmd_str[0] == '\\') cmd_str++;
                    TexNode* space = make_latex_hspace(cmd_str, ctx);
                    if (space) hlist->append_child(space);
                }
            }
        } else if (tag_eq(tag, "curly_group") || tag_eq(tag, "brack_group")) {
            // Process group content
            auto iter = elem.children();
            ItemReader child;
            while (iter.next(&child)) {
                TexNode* child_nodes = convert_inline_item(child, ctx, pool);
                if (child_nodes) {
                    transfer_nodes(hlist, child_nodes);
                }
            }
        } else if (tag_eq(tag, "tabular")) {
            // Tabular inside inline context - convert and embed as hbox
            TexNode* tabular_vlist = convert_latex_tabular(elem, ctx);
            if (tabular_vlist) {
                // Wrap the vlist in an hbox so it can go in horizontal mode
                TexNode* hbox = make_hbox(ctx.doc_ctx.arena);
                // Transfer all children from the vlist to the hbox
                TexNode* child = tabular_vlist->first_child;
                while (child) {
                    TexNode* next = child->next_sibling;
                    child->prev_sibling = nullptr;
                    child->next_sibling = nullptr;
                    hbox->append_child(child);
                    child = next;
                }
                hlist->append_child(hbox);
            }
        } else if (tag_eq(tag, "vspace") || tag_eq(tag, "hspace")) {
            // Vertical/horizontal space commands - skip content, handled at block level
            // (don't render "1cm" as text)
        } else if (tag_eq(tag, "control_symbol")) {
            // Escaped special character
            auto iter = elem.children();
            ItemReader child;
            if (iter.next(&child) && child.isString()) {
                const char* s = child.cstring();
                if (s && s[0] == '\\' && s[1]) {
                    append_latex_special_char(hlist, s[1], ctx);
                }
            }
        } else if (tag_eq(tag, "nbsp")) {
            // Non-breaking space
            TexNode* kern = make_kern(ctx.doc_ctx.arena, ctx.doc_ctx.base_size_pt / 3.0f);
            if (kern) hlist->append_child(kern);
        } else {
            // Unknown tag - process children
            auto iter = elem.children();
            ItemReader child;
            while (iter.next(&child)) {
                TexNode* child_nodes = convert_inline_item(child, ctx, pool);
                if (child_nodes) {
                    transfer_nodes(hlist, child_nodes);
                }
            }
        }
    }

    return hlist;
}

TexNode* convert_latex_inline(const ItemReader& content, LaTeXContext& ctx) {
    Pool* pool = pool_create();
    TexNode* result = convert_inline_item(content, ctx, pool);
    pool_destroy(pool);
    return result;
}

// ============================================================================
// Paragraph Processing
// ============================================================================

TexNode* build_latex_paragraph_hlist(const ElementReader& elem, LaTeXContext& ctx) {
    Pool* pool = pool_create();
    TexNode* hlist = make_hlist(ctx.doc_ctx.arena);

    int child_idx = 0;
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        child_idx++;
        if (child.isElement()) {
            ElementReader ce = child.asElement();
            fprintf(stderr, "[DEBUG] build_paragraph_hlist: child %d tag='%s'\n", child_idx, ce.tagName());
        } else if (child.isString()) {
            const char* s = child.cstring();
            fprintf(stderr, "[DEBUG] build_paragraph_hlist: child %d is string '%s'\n", child_idx, s ? s : "(null)");
        }
        TexNode* child_nodes = convert_inline_item(child, ctx, pool);
        fprintf(stderr, "[DEBUG] build_paragraph_hlist: child %d produced %p\n", child_idx, (void*)child_nodes);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    pool_destroy(pool);
    fprintf(stderr, "[DEBUG] build_paragraph_hlist: final hlist has %d children\n",
            hlist->first_child ? 1 : 0);
    return hlist;
}

TexNode* break_latex_paragraph(TexNode* hlist, LaTeXContext& ctx) {
    if (!hlist || !hlist->first_child) {
        return nullptr;
    }

    // Apply hyphenation if available
    if (ctx.doc_ctx.hyphenator) {
        FontSpec font = ctx.doc_ctx.current_font();
        hlist = insert_discretionary_hyphens(hlist, ctx.doc_ctx.hyphenator, font, ctx.doc_ctx.arena);
    }

    // Apply line breaking
    LineBreakParams params = ctx.doc_ctx.line_break_params();
    TexNode* paragraph = typeset_paragraph(hlist, params, ctx.doc_ctx.baseline_skip(), ctx.doc_ctx.arena);

    return paragraph;
}

// Convert paragraph that may contain display math
// Returns a VList containing all content (text paragraphs + display math blocks)
TexNode* convert_latex_paragraph(const ElementReader& elem, LaTeXContext& ctx) {
    fprintf(stderr, "[DEBUG] convert_latex_paragraph: processing paragraph with display math splitting\n");

    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    Pool* pool = pool_create();
    TexNode* current_hlist = make_hlist(ctx.doc_ctx.arena);
    bool has_inline_content = false;

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        bool is_display = false;
        ElementReader child_elem;

        if (child.isElement()) {
            child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && (tag_eq(tag, "display_math") || tag_eq(tag, "displaymath"))) {
                is_display = true;
            }
        }

        if (is_display) {
            // Flush current inline content as a paragraph first
            if (has_inline_content && current_hlist->first_child) {
                TexNode* para = break_latex_paragraph(current_hlist, ctx);
                if (para) {
                    add_raw(vctx, para);
                    // Add inter-paragraph skip
                    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));
                }
                // Start new hlist
                current_hlist = make_hlist(ctx.doc_ctx.arena);
                has_inline_content = false;
            }

            // Add the display math block - flatten if it's a VList to enable page breaks
            TexNode* display = convert_latex_display_math(child_elem, ctx);
            if (display) {
                if (display->node_class == NodeClass::VList && display->first_child) {
                    // Flatten: move children to parent VList
                    TexNode* dchild = display->first_child;
                    while (dchild) {
                        TexNode* next = dchild->next_sibling;
                        dchild->prev_sibling = nullptr;
                        dchild->next_sibling = nullptr;
                        dchild->parent = nullptr;
                        add_raw(vctx, dchild);
                        dchild = next;
                    }
                } else {
                    add_raw(vctx, display);
                }
            }
        } else {
            // Add to current inline content
            TexNode* child_nodes = convert_inline_item(child, ctx, pool);
            if (child_nodes) {
                transfer_nodes(current_hlist, child_nodes);
                // Check if we added any real content (not just whitespace)
                if (current_hlist->first_child) {
                    has_inline_content = true;
                }
            }
        }
    }

    // Flush any remaining inline content
    if (has_inline_content && current_hlist->first_child) {
        TexNode* para = break_latex_paragraph(current_hlist, ctx);
        if (para) {
            add_raw(vctx, para);
        }
    }

    pool_destroy(pool);

    TexNode* result = end_vlist(vctx);
    if (result && !result->first_child) {
        return nullptr;  // Empty paragraph
    }
    return result;
}

// ============================================================================
// Section Conversion
// ============================================================================

TexNode* convert_latex_section(const ElementReader& elem, int level, LaTeXContext& ctx) {
    // Increment appropriate counter
    bool has_chapters = (strcmp(ctx.document_class, "book") == 0 ||
                         strcmp(ctx.document_class, "report") == 0);

    switch (level) {
        case 0:  // chapter
            ctx.chapter_num++;
            ctx.reset_chapter_counters();
            break;
        case 1:  // section
            ctx.section_num++;
            ctx.subsection_num = 0;
            ctx.subsubsection_num = 0;
            break;
        case 2:  // subsection
            ctx.subsection_num++;
            ctx.subsubsection_num = 0;
            break;
        case 3:  // subsubsection
            ctx.subsubsection_num++;
            break;
    }

    // Get section title from 'title' attribute
    const char* title = nullptr;
    ItemReader title_item = elem.get_attr("title");
    if (!title_item.isNull()) {
        if (title_item.isElement()) {
            // Title is an element (curly_group) - extract text
            Pool* pool = pool_create();
            StringBuf* sb = stringbuf_new(pool);
            ElementReader title_elem = title_item.asElement();
            title_elem.textContent(sb);
            if (sb->length > 0 && sb->str) {
                char* title_copy = (char*)arena_alloc(ctx.doc_ctx.arena, sb->length + 1);
                memcpy(title_copy, sb->str->chars, sb->length);
                title_copy[sb->length] = '\0';
                title = title_copy;
            }
            stringbuf_free(sb);
            pool_destroy(pool);
        } else if (title_item.isString()) {
            title = title_item.cstring();
        }
    }

    if (!title) {
        // Fallback: get title from children
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                if (tag_eq(child_elem.tagName(), "curly_group")) {
                    Pool* pool = pool_create();
                    StringBuf* sb = stringbuf_new(pool);
                    child_elem.textContent(sb);
                    if (sb->length > 0 && sb->str) {
                        char* title_copy = (char*)arena_alloc(ctx.doc_ctx.arena, sb->length + 1);
                        memcpy(title_copy, sb->str->chars, sb->length);
                        title_copy[sb->length] = '\0';
                        title = title_copy;
                    }
                    stringbuf_free(sb);
                    pool_destroy(pool);
                    break;
                }
            }
        }
    }

    if (!title) {
        title = "Untitled Section";
    }

    // Build section number
    const char* sec_num = ctx.format_section_number(level, ctx.doc_ctx.arena);

    // Build full title: "1.2 Section Title"
    size_t full_len = strlen(sec_num) + 1 + strlen(title) + 1;
    char* full_title = (char*)arena_alloc(ctx.doc_ctx.arena, full_len);
    if (sec_num[0]) {
        snprintf(full_title, full_len, "%s %s", sec_num, title);
    } else {
        snprintf(full_title, full_len, "%s", title);
    }

    // Size factors for different heading levels
    static const float SIZE_FACTORS[] = {1.728f, 1.44f, 1.2f, 1.0f, 0.9f, 0.8f};
    float factor = (level >= 0 && level < 6) ? SIZE_FACTORS[level] : 1.0f;

    // Build HList for title
    TextStyle saved_style = ctx.doc_ctx.format.style;
    ctx.doc_ctx.format.style = TextStyle::Bold;

    HListContext hctx = make_hlist_ctx(ctx);
    TexNode* title_hlist = text_to_hlist(full_title, strlen(full_title), hctx);

    ctx.doc_ctx.format.style = saved_style;

    // Create VList with heading
    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    vctx.body_font = ctx.doc_ctx.bold_font;
    vctx.body_font.size_pt *= factor;

    begin_vlist(vctx);

    // Space above heading
    float space_above = (level <= 1) ? 18.0f : 12.0f;
    add_vspace(vctx, Glue::flexible(space_above, 4.0f, 2.0f));

    // Add heading line
    if (title_hlist) {
        HListDimensions dims = measure_hlist(title_hlist);
        TexNode* heading_hbox = hlist_to_hbox(title_hlist, dims.width, ctx.doc_ctx.arena);
        if (heading_hbox) {
            add_line(vctx, heading_hbox);
        }
    }

    // Space below heading
    float space_below = (level <= 1) ? 12.0f : 6.0f;
    add_vspace(vctx, Glue::flexible(space_below, 2.0f, 1.0f));

    return end_vlist(vctx);
}

// ============================================================================
// List Conversion
// ============================================================================

TexNode* convert_latex_list(const ElementReader& elem, bool ordered, bool description, LaTeXContext& ctx) {
    // Increase list depth
    int depth = ctx.doc_ctx.format.list_depth;
    ctx.doc_ctx.format.list_depth++;
    if (depth < 8) {
        ctx.doc_ctx.format.list_counter[depth] = 0;
    }

    // Create VList for list items
    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    // Process children looking for \item commands
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (!child.isElement()) continue;

        ElementReader child_elem = child.asElement();
        const char* tag = child_elem.tagName();

        // Check for item command
        bool is_item = tag_eq(tag, "item") || tag_eq(tag, "command");

        if (is_item) {
            // Increment counter for ordered lists
            if (ordered && depth < 8) {
                ctx.doc_ctx.format.list_counter[depth]++;
            }

            // Determine bullet/number marker
            const char* marker;
            char num_buf[16];

            if (ordered && depth < 8) {
                snprintf(num_buf, sizeof(num_buf), "%d.", ctx.doc_ctx.format.list_counter[depth]);
                marker = num_buf;
            } else {
                // Bullet markers by depth
                static const char* BULLETS[] = {"•", "◦", "▪", "▫"};
                marker = BULLETS[(depth) % 4];
            }

            // Calculate indent
            float indent = ctx.doc_ctx.parindent + depth * 15.0f;

            // Build item content
            Pool* pool = pool_create();
            TexNode* content_hlist = make_hlist(ctx.doc_ctx.arena);

            // Add marker
            HListContext hctx = make_hlist_ctx(ctx);
            TexNode* marker_hlist = text_to_hlist(marker, strlen(marker), hctx);
            if (marker_hlist) {
                transfer_nodes(content_hlist, marker_hlist);
            }

            // Add space after marker
            TexNode* marker_space = make_glue(ctx.doc_ctx.arena, Glue::fixed(5.0f));
            if (marker_space) content_hlist->append_child(marker_space);

            // Add item content
            auto item_iter = child_elem.children();
            ItemReader item_child;
            while (item_iter.next(&item_child)) {
                TexNode* item_nodes = convert_inline_item(item_child, ctx, pool);
                if (item_nodes) {
                    transfer_nodes(content_hlist, item_nodes);
                }
            }

            pool_destroy(pool);

            // Break into lines
            if (content_hlist && content_hlist->first_child) {
                if (ctx.doc_ctx.hyphenator) {
                    content_hlist = insert_discretionary_hyphens(content_hlist, ctx.doc_ctx.hyphenator,
                                                                  ctx.doc_ctx.current_font(), ctx.doc_ctx.arena);
                }

                LineBreakParams params = ctx.doc_ctx.line_break_params();
                params.hsize = ctx.doc_ctx.text_width - indent;
                TexNode* lines = typeset_paragraph(content_hlist, params, ctx.doc_ctx.baseline_skip(), ctx.doc_ctx.arena);

                if (lines) {
                    add_raw(vctx, lines);
                }
            }
        }
    }

    // Restore list depth
    ctx.doc_ctx.format.list_depth = depth;

    return end_vlist(vctx);
}

// ============================================================================
// Quote Conversion
// ============================================================================

TexNode* convert_latex_quote(const ElementReader& elem, bool quotation, LaTeXContext& ctx) {
    // Increase margins
    float saved_left = ctx.doc_ctx.margin_left;
    float saved_width = ctx.doc_ctx.text_width;

    float indent = 20.0f;
    ctx.doc_ctx.margin_left += indent;
    ctx.doc_ctx.text_width -= indent * 2;

    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    // Space above
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Process children
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            TexNode* block = convert_latex_block(child_elem, ctx);
            if (block) {
                add_raw(vctx, block);
            }
        } else if (child.isString()) {
            const char* str = child.cstring();
            if (str && strlen(str) > 0) {
                TexNode* hlist = build_text_hlist(str, strlen(str), ctx);
                if (hlist && hlist->first_child) {
                    TexNode* para = break_latex_paragraph(hlist, ctx);
                    if (para) {
                        add_raw(vctx, para);
                    }
                }
            }
        }
    }

    // Space below
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Restore margins
    ctx.doc_ctx.margin_left = saved_left;
    ctx.doc_ctx.text_width = saved_width;

    return end_vlist(vctx);
}

// ============================================================================
// Verbatim Conversion
// ============================================================================

TexNode* convert_latex_verbatim(const ElementReader& elem, LaTeXContext& ctx) {
    TextStyle saved = ctx.doc_ctx.format.style;
    ctx.doc_ctx.format.style = TextStyle::Monospace;

    Pool* pool = pool_create();
    StringBuf* sb = stringbuf_new(pool);
    elem.textContent(sb);

    if (!sb->str || sb->length == 0) {
        stringbuf_free(sb);
        pool_destroy(pool);
        ctx.doc_ctx.format.style = saved;
        return nullptr;
    }

    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    vctx.body_font = ctx.doc_ctx.mono_font;
    begin_vlist(vctx);

    // Space above
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    // Split by lines
    const char* text = sb->str->chars;
    const char* line_start = text;
    const char* end = text + sb->length;

    while (line_start < end) {
        const char* line_end = line_start;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }

        size_t line_len = line_end - line_start;
        if (line_len > 0) {
            HListContext hctx = make_hlist_ctx(ctx);
            TexNode* line_hlist = text_to_hlist(line_start, line_len, hctx);

            if (line_hlist) {
                HListDimensions dims = measure_hlist(line_hlist);
                TexNode* line_hbox = hlist_to_hbox(line_hlist, dims.width, ctx.doc_ctx.arena);
                if (line_hbox) {
                    add_line(vctx, line_hbox);
                }
            }
        } else {
            add_vspace(vctx, Glue::fixed(ctx.doc_ctx.baseline_skip() * 0.5f));
        }

        line_start = line_end;
        if (line_start < end && *line_start == '\n') {
            line_start++;
        }
    }

    // Space below
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    stringbuf_free(sb);
    pool_destroy(pool);
    ctx.doc_ctx.format.style = saved;

    return end_vlist(vctx);
}

// ============================================================================
// Tabular Environment Conversion
// ============================================================================

// Column alignment from column spec character
enum class ColAlign { Left, Center, Right };

// Parse tabular column specification (e.g., "|c|c|c|" or "lcr")
struct TabularColSpec {
    ColAlign* aligns;       // alignment for each column
    bool* left_borders;     // border on left side of each column
    bool* right_borders;    // border on right side of each column
    int num_cols;
};

static TabularColSpec parse_tabular_colspec(const char* spec, Arena* arena) {
    TabularColSpec result = {};
    if (!spec || !*spec) return result;

    // First pass: count columns
    int col_count = 0;
    for (const char* p = spec; *p; p++) {
        if (*p == 'l' || *p == 'c' || *p == 'r') {
            col_count++;
        }
    }

    if (col_count == 0) return result;

    result.num_cols = col_count;
    result.aligns = (ColAlign*)arena_alloc(arena, col_count * sizeof(ColAlign));
    result.left_borders = (bool*)arena_alloc(arena, col_count * sizeof(bool));
    result.right_borders = (bool*)arena_alloc(arena, col_count * sizeof(bool));

    // Second pass: parse columns
    int col = 0;
    bool pending_border = false;
    for (const char* p = spec; *p && col < col_count; p++) {
        if (*p == '|') {
            if (col == 0 && result.aligns[0] == ColAlign::Left) {
                // Border before first column
                pending_border = true;
            } else if (col > 0) {
                result.right_borders[col - 1] = true;
            }
            if (col < col_count) {
                result.left_borders[col] = true;
            }
        } else if (*p == 'l') {
            result.aligns[col] = ColAlign::Left;
            result.left_borders[col] = pending_border;
            pending_border = false;
            col++;
        } else if (*p == 'c') {
            result.aligns[col] = ColAlign::Center;
            result.left_borders[col] = pending_border;
            pending_border = false;
            col++;
        } else if (*p == 'r') {
            result.aligns[col] = ColAlign::Right;
            result.left_borders[col] = pending_border;
            pending_border = false;
            col++;
        }
    }

    return result;
}

// Split text by separator, returning array of strings
struct SplitResult {
    const char** parts;
    int count;
};

static SplitResult split_text(const char* text, size_t len, char sep, Arena* arena) {
    SplitResult result = {};
    if (!text || len == 0) return result;

    // Count parts
    int count = 1;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == sep) count++;
    }

    result.count = count;
    result.parts = (const char**)arena_alloc(arena, count * sizeof(const char*));

    // Split
    int part = 0;
    const char* start = text;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == sep) {
            size_t part_len = (text + i) - start;
            char* part_str = (char*)arena_alloc(arena, part_len + 1);
            memcpy(part_str, start, part_len);
            part_str[part_len] = '\0';
            result.parts[part++] = part_str;
            start = text + i + 1;
        }
    }

    return result;
}

// Trim leading/trailing whitespace
static const char* trim_whitespace(const char* str, Arena* arena) {
    if (!str) return "";
    while (*str && isspace(*str)) str++;
    if (!*str) return "";

    size_t len = strlen(str);
    while (len > 0 && isspace(str[len - 1])) len--;

    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

// Helper: collect content children from tabular, skipping curly_group but including inline_math
// Returns a dynamically allocated array of child elements/strings
enum TabularItemType {
    TABULAR_CONTENT = 0,
    TABULAR_ROW_SEP = 1,
    TABULAR_HLINE = 2,
    TABULAR_COL_SEP = 3
};

struct TabularItem {
    ItemReader item;
    TabularItemType type;
};

static void collect_tabular_content(const ElementReader& elem,
                                     ArrayList* items,  // ArrayList of TabularItem*
                                     bool* found_col_spec,
                                     char** col_spec_out,
                                     Arena* arena) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            // First curly_group is the column spec
            if (!*found_col_spec && tag_eq(tag, "curly_group")) {
                Pool* spec_pool = pool_create();
                StringBuf* sb = stringbuf_new(spec_pool);
                child_elem.textContent(sb);
                if (sb->str && sb->length > 0) {
                    char* spec_str = (char*)arena_alloc(arena, sb->length + 1);
                    memcpy(spec_str, sb->str->chars, sb->length);
                    spec_str[sb->length] = '\0';
                    *col_spec_out = spec_str;
                }
                stringbuf_free(sb);
                pool_destroy(spec_pool);
                *found_col_spec = true;
                continue;
            }

            // Skip other curly_group
            if (tag_eq(tag, "curly_group")) {
                continue;
            }

            // Check for special items
            if (tag_eq(tag, "linebreak_command")) {
                TabularItem* item = (TabularItem*)arena_alloc(arena, sizeof(TabularItem));
                item->item = child;
                item->type = TABULAR_ROW_SEP;
                arraylist_append(items, item);
                continue;
            }

            if (tag_eq(tag, "hline")) {
                TabularItem* item = (TabularItem*)arena_alloc(arena, sizeof(TabularItem));
                item->item = child;
                item->type = TABULAR_HLINE;
                arraylist_append(items, item);
                continue;
            }

            // For paragraph, recurse into its children
            if (tag_eq(tag, "paragraph")) {
                collect_tabular_content(child_elem, items, found_col_spec, col_spec_out, arena);
                continue;
            }

            // For sequence, recurse into its children
            if (tag_eq(tag, "sequence")) {
                collect_tabular_content(child_elem, items, found_col_spec, col_spec_out, arena);
                continue;
            }

            // Add this element as content
            TabularItem* item = (TabularItem*)arena_alloc(arena, sizeof(TabularItem));
            item->item = child;
            item->type = TABULAR_CONTENT;
            arraylist_append(items, item);
        } else if (child.isString()) {
            const char* str = child.cstring();
            // Skip whitespace-only strings
            if (str) {
                bool all_whitespace = true;
                for (const char* p = str; *p; p++) {
                    if (!isspace(*p)) { all_whitespace = false; break; }
                }
                if (!all_whitespace) {
                    TabularItem* item = (TabularItem*)arena_alloc(arena, sizeof(TabularItem));
                    item->item = child;
                    item->type = TABULAR_CONTENT;
                    arraylist_append(items, item);
                }
            }
        } else if (child.isSymbol()) {
            // Check for alignment_tab symbol
            Symbol* sym_str = child.asSymbol();
            const char* sym = sym_str ? sym_str->chars : nullptr;
            if (sym && tag_eq(sym, "alignment_tab")) {
                TabularItem* item = (TabularItem*)arena_alloc(arena, sizeof(TabularItem));
                item->item = child;
                item->type = TABULAR_COL_SEP;
                arraylist_append(items, item);
            }
        }
    }
}

TexNode* convert_latex_tabular(const ElementReader& elem, LaTeXContext& ctx) {
    Arena* arena = ctx.doc_ctx.arena;

    // Collect all content items
    Pool* pool = pool_create();
    ArrayList* items = arraylist_new(32);
    bool found_col_spec = false;
    char* col_spec = nullptr;

    collect_tabular_content(elem, items, &found_col_spec, &col_spec, arena);

    TabularColSpec spec = parse_tabular_colspec(col_spec, arena);
    if (spec.num_cols == 0) {
        spec.num_cols = 3;  // Default
        spec.aligns = (ColAlign*)arena_alloc(arena, 3 * sizeof(ColAlign));
        spec.aligns[0] = spec.aligns[1] = spec.aligns[2] = ColAlign::Center;
        spec.left_borders = (bool*)arena_alloc(arena, 3 * sizeof(bool));
        spec.right_borders = (bool*)arena_alloc(arena, 3 * sizeof(bool));
    }

    VListContext vctx(arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    // Space above table
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    float total_width = ctx.doc_ctx.text_width;
    float col_width = total_width / spec.num_cols;

    // Process items, building rows
    // Items between row separators form a row
    TexNode* current_row = make_hlist(arena);
    int current_col = 0;

    for (int i = 0; i < items->length; i++) {
        TabularItem* tci = (TabularItem*)items->data[i];

        if (tci->type == TABULAR_HLINE) {
            // Emit current row if not empty, then draw hline
            if (current_row->first_child) {
                current_row->width = total_width;
                HListDimensions dims = measure_hlist(current_row);
                current_row->height = dims.height;
                current_row->depth = dims.depth;
                add_line(vctx, current_row);
                current_row = make_hlist(arena);
                current_col = 0;
            }
            TexNode* rule = make_rule(arena, total_width, 0.4f, 0);
            if (rule) add_line(vctx, rule);
            continue;
        }

        if (tci->type == TABULAR_ROW_SEP) {
            // Emit current row
            if (current_row->first_child) {
                current_row->width = total_width;
                HListDimensions dims = measure_hlist(current_row);
                current_row->height = dims.height;
                current_row->depth = dims.depth;
                add_line(vctx, current_row);
            }
            current_row = make_hlist(arena);
            current_col = 0;
            continue;
        }

        if (tci->type == TABULAR_COL_SEP) {
            // Move to next column
            current_col++;
            continue;
        }

        // Regular content - convert to tex nodes
        TexNode* content_nodes = convert_inline_item(tci->item, ctx, pool);
        if (content_nodes && content_nodes->first_child) {
            transfer_nodes(current_row, content_nodes);
        }
    }

    // Emit final row if not empty
    if (current_row->first_child) {
        current_row->width = total_width;
        HListDimensions dims = measure_hlist(current_row);
        current_row->height = dims.height;
        current_row->depth = dims.depth;
        add_line(vctx, current_row);
    }

    arraylist_free(items);
    pool_destroy(pool);

    // Space below table
    add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));

    return end_vlist(vctx);
}

// ============================================================================
// Display Math Conversion
// ============================================================================

TexNode* convert_latex_display_math(const ElementReader& elem, LaTeXContext& ctx) {
    MathContext math_ctx = ctx.doc_ctx.math_context();
    math_ctx.style = MathStyle::Display;

    // Use pre-parsed AST from input-latex-ts.cpp
    ItemReader ast_attr = elem.get_attr("ast");
    if (ast_attr.isNull() || !ast_attr.isElement()) {
        log_debug("latex_bridge: display math missing ast attribute");
        return nullptr;
    }

    TexNode* math_hbox = typeset_math_from_ast(ast_attr, math_ctx);
    if (!math_hbox) {
        return nullptr;
    }

    // Create centered display
    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    // Space above - use even higher shrink to compensate for our taller math
    // TeX's values: 10pt plus 2pt minus 5pt, but our math tends to be ~10pt taller per display
    // Need to allow shrinking to about 0pt to match TeX page breaking behavior
    add_vspace(vctx, Glue::flexible(10.0f, 2.0f, 10.0f));

    // Center the math
    TexNode* centered = center_line(math_hbox, ctx.doc_ctx.text_width, ctx.doc_ctx.arena);
    if (centered) {
        add_raw(vctx, centered);
    }

    // Space below - use same high shrink for balance
    add_vspace(vctx, Glue::flexible(10.0f, 2.0f, 10.0f));

    return end_vlist(vctx);
}

// ============================================================================
// Block Element Conversion
// ============================================================================

TexNode* convert_latex_block(const ElementReader& elem, LaTeXContext& ctx) {
    const char* tag = elem.tagName();
    if (!tag) return nullptr;

    // Skip preamble-only commands - these should not produce output
    if (tag_eq(tag, "documentclass") || tag_eq(tag, "pagestyle") ||
        tag_eq(tag, "usepackage") || tag_eq(tag, "title") ||
        tag_eq(tag, "author") || tag_eq(tag, "date") ||
        tag_eq(tag, "newcommand") || tag_eq(tag, "renewcommand") ||
        tag_eq(tag, "setlength") || tag_eq(tag, "setcounter") ||
        tag_eq(tag, "newenvironment") || tag_eq(tag, "renewenvironment") ||
        tag_eq(tag, "newtheorem") || tag_eq(tag, "DeclareMathOperator") ||
        tag_eq(tag, "bibliographystyle") || tag_eq(tag, "makeatletter") ||
        tag_eq(tag, "makeatother") || tag_eq(tag, "input") ||
        tag_eq(tag, "include") || tag_eq(tag, "includeonly")) {
        return nullptr;
    }

    // Section commands
    if (is_section_command(tag)) {
        return convert_latex_section(elem, get_section_level(tag), ctx);
    }

    // Paragraph
    if (tag_eq(tag, "paragraph") || tag_eq(tag, "para")) {
        fprintf(stderr, "[DEBUG] convert_latex_block: found paragraph, calling convert_latex_paragraph\n");
        return convert_latex_paragraph(elem, ctx);
    }

    // Environments
    if (tag_eq(tag, "generic_environment") || tag_eq(tag, "environment")) {
        const char* env_name = get_environment_name(elem);

        if (env_name) {
            if (tag_eq(env_name, "itemize")) {
                return convert_latex_list(elem, false, false, ctx);
            } else if (tag_eq(env_name, "enumerate")) {
                return convert_latex_list(elem, true, false, ctx);
            } else if (tag_eq(env_name, "description")) {
                return convert_latex_list(elem, false, true, ctx);
            } else if (tag_eq(env_name, "quote")) {
                return convert_latex_quote(elem, false, ctx);
            } else if (tag_eq(env_name, "quotation")) {
                return convert_latex_quote(elem, true, ctx);
            } else if (tag_eq(env_name, "verbatim")) {
                return convert_latex_verbatim(elem, ctx);
            } else if (tag_eq(env_name, "tabular")) {
                return convert_latex_tabular(elem, ctx);
            } else if (tag_eq(env_name, "center")) {
                return convert_latex_alignment(elem, 0, ctx);
            } else if (tag_eq(env_name, "flushleft")) {
                return convert_latex_alignment(elem, -1, ctx);
            } else if (tag_eq(env_name, "flushright")) {
                return convert_latex_alignment(elem, 1, ctx);
            } else if (is_math_environment(env_name)) {
                return convert_latex_display_math(elem, ctx);
            }
        }

        // Unknown environment - process children as blocks
        VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
        init_vlist_context(vctx, ctx.doc_ctx.text_width);
        begin_vlist(vctx);

        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                TexNode* block = convert_latex_block(child_elem, ctx);
                if (block) {
                    add_raw(vctx, block);
                }
            }
        }

        return end_vlist(vctx);
    }

    // Display math
    if (tag_eq(tag, "display_math") || tag_eq(tag, "displaymath") ||
        tag_eq(tag, "equation") || tag_eq(tag, "align")) {
        return convert_latex_display_math(elem, ctx);
    }

    // Inline math in block context - wrap as a centered display
    if (tag_eq(tag, "inline_math") || tag_eq(tag, "math")) {
        MathContext math_ctx = ctx.doc_ctx.math_context();
        math_ctx.style = MathStyle::Text;

        ItemReader ast_attr = elem.get_attr("ast");
        if (ast_attr.isNull() || !ast_attr.isElement()) {
            return nullptr;
        }

        TexNode* math_hbox = typeset_math_from_ast(ast_attr, math_ctx);
        if (!math_hbox) return nullptr;

        // Wrap in a paragraph line
        VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
        init_vlist_context(vctx, ctx.doc_ctx.text_width);
        begin_vlist(vctx);
        add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));
        TexNode* centered = center_line(math_hbox, ctx.doc_ctx.text_width, ctx.doc_ctx.arena);
        if (centered) add_raw(vctx, centered);
        add_vspace(vctx, Glue::flexible(6.0f, 2.0f, 1.0f));
        return end_vlist(vctx);
    }

    // Verbatim
    if (tag_eq(tag, "verbatim_environment") || tag_eq(tag, "verbatim")) {
        return convert_latex_verbatim(elem, ctx);
    }

    // Tabular (can appear directly as tag when parsed as named environment)
    if (tag_eq(tag, "tabular")) {
        fprintf(stderr, "[DEBUG] convert_latex_block: found tabular tag, calling convert_latex_tabular\n");
        return convert_latex_tabular(elem, ctx);
    }

    // Document structure
    if (tag_eq(tag, "document") || tag_eq(tag, "latex_document")) {
        fprintf(stderr, "[DEBUG] convert_latex_block: processing document element\n");
        // Process children
        VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
        init_vlist_context(vctx, ctx.doc_ctx.text_width);
        begin_vlist(vctx);

        ctx.in_preamble = false;  // After \begin{document}

        int child_idx = 0;
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            child_idx++;
            if (child.isElement()) {
                ElementReader ce = child.asElement();
                fprintf(stderr, "[DEBUG] document: child %d tag='%s'\n", child_idx, ce.tagName());
            } else if (child.isString()) {
                fprintf(stderr, "[DEBUG] document: child %d is string\n", child_idx);
            }
            TexNode* block = convert_latex_block(child, ctx);
            if (block) {
                fprintf(stderr, "[DEBUG] document: child %d produced block\n", child_idx);
                add_raw(vctx, block);
                if (ctx.doc_ctx.parskip > 0) {
                    add_vspace(vctx, Glue::fixed(ctx.doc_ctx.parskip));
                }
            }
        }

        return end_vlist(vctx);
    }

    // Skip preamble content - only document body should be typeset
    if (tag_eq(tag, "preamble")) {
        return nullptr;
    }

    // Handle ERROR recovery 'sequence' nodes - they may contain valid content
    if (tag_eq(tag, "sequence")) {
        fprintf(stderr, "[DEBUG] convert_latex_block: processing sequence (error recovery)\n");
        VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
        init_vlist_context(vctx, ctx.doc_ctx.text_width);
        begin_vlist(vctx);

        int child_count = 0;
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            child_count++;
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                fprintf(stderr, "[DEBUG] sequence: child %d tag='%s'\n", child_count, child_elem.tagName());
            } else if (child.isString()) {
                const char* s = child.cstring();
                fprintf(stderr, "[DEBUG] sequence: child %d string='%s'\n", child_count, s ? s : "(null)");
            } else {
                fprintf(stderr, "[DEBUG] sequence: child %d is other type\n", child_count);
            }
            TexNode* block = convert_latex_block(child, ctx);
            if (block) {
                add_raw(vctx, block);
            }
        }
        fprintf(stderr, "[DEBUG] sequence: total children = %d\n", child_count);

        return end_vlist(vctx);
    }

    // Generic container (div, span, etc.)
    if (tag_eq(tag, "curly_group") || tag_eq(tag, "brack_group") ||
        tag_eq(tag, "body")) {
        VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
        init_vlist_context(vctx, ctx.doc_ctx.text_width);
        begin_vlist(vctx);

        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            TexNode* block = convert_latex_block(child, ctx);
            if (block) {
                add_raw(vctx, block);
            }
        }

        return end_vlist(vctx);
    }

    // Default: treat as paragraph content
    Pool* pool = pool_create();
    TexNode* hlist = make_hlist(ctx.doc_ctx.arena);

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        TexNode* child_nodes = convert_inline_item(child, ctx, pool);
        if (child_nodes) {
            transfer_nodes(hlist, child_nodes);
        }
    }

    pool_destroy(pool);

    if (hlist && hlist->first_child) {
        return break_latex_paragraph(hlist, ctx);
    }

    return nullptr;
}

TexNode* convert_latex_block(const ItemReader& item, LaTeXContext& ctx) {
    if (item.isElement()) {
        return convert_latex_block(item.asElement(), ctx);
    }

    // String content - treat as paragraph
    if (item.isString()) {
        const char* str = item.cstring();
        if (str && strlen(str) > 0) {
            // Skip whitespace-only
            bool has_content = false;
            for (const char* p = str; *p; p++) {
                if (!isspace(*p)) {
                    has_content = true;
                    break;
                }
            }

            if (has_content) {
                TexNode* hlist = build_text_hlist(str, strlen(str), ctx);
                if (hlist && hlist->first_child) {
                    return break_latex_paragraph(hlist, ctx);
                }
            }
        }
    }

    return nullptr;
}

// ============================================================================
// Alignment Environment Conversion
// ============================================================================

TexNode* convert_latex_alignment(const ElementReader& elem, int alignment, LaTeXContext& ctx) {
    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();

            // Skip begin_env/end_env
            if (tag_eq(tag, "begin_env") || tag_eq(tag, "end_env")) continue;

            // Process as paragraph
            TexNode* block = convert_latex_block(child_elem, ctx);
            if (block) {
                // TODO: Apply alignment
                add_raw(vctx, block);
            }
        } else if (child.isString()) {
            const char* str = child.cstring();
            if (str && strlen(str) > 0) {
                bool has_content = false;
                for (const char* p = str; *p; p++) {
                    if (!isspace(*p)) {
                        has_content = true;
                        break;
                    }
                }

                if (has_content) {
                    TexNode* hlist = build_text_hlist(str, strlen(str), ctx);
                    if (hlist && hlist->first_child) {
                        // Apply alignment
                        TexNode* line = nullptr;
                        if (alignment == 0) {
                            line = center_line(hlist, ctx.doc_ctx.text_width, ctx.doc_ctx.arena);
                        } else if (alignment == 1) {
                            line = right_align_line(hlist, ctx.doc_ctx.text_width, ctx.doc_ctx.arena);
                        } else {
                            HListDimensions dims = measure_hlist(hlist);
                            line = hlist_to_hbox(hlist, dims.width, ctx.doc_ctx.arena);
                        }

                        if (line) {
                            add_line(vctx, line);
                        }
                    }
                }
            }
        }
    }

    return end_vlist(vctx);
}

// ============================================================================
// Main Document Typesetting API
// ============================================================================

TexNode* typeset_latex_document(Item latex_root, LaTeXContext& ctx) {
    log_info("latex_bridge: typeset_latex_document(Item) called");
    if (latex_root.item == ItemNull.item) {
        log_error("latex_bridge: null latex_root");
        return make_vlist(ctx.doc_ctx.arena);
    }

    TypeId type = get_type_id(latex_root);
    if (type != LMD_TYPE_ELEMENT) {
        log_error("latex_bridge: document root must be an Element (got type=%d)", (int)type);
        return make_vlist(ctx.doc_ctx.arena);
    }

    ElementReader root(latex_root.element);
    return typeset_latex_document(root, ctx);
}

TexNode* typeset_latex_document(const ElementReader& latex_root, LaTeXContext& ctx) {
    fprintf(stderr, "[DEBUG] typeset_latex_document: root tag='%s'\n", latex_root.tagName());

    // Create main VList for document
    VListContext vctx(ctx.doc_ctx.arena, ctx.doc_ctx.fonts);
    init_vlist_context(vctx, ctx.doc_ctx.text_width);
    begin_vlist(vctx);

    // Process all children of root element
    int child_count = 0;
    auto iter = latex_root.children();
    ItemReader child;
    while (iter.next(&child)) {
        child_count++;
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            fprintf(stderr, "[DEBUG] typeset_latex_document: processing child %d, tag='%s'\n",
                    child_count, child_elem.tagName());
        }
        TexNode* block = convert_latex_block(child, ctx);
        if (block) {
            fprintf(stderr, "[DEBUG] typeset_latex_document: got block class=%d (VList=%d)\n",
                    (int)block->node_class, (int)NodeClass::VList);
            // If block is a VList, flatten its children into the document recursively
            // This ensures page breaks can happen between display math blocks
            if (block->node_class == NodeClass::VList && block->first_child) {
                fprintf(stderr, "[DEBUG] typeset_latex_document: flattening VList with children\n");
                TexNode* child_node = block->first_child;
                int flat_count = 0;
                while (child_node) {
                    flat_count++;
                    TexNode* next = child_node->next_sibling;
                    // Detach from old VList
                    child_node->prev_sibling = nullptr;
                    child_node->next_sibling = nullptr;
                    child_node->parent = nullptr;

                    // If this child is also a VList, flatten it recursively
                    if (child_node->node_class == NodeClass::VList && child_node->first_child) {
                        fprintf(stderr, "[DEBUG] typeset_latex_document: recursively flattening nested VList\n");
                        TexNode* nested = child_node->first_child;
                        while (nested) {
                            TexNode* nnext = nested->next_sibling;
                            nested->prev_sibling = nullptr;
                            nested->next_sibling = nullptr;
                            nested->parent = nullptr;
                            add_raw(vctx, nested);
                            nested = nnext;
                        }
                    } else {
                        add_raw(vctx, child_node);
                    }
                    child_node = next;
                }
                fprintf(stderr, "[DEBUG] typeset_latex_document: flattened %d children\n", flat_count);
            } else {
                add_raw(vctx, block);
            }
            if (ctx.doc_ctx.parskip > 0) {
                add_vspace(vctx, Glue::fixed(ctx.doc_ctx.parskip));
            }
        }
    }

    return end_vlist(vctx);
}

// ============================================================================
// Page Breaking
// ============================================================================

PageList break_latex_into_pages(TexNode* document, LaTeXContext& ctx) {
    return break_into_pages(document, ctx.doc_ctx);
}

} // namespace tex
