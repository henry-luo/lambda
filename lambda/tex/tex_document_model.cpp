// tex_document_model.cpp - Intermediate Document Model for Unified Pipeline
//
// Implementation of the document model layer that bridges LaTeX AST to
// multiple output formats (HTML, DVI, SVG, PDF).

#include "tex_document_model.hpp"
#include "tex_math_ts.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdio>

// Include input system for doc_model_from_string
#ifndef DOC_MODEL_MINIMAL
#include "../input/input.hpp"
extern "C" void parse_latex_ts(Input* input, const char* latex_string);
#endif

// Conditionally include SVG support
#ifndef DOC_MODEL_NO_SVG
#include "tex_svg_out.hpp"
#endif

namespace tex {

// ============================================================================
// SVG Stub (when SVG support is disabled)
// ============================================================================

#ifdef DOC_MODEL_NO_SVG
// Stub function when SVG library is not linked
static const char* svg_render_math_inline(TexNode*, Arena*, void*) {
    return nullptr;
}
struct SVGParams {
    bool indent;
    static SVGParams defaults() { return {false}; }
};
#endif

// ============================================================================
// Debug Utilities
// ============================================================================

const char* doc_elem_type_name(DocElemType type) {
    switch (type) {
    case DocElemType::PARAGRAPH:    return "PARAGRAPH";
    case DocElemType::HEADING:      return "HEADING";
    case DocElemType::LIST:         return "LIST";
    case DocElemType::LIST_ITEM:    return "LIST_ITEM";
    case DocElemType::TABLE:        return "TABLE";
    case DocElemType::TABLE_ROW:    return "TABLE_ROW";
    case DocElemType::TABLE_CELL:   return "TABLE_CELL";
    case DocElemType::FIGURE:       return "FIGURE";
    case DocElemType::BLOCKQUOTE:   return "BLOCKQUOTE";
    case DocElemType::CODE_BLOCK:   return "CODE_BLOCK";
    case DocElemType::MATH_INLINE:  return "MATH_INLINE";
    case DocElemType::MATH_DISPLAY: return "MATH_DISPLAY";
    case DocElemType::MATH_EQUATION: return "MATH_EQUATION";
    case DocElemType::MATH_ALIGN:   return "MATH_ALIGN";
    case DocElemType::TEXT_SPAN:    return "TEXT_SPAN";
    case DocElemType::TEXT_RUN:     return "TEXT_RUN";
    case DocElemType::LINK:         return "LINK";
    case DocElemType::IMAGE:        return "IMAGE";
    case DocElemType::FOOTNOTE:     return "FOOTNOTE";
    case DocElemType::CITATION:     return "CITATION";
    case DocElemType::CROSS_REF:    return "CROSS_REF";
    case DocElemType::DOCUMENT:     return "DOCUMENT";
    case DocElemType::SECTION:      return "SECTION";
    case DocElemType::ABSTRACT:     return "ABSTRACT";
    case DocElemType::TITLE_BLOCK:  return "TITLE_BLOCK";
    case DocElemType::RAW_HTML:     return "RAW_HTML";
    case DocElemType::RAW_LATEX:    return "RAW_LATEX";
    case DocElemType::SPACE:        return "SPACE";
    case DocElemType::ERROR:        return "ERROR";
    default:                        return "UNKNOWN";
    }
}

// ============================================================================
// Forward Declarations
// ============================================================================

// Forward declaration needed for build_alignment_environment
static void build_body_content_with_paragraphs(DocElement* container, const ElementReader& elem,
                                                Arena* arena, TexDocumentModel* doc);

// ============================================================================
// Document Model Methods
// ============================================================================

void TexDocumentModel::add_label(const char* label, const char* ref_text, int page) {
    if (label_count >= label_capacity) {
        int new_capacity = label_capacity == 0 ? 16 : label_capacity * 2;
        LabelEntry* new_labels = (LabelEntry*)arena_alloc(arena, new_capacity * sizeof(LabelEntry));
        if (labels) {
            memcpy(new_labels, labels, label_count * sizeof(LabelEntry));
        }
        labels = new_labels;
        label_capacity = new_capacity;
    }
    
    labels[label_count].label = label;
    labels[label_count].ref_text = ref_text;
    labels[label_count].page = page;
    label_count++;
}

const char* TexDocumentModel::resolve_ref(const char* label) const {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].label, label) == 0) {
            return labels[i].ref_text;
        }
    }
    return "??";  // Unresolved reference marker
}

void TexDocumentModel::add_macro(const char* name, int num_args, const char* replacement) {
    if (macro_count >= macro_capacity) {
        int new_capacity = macro_capacity == 0 ? 16 : macro_capacity * 2;
        MacroDef* new_macros = (MacroDef*)arena_alloc(arena, new_capacity * sizeof(MacroDef));
        if (macros) {
            memcpy(new_macros, macros, macro_count * sizeof(MacroDef));
        }
        macros = new_macros;
        macro_capacity = new_capacity;
    }
    
    macros[macro_count].name = name;
    macros[macro_count].num_args = num_args;
    macros[macro_count].replacement = replacement;
    macro_count++;
}

const TexDocumentModel::MacroDef* TexDocumentModel::find_macro(const char* name) const {
    for (int i = 0; i < macro_count; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            return &macros[i];
        }
    }
    return nullptr;
}

void TexDocumentModel::add_bib_entry(const char* key, const char* formatted) {
    if (bib_count >= bib_capacity) {
        int new_capacity = bib_capacity == 0 ? 16 : bib_capacity * 2;
        BibEntry* new_entries = (BibEntry*)arena_alloc(arena, new_capacity * sizeof(BibEntry));
        if (bib_entries) {
            memcpy(new_entries, bib_entries, bib_count * sizeof(BibEntry));
        }
        bib_entries = new_entries;
        bib_capacity = new_capacity;
    }
    
    bib_entries[bib_count].key = key;
    bib_entries[bib_count].formatted = formatted;
    bib_count++;
}

const char* TexDocumentModel::resolve_cite(const char* key) const {
    for (int i = 0; i < bib_count; i++) {
        if (strcmp(bib_entries[i].key, key) == 0) {
            return bib_entries[i].formatted;
        }
    }
    return "[?]";  // Unresolved citation marker
}

// ============================================================================
// Element Allocation
// ============================================================================

TexDocumentModel* doc_model_create(Arena* arena) {
    TexDocumentModel* doc = (TexDocumentModel*)arena_alloc(arena, sizeof(TexDocumentModel));
    memset(doc, 0, sizeof(TexDocumentModel));
    doc->arena = arena;
    doc->document_class = "article";  // Default
    return doc;
}

DocElement* doc_alloc_element(Arena* arena, DocElemType type) {
    DocElement* elem = (DocElement*)arena_alloc(arena, sizeof(DocElement));
    memset(elem, 0, sizeof(DocElement));
    elem->type = type;
    return elem;
}

void doc_append_child(DocElement* parent, DocElement* child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    child->next_sibling = nullptr;
    
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        parent->last_child = child;
    }
}

void doc_insert_before(DocElement* parent, DocElement* before, DocElement* child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    
    if (!before || parent->first_child == before) {
        // Insert at beginning
        child->next_sibling = parent->first_child;
        parent->first_child = child;
        if (!parent->last_child) {
            parent->last_child = child;
        }
    } else {
        // Find the element before 'before'
        DocElement* prev = parent->first_child;
        while (prev && prev->next_sibling != before) {
            prev = prev->next_sibling;
        }
        if (prev) {
            child->next_sibling = before;
            prev->next_sibling = child;
        }
    }
}

void doc_remove_child(DocElement* parent, DocElement* child) {
    if (!parent || !child || child->parent != parent) return;
    
    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
        if (parent->last_child == child) {
            parent->last_child = nullptr;
        }
    } else {
        DocElement* prev = parent->first_child;
        while (prev && prev->next_sibling != child) {
            prev = prev->next_sibling;
        }
        if (prev) {
            prev->next_sibling = child->next_sibling;
            if (parent->last_child == child) {
                parent->last_child = prev;
            }
        }
    }
    
    child->parent = nullptr;
    child->next_sibling = nullptr;
}

DocElement* doc_create_text(Arena* arena, const char* text, size_t len, DocTextStyle style) {
    DocElement* elem = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    
    // Copy text to arena
    char* text_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(text_copy, text, len);
    text_copy[len] = '\0';
    
    elem->text.text = text_copy;
    elem->text.text_len = len;
    elem->text.style = style;
    
    return elem;
}

DocElement* doc_create_text_cstr(Arena* arena, const char* text, DocTextStyle style) {
    return doc_create_text(arena, text, strlen(text), style);
}

// Normalize LaTeX whitespace: collapse consecutive whitespace to single space
// This preserves leading and trailing whitespace (single space at most) 
// since inter-element spacing is meaningful in inline context.
// Returns the normalized string allocated in arena, or nullptr if result is empty
static const char* normalize_latex_whitespace(const char* text, Arena* arena) {
    if (!text) return nullptr;
    
    size_t len = strlen(text);
    if (len == 0) return nullptr;
    
    // Allocate buffer (can't be larger than original)
    char* buf = (char*)arena_alloc(arena, len + 1);
    char* out = buf;
    
    bool in_whitespace = false;
    
    for (const char* p = text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            // Collapse consecutive whitespace to single space
            if (!in_whitespace) {
                *out++ = ' ';
                in_whitespace = true;
            }
        } else {
            *out++ = *p;
            in_whitespace = false;
        }
    }
    
    *out = '\0';
    
    size_t result_len = out - buf;
    if (result_len == 0) return nullptr;
    
    return buf;
}

// Create text element with normalized whitespace
static DocElement* doc_create_text_normalized(Arena* arena, const char* text, DocTextStyle style) {
    const char* normalized = normalize_latex_whitespace(text, arena);
    if (!normalized) return nullptr;
    return doc_create_text_cstr(arena, normalized, style);
}

// ============================================================================
// LaTeX Text Transformations
// ============================================================================

// Transform LaTeX text to typographic text with proper:
// - Dash ligatures: --- → em-dash (—), -- → en-dash (–), - → hyphen (‐)
// - Quote ligatures: `` → ", '' → ", ` → ', ' → '
// - Standard ligatures: fi → ﬁ, fl → ﬂ, ff → ﬀ, ffi → ﬃ, ffl → ﬄ
//
// If in_monospace is true, skip all conversions (keep literal ASCII).
// Returns a dynamically allocated string that must be freed by caller.
static char* transform_latex_text(const char* text, size_t len, bool in_monospace) {
    if (!text || len == 0) return nullptr;
    
    // Allocate buffer with room for UTF-8 expansion (3x worst case)
    size_t buf_size = len * 4 + 1;
    char* result = (char*)malloc(buf_size);
    if (!result) return nullptr;
    
    size_t out_pos = 0;
    
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        
        // Ensure we have room (conservative check)
        if (out_pos + 8 >= buf_size) {
            buf_size *= 2;
            char* new_buf = (char*)realloc(result, buf_size);
            if (!new_buf) { free(result); return nullptr; }
            result = new_buf;
        }
        
        if (in_monospace) {
            // In monospace mode, keep all characters as literal ASCII
            result[out_pos++] = c;
            continue;
        }
        
        // Check for dash ligatures
        if (c == '-') {
            // Check for --- (em-dash)
            if (i + 2 < len && text[i+1] == '-' && text[i+2] == '-') {
                // — (U+2014 = em-dash) = E2 80 94
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x94';
                i += 2;  // Skip two more hyphens
                continue;
            }
            // Check for -- (en-dash)
            if (i + 1 < len && text[i+1] == '-') {
                // – (U+2013 = en-dash) = E2 80 93
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x93';
                i += 1;  // Skip one more hyphen
                continue;
            }
            // Single hyphen → typographic hyphen (U+2010)
            // ‐ = E2 80 90
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x90';
            continue;
        }
        
        // Check for quote ligatures
        if (c == '`') {
            // Check for `` (opening double quote)
            if (i + 1 < len && text[i+1] == '`') {
                // " (U+201C) = E2 80 9C
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x9C';
                i += 1;
                continue;
            }
            // Single backtick → opening single quote
            // ' (U+2018) = E2 80 98
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x98';
            continue;
        }
        
        if (c == '\'') {
            // Check for '' (closing double quote)
            if (i + 1 < len && text[i+1] == '\'') {
                // " (U+201D) = E2 80 9D
                result[out_pos++] = '\xE2';
                result[out_pos++] = '\x80';
                result[out_pos++] = '\x9D';
                i += 1;
                continue;
            }
            // Single apostrophe → closing single quote / apostrophe
            // ' (U+2019) = E2 80 99
            result[out_pos++] = '\xE2';
            result[out_pos++] = '\x80';
            result[out_pos++] = '\x99';
            continue;
        }
        
        // Check for f-ligatures
        if (c == 'f') {
            // Check for ffi
            if (i + 2 < len && text[i+1] == 'f' && text[i+2] == 'i') {
                // ﬃ (U+FB03) = EF AC 83
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x83';
                i += 2;
                continue;
            }
            // Check for ffl
            if (i + 2 < len && text[i+1] == 'f' && text[i+2] == 'l') {
                // ﬄ (U+FB04) = EF AC 84
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x84';
                i += 2;
                continue;
            }
            // Check for ff
            if (i + 1 < len && text[i+1] == 'f') {
                // ﬀ (U+FB00) = EF AC 80
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x80';
                i += 1;
                continue;
            }
            // Check for fi
            if (i + 1 < len && text[i+1] == 'i') {
                // ﬁ (U+FB01) = EF AC 81
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x81';
                i += 1;
                continue;
            }
            // Check for fl
            if (i + 1 < len && text[i+1] == 'l') {
                // ﬂ (U+FB02) = EF AC 82
                result[out_pos++] = '\xEF';
                result[out_pos++] = '\xAC';
                result[out_pos++] = '\x82';
                i += 1;
                continue;
            }
        }
        
        // Default: copy character as-is
        result[out_pos++] = c;
    }
    
    result[out_pos] = '\0';
    return result;
}

// HTML escape and append text with LaTeX transformations
// Handles dash ligatures, quote ligatures, and f-ligatures
static void html_escape_append_transformed(StrBuf* out, const char* text, size_t len, bool in_monospace) {
    if (!text || len == 0) return;
    
    char* transformed = transform_latex_text(text, len, in_monospace);
    if (transformed) {
        // HTML escape the transformed text
        for (const char* p = transformed; *p; p++) {
            char c = *p;
            switch (c) {
            case '&':  strbuf_append_str(out, "&amp;"); break;
            case '<':  strbuf_append_str(out, "&lt;"); break;
            case '>':  strbuf_append_str(out, "&gt;"); break;
            case '"':  strbuf_append_str(out, "&quot;"); break;
            // Note: don't escape single quotes - we want the curly ones to show
            default:   strbuf_append_char(out, c); break;
            }
        }
        free(transformed);
    }
}

// ============================================================================
// HTML Utilities
// ============================================================================

void html_escape_append(StrBuf* out, const char* text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        switch (c) {
        case '&':  strbuf_append_str(out, "&amp;"); break;
        case '<':  strbuf_append_str(out, "&lt;"); break;
        case '>':  strbuf_append_str(out, "&gt;"); break;
        case '"':  strbuf_append_str(out, "&quot;"); break;
        case '\'': strbuf_append_str(out, "&#39;"); break;
        default:   strbuf_append_char(out, c); break;
        }
    }
}

void html_indent(StrBuf* out, int depth) {
    for (int i = 0; i < depth; i++) {
        strbuf_append_str(out, "  ");
    }
}

