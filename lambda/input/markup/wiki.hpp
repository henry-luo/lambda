#ifndef LAMBDA_MARKUP_WIKI_HPP
#define LAMBDA_MARKUP_WIKI_HPP

#include "markup_parser.hpp"

namespace lambda {
namespace markup {

enum class WikiHeading { EQUALS, INVERSE_EQUALS, BANG, PLUS, DOT };
enum class WikiLinks { DOUBLE_BRACKET, TIKI, TWIKI, XWIKI, ATLASSIAN };

struct WikiSyntax {
    Flavor flavor;
    const char* name;
    WikiHeading headings;
    WikiLinks links;
    bool repeated_lists;
    char escape;
    const DelimiterSpec* emphasis;
    size_t emphasis_count;
};

const WikiSyntax* wiki_syntax(Flavor flavor);
const WikiSyntax* wiki_syntax(const char* name);
bool wiki_is_mediawiki(const MarkupParser* parser);
bool wiki_is_atlassian(Flavor flavor);
const char* wiki_slice(MarkupParser* parser, const char* begin, size_t length);
Item wiki_text(MarkupParser* parser, const char* begin, size_t length);
Item wiki_raw(MarkupParser* parser, const char* begin, size_t length, const char* kind);
Item parse_wiki_inlines(MarkupParser* parser, const char* text);
Item parse_wiki_block(MarkupParser* parser);
const char* wiki_cell_boundary(const char* begin, const char* end, char separator, Flavor flavor);
void wiki_append_table_row(MarkupParser* parser, Element* head, Element* body, Element* row, bool header);
Item parse_mediawiki_table(MarkupParser* parser, const char* line);
void wiki_append_fragment(MarkupParser* parser, Element* parent,
    const char* source, size_t length, bool blocks);
void wiki_record_tag(MarkupParser* parser, const char* tag, size_t length);

struct WikiMacro {
    const char* name = nullptr;
    size_t name_length = 0;
    const char* arguments = nullptr;
    size_t arguments_length = 0;
    const char* header_end = nullptr;
    const char* close = nullptr;
};
bool wiki_macro_header(MarkupParser* parser, const char* source, WikiMacro* macro);
struct WikiParameter {
    const char* key = nullptr;
    size_t key_length = 0;
    const char* value = nullptr;
    size_t value_length = 0;
};
bool wiki_next_parameter(const char** cursor, const char* end, WikiParameter* parameter, bool pipe_separated = false);
const char* wiki_macro_parameter(MarkupParser* parser, const WikiMacro& macro,
    const char* key);
const char* wiki_macro_body_end(MarkupParser* parser, const WikiMacro& macro);
const char* wiki_find_close(const char* text, const char* close, char escape);
Item parse_wiki_macro(MarkupParser* parser, const char** source, bool blocks);
void apply_task_attributes(MarkupParser* parser, Element* item, const ListItemInfo& info);
void append_task_checkbox(MarkupParser* parser, Element* item, const ListItemInfo& info);

} // namespace markup
} // namespace lambda
#endif
