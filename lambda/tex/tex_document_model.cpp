// tex_document_model.cpp - Intermediate Document Model for Unified Pipeline
//
// Implementation of the document model layer that bridges LaTeX AST to
// multiple output formats (HTML, DVI, SVG, PDF).

#include "tex_document_model.hpp"
#include "tex_doc_model_internal.hpp"
#include "tex_math_ts.hpp"
#include "tex_linebreak.hpp"
#include "tex_pagebreak.hpp"
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

// Forward declaration needed for build_section_command to process labels in titles
static void process_label_command(const ElementReader& elem, Arena* arena,
                                   TexDocumentModel* doc, DocElement* parent);

// Helper to recursively find and process \label commands within an element
static void process_labels_in_element(const ItemReader& item, Arena* arena,
                                       TexDocumentModel* doc, DocElement* parent);

// ============================================================================
// Document Model Methods
// ============================================================================

void TexDocumentModel::add_label(const char* label, const char* ref_text, int page) {
    // Legacy method - uses label as both label and ref_id
    add_label_with_id(label, label, ref_text);
}

void TexDocumentModel::add_label_with_id(const char* label, const char* ref_id, const char* ref_text) {
    log_debug("add_label_with_id: label='%s', ref_id='%s', ref_text='%s'",
              label ? label : "(null)", ref_id ? ref_id : "(null)", ref_text ? ref_text : "(null)");
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
    labels[label_count].ref_id = ref_id;
    labels[label_count].ref_text = ref_text;
    labels[label_count].page = -1;
    label_count++;
}

const char* TexDocumentModel::resolve_ref(const char* label) const {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].label, label) == 0) {
            return labels[i].ref_text ? labels[i].ref_text : "??";
        }
    }
    return nullptr;  // Unresolved - caller decides what to show
}

const char* TexDocumentModel::resolve_ref_id(const char* label) const {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].label, label) == 0) {
            return labels[i].ref_id;
        }
    }
    return nullptr;  // Unresolved
}

void TexDocumentModel::add_pending_ref(DocElement* elem) {
    if (pending_ref_count >= pending_ref_capacity) {
        int new_capacity = pending_ref_capacity == 0 ? 16 : pending_ref_capacity * 2;
        PendingRef* new_refs = (PendingRef*)arena_alloc(arena, new_capacity * sizeof(PendingRef));
        if (pending_refs) {
            memcpy(new_refs, pending_refs, pending_ref_count * sizeof(PendingRef));
        }
        pending_refs = new_refs;
        pending_ref_capacity = new_capacity;
    }
    pending_refs[pending_ref_count].elem = elem;
    pending_ref_count++;
}

void TexDocumentModel::resolve_pending_refs() {
    log_debug("resolve_pending_refs: %d pending refs, %d labels registered", pending_ref_count, label_count);
    for (int i = 0; i < pending_ref_count; i++) {
        DocElement* elem = pending_refs[i].elem;
        if (elem && elem->type == DocElemType::CROSS_REF && elem->ref.ref_label) {
            const char* orig_label = elem->ref.ref_label;
            const char* ref_id = resolve_ref_id(elem->ref.ref_label);
            const char* ref_text = resolve_ref(elem->ref.ref_label);
            log_debug("resolve_pending_refs[%d]: label='%s' -> ref_id='%s', ref_text='%s'",
                      i, orig_label, ref_id ? ref_id : "(null)", ref_text ? ref_text : "(null)");
            if (ref_id) {
                elem->ref.ref_label = ref_id;  // Update to use actual anchor id
            }
            if (ref_text) {
                elem->ref.ref_text = ref_text;
            } else {
                elem->ref.ref_text = "??";  // Unresolved
            }
        }
    }
}

void TexDocumentModel::add_macro(const char* name, int num_args, const char* replacement, const char* params) {
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
    macros[macro_count].params = params;
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

// Helper to parse params string and count mandatory args
// params format: [] = optional, {} = mandatory
// Example: "[]{}[]" = 3 args (opt, mand, opt)
static int count_params(const char* params) {
    if (!params) return 0;
    int count = 0;
    for (const char* p = params; *p; p++) {
        if (*p == '[' || *p == '{') count++;
    }
    return count;
}

#ifndef DOC_MODEL_MINIMAL
// Forward declaration for JSON parsing (in global namespace)
}  // namespace tex (temporary close)
void parse_json(Input* input, const char* json_string);
namespace tex {

// Load macros from a package JSON file
static bool load_package_macros(TexDocumentModel* doc, const char* pkg_name) {
    if (!doc || !pkg_name) return false;
    
    // Build path to package file
    char path[512];
    snprintf(path, sizeof(path), "lambda/tex/packages/%s.pkg.json", pkg_name);
    
    FILE* f = fopen(path, "r");
    if (!f) {
        log_debug("doc_model: package '%s' not found at %s", pkg_name, path);
        return false;
    }
    
    // read file content
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = (char*)arena_alloc(doc->arena, size + 1);
    size_t read_size = fread(content, 1, size, f);
    content[read_size] = '\0';
    fclose(f);
    
    log_debug("doc_model: loading package '%s' from %s", pkg_name, path);
    
    // parse JSON - create an Input for parsing
    Input* input = InputManager::create_input(nullptr);
    if (!input) {
        log_error("doc_model: failed to create input for package '%s'", pkg_name);
        return false;
    }
    parse_json(input, content);
    
    if (get_type_id(input->root) != LMD_TYPE_MAP) {
        log_error("doc_model: package '%s' root is not an object", pkg_name);
        return false;
    }
    
    ItemReader root(input->root.to_const());
    MapReader pkg = root.asMap();
    ItemReader commands_item = pkg.get("commands");
    if (!commands_item.isMap()) {
        log_debug("doc_model: package '%s' has no commands", pkg_name);
        return true;  // not an error - package just has no commands
    }
    
    MapReader commands = commands_item.asMap();
    MapReader::EntryIterator iter = commands.entries();
    const char* cmd_name;
    ItemReader cmd_def;
    
    while (iter.next(&cmd_name, &cmd_def)) {
        if (!cmd_def.isMap()) continue;
        
        MapReader def = cmd_def.asMap();
        ItemReader type_item = def.get("type");
        if (!type_item.isString()) continue;
        
        const char* type = type_item.cstring();
        // only handle macro/constructor types for now
        if (strcmp(type, "macro") != 0 && strcmp(type, "constructor") != 0) {
            continue;
        }
        
        ItemReader pattern_item = def.get("pattern");
        if (!pattern_item.isString()) continue;
        
        ItemReader params_item = def.get("params");
        const char* params = params_item.isString() ? params_item.cstring() : "";
        const char* pattern = pattern_item.cstring();
        
        int num_args = count_params(params);
        
        // intern strings in arena
        size_t name_len = strlen(cmd_name);
        char* name_copy = (char*)arena_alloc(doc->arena, name_len + 2);
        name_copy[0] = '\\';
        memcpy(name_copy + 1, cmd_name, name_len + 1);
        
        size_t pattern_len = strlen(pattern);
        char* pattern_copy = (char*)arena_alloc(doc->arena, pattern_len + 1);
        memcpy(pattern_copy, pattern, pattern_len + 1);
        
        size_t params_len = strlen(params);
        char* params_copy = (char*)arena_alloc(doc->arena, params_len + 1);
        memcpy(params_copy, params, params_len + 1);
        
        doc->add_macro(name_copy, num_args, pattern_copy, params_copy);
        log_debug("doc_model: registered package macro %s with %d args, params='%s', pattern='%s'", name_copy, num_args, params_copy, pattern_copy);
    }
    
    return true;
}
#else
// Stub for minimal builds
static bool load_package_macros(TexDocumentModel*, const char*) { return false; }
#endif

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

// Create a RAW_HTML element with pre-rendered HTML content
static DocElement* doc_create_raw_html(Arena* arena, const char* html, size_t len) {
    DocElement* elem = doc_alloc_element(arena, DocElemType::RAW_HTML);
    
    // Copy HTML to arena
    char* html_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(html_copy, html, len);
    html_copy[len] = '\0';
    
    elem->raw.raw_content = html_copy;
    elem->raw.raw_len = len;
    
    return elem;
}

static DocElement* doc_create_raw_html_cstr(Arena* arena, const char* html) {
    return doc_create_raw_html(arena, html, strlen(html));
}

// Create text element with normalized whitespace
// Uses normalize_latex_whitespace from tex_doc_model_text.cpp
static DocElement* doc_create_text_normalized(Arena* arena, const char* text, DocTextStyle style) {
    const char* normalized = normalize_latex_whitespace(text, arena);
    if (!normalized) return nullptr;
    return doc_create_text_cstr(arena, normalized, style);
}

// ============================================================================
// HTML Utilities
// ============================================================================

void html_escape_append(StrBuf* out, const char* text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        // Check for UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
        if (c == 0xC2 && i + 1 < len && (unsigned char)text[i + 1] == 0xA0) {
            strbuf_append_str(out, "&nbsp;");
            i++;  // Skip the second byte
            continue;
        }
        switch (c) {
        case '&':  strbuf_append_str(out, "&amp;"); break;
        case '<':  strbuf_append_str(out, "&lt;"); break;
        case '>':  strbuf_append_str(out, "&gt;"); break;
        case '"':  strbuf_append_str(out, "&quot;"); break;
        case '\'': strbuf_append_str(out, "&#39;"); break;
        default:   strbuf_append_char(out, (char)c); break;
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

// Helper to check if any ancestor TEXT_SPAN has ITALIC flag
static bool has_italic_ancestor(DocElement* elem) {
    DocElement* parent = elem->parent;
    while (parent) {
        if (parent->type == DocElemType::TEXT_SPAN) {
            if (parent->text.style.has(DocTextStyle::ITALIC)) {
                return true;
            }
        }
        parent = parent->parent;
    }
    return false;
}

// Forward declarations for mutual recursion
static void render_children_html_with_context(DocElement* parent, StrBuf* out, 
                                  const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags);
static void render_children_html(DocElement* parent, StrBuf* out, 
                                  const HtmlOutputOptions& opts, int depth);

// Render a TEXT_SPAN element with inherited style context for emphasis toggling
static void render_text_span_html_with_context(DocElement* elem, StrBuf* out, 
                                   const HtmlOutputOptions& opts, uint16_t inherited_flags) {
    DocTextStyle& style = elem->text.style;
    
    // Resolve EMPHASIS flag: toggle between italic and upright based on context
    // \emph inside italic context -> upright; \emph in upright context -> italic
    uint16_t resolved_flags = style.flags;
    if (style.has(DocTextStyle::EMPHASIS)) {
        // Clear the EMPHASIS flag from resolved
        resolved_flags &= ~DocTextStyle::EMPHASIS;
        // Check if we're already in an italic context (from parent, inherited, or ancestors)
        bool in_italic_context = (inherited_flags & DocTextStyle::ITALIC) != 0 || has_italic_ancestor(elem);
        if (in_italic_context) {
            // In italic context: emphasis means upright
            resolved_flags |= DocTextStyle::UPRIGHT;
        } else {
            // In upright context: emphasis means italic
            resolved_flags |= DocTextStyle::ITALIC;
        }
    }
    
    // Create a temporary style for rendering with resolved flags
    DocTextStyle resolved_style = style;
    resolved_style.flags = resolved_flags;
    
    // Opening tags - use semantic HTML tags
    if (resolved_style.has(DocTextStyle::BOLD))
        strbuf_append_str(out, "<strong>");
    if (resolved_style.has(DocTextStyle::ITALIC))
        strbuf_append_str(out, "<em>");
    if (resolved_style.has(DocTextStyle::MONOSPACE))
        strbuf_append_str(out, "<code>");
    if (resolved_style.has(DocTextStyle::SLANTED))
        strbuf_append_format(out, "<span class=\"%ssl\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::UPRIGHT))
        strbuf_append_format(out, "<span class=\"%sup\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::UNDERLINE))
        strbuf_append_str(out, "<u>");
    if (resolved_style.has(DocTextStyle::STRIKEOUT))
        strbuf_append_str(out, "<s>");
    if (resolved_style.has(DocTextStyle::SMALLCAPS))
        strbuf_append_format(out, "<span class=\"%ssmallcaps\">", opts.css_class_prefix);
    if (resolved_style.has(DocTextStyle::SUPERSCRIPT))
        strbuf_append_str(out, "<sup>");
    if (resolved_style.has(DocTextStyle::SUBSCRIPT))
        strbuf_append_str(out, "<sub>");
    // Font size - use class
    const char* size_class = font_size_name_class(resolved_style.font_size_name);
    if (size_class) {
        strbuf_append_format(out, "<span class=\"%s%s\">", opts.css_class_prefix, size_class);
    }
    
    // Content
    if (elem->text.text && elem->text.text_len > 0) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    
    // Recurse to children with combined flags
    uint16_t child_inherited = inherited_flags | resolved_flags;
    render_children_html_with_context(elem, out, opts, 0, child_inherited);
    
    // Closing tags (reverse order)
    // Close font size first (innermost)
    if (resolved_style.font_size_name != FontSizeName::INHERIT)
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::SUBSCRIPT))
        strbuf_append_str(out, "</sub>");
    if (resolved_style.has(DocTextStyle::SUPERSCRIPT))
        strbuf_append_str(out, "</sup>");
    if (resolved_style.has(DocTextStyle::SMALLCAPS))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::STRIKEOUT))
        strbuf_append_str(out, "</s>");
    if (resolved_style.has(DocTextStyle::UNDERLINE))
        strbuf_append_str(out, "</u>");
    if (resolved_style.has(DocTextStyle::UPRIGHT))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::SLANTED))
        strbuf_append_str(out, "</span>");
    if (resolved_style.has(DocTextStyle::MONOSPACE))
        strbuf_append_str(out, "</code>");
    if (resolved_style.has(DocTextStyle::ITALIC))
        strbuf_append_str(out, "</em>");
    if (resolved_style.has(DocTextStyle::BOLD))
        strbuf_append_str(out, "</strong>");
}

// Legacy wrapper that doesn't pass context
static void render_text_span_html(DocElement* elem, StrBuf* out, 
                                   const HtmlOutputOptions& opts) {
    render_text_span_html_with_context(elem, out, opts, 0);
}

static void render_heading_html(DocElement* elem, StrBuf* out,
                                 const HtmlOutputOptions& opts, int depth) {
    // Map level to HTML heading: part(0)->h1, chapter(1)->h2, section(2)->h3, etc.
    int h_level = elem->heading.level + 1;
    if (h_level > 6) h_level = 6;
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build heading tag with optional id and class
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
    
    // Title
    if (elem->heading.title) {
        html_escape_append(out, elem->heading.title, strlen(elem->heading.title));
    }
    
    strbuf_append_format(out, "</h%d>", h_level);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_paragraph_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth) {
    // Skip empty paragraphs (no children or only empty text runs/whitespace)
    if (!elem->first_child) return;
    
    // Check if paragraph has any visible content
    bool has_content = false;
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        if (child->type == DocElemType::TEXT_RUN) {
            if (child->text.text && child->text.text_len > 0) {
                has_content = true;
                break;
            }
        } else if (child->type == DocElemType::TEXT_SPAN) {
            if ((child->text.text && child->text.text_len > 0) || child->first_child) {
                has_content = true;
                break;
            }
        } else if (child->type != DocElemType::SPACE || child->space.is_linebreak) {
            has_content = true;
            break;
        }
    }
    if (!has_content) return;
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build up the class string based on flags
    bool has_continue = (elem->flags & DocElement::FLAG_CONTINUE) != 0;
    bool has_noindent = (elem->flags & DocElement::FLAG_NOINDENT) != 0;
    bool has_centered = (elem->flags & DocElement::FLAG_CENTERED) != 0;
    bool has_raggedright = (elem->flags & DocElement::FLAG_FLUSH_LEFT) != 0;
    bool has_raggedleft = (elem->flags & DocElement::FLAG_FLUSH_RIGHT) != 0;
    
    bool has_any_class = has_continue || has_noindent || has_centered || has_raggedright || has_raggedleft;
    
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: always add class="latex-paragraph"
        if (has_continue && has_noindent) {
            strbuf_append_format(out, "<p class=\"%sparagraph continue noindent\">", opts.css_class_prefix);
        } else if (has_continue) {
            strbuf_append_format(out, "<p class=\"%sparagraph continue\">", opts.css_class_prefix);
        } else if (has_noindent) {
            strbuf_append_format(out, "<p class=\"%sparagraph noindent\">", opts.css_class_prefix);
        } else {
            strbuf_append_format(out, "<p class=\"%sparagraph\">", opts.css_class_prefix);
        }
    } else {
        // Hybrid mode (no prefix): only add class when needed
        if (has_any_class) {
            strbuf_append_str(out, "<p class=\"");
            bool first = true;
            if (has_raggedright) {
                strbuf_append_str(out, "raggedright");
                first = false;
            }
            if (has_raggedleft) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "raggedleft");
                first = false;
            }
            if (has_centered) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "centering");
                first = false;
            }
            if (has_continue) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "continue");
                first = false;
            }
            if (has_noindent) {
                if (!first) strbuf_append_str(out, " ");
                strbuf_append_str(out, "noindent");
                first = false;
            }
            strbuf_append_str(out, "\">");
        } else {
            strbuf_append_str(out, "<p>");
        }
    }
    
    render_children_html(elem, out, opts, depth + 1);
    
    strbuf_append_str(out, "</p>");
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

static void render_list_html(DocElement* elem, StrBuf* out,
                              const HtmlOutputOptions& opts, int depth) {
    const char* tag;
    const char* list_class;
    switch (elem->list.list_type) {
    case ListType::ITEMIZE:     tag = "ul"; list_class = "itemize"; break;
    case ListType::ENUMERATE:   tag = "ol"; list_class = "enumerate"; break;
    case ListType::DESCRIPTION: tag = "dl"; list_class = "description"; break;
    default:                    tag = "ul"; list_class = "itemize"; break;
    }
    
    if (opts.pretty_print) html_indent(out, depth);
    
    // Build class list - add "centering" if centered
    const char* centering = (elem->flags & DocElement::FLAG_CENTERED) ? " centering" : "";
    
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: class="latex-list"
        strbuf_append_format(out, "<%s class=\"%slist%s\">", tag, opts.css_class_prefix, centering);
    } else {
        // Hybrid mode (no prefix): class="itemize" / "enumerate" / "description"
        strbuf_append_format(out, "<%s class=\"%s%s\">", tag, list_class, centering);
    }
    if (opts.pretty_print) strbuf_append_str(out, "\n");
    
    render_children_html(elem, out, opts, depth + 1);
    
    if (opts.pretty_print) html_indent(out, depth);
    strbuf_append_format(out, "</%s>", tag);
    if (opts.pretty_print) strbuf_append_str(out, "\n");
}

// Helper to calculate list nesting level by walking up parent chain
// Returns 0 for top-level list, 1 for nested, etc.
static int get_list_nesting_level(DocElement* elem) {
    int level = 0;
    // Start from the item's parent (which is the list it belongs to)
    // and count how many LIST elements are above that
    DocElement* list = elem->parent;
    if (!list || list->type != DocElemType::LIST) return 0;
    
    for (DocElement* p = list->parent; p; p = p->parent) {
        if (p->type == DocElemType::LIST) {
            level++;
        }
    }
    return level;
}