void html_write_default_css(StrBuf* out, const char* prefix) {
    strbuf_append_str(out, "<style>\n");
    
    // Document container
    strbuf_append_format(out, ".%sdocument {\n", prefix);
    strbuf_append_str(out, "  max-width: 800px;\n");
    strbuf_append_str(out, "  margin: 0 auto;\n");
    strbuf_append_str(out, "  padding: 2em;\n");
    strbuf_append_str(out, "  font-family: 'Computer Modern Serif', 'Latin Modern Roman', Georgia, serif;\n");
    strbuf_append_str(out, "  font-size: 12pt;\n");
    strbuf_append_str(out, "  line-height: 1.5;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Headings
    for (int level = 0; level < 6; level++) {
        float sizes[] = {2.0f, 1.7f, 1.4f, 1.2f, 1.1f, 1.0f};
        strbuf_append_format(out, ".%sheading-%d {\n", prefix, level);
        strbuf_append_format(out, "  font-size: %.1fem;\n", sizes[level]);
        strbuf_append_str(out, "  font-weight: bold;\n");
        strbuf_append_format(out, "  margin-top: %.1fem;\n", level == 0 ? 1.5f : 1.2f);
        strbuf_append_format(out, "  margin-bottom: %.1fem;\n", 0.5f);
        strbuf_append_str(out, "}\n\n");
    }
    
    // Section numbers
    strbuf_append_format(out, ".%ssection-number {\n", prefix);
    strbuf_append_str(out, "  margin-right: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Paragraphs
    strbuf_append_format(out, ".%sparagraph {\n", prefix);
    strbuf_append_str(out, "  margin: 1em 0;\n");
    strbuf_append_str(out, "  text-align: justify;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Math
    strbuf_append_format(out, ".%smath-inline {\n", prefix);
    strbuf_append_str(out, "  display: inline-block;\n");
    strbuf_append_str(out, "  vertical-align: middle;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%smath-inline svg {\n", prefix);
    strbuf_append_str(out, "  display: inline-block;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%smath-display {\n", prefix);
    strbuf_append_str(out, "  display: block;\n");
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin: 1em 0;\n");
    strbuf_append_str(out, "  position: relative;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%smath-display svg {\n", prefix);
    strbuf_append_str(out, "  display: inline-block;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%seq-number {\n", prefix);
    strbuf_append_str(out, "  position: absolute;\n");
    strbuf_append_str(out, "  right: 0;\n");
    strbuf_append_str(out, "  top: 50%;\n");
    strbuf_append_str(out, "  transform: translateY(-50%);\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%smath-fallback {\n", prefix);
    strbuf_append_str(out, "  font-family: 'CMU Serif', serif;\n");
    strbuf_append_str(out, "  font-style: italic;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Lists
    strbuf_append_format(out, ".%slist {\n", prefix);
    strbuf_append_str(out, "  margin: 0.5em 0;\n");
    strbuf_append_str(out, "  padding-left: 2em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Tables
    strbuf_append_format(out, ".%stable {\n", prefix);
    strbuf_append_str(out, "  border-collapse: collapse;\n");
    strbuf_append_str(out, "  margin: 1em auto;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%stable td, .%stable th {\n", prefix, prefix);
    strbuf_append_str(out, "  padding: 0.3em 0.6em;\n");
    strbuf_append_str(out, "  border: 1px solid #ccc;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Text styling
    strbuf_append_format(out, ".%ssmallcaps {\n", prefix);
    strbuf_append_str(out, "  font-variant: small-caps;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Blockquote
    strbuf_append_format(out, ".%sblockquote {\n", prefix);
    strbuf_append_str(out, "  margin: 1em 2em;\n");
    strbuf_append_str(out, "  font-style: italic;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Code
    strbuf_append_format(out, ".%scode-block {\n", prefix);
    strbuf_append_str(out, "  font-family: 'Computer Modern Typewriter', monospace;\n");
    strbuf_append_str(out, "  background: #f5f5f5;\n");
    strbuf_append_str(out, "  padding: 1em;\n");
    strbuf_append_str(out, "  overflow-x: auto;\n");
    strbuf_append_str(out, "  white-space: pre;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Figure
    strbuf_append_format(out, ".%sfigure {\n", prefix);
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin: 1em 0;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sfigcaption {\n", prefix);
    strbuf_append_str(out, "  font-style: italic;\n");
    strbuf_append_str(out, "  margin-top: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Abstract
    strbuf_append_format(out, ".%sabstract {\n", prefix);
    strbuf_append_str(out, "  margin: 2em 3em;\n");
    strbuf_append_str(out, "  font-size: 0.9em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sabstract-title {\n", prefix);
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  font-weight: bold;\n");
    strbuf_append_str(out, "  margin-bottom: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    // Title block
    strbuf_append_format(out, ".%stitle-block {\n", prefix);
    strbuf_append_str(out, "  text-align: center;\n");
    strbuf_append_str(out, "  margin-bottom: 2em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-title {\n", prefix);
    strbuf_append_str(out, "  font-size: 1.8em;\n");
    strbuf_append_str(out, "  font-weight: bold;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-author {\n", prefix);
    strbuf_append_str(out, "  font-size: 1.2em;\n");
    strbuf_append_str(out, "  margin-top: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_format(out, ".%sdoc-date {\n", prefix);
    strbuf_append_str(out, "  margin-top: 0.5em;\n");
    strbuf_append_str(out, "}\n\n");
    
    strbuf_append_str(out, "</style>\n");
}

// ============================================================================
// HTML Element Rendering
// ============================================================================

// Forward declarations for mutual recursion
static void render_children_html(DocElement* parent, StrBuf* out, 
                                  const HtmlOutputOptions& opts, int depth);

static void render_text_span_html(DocElement* elem, StrBuf* out, 
                                   const HtmlOutputOptions& opts) {
    DocTextStyle& style = elem->text.style;
    
    // Opening tags
    if (opts.legacy_mode) {
        // Legacy mode: use span classes
        // Font size (must be outermost)
        const char* size_class = font_size_name_class(style.font_size_name);
        if (size_class) {
            strbuf_append_format(out, "<span class=\"%s\">", size_class);
        }
        if (style.has(DocTextStyle::BOLD))
            strbuf_append_str(out, "<span class=\"bf\">");
        if (style.has(DocTextStyle::ITALIC))
            strbuf_append_str(out, "<span class=\"it\">");
        if (style.has(DocTextStyle::MONOSPACE))
            strbuf_append_str(out, "<span class=\"tt\">");
        if (style.has(DocTextStyle::UNDERLINE))
            strbuf_append_str(out, "<span class=\"underline\">");
        if (style.has(DocTextStyle::STRIKEOUT))
            strbuf_append_str(out, "<span class=\"sout\">");
        if (style.has(DocTextStyle::SMALLCAPS))
            strbuf_append_str(out, "<span class=\"sc\">");
        if (style.has(DocTextStyle::SUPERSCRIPT))
            strbuf_append_str(out, "<sup>");
        if (style.has(DocTextStyle::SUBSCRIPT))
            strbuf_append_str(out, "<sub>");
    } else {
        // Modern mode: use semantic HTML tags
        if (style.has(DocTextStyle::BOLD))
            strbuf_append_str(out, "<strong>");
        if (style.has(DocTextStyle::ITALIC))
            strbuf_append_str(out, "<em>");
        if (style.has(DocTextStyle::MONOSPACE))
            strbuf_append_str(out, "<code>");
        if (style.has(DocTextStyle::UNDERLINE))
            strbuf_append_str(out, "<u>");
        if (style.has(DocTextStyle::STRIKEOUT))
            strbuf_append_str(out, "<s>");
        if (style.has(DocTextStyle::SMALLCAPS))
            strbuf_append_format(out, "<span class=\"%ssmallcaps\">", opts.css_class_prefix);
        if (style.has(DocTextStyle::SUPERSCRIPT))
            strbuf_append_str(out, "<sup>");
        if (style.has(DocTextStyle::SUBSCRIPT))
            strbuf_append_str(out, "<sub>");
        // Font size in modern mode - use class
        const char* size_class = font_size_name_class(style.font_size_name);
        if (size_class) {
            strbuf_append_format(out, "<span class=\"%s%s\">", opts.css_class_prefix, size_class);
        }
    }
    
    // Content
    if (elem->text.text && elem->text.text_len > 0) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    
    // Recurse to children
    render_children_html(elem, out, opts, 0);
    
    // Closing tags (reverse order)
    if (opts.legacy_mode) {
        // Legacy mode: close span tags
        if (style.has(DocTextStyle::SUBSCRIPT))
            strbuf_append_str(out, "</sub>");
        if (style.has(DocTextStyle::SUPERSCRIPT))
            strbuf_append_str(out, "</sup>");
        if (style.has(DocTextStyle::SMALLCAPS))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::STRIKEOUT))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::UNDERLINE))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::MONOSPACE))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::ITALIC))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::BOLD))
            strbuf_append_str(out, "</span>");
        // Close font size (outermost)
        if (style.font_size_name != FontSizeName::INHERIT)
            strbuf_append_str(out, "</span>");
    } else {
        // Modern mode: close semantic tags
        // Close font size first (innermost in modern mode)
        if (style.font_size_name != FontSizeName::INHERIT)
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::SUBSCRIPT))
            strbuf_append_str(out, "</sub>");
        if (style.has(DocTextStyle::SUPERSCRIPT))
            strbuf_append_str(out, "</sup>");
        if (style.has(DocTextStyle::SMALLCAPS))
            strbuf_append_str(out, "</span>");
        if (style.has(DocTextStyle::STRIKEOUT))
            strbuf_append_str(out, "</s>");
        if (style.has(DocTextStyle::UNDERLINE))
            strbuf_append_str(out, "</u>");
        if (style.has(DocTextStyle::MONOSPACE))
            strbuf_append_str(out, "</code>");
        if (style.has(DocTextStyle::ITALIC))
            strbuf_append_str(out, "</em>");
        if (style.has(DocTextStyle::BOLD))
            strbuf_append_str(out, "</strong>");
    }
}

static void render_heading_html(DocElement* elem, StrBuf* out,
                                 const HtmlOutputOptions& opts, int depth) {
    // Map level to HTML heading
    // Legacy mode: chapter->h1, section->h2, etc.
    // Modern mode: part(0)->h1, chapter(1)->h2, etc.
    int h_level;
    if (opts.legacy_mode) {
        // Legacy mapping: chapter=h1, section=h2, subsection=h3, etc.
        // level 1 (chapter) -> h1, level 2 (section) -> h2
        h_level = elem->heading.level;
        if (h_level < 1) h_level = 1;  // part->h1
        if (h_level > 6) h_level = 6;
    } else {
        h_level = elem->heading.level + 1;  // level 0 (part) -> h1
        if (h_level > 6) h_level = 6;
    }
    
    if (opts.pretty_print) html_indent(out, depth);
    
    if (opts.legacy_mode) {
        // Legacy mode: <h1 id="sec-N">
        if (elem->heading.label) {
            strbuf_append_format(out, "<h%d id=\"%s\">", h_level, elem->heading.label);
        } else {
            strbuf_append_format(out, "<h%d>", h_level);
        }
        
        // Chapter number in a div
        if (elem->heading.number && !(elem->flags & DocElement::FLAG_STARRED)) {
            if (elem->heading.level == 1) {
                // Chapter: <div>Chapter N</div>
                strbuf_append_format(out, "<div>Chapter %s</div>", elem->heading.number);
            } else {
                // Section: number before title with quad space
                strbuf_append_format(out, "%s ", elem->heading.number);
            }
        }
    } else {
        // Modern mode: <h1 class="latex-heading-0">
        if (elem->heading.label) {
            strbuf_append_format(out, "<h%d id=\"%s\" class=\"%sheading-%d\">", 
                h_level, elem->heading.label, opts.css_class_prefix, elem->heading.level);
        } else {
            strbuf_append_format(out, "<h%d class=\"%sheading-%d\">", 
                h_level, opts.css_class_prefix, elem->heading.level);
        }
        
        // Number if present
        if (elem->heading.number && !(elem->flags & DocElement::FLAG_STARRED)) {
            strbuf_append_format(out, "<span class=\"%ssection-number\">%s</span>",
                opts.css_class_prefix, elem->heading.number);
        }
    }
    
    // Title
    if (elem->heading.title) {
        html_escape_append(out, elem->heading.title, strlen(elem->heading.title));
    }
    
    strbuf_append_format(out, "</h%d>", h_level);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_paragraph_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    if (opts.legacy_mode) {
        // In legacy mode, add class="continue" if flag is set
        if (elem->flags & DocElement::FLAG_CONTINUE) {
            strbuf_append_str(out, "<p class=\"continue\">");
        } else {
            strbuf_append_str(out, "<p>");
        }
    } else {
        if (elem->flags & DocElement::FLAG_CONTINUE) {
            strbuf_append_format(out, "<p class=\"%sparagraph continue\">", opts.css_class_prefix);
        } else {
            strbuf_append_format(out, "<p class=\"%sparagraph\">", opts.css_class_prefix);
        }
    }
    
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</p>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_list_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts, int depth) {
    const char* tag;
    switch (elem->list.list_type) {
    case ListType::ITEMIZE:     tag = "ul"; break;
    case ListType::ENUMERATE:   tag = "ol"; break;
    case ListType::DESCRIPTION: tag = "dl"; break;
    default:                    tag = "ul"; break;
    }
    
    if (opts.pretty_print) html_indent(out, depth);
    
    if (opts.legacy_mode) {
        strbuf_append_format(out, "<%s class=\"list\">", tag);
    } else {
        strbuf_append_format(out, "<%s class=\"%slist\">", tag, opts.css_class_prefix);
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "</%s>", tag);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_list_item_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth,
                                   ListType parent_type) {
    if (opts.pretty_print) html_indent(out, depth);
    
    if (parent_type == ListType::DESCRIPTION) {
        // Description list: <dt>term</dt><dd>content</dd>
        if (elem->list_item.label) {
            strbuf_append_str(out, "<dt>");
            html_escape_append(out, elem->list_item.label, strlen(elem->list_item.label));
            strbuf_append_str(out, "</dt>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            if (opts.pretty_print) html_indent(out, depth);
        }
        strbuf_append_str(out, "<dd>");
    } else {
        strbuf_append_str(out, "<li>");
        
        // In legacy mode, add item label (bullet or number)
        if (opts.legacy_mode) {
            strbuf_append_str(out, "<span class=\"itemlabel\">");
            if (parent_type == ListType::ITEMIZE) {
                // Bullet: • in a span with left margin
                strbuf_append_str(out, "<span class=\"hbox llap\">\xE2\x80\xA2</span>");  // •
            } else if (parent_type == ListType::ENUMERATE && elem->list_item.item_number > 0) {
                // Number: (number) in a span
                strbuf_append_format(out, "<span class=\"hbox llap\">%d.</span>", elem->list_item.item_number);
            }
            strbuf_append_str(out, "</span>");
        }
    }
    
    // Wrap children in <p> tags for legacy mode if they're text content
    if (opts.legacy_mode && parent_type != ListType::DESCRIPTION) {
        // Render children wrapped in <p>
        strbuf_append_str(out, "<p>");
        render_children_html(elem, out, opts, depth + 1);
        strbuf_append_str(out, "</p>");
    } else {
        render_children_html(elem, out, opts, depth + 1);
    }
    
    if (parent_type == ListType::DESCRIPTION) {
        strbuf_append_str(out, "</dd>");
    } else {
        strbuf_append_str(out, "</li>");
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_html(DocElement* elem, StrBuf* out,
                               const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<table class=\"%stable\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</table>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_row_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "<tr>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</tr>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_table_cell_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    const char* align_style = "";
    switch (elem->cell.alignment) {
    case 'c': align_style = " style=\"text-align: center;\""; break;
    case 'r': align_style = " style=\"text-align: right;\""; break;
    case 'l': 
    default:  align_style = " style=\"text-align: left;\""; break;
    }
    
    strbuf_append_format(out, "<td%s", align_style);
    if (elem->cell.colspan > 1) {
        strbuf_append_format(out, " colspan=\"%d\"", elem->cell.colspan);
    }
    if (elem->cell.rowspan > 1) {
        strbuf_append_format(out, " rowspan=\"%d\"", elem->cell.rowspan);
    }
    strbuf_append_str(out, ">");
    
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</td>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_math_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts, int depth) {
    bool is_display = (elem->type == DocElemType::MATH_DISPLAY ||
                       elem->type == DocElemType::MATH_EQUATION ||
                       elem->type == DocElemType::MATH_ALIGN);
    
    const char* css_class = is_display ? "math-display" : "math-inline";
    
    // Check if we have a typeset TexNode tree
    bool has_svg = (opts.math_as_svg && elem->math.node != nullptr);
    
    if (is_display) {
        if (opts.pretty_print) html_indent(out, depth);
        strbuf_append_format(out, "<div class=\"%s%s\">", opts.css_class_prefix, css_class);
        if (opts.pretty_print) strbuf_append_str(out, "\n");
        
        if (has_svg) {
            // Render math as inline SVG
            if (opts.pretty_print) html_indent(out, depth + 1);
            
            // Create temporary arena for SVG rendering
            Pool* temp_pool = pool_create();
            Arena* temp_arena = arena_create_default(temp_pool);
            
            SVGParams svg_params = SVGParams::defaults();
            svg_params.indent = false;  // Compact for inline
            
            const char* svg = svg_render_math_inline(elem->math.node, temp_arena, &svg_params);
            if (svg) {
                strbuf_append_str(out, svg);
            }
            
            arena_destroy(temp_arena);
            pool_destroy(temp_pool);
            
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        } else if (elem->math.latex_src) {
            // Fallback: output LaTeX source in a comment
            if (opts.pretty_print) html_indent(out, depth + 1);
            strbuf_append_str(out, "<span class=\"");
            strbuf_append_str(out, opts.css_class_prefix);
            strbuf_append_str(out, "math-fallback\">");
            html_escape_append(out, elem->math.latex_src, strlen(elem->math.latex_src));
            strbuf_append_str(out, "</span>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        }
        
        // Equation number
        if (elem->math.number) {
            if (opts.pretty_print) html_indent(out, depth + 1);
            strbuf_append_format(out, "<span class=\"%seq-number\">(%s)</span>",
                opts.css_class_prefix, elem->math.number);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        }
        
        if (opts.pretty_print) html_indent(out, depth);
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    } else {
        // Inline math
        strbuf_append_format(out, "<span class=\"%s%s\">", opts.css_class_prefix, css_class);
        
        if (has_svg) {
            // Render math as inline SVG
            Pool* temp_pool = pool_create();
            Arena* temp_arena = arena_create_default(temp_pool);
            
            SVGParams svg_params = SVGParams::defaults();
            svg_params.indent = false;
            
            const char* svg = svg_render_math_inline(elem->math.node, temp_arena, &svg_params);
            if (svg) {
                strbuf_append_str(out, svg);
            }
            
            arena_destroy(temp_arena);
            pool_destroy(temp_pool);
        } else if (elem->math.latex_src) {
            // Fallback: output escaped LaTeX
            html_escape_append(out, elem->math.latex_src, strlen(elem->math.latex_src));
        }
        
        strbuf_append_str(out, "</span>");
    }
}

static void render_link_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<a href=\"");
    if (elem->link.href) {
        html_escape_append(out, elem->link.href, strlen(elem->link.href));
    }
    strbuf_append_str(out, "\">");
    
    if (elem->link.link_text) {
        html_escape_append(out, elem->link.link_text, strlen(elem->link.link_text));
    }
    
    render_children_html(elem, out, opts, 0);
    
    strbuf_append_str(out, "</a>");
}

static void render_image_html(DocElement* elem, StrBuf* out,
                               const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    
    strbuf_append_str(out, "<img src=\"");
    if (elem->image.src) {
        html_escape_append(out, elem->image.src, strlen(elem->image.src));
    }
    strbuf_append_str(out, "\"");
    
    if (elem->image.width > 0) {
        strbuf_append_format(out, " width=\"%.0f\"", elem->image.width);
    }
    if (elem->image.height > 0) {
        strbuf_append_format(out, " height=\"%.0f\"", elem->image.height);
    }
    if (elem->image.alt) {
        strbuf_append_str(out, " alt=\"");
        html_escape_append(out, elem->image.alt, strlen(elem->image.alt));
        strbuf_append_str(out, "\"");
    }
    
    strbuf_append_str(out, " />");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_figure_html(DocElement* elem, StrBuf* out,
                                const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<figure class=\"%sfigure\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</figure>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_blockquote_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<blockquote class=\"%sblockquote\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</blockquote>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_code_block_html(DocElement* elem, StrBuf* out,
                                    const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<pre class=\"%scode-block\"><code>", opts.css_class_prefix);
    
    // For code blocks, render text directly without escaping newlines
    if (elem->text.text && elem->text.text_len > 0) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</code></pre>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_cross_ref_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<a href=\"#");
    if (elem->ref.ref_label) {
        html_escape_append(out, elem->ref.ref_label, strlen(elem->ref.ref_label));
    }
    strbuf_append_str(out, "\">");
    
    if (elem->ref.ref_text) {
        html_escape_append(out, elem->ref.ref_text, strlen(elem->ref.ref_text));
    }
    
    strbuf_append_str(out, "</a>");
}

static void render_citation_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts) {
    strbuf_append_str(out, "<cite>");
    if (elem->citation.cite_text) {
        html_escape_append(out, elem->citation.cite_text, strlen(elem->citation.cite_text));
    }
    strbuf_append_str(out, "</cite>");
}

static void render_footnote_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts) {
    strbuf_append_format(out, "<sup class=\"%sfootnote\"><a href=\"#fn%d\">[%d]</a></sup>",
        opts.css_class_prefix, elem->footnote.footnote_number, elem->footnote.footnote_number);
}

static void render_abstract_html(DocElement* elem, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<div class=\"%sabstract\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    if (opts.pretty_print) html_indent(out, depth + 1);
    strbuf_append_format(out, "<div class=\"%sabstract-title\">Abstract</div>", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</div>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_title_block_html(DocElement* elem, StrBuf* out,
                                     const HtmlOutputOptions& opts, int depth,
                                     TexDocumentModel* doc) {
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "<div class=\"%stitle-block\">", opts.css_class_prefix);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    if (doc && doc->title) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-title\">", opts.css_class_prefix);
        html_escape_append(out, doc->title, strlen(doc->title));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    if (doc && doc->author) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-author\">", opts.css_class_prefix);
        html_escape_append(out, doc->author, strlen(doc->author));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    if (doc && doc->date) {
        if (opts.pretty_print) html_indent(out, depth + 1);
        strbuf_append_format(out, "<div class=\"%sdoc-date\">", opts.css_class_prefix);
        html_escape_append(out, doc->date, strlen(doc->date));
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
    }
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_str(out, "</div>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_children_html(DocElement* parent, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth) {
    for (DocElement* child = parent->first_child; child; child = child->next_sibling) {
        doc_element_to_html(child, out, opts, depth);
    }
}

// helper: check if element is inline (should be wrapped in paragraph at doc level)
static bool is_inline_element(DocElement* elem) {
    if (!elem) return false;
    switch (elem->type) {
    case DocElemType::TEXT_RUN:
    case DocElemType::TEXT_SPAN:
    case DocElemType::SPACE:
        return true;
    default:
        return false;
    }
}

// render document children in legacy mode - wraps consecutive inline elements in <p>
static void render_document_children_legacy(DocElement* doc, StrBuf* out,
                                            const HtmlOutputOptions& opts, int depth) {
    bool in_paragraph = false;
    
    for (DocElement* child = doc->first_child; child; child = child->next_sibling) {
        if (is_inline_element(child)) {
            // skip whitespace-only text runs at start
            if (!in_paragraph) {
                if (child->type == DocElemType::TEXT_RUN && child->text.text) {
                    // check if whitespace only
                    bool whitespace_only = true;
                    for (size_t i = 0; i < child->text.text_len; i++) {
                        char c = child->text.text[i];
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                            whitespace_only = false;
                            break;
                        }
                    }
                    if (whitespace_only) continue;
                }
                strbuf_append_str(out, "<p>");
                in_paragraph = true;
            }
            doc_element_to_html(child, out, opts, depth);
        } else {
            // block element - close paragraph if open
            if (in_paragraph) {
                strbuf_append_str(out, "</p>\n");
                in_paragraph = false;
            }
            doc_element_to_html(child, out, opts, depth);
        }
    }
    
    // close trailing paragraph
    if (in_paragraph) {
        strbuf_append_str(out, "</p>\n");
    }
}

void doc_element_to_html(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth) {
    if (!elem) return;
    
    switch (elem->type) {
    case DocElemType::DOCUMENT:
        if (opts.legacy_mode) {
            render_document_children_legacy(elem, out, opts, depth);
        } else {
            render_children_html(elem, out, opts, depth);
        }
        break;
        
    case DocElemType::TEXT_SPAN:
        render_text_span_html(elem, out, opts);
        break;
        
    case DocElemType::TEXT_RUN:
        if (elem->text.text && elem->text.text_len > 0) {
            // Check if we're in monospace context (texttt style)
            bool in_monospace = elem->text.style.has(DocTextStyle::MONOSPACE);
            html_escape_append_transformed(out, elem->text.text, elem->text.text_len, in_monospace);
        }
        break;
        
    case DocElemType::HEADING:
        render_heading_html(elem, out, opts, depth);
        break;
        
    case DocElemType::PARAGRAPH:
        render_paragraph_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LIST:
        render_list_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LIST_ITEM: {
        // Determine parent list type
        ListType parent_type = ListType::ITEMIZE;
        if (elem->parent && elem->parent->type == DocElemType::LIST) {
            parent_type = elem->parent->list.list_type;
        }
        render_list_item_html(elem, out, opts, depth, parent_type);
        break;
    }
        
    case DocElemType::TABLE:
        render_table_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TABLE_ROW:
        render_table_row_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TABLE_CELL:
        render_table_cell_html(elem, out, opts, depth);
        break;
        
    case DocElemType::MATH_INLINE:
    case DocElemType::MATH_DISPLAY:
    case DocElemType::MATH_EQUATION:
    case DocElemType::MATH_ALIGN:
        render_math_html(elem, out, opts, depth);
        break;
        
    case DocElemType::LINK:
        render_link_html(elem, out, opts);
        break;
        
    case DocElemType::IMAGE:
        render_image_html(elem, out, opts, depth);
        break;
        
    case DocElemType::FIGURE:
        render_figure_html(elem, out, opts, depth);
        break;
        
    case DocElemType::BLOCKQUOTE:
        render_blockquote_html(elem, out, opts, depth);
        break;
        
    case DocElemType::CODE_BLOCK:
        render_code_block_html(elem, out, opts, depth);
        break;
        
    case DocElemType::ALIGNMENT: {
        // Determine alignment class from flags
        const char* align_class = "list";
        if (elem->flags & DocElement::FLAG_CENTERED) {
            align_class = "list center";
        } else if (elem->flags & DocElement::FLAG_FLUSH_LEFT) {
            align_class = "list flushleft";
        } else if (elem->flags & DocElement::FLAG_FLUSH_RIGHT) {
            align_class = "list flushright";
        }
        strbuf_append_format(out, "<div class=\"%s\">", align_class);
        if (opts.pretty_print) strbuf_append_str(out, "\n");
        render_children_html(elem, out, opts, depth + 1);
        strbuf_append_str(out, "</div>");
        if (opts.pretty_print) strbuf_append_str(out, "\n");
        break;
    }
        
    case DocElemType::CROSS_REF:
        render_cross_ref_html(elem, out, opts);
        break;
        
    case DocElemType::CITATION:
        render_citation_html(elem, out, opts);
        break;
        
    case DocElemType::FOOTNOTE:
        render_footnote_html(elem, out, opts);
        break;
        
    case DocElemType::ABSTRACT:
        render_abstract_html(elem, out, opts, depth);
        break;
        
    case DocElemType::TITLE_BLOCK:
        render_title_block_html(elem, out, opts, depth, nullptr);
        break;
        
    case DocElemType::SECTION:
        render_children_html(elem, out, opts, depth);
        break;
        
    case DocElemType::SPACE:
        if (elem->space.is_linebreak) {
            strbuf_append_str(out, "<br>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
        } else {
            strbuf_append_str(out, " ");
        }
        break;
        
    case DocElemType::RAW_HTML:
        if (elem->raw.raw_content && elem->raw.raw_len > 0) {
            strbuf_append_str_n(out, elem->raw.raw_content, elem->raw.raw_len);
        }
        break;
        
    case DocElemType::RAW_LATEX:
        // Skip raw LaTeX in HTML output
        strbuf_append_str(out, "<!-- LaTeX: ");
        if (elem->raw.raw_content && elem->raw.raw_len > 0) {
            html_escape_append(out, elem->raw.raw_content, elem->raw.raw_len);
        }
        strbuf_append_str(out, " -->");
        break;
        
    case DocElemType::ERROR:
        strbuf_append_str(out, "<span class=\"error\">[ERROR]</span>");
        break;
        
    default:
        log_debug("doc_element_to_html: unhandled type %s", doc_elem_type_name(elem->type));
        break;
    }
}

// ============================================================================
// Document to HTML
// ============================================================================

bool doc_model_to_html(TexDocumentModel* doc, StrBuf* output, const HtmlOutputOptions& opts) {
    if (!doc || !output) return false;
    
    // HTML header
    if (opts.standalone) {
        strbuf_append_str(output, "<!DOCTYPE html>\n");
        strbuf_append_format(output, "<html lang=\"%s\">\n", opts.lang);
        strbuf_append_str(output, "<head>\n");
        strbuf_append_str(output, "  <meta charset=\"UTF-8\">\n");
        strbuf_append_str(output, "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
        
        // Title
        if (doc->title) {
            strbuf_append_str(output, "  <title>");
            html_escape_append(output, doc->title, strlen(doc->title));
            strbuf_append_str(output, "</title>\n");
        } else {
            strbuf_append_str(output, "  <title>Document</title>\n");
        }
        
        // Web fonts
        if (opts.font_mode == HtmlOutputOptions::FONT_WEBFONT) {
            strbuf_append_str(output, "  <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/computer-modern@0.1.2/cmsans.min.css\">\n");
            strbuf_append_str(output, "  <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/computer-modern@0.1.2/cmserif.min.css\">\n");
        }
        
        // CSS
        if (opts.include_css) {
            html_write_default_css(output, opts.css_class_prefix);
        }
        
        strbuf_append_str(output, "</head>\n");
        strbuf_append_str(output, "<body>\n");
    }
    
    // Document container
    if (opts.legacy_mode) {
        // Legacy mode: <div class="body">
        strbuf_append_str(output, "<div class=\"body\">\n");
    } else {
        // Modern mode: <article class="latex-document ...">
        strbuf_append_format(output, "<article class=\"%sdocument %s%s\">\n", 
            opts.css_class_prefix, opts.css_class_prefix, doc->document_class);
    }
    
    // Title block (only in non-legacy mode)
    if (!opts.legacy_mode && (doc->title || doc->author || doc->date)) {
        strbuf_append_format(output, "  <header class=\"%stitle-block\">\n", opts.css_class_prefix);
        if (doc->title) {
            strbuf_append_format(output, "    <h1 class=\"%sdoc-title\">", opts.css_class_prefix);
            html_escape_append(output, doc->title, strlen(doc->title));
            strbuf_append_str(output, "</h1>\n");
        }
        if (doc->author) {
            strbuf_append_format(output, "    <div class=\"%sdoc-author\">", opts.css_class_prefix);
            html_escape_append(output, doc->author, strlen(doc->author));
            strbuf_append_str(output, "</div>\n");
        }
        if (doc->date) {
            strbuf_append_format(output, "    <div class=\"%sdoc-date\">", opts.css_class_prefix);
            html_escape_append(output, doc->date, strlen(doc->date));
            strbuf_append_str(output, "</div>\n");
        }
        strbuf_append_str(output, "  </header>\n");
    }
    
    // Document content
    if (doc->root) {
        doc_element_to_html(doc->root, output, opts, 1);
    }
    
    // Close document container
    if (opts.legacy_mode) {
        strbuf_append_str(output, "</div>\n");
    } else {
        strbuf_append_str(output, "</article>\n");
    }
    
    // HTML footer
    if (opts.standalone) {
        strbuf_append_str(output, "</body>\n");
        strbuf_append_str(output, "</html>\n");
    }
    
    return true;
}

// ============================================================================
// Debug Output
// ============================================================================

void doc_element_dump(DocElement* elem, StrBuf* out, int depth) {
    if (!elem) return;
    
    // Indent
    for (int i = 0; i < depth; i++) {
        strbuf_append_str(out, "  ");
    }
    
    // Type name
    strbuf_append_format(out, "[%s]", doc_elem_type_name(elem->type));
    
    // Type-specific info
    switch (elem->type) {
    case DocElemType::TEXT_SPAN:
    case DocElemType::TEXT_RUN:
        if (elem->text.text && elem->text.text_len > 0) {
            strbuf_append_str(out, " \"");
            size_t show_len = elem->text.text_len > 40 ? 40 : elem->text.text_len;
            strbuf_append_str_n(out, elem->text.text, show_len);
            if (elem->text.text_len > 40) strbuf_append_str(out, "...");
            strbuf_append_str(out, "\"");
        }
        if (elem->text.style.flags != DocTextStyle::NONE) {
            strbuf_append_format(out, " flags=0x%x", elem->text.style.flags);
        }
        break;
        
    case DocElemType::HEADING:
        strbuf_append_format(out, " level=%d", elem->heading.level);
        if (elem->heading.title) {
            strbuf_append_format(out, " title=\"%s\"", elem->heading.title);
        }
        if (elem->heading.number) {
            strbuf_append_format(out, " number=\"%s\"", elem->heading.number);
        }
        break;
        
    case DocElemType::LIST:
        strbuf_append_format(out, " type=%d", (int)elem->list.list_type);
        break;
        
    case DocElemType::MATH_INLINE:
    case DocElemType::MATH_DISPLAY:
        if (elem->math.latex_src) {
            strbuf_append_format(out, " src=\"%s\"", elem->math.latex_src);
        }
        break;
        
    default:
        break;
    }
    
    strbuf_append_str(out, "\n");
    
    // Recurse to children
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        doc_element_dump(child, out, depth + 1);
    }
}

void doc_model_dump(TexDocumentModel* doc, StrBuf* out) {
    if (!doc) {
        strbuf_append_str(out, "(null document)\n");
        return;
    }
    
    strbuf_append_str(out, "=== Document Model ===\n");
    strbuf_append_format(out, "Class: %s\n", doc->document_class ? doc->document_class : "(none)");
    if (doc->title) strbuf_append_format(out, "Title: %s\n", doc->title);
    if (doc->author) strbuf_append_format(out, "Author: %s\n", doc->author);
    if (doc->date) strbuf_append_format(out, "Date: %s\n", doc->date);
    strbuf_append_str(out, "\n--- Tree ---\n");
    
    if (doc->root) {
        doc_element_dump(doc->root, out, 0);
    } else {
        strbuf_append_str(out, "(no root element)\n");
    }
}

// ============================================================================
// Phase C: LaTeX AST to Document Model Builder
// ============================================================================

// This section requires the Lambda runtime (ItemReader, ElementReader).
// Define DOC_MODEL_MINIMAL to exclude this section for minimal test builds.
#ifndef DOC_MODEL_MINIMAL

// Forward declarations for recursive builders
static DocElement* build_doc_element(const ItemReader& item, Arena* arena, 
                                      TexDocumentModel* doc);
static DocElement* build_inline_content(const ItemReader& item, Arena* arena,
                                         TexDocumentModel* doc);

// Helper to check tag name equality (case-insensitive for safety)
static bool tag_eq(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

// Sentinel pointer values for special markers (used internally during tree building)
static DocElement* const PARBREAK_MARKER = (DocElement*)1;  // paragraph break marker
static DocElement* const LINEBREAK_MARKER = (DocElement*)2; // line break marker

// Check if an item is a paragraph break marker (parbreak symbol or \par command)
static bool is_parbreak_item(const ItemReader& item) {
    // Symbol "parbreak" (from paragraph_break in grammar)
    if (item.isSymbol()) {
        const char* sym = item.cstring();
        if (sym && strcmp(sym, "parbreak") == 0) {
            return true;
        }
    }
    
    // String "parbreak" (symbols may come through as strings in some cases)
    if (item.isString()) {
        const char* str = item.cstring();
        if (str && strcmp(str, "parbreak") == 0) {
            return true;
        }
    }
    
    // Element with tag "par" (from \par command)
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        const char* tag = elem.tagName();
        if (tag && strcmp(tag, "par") == 0) {
            return true;
        }
    }
    
    return false;
}

// Check if an item is a line break command (\\, \newline)
static bool is_linebreak_item(const ItemReader& item) {
    if (!item.isElement()) return false;
    
    ElementReader elem = item.asElement();
    const char* tag = elem.tagName();
    if (!tag) return false;
    
    // linebreak_command tag from \\
    if (strcmp(tag, "linebreak_command") == 0) return true;
    
    // \newline command
    if (strcmp(tag, "newline") == 0) return true;
    
    return false;
}

// Check if a string is a font size command (returns the FontSizeName or INHERIT if not)
static FontSizeName get_font_size_cmd(const char* text) {
    if (!text || text[0] != '\\') return FontSizeName::INHERIT;
    const char* cmd = text + 1;  // skip backslash
    if (strcmp(cmd, "tiny") == 0) return FontSizeName::FONT_TINY;
    if (strcmp(cmd, "scriptsize") == 0) return FontSizeName::FONT_SCRIPTSIZE;
    if (strcmp(cmd, "footnotesize") == 0) return FontSizeName::FONT_FOOTNOTESIZE;
    if (strcmp(cmd, "small") == 0) return FontSizeName::FONT_SMALL;
    if (strcmp(cmd, "normalsize") == 0) return FontSizeName::FONT_NORMALSIZE;
    if (strcmp(cmd, "large") == 0) return FontSizeName::FONT_LARGE;
    if (strcmp(cmd, "Large") == 0) return FontSizeName::FONT_LARGE2;
    if (strcmp(cmd, "LARGE") == 0) return FontSizeName::FONT_LARGE3;
    if (strcmp(cmd, "huge") == 0) return FontSizeName::FONT_HUGE;
    if (strcmp(cmd, "Huge") == 0) return FontSizeName::FONT_HUGE2;
    return FontSizeName::INHERIT;
}

// Extract text content from an item (recursively collects all text)
static const char* extract_text_content(const ItemReader& item, Arena* arena) {
    if (item.isString()) {
        const char* str = item.cstring();
        if (str) {
            size_t len = strlen(str);
            char* copy = (char*)arena_alloc(arena, len + 1);
            memcpy(copy, str, len + 1);
            return copy;
        }
        return nullptr;
    }
    
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        StrBuf* buf = strbuf_new_cap(256);
        
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            const char* child_text = extract_text_content(child, arena);
            if (child_text) {
                strbuf_append_str(buf, child_text);
            }
        }
        
        if (buf->length > 0) {
            char* result = (char*)arena_alloc(arena, buf->length + 1);
            memcpy(result, buf->str, buf->length + 1);
            strbuf_free(buf);
            return result;
        }
        strbuf_free(buf);
    }
    
    return nullptr;
}

