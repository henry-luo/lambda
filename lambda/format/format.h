#ifndef FORMAT_H
#define FORMAT_H

#define LAMBDA_STATIC
#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"

// Common function declarations
Item create_item_from_field_data(void* field_data, TypeId type_id);
void format_number(StringBuf* sb, Item item);

// Format function declarations
String* format_json(VariableMemPool* pool, Item root_item);
void format_json_to_strbuf(StringBuf* sb, Item root_item);
void format_markdown(StringBuf* sb, Item root_item);
String* format_xml(VariableMemPool* pool, Item root_item);
String* format_html(VariableMemPool* pool, Item root_item);
String* format_yaml(VariableMemPool* pool, Item root_item);
String* format_toml(VariableMemPool* pool, Item root_item);
String* format_ini(VariableMemPool* pool, Item root_item);
String* format_properties(VariableMemPool* pool, Item root_item);
String* format_css(VariableMemPool* pool, Item root_item);
String* format_jsx(VariableMemPool* pool, Item root_item);
String* format_latex(VariableMemPool* pool, Item root_item);
void format_rst(StringBuf* sb, Item root_item);
String* format_rst_string(VariableMemPool* pool, Item root_item);
void format_org(StringBuf* sb, Item root_item);
String* format_org_string(VariableMemPool* pool, Item root_item);
void format_wiki(StringBuf* sb, Item root_item);
String* format_wiki_string(VariableMemPool* pool, Item root_item);
void format_text(StringBuf* sb, Item root_item);
String* format_text_string(VariableMemPool* pool, Item root_item);

// Math format function declarations
String* format_math(VariableMemPool* pool, Item root_item);
String* format_math_latex(VariableMemPool* pool, Item root_item);
String* format_math_typst(VariableMemPool* pool, Item root_item);
String* format_math_ascii(VariableMemPool* pool, Item root_item);
String* format_math_mathml(VariableMemPool* pool, Item root_item);

// Standalone ASCII math formatter
String* format_math_ascii_standalone(VariableMemPool* pool, Item root_item);

#endif // FORMAT_H