static void render_list_item_html(DocElement* elem, StrBuf* out,
                                   const HtmlOutputOptions& opts, int depth,
                                   ListType parent_type) {
    if (opts.pretty_print) html_indent(out, depth);
    
    // Check if item is centered
    const char* centering_class = (elem->flags & DocElement::FLAG_CENTERED) ? " class=\"centering\"" : "";
    
    if (parent_type == ListType::DESCRIPTION) {
        // Description list: <dt>term</dt><dd>content</dd>
        if (elem->list_item.label) {
            strbuf_append_format(out, "<dt%s>", centering_class);
            html_escape_append(out, elem->list_item.label, strlen(elem->list_item.label));
            strbuf_append_str(out, "</dt>");
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            if (opts.pretty_print) html_indent(out, depth);
        }
        strbuf_append_format(out, "<dd%s>", centering_class);
    } else {
        strbuf_append_format(out, "<li%s>", centering_class);
        // Semantic HTML: no bullet/number markup - let CSS handle list styling
    }
    
    // Render children directly
    render_children_html(elem, out, opts, depth + 1);
    
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

// Forward declaration for context-aware element rendering
static void doc_element_to_html_with_context(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags);

// Context-aware version that propagates inherited style flags for emphasis toggling
static void render_children_html_with_context(DocElement* parent, StrBuf* out,
                                  const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags) {
    for (DocElement* child = parent->first_child; child; child = child->next_sibling) {
        doc_element_to_html_with_context(child, out, opts, depth, inherited_flags);
    }
}

// Helper to check if an element is inline content
static bool is_inline_element(DocElement* elem) {
    if (!elem) return false;
    switch (elem->type) {
    case DocElemType::TEXT_RUN:
    case DocElemType::TEXT_SPAN:
    case DocElemType::SPACE:
    case DocElemType::RAW_HTML:  // inline HTML like logos
    case DocElemType::CROSS_REF: // cross-references are inline
        return true;
    default:
        return false;
    }
}

// Context-aware element rendering that handles emphasis toggling
static void doc_element_to_html_with_context(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth, uint16_t inherited_flags) {
    if (!elem) return;
    
    switch (elem->type) {
    case DocElemType::TEXT_SPAN:
        render_text_span_html_with_context(elem, out, opts, inherited_flags);
        break;
        
    case DocElemType::TEXT_RUN:
        if (elem->text.text && elem->text.text_len > 0) {
            bool in_monospace = elem->text.style.has(DocTextStyle::MONOSPACE);
            html_escape_append_transformed(out, elem->text.text, elem->text.text_len, in_monospace);
        }
        break;
        
    default:
        // For all other elements, fall back to the regular renderer
        doc_element_to_html(elem, out, opts, depth);
        break;
    }
}

void doc_element_to_html(DocElement* elem, StrBuf* out,
                          const HtmlOutputOptions& opts, int depth) {
    if (!elem) return;
    
    switch (elem->type) {
    case DocElemType::DOCUMENT:
        render_children_html(elem, out, opts, depth);
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
        // Determine alignment class from env_name or flags
        const char* align_class = "list";
        bool use_list_prefix = (opts.css_class_prefix && opts.css_class_prefix[0]);
        bool is_quote_env = false;
        if (elem->alignment.env_name) {
            // Check if this is a quote-like environment
            is_quote_env = (strcmp(elem->alignment.env_name, "quote") == 0 ||
                           strcmp(elem->alignment.env_name, "quotation") == 0 ||
                           strcmp(elem->alignment.env_name, "verse") == 0);
            // Use stored environment name: "list quote", "list quotation", "list verse", etc.
            static char class_buf[64];
            if (use_list_prefix) {
                snprintf(class_buf, sizeof(class_buf), "list %s", elem->alignment.env_name);
            } else {
                snprintf(class_buf, sizeof(class_buf), "%s", elem->alignment.env_name);
            }
            align_class = class_buf;
        } else if (elem->flags & DocElement::FLAG_CENTERED) {
            align_class = use_list_prefix ? "list center" : "center";
        } else if (elem->flags & DocElement::FLAG_FLUSH_LEFT) {
            align_class = use_list_prefix ? "list flushleft" : "flushleft";
        } else if (elem->flags & DocElement::FLAG_FLUSH_RIGHT) {
            align_class = use_list_prefix ? "list flushright" : "flushright";
        }
        // Use <blockquote> for quote environments
        if (is_quote_env) {
            strbuf_append_format(out, "<blockquote class=\"%s\">", align_class);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            render_children_html(elem, out, opts, depth + 1);
            strbuf_append_str(out, "</blockquote>");
        } else {
            strbuf_append_format(out, "<div class=\"%s\">", align_class);
            if (opts.pretty_print) strbuf_append_str(out, "\n");
            render_children_html(elem, out, opts, depth + 1);
            strbuf_append_str(out, "</div>");
        }
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
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        // Mode with prefix: <article class="latex-document latex-article">
        strbuf_append_format(output, "<article class=\"%sdocument %s%s\">\n", 
            opts.css_class_prefix, opts.css_class_prefix, doc->document_class);
    } else {
        // Hybrid mode: <article class="latex-document">
        strbuf_append_str(output, "<article class=\"latex-document\">");
    }
    
    // Title block
    if (doc->title || doc->author || doc->date) {
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
    if (opts.css_class_prefix && opts.css_class_prefix[0]) {
        strbuf_append_str(output, "</article>\n");
    } else {
        // Hybrid mode: no trailing newline for compact output
        strbuf_append_str(output, "</article>");
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

// Helper to transform text content with typographic ligatures
// Handles: !´ → ¡, ?´ → ¿, --- → —, -- → –, etc.
static const char* transform_text_ligatures(const char* text, Arena* arena) {
    if (!text || text[0] == '\0') return text;
    
    size_t len = strlen(text);
    // Allocate enough for worst case (each char could expand)
    char* result = (char*)arena_alloc(arena, len * 3 + 1);
    char* out = result;
    const char* p = text;
    
    while (*p) {
        // !´ (exclamation + acute accent U+00B4) → ¡ (inverted exclamation U+00A1)
        if (*p == '!' && (unsigned char)*(p+1) == 0xC2 && (unsigned char)*(p+2) == 0xB4) {
            *out++ = '\xC2';
            *out++ = '\xA1';  // ¡
            p += 3;  // Skip ! and ´ (2 bytes)
        }
        // ?´ (question + acute accent U+00B4) → ¿ (inverted question U+00BF)
        else if (*p == '?' && (unsigned char)*(p+1) == 0xC2 && (unsigned char)*(p+2) == 0xB4) {
            *out++ = '\xC2';
            *out++ = '\xBF';  // ¿
            p += 3;  // Skip ? and ´ (2 bytes)
        }
        else {
            *out++ = *p++;
        }
    }
    *out = '\0';
    return result;
}

// Sentinel pointer values for special markers (used internally during tree building)
static DocElement* const PARBREAK_MARKER = (DocElement*)1;  // paragraph break marker
static DocElement* const LINEBREAK_MARKER = (DocElement*)2; // line break marker
static DocElement* const NOINDENT_MARKER = (DocElement*)3;  // \noindent command marker

// Alignment markers for paragraph alignment scoping (\centering, \raggedright, \raggedleft)
static DocElement* const CENTERING_MARKER = (DocElement*)4;    // \centering command marker
static DocElement* const RAGGEDRIGHT_MARKER = (DocElement*)5;  // \raggedright command marker
static DocElement* const RAGGEDLEFT_MARKER = (DocElement*)6;   // \raggedleft command marker

// Font declaration markers for font commands without arguments (\bfseries, \itshape, etc.)
// These affect all following content in the current scope
static DocElement* const BOLD_MARKER = (DocElement*)7;         // \bfseries, \bf
static DocElement* const ITALIC_MARKER = (DocElement*)8;       // \itshape, \it
static DocElement* const MONOSPACE_MARKER = (DocElement*)9;    // \ttfamily, \tt
static DocElement* const SMALLCAPS_MARKER = (DocElement*)10;   // \scshape
static DocElement* const SLANTED_MARKER = (DocElement*)11;     // \slshape
static DocElement* const UPRIGHT_MARKER = (DocElement*)12;     // \upshape
static DocElement* const EMPHASIS_MARKER = (DocElement*)13;    // \em (toggles italic/upright)

// Alignment state enumeration for paragraph building
enum class ParagraphAlignment : uint8_t {
    NONE = 0,       // No explicit alignment (default)
    CENTERING,      // \centering - center alignment
    RAGGEDRIGHT,    // \raggedright - left alignment (ragged right)
    RAGGEDLEFT      // \raggedleft - right alignment (ragged left)
};

// Check if a marker is an alignment marker
static bool is_alignment_marker(DocElement* elem) {
    return elem == CENTERING_MARKER || elem == RAGGEDRIGHT_MARKER || elem == RAGGEDLEFT_MARKER;
}

// Check if a marker is a font style marker
static bool is_font_marker(DocElement* elem) {
    return elem == BOLD_MARKER || elem == ITALIC_MARKER || elem == MONOSPACE_MARKER || 
           elem == SMALLCAPS_MARKER || elem == SLANTED_MARKER || elem == UPRIGHT_MARKER ||
           elem == EMPHASIS_MARKER;
}

// Check if a pointer is any special marker (not a real DocElement pointer)
static bool is_special_marker(DocElement* elem) {
    return elem == PARBREAK_MARKER || elem == LINEBREAK_MARKER || elem == NOINDENT_MARKER || 
           is_alignment_marker(elem) || is_font_marker(elem);
}

// Get alignment from marker
static ParagraphAlignment marker_to_alignment(DocElement* elem) {
    if (elem == CENTERING_MARKER) return ParagraphAlignment::CENTERING;
    if (elem == RAGGEDRIGHT_MARKER) return ParagraphAlignment::RAGGEDRIGHT;
    if (elem == RAGGEDLEFT_MARKER) return ParagraphAlignment::RAGGEDLEFT;
    return ParagraphAlignment::NONE;
}

// Convert font marker to DocTextStyle flags
static uint32_t font_marker_to_style_flags(DocElement* elem) {
    if (elem == BOLD_MARKER) return DocTextStyle::BOLD;
    if (elem == ITALIC_MARKER) return DocTextStyle::ITALIC;
    if (elem == MONOSPACE_MARKER) return DocTextStyle::MONOSPACE;
    if (elem == SMALLCAPS_MARKER) return DocTextStyle::SMALLCAPS;
    if (elem == SLANTED_MARKER) return DocTextStyle::SLANTED;
    if (elem == UPRIGHT_MARKER) return DocTextStyle::UPRIGHT;
    if (elem == EMPHASIS_MARKER) return DocTextStyle::EMPHASIS;
    return 0;
}

// Wrap a DocElement in a styled span using font flags
static DocElement* wrap_in_font_style(DocElement* elem, unsigned int font_flags, Arena* arena) {
    if (!elem || font_flags == 0) return elem;
    
    DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    styled_span->text.style = DocTextStyle::plain();
    styled_span->text.style.flags |= font_flags;
    doc_append_child(styled_span, elem);
    return styled_span;
}

// Apply alignment to paragraph flags
static void apply_alignment_to_paragraph(DocElement* para, ParagraphAlignment align) {
    if (!para) return;
    // Clear existing alignment flags
    para->flags &= ~(DocElement::FLAG_CENTERED | DocElement::FLAG_FLUSH_LEFT | DocElement::FLAG_FLUSH_RIGHT);
    switch (align) {
        case ParagraphAlignment::CENTERING:
            para->flags |= DocElement::FLAG_CENTERED;
            break;
        case ParagraphAlignment::RAGGEDRIGHT:
            para->flags |= DocElement::FLAG_FLUSH_LEFT;
            break;
        case ParagraphAlignment::RAGGEDLEFT:
            para->flags |= DocElement::FLAG_FLUSH_RIGHT;
            break;
        case ParagraphAlignment::NONE:
            break;
    }
}

// Forward declaration for is_block_element_tag (defined later)
static bool is_block_element_tag(const char* tag);

// Helper to check if an element contains block elements (center, lists, etc.)
// This is used to detect when a paragraph element needs special handling
static bool contains_block_elements(const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && is_block_element_tag(tag)) {
                return true;
            }
        }
    }
    return false;
}

// Helper to check if an element contains paragraph break markers (parbreak symbol or \par command)
static bool contains_parbreak_markers(const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        // Check for "parbreak" symbol
        if (child.isSymbol()) {
            const char* sym = child.cstring();
            if (sym && strcmp(sym, "parbreak") == 0) {
                return true;
            }
        }
        // Check for "parbreak" as string (symbols may come through as strings)
        if (child.isString()) {
            const char* str = child.cstring();
            if (str && strcmp(str, "parbreak") == 0) {
                return true;
            }
        }
        // Check for \par command element
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "par") == 0) {
                return true;
            }
        }
    }
    return false;
}

// Check if an element contains alignment commands (centering, raggedright, raggedleft)
static bool contains_alignment_commands(const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && (tag_eq(tag, "centering") || tag_eq(tag, "raggedright") || tag_eq(tag, "raggedleft"))) {
                return true;
            }
        }
    }
    return false;
}

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
    
    // Handle symbols (like #1 in macro definitions)
    if (item.isSymbol()) {
        const char* sym = item.cstring();
        if (sym) {
            size_t len = strlen(sym);
            char* copy = (char*)arena_alloc(arena, len + 1);
            memcpy(copy, sym, len + 1);
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

// Check if a tag is a word-forming command that absorbs following space
// These are single-char commands that produce letters and should consume trailing space
// unless followed by {} (which acts as a terminator)
static bool is_word_forming_command(const char* tag) {
    if (!tag) return false;
    // Single-char commands for special letters
    static const char* word_forming[] = {
        "i", "j",           // Dotless letters
        "o", "O",           // Scandinavian slashed o
        "l", "L",           // Polish L with stroke
        "ae", "AE",         // AE ligature
        "oe", "OE",         // OE ligature
        "aa", "AA",         // Double-a ring (Scandinavian)
        "ss",               // German sharp s (eszett)
        nullptr
    };
    for (int k = 0; word_forming[k]; k++) {
        if (tag_eq(tag, word_forming[k])) return true;
    }
    return false;
}

// Check if an element has an empty curly_group child (terminator like \ss{})
static bool has_empty_curly_terminator(const ElementReader& elem, Arena* arena) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "group"))) {
                const char* content = extract_text_content(child, arena);
                if (!content || content[0] == '\0') {
                    return true;  // Empty curly_group found
                }
            }
        }
    }
    return false;
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

// Forward declaration for render_brack_group_to_html
static DocElement* build_doc_element(const ItemReader& item, Arena* arena, TexDocumentModel* doc);

// Forward declaration for inline ref handling
static DocElement* build_ref_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Helper to recursively find and process \label commands within an element
// This is used to find labels inside section titles, figure captions, etc.
static void process_labels_in_element(const ItemReader& item, Arena* arena,
                                       TexDocumentModel* doc, DocElement* parent) {
    if (!item.isElement()) return;
    
    ElementReader elem = item.asElement();
    const char* tag = elem.tagName();
    log_debug("process_labels_in_element: tag='%s'", tag ? tag : "(null)");
    
    // Check if this is a label command
    if (tag && tag_eq(tag, "label")) {
        log_debug("process_labels_in_element: found label command");
        process_label_command(elem, arena, doc, parent);
        return;
    }
    
    // Recurse into children
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            process_labels_in_element(child, arena, doc, parent);
        }
    }
}

// ============================================================================
// Macro Registration and Expansion
// ============================================================================

// Parse and register a \newcommand or \renewcommand definition
// AST format: <newcommand "\cmdname" <brack_group "1"> <curly_group "+#1+">>
// Returns true if successfully parsed and registered
static bool register_newcommand(const ElementReader& elem, Arena* arena, TexDocumentModel* doc) {
    int64_t child_count = elem.childCount();
    if (child_count < 2) return false;
    
    const char* cmd_name = nullptr;
    int num_args = 0;
    const char* replacement = nullptr;
    
    int arg_index = 0;
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child = elem.childAt(i);
        
        if (child.isString()) {
            const char* text = child.cstring();
            if (!text) continue;
            
            // Skip whitespace-only strings
            bool is_whitespace = true;
            for (const char* p = text; *p; p++) {
                if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                    is_whitespace = false;
                    break;
                }
            }
            if (is_whitespace) continue;
            
            // First non-whitespace string should be command name (like "\echoOGO")
            if (!cmd_name) {
                // Skip leading backslash if present
                cmd_name = (text[0] == '\\') ? text + 1 : text;
                arg_index++;
            }
        } else if (child.isElement()) {
            ElementReader ch_elem = child.asElement();
            const char* ch_tag = ch_elem.tagName();
            
            if (!ch_tag) continue;
            
            if (tag_eq(ch_tag, "brack_group")) {
                // Optional argument count: [1]
                const char* arg_text = extract_text_content(child, arena);
                if (arg_text && strlen(arg_text) > 0) {
                    num_args = atoi(arg_text);
                }
            } else if (tag_eq(ch_tag, "curly_group")) {
                if (!cmd_name) {
                    // Command name in curly braces: {\echoOGO}
                    const char* text = extract_text_content(child, arena);
                    if (text && strlen(text) > 0) {
                        cmd_name = (text[0] == '\\') ? text + 1 : text;
                    }
                } else if (!replacement) {
                    // Replacement text: {+#1+}
                    replacement = extract_text_content(child, arena);
                }
            }
        }
    }
    
    if (!cmd_name || !replacement) {
        log_debug("doc_model: newcommand parse failed - name=%s, replacement=%s", 
                  cmd_name ? cmd_name : "(null)", replacement ? replacement : "(null)");
        return false;
    }
    
    // Register the macro
    log_debug("doc_model: registering macro \\%s with %d args, replacement='%s'", 
              cmd_name, num_args, replacement);
    doc->add_macro(cmd_name, num_args, replacement);
    return true;
}

// Expand a user-defined macro and return the result as a DocElement
// Returns nullptr if the tag is not a registered macro
static DocElement* try_expand_macro(const char* tag, const ElementReader& elem, 
                                     Arena* arena, TexDocumentModel* doc) {
    // Look up macro - macros are registered with backslash prefix
    char macro_name[256];
    snprintf(macro_name, sizeof(macro_name), "\\%s", tag);
    const TexDocumentModel::MacroDef* macro = doc->find_macro(macro_name);
    if (!macro) return nullptr;
    
    log_debug("doc_model: expanding macro %s, params='%s'", macro_name, macro->params ? macro->params : "");
    
    // Parse params string to understand argument positions
    // [] = optional (brack_group), {} = mandatory (curly_group or direct text)
    bool is_optional[9] = {false};
    int param_count = 0;
    int first_mandatory_pos = -1;
    if (macro->params) {
        for (const char* pp = macro->params; *pp && param_count < 9; pp++) {
            if (*pp == '[') {
                is_optional[param_count++] = true;
            } else if (*pp == '{') {
                if (first_mandatory_pos < 0) first_mandatory_pos = param_count;
                is_optional[param_count++] = false;
            }
        }
    }
    
    // Collect arguments from the element's children
    const char* args[9] = {nullptr};  // LaTeX supports up to 9 arguments
    int64_t child_count = elem.childCount();
    log_debug("doc_model: macro has %d params, %d children", param_count, (int)child_count);
    
    // Build list of actual arguments from children
    const char* provided_args[9] = {nullptr};
    int provided_count = 0;
    
    for (int64_t i = 0; i < child_count && provided_count < 9; i++) {
        ItemReader child = elem.childAt(i);
        
        if (child.isString()) {
            const char* text = child.cstring();
            if (!text) continue;
            
            // Skip pure whitespace
            bool is_whitespace = true;
            for (const char* p = text; *p; p++) {
                if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                    is_whitespace = false;
                    break;
                }
            }
            if (is_whitespace) continue;
            
            provided_args[provided_count++] = text;
        } else if (child.isElement()) {
            ElementReader ch_elem = child.asElement();
            const char* arg_text = extract_text_content(child, arena);
            if (arg_text && strlen(arg_text) > 0) {
                provided_args[provided_count++] = arg_text;
            }
        }
    }
    
    log_debug("doc_model: collected %d provided args", provided_count);
    
    // Now map provided_args to args based on param positions
    // Strategy: Optional args at the start that aren't provided are skipped
    // If params is "[]{}[]" and we get 1 arg, it goes to position 1 (first mandatory)
    // If params is "[]{}[]" and we get 2 args, first goes to pos 0, second to pos 1
    // If params is "[]{}[]" and we get 3 args, they go to pos 0, 1, 2 in order
    
    if (param_count > 0 && first_mandatory_pos >= 0) {
        // Count how many optional args are before the first mandatory
        int leading_optionals = first_mandatory_pos;
        
        // If we have fewer provided args than param_count, 
        // we assume leading optional args are empty
        int args_to_skip = 0;
        if (provided_count < param_count) {
            args_to_skip = param_count - provided_count;
            // But don't skip more than leading optionals
            if (args_to_skip > leading_optionals) {
                args_to_skip = leading_optionals;
            }
        }
        
        // Map provided args to positions
        int provided_idx = 0;
        for (int pos = 0; pos < param_count && provided_idx < provided_count; pos++) {
            if (pos < args_to_skip && is_optional[pos]) {
                // Skip this optional arg position
                args[pos] = "";
            } else {
                args[pos] = provided_args[provided_idx++];
                log_debug("doc_model: mapping arg[%d] = '%s'", pos, args[pos] ? args[pos] : "null");
            }
        }
    } else {
        // No params info or no mandatory - just use provided args directly
        for (int i = 0; i < provided_count && i < 9; i++) {
            args[i] = provided_args[i];
        }
    }
    
    // Perform substitution: replace #1, #2, etc. with actual arguments
    StrBuf* result = strbuf_new();
    const char* p = macro->replacement;
    while (*p) {
        if (*p == '#' && p[1] >= '1' && p[1] <= '9') {
            int arg_num = p[1] - '1';  // 0-indexed
            if (args[arg_num] && strlen(args[arg_num]) > 0) {
                strbuf_append_str(result, args[arg_num]);
            }
            p += 2;
        } else {
            strbuf_append_char(result, *p);
            p++;
        }
    }
    
    // Create a TEXT_RUN element with the expanded text
    const char* expanded_text = result->str;
    size_t expanded_len = result->length;
    
    if (expanded_len == 0) {
        strbuf_free(result);
        return nullptr;
    }
    
    // Copy to arena and create text element
    char* text_copy = (char*)arena_alloc(arena, expanded_len + 1);
    memcpy(text_copy, expanded_text, expanded_len + 1);
    strbuf_free(result);
    
    DocElement* text_elem = doc_alloc_element(arena, DocElemType::TEXT_RUN);
    text_elem->text.text = text_copy;
    text_elem->text.text_len = expanded_len;
    text_elem->text.style = DocTextStyle::plain();
    
    log_debug("doc_model: macro expanded to '%s'", text_copy);
    return text_elem;
}

// Check if a tag is a font declaration (changes style for following content)
static bool is_font_declaration_tag(const char* tag) {
    if (!tag) return false;
    return tag_eq(tag, "itshape") || tag_eq(tag, "bfseries") || tag_eq(tag, "ttfamily") ||
           tag_eq(tag, "scshape") || tag_eq(tag, "it") || tag_eq(tag, "bf") || tag_eq(tag, "tt") ||
           tag_eq(tag, "emph");
}