// Extract math source - checks "source" attribute first, then falls back to text content
static const char* extract_math_source(const ElementReader& elem, Arena* arena) {
    // Check for "source" attribute (present in display_math, inline_math)
    const char* src = elem.get_attr_string("source");
    if (src) {
        size_t len = strlen(src);
        char* copy = (char*)arena_alloc(arena, len + 1);
        memcpy(copy, src, len + 1);
        return copy;
    }
    
    // Fallback to extracting text content from children
    ConstItem item;
    item.element = elem.element();
    ItemReader item_reader(item);
    return extract_text_content(item_reader, arena);
}

// Helper to set style flags based on font command name
static void build_text_command_set_style(const char* cmd_name, DocTextStyle* style) {
    *style = DocTextStyle::plain();
    
    // Style flags
    if (tag_eq(cmd_name, "textbf") || tag_eq(cmd_name, "bf") || tag_eq(cmd_name, "bfseries")) {
        style->flags |= DocTextStyle::BOLD;
    } else if (tag_eq(cmd_name, "textit") || tag_eq(cmd_name, "it") || 
               tag_eq(cmd_name, "itshape") || tag_eq(cmd_name, "emph")) {
        style->flags |= DocTextStyle::ITALIC;
    } else if (tag_eq(cmd_name, "texttt") || tag_eq(cmd_name, "tt") || tag_eq(cmd_name, "ttfamily")) {
        style->flags |= DocTextStyle::MONOSPACE;
    } else if (tag_eq(cmd_name, "textsc") || tag_eq(cmd_name, "scshape")) {
        style->flags |= DocTextStyle::SMALLCAPS;
    } else if (tag_eq(cmd_name, "underline")) {
        style->flags |= DocTextStyle::UNDERLINE;
    } else if (tag_eq(cmd_name, "sout") || tag_eq(cmd_name, "st")) {
        style->flags |= DocTextStyle::STRIKEOUT;
    }
    // Font size names
    else if (tag_eq(cmd_name, "tiny")) {
        style->font_size_name = FontSizeName::FONT_TINY;
    } else if (tag_eq(cmd_name, "scriptsize")) {
        style->font_size_name = FontSizeName::FONT_SCRIPTSIZE;
    } else if (tag_eq(cmd_name, "footnotesize")) {
        style->font_size_name = FontSizeName::FONT_FOOTNOTESIZE;
    } else if (tag_eq(cmd_name, "small")) {
        style->font_size_name = FontSizeName::FONT_SMALL;
    } else if (tag_eq(cmd_name, "normalsize")) {
        style->font_size_name = FontSizeName::FONT_NORMALSIZE;
    } else if (tag_eq(cmd_name, "large")) {
        style->font_size_name = FontSizeName::FONT_LARGE;
    } else if (tag_eq(cmd_name, "Large")) {
        style->font_size_name = FontSizeName::FONT_LARGE2;
    } else if (tag_eq(cmd_name, "LARGE")) {
        style->font_size_name = FontSizeName::FONT_LARGE3;
    } else if (tag_eq(cmd_name, "huge")) {
        style->font_size_name = FontSizeName::FONT_HUGE;
    } else if (tag_eq(cmd_name, "Huge")) {
        style->font_size_name = FontSizeName::FONT_HUGE2;
    }
}

