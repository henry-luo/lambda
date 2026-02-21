#ifndef FORMAT_MARKUP_H
#define FORMAT_MARKUP_H

#include "format-utils.h"

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

#endif // FORMAT_MARKUP_H