// Render brack_group content to HTML string
// Used for custom item labels like \item[\itshape text]
// Handles font declarations that affect following text
static const char* render_brack_group_to_html(const ItemReader& item, Arena* arena, TexDocumentModel* doc) {
    StrBuf* buf = strbuf_new();
    
    if (item.isElement()) {
        ElementReader elem = item.asElement();
        
        // First pass: check for font declaration followed by text
        // If pattern matches, wrap all content in styled span
        DocTextStyle active_style = DocTextStyle::plain();
        bool has_font_decl = false;
        
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* tag = child_elem.tagName();
                if (tag && is_font_declaration_tag(tag)) {
                    has_font_decl = true;
                    // Set style based on font declaration
                    if (tag_eq(tag, "itshape") || tag_eq(tag, "it") || tag_eq(tag, "emph")) {
                        active_style.flags |= DocTextStyle::ITALIC;
                    } else if (tag_eq(tag, "bfseries") || tag_eq(tag, "bf")) {
                        active_style.flags |= DocTextStyle::BOLD;
                    } else if (tag_eq(tag, "ttfamily") || tag_eq(tag, "tt")) {
                        active_style.flags |= DocTextStyle::MONOSPACE;
                    } else if (tag_eq(tag, "scshape")) {
                        active_style.flags |= DocTextStyle::SMALLCAPS;
                    }
                }
            }
        }
        
        // Second pass: collect content with applied style
        // If we have font declarations, create a span with that style containing all other content
        if (has_font_decl) {
            // Create outer wrapper span (expected format: <span><span class="it">text</span></span>)
            strbuf_append_str(buf, "<span>");
            
            // Create a styled inner wrapper
            DocElement* wrapper = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
            wrapper->text.style = active_style;
            
            // Iterate again and add non-font-declaration content
            bool first_content = true;
            iter = elem.children();
            while (iter.next(&child)) {
                if (child.isElement()) {
                    ElementReader child_elem = child.asElement();
                    const char* tag = child_elem.tagName();
                    if (tag && is_font_declaration_tag(tag)) {
                        // Skip font declarations - already applied to wrapper style
                        continue;
                    }
                }
                // Build and add to wrapper
                DocElement* child_elem = build_doc_element(child, arena, doc);
                if (child_elem) {
                    // Trim leading whitespace from first text content
                    if (first_content && child_elem->type == DocElemType::TEXT_RUN && 
                        child_elem->text.text && child_elem->text.text_len > 0) {
                        // Skip leading spaces
                        const char* text = child_elem->text.text;
                        size_t len = child_elem->text.text_len;
                        while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\n')) {
                            text++;
                            len--;
                        }
                        if (len > 0) {
                            // Create new text run with trimmed content
                            child_elem = doc_create_text_cstr(arena, text, child_elem->text.style);
                            first_content = false;
                        } else {
                            continue;  // Skip empty text
                        }
                    } else {
                        first_content = false;
                    }
                    doc_append_child(wrapper, child_elem);
                }
            }
            
            // Render the styled wrapper
            HtmlOutputOptions opts = HtmlOutputOptions::hybrid();
            doc_element_to_html(wrapper, buf, opts, 0);
            
            // Close outer wrapper
            strbuf_append_str(buf, "</span>");
        } else {
            // No font declarations - render children directly
            iter = elem.children();
            while (iter.next(&child)) {
                DocElement* child_elem = build_doc_element(child, arena, doc);
                if (child_elem) {
                    HtmlOutputOptions opts = HtmlOutputOptions::hybrid();
                    doc_element_to_html(child_elem, buf, opts, 0);
                }
            }
        }
    }
    
    // Allocate result in arena
    const char* result = nullptr;
    if (buf->length > 0) {
        char* copy = (char*)arena_alloc(arena, buf->length + 1);
        memcpy(copy, buf->str, buf->length);
        copy[buf->length] = '\0';
        result = copy;
    }
    strbuf_free(buf);
    return result;
}

// Helper to set style flags based on font command name
static void build_text_command_set_style(const char* cmd_name, DocTextStyle* style) {
    *style = DocTextStyle::plain();
    
    // Style flags
    if (tag_eq(cmd_name, "textbf") || tag_eq(cmd_name, "bf") || tag_eq(cmd_name, "bfseries")) {
        style->flags |= DocTextStyle::BOLD;
    } else if (tag_eq(cmd_name, "textit") || tag_eq(cmd_name, "it") || tag_eq(cmd_name, "itshape")) {
        style->flags |= DocTextStyle::ITALIC;
    } else if (tag_eq(cmd_name, "textsl") || tag_eq(cmd_name, "sl") || tag_eq(cmd_name, "slshape")) {
        style->flags |= DocTextStyle::SLANTED;
    } else if (tag_eq(cmd_name, "textup") || tag_eq(cmd_name, "upshape")) {
        style->flags |= DocTextStyle::UPRIGHT;
    } else if (tag_eq(cmd_name, "emph")) {
        // \emph uses EMPHASIS flag for context-dependent toggling
        style->flags |= DocTextStyle::EMPHASIS;
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
    } else if (tag_eq(cmd_name, "textit") || tag_eq(cmd_name, "it") || tag_eq(cmd_name, "itshape")) {
        span->text.style.flags |= DocTextStyle::ITALIC;
    } else if (tag_eq(cmd_name, "textsl") || tag_eq(cmd_name, "sl") || tag_eq(cmd_name, "slshape")) {
        span->text.style.flags |= DocTextStyle::SLANTED;
    } else if (tag_eq(cmd_name, "textup") || tag_eq(cmd_name, "upshape")) {
        span->text.style.flags |= DocTextStyle::UPRIGHT;
    } else if (tag_eq(cmd_name, "emph")) {
        // \emph uses EMPHASIS flag for context-dependent toggling
        span->text.style.flags |= DocTextStyle::EMPHASIS;
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
    DocElement* last_text_child = nullptr;  // track last text run for trailing space trimming
    while (iter.next(&child)) {
        DocElement* child_elem = build_inline_content(child, arena, doc);
        if (child_elem) {
            // If this is a heading and previous was a text run, trim trailing space
            if (child_elem->type == DocElemType::HEADING && last_text_child &&
                last_text_child->type == DocElemType::TEXT_RUN && last_text_child->text.text) {
                char* text_content = (char*)last_text_child->text.text;
                size_t len = strlen(text_content);
                // Trim trailing whitespace
                while (len > 0 && (text_content[len-1] == ' ' || text_content[len-1] == '\t' || text_content[len-1] == '\n')) {
                    text_content[--len] = '\0';
                }
            }
            doc_append_child(span, child_elem);
            // Track text elements for trailing space trimming
            if (child_elem->type == DocElemType::TEXT_RUN || child_elem->type == DocElemType::TEXT_SPAN) {
                last_text_child = child_elem;
            } else {
                last_text_child = nullptr;
            }
        }
    }
    
    return span;
}

// Build a HEADING element from section command
static DocElement* build_section_command(const char* cmd_name, const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc) {
    log_debug("build_section_command: cmd_name='%s'", cmd_name);
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
    
    // First pass: check for starred version
    auto first_iter = elem.children();
    ItemReader first_child;
    bool has_star = false;
    
    while (first_iter.next(&first_child)) {
        if (first_child.isElement()) {
            ElementReader child_elem = first_child.asElement();
            const char* tag = child_elem.tagName();
            if (tag_eq(tag, "star") || tag_eq(tag, "*")) {
                has_star = true;
                break;
            }
        }
    }
    
    // Generate ID and number for non-starred sections
    if (has_star) {
        heading->flags |= DocElement::FLAG_STARRED;
    } else {
        heading->flags |= DocElement::FLAG_NUMBERED;
        
        // Generate global section ID (sec-1, sec-2, sec-3, etc.)
        doc->section_id_counter++;
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "sec-%d", doc->section_id_counter);
        size_t id_len = strlen(id_buf);
        char* id_str = (char*)arena_alloc(arena, id_len + 1);
        memcpy(id_str, id_buf, id_len + 1);
        heading->heading.label = id_str;  // Store as label so render uses it as id
        
        // Generate section number for display
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
        
        // Set current ref context for any \label commands in the title
        doc->current_ref_id = heading->heading.label;
        doc->current_ref_text = heading->heading.number;
    }
    
    // Second pass: extract title and process labels
    // Check for title attribute first (parser may output as attribute)
    if (elem.has_attr("title")) {
        ItemReader title_item = elem.get_attr("title");
        heading->heading.title = extract_text_content(title_item, arena);
        // Also process labels in the title
        if (title_item.isElement()) {
            process_labels_in_element(title_item, arena, doc, heading);
        }
    }
    
    // Check for title in children AND process any label elements
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag_eq(tag, "star") || tag_eq(tag, "*")) {
                // Already handled
            } else if (tag && tag_eq(tag, "label")) {
                // Direct label child - process it with the heading as parent
                log_debug("build_section_command: found direct label child");
                process_label_command(child_elem, arena, doc, heading);
            } else if (!heading->heading.title &&
                       (tag_eq(tag, "curly_group") || tag_eq(tag, "title") || 
                        tag_eq(tag, "brack_group") || tag_eq(tag, "text") || 
                        tag_eq(tag, "arg"))) {
                // Extract title text (only if not already set from attribute)
                heading->heading.title = extract_text_content(child, arena);
                // Process any labels inside the title group
                process_labels_in_element(child, arena, doc, heading);
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
    
    // Note: Don't clear current_ref_id/current_ref_text here
    // Labels appearing after \section should still associate with this section
    // The context will be overwritten when the next section is processed
    
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
        tag_eq(tag, "emph") || tag_eq(tag, "textsc") || tag_eq(tag, "underline") ||
        tag_eq(tag, "textup") || tag_eq(tag, "textsl")) {
        return build_text_command(tag, elem, arena, doc);
    }
    
    // \char command - character by code point
    // Formats: \char98 (decimal), \char"A0 (hex), \char'141 (octal)
    if (tag_eq(tag, "char_command")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* cmd_text = child.cstring();
            // cmd_text is like "\\char98" or "\\char\"A0" or "\\char'141"
            if (cmd_text && strncmp(cmd_text, "\\char", 5) == 0) {
                const char* num_part = cmd_text + 5;  // skip "\\char"
                long code_point = 0;
                if (*num_part == '"') {
                    // hex: \char"A0
                    code_point = strtol(num_part + 1, nullptr, 16);
                } else if (*num_part == '\'') {
                    // octal: \char'141
                    code_point = strtol(num_part + 1, nullptr, 8);
                } else {
                    // decimal: \char98
                    code_point = strtol(num_part, nullptr, 10);
                }
                // Convert code point to UTF-8
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;  // failed to parse char_command
    }
    
    // \verb|text| - inline verbatim with arbitrary delimiter
    // Format: \verb<delim>content<delim> or \verb*<delim>content<delim>
    // Expected output: <code class="tt">content</code>
    // For \verb*, spaces are shown as ␣ (U+2423)
    if (tag_eq(tag, "verb_command")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* verb_text = child.cstring();
            // verb_text is like "\\verb|text|" or "\\verb*|text|"
            if (verb_text && strncmp(verb_text, "\\verb", 5) == 0) {
                const char* content_start = verb_text + 5;  // skip "\\verb"
                bool is_starred = false;
                if (*content_start == '*') {
                    is_starred = true;
                    content_start++;
                }
                // The next character is the delimiter
                if (*content_start) {
                    char delim = *content_start;
                    content_start++;  // skip delimiter
                    // Find the closing delimiter
                    const char* content_end = strchr(content_start, delim);
                    if (content_end) {
                        size_t content_len = content_end - content_start;
                        // Build the output
                        StrBuf* out = strbuf_new_cap(content_len * 4 + 64);
                        strbuf_append_str(out, "<code class=\"tt\">");
                        // Process content - escape HTML entities, handle spaces for verb*
                        for (size_t i = 0; i < content_len; i++) {
                            char c = content_start[i];
                            if (c == ' ' && is_starred) {
                                // For \verb*, show visible space ␣ (U+2423)
                                strbuf_append_str(out, "\xE2\x90\xA3");
                            } else if (c == '<') {
                                strbuf_append_str(out, "&lt;");
                            } else if (c == '>') {
                                strbuf_append_str(out, "&gt;");
                            } else if (c == '&') {
                                strbuf_append_str(out, "&amp;");
                            } else {
                                strbuf_append_char(out, c);
                            }
                        }
                        strbuf_append_str(out, "</code>");
                        
                        char* html_copy = (char*)arena_alloc(arena, out->length + 1);
                        memcpy(html_copy, out->str, out->length + 1);
                        strbuf_free(out);
                        
                        return doc_create_raw_html_cstr(arena, html_copy);
                    }
                }
            }
        }
        return nullptr;  // failed to parse verb_command
    }
    
    // TeX caret notation: ^^XX (2 hex digits) or ^^^^XXXX (4 hex digits) or ^^c (char XOR 64)
    if (tag_eq(tag, "caret_char")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* caret_text = child.cstring();
            if (caret_text && strncmp(caret_text, "^^", 2) == 0) {
                const char* after_caret = caret_text + 2;
                long code_point = 0;
                if (strncmp(after_caret, "^^", 2) == 0) {
                    code_point = strtol(after_caret + 2, nullptr, 16);
                } else {
                    size_t len = strlen(after_caret);
                    if (len == 2 && isxdigit(after_caret[0]) && isxdigit(after_caret[1])) {
                        code_point = strtol(after_caret, nullptr, 16);
                    } else if (len == 1) {
                        code_point = (unsigned char)after_caret[0] ^ 64;
                    } else {
                        code_point = strtol(after_caret, nullptr, 16);
                    }
                }
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;
    }
    // \symbol{} command - character by code point
    if (tag_eq(tag, "symbol")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* arg = child.cstring();
            if (arg) {
                while (*arg && (*arg == ' ' || *arg == '\t')) arg++;
                long code_point = 0;
                if (*arg == '"') {
                    code_point = strtol(arg + 1, nullptr, 16);
                } else if (*arg == '\'') {
                    code_point = strtol(arg + 1, nullptr, 8);
                } else if (*arg == '`') {
                    if (arg[1]) code_point = (unsigned char)arg[1];
                } else {
                    code_point = strtol(arg, nullptr, 10);
                }
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;
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
    // LaTeX/TeX logos - styled HTML spans
    if (tag_eq(tag, "LaTeX")) {
        return doc_create_raw_html_cstr(arena,
            "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
    }
    if (tag_eq(tag, "TeX")) {
        return doc_create_raw_html_cstr(arena,
            "<span class=\"tex\">T<span class=\"e\">e</span>X</span>");
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
    // Spacing commands that output Unicode space characters
    // U+2003 Em Space for \quad
    if (tag_eq(tag, "quad")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x83", DocTextStyle::plain());  // em space
    }
    // U+2003 Em Space twice for \qquad
    if (tag_eq(tag, "qquad")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x83\xE2\x80\x83", DocTextStyle::plain());  // 2x em space
    }
    // U+2002 En Space for \enspace/\enskip
    if (tag_eq(tag, "enspace") || tag_eq(tag, "enskip")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x82", DocTextStyle::plain());  // en space
    }
    // U+2009 Thin Space for \thinspace/\,
    if (tag_eq(tag, "thinspace")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x89", DocTextStyle::plain());  // thin space
    }
    // Negative thin space needs CSS styling
    if (tag_eq(tag, "negthinspace")) {
        return doc_create_raw_html_cstr(arena, "<span class=\"negthinspace\"></span>");
    }
    // \hspace{...} - horizontal space with specified width
    if (tag_eq(tag, "hspace")) {
        // Get width from child text content using extract_text_content
        const char* width_str = extract_text_content(item, arena);
        if (width_str && strlen(width_str) > 0) {
            // Parse length and convert to pixels
            char* end = nullptr;
            double num = strtod(width_str, &end);  // use double for precision
            if (end != width_str) {
                // Skip whitespace
                while (end && *end == ' ') end++;
                // Handle units - use precise conversion factors
                double width_px = num;  // default: assume pixels
                if (end && *end) {
                    if (strncmp(end, "pt", 2) == 0) {
                        width_px = num * (96.0 / 72.0);  // 1pt = 1/72 in
                    } else if (strncmp(end, "cm", 2) == 0) {
                        width_px = num * (96.0 / 2.54);  // 1cm = 96/2.54 px
                    } else if (strncmp(end, "mm", 2) == 0) {
                        width_px = num * (96.0 / 25.4);  // 1mm = 96/25.4 px
                    } else if (strncmp(end, "in", 2) == 0) {
                        width_px = num * 96.0;           // 1in = 96px
                    } else if (strncmp(end, "em", 2) == 0) {
                        width_px = num * 16.0;           // assume 1em = 16px
                    }
                }
                if (width_px > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "<span style=\"margin-right:%.3fpx\"></span>", width_px);
                    return doc_create_raw_html_cstr(arena, buf);
                }
            }
        }
        // Default: output a regular space if width not specified
        return doc_create_text_cstr(arena, " ", DocTextStyle::plain());
    }
    // Non-breaking space
    if (tag_eq(tag, "nobreakspace") || tag_eq(tag, "nbsp")) {
        return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
    }
    
    // Diacritic commands (single character tags like ^, ", ', `, ~, =, ., u, v, H, c, d, b, r, k)
    // These apply an accent to the following character
    if (is_diacritic_tag(tag)) {
        char diacritic_cmd = tag[0];
        
        // Get the base character from children
        const char* base_char = nullptr;
        bool has_empty_curly_group = false;
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isString()) {
                const char* text = child.cstring();
                if (text && text[0] != '\0') {
                    // Get first character only (may be multi-byte UTF-8)
                    base_char = text;
                    break;
                }
            } else if (child.isElement()) {
                // Could be a curly_group containing the character
                ElementReader ch_elem = child.asElement();
                const char* ch_tag = ch_elem.tagName();
                if (ch_tag && (tag_eq(ch_tag, "curly_group") || tag_eq(ch_tag, "group"))) {
                    // Extract text from group
                    base_char = extract_text_content(child, arena);
                    if (base_char && strlen(base_char) > 0) {
                        break;
                    }
                    // Empty curly_group (like \^{})
                    has_empty_curly_group = true;
                }
            }
        }
        
        if (base_char && strlen(base_char) > 0) {
            const char* result = apply_diacritic(diacritic_cmd, base_char, arena);
            if (result) {
                return doc_create_text_cstr(arena, result, DocTextStyle::plain());
            }
        }
        // Empty curly group: output diacritic char + ZWS (e.g., \^{} → ^​)
        if (has_empty_curly_group) {
            char buf[8];
            buf[0] = diacritic_cmd;
            buf[1] = '\xE2';  // U+200B ZWS
            buf[2] = '\x80';
            buf[3] = '\x8B';
            buf[4] = '\0';
            return doc_create_text_cstr(arena, buf, DocTextStyle::plain());
        }
        return nullptr;
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
                tag_eq(cmd_name, "textsc") || tag_eq(cmd_name, "underline") ||
                tag_eq(cmd_name, "textup") || tag_eq(cmd_name, "textsl")) {
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
            // LaTeX/TeX logos - styled HTML spans
            if (tag_eq(cmd_name, "LaTeX")) {
                return doc_create_raw_html_cstr(arena,
                    "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            }
            if (tag_eq(cmd_name, "TeX")) {
                return doc_create_raw_html_cstr(arena,
                    "<span class=\"tex\">T<span class=\"e\">e</span>X</span>");
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
            // Spacing commands that output Unicode space characters
            // U+2003 Em Space for \quad
            if (tag_eq(cmd_name, "quad")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x83", DocTextStyle::plain());
            }
            // U+2003 Em Space twice for \qquad
            if (tag_eq(cmd_name, "qquad")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x83\xE2\x80\x83", DocTextStyle::plain());
            }
            // U+2002 En Space for \enspace/\enskip
            if (tag_eq(cmd_name, "enspace") || tag_eq(cmd_name, "enskip")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x82", DocTextStyle::plain());
            }
            // U+2009 Thin Space for \thinspace/\,
            if (tag_eq(cmd_name, "thinspace")) {
                return doc_create_text_cstr(arena, "\xE2\x80\x89", DocTextStyle::plain());
            }
            // Negative thin space needs CSS styling
            if (tag_eq(cmd_name, "negthinspace")) {
                return doc_create_raw_html_cstr(arena, "<span class=\"negthinspace\"></span>");
            }
            // \hspace{...} - horizontal space with specified width
            if (tag_eq(cmd_name, "hspace")) {
                // Get width from the argument (next curly_group)
                const char* width_str = nullptr;
                auto iter2 = elem.children();
                ItemReader arg;
                while (iter2.next(&arg)) {
                    if (arg.isElement()) {
                        ElementReader arg_elem = arg.asElement();
                        const char* arg_tag = arg_elem.tagName();
                        if (tag_eq(arg_tag, "curly_group") || tag_eq(arg_tag, "group")) {
                            width_str = extract_text_content(arg, arena);
                            break;
                        }
                    }
                }
                if (width_str && width_str[0]) {
                    // Parse length and convert to pixels
                    char* end = nullptr;
                    double num = strtod(width_str, &end);  // use double for precision
                    if (end != width_str) {
                        while (end && *end == ' ') end++;
                        double width_px = num;  // default: assume pixels
                        if (end && *end) {
                            if (strncmp(end, "pt", 2) == 0) {
                                width_px = num * (96.0 / 72.0);
                            } else if (strncmp(end, "cm", 2) == 0) {
                                width_px = num * (96.0 / 2.54);
                            } else if (strncmp(end, "mm", 2) == 0) {
                                width_px = num * (96.0 / 25.4);
                            } else if (strncmp(end, "in", 2) == 0) {
                                width_px = num * 96.0;
                            } else if (strncmp(end, "em", 2) == 0) {
                                width_px = num * 16.0;
                            }
                        }
                        if (width_px > 0) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "<span style=\"margin-right:%.3fpx\"></span>", width_px);
                            return doc_create_raw_html_cstr(arena, buf);
                        }
                    }
                }
                // Default: output a regular space if width not specified
                return doc_create_text_cstr(arena, " ", DocTextStyle::plain());
            }
            // Non-breaking space
            if (tag_eq(cmd_name, "nobreakspace") || tag_eq(cmd_name, "nbsp")) {
                return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
            }
        }
    }
    
    // Curly group - process children, adding ZWSP at whitespace boundaries
    // ZWSP is always added at the end to mark the } boundary
    // sequence is a pseudo-element from error recovery that just groups inline_math children
    if (tag_eq(tag, "curly_group") || tag_eq(tag, "brack_group") || tag_eq(tag, "group") ||
        tag_eq(tag, "sequence")) {
        
        // Check if curly_group contains only an environment name (from broken \begin{...} or \end{...})
        // If so, skip it entirely to avoid spurious text output
        int64_t child_count = elem.childCount();
        if (child_count == 1) {
            ItemReader only_child = elem.childAt(0);
            if (only_child.isString()) {
                const char* content = only_child.cstring();
                if (content) {
                    // List of environment names that should be filtered out
                    static const char* env_names[] = {
                        "center", "itshape", "document", "bfseries", "mdseries",
                        "slshape", "upshape", "scshape", "rmfamily", "sffamily",
                        "ttfamily", "tiny", "scriptsize", "footnotesize", "small",
                        "normalsize", "large", "Large", "LARGE", "huge", "Huge",
                        "abstract", "itemize", "enumerate", "description",
                        "quote", "quotation", "verse", "flushleft", "flushright",
                        "verbatim", "picture", "minipage", "tabular", "table",
                        "figure", "multicols", "equation", "align", "gather"
                    };
                    for (size_t i = 0; i < sizeof(env_names)/sizeof(env_names[0]); i++) {
                        if (tag_eq(content, env_names[i])) {
                            return nullptr;  // Skip this curly_group - it's a spurious env name
                        }
                    }
                }
            }
        }
        
        DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        span->text.style = DocTextStyle::plain();
        
        // sequence elements from error recovery don't need ZWSP boundaries
        bool is_sequence = tag_eq(tag, "sequence");
        
        // Check if group starts with whitespace - add ZWSP
        bool starts_with_space = false;
        bool ends_with_space = false;
        bool has_content = false;
        
        // Scan children to detect whitespace at boundaries
        auto scan_iter = elem.children();
        ItemReader scan_child;
        bool first = true;
        while (scan_iter.next(&scan_child)) {
            if (scan_child.isString()) {
                const char* text = scan_child.cstring();
                if (text && strlen(text) > 0) {
                    if (first && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n')) {
                        starts_with_space = true;
                    }
                    // Check last char for ends_with_space
                    size_t len = strlen(text);
                    char last = text[len - 1];
                    if (last == ' ' || last == '\t' || last == '\n') {
                        ends_with_space = true;
                    } else {
                        ends_with_space = false;
                    }
                    // Check if this string has non-whitespace content
                    for (const char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n') {
                            has_content = true;
                            break;
                        }
                    }
                    first = false;
                }
            } else if (scan_child.isElement()) {
                has_content = true;
                first = false;
                ends_with_space = false;  // Element at end means no trailing space
            }
        }
        
        // Add ZWSP at start if group has leading whitespace (not for sequence)
        if (starts_with_space && !is_sequence) {
            doc_append_child(span, doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain()));
        }
        
        // Track active font flags within this group for \em toggle behavior
        uint32_t active_font_flags = 0;
        
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            DocElement* child_elem = build_inline_content(child, arena, doc);
            if (child_elem) {
                // Handle font markers with toggle logic for \em
                if (is_font_marker(child_elem)) {
                    uint32_t new_flags = font_marker_to_style_flags(child_elem);
                    if (new_flags == DocTextStyle::EMPHASIS) {
                        // \em toggles between italic and upright
                        bool currently_italic = (active_font_flags & DocTextStyle::ITALIC) != 0;
                        bool currently_upright = (active_font_flags & DocTextStyle::UPRIGHT) != 0;
                        active_font_flags &= ~(DocTextStyle::ITALIC | DocTextStyle::UPRIGHT);
                        if (currently_italic) {
                            active_font_flags |= DocTextStyle::UPRIGHT;
                        } else if (currently_upright) {
                            active_font_flags |= DocTextStyle::ITALIC;
                        } else {
                            active_font_flags |= DocTextStyle::ITALIC;
                        }
                    } else {
                        active_font_flags |= new_flags;
                    }
                    continue;  // Don't add marker to output
                }
                
                // Wrap content with active font flags if any
                if (active_font_flags != 0) {
                    DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                    styled_span->text.style = DocTextStyle::plain();
                    styled_span->text.style.flags = active_font_flags;
                    doc_append_child(styled_span, child_elem);
                    doc_append_child(span, styled_span);
                } else {
                    doc_append_child(span, child_elem);
                }
            }
        }
        
        // Add ZWSP at end of curly_group/brack_group/group (not for sequence):
        // - If trailing whitespace inside: marks the boundary
        // - If content but no trailing whitespace: marks the } boundary for word-break
        if (!is_sequence && (ends_with_space || has_content)) {
            doc_append_child(span, doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain()));
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
    
    // Cross-reference commands (inline)
    if (tag_eq(tag, "ref") || tag_eq(tag, "eqref") || tag_eq(tag, "pageref")) {
        return build_ref_command(elem, arena, doc);
    }
    
    // Label commands - register with document but produce no visible output
    if (tag_eq(tag, "label")) {
        process_label_command(elem, arena, doc, nullptr);
        return nullptr;  // Labels don't produce visible output
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
    
    // Space command - handles various spacing commands
    // \<space>, \<tab>, \<newline> → ZWSP + space (boundary marker)
    // \, → thin space (U+2009)
    // \- → soft hyphen (U+00AD)
    // \; → thick space (like \enspace)
    // \! → negative thin space (nothing in HTML)
    if (tag_eq(tag, "space_cmd")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* cmd = child.cstring();
            if (cmd && strlen(cmd) >= 2) {
                char space_char = cmd[1];
                if (space_char == ',') {
                    // Thin space U+2009
                    return doc_create_text_cstr(arena, "\xE2\x80\x89", DocTextStyle::plain());
                } else if (space_char == '-') {
                    // Soft hyphen U+00AD (discretionary hyphen)
                    return doc_create_text_cstr(arena, "\xC2\xAD", DocTextStyle::plain());
                } else if (space_char == ';') {
                    // Thick space - use space element
                    DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
                    space->space.is_linebreak = false;
                    return space;
                } else if (space_char == '!') {
                    // Negative thin space - absorbs space, output nothing
                    return nullptr;
                }
            }
        }
        // Default: ZWSP + space (for \ <space>, \<tab>, \<newline>)
        // ZWSP marks boundary, space is visible
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
    
    // Sectioning commands (unusual but possible: \section inside \emph{})
    // Output as heading element - will be rendered inline (technically invalid HTML but expected)
    if (tag_eq(tag, "section") || tag_eq(tag, "subsection") || tag_eq(tag, "subsubsection") ||
        tag_eq(tag, "chapter") || tag_eq(tag, "part") || tag_eq(tag, "paragraph") || tag_eq(tag, "subparagraph")) {
        return build_section_command(tag, elem, arena, doc);
    }
    
    // Block-level elements that can appear inside paragraphs - delegate to build_doc_element
    // This handles cases like \begin{itemize}...\end{itemize} inside a paragraph
    if (is_block_element_tag(tag)) {
        return build_doc_element(item, arena, doc);
    }
    
    // Try to expand as user-defined macro
    DocElement* macro_result = try_expand_macro(tag, elem, arena, doc);
    if (macro_result) {
        return macro_result;
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

// Try to apply a diacritic command to the next child in sequence
// Returns true if diacritic was applied, false otherwise
// next_child will be advanced if diacritic was consumed
static bool try_apply_diacritic(char diacritic_cmd, ElementReader::ChildIterator& iter, ItemReader* next_child,
                                 DocElement* parent, Arena* arena, TexDocumentModel* doc) {
    // Try to peek at the next child
    ItemReader peek;
    if (!iter.next(&peek)) {
        return false;  // No more children
    }
    
    const char* base_char = nullptr;
    bool consumed = false;
    
    if (peek.isString()) {
        const char* text = peek.cstring();
        if (text && text[0] != '\0') {
            // Apply diacritic to first character, return rest as separate text
            const char* result = apply_diacritic(diacritic_cmd, text, arena);
            if (result) {
                DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                if (text_elem) {
                    doc_append_child(parent, text_elem);
                }
                
                // Check if there are more characters after the first one
                int char_len = utf8_char_len((unsigned char)text[0]);
                if (text[char_len] != '\0') {
                    DocElement* rest = doc_create_text_cstr(arena, text + char_len, DocTextStyle::plain());
                    if (rest) {
                        doc_append_child(parent, rest);
                    }
                }
                return true;
            }
        }
    } else if (peek.isElement()) {
        // Could be a curly_group or other element containing the base character
        ElementReader peek_elem = peek.asElement();
        const char* peek_tag = peek_elem.tagName();
        if (peek_tag && (tag_eq(peek_tag, "curly_group") || tag_eq(peek_tag, "group"))) {
            // Extract text from the group and apply diacritic
            const char* text = extract_text_content(peek, arena);
            if (text && text[0] != '\0') {
                const char* result = apply_diacritic(diacritic_cmd, text, arena);
                if (result) {
                    DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(parent, text_elem);
                    }
                    return true;
                }
            }
        }
    }
    
    // Could not apply diacritic - process the peeked child normally and put current child back
    // Actually we need to process the peeked item normally
    *next_child = peek;
    return false;
}

// Build a paragraph element with diacritic merging
static DocElement* build_paragraph(const ElementReader& elem, Arena* arena,
                                    TexDocumentModel* doc) {
    DocElement* para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        // Check if this is a diacritic command element
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && is_diacritic_tag(tag)) {
                // Check if diacritic has a child (braced form like \^{o})
                auto child_iter = child_elem.children();
                ItemReader diacritic_child;
                bool has_child = child_iter.next(&diacritic_child);
                
                if (has_child) {
                    // Braced form: apply to the contained text
                    const char* base_text = nullptr;
                    if (diacritic_child.isString()) {
                        base_text = diacritic_child.cstring();
                    } else {
                        base_text = extract_text_content(diacritic_child, arena);
                    }
                    if (base_text && base_text[0] != '\0') {
                        const char* result = apply_diacritic(tag[0], base_text, arena);
                        if (result) {
                            DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                            if (text_elem) {
                                doc_append_child(para, text_elem);
                            }
                            continue;
                        }
                    }
                } else {
                    // Unbraced form: try to apply to next sibling
                    if (try_apply_diacritic(tag[0], iter, &child, para, arena, doc)) {
                        continue;
                    }
                    // If try_apply_diacritic returned false, child now holds the peeked item
                    // Fall through to process it normally
                }
            }
        }
        
        DocElement* child_elem = build_inline_content(child, arena, doc);
        if (child_elem) {
            doc_append_child(para, child_elem);
        }
    }
    
    // Trim leading/trailing whitespace from paragraph
    if (para->first_child) {
        trim_paragraph_whitespace(para, arena);
    }
    
    // Check if paragraph has any actual content (not just empty text)
    bool has_content = false;
    for (DocElement* ch = para->first_child; ch && !has_content; ch = ch->next_sibling) {
        if (ch->type == DocElemType::TEXT_RUN) {
            if (ch->text.text && ch->text.text_len > 0) {
                has_content = true;
            }
        } else {
            // Non-text elements count as content
            has_content = true;
        }
    }
    
    return has_content ? para : nullptr;
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