// Build a TEXT_SPAN element with style flags
static DocElement* build_text_command(const char* cmd_name, const ElementReader& elem,
                                       Arena* arena, TexDocumentModel* doc) {
    DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    span->text.style = DocTextStyle::plain();
    
    // Set style flags based on command
    if (tag_eq(cmd_name, "textbf") || tag_eq(cmd_name, "bf") || tag_eq(cmd_name, "bfseries")) {
        span->text.style.flags |= DocTextStyle::BOLD;
    } else if (tag_eq(cmd_name, "textit") || tag_eq(cmd_name, "it") || 
               tag_eq(cmd_name, "itshape") || tag_eq(cmd_name, "emph")) {
        span->text.style.flags |= DocTextStyle::ITALIC;
    } else if (tag_eq(cmd_name, "texttt") || tag_eq(cmd_name, "tt") || tag_eq(cmd_name, "ttfamily")) {
        span->text.style.flags |= DocTextStyle::MONOSPACE;
    } else if (tag_eq(cmd_name, "textsc") || tag_eq(cmd_name, "scshape")) {
        span->text.style.flags |= DocTextStyle::SMALLCAPS;
    } else if (tag_eq(cmd_name, "underline")) {
        span->text.style.flags |= DocTextStyle::UNDERLINE;
    } else if (tag_eq(cmd_name, "sout") || tag_eq(cmd_name, "st")) {
        span->text.style.flags |= DocTextStyle::STRIKEOUT;
    }
    // Font size commands
    else if (tag_eq(cmd_name, "tiny")) {
        span->text.style.font_size_name = FontSizeName::FONT_TINY;
    } else if (tag_eq(cmd_name, "scriptsize")) {
        span->text.style.font_size_name = FontSizeName::FONT_SCRIPTSIZE;
    } else if (tag_eq(cmd_name, "footnotesize")) {
        span->text.style.font_size_name = FontSizeName::FONT_FOOTNOTESIZE;
    } else if (tag_eq(cmd_name, "small")) {
        span->text.style.font_size_name = FontSizeName::FONT_SMALL;
    } else if (tag_eq(cmd_name, "normalsize")) {
        span->text.style.font_size_name = FontSizeName::FONT_NORMALSIZE;
    } else if (tag_eq(cmd_name, "large")) {
        span->text.style.font_size_name = FontSizeName::FONT_LARGE;
    } else if (tag_eq(cmd_name, "Large")) {
        span->text.style.font_size_name = FontSizeName::FONT_LARGE2;
    } else if (tag_eq(cmd_name, "LARGE")) {
        span->text.style.font_size_name = FontSizeName::FONT_LARGE3;
    } else if (tag_eq(cmd_name, "huge")) {
        span->text.style.font_size_name = FontSizeName::FONT_HUGE;
    } else if (tag_eq(cmd_name, "Huge")) {
        span->text.style.font_size_name = FontSizeName::FONT_HUGE2;
    }
    
    // Process children
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        DocElement* child_elem = build_inline_content(child, arena, doc);
        if (child_elem) {
            doc_append_child(span, child_elem);
        }
    }
    
    return span;
}

// Build a HEADING element from section command
static DocElement* build_section_command(const char* cmd_name, const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc) {
    DocElement* heading = doc_alloc_element(arena, DocElemType::HEADING);
    
    // Determine level from command name
    if (tag_eq(cmd_name, "part")) {
        heading->heading.level = 0;
    } else if (tag_eq(cmd_name, "chapter")) {
        heading->heading.level = 1;
    } else if (tag_eq(cmd_name, "section")) {
        heading->heading.level = 2;
    } else if (tag_eq(cmd_name, "subsection")) {
        heading->heading.level = 3;
    } else if (tag_eq(cmd_name, "subsubsection")) {
        heading->heading.level = 4;
    } else if (tag_eq(cmd_name, "paragraph")) {
        heading->heading.level = 5;
    } else if (tag_eq(cmd_name, "subparagraph")) {
        heading->heading.level = 6;
    } else {
        heading->heading.level = 2; // default to section
    }
    
    // Check for starred version (no numbering)
    // Look for "star" child element
    auto iter = elem.children();
    ItemReader child;
    bool has_star = false;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag_eq(tag, "star") || tag_eq(tag, "*")) {
                has_star = true;
            } else if (tag_eq(tag, "curly_group") || tag_eq(tag, "title") || 
                       tag_eq(tag, "brack_group") || tag_eq(tag, "text") || 
                       tag_eq(tag, "arg")) {
                // Extract title text
                heading->heading.title = extract_text_content(child, arena);
            }
        } else if (child.isString() && !heading->heading.title) {
            // Direct string child is the title
            const char* text = child.cstring();
            if (text && strlen(text) > 0 && text[0] != '\n') {
                size_t len = strlen(text);
                char* title = (char*)arena_alloc(arena, len + 1);
                memcpy(title, text, len + 1);
                heading->heading.title = title;
            }
        }
    }
    
    if (has_star) {
        heading->flags |= DocElement::FLAG_STARRED;
    } else {
        heading->flags |= DocElement::FLAG_NUMBERED;
        
        // Generate section number
        switch (heading->heading.level) {
            case 1: doc->chapter_num++; 
                    doc->section_num = 0; 
                    break;
            case 2: doc->section_num++;
                    doc->subsection_num = 0;
                    break;
            case 3: doc->subsection_num++; break;
        }
        
        // Format number string
        char num_buf[64];
        switch (heading->heading.level) {
            case 1:
                snprintf(num_buf, sizeof(num_buf), "%d", doc->chapter_num);
                break;
            case 2:
                if (doc->chapter_num > 0) {
                    snprintf(num_buf, sizeof(num_buf), "%d.%d", 
                             doc->chapter_num, doc->section_num);
                } else {
                    snprintf(num_buf, sizeof(num_buf), "%d", doc->section_num);
                }
                break;
            case 3:
                if (doc->chapter_num > 0) {
                    snprintf(num_buf, sizeof(num_buf), "%d.%d.%d",
                             doc->chapter_num, doc->section_num, doc->subsection_num);
                } else {
                    snprintf(num_buf, sizeof(num_buf), "%d.%d", 
                             doc->section_num, doc->subsection_num);
                }
                break;
            default:
                num_buf[0] = '\0';
        }
        
        if (num_buf[0]) {
            size_t len = strlen(num_buf);
            char* num = (char*)arena_alloc(arena, len + 1);
            memcpy(num, num_buf, len + 1);
            heading->heading.number = num;
        }
    }
    
    return heading;
}

