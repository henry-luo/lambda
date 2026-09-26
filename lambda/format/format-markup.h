#ifndef FORMAT_MARKUP_H
#define FORMAT_MARKUP_H

#include "format-utils.h"
#include "format-utils.hpp"

// ==============================================================================
// Unified Markup Emitter — Public API
// ==============================================================================

// Format a Lambda data item into lightweight markup syntax using the given rules.
// Replaces the per-format functions: format_markdown, format_rst, format_org,
// format_wiki, format_textile.
void format_markup(StringBuf* sb, Item root_item, const MarkupOutputRules* rules);

// Format into a newly allocated String*.
String* format_markup_string(Pool* pool, Item root_item, const MarkupOutputRules* rules);

// ==============================================================================
// MarkupEmitter — unified markup formatter driven by MarkupOutputRules
// ==============================================================================
// Declared here so a format with its own block layer (Markdown, format-md.cpp)
// can reuse the shared inline emission from its custom element handler.

class MarkupEmitter : public FormatterContextCpp {
public:
    MarkupEmitter(const MarkupOutputRules* rules, Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , rules_(rules)
        , list_depth_(0)
    {}

    void format_item(const ItemReader& item);
    void format_element(const ElementReader& elem);
    void format_children(const ElementReader& elem);
    void format_children_raw(const ElementReader& elem);
    void format_text(String* str);
    void format_text_cstr(const char* text);

    const MarkupOutputRules* rules() const { return rules_; }
    int list_depth() const { return list_depth_; }

    // Markdown block layout nesting: 0 while an element is reached through the
    // generic dispatch at the top of the output, so the block layer knows when
    // it owns the document's final newline.
    int block_nesting = 0;

private:
    const MarkupOutputRules* rules_;
    int list_depth_;

    // element handlers
    void emit_heading(const ElementReader& elem);
    void emit_inline(const ElementReader& elem, const char* open, const char* close);
    void emit_link(const ElementReader& elem);
    void emit_image(const ElementReader& elem);
    void emit_list(const ElementReader& elem, bool ordered, int depth);
    void emit_list_item(const ElementReader& elem, bool ordered, int depth, int index);
    void emit_code_block(const ElementReader& elem);
    void emit_blockquote(const ElementReader& elem);
    void emit_paragraph(const ElementReader& elem);
    void emit_hr();
    void emit_br();

    // tag matching helpers
    bool match_inline_tag(const char* tag, const char* open, const char* close);
    bool is_container_tag(const char* tag) const;
    bool is_skip_tag(const char* tag) const;
};

// ==============================================================================
// Table handler callbacks (used in MarkupOutputRules.emit_table)
// ==============================================================================

void emit_table_pipe(StringBuf* sb, const ElementReader& elem, void* emitter_ctx);
void emit_table_rst(StringBuf* sb, const ElementReader& elem, void* emitter_ctx);
void emit_table_wiki(StringBuf* sb, const ElementReader& elem, void* emitter_ctx);
void emit_table_textile(StringBuf* sb, const ElementReader& elem, void* emitter_ctx);
void emit_table_org(StringBuf* sb, const ElementReader& elem, void* emitter_ctx);

// ==============================================================================
// Custom element handler callbacks (used in MarkupOutputRules.custom_element_handler)
// ==============================================================================

bool org_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem);
bool textile_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem);
// CommonMark block layer and inline overrides (format-md.cpp).
bool markdown_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem);

// Context-aware text escaping (used in MarkupOutputRules.escape_text).
void markdown_escape_text(StringBuf* sb, const char* s, size_t len);

#endif // FORMAT_MARKUP_H
