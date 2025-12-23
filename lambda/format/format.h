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

// Format function declarations
String* format_json(Pool* pool, const Item root_item);
void format_json_to_strbuf(StringBuf* sb, Item root_item);
void format_markdown(StringBuf* sb, Item root_item);
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

// LaTeX to HTML formatter (v2) and full document formatter (v3.1)
#ifdef __cplusplus
namespace lambda {
    // Generate HTML fragment from LaTeX input
    Item format_latex_html_v2(struct Input* input, bool text_mode);
    
    // Generate complete HTML document with CSS and fonts
    // doc_class: document class (e.g., "article", "book") - defaults to "article"
    // asset_base_url: base URL for external CSS/font files, or NULL for embedded
    // embed_css: if true and asset_base_url is NULL, embed CSS inline
    std::string format_latex_html_v2_document(struct Input* input, const char* doc_class,
                                               const char* asset_base_url, bool embed_css);
}
extern "C" {
#endif

// C API wrappers for LaTeX to HTML
Item format_latex_html_v2_c(struct Input* input, int text_mode);
const char* format_latex_html_v2_document_c(struct Input* input, const char* doc_class,
                                             const char* asset_base_url, int embed_css);

#ifdef __cplusplus
}
#endif

void format_rst(StringBuf* sb, Item root_item);
String* format_rst_string(Pool* pool, Item root_item);
void format_org(StringBuf* sb, Item root_item);
String* format_org_string(Pool* pool, Item root_item);
void format_wiki(StringBuf* sb, Item root_item);
String* format_wiki_string(Pool* pool, Item root_item);
void format_text(StringBuf* sb, Item root_item);
String* format_text_string(Pool* pool, Item root_item);

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