// Build inline content (text runs, styled spans, etc.)
static DocElement* build_inline_content(const ItemReader& item, Arena* arena,
                                         TexDocumentModel* doc) {
    if (item.isString()) {
        const char* text = item.cstring();
        if (text && strlen(text) > 0) {
            return doc_create_text_normalized(arena, text, DocTextStyle::plain());
        }
        return nullptr;
    }
    
    if (!item.isElement()) {
        return nullptr;
    }
    
    ElementReader elem = item.asElement();
    const char* tag = elem.tagName();
    
    if (!tag) return nullptr;
    
    // Text formatting commands
    if (tag_eq(tag, "textbf") || tag_eq(tag, "textit") || tag_eq(tag, "texttt") ||
        tag_eq(tag, "emph") || tag_eq(tag, "textsc") || tag_eq(tag, "underline")) {
        return build_text_command(tag, elem, arena, doc);
    }
    
    // Symbol commands parsed directly as element tags (e.g., {"$":"textellipsis"})
    // Ellipsis
    if (tag_eq(tag, "textellipsis") || tag_eq(tag, "ldots") || tag_eq(tag, "dots")) {
        return doc_create_text_cstr(arena, "\xE2\x80\xA6", DocTextStyle::plain());  // …
    }
    // En-dash and em-dash
    if (tag_eq(tag, "textendash")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x93", DocTextStyle::plain());  // –
    }
    if (tag_eq(tag, "textemdash")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x94", DocTextStyle::plain());  // —
    }
    // LaTeX/TeX logos
    if (tag_eq(tag, "LaTeX")) {
        return doc_create_text_cstr(arena, "LaTeX", DocTextStyle::plain());
    }
    if (tag_eq(tag, "TeX")) {
        return doc_create_text_cstr(arena, "TeX", DocTextStyle::plain());
    }
    // Special characters
    if (tag_eq(tag, "textbackslash")) {
        return doc_create_text_cstr(arena, "\\", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textasciitilde")) {
        return doc_create_text_cstr(arena, "~", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textasciicircum")) {
        return doc_create_text_cstr(arena, "^", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textbar")) {
        return doc_create_text_cstr(arena, "|", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textless")) {
        return doc_create_text_cstr(arena, "<", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textgreater")) {
        return doc_create_text_cstr(arena, ">", DocTextStyle::plain());
    }
    // Quotation marks
    if (tag_eq(tag, "textquoteleft")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x98", DocTextStyle::plain());  // '
    }
    if (tag_eq(tag, "textquoteright")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x99", DocTextStyle::plain());  // '
    }
    if (tag_eq(tag, "textquotedblleft")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x9C", DocTextStyle::plain());  // "
    }
    if (tag_eq(tag, "textquotedblright")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x9D", DocTextStyle::plain());  // "
    }
    // Copyright/trademark
    if (tag_eq(tag, "copyright") || tag_eq(tag, "textcopyright")) {
        return doc_create_text_cstr(arena, "\xC2\xA9", DocTextStyle::plain());  // ©
    }
    if (tag_eq(tag, "trademark") || tag_eq(tag, "texttrademark")) {
        return doc_create_text_cstr(arena, "\xE2\x84\xA2", DocTextStyle::plain());  // ™
    }
    if (tag_eq(tag, "textregistered")) {
        return doc_create_text_cstr(arena, "\xC2\xAE", DocTextStyle::plain());  // ®
    }
    // Spacing commands that output space
    if (tag_eq(tag, "quad") || tag_eq(tag, "qquad") || 
        tag_eq(tag, "enspace") || tag_eq(tag, "enskip") ||
        tag_eq(tag, "thinspace")) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = false;
        return space;
    }
    // Non-breaking space
    if (tag_eq(tag, "nobreakspace") || tag_eq(tag, "nbsp")) {
        return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
    }
    
    // Generic command - check command_name child
    if (tag_eq(tag, "generic_command") || tag_eq(tag, "command")) {
        auto iter = elem.children();
        ItemReader child;
        const char* cmd_name = nullptr;
        
        // Find command name
        while (iter.next(&child)) {
            if (child.isString()) {
                cmd_name = child.cstring();
                if (cmd_name && cmd_name[0] == '\\') cmd_name++;
                break;
            }
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                if (tag_eq(child_elem.tagName(), "command_name")) {
                    cmd_name = extract_text_content(child, arena);
                    if (cmd_name && cmd_name[0] == '\\') cmd_name++;
                    break;
                }
            }
        }
        
        if (cmd_name) {
            if (tag_eq(cmd_name, "textbf") || tag_eq(cmd_name, "textit") || 
                tag_eq(cmd_name, "texttt") || tag_eq(cmd_name, "emph") ||
                tag_eq(cmd_name, "textsc") || tag_eq(cmd_name, "underline")) {
                return build_text_command(cmd_name, elem, arena, doc);
            }
            
            // Text symbol commands - output fixed Unicode characters
            // Ellipsis
            if (tag_eq(cmd_name, "textellipsis") || tag_eq(cmd_name, "ldots") || tag_eq(cmd_name, "dots")) {
                return doc_create_text_cstr(arena, "\xE2\x80\xA6", DocTextStyle::plain());  // …
            }
            // En-dash and em-dash
            if (tag_eq(cmd_name, "textendash")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x93", DocTextStyle::plain());  // –
            }
            if (tag_eq(cmd_name, "textemdash")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x94", DocTextStyle::plain());  // —
            }
            // LaTeX/TeX logos
            if (tag_eq(cmd_name, "LaTeX")) {
                // TODO: proper styled LaTeX logo
                return doc_create_text_cstr(arena, "LaTeX", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "TeX")) {
                // TODO: proper styled TeX logo
                return doc_create_text_cstr(arena, "TeX", DocTextStyle::plain());
            }
            // Special characters
            if (tag_eq(cmd_name, "textbackslash")) {
                return doc_create_text_cstr(arena, "\\", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "textasciitilde")) {
                return doc_create_text_cstr(arena, "~", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "textasciicircum")) {
                return doc_create_text_cstr(arena, "^", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "textbar")) {
                return doc_create_text_cstr(arena, "|", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "textless")) {
                return doc_create_text_cstr(arena, "<", DocTextStyle::plain());
            }
            if (tag_eq(cmd_name, "textgreater")) {
                return doc_create_text_cstr(arena, ">", DocTextStyle::plain());
            }
            // Quotation marks
            if (tag_eq(cmd_name, "textquoteleft")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x98", DocTextStyle::plain());  // '
            }
            if (tag_eq(cmd_name, "textquoteright")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x99", DocTextStyle::plain());  // '
            }
            if (tag_eq(cmd_name, "textquotedblleft")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x9C", DocTextStyle::plain());  // "
            }
            if (tag_eq(cmd_name, "textquotedblright")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x9D", DocTextStyle::plain());  // "
            }
            // Copyright/trademark
            if (tag_eq(cmd_name, "copyright") || tag_eq(cmd_name, "textcopyright")) {
                return doc_create_text_cstr(arena, "\xC2\xA9", DocTextStyle::plain());  // ©
            }
            if (tag_eq(cmd_name, "trademark") || tag_eq(cmd_name, "texttrademark")) {
                return doc_create_text_cstr(arena, "\xE2\x84\xA2", DocTextStyle::plain());  // ™
            }
            if (tag_eq(cmd_name, "textregistered")) {
                return doc_create_text_cstr(arena, "\xC2\xAE", DocTextStyle::plain());  // ®
            }
            // Spacing commands that output space
            if (tag_eq(cmd_name, "quad") || tag_eq(cmd_name, "qquad") || 
                tag_eq(cmd_name, "enspace") || tag_eq(cmd_name, "enskip") ||
                tag_eq(cmd_name, "thinspace")) {
                DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
                space->space.is_linebreak = false;
                return space;
            }
            // Non-breaking space
            if (tag_eq(cmd_name, "nobreakspace") || tag_eq(cmd_name, "nbsp")) {
                return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
            }
        }
    }
    
    // Curly group - process children
    if (tag_eq(tag, "curly_group") || tag_eq(tag, "brack_group") || tag_eq(tag, "group")) {
        DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        span->text.style = DocTextStyle::plain();
        
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            DocElement* child_elem = build_inline_content(child, arena, doc);
            if (child_elem) {
                doc_append_child(span, child_elem);
            }
        }
        
        // If only one child, return it directly
        if (span->first_child && span->first_child == span->last_child) {
            DocElement* only_child = span->first_child;
            only_child->parent = nullptr;
            only_child->next_sibling = nullptr;
            return only_child;
        }
        
        return span->first_child ? span : nullptr;
    }
    
    // Inline math
    if (tag_eq(tag, "inline_math") || tag_eq(tag, "math")) {
        DocElement* math = doc_alloc_element(arena, DocElemType::MATH_INLINE);
        math->math.latex_src = extract_math_source(elem, arena);
        math->math.node = nullptr; // Will be populated by typesetter if needed
        return math;
    }
    
    // Display math (can appear inside paragraphs too)
    if (tag_eq(tag, "display_math") || tag_eq(tag, "displaymath") ||
        tag_eq(tag, "equation") || tag_eq(tag, "equation*")) {
        DocElement* math = doc_alloc_element(arena, DocElemType::MATH_DISPLAY);
        math->math.latex_src = extract_math_source(elem, arena);
        math->math.node = nullptr;
        return math;
    }
    
    // Line break commands: \\ (linebreak_command) or \newline
    if (tag_eq(tag, "linebreak_command") || tag_eq(tag, "newline")) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = true;
        return space;
    }
    
    // Space command - handles \<space>, \<tab>, and \<newline>
    // All produce ZWSP (as boundary marker) + space
    if (tag_eq(tag, "space_cmd")) {
        // All space commands produce ZWSP followed by space
        // The ZWSP marks a boundary/break point, the space is the visible character
        return doc_create_text_cstr(arena, "\xE2\x80\x8B ", DocTextStyle::plain());
    }
    
    // Text content
    if (tag_eq(tag, "text") || tag_eq(tag, "word") || tag_eq(tag, "TEXT")) {
        const char* text = extract_text_content(item, arena);
        if (text && strlen(text) > 0) {
            return doc_create_text_cstr(arena, text, DocTextStyle::plain());
        }
        return nullptr;
    }
    
    // Default: process children
    auto iter = elem.children();
    ItemReader child;
    DocElement* result = nullptr;
    
    while (iter.next(&child)) {
        DocElement* child_elem = build_inline_content(child, arena, doc);
        if (child_elem) {
            if (!result) {
                result = child_elem;
            } else {
                // Multiple children - wrap in span
                DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                span->text.style = DocTextStyle::plain();
                doc_append_child(span, result);
                doc_append_child(span, child_elem);
                result = span;
            }
        }
    }
    
    return result;
}

// Forward declaration for trim_paragraph_whitespace
static void trim_paragraph_whitespace(DocElement* para, Arena* arena);

// Build a paragraph element
static DocElement* build_paragraph(const ElementReader& elem, Arena* arena,
                                    TexDocumentModel* doc) {
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        DocElement* child_elem = build_inline_content(child, arena, doc);
        if (child_elem) {
            doc_append_child(para, child_elem);
        }
    }
    
    // Trim leading/trailing whitespace from paragraph
    if (para->first_child) {
        trim_paragraph_whitespace(para, arena);
    }
    
    return para->first_child ? para : nullptr;
}

// ============================================================================
// Phase D: List and Table Environment Builders
// ============================================================================

// Forward declaration for recursive builder
static DocElement* build_doc_element(const ItemReader& item, Arena* arena, 
                                      TexDocumentModel* doc);

// Build a list item element
static DocElement* build_list_item(const ElementReader& item_elem, Arena* arena,
                                    TexDocumentModel* doc, ListType list_type) {
    DocElement* li = doc_alloc_element(arena, DocElemType::LIST_ITEM);
    
    // For description lists, extract label from optional argument
    if (list_type == ListType::DESCRIPTION) {
        // Look for label child
        auto iter = item_elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && (tag_eq(child_tag, "label") || tag_eq(child_tag, "optional"))) {
                    li->list_item.label = extract_text_content(child, arena);
                    continue;
                }
            }
            // Build content
            DocElement* content_elem = build_doc_element(child, arena, doc);
            if (content_elem) {
                doc_append_child(li, content_elem);
            }
        }
    } else {
        // Process all children as content
        auto iter = item_elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            DocElement* content_elem = build_doc_element(child, arena, doc);
            if (content_elem) {
                doc_append_child(li, content_elem);
            }
        }
    }
    
    return li;
}

// Helper to process items from a content container
static void process_list_content(DocElement* list, const ItemReader& container,
                                  Arena* arena, TexDocumentModel* doc, int& item_number) {
    if (!container.isElement()) return;
    
    ElementReader elem = container.asElement();
    DocElement* current_item = nullptr;
    
    // Scan children for items and their content
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && tag_eq(child_tag, "item")) {
                // Save previous item if exists
                if (current_item && current_item->first_child) {
                    doc_append_child(list, current_item);
                }
                // Start new item
                current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                if (list->list.list_type == ListType::ENUMERATE) {
                    current_item->list_item.item_number = item_number++;
                }
            } else if (child_tag && (tag_eq(child_tag, "paragraph") || 
                                     tag_eq(child_tag, "text_mode") ||
                                     tag_eq(child_tag, "content"))) {
                // Recurse into nested content containers
                process_list_content(list, child, arena, doc, item_number);
            } else if (current_item) {
                // Add content to current item
                DocElement* content = build_doc_element(child, arena, doc);
                if (content) {
                    doc_append_child(current_item, content);
                }
            }
        } else if (child.isString() && current_item) {
            // String content for current item
            const char* text = child.cstring();
            if (text && strlen(text) > 0) {
                // Skip whitespace-only strings
                const char* p = text;
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                if (*p) {
                    DocElement* text_elem = doc_create_text_cstr(arena, text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_item, text_elem);
                    }
                }
            }
        }
    }
    
    // Add last item
    if (current_item && current_item->first_child) {
        doc_append_child(list, current_item);
    }
}

// Build a list environment (itemize, enumerate, description)
static DocElement* build_list_environment(const char* env_name, const ElementReader& elem,
                                           Arena* arena, TexDocumentModel* doc) {
    DocElement* list = doc_alloc_element(arena, DocElemType::LIST);
    
    // Determine list type
    if (tag_eq(env_name, "itemize")) {
        list->list.list_type = ListType::ITEMIZE;
    } else if (tag_eq(env_name, "enumerate")) {
        list->list.list_type = ListType::ENUMERATE;
        list->list.start_num = 1;
    } else if (tag_eq(env_name, "description")) {
        list->list.list_type = ListType::DESCRIPTION;
    }
    
    int item_number = list->list.start_num;
    
    // Process children, looking for \item commands
    // Items may be directly under the environment or nested inside paragraph elements
    auto iter = elem.children();
    ItemReader child;
    DocElement* current_item = nullptr;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (!child_tag) continue;
            
            if (tag_eq(child_tag, "item")) {
                // Save previous item if exists
                if (current_item && current_item->first_child) {
                    doc_append_child(list, current_item);
                }
                // Start new item
                current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                if (list->list.list_type == ListType::ENUMERATE) {
                    current_item->list_item.item_number = item_number++;
                }
                // Process item children
                auto item_iter = child_elem.children();
                ItemReader item_child;
                while (item_iter.next(&item_child)) {
                    DocElement* content = build_doc_element(item_child, arena, doc);
                    if (content) {
                        doc_append_child(current_item, content);
                    }
                }
            } else if (tag_eq(child_tag, "paragraph") || tag_eq(child_tag, "text_mode") ||
                       tag_eq(child_tag, "content")) {
                // Items may be inside paragraph - process recursively
                process_list_content(list, child, arena, doc, item_number);
            }
        } else if (child.isString() && current_item) {
            // String content for current item
            const char* text = child.cstring();
            if (text && strlen(text) > 0) {
                const char* p = text;
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                if (*p) {
                    DocElement* text_elem = doc_create_text_cstr(arena, text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_item, text_elem);
                    }
                }
            }
        }
    }
    
    // Add last item
    if (current_item && current_item->first_child) {
        doc_append_child(list, current_item);
    }
    
    return list->first_child ? list : nullptr;
}

// Parse column alignment from column spec (e.g., "l|c|r")
static char get_column_alignment(const char* spec, int col_index) {
    if (!spec) return 'l';
    
    int col = 0;
    for (const char* p = spec; *p; p++) {
        if (*p == 'l' || *p == 'c' || *p == 'r' || *p == 'p') {
            if (col == col_index) {
                return *p;
            }
            col++;
        }
        // Skip modifiers like |, @{}, etc.
    }
    return 'l'; // default to left
}

// Count columns from column spec
static int count_columns_from_spec(const char* spec) {
    if (!spec) return 0;
    
    int count = 0;
    for (const char* p = spec; *p; p++) {
        if (*p == 'l' || *p == 'c' || *p == 'r' || *p == 'p') {
            count++;
        }
    }
    return count;
}