// Helper to check if a tag is a block element (list, alignment, etc.)
static bool is_block_element_tag(const char* tag) {
    if (!tag) return false;
    return tag_eq(tag, "itemize") || tag_eq(tag, "enumerate") || tag_eq(tag, "description") ||
           tag_eq(tag, "center") || tag_eq(tag, "quote") || tag_eq(tag, "quotation") ||
           tag_eq(tag, "verse") || tag_eq(tag, "flushleft") || tag_eq(tag, "flushright");
}

// Helper to check if a tag is a document-level block (section, environment, etc.)
// These should not be wrapped in TEXT_SPAN
static bool is_document_block_tag(const char* tag) {
    if (!tag) return false;
    // Sectioning
    if (tag_eq(tag, "section") || tag_eq(tag, "subsection") || tag_eq(tag, "subsubsection") ||
        tag_eq(tag, "paragraph") || tag_eq(tag, "subparagraph") ||
        tag_eq(tag, "chapter") || tag_eq(tag, "part")) {
        return true;
    }
    // Document structure
    if (tag_eq(tag, "latex_document") || tag_eq(tag, "document") || tag_eq(tag, "document_body") ||
        tag_eq(tag, "body") || tag_eq(tag, "preamble")) {
        return true;
    }
    // Other block-level
    return is_block_element_tag(tag);
}

// Helper to process items from a content container
// Each list item can contain multiple paragraphs separated by parbreaks
static void process_list_content(DocElement* list, const ItemReader& container,
                                  Arena* arena, TexDocumentModel* doc, int& item_number) {
    if (!container.isElement()) return;
    
    bool list_centered = (list->flags & DocElement::FLAG_CENTERED) != 0;
    ElementReader elem = container.asElement();
    DocElement* current_item = nullptr;
    DocElement* current_para = nullptr;  // Current paragraph within item
    bool at_item_start = true;  // For trimming leading whitespace
    
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            // Handle centering command - set list flag for subsequent items
            if (child_tag && tag_eq(child_tag, "centering")) {
                list->flags |= DocElement::FLAG_CENTERED;
                list_centered = true;
                continue;
            }
            
            if (child_tag && tag_eq(child_tag, "item")) {
                // Finalize current paragraph if exists
                if (current_para && current_para->first_child && current_item) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                // Save previous item if exists
                if (current_item && current_item->first_child) {
                    doc_append_child(list, current_item);
                }
                // Start new item
                current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                if (list_centered) {
                    current_item->flags |= DocElement::FLAG_CENTERED;
                }
                // Start first paragraph in item
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                if (list_centered) {
                    current_para->flags |= DocElement::FLAG_CENTERED;
                }
                at_item_start = true;
                
                // First, check if item has custom label (brack_group)
                bool has_brack_group = false;
                auto peek_iter = child_elem.children();
                ItemReader peek_child;
                while (peek_iter.next(&peek_child)) {
                    if (peek_child.isElement()) {
                        ElementReader peek_elem = peek_child.asElement();
                        const char* peek_tag = peek_elem.tagName();
                        if (peek_tag && tag_eq(peek_tag, "brack_group")) {
                            has_brack_group = true;
                            break;
                        }
                    }
                }
                
                // Assign item number only if no custom label (enumerate only)
                if (list->list.list_type == ListType::ENUMERATE && !has_brack_group) {
                    current_item->list_item.item_number = item_number++;
                }
                
                // Process item children for brack_group labels
                auto item_iter = child_elem.children();
                ItemReader item_child;
                while (item_iter.next(&item_child)) {
                    if (item_child.isElement()) {
                        ElementReader item_child_elem = item_child.asElement();
                        const char* item_child_tag = item_child_elem.tagName();
                        if (item_child_tag && tag_eq(item_child_tag, "brack_group")) {
                            // Custom label via \item[label]
                            current_item->list_item.has_custom_label = true;
                            current_item->list_item.label = extract_text_content(item_child, arena);
                            current_item->list_item.html_label = render_brack_group_to_html(item_child, arena, doc);
                        } else {
                            DocElement* content = build_doc_element(item_child, arena, doc);
                            if (content) {
                                doc_append_child(current_para, content);
                                at_item_start = false;
                            }
                        }
                    }
                }
            } else if (child_tag && (tag_eq(child_tag, "paragraph") || 
                                     tag_eq(child_tag, "text_mode") ||
                                     tag_eq(child_tag, "content"))) {
                // Recurse into nested content containers
                process_list_content(list, child, arena, doc, item_number);
            } else if (child_tag && is_block_element_tag(child_tag) && current_item) {
                // Block element (nested list, etc.) - close current paragraph first
                if (current_para && current_para->first_child) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                // Add block element directly to item (not to paragraph)
                DocElement* content = build_doc_element(child, arena, doc);
                if (content && !is_special_marker(content)) {
                    doc_append_child(current_item, content);
                }
                // Start new paragraph for any following content
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                at_item_start = true;
            } else if (current_item && current_para) {
                // Add content to current paragraph
                DocElement* content = build_doc_element(child, arena, doc);
                if (content && !is_special_marker(content)) {
                    doc_append_child(current_para, content);
                    at_item_start = false;
                } else if (content == NOINDENT_MARKER) {
                    // Apply noindent to current paragraph
                    current_para->flags |= DocElement::FLAG_NOINDENT;
                } else if (is_alignment_marker(content)) {
                    // Apply alignment to current paragraph
                    apply_alignment_to_paragraph(current_para, marker_to_alignment(content));
                }
            }
        } else if (child.isSymbol()) {
            // Check for parbreak symbol
            String* sym = child.asSymbol();
            if (sym && strcmp(sym->chars, "parbreak") == 0 && current_item && current_para) {
                // Finalize current paragraph and start a new one
                if (current_para->first_child) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                at_item_start = true;  // Trim leading whitespace in new paragraph
            }
        } else if (child.isString() && current_item && current_para) {
            // String content for current paragraph
            const char* text = child.cstring();
            if (text && strlen(text) > 0) {
                // Skip whitespace-only strings at start of item/paragraph
                const char* p = text;
                if (at_item_start) {
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                }
                if (*p) {
                    DocElement* text_elem = doc_create_text_cstr(arena, at_item_start ? p : text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_para, text_elem);
                        at_item_start = false;
                    }
                }
            }
        }
    }
    
    // Finalize last paragraph
    if (current_para && current_para->first_child && current_item) {
        trim_paragraph_whitespace(current_para, arena);
        doc_append_child(current_item, current_para);
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
    bool list_centered = false;  // Track if \centering command was encountered
    
    // Process children, looking for \item commands
    // Items may be directly under the environment or nested inside paragraph elements
    auto iter = elem.children();
    ItemReader child;
    DocElement* current_item = nullptr;
    DocElement* current_para = nullptr;
    bool at_item_start = true;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();
            
            if (!child_tag) continue;
            
            if (tag_eq(child_tag, "item")) {
                // Finalize current paragraph if exists
                if (current_para && current_para->first_child && current_item) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                // Save previous item if exists
                if (current_item && current_item->first_child) {
                    doc_append_child(list, current_item);
                }
                // Start new item
                current_item = doc_alloc_element(arena, DocElemType::LIST_ITEM);
                if (list_centered) {
                    current_item->flags |= DocElement::FLAG_CENTERED;
                }
                // Start first paragraph in item
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                if (list_centered) {
                    current_para->flags |= DocElement::FLAG_CENTERED;
                }
                at_item_start = true;
                
                // First, check if item has custom label (brack_group)
                bool has_brack_group = false;
                auto peek_iter = child_elem.children();
                ItemReader peek_child;
                while (peek_iter.next(&peek_child)) {
                    if (peek_child.isElement()) {
                        ElementReader peek_elem = peek_child.asElement();
                        const char* peek_tag = peek_elem.tagName();
                        if (peek_tag && tag_eq(peek_tag, "brack_group")) {
                            has_brack_group = true;
                            break;
                        }
                    }
                }
                
                // Assign item number only if no custom label (enumerate only)
                if (list->list.list_type == ListType::ENUMERATE && !has_brack_group) {
                    current_item->list_item.item_number = item_number++;
                }
                
                // Process item children (e.g., brack_group for labels)
                auto item_iter = child_elem.children();
                ItemReader item_child;
                while (item_iter.next(&item_child)) {
                    if (item_child.isElement()) {
                        ElementReader item_child_elem = item_child.asElement();
                        const char* item_child_tag = item_child_elem.tagName();
                        if (item_child_tag && tag_eq(item_child_tag, "brack_group")) {
                            // Custom label via \item[label]
                            current_item->list_item.has_custom_label = true;
                            current_item->list_item.label = extract_text_content(item_child, arena);
                            current_item->list_item.html_label = render_brack_group_to_html(item_child, arena, doc);
                        } else {
                            DocElement* content = build_doc_element(item_child, arena, doc);
                            if (content) {
                                doc_append_child(current_para, content);
                                at_item_start = false;
                            }
                        }
                    }
                }
            } else if (tag_eq(child_tag, "paragraph") || tag_eq(child_tag, "text_mode") ||
                       tag_eq(child_tag, "content")) {
                // Items may be inside paragraph - process recursively
                process_list_content(list, child, arena, doc, item_number);
            } else if (tag_eq(child_tag, "centering")) {
                // \centering command inside list - set flag to center all subsequent items
                list_centered = true;
                list->flags |= DocElement::FLAG_CENTERED;
            } else if (is_block_element_tag(child_tag) && current_item) {
                // Block element (nested list, etc.) - close current paragraph first
                if (current_para && current_para->first_child) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                // Add block element directly to item
                DocElement* content = build_doc_element(child, arena, doc);
                if (content) {
                    doc_append_child(current_item, content);
                }
                // Start new paragraph for any following content
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                at_item_start = true;
            } else if (current_item && current_para) {
                // Add element content to current paragraph
                DocElement* content = build_doc_element(child, arena, doc);
                if (content) {
                    doc_append_child(current_para, content);
                    at_item_start = false;
                }
            }
        } else if (child.isSymbol()) {
            // Check for parbreak
            String* sym = child.asSymbol();
            if (sym && strcmp(sym->chars, "parbreak") == 0 && current_item && current_para) {
                if (current_para->first_child) {
                    trim_paragraph_whitespace(current_para, arena);
                    doc_append_child(current_item, current_para);
                }
                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                at_item_start = true;
            }
        } else if (child.isString() && current_item && current_para) {
            // String content for current paragraph
            const char* text = child.cstring();
            if (text && strlen(text) > 0) {
                const char* p = text;
                if (at_item_start) {
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                }
                if (*p) {
                    DocElement* text_elem = doc_create_text_cstr(arena, at_item_start ? p : text, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_para, text_elem);
                        at_item_start = false;
                    }
                }
            }
        }
    }
    
    // Finalize last paragraph
    if (current_para && current_para->first_child && current_item) {
        trim_paragraph_whitespace(current_para, arena);
        doc_append_child(current_item, current_para);
    }
    // Add last item
    if (current_item && current_item->first_child) {
        doc_append_child(list, current_item);
    }
    
    // Always return the list, even if empty (supports empty itemize/enumerate)
    return list;
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

// Check if a paragraph has any visible content after trimming
// Returns true if any child is non-empty (TEXT_RUN with content, or non-TEXT_RUN element)
static bool paragraph_has_visible_content(DocElement* para) {
    if (!para || !para->first_child) return false;
    
    for (DocElement* child = para->first_child; child; child = child->next_sibling) {
        if (child->type == DocElemType::TEXT_RUN) {
            if (child->text.text && child->text.text_len > 0) {
                return true;
            }
        } else if (child->type == DocElemType::TEXT_SPAN) {
            // Check if span has content
            if (child->text.text && child->text.text_len > 0) {
                return true;
            }
            // Check if span has children with content
            if (child->first_child) {
                return true;  // Assume spans with children have content
            }
        } else if (child->type != DocElemType::SPACE || child->space.is_linebreak) {
            // Non-space element (or linebreak) counts as content
            return true;
        }
    }
    return false;
}

