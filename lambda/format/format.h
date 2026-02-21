#ifndef FORMAT_H
#define FORMAT_H

#define LAMBDA_STATIC
#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"

#ifdef __cplusplus
#include <string>
#endif

// Common function declarations
Item create_item_from_field_data(void* field_data, TypeId type_id);
void format_number(StringBuf* sb, Item item);

// Format function declarations — all return String* (canonical public API)
String* format_json(Pool* pool, const Item root_item);
String* format_xml(Pool* pool, Item root_item);
String* format_html(Pool* pool, Item root_item);
String* format_yaml(Pool* pool, Item root_item);
String* format_toml(Pool* pool, Item root_item);
String* format_ini(Pool* pool, Item root_item);
String* format_properties(Pool* pool, Item root_item);
String* format_css(Pool* pool, Item root_item);
String* format_jsx(Pool* pool, Item root_item);
String* format_mdx(Pool* pool, Item root_item);
String* format_latex(Pool* pool, Item root_item);
String* format_markdown_string(Pool* pool, Item root_item);
String* format_rst_string(Pool* pool, Item root_item);
String* format_org_string(Pool* pool, Item root_item);
String* format_wiki_string(Pool* pool, Item root_item);
String* format_textile_string(Pool* pool, Item root_item);
String* format_text_string(Pool* pool, Item root_item);

// StringBuf variants (internal — prefer String* versions above)
void format_json_to_strbuf(StringBuf* sb, Item root_item);
void format_markdown(StringBuf* sb, Item root_item);
void format_rst(StringBuf* sb, Item root_item);
void format_org(StringBuf* sb, Item root_item);
void format_wiki(StringBuf* sb, Item root_item);
void format_textile(StringBuf* sb, Item root_item);
void format_text(StringBuf* sb, Item root_item);

// Graph format function declarations
String* format_graph(Pool* pool, Item root_item);
String* format_graph_with_flavor(Pool* pool, Item root_item, const char* flavor);

// Math format function declarations
String* format_math(Pool* pool, Item root_item);
String* format_math_latex(Pool* pool, Item root_item);
String* format_math_typst(Pool* pool, Item root_item);
String* format_math_ascii(Pool* pool, Item root_item);
String* format_math_mathml(Pool* pool, Item root_item);

// Standalone ASCII math formatter
String* format_math_ascii_standalone(Pool* pool, Item root_item);

#endif // FORMAT_H