// Build a table environment
static DocElement* build_table_environment(const char* env_name, const ElementReader& elem,
                                            Arena* arena, TexDocumentModel* doc) {
    DocElement* table = doc_alloc_element(arena, DocElemType::TABLE);
    
    // Look for column spec in first argument
    auto iter = elem.children();
    ItemReader child;
    
    // First pass: find column spec
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            if (child_tag && (tag_eq(child_tag, "column_spec") || tag_eq(child_tag, "arg"))) {
                table->table.column_spec = extract_text_content(child, arena);
                table->table.num_columns = count_columns_from_spec(table->table.column_spec);
                break;
            }
        }
    }
    
    // Second pass: process rows
    DocElement* current_row = nullptr;
    DocElement* current_cell = nullptr;
    
    iter = elem.children();
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (!child_tag) continue;
            
            // Row separator (\\)
            if (tag_eq(child_tag, "row_sep") || tag_eq(child_tag, "newline") || 
                tag_eq(child_tag, "\\\\")) {
                if (current_cell) {
                    if (!current_row) {
                        current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
                    }
                    doc_append_child(current_row, current_cell);
                    current_cell = nullptr;
                }
                if (current_row && current_row->first_child) {
                    doc_append_child(table, current_row);
                }
                current_row = nullptr;
                continue;
            }
            
            // Cell separator (&)
            if (tag_eq(child_tag, "cell_sep") || tag_eq(child_tag, "ampersand") ||
                tag_eq(child_tag, "&")) {
                if (current_cell) {
                    if (!current_row) {
                        current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
                    }
                    doc_append_child(current_row, current_cell);
                }
                current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
                continue;
            }
            
            // Skip column spec
            if (tag_eq(child_tag, "column_spec") || tag_eq(child_tag, "arg")) {
                continue;
            }
        }
        
        // Content for current cell
        if (!current_row) {
            current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
        }
        if (!current_cell) {
            current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
        }
        
        DocElement* content = build_doc_element(child, arena, doc);
        if (content) {
            doc_append_child(current_cell, content);
        }
    }
    
    // Append final row/cell
    if (current_cell) {
        if (!current_row) {
            current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
        }
        doc_append_child(current_row, current_cell);
    }
    if (current_row && current_row->first_child) {
        doc_append_child(table, current_row);
    }
    
    return table->first_child ? table : nullptr;
}

// Build blockquote environment (quote, quotation)
static DocElement* build_blockquote_environment(const ElementReader& elem,
                                                 Arena* arena, TexDocumentModel* doc) {
    DocElement* quote = doc_alloc_element(arena, DocElemType::BLOCKQUOTE);
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        DocElement* child_elem = build_doc_element(child, arena, doc);
        if (child_elem) {
            doc_append_child(quote, child_elem);
        }
    }
    
    return quote->first_child ? quote : nullptr;
}

// Helper function to trim leading/trailing whitespace from a string
static const char* trim_whitespace(const char* str, Arena* arena) {
    if (!str) return nullptr;
    
    // Skip leading whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }
    
    // Find end, trimming trailing whitespace
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) {
        len--;
    }
    
    if (len == 0) return nullptr;
    
    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

// Helper function to trim leading whitespace only from a string
static const char* trim_leading_whitespace(const char* str, Arena* arena) {
    if (!str) return nullptr;
    
    // Skip leading whitespace
    const char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }
    
    if (*start == '\0') return nullptr;
    
    // If no trimming needed, return original
    if (start == str) return str;
    
    // Copy the trimmed string
    size_t len = strlen(start);
    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, start, len + 1);
    return result;
}

// Helper function to trim trailing whitespace only from a string
// Preserves space after ZWSP (U+200B = E2 80 8B) as it's meaningful output from space_cmd  
static const char* trim_trailing_whitespace(const char* str, Arena* arena) {
    if (!str) return nullptr;
    
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || str[len-1] == '\n' || str[len-1] == '\r')) {
        // Check if trimming would leave a ZWSP at the end - if so, keep the space
        // ZWSP in UTF-8 is E2 80 8B (3 bytes)
        if (len >= 4 && 
            (unsigned char)str[len-4] == 0xE2 &&
            (unsigned char)str[len-3] == 0x80 &&
            (unsigned char)str[len-2] == 0x8B &&
            str[len-1] == ' ') {
            // This is "ZWSP + space" pattern - don't trim the space
            break;
        }
        len--;
    }
    
    if (len == 0) return nullptr;
    
    // If no trimming needed, return original  
    if (len == strlen(str)) return str;
    
    char* result = (char*)arena_alloc(arena, len + 1);
    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

// Trim whitespace at paragraph boundaries:
// - Leading whitespace from first text element
// - Trailing whitespace from last text element  
// - Leading whitespace from text elements that follow SPACE (linebreak)
static void trim_paragraph_whitespace(DocElement* para, Arena* arena) {
    if (!para || !para->first_child) return;
    
    // Trim leading whitespace from first text element
    // Skip over whitespace-only TEXT_RUN elements
    DocElement* first = para->first_child;
    while (first && first->type == DocElemType::TEXT_RUN && first->text.text) {
        const char* trimmed = trim_leading_whitespace(first->text.text, arena);
        if (trimmed) {
            first->text.text = trimmed;
            first->text.text_len = strlen(trimmed);
            break;  // Successfully trimmed leading whitespace
        } else {
            // Text was all whitespace - mark as empty and try next element
            first->text.text = "";
            first->text.text_len = 0;
            first = first->next_sibling;
        }
    }
    
    // Trim trailing whitespace from last text element
    // Skip over whitespace-only TEXT_RUN elements from the end
    DocElement* last = para->last_child;
    while (last && last->type == DocElemType::TEXT_RUN && last->text.text) {
        const char* trimmed = trim_trailing_whitespace(last->text.text, arena);
        if (trimmed) {
            last->text.text = trimmed;
            last->text.text_len = strlen(trimmed);
            break;  // Successfully trimmed trailing whitespace
        } else {
            // Text was all whitespace - mark as empty and try previous element
            last->text.text = "";
            last->text.text_len = 0;
            // Find previous sibling (inefficient but rare case)
            DocElement* prev = nullptr;
            for (DocElement* c = para->first_child; c; c = c->next_sibling) {
                if (c->next_sibling == last) {
                    prev = c;
                    break;
                }
            }
            last = prev;
        }
    }
    
    // Trim leading whitespace from text elements that follow a linebreak
    DocElement* prev = nullptr;
    for (DocElement* child = para->first_child; child; child = child->next_sibling) {
        if (prev && prev->type == DocElemType::SPACE && prev->space.is_linebreak) {
            // Need to trim leading whitespace from elements after linebreak
            DocElement* curr = child;
            while (curr && curr->type == DocElemType::TEXT_RUN && curr->text.text) {
                const char* trimmed = trim_leading_whitespace(curr->text.text, arena);
                if (trimmed) {
                    curr->text.text = trimmed;
                    curr->text.text_len = strlen(trimmed);
                    break;
                } else {
                    curr->text.text = "";
                    curr->text.text_len = 0;
                    curr = curr->next_sibling;
                }
            }
        }
        prev = child;
    }
}

// Build alignment environment content with proper paragraph splitting
// Environment structure is: center -> paragraph -> [content with parbreaks]
static void build_alignment_content(DocElement* container, const ElementReader& elem,
                                     Arena* arena, TexDocumentModel* doc) {
    // First, look for paragraph children and process them
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (!child.isElement()) continue;
        
        ElementReader child_elem = child.asElement();
        const char* tag = child_elem.tagName();
        
        // Handle "paragraph" elements by processing their contents with parbreak detection
        if (tag && tag_eq(tag, "paragraph")) {
            DocElement* current_para = nullptr;
            
            auto para_iter = child_elem.children();
            ItemReader para_child;
            
            while (para_iter.next(&para_child)) {
                // Check for parbreak
                if (is_parbreak_item(para_child)) {
                    if (current_para && current_para->first_child) {
                        trim_paragraph_whitespace(current_para, arena);
                        doc_append_child(container, current_para);
                    }
                    current_para = nullptr;
                    continue;
                }
                
                // Start a new paragraph if needed
                if (!current_para) {
                    current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                }
                
                // Build the inline content
                DocElement* inline_elem = build_inline_content(para_child, arena, doc);
                if (inline_elem) {
                    doc_append_child(current_para, inline_elem);
                }
            }
            
            // Finalize the last paragraph
            if (current_para && current_para->first_child) {
                trim_paragraph_whitespace(current_para, arena);
                doc_append_child(container, current_para);
            }
        } else {
            // Non-paragraph children - process directly
            DocElement* child_doc = build_doc_element(child, arena, doc);
            if (child_doc && child_doc != PARBREAK_MARKER) {
                doc_append_child(container, child_doc);
            }
        }
    }
}

// Build alignment environment (center, flushleft, flushright)
// These produce a div with appropriate alignment class, containing paragraphs
static DocElement* build_alignment_environment(const char* env_name, const ElementReader& elem,
                                                Arena* arena, TexDocumentModel* doc) {
    // Create a container div with the alignment
    DocElement* container = doc_alloc_element(arena, DocElemType::ALIGNMENT);
    
    // Set alignment flag
    if (tag_eq(env_name, "center")) {
        container->flags |= DocElement::FLAG_CENTERED;
    } else if (tag_eq(env_name, "flushleft")) {
        container->flags |= DocElement::FLAG_FLUSH_LEFT;
    } else if (tag_eq(env_name, "flushright")) {
        container->flags |= DocElement::FLAG_FLUSH_RIGHT;
    }
    
    // Process children with paragraph splitting on parbreaks
    build_alignment_content(container, elem, arena, doc);
    
    return container->first_child ? container : nullptr;
}

// Build code block environment (verbatim, lstlisting)
// Helper to collect all text content recursively
static void collect_text_recursive(const ItemReader& item, StrBuf* buf) {
    if (item.isString()) {
        const char* text = item.cstring();
        if (text) {
            strbuf_append_str(buf, text);
        }
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        const char* tag = elem.tagName();
        // Skip optional arguments
        if (tag && tag_eq(tag, "optional")) {
            return;
        }
        // Recurse into children
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            collect_text_recursive(child, buf);
        }
    }
}

static DocElement* build_code_block_environment(const char* env_name, const ElementReader& elem,
                                                 Arena* arena, TexDocumentModel* doc) {
    (void)doc; // unused
    (void)env_name; // unused
    DocElement* code = doc_alloc_element(arena, DocElemType::CODE_BLOCK);
    code->text.text = nullptr;
    code->text.text_len = 0;
    code->text.style = DocTextStyle::plain();
    
    // Collect all text content from children recursively
    StrBuf* buf = strbuf_new_cap(256);
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        collect_text_recursive(child, buf);
    }
    
    if (buf->length > 0) {
        char* text_copy = (char*)arena_alloc(arena, buf->length + 1);
        memcpy(text_copy, buf->str, buf->length + 1);
        code->text.text = text_copy;
        code->text.text_len = buf->length;
    }
    
    strbuf_free(buf);
    return code;
}

// ============================================================================
// Phase E: Image, Link, Figure, and Cross-Reference Builders
// ============================================================================

// Parse dimension value (e.g., "10cm", "100pt", "0.5\textwidth")
static float parse_dimension(const char* value, Arena* arena) {
    (void)arena;
    if (!value) return 0.0f;
    
    // Try to parse as a simple number
    char* end = nullptr;
    float num = strtof(value, &end);
    
    if (end == value) return 0.0f; // no number found
    
    // Handle units
    if (end && *end) {
        // Skip whitespace
        while (*end == ' ') end++;
        
        // Convert common units to pixels (approximate)
        if (strncmp(end, "pt", 2) == 0) {
            return num * 1.333f;  // 1pt ≈ 1.333px
        } else if (strncmp(end, "cm", 2) == 0) {
            return num * 37.795f; // 1cm ≈ 37.795px
        } else if (strncmp(end, "mm", 2) == 0) {
            return num * 3.7795f; // 1mm ≈ 3.7795px
        } else if (strncmp(end, "in", 2) == 0) {
            return num * 96.0f;   // 1in = 96px
        } else if (strncmp(end, "px", 2) == 0) {
            return num;
        } else if (strncmp(end, "em", 2) == 0) {
            return num * 16.0f;   // assume 1em = 16px
        }
        // textwidth, linewidth - return as percentage approximation
        if (strstr(end, "textwidth") || strstr(end, "linewidth")) {
            return num * 600.0f;  // assume textwidth ~600px
        }
    }
    
    return num;
}

// Parse graphics options like [width=10cm, height=5cm]
static void parse_graphics_options(const char* opts, float* width, float* height, Arena* arena) {
    if (!opts) return;
    
    *width = 0.0f;
    *height = 0.0f;
    
    // Look for width=...
    const char* w = strstr(opts, "width=");
    if (w) {
        w += 6; // skip "width="
        // Find end of value (comma or end of string)
        const char* end = w;
        while (*end && *end != ',' && *end != ']' && *end != ' ') end++;
        size_t len = end - w;
        char* val = (char*)arena_alloc(arena, len + 1);
        memcpy(val, w, len);
        val[len] = '\0';
        *width = parse_dimension(val, arena);
    }
    
    // Look for height=...
    const char* h = strstr(opts, "height=");
    if (h) {
        h += 7; // skip "height="
        const char* end = h;
        while (*end && *end != ',' && *end != ']' && *end != ' ') end++;
        size_t len = end - h;
        char* val = (char*)arena_alloc(arena, len + 1);
        memcpy(val, h, len);
        val[len] = '\0';
        *height = parse_dimension(val, arena);
    }
}

// Build image command (\includegraphics)
static DocElement* build_image_command(const ElementReader& elem, Arena* arena,
                                        TexDocumentModel* doc) {
    (void)doc;
    DocElement* img = doc_alloc_element(arena, DocElemType::IMAGE);
    img->image.src = nullptr;
    img->image.width = 0.0f;
    img->image.height = 0.0f;
    img->image.alt = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag) {
                // Optional arguments contain width/height
                if (tag_eq(child_tag, "optional") || tag_eq(child_tag, "brack_group")) {
                    const char* opts = extract_text_content(child, arena);
                    parse_graphics_options(opts, &img->image.width, &img->image.height, arena);
                }
                // Required argument is the file path
                else if (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg") ||
                         tag_eq(child_tag, "path")) {
                    img->image.src = extract_text_content(child, arena);
                }
            }
        } else if (child.isString()) {
            // Could be the path directly
            if (!img->image.src) {
                img->image.src = child.cstring();
            }
        }
    }
    
    return img;
}

// Build href command (\href{url}{text})
static DocElement* build_href_command(const ElementReader& elem, Arena* arena,
                                       TexDocumentModel* doc) {
    (void)doc;
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = nullptr;
    link->link.link_text = nullptr;
    
    int arg_index = 0;
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                const char* text = extract_text_content(child, arena);
                if (arg_index == 0) {
                    link->link.href = text;  // first arg is URL
                } else {
                    link->link.link_text = text;  // second arg is display text
                }
                arg_index++;
            }
        } else if (child.isString()) {
            // Direct string children (tree-sitter output format)
            const char* text = child.cstring();
            if (text && strlen(text) > 0 && text[0] != '\n') {
                // Copy the string
                size_t len = strlen(text);
                char* str = (char*)arena_alloc(arena, len + 1);
                memcpy(str, text, len + 1);
                
                if (arg_index == 0) {
                    link->link.href = str;  // first string is URL
                } else {
                    link->link.link_text = str;  // second string is display text
                }
                arg_index++;
            }
        }
    }
    
    return link;
}

// Build url command (\url{...})
static DocElement* build_url_command(const ElementReader& elem, Arena* arena,
                                      TexDocumentModel* doc) {
    (void)doc;
    DocElement* link = doc_alloc_element(arena, DocElemType::LINK);
    link->link.href = nullptr;
    link->link.link_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                const char* url = extract_text_content(child, arena);
                link->link.href = url;
                link->link.link_text = url;  // Display URL as link text
            }
        } else if (child.isString()) {
            const char* url = child.cstring();
            if (url) {
                link->link.href = url;
                link->link.link_text = url;
            }
        }
    }
    
    return link;
}

// Build label command (\label{...})
static void process_label_command(const ElementReader& elem, Arena* arena,
                                   TexDocumentModel* doc, DocElement* parent) {
    const char* label = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                label = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            label = child.cstring();
        }
    }
    
    if (label) {
        doc->add_label(label, nullptr, -1);
        
        // If parent is a heading, set the label on it
        if (parent && parent->type == DocElemType::HEADING) {
            parent->heading.label = label;
        }
    }
}

// Build ref command (\ref{...})
static DocElement* build_ref_command(const ElementReader& elem, Arena* arena,
                                      TexDocumentModel* doc) {
    DocElement* ref = doc_alloc_element(arena, DocElemType::CROSS_REF);
    ref->ref.ref_label = nullptr;
    ref->ref.ref_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                ref->ref.ref_label = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            ref->ref.ref_label = child.cstring();
        }
    }
    
    // Try to resolve the reference
    if (ref->ref.ref_label) {
        const char* resolved = doc->resolve_ref(ref->ref.ref_label);
        if (resolved) {
            ref->ref.ref_text = resolved;
        } else {
            // Unresolved reference - show ?? for now
            ref->ref.ref_text = "??";
        }
    }
    
    return ref;
}