// Trim whitespace at paragraph boundaries:
// - Leading whitespace from first text element
// - Trailing whitespace from last text element  
// - Optionally: trim/collapse leading whitespace from text elements that follow SPACE (linebreak)
// If preserve_linebreak_space is true, multiple whitespace after linebreak is collapsed to one space
// If false, all leading whitespace after linebreak is trimmed (like quote/quotation)
static void trim_paragraph_whitespace_ex(DocElement* para, Arena* arena, bool preserve_linebreak_space) {
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
    
    // Handle whitespace around linebreaks
    // - Trim trailing whitespace from elements that precede a linebreak (\unskip behavior)
    // - Handle leading whitespace after linebreaks based on preserve_linebreak_space flag
    DocElement* prev = nullptr;
    for (DocElement* child = para->first_child; child; child = child->next_sibling) {
        // Handle leading whitespace after linebreak
        if (prev && prev->type == DocElemType::SPACE && prev->space.is_linebreak) {
            // Process leading whitespace from elements after linebreak
            DocElement* curr = child;
            while (curr && curr->type == DocElemType::TEXT_RUN && curr->text.text && curr->text.text_len > 0) {
                if (preserve_linebreak_space) {
                    // Collapse multiple whitespace to a single space (verse behavior)
                    const char* text = curr->text.text;
                    size_t len = curr->text.text_len;
                    
                    // Count leading whitespace
                    size_t ws_count = 0;
                    while (ws_count < len && (text[ws_count] == ' ' || text[ws_count] == '\t' || text[ws_count] == '\n' || text[ws_count] == '\r')) {
                        ws_count++;
                    }
                    
                    if (ws_count > 1) {
                        // Replace multiple whitespace with single space
                        char* new_text = (char*)arena_alloc(arena, len - ws_count + 2);
                        new_text[0] = ' ';  // Single space
                        memcpy(new_text + 1, text + ws_count, len - ws_count + 1);
                        curr->text.text = new_text;
                        curr->text.text_len = len - ws_count + 1;
                    }
                    break;
                } else {
                    // Trim all leading whitespace (quote/quotation behavior)
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
        }
        // Trim trailing whitespace from elements that precede a linebreak (\unskip)
        if (child->type == DocElemType::SPACE && child->space.is_linebreak && prev) {
            // Walk backwards from prev to trim trailing whitespace
            DocElement* curr = prev;
            while (curr && curr->type == DocElemType::TEXT_RUN && curr->text.text) {
                const char* trimmed = trim_trailing_whitespace(curr->text.text, arena);
                if (trimmed) {
                    curr->text.text = trimmed;
                    curr->text.text_len = strlen(trimmed);
                    break;  // Successfully trimmed trailing whitespace
                } else {
                    // Text was all whitespace - mark as empty, but can't walk backwards easily
                    curr->text.text = "";
                    curr->text.text_len = 0;
                    break;  // Can't easily walk backwards, so stop here
                }
            }
        }
        prev = child;
    }
}

// Wrapper for the common case (default: trim all leading whitespace after linebreak)
static void trim_paragraph_whitespace(DocElement* para, Arena* arena) {
    trim_paragraph_whitespace_ex(para, arena, false);
}

// Build alignment environment content with proper paragraph splitting
// Environment structure is: center -> paragraph -> [content with parbreaks]
// env_name is used to determine whitespace behavior (verse preserves leading space after \\)
static void build_alignment_content(DocElement* container, const ElementReader& elem,
                                     Arena* arena, TexDocumentModel* doc, const char* env_name) {
    // Verse environment preserves leading space after linebreak, others don't
    bool preserve_linebreak_space = env_name && tag_eq(env_name, "verse");
    
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
                        trim_paragraph_whitespace_ex(current_para, arena, preserve_linebreak_space);
                        doc_append_child(container, current_para);
                    }
                    current_para = nullptr;
                    continue;
                }
                
                // Check if this is a block element (e.g., itemize, enumerate, etc.)
                if (para_child.isElement()) {
                    ElementReader para_child_elem = para_child.asElement();
                    const char* para_child_tag = para_child_elem.tagName();
                    if (para_child_tag && is_block_element_tag(para_child_tag)) {
                        // Finalize current paragraph before block element
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace_ex(current_para, arena, preserve_linebreak_space);
                            doc_append_child(container, current_para);
                            current_para = nullptr;
                        }
                        // Build block element directly
                        DocElement* block_elem = build_doc_element(para_child, arena, doc);
                        if (block_elem && !is_special_marker(block_elem)) {
                            doc_append_child(container, block_elem);
                        }
                        continue;
                    }
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
                trim_paragraph_whitespace_ex(current_para, arena, preserve_linebreak_space);
                doc_append_child(container, current_para);
            }
        } else {
            // Non-paragraph children - process directly
            DocElement* child_doc = build_doc_element(child, arena, doc);
            if (child_doc && !is_special_marker(child_doc)) {
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
    
    // Store the environment name for rendering
    size_t len = strlen(env_name);
    char* name_copy = (char*)arena_alloc(arena, len + 1);
    memcpy(name_copy, env_name, len + 1);
    container->alignment.env_name = name_copy;
    
    // Set alignment flag
    if (tag_eq(env_name, "center")) {
        container->flags |= DocElement::FLAG_CENTERED;
    } else if (tag_eq(env_name, "flushleft")) {
        container->flags |= DocElement::FLAG_FLUSH_LEFT;
    } else if (tag_eq(env_name, "flushright")) {
        container->flags |= DocElement::FLAG_FLUSH_RIGHT;
    }
    
    // Process children with paragraph splitting on parbreaks
    build_alignment_content(container, elem, arena, doc, env_name);
    
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
        // Use current referable context if available
        const char* ref_id = doc->current_ref_id;
        const char* ref_text = doc->current_ref_text;
        
        // If parent is a heading, use its label (sec-N) and number
        if (parent && parent->type == DocElemType::HEADING) {
            ref_id = parent->heading.label;  // sec-N
            ref_text = parent->heading.number;  // e.g., "1" or "2.3"
        }
        
        log_debug("process_label_command: label='%s', ref_id='%s', ref_text='%s', parent=%s",
                  label, ref_id ? ref_id : "(null)", ref_text ? ref_text : "(null)",
                  parent ? doc_elem_type_name(parent->type) : "(null)");
        
        doc->add_label_with_id(label, ref_id, ref_text);
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
    
    // Add to pending refs for two-pass resolution
    // The reference will be resolved after the entire document is built
    if (ref->ref.ref_label) {
        doc->add_pending_ref(ref);
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
// This version adds support for PARBREAK_MARKER, NOINDENT_MARKER, and alignment markers.
static bool is_inline_or_break(DocElement* elem) {
    if (!elem) return false;
    if (elem == PARBREAK_MARKER || elem == LINEBREAK_MARKER || elem == NOINDENT_MARKER) return false;
    if (is_alignment_marker(elem)) return false;  // alignment markers are not inline content
    if (is_font_marker(elem)) return false;  // font markers are not inline content
    return is_inline_element(elem);
}

// Helper function to process body content with paragraph grouping
// Collects inline content into paragraphs, respecting parbreak markers
// Also tracks paragraph alignment (\centering, \raggedright, \raggedleft)
// and font declarations (\bfseries, \itshape, etc.)
static void build_body_content_with_paragraphs(DocElement* container, const ElementReader& elem,
                                                Arena* arena, TexDocumentModel* doc) {
    DocElement* current_para = nullptr;
    bool after_block_element = false;  // Track if the previous element was a block
    bool next_para_noindent = false;   // Track if next paragraph should have noindent class
    bool strip_next_leading_space = false;  // Track if next text should have leading space stripped
    ParagraphAlignment current_alignment = ParagraphAlignment::NONE;  // Track current alignment
    uint32_t active_font_flags = 0;  // Track active font style flags from declarations
    
    int64_t child_count = elem.childCount();
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child_item = elem.childAt(i);
        
        // Handle string children - may need to strip leading space
        if (child_item.isString()) {
            const char* text = child_item.cstring();
            if (text && text[0] != '\0') {
                // Strip leading space if flag is set
                if (strip_next_leading_space) {
                    strip_next_leading_space = false;
                    while (*text == ' ' || *text == '\t') text++;
                    if (text[0] == '\0') continue;  // String was only whitespace
                }
                
                // Apply typographic ligature transformations (!´ → ¡, ?´ → ¿)
                text = transform_text_ligatures(text, arena);
                
                // Ensure we have a paragraph
                if (!current_para) {
                    current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                    if (after_block_element) {
                        current_para->flags |= DocElement::FLAG_CONTINUE;
                        after_block_element = false;
                    }
                    if (next_para_noindent) {
                        current_para->flags |= DocElement::FLAG_NOINDENT;
                        next_para_noindent = false;
                    }
                }
                // Create text element with active font style if any
                DocTextStyle style = DocTextStyle::plain();
                style.flags = active_font_flags;
                DocElement* text_elem = doc_create_text_normalized(arena, text, style);
                if (text_elem) {
                    // If there's an active font style, wrap in a styled span
                    if (active_font_flags != 0) {
                        DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                        styled_span->text.style = style;
                        doc_append_child(styled_span, text_elem);
                        doc_append_child(current_para, styled_span);
                    } else {
                        doc_append_child(current_para, text_elem);
                    }
                }
            }
            continue;
        }
        
        // Special case: nested "document" elements - process their children inline instead of creating a DOCUMENT
        // This preserves the after_block_element context across document boundaries
        if (child_item.isElement()) {
            ElementReader child_elem = child_item.asElement();
            const char* tag = child_elem.tagName();
            if (tag && tag_eq(tag, "document")) {
                // Process the document's children inline, carrying forward the current paragraph
                // Don't finalize paragraph here - let content processing handle breaks naturally
                int64_t doc_child_count = child_elem.childCount();
                for (int64_t j = 0; j < doc_child_count; j++) {
                    // Create a temporary sub-call context by processing each child inline
                    ItemReader doc_child_item = child_elem.childAt(j);
                    
                    // Handle string children
                    if (doc_child_item.isString()) {
                        const char* text = doc_child_item.cstring();
                        if (text && text[0] != '\0') {
                            // Ensure we have a paragraph
                            if (!current_para) {
                                current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                                if (after_block_element) {
                                    current_para->flags |= DocElement::FLAG_CONTINUE;
                                    after_block_element = false;
                                }
                                if (next_para_noindent) {
                                    current_para->flags |= DocElement::FLAG_NOINDENT;
                                    next_para_noindent = false;
                                }
                            }
                            DocElement* text_elem = doc_create_text_normalized(arena, text, DocTextStyle::plain());
                            if (text_elem) {
                                doc_append_child(current_para, text_elem);
                            }
                        }
                        continue;
                    }
                    
                    // Handle element children
                    // Special case: paragraph with parbreaks returns nullptr - process its children directly
                    if (doc_child_item.isElement()) {
                        ElementReader child_elem = doc_child_item.asElement();
                        const char* child_tag = child_elem.tagName();
                        if (child_tag && tag_eq(child_tag, "paragraph")) {
                            // Check if this paragraph has parbreaks
                            if (contains_parbreak_markers(child_elem)) {
                                // Process the paragraph's children directly (same as build_body_content_with_paragraphs does)
                                int64_t para_child_count = child_elem.childCount();
                                for (int64_t k = 0; k < para_child_count; k++) {
                                    ItemReader para_child_item = child_elem.childAt(k);
                                    DocElement* para_child_elem = build_doc_element(para_child_item, arena, doc);
                                    
                                    if (!para_child_elem) continue;
                                    
                                    if (para_child_elem == PARBREAK_MARKER) {
                                        if (current_para && current_para->first_child) {
                                            trim_paragraph_whitespace(current_para, arena);
                                            if (paragraph_has_visible_content(current_para)) {
                                                doc_append_child(container, current_para);
                                            }
                                        }
                                        current_para = nullptr;
                                        after_block_element = false;
                                        continue;
                                    }
                                    
                                    if (para_child_elem == NOINDENT_MARKER) {
                                        next_para_noindent = true;
                                        continue;
                                    }
                                    
                                    if (is_inline_or_break(para_child_elem)) {
                                        if (!current_para) {
                                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                                            if (after_block_element) {
                                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                                after_block_element = false;
                                            }
                                            if (next_para_noindent) {
                                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                                next_para_noindent = false;
                                            }
                                        }
                                        doc_append_child(current_para, para_child_elem);
                                    } else {
                                        if (current_para && current_para->first_child) {
                                            trim_paragraph_whitespace(current_para, arena);
                                            if (paragraph_has_visible_content(current_para)) {
                                                doc_append_child(container, current_para);
                                            } else if (current_para->flags & DocElement::FLAG_CONTINUE) {
                                                after_block_element = true;
                                            }
                                            current_para = nullptr;
                                        }
                                        doc_append_child(container, para_child_elem);
                                        if (para_child_elem->type != DocElemType::HEADING) {
                                            after_block_element = true;
                                        }
                                    }
                                }
                                continue;  // Done processing paragraph with parbreaks
                            }
                        }
                    }
                    
                    DocElement* doc_child_elem = build_doc_element(doc_child_item, arena, doc);
                    if (!doc_child_elem) continue;
                    
                    if (doc_child_elem == PARBREAK_MARKER) {
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            }
                        }
                        current_para = nullptr;
                        after_block_element = false;
                        continue;
                    }
                    
                    if (doc_child_elem == NOINDENT_MARKER) {
                        next_para_noindent = true;
                        continue;
                    }
                    
                    if (is_inline_or_break(doc_child_elem)) {
                        if (!current_para) {
                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                            if (after_block_element) {
                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                after_block_element = false;
                            }
                            if (next_para_noindent) {
                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                next_para_noindent = false;
                            }
                        }
                        doc_append_child(current_para, doc_child_elem);
                    } else if (doc_child_elem->type == DocElemType::PARAGRAPH) {
                        // Special case: unwrap PARAGRAPH from the nested document
                        // Move its inline children to our current paragraph
                        if (!current_para) {
                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                            if (after_block_element) {
                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                after_block_element = false;
                            }
                            if (next_para_noindent) {
                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                next_para_noindent = false;
                            }
                        }
                        // Move children from the nested paragraph to current
                        for (DocElement* para_child = doc_child_elem->first_child; para_child; ) {
                            DocElement* next = para_child->next_sibling;
                            para_child->parent = nullptr;
                            para_child->next_sibling = nullptr;
                            doc_append_child(current_para, para_child);
                            para_child = next;
                        }
                    } else {
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            } else if (current_para->flags & DocElement::FLAG_CONTINUE) {
                                after_block_element = true;
                            }
                            current_para = nullptr;
                        }
                        doc_append_child(container, doc_child_elem);
                        if (doc_child_elem->type != DocElemType::HEADING) {
                            after_block_element = true;
                        }
                    }
                }
                continue;  // Done processing document element inline
            }
        }
        
        // Check for diacritic command elements (single-char tags like ^, ", ', `, ~, etc.)
        if (child_item.isElement()) {
            ElementReader child_elem = child_item.asElement();
            const char* tag = child_elem.tagName();
            if (tag && is_diacritic_tag(tag)) {
                char diacritic_cmd = tag[0];
                
                // Check if diacritic has its own child (braced form like \^{o})
                ItemReader diacritic_child;
                auto diacritic_iter = child_elem.children();
                bool has_child = diacritic_iter.next(&diacritic_child);
                
                const char* result = nullptr;
                bool consumed_next = false;
                bool is_empty_curly_group = false;
                
                if (has_child) {
                    // Braced form: apply to the contained text
                    const char* base_text = nullptr;
                    if (diacritic_child.isString()) {
                        base_text = diacritic_child.cstring();
                    } else {
                        // Check if this is an empty curly_group element
                        if (diacritic_child.isElement()) {
                            ElementReader dc_elem = diacritic_child.asElement();
                            const char* dc_tag = dc_elem.tagName();
                            if (dc_tag && (tag_eq(dc_tag, "curly_group") || tag_eq(dc_tag, "group"))) {
                                base_text = extract_text_content(diacritic_child, arena);
                                if (!base_text || base_text[0] == '\0') {
                                    is_empty_curly_group = true;
                                }
                            } else {
                                base_text = extract_text_content(diacritic_child, arena);
                            }
                        } else {
                            base_text = extract_text_content(diacritic_child, arena);
                        }
                    }
                    if (base_text && base_text[0] != '\0') {
                        result = apply_diacritic(diacritic_cmd, base_text, arena);
                    }
                } else {
                    // Unbraced form (e.g., \^o) - look at next sibling for base character
                    if (i + 1 < child_count) {
                        ItemReader next_item = elem.childAt(i + 1);
                        if (next_item.isString()) {
                            const char* next_text = next_item.cstring();
                            if (next_text && next_text[0] != '\0') {
                                // Apply diacritic to first character
                                result = apply_diacritic(diacritic_cmd, next_text, arena);
                                if (result) {
                                    consumed_next = true;
                                    
                                    // Ensure we have a paragraph
                                    if (!current_para) {
                                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                                        if (after_block_element) {
                                            current_para->flags |= DocElement::FLAG_CONTINUE;
                                            after_block_element = false;
                                        }
                                        if (next_para_noindent) {
                                            current_para->flags |= DocElement::FLAG_NOINDENT;
                                            next_para_noindent = false;
                                        }
                                    }
                                    
                                    // Add the accented character
                                    DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                                    if (text_elem) {
                                        doc_append_child(current_para, text_elem);
                                    }
                                    
                                    // Add remaining characters from next_text (if any)
                                    int char_len = utf8_char_len((unsigned char)next_text[0]);
                                    if (next_text[char_len] != '\0') {
                                        DocElement* rest = doc_create_text_cstr(arena, next_text + char_len, DocTextStyle::plain());
                                        if (rest) {
                                            doc_append_child(current_para, rest);
                                        }
                                    }
                                    
                                    i++;  // Skip the consumed next item
                                    continue;
                                }
                            }
                        }
                        // Check if next item is an element
                        if (next_item.isElement()) {
                            ElementReader next_elem = next_item.asElement();
                            const char* next_tag = next_elem.tagName();
                            
                            // Check for empty curly_group sibling (e.g., \^{} → ^ + empty curly_group)
                            // In this case, output diacritic char + ZWS
                            if (next_tag && (tag_eq(next_tag, "curly_group") || tag_eq(next_tag, "group"))) {
                                const char* group_text = extract_text_content(next_item, arena);
                                if (!group_text || group_text[0] == '\0') {
                                    // Empty curly_group sibling - output diacritic char + ZWS
                                    if (!current_para) {
                                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                                        if (after_block_element) {
                                            current_para->flags |= DocElement::FLAG_CONTINUE;
                                            after_block_element = false;
                                        }
                                        if (next_para_noindent) {
                                            current_para->flags |= DocElement::FLAG_NOINDENT;
                                            next_para_noindent = false;
                                        }
                                    }
                                    char buf[8];
                                    buf[0] = diacritic_cmd;
                                    buf[1] = '\xE2';  // U+200B ZWS
                                    buf[2] = '\x80';
                                    buf[3] = '\x8B';
                                    buf[4] = '\0';
                                    DocElement* text_elem = doc_create_text_cstr(arena, buf, DocTextStyle::plain());
                                    if (text_elem) {
                                        doc_append_child(current_para, text_elem);
                                    }
                                    i++;  // Skip the consumed curly_group
                                    continue;
                                }
                            }
                        }
                        if (next_item.isElement()) {
                            // Next item is an element - could be \i (dotless i), \j (dotless j), etc.
                            ElementReader next_elem = next_item.asElement();
                            const char* next_tag = next_elem.tagName();
                            
                            // Handle dotless i and j for diacritics
                            const char* base_text = nullptr;
                            if (next_tag && tag_eq(next_tag, "i")) {
                                base_text = "\xC4\xB1";  // ı (U+0131 LATIN SMALL LETTER DOTLESS I)
                            } else if (next_tag && tag_eq(next_tag, "j")) {
                                base_text = "\xC8\xB7";  // ȷ (U+0237 LATIN SMALL LETTER DOTLESS J)
                            }
                            
                            if (base_text) {
                                result = apply_diacritic(diacritic_cmd, base_text, arena);
                                if (result) {
                                    consumed_next = true;
                                    
                                    // Ensure we have a paragraph
                                    if (!current_para) {
                                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                                        if (after_block_element) {
                                            current_para->flags |= DocElement::FLAG_CONTINUE;
                                            after_block_element = false;
                                        }
                                        if (next_para_noindent) {
                                            current_para->flags |= DocElement::FLAG_NOINDENT;
                                            next_para_noindent = false;
                                        }
                                    }
                                    
                                    // Add the accented character
                                    DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                                    if (text_elem) {
                                        doc_append_child(current_para, text_elem);
                                    }
                                    
                                    i++;  // Skip the consumed next item
                                    
                                    // \i and \j are word-forming - strip next leading space
                                    strip_next_leading_space = true;
                                    continue;
                                }
                            }
                        }
                    }
                }
                
                if (result && !consumed_next) {
                    // Ensure we have a paragraph
                    if (!current_para) {
                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                        if (after_block_element) {
                            current_para->flags |= DocElement::FLAG_CONTINUE;
                            after_block_element = false;
                        }
                        if (next_para_noindent) {
                            current_para->flags |= DocElement::FLAG_NOINDENT;
                            next_para_noindent = false;
                        }
                    }
                    DocElement* text_elem = doc_create_text_cstr(arena, result, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_para, text_elem);
                    }
                    continue;
                }
                
                // Empty curly group: output diacritic char + ZWS (e.g., \^{} → ^​)
                if (is_empty_curly_group) {
                    if (!current_para) {
                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                        if (after_block_element) {
                            current_para->flags |= DocElement::FLAG_CONTINUE;
                            after_block_element = false;
                        }
                        if (next_para_noindent) {
                            current_para->flags |= DocElement::FLAG_NOINDENT;
                            next_para_noindent = false;
                        }
                    }
                    char buf[8];
                    buf[0] = diacritic_cmd;
                    buf[1] = '\xE2';  // U+200B ZWS
                    buf[2] = '\x80';
                    buf[3] = '\x8B';
                    buf[4] = '\0';
                    DocElement* text_elem = doc_create_text_cstr(arena, buf, DocTextStyle::plain());
                    if (text_elem) {
                        doc_append_child(current_para, text_elem);
                    }
                    continue;
                }
                
                // If we get here, diacritic processing failed - fall through to normal processing
            }
            
            // Check for word-forming commands (\i, \o, \ss, etc.)
            if (tag && is_word_forming_command(tag)) {
                // Build the character output
                DocElement* char_elem = build_doc_element(child_item, arena, doc);
                if (char_elem) {
                    // Ensure we have a paragraph
                    if (!current_para) {
                        current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                        if (after_block_element) {
                            current_para->flags |= DocElement::FLAG_CONTINUE;
                            after_block_element = false;
                        }
                        if (next_para_noindent) {
                            current_para->flags |= DocElement::FLAG_NOINDENT;
                            next_para_noindent = false;
                        }
                    }
                    doc_append_child(current_para, char_elem);
                    
                    // Check for empty curly_group terminator (like \ss{})
                    if (has_empty_curly_terminator(child_elem, arena)) {
                        // Output ZWS after the character
                        DocElement* zws = doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain());
                        if (zws) {
                            doc_append_child(current_para, zws);
                        }
                    } else {
                        // Set flag to strip next leading space
                        strip_next_leading_space = true;
                    }
                }
                continue;
            }
        }
        
        // Check if this is a curly_group that contains alignment commands and parbreaks
        // These need special processing to preserve alignment scoping:
        // The alignment inside the group should only affect paragraphs until the group ends
        if (child_item.isElement()) {
            ElementReader group_elem = child_item.asElement();
            const char* group_tag = group_elem.tagName();
            if (group_tag && (tag_eq(group_tag, "curly_group") || tag_eq(group_tag, "group")) &&
                contains_alignment_commands(group_elem) && contains_parbreak_markers(group_elem)) {
                // Save current alignment to restore after the group
                ParagraphAlignment saved_alignment = current_alignment;
                
                // Process the group's children inline, tracking alignment within the group scope
                int64_t group_child_count = group_elem.childCount();
                for (int64_t j = 0; j < group_child_count; j++) {
                    ItemReader group_child = group_elem.childAt(j);
                    DocElement* group_child_elem = build_doc_element(group_child, arena, doc);
                    
                    if (!group_child_elem) continue;
                    
                    // Handle paragraph breaks within the group
                    if (group_child_elem == PARBREAK_MARKER) {
                        if (current_para && current_para->first_child) {
                            apply_alignment_to_paragraph(current_para, current_alignment);
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            }
                        }
                        current_para = nullptr;
                        after_block_element = false;
                        next_para_noindent = false;
                        continue;
                    }
                    
                    // Handle alignment markers within the group
                    if (is_alignment_marker(group_child_elem)) {
                        current_alignment = marker_to_alignment(group_child_elem);
                        continue;
                    }
                    
                    // Handle noindent markers
                    if (group_child_elem == NOINDENT_MARKER) {
                        next_para_noindent = true;
                        continue;
                    }
                    
                    // Add inline content to current paragraph
                    if (is_inline_or_break(group_child_elem)) {
                        if (!current_para) {
                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                            if (after_block_element) {
                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                after_block_element = false;
                            }
                            if (next_para_noindent) {
                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                next_para_noindent = false;
                            }
                        }
                        doc_append_child(current_para, group_child_elem);
                    } else {
                        // Block element within group - finalize paragraph first
                        if (current_para && current_para->first_child) {
                            apply_alignment_to_paragraph(current_para, current_alignment);
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            }
                            current_para = nullptr;
                        }
                        doc_append_child(container, group_child_elem);
                        after_block_element = true;
                    }
                }
                
                // Add ZWSP at end of group to preserve word boundary
                if (current_para) {
                    DocElement* zwsp = doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain());
                    if (zwsp) {
                        doc_append_child(current_para, zwsp);
                    }
                }
                
                // Restore alignment after group ends
                current_alignment = saved_alignment;
                continue;
            }
        }
        
        // Check if this is a "paragraph" element that needs special handling:
        // - Contains parbreak markers (\par or blank lines)
        // - Contains block elements (center, lists, etc.)
        // In these cases, we need to process its children directly instead of treating it as a unit
        if (child_item.isElement()) {
            ElementReader para_elem = child_item.asElement();
            const char* para_tag = para_elem.tagName();
            if (para_tag && tag_eq(para_tag, "paragraph") && 
                (contains_parbreak_markers(para_elem) || contains_block_elements(para_elem))) {
                // Process the paragraph element's children as if they were direct children here
                // This handles \par, parbreak markers, and block elements properly
                int64_t para_child_count = para_elem.childCount();
                for (int64_t j = 0; j < para_child_count; j++) {
                    ItemReader para_child = para_elem.childAt(j);
                    DocElement* para_child_elem = build_doc_element(para_child, arena, doc);
                    
                    if (!para_child_elem) continue;
                    
                    if (para_child_elem == PARBREAK_MARKER) {
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            }
                        }
                        current_para = nullptr;
                        after_block_element = false;
                        next_para_noindent = false;
                        continue;
                    }
                    
                    if (para_child_elem == NOINDENT_MARKER) {
                        next_para_noindent = true;
                        continue;
                    }
                    
                    if (is_inline_or_break(para_child_elem)) {
                        if (!current_para) {
                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                            if (after_block_element) {
                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                after_block_element = false;
                            }
                            if (next_para_noindent) {
                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                next_para_noindent = false;
                            }
                        }
                        doc_append_child(current_para, para_child_elem);
                    } else {
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            } else {
                                // Paragraph was not added - restore after_block_element if this para consumed it
                                if (current_para->flags & DocElement::FLAG_CONTINUE) {
                                    after_block_element = true;
                                }
                            }
                            current_para = nullptr;
                        }
                        doc_append_child(container, para_child_elem);
                        after_block_element = true;
                    }
                }
                continue;
            }
        }
        
        DocElement* child_elem = build_doc_element(child_item, arena, doc);
        
        if (!child_elem) continue;
        
        // Paragraph break marker - finalize current paragraph and start new one
        if (child_elem == PARBREAK_MARKER) {
            if (current_para && current_para->first_child) {
                // Apply current alignment to paragraph before finalizing
                apply_alignment_to_paragraph(current_para, current_alignment);
                trim_paragraph_whitespace(current_para, arena);
                // Only append if paragraph has visible content after trimming
                if (paragraph_has_visible_content(current_para)) {
                    doc_append_child(container, current_para);
                }
            }
            current_para = nullptr;
            // Note: parbreak resets after_block_element since it's a new paragraph context
            after_block_element = false;
            // Reset noindent flag - \noindent followed by blank line does NOT affect next paragraph
            next_para_noindent = false;
            continue;
        }
        
        // Noindent marker - set flag for next paragraph
        if (child_elem == NOINDENT_MARKER) {
            next_para_noindent = true;
            continue;
        }
        
        // Alignment markers - update current alignment state
        if (is_alignment_marker(child_elem)) {
            current_alignment = marker_to_alignment(child_elem);
            continue;
        }
        
        // Font style markers - update active font style flags
        // Font declarations like \bfseries affect all following content until scope ends
        if (is_font_marker(child_elem)) {
            uint32_t new_flags = font_marker_to_style_flags(child_elem);
            if (new_flags == DocTextStyle::EMPHASIS) {
                // \em toggles between italic and upright
                // Check current state of italic/upright in active_font_flags
                bool currently_italic = (active_font_flags & DocTextStyle::ITALIC) != 0;
                bool currently_upright = (active_font_flags & DocTextStyle::UPRIGHT) != 0;
                // Clear both italic and upright
                active_font_flags &= ~(DocTextStyle::ITALIC | DocTextStyle::UPRIGHT);
                if (currently_italic) {
                    // Was italic, toggle to upright
                    active_font_flags |= DocTextStyle::UPRIGHT;
                } else if (currently_upright) {
                    // Was upright, toggle to italic
                    active_font_flags |= DocTextStyle::ITALIC;
                } else {
                    // Neither set - start with italic
                    active_font_flags |= DocTextStyle::ITALIC;
                }
            } else {
                active_font_flags |= new_flags;
            }
            strip_next_leading_space = true;  // strip space after font declaration
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
                // Apply noindent flag if set
                if (next_para_noindent) {
                    current_para->flags |= DocElement::FLAG_NOINDENT;
                    next_para_noindent = false;
                }
            }
            // If there's an active font style from declarations like \em, wrap inline content
            if (active_font_flags != 0) {
                DocElement* styled_span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
                styled_span->text.style = DocTextStyle::plain();
                styled_span->text.style.flags = active_font_flags;
                doc_append_child(styled_span, child_elem);
                doc_append_child(current_para, styled_span);
            } else {
                doc_append_child(current_para, child_elem);
            }
        } else {
            // Block element - finalize current paragraph first
            if (current_para && current_para->first_child) {
                apply_alignment_to_paragraph(current_para, current_alignment);
                trim_paragraph_whitespace(current_para, arena);
                if (paragraph_has_visible_content(current_para)) {
                    doc_append_child(container, current_para);
                } else {
                    // Paragraph was not added - restore after_block_element if this para consumed it
                    if (current_para->flags & DocElement::FLAG_CONTINUE) {
                        
                        after_block_element = true;
                    }
                }
                current_para = nullptr;
            }
            
            // Special case: if child_elem is a DOCUMENT element, unwrap its children
            // This handles nested document elements that occur in certain parse tree structures
            if (child_elem->type == DocElemType::DOCUMENT && child_elem->first_child) {
                
                // Move children from nested DOCUMENT to current container
                for (DocElement* doc_child = child_elem->first_child; doc_child; ) {
                    DocElement* next = doc_child->next_sibling;
                    doc_child->parent = nullptr;
                    doc_child->next_sibling = nullptr;
                    
                    // Process each child with current after_block_element state
                    if (is_inline_or_break(doc_child)) {
                        if (!current_para) {
                            current_para = doc_alloc_element(arena, DocElemType::PARAGRAPH);
                            if (after_block_element) {
                                current_para->flags |= DocElement::FLAG_CONTINUE;
                                after_block_element = false;
                            }
                            if (next_para_noindent) {
                                current_para->flags |= DocElement::FLAG_NOINDENT;
                                next_para_noindent = false;
                            }
                        }
                        doc_append_child(current_para, doc_child);
                    } else {
                        if (current_para && current_para->first_child) {
                            trim_paragraph_whitespace(current_para, arena);
                            if (paragraph_has_visible_content(current_para)) {
                                doc_append_child(container, current_para);
                            } else {
                                // Paragraph was not added - restore after_block_element if this para consumed it
                                if (current_para->flags & DocElement::FLAG_CONTINUE) {
                                    after_block_element = true;
                                }
                            }
                            current_para = nullptr;
                        }
                        doc_append_child(container, doc_child);
                        if (doc_child->type != DocElemType::HEADING) {
                            after_block_element = true;
                        }
                    }
                    
                    doc_child = next;
                }
                continue;  // Don't add the empty DOCUMENT element
            }
            
            doc_append_child(container, child_elem);
            // Mark that we just added a block element (for continue flag)
            // But NOT for headings - paragraphs after headings are normal, not "continue"
            if (child_elem->type != DocElemType::HEADING) {
                after_block_element = true;
            }
        }
    }
    
    // Finalize any remaining paragraph
    if (current_para && current_para->first_child) {
        apply_alignment_to_paragraph(current_para, current_alignment);
        trim_paragraph_whitespace(current_para, arena);
        if (paragraph_has_visible_content(current_para)) {
            doc_append_child(container, current_para);
        }
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
        tag_eq(tag, "textup") || tag_eq(tag, "textsl") ||
        tag_eq(tag, "emph") || tag_eq(tag, "textsc") || tag_eq(tag, "underline") ||
        tag_eq(tag, "bf") || tag_eq(tag, "it") || tag_eq(tag, "tt") || tag_eq(tag, "em") ||
        tag_eq(tag, "sl") || tag_eq(tag, "up") ||
        tag_eq(tag, "bfseries") || tag_eq(tag, "itshape") || tag_eq(tag, "ttfamily") ||
        tag_eq(tag, "scshape") || tag_eq(tag, "slshape") || tag_eq(tag, "upshape") ||
        tag_eq(tag, "sout") || tag_eq(tag, "st") ||
        // Font size commands
        tag_eq(tag, "tiny") || tag_eq(tag, "scriptsize") || tag_eq(tag, "footnotesize") ||
        tag_eq(tag, "small") || tag_eq(tag, "normalsize") || tag_eq(tag, "large") ||
        tag_eq(tag, "Large") || tag_eq(tag, "LARGE") || tag_eq(tag, "huge") || tag_eq(tag, "Huge"));
    
    // Use text command handler only if NOT an environment (no paragraph children)
    if (is_font_tag && !has_paragraph_children()) {
        // Check if this is a font declaration (no children) that affects following content
        // Font declarations like \bfseries, \itshape have no arguments but affect subsequent text
        bool has_children = (elem.childCount() > 0);
        if (!has_children) {
            // No children - this is a font declaration, return marker for parent to handle
            if (tag_eq(tag, "bfseries") || tag_eq(tag, "bf")) return BOLD_MARKER;
            if (tag_eq(tag, "itshape") || tag_eq(tag, "it")) return ITALIC_MARKER;
            if (tag_eq(tag, "em")) return EMPHASIS_MARKER;
            if (tag_eq(tag, "ttfamily") || tag_eq(tag, "tt")) return MONOSPACE_MARKER;
            if (tag_eq(tag, "scshape")) return SMALLCAPS_MARKER;
            if (tag_eq(tag, "slshape")) return SLANTED_MARKER;
            if (tag_eq(tag, "upshape")) return UPRIGHT_MARKER;
            // Other font commands without children - return empty span (no visible effect)
        }
        return build_text_command(tag, elem, arena, doc);
    }
    
    // Paragraph alignment commands - return markers for alignment tracking
    // These affect the alignment of the current and subsequent paragraphs in the scope
    if (tag_eq(tag, "centering")) {
        return CENTERING_MARKER;
    }
    if (tag_eq(tag, "raggedright")) {
        return RAGGEDRIGHT_MARKER;
    }
    if (tag_eq(tag, "raggedleft")) {
        return RAGGEDLEFT_MARKER;
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
    // \/ ligature break - inserts ZWNJ (U+200C) to prevent ligature formation
    if (tag_eq(tag, "/")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x8C", DocTextStyle::plain());  // ZWNJ U+200C
    }
    // \mbox{} - horizontal box, prevents line breaks
    // Output as <span class="hbox"><span>content</span></span>
    if (tag_eq(tag, "mbox")) {
        // For now, just output the empty hbox structure
        // The content will be processed and rendered inline
        // Check if there are children with actual content
        bool has_content = false;
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isString()) {
                const char* text = child.cstring();
                if (text && strlen(text) > 0) {
                    // Has text content - trim whitespace-only strings
                    bool is_whitespace = true;
                    for (const char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                            is_whitespace = false;
                            break;
                        }
                    }
                    if (!is_whitespace) has_content = true;
                }
            } else if (child.isElement()) {
                has_content = true;
            }
        }
        
        // If empty, just output the empty hbox structure
        // This breaks ligatures and prevents line breaks at this point
        return doc_create_raw_html_cstr(arena, "<span class=\"hbox\"><span></span></span>");
    }
    // \verb|text| - inline verbatim with arbitrary delimiter
    // Format: \verb<delim>content<delim> or \verb*<delim>content<delim>
    // Expected output: <code class="tt">content</code>
    // For \verb*, spaces are shown as ␣ (U+2423)
    if (tag_eq(tag, "verb_command")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* verb_text = child.cstring();
            // verb_text is like "\\verb|text|" or "\\verb*|text|"
            if (verb_text && strncmp(verb_text, "\\verb", 5) == 0) {
                const char* content_start = verb_text + 5;  // skip "\\verb"
                bool is_starred = false;
                if (*content_start == '*') {
                    is_starred = true;
                    content_start++;
                }
                // The next character is the delimiter
                if (*content_start) {
                    char delim = *content_start;
                    content_start++;  // skip delimiter
                    // Find the closing delimiter
                    const char* content_end = strchr(content_start, delim);
                    if (content_end) {
                        size_t content_len = content_end - content_start;
                        // Build the output
                        StrBuf* out = strbuf_new_cap(content_len * 4 + 64);
                        strbuf_append_str(out, "<code class=\"tt\">");
                        // Process content - escape HTML entities, handle spaces for verb*
                        for (size_t i = 0; i < content_len; i++) {
                            char c = content_start[i];
                            if (c == ' ' && is_starred) {
                                // For \verb*, show visible space ␣ (U+2423)
                                strbuf_append_str(out, "\xE2\x90\xA3");
                            } else if (c == '<') {
                                strbuf_append_str(out, "&lt;");
                            } else if (c == '>') {
                                strbuf_append_str(out, "&gt;");
                            } else if (c == '&') {
                                strbuf_append_str(out, "&amp;");
                            } else {
                                strbuf_append_char(out, c);
                            }
                        }
                        strbuf_append_str(out, "</code>");
                        
                        char* html_copy = (char*)arena_alloc(arena, out->length + 1);
                        memcpy(html_copy, out->str, out->length + 1);
                        strbuf_free(out);
                        
                        return doc_create_raw_html_cstr(arena, html_copy);
                    }
                }
            }
        }
        return nullptr;  // Failed to parse
    }
    // LaTeX/TeX logos - styled HTML spans
    if (tag_eq(tag, "LaTeX")) {
        return doc_create_raw_html_cstr(arena,
            "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
    }
    if (tag_eq(tag, "TeX")) {
        return doc_create_raw_html_cstr(arena,
            "<span class=\"tex\">T<span class=\"e\">e</span>X</span>");
    }
    // \char command - character by code point
    // Formats: \char98 (decimal), \char"A0 (hex), \char'141 (octal)
    if (tag_eq(tag, "char_command")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* cmd_text = child.cstring();
            // cmd_text is like "\\char98" or "\\char\"A0" or "\\char'141"
            if (cmd_text && strncmp(cmd_text, "\\char", 5) == 0) {
                const char* num_part = cmd_text + 5;  // skip "\\char"
                long code_point = 0;
                if (*num_part == '"') {
                    // hex: \char"A0
                    code_point = strtol(num_part + 1, nullptr, 16);
                } else if (*num_part == '\'') {
                    // octal: \char'141
                    code_point = strtol(num_part + 1, nullptr, 8);
                } else {
                    // decimal: \char98
                    code_point = strtol(num_part, nullptr, 10);
                }
                // Convert code point to UTF-8
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;  // failed to parse
    }
    // TeX caret notation: ^^XX (2 hex digits) or ^^^^XXXX (4 hex digits) or ^^c (char XOR 64)
    // Examples: ^^A0 → U+00A0 (nbsp), ^^^^2103 → U+2103 (℃), ^^7 → char(55 XOR 64) = w
    if (tag_eq(tag, "caret_char")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* caret_text = child.cstring();
            // caret_text is like "^^A0" or "^^^^2103" or "^^7"
            if (caret_text && strncmp(caret_text, "^^", 2) == 0) {
                const char* after_caret = caret_text + 2;
                long code_point = 0;
                if (strncmp(after_caret, "^^", 2) == 0) {
                    // ^^^^XXXX - 4 hex digits
                    code_point = strtol(after_caret + 2, nullptr, 16);
                } else {
                    // ^^XX or ^^c
                    // Check if it's 2 hex digits
                    size_t len = strlen(after_caret);
                    if (len == 2 && isxdigit(after_caret[0]) && isxdigit(after_caret[1])) {
                        // 2 hex digits: ^^A0
                        code_point = strtol(after_caret, nullptr, 16);
                    } else if (len == 1) {
                        // Single char: ^^c means char XOR 64
                        // ^^7 = '7' (55) XOR 64 = 'w' (119) - wait, that's wrong
                        // Actually in TeX: ^^c means char(c) XOR 64 for printable, OR char+64 for control
                        // For ^^7: '7' is ASCII 55, 55 XOR 64 = 119 = 'w'
                        // For ^^+: '+' is ASCII 43, 43 XOR 64 = 107 = 'k'
                        code_point = (unsigned char)after_caret[0] ^ 64;
                    } else {
                        // Other formats - just try hex
                        code_point = strtol(after_caret, nullptr, 16);
                    }
                }
                // Convert code point to UTF-8
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;  // failed to parse
    }
    // \symbol{} command - character by code point (LaTeX fontenc)
    // Formats: \symbol{98} (decimal), \symbol{"00A9} (hex with "), \symbol{'141} (octal with ')
    if (tag_eq(tag, "symbol")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* arg = child.cstring();
            // arg is the content inside braces, may have leading space
            if (arg) {
                // Skip leading whitespace
                while (*arg && (*arg == ' ' || *arg == '\t')) arg++;
                long code_point = 0;
                if (*arg == '"') {
                    // hex: "00A9
                    code_point = strtol(arg + 1, nullptr, 16);
                } else if (*arg == '\'') {
                    // octal: '141
                    code_point = strtol(arg + 1, nullptr, 8);
                } else if (*arg == '`') {
                    // Character: `a means ASCII value of 'a'
                    if (arg[1]) {
                        code_point = (unsigned char)arg[1];
                    }
                } else {
                    // decimal: 98
                    code_point = strtol(arg, nullptr, 10);
                }
                // Convert code point to UTF-8
                if (code_point > 0 && code_point <= 0x10FFFF) {
                    char utf8_buf[8];
                    int len = 0;
                    if (code_point <= 0x7F) {
                        utf8_buf[len++] = (char)code_point;
                    } else if (code_point <= 0x7FF) {
                        utf8_buf[len++] = (char)(0xC0 | (code_point >> 6));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else if (code_point <= 0xFFFF) {
                        utf8_buf[len++] = (char)(0xE0 | (code_point >> 12));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    } else {
                        utf8_buf[len++] = (char)(0xF0 | (code_point >> 18));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
                        utf8_buf[len++] = (char)(0x80 | (code_point & 0x3F));
                    }
                    utf8_buf[len] = '\0';
                    return doc_create_text_cstr(arena, utf8_buf, DocTextStyle::plain());
                }
            }
        }
        return nullptr;
    }
    // Special characters
    // Helper to check for empty curly_group child (e.g., \textbackslash{})
    auto has_empty_curly_group_child = [&elem]() -> bool {
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isElement()) {
                ElementReader ch_elem = child.asElement();
                const char* ch_tag = ch_elem.tagName();
                if (ch_tag && (tag_eq(ch_tag, "curly_group") || tag_eq(ch_tag, "group"))) {
                    // Check if the group is empty
                    auto group_iter = ch_elem.children();
                    ItemReader group_child;
                    if (!group_iter.next(&group_child)) {
                        return true;  // empty curly group
                    }
                    // Check if the child is whitespace-only string
                    if (group_child.isString()) {
                        const char* text = group_child.cstring();
                        if (!text || text[0] == '\0') return true;
                        // Check if all whitespace
                        bool all_ws = true;
                        for (const char* p = text; *p; p++) {
                            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                                all_ws = false;
                                break;
                            }
                        }
                        if (all_ws) return true;
                    }
                }
            }
        }
        return false;
    };
    
    if (tag_eq(tag, "textbackslash")) {
        // If followed by empty {}, add ZWS for word separation
        if (has_empty_curly_group_child()) {
            return doc_create_text_cstr(arena, "\\\xE2\x80\x8B", DocTextStyle::plain());  // \ + ZWS
        }
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
    // Spacing commands that output Unicode space characters
    // U+2003 Em Space for \quad
    if (tag_eq(tag, "quad")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x83", DocTextStyle::plain());  // em space
    }
    // U+2003 Em Space twice for \qquad
    if (tag_eq(tag, "qquad")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x83\xE2\x80\x83", DocTextStyle::plain());  // 2x em space
    }
    // U+2002 En Space for \enspace/\enskip
    if (tag_eq(tag, "enspace") || tag_eq(tag, "enskip")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x82", DocTextStyle::plain());  // en space
    }
    // U+2009 Thin Space for \thinspace/\,
    if (tag_eq(tag, "thinspace")) {
        return doc_create_text_cstr(arena, "\xE2\x80\x89", DocTextStyle::plain());  // thin space
    }
    // Negative thin space needs CSS styling
    if (tag_eq(tag, "negthinspace")) {
        return doc_create_raw_html_cstr(arena, "<span class=\"negthinspace\"></span>");
    }
    // \hspace{...} - horizontal space with specified width
    if (tag_eq(tag, "hspace")) {
        // Get width from child text content using extract_text_content
        const char* width_str = extract_text_content(item, arena);
        if (width_str && strlen(width_str) > 0) {
            // Parse length and convert to pixels
            char* end = nullptr;
            double num = strtod(width_str, &end);  // use double for precision
            if (end != width_str) {
                // Skip whitespace
                while (end && *end == ' ') end++;
                // Handle units - use precise conversion factors
                double width_px = num;  // default: assume pixels
                if (end && *end) {
                    if (strncmp(end, "pt", 2) == 0) {
                        width_px = num * (96.0 / 72.0);  // 1pt = 1/72 in
                    } else if (strncmp(end, "cm", 2) == 0) {
                        width_px = num * (96.0 / 2.54);  // 1cm = 96/2.54 px
                    } else if (strncmp(end, "mm", 2) == 0) {
                        width_px = num * (96.0 / 25.4);  // 1mm = 96/25.4 px
                    } else if (strncmp(end, "in", 2) == 0) {
                        width_px = num * 96.0;           // 1in = 96px
                    } else if (strncmp(end, "em", 2) == 0) {
                        width_px = num * 16.0;           // assume 1em = 16px
                    }
                }
                if (width_px > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "<span style=\"margin-right:%.3fpx\"></span>", width_px);
                    return doc_create_raw_html_cstr(arena, buf);
                }
            }
        }
        // Default: output a regular space if width not specified
        return doc_create_text_cstr(arena, " ", DocTextStyle::plain());
    }
    // Non-breaking space
    if (tag_eq(tag, "nobreakspace") || tag_eq(tag, "nbsp")) {
        return doc_create_text_cstr(arena, "\xC2\xA0", DocTextStyle::plain());  // &nbsp;
    }
    
    // Special letter commands (not diacritics, but single-char commands producing specific letters)
    // Scandinavian slashed o
    if (tag_eq(tag, "o")) {
        return doc_create_text_cstr(arena, "\xC3\xB8", DocTextStyle::plain());  // ø
    }
    if (tag_eq(tag, "O")) {
        return doc_create_text_cstr(arena, "\xC3\x98", DocTextStyle::plain());  // Ø
    }
    // German sharp s (eszett)
    if (tag_eq(tag, "ss")) {
        return doc_create_text_cstr(arena, "\xC3\x9F", DocTextStyle::plain());  // ß
    }
    // Dotless i and j (used as base for diacritics)
    if (tag_eq(tag, "i")) {
        return doc_create_text_cstr(arena, "\xC4\xB1", DocTextStyle::plain());  // ı
    }
    if (tag_eq(tag, "j")) {
        return doc_create_text_cstr(arena, "\xC8\xB7", DocTextStyle::plain());  // ȷ
    }
    // Latin ligatures
    if (tag_eq(tag, "ae")) {
        return doc_create_text_cstr(arena, "\xC3\xA6", DocTextStyle::plain());  // æ
    }
    if (tag_eq(tag, "AE")) {
        return doc_create_text_cstr(arena, "\xC3\x86", DocTextStyle::plain());  // Æ
    }
    if (tag_eq(tag, "oe")) {
        return doc_create_text_cstr(arena, "\xC5\x93", DocTextStyle::plain());  // œ
    }
    if (tag_eq(tag, "OE")) {
        return doc_create_text_cstr(arena, "\xC5\x92", DocTextStyle::plain());  // Œ
    }
    // Polish L with stroke
    if (tag_eq(tag, "l")) {
        return doc_create_text_cstr(arena, "\xC5\x82", DocTextStyle::plain());  // ł
    }
    if (tag_eq(tag, "L")) {
        return doc_create_text_cstr(arena, "\xC5\x81", DocTextStyle::plain());  // Ł
    }
    // Inverted punctuation
    if (tag_eq(tag, "textexclamdown")) {
        return doc_create_text_cstr(arena, "\xC2\xA1", DocTextStyle::plain());  // ¡
    }
    if (tag_eq(tag, "textquestiondown")) {
        return doc_create_text_cstr(arena, "\xC2\xBF", DocTextStyle::plain());  // ¿
    }
    
    // Paragraph break command (\par)
    if (tag_eq(tag, "par")) {
        return PARBREAK_MARKER;
    }
    
    // Noindent command (\noindent)
    if (tag_eq(tag, "noindent")) {
        return NOINDENT_MARKER;
    }
    
    // Line break commands (\\ and \newline)
    if (tag_eq(tag, "linebreak_command") || tag_eq(tag, "newline")) {
        DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
        space->space.is_linebreak = true;
        return space;
    }
    
    // Space command - handles various spacing commands
    if (tag_eq(tag, "space_cmd")) {
        auto iter = elem.children();
        ItemReader child;
        if (iter.next(&child) && child.isString()) {
            const char* cmd = child.cstring();
            if (cmd && strlen(cmd) >= 2) {
                char space_char = cmd[1];
                if (space_char == ',') {
                    // Thin space U+2009
                    return doc_create_text_cstr(arena, "\xE2\x80\x89", DocTextStyle::plain());
                } else if (space_char == '-') {
                    // Soft hyphen U+00AD (discretionary hyphen)
                    return doc_create_text_cstr(arena, "\xC2\xAD", DocTextStyle::plain());
                } else if (space_char == ';') {
                    // Thick space - use space element
                    DocElement* space = doc_alloc_element(arena, DocElemType::SPACE);
                    space->space.is_linebreak = false;
                    return space;
                } else if (space_char == '!') {
                    // Negative thin space - absorbs space, output nothing
                    return nullptr;
                } else if (space_char == '/') {
                    // \/ ligature break - inserts ZWNJ (U+200C) to prevent ligature formation
                    return doc_create_text_cstr(arena, "\xE2\x80\x8C", DocTextStyle::plain());
                }
            }
        }
        // Default: ZWSP + space (for \ <space>, \<tab>, \<newline>)
        return doc_create_text_cstr(arena, "\xE2\x80\x8B ", DocTextStyle::plain());
    }
    
    // Section commands (but not paragraph element containing content)
    if (tag_eq(tag, "section") || tag_eq(tag, "subsection") || 
        tag_eq(tag, "subsubsection") || tag_eq(tag, "chapter") || tag_eq(tag, "part")) {
        return build_section_command(tag, elem, arena, doc);
    }
    
    // Handle "paragraph" tag - could be \paragraph{} command or content paragraph
    // \paragraph{} command: has a "title" attribute or child curly_group
    // Content paragraph: children include text content, parbreak, etc.
    if (tag_eq(tag, "paragraph")) {
        // Check for title attribute (indicates \paragraph{Title} command)
        if (elem.has_attr("title")) {
            // \paragraph{Title} sectioning command
            return build_section_command(tag, elem, arena, doc);
        }
        
        // Check if this is a sectioning command vs content paragraph
        // Sectioning command: typically has curly_group child as title
        // Content paragraph: has text strings, parbreak, etc.
        bool is_sectioning_cmd = false;
        bool has_text_content = false;
        
        auto check_iter = elem.children();
        ItemReader check_child;
        while (check_iter.next(&check_child)) {
            if (check_child.isElement()) {
                ElementReader child_elem = check_child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && (tag_eq(child_tag, "curly_group") || 
                                  tag_eq(child_tag, "brack_group"))) {
                    is_sectioning_cmd = true;
                    break;
                }
            } else if (check_child.isString()) {
                const char* text = check_child.cstring();
                // Check if there's meaningful text (not just whitespace or parbreak)
                if (text && strlen(text) > 0) {
                    // Check for content that isn't just whitespace
                    bool all_whitespace = true;
                    for (const char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                            all_whitespace = false;
                            break;
                        }
                    }
                    if (!all_whitespace && strcmp(text, "parbreak") != 0) {
                        has_text_content = true;
                    }
                }
            }
        }
        
        if (is_sectioning_cmd) {
            // \paragraph{Title} sectioning command
            return build_section_command(tag, elem, arena, doc);
        } else {
            // Content paragraph - check if it contains paragraph breaks
            if (contains_parbreak_markers(elem)) {
                // Contains \par or parbreak - need to split into multiple paragraphs
                // Return nullptr - caller (build_body_content_with_paragraphs) will handle this
                // by processing children directly
                return nullptr;
            } else {
                // Simple paragraph - process inline content only
                return build_paragraph(elem, arena, doc);
            }
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
    
    // Quote/verse environments - use alignment for rendering as <div class="list quote"> etc
    if (tag_eq(tag, "quote") || tag_eq(tag, "quotation") || tag_eq(tag, "verse")) {
        return build_alignment_environment(tag, elem, arena, doc);
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
    
    // Preamble commands - these set document metadata but produce no output
    if (tag_eq(tag, "documentclass")) {
        // Extract document class name from first child (text or curly_group)
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            if (child.isString()) {
                const char* text = child.cstring();
                if (text && strlen(text) > 0 && text[0] != '\n') {
                    // Copy and store document class
                    size_t len = strlen(text);
                    char* class_name = (char*)arena_alloc(arena, len + 1);
                    memcpy(class_name, text, len + 1);
                    doc->document_class = class_name;
                    break;
                }
            } else if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                    const char* text = extract_text_content(child, arena);
                    if (text && strlen(text) > 0) {
                        doc->document_class = text;
                        break;
                    }
                }
            }
        }
        return nullptr;  // No output
    }
    
    // Macro definitions - register with document model, no visible output
    if (tag_eq(tag, "newcommand") || tag_eq(tag, "renewcommand") || tag_eq(tag, "providecommand")) {
        register_newcommand(elem, arena, doc);
        return nullptr;  // No visible output
    }
    
    // Package imports - load package macros
    if (tag_eq(tag, "usepackage") || tag_eq(tag, "RequirePackage")) {
        // extract package name - could be direct string child or in curly_group
        auto iter = elem.children();
        ItemReader child;
        const char* pkg_name = nullptr;
        while (iter.next(&child)) {
            if (child.isString()) {
                const char* text = child.cstring();
                // skip whitespace-only strings
                if (text && text[0] != '\0') {
                    bool all_space = true;
                    for (const char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                            all_space = false;
                            break;
                        }
                    }
                    if (!all_space) {
                        pkg_name = text;
                        break;
                    }
                }
            } else if (child.isElement()) {
                ElementReader child_elem = child.asElement();
                const char* child_tag = child_elem.tagName();
                if (child_tag && (tag_eq(child_tag, "curly_group") || tag_eq(child_tag, "arg"))) {
                    pkg_name = extract_text_content(child, arena);
                    break;
                }
            }
        }
        if (pkg_name && strlen(pkg_name) > 0) {
            load_package_macros(doc, pkg_name);
        }
        return nullptr;  // No visible output
    }
    
    // Other preamble commands - ignored in output
    if (tag_eq(tag, "input") || tag_eq(tag, "include") ||
        tag_eq(tag, "author") || tag_eq(tag, "title") || tag_eq(tag, "date") ||
        tag_eq(tag, "newenvironment") ||
        tag_eq(tag, "renewenvironment") || tag_eq(tag, "newtheorem") ||
        tag_eq(tag, "DeclareMathOperator") || tag_eq(tag, "setlength") ||
        tag_eq(tag, "setcounter") || tag_eq(tag, "pagestyle") ||
        tag_eq(tag, "pagenumbering") || tag_eq(tag, "thispagestyle") ||
        tag_eq(tag, "makeatletter") || tag_eq(tag, "makeatother") ||
        tag_eq(tag, "bibliography") || tag_eq(tag, "bibliographystyle") ||
        tag_eq(tag, "graphicspath") || tag_eq(tag, "hypersetup")) {
        return nullptr;  // No visible output
    }
    
    // Standalone begin/end elements (from error recovery) - ignored
    // These are orphaned \begin{...} or \end{...} without matching environment
    if (tag_eq(tag, "begin") || tag_eq(tag, "end") ||
        tag_eq(tag, "begin_env") || tag_eq(tag, "end_env")) {
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
        
        // \begin{empty}...\end{empty} environment - inline pass-through with ZWSP at end boundary only
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
        
        // Trim trailing whitespace from last content text element (so ZWSP immediately follows content)
        // Find the last text element that has non-whitespace content
        DocElement* last_content_text = nullptr;
        for (DocElement* child = container->first_child; child; child = child->next_sibling) {
            if (child->type == DocElemType::TEXT_RUN && child->text.text && child->text.text_len > 0) {
                // Check if this text has any non-whitespace content
                const char* p = child->text.text;
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                if (*p) last_content_text = child;  // Has non-whitespace content
            } else if (child->type == DocElemType::TEXT_SPAN) {
                // Check inside span for text elements with content
                for (DocElement* inner = child->first_child; inner; inner = inner->next_sibling) {
                    if (inner->type == DocElemType::TEXT_RUN && inner->text.text && inner->text.text_len > 0) {
                        const char* p = inner->text.text;
                        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
                        if (*p) last_content_text = inner;  // Has non-whitespace content
                    }
                }
            }
        }
        if (last_content_text) {
            // Trim trailing whitespace from the last content text element
            char* text = (char*)last_content_text->text.text;
            size_t len = last_content_text->text.text_len;
            while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\n')) {
                len--;
            }
            text[len] = '\0';
            last_content_text->text.text_len = len;
        }
        
        // Clear any whitespace-only TEXT_RUNs that appear after last_content_text
        // These would create space before the ZWSP
        bool found_last = false;
        for (DocElement* child = container->first_child; child; child = child->next_sibling) {
            if (child == last_content_text) {
                found_last = true;
            } else if (found_last && child->type == DocElemType::TEXT_RUN) {
                // Check if whitespace-only
                bool ws_only = true;
                if (child->text.text) {
                    for (const char* p = child->text.text; *p && ws_only; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n') ws_only = false;
                    }
                }
                if (ws_only) {
                    // Clear this text run
                    child->text.text_len = 0;
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
    
    // Curly/brack groups and _seq - inline transparent containers
    // These should NOT be block elements - just process their children inline
    // For curly_group and brack_group, add ZWSP at whitespace boundaries to preserve spacing
    // ZWSP is always added at the end to mark the } boundary
    // sequence is a pseudo-element from error recovery that just groups inline_math children
    // EXCEPTION: _seq with document-level block children should be processed as block container
    if (tag_eq(tag, "curly_group") || tag_eq(tag, "brack_group") || tag_eq(tag, "group") || 
        tag_eq(tag, "_seq") || tag_eq(tag, "sequence")) {
        
        bool is_seq = tag_eq(tag, "_seq") || tag_eq(tag, "sequence");
        
        // Check if curly_group/brack_group contains only an environment name (from broken \begin{...} or \end{...})
        // If so, skip it entirely to avoid spurious text output
        if (!is_seq) {
            int64_t child_count = elem.childCount();
            if (child_count == 1) {
                ItemReader only_child = elem.childAt(0);
                if (only_child.isString()) {
                    const char* content = only_child.cstring();
                    if (content) {
                        // List of environment names that should be filtered out
                        static const char* env_names[] = {
                            "center", "itshape", "document", "bfseries", "mdseries",
                            "slshape", "upshape", "scshape", "rmfamily", "sffamily",
                            "ttfamily", "tiny", "scriptsize", "footnotesize", "small",
                            "normalsize", "large", "Large", "LARGE", "huge", "Huge",
                            "abstract", "itemize", "enumerate", "description",
                            "quote", "quotation", "verse", "flushleft", "flushright",
                            "verbatim", "picture", "minipage", "tabular", "table",
                            "figure", "multicols", "equation", "align", "gather"
                        };
                        for (size_t i = 0; i < sizeof(env_names)/sizeof(env_names[0]); i++) {
                            if (tag_eq(content, env_names[i])) {
                                return nullptr;  // Skip this curly_group - it's a spurious env name
                            }
                        }
                    }
                }
            }
        }
        
        // For _seq, check if it contains document-level blocks (sections, environments)
        // If so, process like a document body instead of inline container
        if (is_seq) {
            bool has_document_blocks = false;
            auto check_iter = elem.children();
            ItemReader check_child;
            while (check_iter.next(&check_child)) {
                if (check_child.isElement()) {
                    ElementReader check_elem = check_child.asElement();
                    const char* check_tag = check_elem.tagName();
                    if (check_tag && is_document_block_tag(check_tag)) {
                        has_document_blocks = true;
                        break;
                    }
                }
            }
            
            if (has_document_blocks) {
                // Process like document body - use build_body_content_with_paragraphs
                DocElement* block_container = doc_alloc_element(arena, DocElemType::SECTION);
                build_body_content_with_paragraphs(block_container, elem, arena, doc);
                
                // If only one child, return it directly (unwrap the section)
                if (block_container->first_child && block_container->first_child == block_container->last_child) {
                    DocElement* only_child = block_container->first_child;
                    only_child->parent = nullptr;
                    only_child->next_sibling = nullptr;
                    return only_child;
                }
                return block_container->first_child ? block_container : nullptr;
            }
        }
        
        DocElement* span = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
        span->text.style = DocTextStyle::plain();
        
        // Scan children to detect whitespace at boundaries (for ZWSP insertion)
        bool starts_with_space = false;
        bool ends_with_space = false;
        bool has_content = false;
        
        auto scan_iter = elem.children();
        ItemReader scan_child;
        bool first = true;
        while (scan_iter.next(&scan_child)) {
            if (scan_child.isString()) {
                const char* text = scan_child.cstring();
                if (text && strlen(text) > 0) {
                    if (first && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n')) {
                        starts_with_space = true;
                    }
                    // Check last char for ends_with_space
                    size_t len = strlen(text);
                    char last = text[len - 1];
                    if (last == ' ' || last == '\t' || last == '\n') {
                        ends_with_space = true;
                    } else {
                        ends_with_space = false;
                    }
                    // Check if this string has non-whitespace content
                    for (const char* p = text; *p; p++) {
                        if (*p != ' ' && *p != '\t' && *p != '\n') {
                            has_content = true;
                            break;
                        }
                    }
                    first = false;
                }
            } else if (scan_child.isElement()) {
                has_content = true;
                first = false;
                ends_with_space = false;  // Element at end means no trailing space
            }
        }
        
        // Add ZWSP at start if group has leading whitespace (and not _seq)
        if (starts_with_space && !is_seq) {
            doc_append_child(span, doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain()));
        }
        
        // Track font state within this group - font declarations apply until group ends
        unsigned int group_font_flags = 0;
        bool need_strip_leading_space = false;
        
        auto iter = elem.children();
        ItemReader child;
        while (iter.next(&child)) {
            DocElement* child_elem = build_doc_element(child, arena, doc);
            if (!child_elem) continue;
            
            // Font markers update group font state with toggle logic for \em
            if (is_font_marker(child_elem)) {
                uint32_t new_flags = font_marker_to_style_flags(child_elem);
                if (new_flags == DocTextStyle::EMPHASIS) {
                    // \em toggles between italic and upright
                    bool currently_italic = (group_font_flags & DocTextStyle::ITALIC) != 0;
                    bool currently_upright = (group_font_flags & DocTextStyle::UPRIGHT) != 0;
                    group_font_flags &= ~(DocTextStyle::ITALIC | DocTextStyle::UPRIGHT);
                    if (currently_italic) {
                        group_font_flags |= DocTextStyle::UPRIGHT;
                    } else if (currently_upright) {
                        group_font_flags |= DocTextStyle::ITALIC;
                    } else {
                        group_font_flags |= DocTextStyle::ITALIC;
                    }
                } else {
                    group_font_flags |= new_flags;
                }
                need_strip_leading_space = true;  // strip space after font declaration
                continue;
            }
            
            // Skip other special markers
            if (is_special_marker(child_elem)) continue;
            
            // Strip leading space after font declaration (e.g., "\bfseries text" should not have space before "text")
            if (need_strip_leading_space && child_elem->type == DocElemType::TEXT_RUN) {
                const char* text = child_elem->text.text;
                if (text && text[0] == ' ') {
                    // Skip the leading space by adjusting pointer
                    if (text[1]) {
                        child_elem->text.text = arena_strdup(arena, text + 1);
                    } else {
                        // Just a space - skip entirely
                        continue;
                    }
                }
                need_strip_leading_space = false;
            }
            // If font flags are active, wrap content in styled element
            if (group_font_flags != 0) {
                DocElement* styled = wrap_in_font_style(child_elem, group_font_flags, arena);
                doc_append_child(span, styled);
            } else {
                doc_append_child(span, child_elem);
            }
        }
        
        // Add ZWSP at end of curly_group/brack_group/group (not _seq):
        // Only add ZWSP if group has content but NO trailing whitespace
        // When trailing whitespace exists, the space character marks the boundary
        if (!is_seq && has_content && !ends_with_space) {
            doc_append_child(span, doc_create_text_cstr(arena, "\xE2\x80\x8B", DocTextStyle::plain()));
        }
        
        // If only one child (and no ZWSP added), return it directly
        if (is_seq && span->first_child && span->first_child == span->last_child) {
            DocElement* only_child = span->first_child;
            only_child->parent = nullptr;
            only_child->next_sibling = nullptr;
            return only_child;
        }
        
        return span->first_child ? span : nullptr;
    }
    
    // Try to expand as user-defined macro before falling back to generic handling
    log_debug("doc_model: checking for macro expansion, tag='%s'", tag);
    DocElement* macro_result = try_expand_macro(tag, elem, arena, doc);
    if (macro_result) {
        return macro_result;
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
    
    // Two-pass resolution: resolve all pending cross-references
    // Now that all labels are registered, we can resolve forward references
    doc->resolve_pending_refs();
    
    log_debug("doc_model_from_latex: built document with %d labels, %d macros, %d pending refs resolved",
              doc->label_count, doc->macro_count, doc->pending_ref_count);
    
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
    
    // Two-pass resolution: resolve all pending cross-references
    // Now that all labels are registered, we can resolve forward references
    doc->resolve_pending_refs();
    
    log_debug("doc_model_from_string: built document model from %zu bytes of LaTeX, %d labels, %d pending refs",
              len, doc->label_count, doc->pending_ref_count);
    
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
// DocElement to TexNode Conversion (Unified Pipeline)
// ============================================================================

#ifndef DOC_MODEL_MINIMAL

// Forward declarations for conversion helpers
static TexNode* convert_text_run(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_text_span(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_paragraph(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_heading(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_list(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_list_item(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_math(DocElement* elem, Arena* arena, LaTeXContext& ctx);
static TexNode* convert_space(DocElement* elem, Arena* arena, LaTeXContext& ctx);

// Get font spec from DocTextStyle
static FontSpec doc_style_to_font(const DocTextStyle& style, float base_size_pt, LaTeXContext& ctx) {
    (void)ctx;  // May be used for font lookup later
    FontSpec font;
    
    // Determine font name based on style flags
    const char* font_name = "cmr10";  // default roman
    if (style.has(DocTextStyle::MONOSPACE)) {
        font_name = "cmtt10";  // typewriter
    } else if (style.has(DocTextStyle::SANS_SERIF)) {
        font_name = "cmss10";  // sans-serif
    } else if (style.has(DocTextStyle::ITALIC) || style.has(DocTextStyle::SLANTED)) {
        if (style.has(DocTextStyle::BOLD)) {
            font_name = "cmbx10";  // bold extended (no bold italic in CM)
        } else {
            font_name = "cmti10";  // text italic
        }
    } else if (style.has(DocTextStyle::BOLD)) {
        font_name = "cmbx10";  // bold extended
    } else if (style.has(DocTextStyle::SMALLCAPS)) {
        font_name = "cmcsc10";  // small caps
    }
    
    // Determine size
    float size_pt = base_size_pt;
    if (style.font_size_pt > 0) {
        size_pt = style.font_size_pt;
    } else {
        // Map named sizes to points (based on 10pt base)
        switch (style.font_size_name) {
            case FontSizeName::FONT_TINY:         size_pt = 5.0f; break;
            case FontSizeName::FONT_SCRIPTSIZE:   size_pt = 7.0f; break;
            case FontSizeName::FONT_FOOTNOTESIZE: size_pt = 8.0f; break;
            case FontSizeName::FONT_SMALL:        size_pt = 9.0f; break;
            case FontSizeName::FONT_NORMALSIZE:   size_pt = 10.0f; break;
            case FontSizeName::FONT_LARGE:        size_pt = 12.0f; break;
            case FontSizeName::FONT_LARGE2:       size_pt = 14.4f; break;
            case FontSizeName::FONT_LARGE3:       size_pt = 17.28f; break;
            case FontSizeName::FONT_HUGE:         size_pt = 20.74f; break;
            case FontSizeName::FONT_HUGE2:        size_pt = 24.88f; break;
            default: break;
        }
    }
    
    font.name = font_name;
    font.size_pt = size_pt;
    font.face = nullptr;  // Will be set by TFM lookup
    font.tfm_index = 0;
    
    return font;
}

// Create a character node with metrics from TFM
static TexNode* make_text_char(Arena* arena, int32_t codepoint, const FontSpec& font, 
                                TFMFontManager* fonts) {
    TexNode* node = make_char(arena, codepoint, font);
    
    // Get metrics from TFM if available
    if (fonts) {
        TFMFont* tfm = fonts->get_font(font.name);
        if (tfm && codepoint >= 0 && codepoint <= 127) {
            float scale = font.size_pt / tfm->design_size;
            node->width = tfm->char_width(codepoint) * scale;
            node->height = tfm->char_height(codepoint) * scale;
            node->depth = tfm->char_depth(codepoint) * scale;
            node->italic = tfm->char_italic(codepoint) * scale;
        } else {
            // Fallback metrics
            node->width = font.size_pt * 0.5f;
            node->height = font.size_pt * 0.7f;
            node->depth = 0;
            node->italic = 0;
        }
    }
    
    return node;
}

// Create interword glue
static TexNode* make_text_space(Arena* arena, const FontSpec& font, TFMFontManager* fonts) {
    float space = font.size_pt / 3.0f;       // 1/3 em
    float stretch = font.size_pt / 6.0f;     // 1/6 em stretch
    float shrink = font.size_pt / 9.0f;      // 1/9 em shrink
    
    if (fonts) {
        TFMFont* tfm = fonts->get_font(font.name);
        if (tfm) {
            float scale = font.size_pt / tfm->design_size;
            space = tfm->space * scale;
            stretch = tfm->space_stretch * scale;
            shrink = tfm->space_shrink * scale;
        }
    }
    
    Glue g = Glue::flexible(space, stretch, shrink);
    return make_glue(arena, g, "interword");
}

// Convert TEXT_RUN to character nodes
static TexNode* convert_text_run(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::TEXT_RUN) return nullptr;
    
    const char* text = elem->text.text;
    size_t len = elem->text.text_len;
    if (!text || len == 0) return nullptr;
    
    FontSpec font = doc_style_to_font(elem->text.style, 10.0f, ctx);
    TFMFontManager* fonts = ctx.doc_ctx.fonts;
    
    // Create HList to hold characters
    TexNode* hlist = make_hlist(arena);
    
    for (size_t i = 0; i < len; i++) {
        int32_t cp = (unsigned char)text[i];
        
        if (cp == ' ' || cp == '\t') {
            // Interword space
            TexNode* space = make_text_space(arena, font, fonts);
            hlist->append_child(space);
        } else if (cp == '\n') {
            // Line break in source - treat as space
            TexNode* space = make_text_space(arena, font, fonts);
            hlist->append_child(space);
        } else {
            // Regular character
            TexNode* ch = make_text_char(arena, cp, font, fonts);
            hlist->append_child(ch);
        }
    }
    
    return hlist;
}

// Convert TEXT_SPAN (styled text) to HList
static TexNode* convert_text_span(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::TEXT_SPAN) return nullptr;
    
    // Get font from style
    FontSpec font = doc_style_to_font(elem->text.style, 10.0f, ctx);
    
    // Create HList for span
    TexNode* hlist = make_hlist(arena);
    
    // If there's direct text content
    if (elem->text.text && elem->text.text_len > 0) {
        const char* text = elem->text.text;
        size_t len = elem->text.text_len;
        TFMFontManager* fonts = ctx.doc_ctx.fonts;
        
        for (size_t i = 0; i < len; i++) {
            int32_t cp = (unsigned char)text[i];
            if (cp == ' ' || cp == '\t' || cp == '\n') {
                hlist->append_child(make_text_space(arena, font, fonts));
            } else {
                hlist->append_child(make_text_char(arena, cp, font, fonts));
            }
        }
    }
    
    // Process children
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        TexNode* child_node = doc_element_to_texnode(child, arena, ctx);
        if (child_node) {
            hlist->append_child(child_node);
        }
    }
    
    return hlist;
}

// Convert PARAGRAPH to HList (before line breaking)
static TexNode* convert_paragraph(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::PARAGRAPH) return nullptr;
    
    // Create HList for paragraph content
    TexNode* hlist = make_hlist(arena);
    
    // Add paragraph indentation unless FLAG_NOINDENT
    if (!(elem->flags & DocElement::FLAG_NOINDENT)) {
        float parindent = 20.0f;  // ~1.5em at 10pt
        TexNode* indent = make_kern(arena, parindent);
        hlist->append_child(indent);
    }
    
    // Process children (text runs, spans, inline math)
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        TexNode* child_node = doc_element_to_texnode(child, arena, ctx);
        if (child_node) {
            hlist->append_child(child_node);
        }
    }
    
    // Add parfillskip (stretchable glue to fill last line)
    Glue parfillskip = Glue::flexible(0, 1, 0);
    parfillskip.stretch_order = GlueOrder::Fil;
    hlist->append_child(make_glue(arena, parfillskip, "parfillskip"));
    
    return hlist;
}

// Convert HEADING to VList with title
static TexNode* convert_heading(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::HEADING) return nullptr;
    
    int level = elem->heading.level;
    const char* title = elem->heading.title;
    const char* number = elem->heading.number;
    
    // Determine font size based on level
    float size_pt = 10.0f;
    switch (level) {
        case 0: size_pt = 24.88f; break;  // \part
        case 1: size_pt = 20.74f; break;  // \chapter
        case 2: size_pt = 14.4f; break;   // \section
        case 3: size_pt = 12.0f; break;   // \subsection
        case 4: size_pt = 10.0f; break;   // \subsubsection
        default: size_pt = 10.0f; break;
    }
    
    // Create font spec for heading (bold)
    DocTextStyle style = DocTextStyle::plain();
    style.set(DocTextStyle::BOLD);
    style.font_size_pt = size_pt;
    FontSpec font = doc_style_to_font(style, size_pt, ctx);
    
    TFMFontManager* fonts = ctx.doc_ctx.fonts;
    
    // Create HList for heading text
    TexNode* hlist = make_hlist(arena);
    
    // Add section number if present
    if (number && !(elem->flags & DocElement::FLAG_STARRED)) {
        for (const char* p = number; *p; p++) {
            hlist->append_child(make_text_char(arena, *p, font, fonts));
        }
        // Quad space after number
        Glue quad = Glue::fixed(size_pt);
        hlist->append_child(make_glue(arena, quad, "quad"));
    }
    
    // Add title text
    if (title) {
        for (const char* p = title; *p; p++) {
            if (*p == ' ') {
                hlist->append_child(make_text_space(arena, font, fonts));
            } else {
                hlist->append_child(make_text_char(arena, *p, font, fonts));
            }
        }
    }
    
    // Wrap in VList with spacing
    TexNode* vlist = make_vlist(arena);
    
    // Add space before heading
    float above_skip = size_pt * 2.0f;  // 2em above
    vlist->append_child(make_glue(arena, Glue::flexible(above_skip, above_skip/3, 0), "abovesectionskip"));
    
    // Add the heading line
    vlist->append_child(hlist);
    
    // Add space after heading
    float below_skip = size_pt * 1.0f;  // 1em below
    vlist->append_child(make_glue(arena, Glue::flexible(below_skip, 0, 0), "belowsectionskip"));
    
    return vlist;
}

// Convert LIST to VList
static TexNode* convert_list(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::LIST) return nullptr;
    
    TexNode* vlist = make_vlist(arena);
    
    // Add some vertical space before list
    vlist->append_child(make_glue(arena, Glue::flexible(6.0f, 2.0f, 1.0f), "listskip"));
    
    // Process list items
    int item_num = elem->list.start_num;
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        if (child->type == DocElemType::LIST_ITEM) {
            // Set item number for enumerate
            if (elem->list.list_type == ListType::ENUMERATE) {
                child->list_item.item_number = item_num++;
            }
            
            TexNode* item_node = convert_list_item(child, arena, ctx);
            if (item_node) {
                vlist->append_child(item_node);
            }
        }
    }
    
    // Add some vertical space after list
    vlist->append_child(make_glue(arena, Glue::flexible(6.0f, 2.0f, 1.0f), "listskip"));
    
    return vlist;
}

// Convert LIST_ITEM to HList with label
static TexNode* convert_list_item(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem || elem->type != DocElemType::LIST_ITEM) return nullptr;
    
    TexNode* hlist = make_hlist(arena);
    FontSpec font = doc_style_to_font(DocTextStyle::plain(), 10.0f, ctx);
    TFMFontManager* fonts = ctx.doc_ctx.fonts;
    
    // Add left margin indent
    float indent = 20.0f + (elem->parent ? elem->parent->list.nesting_level * 15.0f : 0);
    hlist->append_child(make_kern(arena, indent));
    
    // Add item label
    if (elem->list_item.has_custom_label && elem->list_item.label) {
        // Custom label text
        for (const char* p = elem->list_item.label; *p; p++) {
            hlist->append_child(make_text_char(arena, *p, font, fonts));
        }
    } else if (elem->parent && elem->parent->list.list_type == ListType::ENUMERATE) {
        // Numbered item
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.", elem->list_item.item_number);
        for (const char* p = buf; *p; p++) {
            hlist->append_child(make_text_char(arena, *p, font, fonts));
        }
    } else {
        // Bullet (use bullet character or dash)
        hlist->append_child(make_text_char(arena, 0x2022, font, fonts)); // bullet
    }
    
    // Space after label
    hlist->append_child(make_glue(arena, Glue::fixed(6.0f), "labelsep"));
    
    // Process item content
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        TexNode* child_node = doc_element_to_texnode(child, arena, ctx);
        if (child_node) {
            hlist->append_child(child_node);
        }
    }
    
    return hlist;
}