// Build figure environment
static DocElement* build_figure_environment(const ElementReader& elem, Arena* arena,
                                             TexDocumentModel* doc) {
    DocElement* fig = doc_alloc_element(arena, DocElemType::FIGURE);
    fig->flags |= DocElement::FLAG_NUMBERED;
    
    // Track caption and label for this figure
    const char* caption_text = nullptr;
    const char* label = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (!child.isElement()) continue;
        
        ElementReader child_elem = child.asElement();
        const char* child_tag = child_elem.tagName();
        
        if (!child_tag) continue;
        
        // Handle caption
        if (tag_eq(child_tag, "caption")) {
            auto caption_iter = child_elem.children();
            ItemReader caption_child;
            while (caption_iter.next(&caption_child)) {
                if (caption_child.isElement()) {
                    ElementReader cc_elem = caption_child.asElement();
                    const char* cc_tag = cc_elem.tagName();
                    if (cc_tag && (tag_eq(cc_tag, "curly_group") || tag_eq(cc_tag, "arg"))) {
                        caption_text = extract_text_content(caption_child, arena);
                    }
                }
            }
        }
        // Handle label
        else if (tag_eq(child_tag, "label")) {
            auto label_iter = child_elem.children();
            ItemReader label_child;
            while (label_iter.next(&label_child)) {
                if (label_child.isElement()) {
                    ElementReader lc_elem = label_child.asElement();
                    const char* lc_tag = lc_elem.tagName();
                    if (lc_tag && (tag_eq(lc_tag, "curly_group") || tag_eq(lc_tag, "arg"))) {
                        label = extract_text_content(label_child, arena);
                    }
                } else if (label_child.isString()) {
                    label = label_child.cstring();
                }
            }
        }
        // Handle centering (skip)
        else if (tag_eq(child_tag, "centering")) {
            fig->flags |= DocElement::FLAG_CENTERED;
        }
        // Handle includegraphics
        else if (tag_eq(child_tag, "includegraphics")) {
            DocElement* img = build_image_command(child_elem, arena, doc);
            if (img) {
                doc_append_child(fig, img);
            }
        }
        // Other content
        else {
            DocElement* content = build_doc_element(child, arena, doc);
            if (content) {
                doc_append_child(fig, content);
            }
        }
    }
    
    // Add caption element if present
    if (caption_text) {
        // Create figcaption structure
        DocElement* caption_elem = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        caption_elem->text.style = DocTextStyle::plain();
        
        // Format caption with figure number
        // Note: In a full implementation, we'd track figure numbering per chapter
        static int figure_num = 0;
        figure_num++;
        
        char* formatted = (char*)arena_alloc(arena, strlen(caption_text) + 32);
        sprintf(formatted, "Figure %d: %s", figure_num, caption_text);
        caption_elem->text.text = formatted;
        caption_elem->text.text_len = strlen(formatted);
        
        doc_append_child(fig, caption_elem);
        
        // Register label if present
        if (label) {
            char num_str[16];
            sprintf(num_str, "%d", figure_num);
            doc->add_label(label, num_str, -1);
        }
    }
    
    return fig;
}

// Build footnote command (\footnote{...})
static DocElement* build_footnote_command(const ElementReader& elem, Arena* arena,
                                           TexDocumentModel* doc) {
    (void)doc;
    DocElement* fn = doc_alloc_element(arena, DocElemType::FOOTNOTE);
    
    static int footnote_num = 0;
    footnote_num++;
    fn->footnote.footnote_number = footnote_num;
    
    // Process footnote content
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                DocElement* content = build_doc_element(child, arena, doc);
                if (content) {
                    doc_append_child(fn, content);
                }
            }
        }
    }
    
    return fn;
}

// Build cite command (\cite{...})
static DocElement* build_cite_command(const ElementReader& elem, Arena* arena,
                                       TexDocumentModel* doc) {
    DocElement* cite = doc_alloc_element(arena, DocElemType::CITATION);
    cite->citation.key = nullptr;
    cite->citation.cite_text = nullptr;
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                cite->citation.key = extract_text_content(child, arena);
            }
        } else if (child.isString()) {
            cite->citation.key = child.cstring();
        }
    }
    
    // Try to resolve citation
    if (cite->citation.key) {
        const char* resolved = doc->resolve_cite(cite->citation.key);
        if (resolved) {
            cite->citation.cite_text = resolved;
        } else {
            // Unresolved citation - format as [key]
            size_t len = strlen(cite->citation.key) + 3;
            char* text = (char*)arena_alloc(arena, len);
            sprintf(text, "[%s]", cite->citation.key);
            cite->citation.cite_text = text;
        }
    }
    
    return cite;
}

// Helper function to check if an element should be collected into a paragraph
// vs treated as a block element (like sections, lists, etc.)
// Note: Uses the already-defined is_inline_element() function in the HTML rendering section.
// This version adds support for PARBREAK_MARKER sentinel.
static bool is_inline_or_break(DocElement* elem) {
    if (!elem) return false;
    if (elem == PARBREAK_MARKER || elem == LINEBREAK_MARKER) return false;
    return is_inline_element(elem);
}

// Helper function to process body content with paragraph grouping
// Collects inline content into paragraphs, respecting parbreak markers
static void build_body_content_with_paragraphs(DocElement* container, const ElementReader& elem,
                                                Arena* arena, TexDocumentModel* doc) {
    DocElement* current_para = nullptr;
    bool after_block_element = false;  // Track if the previous element was a block
    
    int64_t child_count = elem.childCount();
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child_item = elem.childAt(i);
        DocElement* child_elem = build_doc_element(child_item, arena, doc);
        
        if (!child_elem) continue;
        
        // Paragraph break marker - finalize current paragraph and start new one
        if (child_elem == PARBREAK_MARKER) {
            if (current_para && current_para->first_child) {
                trim_paragraph_whitespace(current_para, arena);
                doc_append_child(container, current_para);
            }
            current_para = nullptr;
            // Note: parbreak resets after_block_element since it's a new paragraph context
            after_block_element = false;
            continue;
        }
        
        // Check if this is inline content or a block element
        if (is_inline_or_break(child_elem)) {
            // Start a new paragraph if needed
            if (!current_para) {
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                // Mark as continue if this paragraph follows a block element
                if (after_block_element) {
                    current_para->flags |= DocElement::FLAG_CONTINUE;
                    after_block_element = false;
                }
            }
            doc_append_child(current_para, child_elem);
        } else {
            // Block element - finalize current paragraph first
            if (current_para && current_para->first_child) {
                trim_paragraph_whitespace(current_para, arena);
                doc_append_child(container, current_para);
                current_para = nullptr;
            }
            doc_append_child(container, child_elem);
            // Mark that we just added a block element
            after_block_element = true;
        }
    }
    
    // Finalize any remaining paragraph
    if (current_para && current_para->first_child) {
        trim_paragraph_whitespace(current_para, arena);
        doc_append_child(container, current_para);
    }
}

// Main builder - converts LaTeX AST item to DocElement
static DocElement* build_doc_element(const ItemReader& item, Arena* arena,
                                      TexDocumentModel* doc) {
    // Check for paragraph break marker (parbreak symbol)
    if (is_parbreak_item(item)) {
        return PARBREAK_MARKER;
    }
    
    // Check for line break command (\\ or \newline)
    if (is_linebreak_item(item)) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = true;
        return space;
    }
    
    if (item.isString()) {
        const char* text = item.cstring();
        if (text && strlen(text) > 0) {
            // Create a text run for bare strings with normalized whitespace
            return doc_create_text_normalized(arena, text, DocTextStyle::plain());
        }
        return nullptr;
    }
    
    if (!item.isElement()) {
        return nullptr;
    }
    
    ElementReader elem = item.asElement();
    const char* tag = elem.tagName();
    
    if (!tag) return nullptr;
    
    // Helper lambda to check if element has paragraph children (indicates environment usage)
    auto has_paragraph_children = [&elem]() -> bool {
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && tag_eq(child_tag, "paragraph")) {
                    return true;
                }
            }
        }
        return false;
    };
    
    // Text formatting commands (not environments - those with paragraph children get different handling)
    // Commands like \textbf{...} vs environments like \begin{bfseries}...\end{bfseries}
    bool is_font_tag = (tag_eq(tag, "textbf") || tag_eq(tag, "textit") || tag_eq(tag, "texttt") ||
        tag_eq(tag, "emph") || tag_eq(tag, "textsc") || tag_eq(tag, "underline") ||
        tag_eq(tag, "bf") || tag_eq(tag, "it") || tag_eq(tag, "tt") ||
        tag_eq(tag, "bfseries") || tag_eq(tag, "itshape") || tag_eq(tag, "ttfamily") ||
        tag_eq(tag, "scshape") || tag_eq(tag, "sout") || tag_eq(tag, "st") ||
        // Font size commands
        tag_eq(tag, "tiny") || tag_eq(tag, "scriptsize") || tag_eq(tag, "footnotesize") ||
        tag_eq(tag, "small") || tag_eq(tag, "normalsize") || tag_eq(tag, "large") ||
        tag_eq(tag, "Large") || tag_eq(tag, "LARGE") || tag_eq(tag, "huge") || tag_eq(tag, "Huge"));
    
    // Use text command handler only if NOT an environment (no paragraph children)
    if (is_font_tag && !has_paragraph_children()) {
        return build_text_command(tag, elem, arena, doc);
    }
    
    // Symbol commands parsed directly as element tags (e.g., {"$":"textellipsis"})
    // Ellipsis
    if (tag_eq(tag, "textellipsis") || tag_eq(tag, "ldots") || tag_eq(tag, "dots")) {
        return doc_create_text_cstr(arena, "\xE2\x80\xA6", DocTextStyle::plain());  // …
    }
    // En-dash and em-dash
    if (tag_eq(tag, "textendash")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x93", DocTextStyle::plain());  // –
    }
    if (tag_eq(tag, "textemdash")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x94", DocTextStyle::plain());  // —
    }
    // LaTeX/TeX logos
    if (tag_eq(tag, "LaTeX")) {
        return doc_create_text_cstr(arena, "LaTeX", DocTextStyle::plain());
    }
    if (tag_eq(tag, "TeX")) {
        return doc_create_text_cstr(arena, "TeX", DocTextStyle::plain());
    }
    // Special characters
    if (tag_eq(tag, "textbackslash")) {
        return doc_create_text_cstr(arena, "\\", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textasciitilde")) {
        return doc_create_text_cstr(arena, "~", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textasciicircum")) {
        return doc_create_text_cstr(arena, "^", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textbar")) {
        return doc_create_text_cstr(arena, "|", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textless")) {
        return doc_create_text_cstr(arena, "<", DocTextStyle::plain());
    }
    if (tag_eq(tag, "textgreater")) {
        return doc_create_text_cstr(arena, ">", DocTextStyle::plain());
    }
    // Quotation marks
    if (tag_eq(tag, "textquoteleft")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x98", DocTextStyle::plain());  // '
    }
    if (tag_eq(tag, "textquoteright")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x99", DocTextStyle::plain());  // '
    }
    if (tag_eq(tag, "textquotedblleft")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x9C", DocTextStyle::plain());  // "
    }
    if (tag_eq(tag, "textquotedblright")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x9D", DocTextStyle::plain());  // "
    }
    // Copyright/trademark
    if (tag_eq(tag, "copyright") || tag_eq(tag, "textcopyright")) {
        return doc_create_text_cstr(arena, "\xC2\xA9", DocTextStyle::plain());  // ©
    }
    if (tag_eq(tag, "trademark") || tag_eq(tag, "texttrademark")) {
        return doc_create_text_cstr(arena, "\xE2\x84\xA2", DocTextStyle::plain());  // ™
    }
    if (tag_eq(tag, "textregistered")) {
        return doc_create_text_cstr(arena, "\xC2\xAE", DocTextStyle::plain());  // ®
    }
    // Spacing commands that output space
    if (tag_eq(tag, "quad") || tag_eq(tag, "qquad") || 
        tag_eq(tag, "enspace") || tag_eq(tag, "enskip") ||
        tag_eq(tag, "thinspace")) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = false;
        return space;
    }
    // Non-breaking space
    if (tag_eq(tag, "nobreakspace") || tag_eq(tag, "nbsp")) {
        return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
    }
    
    // Paragraph break command (\par)
    if (tag_eq(tag, "par")) {
        return PARBREAK_MARKER;
    }
    
    // Line break commands (\\ and \newline)
    if (tag_eq(tag, "linebreak_command") || tag_eq(tag, "newline")) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = true;
        return space;
    }
    
    // Space command - handles \<space>, \<tab>, and \<newline>
    // All produce ZWSP (as boundary marker) + space
    if (tag_eq(tag, "space_cmd")) {
        // All space commands produce ZWSP followed by space
        // The ZWSP marks a boundary/break point, the space is the visible character
        return doc_create_text_cstr(arena, "\xE2\x80\x8B ", DocTextStyle::plain());
    }
    
    // Section commands (but not paragraph element containing content)
    if (tag_eq(tag, "section") || tag_eq(tag, "subsection") || 
        tag_eq(tag, "subsubsection") || tag_eq(tag, "chapter") || tag_eq(tag, "part")) {
        return build_section_command(tag, elem, arena, doc);
    }
    
    // Handle "paragraph" tag - could be \paragraph{} command or content paragraph
    // \paragraph{} command: children are text for title
    // Content paragraph: children include elements like display_math, inline_math, etc.
    if (tag_eq(tag, "paragraph")) {
        // Check if any child is an element (not just text)
        bool has_element_children = false;
        auto check_iter = elem.children();
        ItemReader check_child;
        while (check_iter.next(&check_child)) {
            if (check_child.isElement()) {
                has_element_children = true;
                break;
            }
        }
        
        if (has_element_children) {
            // Content paragraph - process like paragraph_content
            return build_paragraph(elem, arena, doc);
        } else {
            // \paragraph{} sectioning command
            return build_section_command(tag, elem, arena, doc);
        }
    }
    
    // Paragraph
    if (tag_eq(tag, "paragraph_content") || tag_eq(tag, "text_mode")) {
        return build_paragraph(elem, arena, doc);
    }
    
    // Display math
    if (tag_eq(tag, "display_math") || tag_eq(tag, "equation") || 
        tag_eq(tag, "equation*") || tag_eq(tag, "displaymath")) {
        DocElement* math = doc_alloc_element(arena, DocElemType::MATH_DISPLAY);
        math->math.latex_src = extract_math_source(elem, arena);
        math->math.node = nullptr;
        return math;
    }
    
    // Inline math
    if (tag_eq(tag, "inline_math") || tag_eq(tag, "math")) {
        DocElement* math = doc_alloc_element(arena, DocElemType::MATH_INLINE);
        math->math.latex_src = extract_math_source(elem, arena);
        math->math.node = nullptr;
        return math;
    }
    
    // List environments
    if (tag_eq(tag, "itemize") || tag_eq(tag, "enumerate") || tag_eq(tag, "description")) {
        return build_list_environment(tag, elem, arena, doc);
    }
    
    // Table environments
    if (tag_eq(tag, "tabular") || tag_eq(tag, "tabular*") || tag_eq(tag, "array")) {
        return build_table_environment(tag, elem, arena, doc);
    }
    
    // Quote environments
    if (tag_eq(tag, "quote") || tag_eq(tag, "quotation")) {
        return build_blockquote_environment(elem, arena, doc);
    }
    
    // Code environments
    if (tag_eq(tag, "verbatim") || tag_eq(tag, "lstlisting") || tag_eq(tag, "listing")) {
        return build_code_block_environment(tag, elem, arena, doc);
    }
    
    // Alignment environments (center, flushleft, flushright)
    if (tag_eq(tag, "center") || tag_eq(tag, "flushleft") || tag_eq(tag, "flushright")) {
        return build_alignment_environment(tag, elem, arena, doc);
    }
    
    // Phase E: Image, Link, Figure, Cross-Reference Commands
    
    // Image command (\includegraphics)
    if (tag_eq(tag, "includegraphics")) {
        return build_image_command(elem, arena, doc);
    }
    
    // Link commands (\href, \url)
    if (tag_eq(tag, "href")) {
        return build_href_command(elem, arena, doc);
    }
    if (tag_eq(tag, "url")) {
        return build_url_command(elem, arena, doc);
    }
    
    // Figure environment
    if (tag_eq(tag, "figure") || tag_eq(tag, "figure*")) {
        return build_figure_environment(elem, arena, doc);
    }
    
    // Cross-reference commands
    if (tag_eq(tag, "label")) {
        // Labels don't produce visible output, but register with doc
        process_label_command(elem, arena, doc, nullptr);
        return nullptr;
    }
    if (tag_eq(tag, "ref") || tag_eq(tag, "eqref") || tag_eq(tag, "pageref")) {
        return build_ref_command(elem, arena, doc);
    }
    
    // Footnote command
    if (tag_eq(tag, "footnote")) {
        return build_footnote_command(elem, arena, doc);
    }
    
    // Citation command
    if (tag_eq(tag, "cite") || tag_eq(tag, "citep") || tag_eq(tag, "citet")) {
        return build_cite_command(elem, arena, doc);
    }
    
    // Document root
    if (tag_eq(tag, "latex_document") || tag_eq(tag, "document")) {
        DocElement* doc_elem = doc_alloc_element(arena, DocElemType::DOCUMENT);
        
        // Process children with paragraph grouping
        build_body_content_with_paragraphs(doc_elem, elem, arena, doc);
        
        return doc_elem;
    }
    
    // Document body (between \begin{document} and \end{document})
    if (tag_eq(tag, "document_body") || tag_eq(tag, "body")) {
        // Process children with paragraph grouping
        DocElement* container = doc_alloc_element(arena, DocElemType::SECTION);
        build_body_content_with_paragraphs(container, elem, arena, doc);
        return container->first_child ? container : nullptr;
    }
    
    // Comment environment - completely ignored (returns nothing)
    if (tag_eq(tag, "comment")) {
        return nullptr;
    }
    
    // Empty - handles both \empty command and \begin{empty}...\end{empty} environment
    // - \empty command (no meaningful children): produces no output, consumes trailing space
    // - \begin{empty}...\end{empty} environment (has children): inline pass-through with ZWSP at end boundary
    if (tag_eq(tag, "empty")) {
        // Check if this is the \empty command (no children or only empty group)
        // vs the \begin{empty}...\end{empty} environment (has actual content children)
        int64_t child_count = elem.childCount();
        bool has_content_children = false;
        for (int64_t i = 0; i < child_count && !has_content_children; i++) {
            ItemReader ch = elem.childAt(i);
            if (ch.isElement()) {
                ElementReader ch_elem = ch.asElement();
                const char* ch_tag = ch_elem.tagName();
                // paragraph or text children indicate environment usage
                if (ch_tag && (tag_eq(ch_tag, "paragraph") || tag_eq(ch_tag, "text"))) {
                    // Check if paragraph has any actual content
                    if (ch_elem.childCount() > 0) {
                        has_content_children = true;
                    }
                }
            } else if (ch.isString()) {
                const char* s = ch.cstring();
                if (s && strlen(s) > 0) {
                    // Non-empty string child - has content
                    has_content_children = true;
                }
            }
        }
        
        if (!has_content_children) {
            // Check if this is \empty{} (with braces) or plain \empty (no braces)
            // \empty{} should produce ZWSP to mark boundary (force space after macro)
            // \empty should produce nothing (consumes trailing space)
            bool has_braces = false;
            for (int64_t i = 0; i < child_count; i++) {
                ItemReader ch = elem.childAt(i);
                if (ch.isElement()) {
                    ElementReader ch_elem = ch.asElement();
                    const char* ch_tag = ch_elem.tagName();
                    if (ch_tag && tag_eq(ch_tag, "curly_group")) {
                        has_braces = true;
                        break;
                    }
                }
            }
            
            if (has_braces) {
                // \empty{} - produce ZWSP to mark boundary
                return doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain());  // ZWSP only
            }
            // Plain \empty - produce nothing (consumes trailing space)
            return nullptr;
        }
        
        // \begin{empty}...\end{empty} environment - inline pass-through with ZWSP at end
        DocElement* container = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        container->text.style = DocTextStyle::plain();
        
        // Process children - may be paragraphs or direct content
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                
                if (child_tag && (tag_eq(child_tag, "paragraph") || tag_eq(child_tag, "text"))) {
                    // Process paragraph/text content inline
                    auto para_iter = child_elem.children();
                    ItemReader para_child;
                    while (para_iter.next(&para_child)) {
                        // Recursively process - may contain nested empty environments
                        DocElement* para_elem = build_doc_element(para_child, arena, doc);
                        if (para_elem) {
                            doc_append_child(container, para_elem);
                        }
                    }
                } else {
                    // Other elements - process directly
                    DocElement* elem_result = build_doc_element(child, arena, doc);
                    if (elem_result) {
                        doc_append_child(container, elem_result);
                    }
                }
            } else if (child.isString()) {
                const char* text = child.cstring();
                if (text && strlen(text) > 0) {
                    DocElement* text_elem = doc_create_text_normalized(arena, text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(container, text_elem);
                    }
                }
            }
        }
        
        // Add zero-width space at the end boundary (where \end{empty} was)
        DocElement* end_zwsp = doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain());  // U+200B ZWSP
        doc_append_child(container, end_zwsp);
        
        return container;
    }
    
    // Font environments - inline styling with zero-width space boundaries
    // These are environments like \begin{small}...\end{small} that act like font commands
    // Expected output pattern:
    //   <span class="X">​ </span>   (ZWSP at start)
    //   <span class="X">content</span> (styled content)
    //   <span class="X">​ </span>   (ZWSP at end)
    // Nested environments recursively apply the same pattern
    if (tag_eq(tag, "small") || tag_eq(tag, "normalsize") || tag_eq(tag, "large") ||
        tag_eq(tag, "Large") || tag_eq(tag, "LARGE") || tag_eq(tag, "huge") || tag_eq(tag, "Huge") ||
        tag_eq(tag, "tiny") || tag_eq(tag, "scriptsize") || tag_eq(tag, "footnotesize") ||
        tag_eq(tag, "bfseries") || tag_eq(tag, "itshape") || tag_eq(tag, "ttfamily") ||
        tag_eq(tag, "scshape") || tag_eq(tag, "upshape") || tag_eq(tag, "rmfamily") ||
        tag_eq(tag, "sffamily") || tag_eq(tag, "mdseries") || tag_eq(tag, "slshape")) {
        
        DocElement* container = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        container->text.style = DocTextStyle::plain();  // Container has no styling
        
        // Add ZWSP span at start of environment
        DocElement* start_zwsp_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        build_text_command_set_style(tag, &start_zwsp_span->text.style);
        DocElement* start_zwsp = doc_create_text_cstr(arena, "\xE2\x80\x8B ", DocTextStyle::plain());  // ZWSP + space
        doc_append_child(start_zwsp_span, start_zwsp);
        doc_append_child(container, start_zwsp_span);
        
        // Accumulate text into a single span, then flush when encountering nested env or end
        StrBuf* text_accum = strbuf_new();
        
        // Helper to flush accumulated text into a styled span
        // Normalizes to: strip leading ws, collapse internal ws, ensure trailing space separator
        auto flush_text = [&]() {
            if (text_accum->length > 0) {
                const char* accumulated = text_accum->str;
                size_t len = text_accum->length;
                
                // Normalize: strip leading ws, collapse internal ws
                char* buf = (char*)arena_alloc(arena, len + 2);  // +2 for potential trailing space + null
                char* out = buf;
                
                bool in_ws = false;
                bool content_started = false;
                
                for (size_t i = 0; i < len; i++) {
                    char c = accumulated[i];
                    bool is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
                    
                    if (is_ws) {
                        if (content_started && !in_ws) {
                            // Internal whitespace - collapse to single space
                            *out++ = ' ';
                        }
                        in_ws = true;
                    } else {
                        content_started = true;
                        in_ws = false;
                        *out++ = c;
                    }
                }
                
                // If we have content and it doesn't already end with space, add trailing space
                // (This ensures proper word separation when adjacent to other elements)
                if (out > buf && out[-1] != ' ') {
                    *out++ = ' ';
                }
                *out = '\0';
                
                size_t result_len = out - buf;
                if (result_len > 0) {
                    DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                    build_text_command_set_style(tag, &styled_span->text.style);
                    DocElement* text_elem = doc_create_text_cstr(arena, buf, DocTextStyle::plain());
                    doc_append_child(styled_span, text_elem);
                    doc_append_child(container, styled_span);
                }
                strbuf_reset(text_accum);
            }
        };
        
        // Helper to check if tag is a font environment
        auto is_font_env_tag = [](const char* t) -> bool {
            return t && (
                tag_eq(t, "small") || tag_eq(t, "normalsize") || 
                tag_eq(t, "large") || tag_eq(t, "Large") || 
                tag_eq(t, "LARGE") || tag_eq(t, "huge") || 
                tag_eq(t, "Huge") || tag_eq(t, "tiny") || 
                tag_eq(t, "scriptsize") || tag_eq(t, "footnotesize") ||
                tag_eq(t, "bfseries") || tag_eq(t, "itshape") || 
                tag_eq(t, "ttfamily") || tag_eq(t, "scshape") || 
                tag_eq(t, "upshape") || tag_eq(t, "rmfamily") ||
                tag_eq(t, "sffamily") || tag_eq(t, "mdseries") || 
                tag_eq(t, "slshape"));
        };
        
        // Process children - unwrap paragraphs and handle content inline
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                
                // Unwrap paragraph/text elements - process their children
                if (child_tag && (tag_eq(child_tag, "paragraph") || tag_eq(child_tag, "text"))) {
                    auto para_iter = child_elem.children();
                    ItemReader para_child;
                    while (para_iter.next(&para_child)) {
                        if (para_child.isElement()) {
                            ElementReader nested_elem = para_child.asElement();
                            const char* nested_tag = nested_elem.tagName();
                            
                            if (is_font_env_tag(nested_tag)) {
                                // Flush accumulated text before nested font env
                                flush_text();
                                // Recurse - this creates the nested font env's ZWSP structure
                                DocElement* nested_result = build_doc_element(para_child, arena, doc);
                                if (nested_result) {
                                    doc_append_child(container, nested_result);
                                }
                            } else {
                                // Other elements - flush text and process
                                flush_text();
                                DocElement* elem_result = build_doc_element(para_child, arena, doc);
                                if (elem_result) {
                                    DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                                    build_text_command_set_style(tag, &styled_span->text.style);
                                    doc_append_child(styled_span, elem_result);
                                    doc_append_child(container, styled_span);
                                }
                            }
                        } else if (para_child.isString()) {
                            // Accumulate text
                            const char* text = para_child.cstring();
                            if (text) {
                                strbuf_append_str(text_accum, text);
                            }
                        }
                    }
                } else if (is_font_env_tag(child_tag)) {
                    // Font environment as direct child
                    flush_text();
                    DocElement* nested_result = build_doc_element(child, arena, doc);
                    if (nested_result) {
                        doc_append_child(container, nested_result);
                    }
                } else {
                    // Non-paragraph element - process directly with style
                    flush_text();
                    DocElement* elem_result = build_doc_element(child, arena, doc);
                    if (elem_result) {
                        DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                        build_text_command_set_style(tag, &styled_span->text.style);
                        doc_append_child(styled_span, elem_result);
                        doc_append_child(container, styled_span);
                    }
                }
            } else if (child.isString()) {
                // Accumulate text
                const char* text = child.cstring();
                if (text) {
                    strbuf_append_str(text_accum, text);
                }
            }
        }
        
        // Flush any remaining accumulated text
        flush_text();
        strbuf_free(text_accum);
        
        // Add ZWSP span at end of environment
        DocElement* end_zwsp_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        build_text_command_set_style(tag, &end_zwsp_span->text.style);
        DocElement* end_zwsp = doc_create_text_cstr(arena, "\xE2\x80\x8B ", DocTextStyle::plain());  // ZWSP + space
        doc_append_child(end_zwsp_span, end_zwsp);
        doc_append_child(container, end_zwsp_span);
        
        return container;
    }
    
    // Generic element - recurse to children with paragraph grouping
    DocElement* container = doc_alloc_element(arena, DocElemType::SECTION);
    build_body_content_with_paragraphs(container, elem, arena, doc);
    
    // If only one child element, return it directly instead of wrapped in SECTION
    if (container->first_child && container->first_child == container->last_child) {
        DocElement* only_child = container->first_child;
        only_child->parent = nullptr;
        only_child->next_sibling = nullptr;
        return only_child;
    }
    
    return container->first_child ? container : nullptr;
}

// ============================================================================
// Main API: LaTeX AST to Document Model
// ============================================================================

TexDocumentModel* doc_model_from_latex(Item elem, Arena* arena, LaTeXContext& ctx) {
    TexDocumentModel* doc = doc_model_create(arena);
    
    // Check for null element
    if (get_type_id(elem) == LMD_TYPE_NULL) {
        log_error("doc_model_from_latex: null element");
        doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
        return doc;
    }
    
    // Use ItemReader to traverse the Lambda Element tree
    ItemReader reader(elem.to_const());
    
    // Build the document tree
    DocElement* root = build_doc_element(reader, arena, doc);
    
    if (root) {
        if (root->type != DocElemType::DOCUMENT) {
            // Wrap in document element
            doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
            doc_append_child(doc->root, root);
        } else {
            doc->root = root;
        }
    } else {
        doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    }
    
    log_debug("doc_model_from_latex: built document with %d labels, %d macros",
              doc->label_count, doc->macro_count);
    
    return doc;
}

TexDocumentModel* doc_model_from_string(const char* latex, size_t len, Arena* arena, TFMFontManager* fonts) {
    (void)len; // length can be used for optimization
    
    // Create Input structure for parsing
    Input* input = InputManager::create_input(nullptr);
    if (!input) {
        log_error("doc_model_from_string: failed to create input");
        TexDocumentModel* doc = doc_model_create(arena);
        doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
        return doc;
    }
    
    // Parse LaTeX source using tree-sitter parser
    parse_latex_ts(input, latex);
    
    // Get the parsed root element
    Item root = input->root;
    
    if (get_type_id(root) == LMD_TYPE_NULL) {
        log_error("doc_model_from_string: parse returned null");
        TexDocumentModel* doc = doc_model_create(arena);
        doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
        return doc;
    }
    
    // Create document model and build from AST
    TexDocumentModel* doc = doc_model_create(arena);
    
    // Use ItemReader to traverse the Lambda Element tree
    ItemReader reader(root.to_const());
    
    // Build the document tree (simplified path without LaTeXContext for HTML-only use)
    DocElement* doc_root = build_doc_element(reader, arena, doc);
    
    if (doc_root) {
        if (doc_root->type != DocElemType::DOCUMENT) {
            // Wrap in document element
            doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
            doc_append_child(doc->root, doc_root);
        } else {
            doc->root = doc_root;
        }
    } else {
        doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    }
    
    log_debug("doc_model_from_string: built document model from %zu bytes of LaTeX", len);
    
    return doc;
}

#else // DOC_MODEL_MINIMAL

// Stub implementation when Lambda runtime is not available
TexDocumentModel* doc_model_from_latex(Item elem, Arena* arena, TFMFontManager* fonts) {
    (void)elem; (void)fonts;
    TexDocumentModel* doc = doc_model_create(arena);
    doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    log_debug("doc_model_from_latex: minimal stub (DOC_MODEL_MINIMAL defined)");
    return doc;
}

TexDocumentModel* doc_model_from_string(const char* latex, size_t len, Arena* arena, TFMFontManager* fonts) {
    (void)latex; (void)len; (void)fonts;
    TexDocumentModel* doc = doc_model_create(arena);
    doc->root = doc_alloc_element(arena, DocElemType::DOCUMENT);
    log_debug("doc_model_from_string: minimal stub (DOC_MODEL_MINIMAL defined)");
    return doc;
}

#endif // DOC_MODEL_MINIMAL

// ============================================================================
// Placeholder for TexNode conversion
// (To be implemented in Phase B)
// ============================================================================

TexNode* doc_model_to_texnode(TexDocumentModel* doc, Arena* arena, LaTeXContext& ctx) {
    // TODO: Implement document model to TexNode conversion
    log_debug("doc_model_to_texnode: not yet implemented");
    return nullptr;
}

TexNode* doc_element_to_texnode(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    // TODO: Implement element to TexNode conversion
    log_debug("doc_element_to_texnode: not yet implemented");
    return nullptr;
}

} // namespace tex