// Convert MATH_* - pass through existing TexNode
static TexNode* convert_math(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    (void)arena; (void)ctx;
    if (!elem) return nullptr;
    
    // Math elements already have a pre-typeset TexNode
    if (elem->math.node) {
        return elem->math.node;
    }
    
    log_debug("doc_model: math element has no pre-typeset node");
    return nullptr;
}

// Convert SPACE elements
static TexNode* convert_space(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    (void)ctx;
    if (!elem || elem->type != DocElemType::SPACE) return nullptr;
    
    if (elem->space.is_linebreak) {
        // Line break - force break penalty
        return make_penalty(arena, PENALTY_FORCE_BREAK);
    }
    
    if (elem->space.vspace > 0) {
        // Vertical space
        return make_glue(arena, Glue::fixed(elem->space.vspace), "vspace");
    }
    
    if (elem->space.hspace > 0) {
        // Horizontal space
        return make_glue(arena, Glue::fixed(elem->space.hspace), "hspace");
    }
    
    // Default space
    return make_glue(arena, Glue::fixed(3.0f), "space");
}

// Main dispatcher for doc_element_to_texnode
TexNode* doc_element_to_texnode(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem) return nullptr;
    
    switch (elem->type) {
        case DocElemType::TEXT_RUN:
            return convert_text_run(elem, arena, ctx);
            
        case DocElemType::TEXT_SPAN:
            return convert_text_span(elem, arena, ctx);
            
        case DocElemType::PARAGRAPH:
            return convert_paragraph(elem, arena, ctx);
            
        case DocElemType::HEADING:
            return convert_heading(elem, arena, ctx);
            
        case DocElemType::LIST:
            return convert_list(elem, arena, ctx);
            
        case DocElemType::LIST_ITEM:
            return convert_list_item(elem, arena, ctx);
            
        case DocElemType::MATH_INLINE:
        case DocElemType::MATH_DISPLAY:
        case DocElemType::MATH_EQUATION:
        case DocElemType::MATH_ALIGN:
            return convert_math(elem, arena, ctx);
            
        case DocElemType::SPACE:
            return convert_space(elem, arena, ctx);
            
        case DocElemType::DOCUMENT:
        case DocElemType::SECTION: {
            // Container types - create VList of children
            TexNode* vlist = make_vlist(arena);
            for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
                TexNode* child_node = doc_element_to_texnode(child, arena, ctx);
                if (child_node) {
                    vlist->append_child(child_node);
                    
                    // Add paragraph skip between block elements
                    if (child->type == DocElemType::PARAGRAPH && 
                        child->next_sibling && 
                        child->next_sibling->type == DocElemType::PARAGRAPH) {
                        Glue parskip = Glue::flexible(6.0f, 3.0f, 1.0f);
                        vlist->append_child(make_glue(arena, parskip, "parskip"));
                    }
                }
            }
            return vlist;
        }
        
        case DocElemType::BLOCKQUOTE:
        case DocElemType::ALIGNMENT: {
            // Block with indentation
            TexNode* vlist = make_vlist(arena);
            
            // Add left margin
            float margin = 20.0f;
            vlist->append_child(make_kern(arena, margin));
            
            for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
                TexNode* child_node = doc_element_to_texnode(child, arena, ctx);
                if (child_node) {
                    vlist->append_child(child_node);
                }
            }
            
            return vlist;
        }
        
        case DocElemType::FOOTNOTE:
        case DocElemType::CITATION:
        case DocElemType::CROSS_REF:
        case DocElemType::LINK:
            // Inline references - create simple text
            // TODO: Implement proper formatting
            return nullptr;
            
        case DocElemType::TABLE:
        case DocElemType::TABLE_ROW:
        case DocElemType::TABLE_CELL:
        case DocElemType::FIGURE:
        case DocElemType::IMAGE:
        case DocElemType::CODE_BLOCK:
        case DocElemType::ABSTRACT:
        case DocElemType::TITLE_BLOCK:
            // Complex elements - TODO: implement
            log_debug("doc_element_to_texnode: %s not yet implemented", 
                     doc_elem_type_name(elem->type));
            return nullptr;
            
        case DocElemType::RAW_HTML:
        case DocElemType::RAW_LATEX:
        case DocElemType::ERROR:
            // Skip these
            return nullptr;
    }
    
    return nullptr;
}

// Main entry point: convert entire document model to TexNode tree
TexNode* doc_model_to_texnode(TexDocumentModel* doc, Arena* arena, LaTeXContext& ctx) {
    if (!doc || !doc->root) {
        log_error("doc_model_to_texnode: no document or root element");
        return nullptr;
    }
    
    log_debug("doc_model_to_texnode: converting document model to TexNode");
    
    TexNode* result = doc_element_to_texnode(doc->root, arena, ctx);
    
    if (result) {
        log_debug("doc_model_to_texnode: created TexNode tree");
    } else {
        log_error("doc_model_to_texnode: conversion failed");
    }
    
    return result;
}

// Apply line breaking to all paragraphs in a VList
static void apply_line_breaking_recursive(TexNode* node, Arena* arena, 
                                           const LineBreakParams& params,
                                           float baseline_skip) {
    if (!node) return;
    
    // Process children
    for (TexNode* child = node->first_child; child; ) {
        TexNode* next = child->next_sibling;
        
        // If this child is an HList that looks like a paragraph content,
        // apply line breaking to it
        if (child->node_class == NodeClass::HList && child->first_child) {
            // Check if it has text/glue content (paragraph content)
            bool has_chars = false;
            for (TexNode* n = child->first_child; n; n = n->next_sibling) {
                if (n->node_class == NodeClass::Char || 
                    n->node_class == NodeClass::Glue ||
                    n->node_class == NodeClass::Ligature) {
                    has_chars = true;
                    break;
                }
            }
            
            if (has_chars) {
                // This looks like paragraph content - apply line breaking
                TexNode* typeset_para = typeset_paragraph(child, params, baseline_skip, arena);
                if (typeset_para) {
                    // Replace the HList with the typeset VList
                    // Insert the new content before the child
                    if (child->prev_sibling) {
                        child->prev_sibling->next_sibling = typeset_para;
                        typeset_para->prev_sibling = child->prev_sibling;
                    } else {
                        node->first_child = typeset_para;
                    }
                    
                    if (child->next_sibling) {
                        child->next_sibling->prev_sibling = typeset_para;
                        typeset_para->next_sibling = child->next_sibling;
                    } else {
                        node->last_child = typeset_para;
                    }
                    
                    typeset_para->parent = node;
                }
            } else {
                // Recurse into non-paragraph HLists
                apply_line_breaking_recursive(child, arena, params, baseline_skip);
            }
        } else if (child->node_class == NodeClass::VList || 
                   child->node_class == NodeClass::VBox) {
            // Recurse into VLists
            apply_line_breaking_recursive(child, arena, params, baseline_skip);
        }
        
        child = next;
    }
}

// Typeset a document with line breaking and page breaking
TexNode* doc_model_typeset(TexDocumentModel* doc, Arena* arena, LaTeXContext& ctx,
                           const LineBreakParams& line_params,
                           const PageBreakParams& page_params) {
    if (!doc || !doc->root) {
        log_error("doc_model_typeset: no document or root element");
        return nullptr;
    }
    
    log_debug("doc_model_typeset: converting and typesetting document");
    
    // Step 1: Convert DocElement tree to TexNode tree (without line breaking)
    TexNode* vlist = doc_element_to_texnode(doc->root, arena, ctx);
    if (!vlist) {
        log_error("doc_model_typeset: conversion failed");
        return nullptr;
    }
    
    // Step 2: Apply line breaking to paragraphs
    float baseline_skip = ctx.doc_ctx.baseline_skip();
    apply_line_breaking_recursive(vlist, arena, line_params, baseline_skip);
    
    log_debug("doc_model_typeset: line breaking complete");
    
    // Step 3: Apply page breaking (optional - only if page_height > 0)
    if (page_params.page_height > 0) {
        int page_count = 0;
        PageContent* pages = paginate(vlist, page_params, &page_count, arena);
        
        if (pages && page_count > 0) {
            log_debug("doc_model_typeset: page breaking complete, %d pages", page_count);
            
            // For now, return the first page's content
            // TODO: Return full page array or wrap in document structure
            return pages[0].vlist;
        }
    }
    
    return vlist;
}

#else // DOC_MODEL_MINIMAL

// Stub implementations when Lambda runtime is not available
TexNode* doc_model_to_texnode(TexDocumentModel* doc, Arena* arena, LaTeXContext& ctx) {
    (void)doc; (void)arena; (void)ctx;
    log_debug("doc_model_to_texnode: minimal stub (DOC_MODEL_MINIMAL defined)");
    return nullptr;
}

TexNode* doc_element_to_texnode(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    (void)elem; (void)arena; (void)ctx;
    log_debug("doc_element_to_texnode: minimal stub (DOC_MODEL_MINIMAL defined)");
    return nullptr;
}

#endif // DOC_MODEL_MINIMAL

} // namespace tex
