#include "format-utils.h"
#include "format-markup.h"
#include "../../lib/str.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>



// ==============================================================================
// Common Text Processing
// ==============================================================================

// format raw text without any escaping, handling null strings
void format_raw_text_common(StringBuf* sb, String* str) {
    if (!sb || !str || str->len == 0) return;

    stringbuf_append_str_n(sb, str->chars, str->len);
}

// helper function to get markdown escape sequence
static const char* markdown_escape(char c) {
    switch (c) {
        case '*':
        case '_':
        case '`':
        case '#':
        case '[':
        case ']':
        case '(':
        case ')':
        case '\\':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// helper function to get wiki escape sequence
static const char* wiki_escape(char c) {
    switch (c) {
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// helper function to get rst escape sequence
static const char* rst_escape(char c) {
    switch (c) {
        case '*':
        case '`':
        case '_':
        case '\\':
        case '[':
        case ']':
        case '|':
            return NULL; // use backslash prefix
        default:
            return NULL;
    }
}

// predefined escape configurations
const TextEscapeConfig MARKDOWN_ESCAPE_CONFIG = {
    .chars_to_escape = "*_`#[]()\\",
    .use_backslash_escape = true,
    .escape_fn = markdown_escape
};

const TextEscapeConfig WIKI_ESCAPE_CONFIG = {
    .chars_to_escape = "[]{}|",
    .use_backslash_escape = true,
    .escape_fn = wiki_escape
};

const TextEscapeConfig RST_ESCAPE_CONFIG = {
    .chars_to_escape = "*`_\\[]|",
    .use_backslash_escape = true,
    .escape_fn = rst_escape
};

// format text with configurable escaping
void format_text_with_escape(StringBuf* sb, String* str, const TextEscapeConfig* config) {
    if (!sb || !str || str->len == 0 || !config) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        bool needs_escape = false;

        // check if character needs escaping
        if (config->chars_to_escape) {
            for (const char* p = config->chars_to_escape; *p; p++) {
                if (c == *p) {
                    needs_escape = true;
                    break;
                }
            }
        }

        if (needs_escape) {
            if (config->use_backslash_escape) {
                // use backslash escape
                stringbuf_append_char(sb, '\\');
                stringbuf_append_char(sb, c);
            } else if (config->escape_fn) {
                // use custom escape function
                const char* escaped = config->escape_fn(c);
                if (escaped) {
                    stringbuf_append_str(sb, escaped);
                } else {
                    // fallback to character itself
                    stringbuf_append_char(sb, c);
                }
            } else {
                // no escaping method specified
                stringbuf_append_char(sb, c);
            }
        } else {
            stringbuf_append_char(sb, c);
        }
    }
}

// ==============================================================================
// Element Child Iteration
// ==============================================================================

// process element children with custom text and item handlers
void format_element_children_with_processors(
    StringBuf* sb,
    const ElementReader& elem,
    TextProcessor text_proc,
    ItemProcessor item_proc
) {
    if (!sb) return;

    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            if (text_proc) {
                text_proc(sb, child.asString());
            }
        } else if (item_proc) {
            item_proc(sb, child);
        }
    }
}

// ==============================================================================
// HTML Entity Handling
// ==============================================================================

// check if position in string is start of HTML entity
bool is_html_entity(const char* str, size_t len, size_t pos, size_t* entity_end) {
    if (!str || pos >= len || str[pos] != '&') {
        return false;
    }

    size_t j = pos + 1;

    // check for numeric entity: &#123; or &#xAB;
    if (j < len && str[j] == '#') {
        j++;
        if (j < len && (str[j] == 'x' || str[j] == 'X')) {
            j++; // hex entity
            while (j < len && ((str[j] >= '0' && str[j] <= '9') ||
                               (str[j] >= 'a' && str[j] <= 'f') ||
                               (str[j] >= 'A' && str[j] <= 'F'))) {
                j++;
            }
        } else {
            // decimal entity
            while (j < len && str[j] >= '0' && str[j] <= '9') {
                j++;
            }
        }
        if (j < len && str[j] == ';') {
            if (entity_end) *entity_end = j;
            return true;
        }
    } else {
        // check for named entity: &nbsp; &lt; &gt; &frac12; etc.
        // entity names can contain letters and digits
        while (j < len && ((str[j] >= 'a' && str[j] <= 'z') ||
                           (str[j] >= 'A' && str[j] <= 'Z') ||
                           (str[j] >= '0' && str[j] <= '9'))) {
            j++;
        }
        if (j < len && str[j] == ';' && j > pos + 1) {
            if (entity_end) *entity_end = j;
            return true;
        }
    }

    return false;
}

// format string with HTML entity escaping (prevents double-encoding)
void format_html_string_safe(StringBuf* sb, String* str, bool is_attribute) {
    if (!sb || !str || !str->chars) return;

    const char* s = str->chars;
    size_t len = str->len;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];

        // check if this is an already-encoded entity (starts with & and ends with ;)
        // this prevents double-encoding of entities like &lt; -> &amp;lt;
        if (c == '&') {
            size_t entity_end = 0;

            if (is_html_entity(s, len, i, &entity_end)) {
                // copy the entire entity as-is (already encoded)
                while (i <= entity_end && i < len) {
                    stringbuf_append_char(sb, s[i]);
                    i++;
                }
                i--; // adjust because loop will increment
                continue;
            } else {
                // not an entity, encode the ampersand
                stringbuf_append_str(sb, "&amp;");
            }
        } else {
            switch (c) {
            case '<':
                stringbuf_append_str(sb, "&lt;");
                break;
            case '>':
                stringbuf_append_str(sb, "&gt;");
                break;
            case '"':
                // only encode quotes when inside attribute values
                if (is_attribute) {
                    stringbuf_append_str(sb, "&quot;");
                } else {
                    stringbuf_append_char(sb, '"');
                }
                break;
            case '\'':
                // apostrophes don't need to be encoded in text content (only in attributes)
                // for HTML5, apostrophes in text are safe and don't need encoding
                stringbuf_append_char(sb, '\'');
                break;
            default:
                // use unsigned char for comparison to handle UTF-8 multibyte sequences correctly
                // UTF-8 continuation bytes (0x80-0xBF) and start bytes (0xC0-0xF7) should pass through
                if ((unsigned char)c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                    // control characters - encode as numeric character reference
                    char hex_buf[10];
                    snprintf(hex_buf, sizeof(hex_buf), "&#x%02x;", (unsigned char)c);
                    stringbuf_append_str(sb, hex_buf);
                } else {
                    stringbuf_append_char(sb, c);
                }
                break;
            }
        }
    }
}

// ==============================================================================
// Table Processing Utilities
// ==============================================================================

// analyze table structure
TableInfo* analyze_table(Pool* pool, const ElementReader& table_elem) {
    if (!pool) return NULL;

    TableInfo* info = (TableInfo*)pool_alloc(pool, sizeof(TableInfo));
    if (!info) return NULL;

    info->pool = pool;
    info->row_count = 0;
    info->column_count = 0;
    info->has_header = false;
    info->alignments = NULL;

    // iterate through table sections (thead, tbody)
    auto section_it = table_elem.children();
    ItemReader section_item;

    while (section_it.next(&section_item)) {
        if (section_item.isElement()) {
            ElementReader section = section_item.asElement();
            const char* section_tag = section.tagName();

            if (!section_tag) continue;

            // check if this is a header section
            if (strcmp(section_tag, "thead") == 0) {
                info->has_header = true;
            }

            // count rows in this section
            auto row_it = section.children();
            ItemReader row_item;

            while (row_it.next(&row_item)) {
                if (row_item.isElement()) {
                    ElementReader row = row_item.asElement();
                    info->row_count++;

                    // count columns from first row
                    if (info->column_count == 0) {
                        auto cell_it = row.children();
                        ItemReader cell_item;
                        while (cell_it.next(&cell_item)) {
                            if (cell_item.isElement()) {
                                info->column_count++;
                            }
                        }
                    }
                }
            }
        }
    }

    // allocate alignment array (default to NONE)
    if (info->column_count > 0) {
        info->alignments = (TableAlignment*)pool_alloc(
            pool,
            sizeof(TableAlignment) * info->column_count
        );
        if (info->alignments) {
            for (int i = 0; i < info->column_count; i++) {
                info->alignments[i] = TABLE_ALIGN_NONE;
            }
        }
    }

    return info;
}

// free table info (currently no-op since pool-allocated)
void free_table_info(TableInfo* info) {
    // pool-allocated memory will be freed when pool is destroyed
    // this function exists for API completeness
    (void)info;
}

// iterate table rows with callback
void iterate_table_rows(
    const ElementReader& table_elem,
    StringBuf* sb,
    TableRowHandler handler,
    void* context
) {
    if (!handler) return;

    int row_idx = 0;

    // iterate through table sections (thead, tbody)
    auto section_it = table_elem.children();
    ItemReader section_item;

    while (section_it.next(&section_item)) {
        if (section_item.isElement()) {
            ElementReader section = section_item.asElement();
            const char* section_tag = section.tagName();

            if (!section_tag) continue;

            // handle direct <tr> children (no thead/tbody wrapper)
            if (strcmp(section_tag, "tr") == 0) {
                bool is_header = false;
                // detect header: check if first cell child is <th>
                auto cell_it = section.children();
                ItemReader first_cell;
                if (cell_it.next(&first_cell) && first_cell.isElement()) {
                    ElementReader fc = first_cell.asElement();
                    if (fc.tagName() && strcmp(fc.tagName(), "th") == 0) {
                        is_header = true;
                    }
                }
                handler(sb, section, row_idx, is_header, context);
                row_idx++;
                continue;
            }

            bool is_header = (strcmp(section_tag, "thead") == 0);

            // iterate rows in this section
            auto row_it = section.children();
            ItemReader row_item;

            while (row_it.next(&row_item)) {
                if (row_item.isElement()) {
                    ElementReader row = row_item.asElement();
                    handler(sb, row, row_idx, is_header, context);
                    row_idx++;
                }
            }
        }
    }
}

// ==============================================================================
// Heading Level Extraction
// ==============================================================================

int get_heading_level(const ElementReader& elem, int default_level) {
    const char* tag_name = elem.tagName();
    int level = default_level;

    // first try the "level" attribute (Pandoc / semantic schema)
    ItemReader level_attr = elem.get_attr("level");
    if (level_attr.isString()) {
        String* level_str = level_attr.asString();
        if (level_str && level_str->len > 0) {
            level = (int)str_to_int64_default(level_str->chars, strlen(level_str->chars), 0);
            if (level < 1) level = 1;
            if (level > 6) level = 6;
            return level;
        }
    }

    // fallback: parse hN tag name (h1, h2, ... h6)
    if (tag_name && strlen(tag_name) >= 2 && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        level = tag_name[1] - '0';
        if (level < 1) level = 1;
        if (level > 6) level = 6;
    }

    return level;
}

bool is_heading_tag(const char* tag_name) {
    if (!tag_name) return false;
    // check h1-h6
    if (strlen(tag_name) == 2 && tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') {
        return true;
    }
    // check semantic names
    if (strcmp(tag_name, "heading") == 0 || strcmp(tag_name, "header") == 0) {
        return true;
    }
    return false;
}

// ==============================================================================
// Table-Driven String Escaping
// ==============================================================================

void format_escaped_string(StringBuf* sb, const char* str, size_t len,
                           const EscapeRule* rules, int num_rules) {
    if (!str || len == 0) return;

    size_t flush_start = 0;

    for (size_t i = 0; i < len; i++) {
        char ch = str[i];
        const char* replacement = NULL;

        // linear scan is fine for small rule tables (typically 3-10 entries)
        for (int r = 0; r < num_rules; r++) {
            if (ch == rules[r].from) {
                replacement = rules[r].to;
                break;
            }
        }

        if (replacement) {
            // flush accumulated literal segment
            if (i > flush_start) {
                stringbuf_append_str_n(sb, str + flush_start, i - flush_start);
            }
            stringbuf_append_str(sb, replacement);
            flush_start = i + 1;
        }
    }

    // flush trailing segment
    if (flush_start < len) {
        stringbuf_append_str_n(sb, str + flush_start, len - flush_start);
    }
}

// ==============================================================================
// Predefined Escape Rule Tables
// ==============================================================================

const EscapeRule JSON_ESCAPE_RULES[] = {
    { '"',  "\\\"" },
    { '\\', "\\\\" },
    { '\n', "\\n"  },
    { '\r', "\\r"  },
    { '\t', "\\t"  },
    { '\b', "\\b"  },
    { '\f', "\\f"  },
};
const int JSON_ESCAPE_RULES_COUNT = sizeof(JSON_ESCAPE_RULES) / sizeof(JSON_ESCAPE_RULES[0]);

const EscapeRule XML_TEXT_ESCAPE_RULES[] = {
    { '<', "&lt;"   },
    { '>', "&gt;"   },
    { '&', "&amp;"  },
};
const int XML_TEXT_ESCAPE_RULES_COUNT = sizeof(XML_TEXT_ESCAPE_RULES) / sizeof(XML_TEXT_ESCAPE_RULES[0]);

const EscapeRule XML_ATTR_ESCAPE_RULES[] = {
    { '<',  "&lt;"    },
    { '>',  "&gt;"    },
    { '&',  "&amp;"   },
    { '"',  "&quot;"  },
    { '\'', "&apos;"  },
};
const int XML_ATTR_ESCAPE_RULES_COUNT = sizeof(XML_ATTR_ESCAPE_RULES) / sizeof(XML_ATTR_ESCAPE_RULES[0]);

const EscapeRule LATEX_ESCAPE_RULES[] = {
    { '#',  "\\#"  },
    { '$',  "\\$"  },
    { '&',  "\\&"  },
    { '%',  "\\%"  },
    { '_',  "\\_"  },
    { '{',  "\\{"  },
    { '}',  "\\}"  },
    { '^',  "\\^{}" },
    { '~',  "\\~{}" },
    { '\\', "\\textbackslash{}" },
};
const int LATEX_ESCAPE_RULES_COUNT = sizeof(LATEX_ESCAPE_RULES) / sizeof(LATEX_ESCAPE_RULES[0]);

const EscapeRule HTML_TEXT_ESCAPE_RULES[] = {
    { '<', "&lt;"   },
    { '>', "&gt;"   },
    { '&', "&amp;"  },
};
const int HTML_TEXT_ESCAPE_RULES_COUNT = sizeof(HTML_TEXT_ESCAPE_RULES) / sizeof(HTML_TEXT_ESCAPE_RULES[0]);

const EscapeRule HTML_ATTR_ESCAPE_RULES[] = {
    { '<',  "&lt;"    },
    { '>',  "&gt;"    },
    { '&',  "&amp;"   },
    { '"',  "&quot;"  },
    { '\'', "&#39;"   },
};
const int HTML_ATTR_ESCAPE_RULES_COUNT = sizeof(HTML_ATTR_ESCAPE_RULES) / sizeof(HTML_ATTR_ESCAPE_RULES[0]);

// ==============================================================================
// Unified Markup Output Rules — Link / Image Callbacks
// ==============================================================================

// Markdown: [text](url "title")
static void emit_link_markdown(StringBuf* sb, const char* url, const char* text, const char* title) {
    stringbuf_append_char(sb, '[');
    if (text) stringbuf_append_str(sb, text);
    stringbuf_append_str(sb, "](");
    if (url) stringbuf_append_str(sb, url);
    if (title && title[0] != '\0') {
        stringbuf_append_str(sb, " \"");
        stringbuf_append_str(sb, title);
        stringbuf_append_char(sb, '"');
    }
    stringbuf_append_char(sb, ')');
}

// RST: `text <url>`_
static void emit_link_rst(StringBuf* sb, const char* url, const char* text, const char* title) {
    (void)title;
    stringbuf_append_char(sb, '`');
    if (text) stringbuf_append_str(sb, text);
    if (url && url[0] != '\0') {
        stringbuf_append_str(sb, " <");
        stringbuf_append_str(sb, url);
        stringbuf_append_char(sb, '>');
    }
    stringbuf_append_str(sb, "`_");
}

// Org: [[url][text]]  or  [[url]]
static void emit_link_org(StringBuf* sb, const char* url, const char* text, const char* title) {
    (void)title;
    stringbuf_append_str(sb, "[[");
    if (url) stringbuf_append_str(sb, url);
    if (text && text[0] != '\0') {
        stringbuf_append_str(sb, "][");
        stringbuf_append_str(sb, text);
    }
    stringbuf_append_str(sb, "]]");
}

// Wiki: [url text]  (external) or  [[text]]  (internal/no-href)
static void emit_link_wiki(StringBuf* sb, const char* url, const char* text, const char* title) {
    if (url && url[0] != '\0') {
        // external link
        stringbuf_append_char(sb, '[');
        stringbuf_append_str(sb, url);
        if (text && text[0] != '\0') {
            stringbuf_append_char(sb, ' ');
            stringbuf_append_str(sb, text);
        } else if (title && title[0] != '\0') {
            stringbuf_append_char(sb, ' ');
            stringbuf_append_str(sb, title);
        }
        stringbuf_append_char(sb, ']');
    } else {
        // internal wiki link
        stringbuf_append_str(sb, "[[");
        if (text) stringbuf_append_str(sb, text);
        stringbuf_append_str(sb, "]]");
    }
}

// Textile: "text(title)":url  or  "text":url
static void emit_link_textile(StringBuf* sb, const char* url, const char* text, const char* title) {
    stringbuf_append_char(sb, '"');
    if (text) stringbuf_append_str(sb, text);
    if (title && title[0] != '\0') {
        stringbuf_append_char(sb, '(');
        stringbuf_append_str(sb, title);
        stringbuf_append_char(sb, ')');
    }
    stringbuf_append_str(sb, "\":");
    if (url) stringbuf_append_str(sb, url);
}

// Textile image: !url(alt)! or !url!
static void emit_image_textile(StringBuf* sb, const char* url, const char* alt) {
    stringbuf_append_char(sb, '!');
    if (url) stringbuf_append_str(sb, url);
    if (alt && alt[0] != '\0') {
        stringbuf_append_char(sb, '(');
        stringbuf_append_str(sb, alt);
        stringbuf_append_char(sb, ')');
    }
    stringbuf_append_char(sb, '!');
}

// Markdown image: ![alt](url)
static void emit_image_markdown(StringBuf* sb, const char* url, const char* alt) {
    stringbuf_append_str(sb, "![");
    if (alt) stringbuf_append_str(sb, alt);
    stringbuf_append_str(sb, "](");
    if (url) stringbuf_append_str(sb, url);
    stringbuf_append_char(sb, ')');
}

// ==============================================================================
// Unified Markup Output Rules — Rule Table Definitions
// ==============================================================================

const MarkupOutputRules MARKDOWN_RULES = {
    // heading
    .heading = {
        .type = MarkupOutputRules::HeadingStyle::PREFIX,
        .repeated_char = '#',
        .prefix = {NULL, NULL, NULL, NULL, NULL, NULL},
        .underline_chars = {0, 0, 0, 0, 0, 0},
    },
    // inline markup
    .inline_markup = {
        .bold_open   = "**",  .bold_close   = "**",
        .italic_open = "*",   .italic_close = "*",
        .code_open   = "`",   .code_close   = "`",
        .strikethrough_open  = "~~",  .strikethrough_close = "~~",
        .underline_open      = NULL,  .underline_close     = NULL,
        .superscript_open    = NULL,  .superscript_close   = NULL,
        .subscript_open      = NULL,  .subscript_close     = NULL,
        .verbatim_open       = NULL,  .verbatim_close      = NULL,
    },
    // tag names
    .tag_names = {
        .bold_tags      = {"strong", "b", NULL, NULL},
        .italic_tags    = {"em", "i", NULL, NULL},
        .code_tag       = "code",
        .strike_tags    = {"s", "del", "strike", NULL},
        .underline_tags = {NULL, NULL, NULL, NULL},
        .sup_tag        = NULL,
        .sub_tag        = NULL,
        .verbatim_tag   = NULL,
    },
    // links and images
    .emit_link  = emit_link_markdown,
    .emit_image = emit_image_markdown,
    // lists
    .list = {
        .unordered_marker      = "- ",
        .ordered_format        = "%d. ",
        .ordered_repeat_char   = 0,
        .unordered_repeat_char = 0,
        .use_depth_repetition  = false,
        .indent_spaces         = 2,
    },
    // code block
    .code_block = {
        .type          = MarkupOutputRules::CodeBlockStyle::FENCE,
        .open_prefix   = "```",
        .close_text    = "```\n",
        .lang_after_open = true,
        .lang_in_parens  = false,
    },
    // block-level
    .hr                         = "---\n\n",
    .paragraph_suffix           = "\n",
    .blockquote_open            = "> ",
    .blockquote_close           = "\n",
    .blockquote_prefix_each_line = true,
    // table
    .emit_table = emit_table_pipe,
    // escaping
    .escape_config = &MARKDOWN_ESCAPE_CONFIG,
    // custom handler
    .custom_element_handler = NULL,
    // container tags
    .container_tags = {"doc", "document", "body", "span", NULL, NULL, NULL, NULL},
    .skip_tags      = {"meta", NULL, NULL, NULL},
    // link tag
    .link_tag = "a",
};

const MarkupOutputRules RST_RULES = {
    // heading
    .heading = {
        .type = MarkupOutputRules::HeadingStyle::UNDERLINE,
        .repeated_char = 0,
        .prefix = {NULL, NULL, NULL, NULL, NULL, NULL},
        .underline_chars = {'=', '-', '~', '^', '"', '\''},
    },
    // inline markup
    .inline_markup = {
        .bold_open   = "**",  .bold_close   = "**",
        .italic_open = "*",   .italic_close = "*",
        .code_open   = "``",  .code_close   = "``",
        .strikethrough_open  = NULL,  .strikethrough_close = NULL,
        .underline_open      = NULL,  .underline_close     = NULL,
        .superscript_open    = NULL,  .superscript_close   = NULL,
        .subscript_open      = NULL,  .subscript_close     = NULL,
        .verbatim_open       = NULL,  .verbatim_close      = NULL,
    },
    // tag names
    .tag_names = {
        .bold_tags      = {"strong", "b", NULL, NULL},
        .italic_tags    = {"em", "i", NULL, NULL},
        .code_tag       = "code",
        .strike_tags    = {NULL, NULL, NULL, NULL},
        .underline_tags = {NULL, NULL, NULL, NULL},
        .sup_tag        = NULL,
        .sub_tag        = NULL,
        .verbatim_tag   = NULL,
    },
    // links and images
    .emit_link  = emit_link_rst,
    .emit_image = NULL,
    // lists
    .list = {
        .unordered_marker      = "- ",
        .ordered_format        = "%d. ",
        .ordered_repeat_char   = 0,
        .unordered_repeat_char = 0,
        .use_depth_repetition  = false,
        .indent_spaces         = 3,
    },
    // code block
    .code_block = {
        .type          = MarkupOutputRules::CodeBlockStyle::DIRECTIVE,
        .open_prefix   = ".. code-block:: ",
        .close_text    = "\n\n",
        .lang_after_open = true,
        .lang_in_parens  = false,
    },
    // block-level
    .hr                         = "----\n\n",
    .paragraph_suffix           = "\n\n",
    .blockquote_open            = NULL,
    .blockquote_close           = NULL,
    .blockquote_prefix_each_line = false,
    // table
    .emit_table = emit_table_rst,
    // escaping
    .escape_config = &RST_ESCAPE_CONFIG,
    // custom handler
    .custom_element_handler = NULL,
    // container tags
    .container_tags = {"doc", "document", "body", "span", NULL, NULL, NULL, NULL},
    .skip_tags      = {"meta", NULL, NULL, NULL},
    // link tag
    .link_tag = "a",
};

const MarkupOutputRules ORG_RULES = {
    // heading
    .heading = {
        .type = MarkupOutputRules::HeadingStyle::PREFIX,
        .repeated_char = '*',
        .prefix = {NULL, NULL, NULL, NULL, NULL, NULL},
        .underline_chars = {0, 0, 0, 0, 0, 0},
    },
    // inline markup
    .inline_markup = {
        .bold_open   = "*",   .bold_close   = "*",
        .italic_open = "/",   .italic_close = "/",
        .code_open   = "~",   .code_close   = "~",
        .strikethrough_open  = "+",   .strikethrough_close = "+",
        .underline_open      = "_",   .underline_close     = "_",
        .superscript_open    = NULL,  .superscript_close   = NULL,
        .subscript_open      = NULL,  .subscript_close     = NULL,
        .verbatim_open       = "=",   .verbatim_close      = "=",
    },
    // tag names
    .tag_names = {
        .bold_tags      = {"bold", "strong", "b", NULL},
        .italic_tags    = {"italic", "em", "i", NULL},
        .code_tag       = "code",
        .strike_tags    = {"strikethrough", "s", "del", "strike"},
        .underline_tags = {"underline", "u", "ins", NULL},
        .sup_tag        = NULL,
        .sub_tag        = NULL,
        .verbatim_tag   = "verbatim",
    },
    // links and images
    .emit_link  = emit_link_org,
    .emit_image = NULL,
    // lists
    .list = {
        .unordered_marker      = "- ",
        .ordered_format        = "%d. ",
        .ordered_repeat_char   = 0,
        .unordered_repeat_char = 0,
        .use_depth_repetition  = false,
        .indent_spaces         = 2,
    },
    // code block
    .code_block = {
        .type          = MarkupOutputRules::CodeBlockStyle::BEGIN_END,
        .open_prefix   = "#+BEGIN_SRC",
        .close_text    = "#+END_SRC\n",
        .lang_after_open = true,
        .lang_in_parens  = false,
    },
    // block-level
    .hr                         = "-----\n",
    .paragraph_suffix           = "\n",
    .blockquote_open            = "#+BEGIN_QUOTE\n",
    .blockquote_close           = "#+END_QUOTE\n",
    .blockquote_prefix_each_line = false,
    // table
    .emit_table = emit_table_org,
    // escaping
    .escape_config = NULL,
    // custom handler
    .custom_element_handler = org_custom_handler,
    // container tags
    .container_tags = {"text_content", NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    .skip_tags      = {NULL, NULL, NULL, NULL},
    // link tag
    .link_tag = "link",
};

const MarkupOutputRules WIKI_RULES = {
    // heading
    .heading = {
        .type = MarkupOutputRules::HeadingStyle::SURROUND,
        .repeated_char = '=',
        .prefix = {NULL, NULL, NULL, NULL, NULL, NULL},
        .underline_chars = {0, 0, 0, 0, 0, 0},
    },
    // inline markup
    .inline_markup = {
        .bold_open   = "'''",  .bold_close   = "'''",
        .italic_open = "''",   .italic_close = "''",
        .code_open   = "<code>", .code_close = "</code>",
        .strikethrough_open  = "<s>",    .strikethrough_close = "</s>",
        .underline_open      = "<u>",    .underline_close     = "</u>",
        .superscript_open    = "<sup>",  .superscript_close   = "</sup>",
        .subscript_open      = "<sub>",  .subscript_close     = "</sub>",
        .verbatim_open       = NULL,     .verbatim_close      = NULL,
    },
    // tag names
    .tag_names = {
        .bold_tags      = {"strong", "b", NULL, NULL},
        .italic_tags    = {"em", "i", NULL, NULL},
        .code_tag       = "code",
        .strike_tags    = {"s", "del", "strike", NULL},
        .underline_tags = {"u", "ins", NULL, NULL},
        .sup_tag        = "sup",
        .sub_tag        = "sub",
        .verbatim_tag   = NULL,
    },
    // links and images
    .emit_link  = emit_link_wiki,
    .emit_image = NULL,
    // lists
    .list = {
        .unordered_marker      = NULL,
        .ordered_format        = NULL,
        .ordered_repeat_char   = '#',
        .unordered_repeat_char = '*',
        .use_depth_repetition  = true,
        .indent_spaces         = 0,
    },
    // code block
    .code_block = {
        .type          = MarkupOutputRules::CodeBlockStyle::TAG,
        .open_prefix   = "<pre>",
        .close_text    = "</pre>\n\n",
        .lang_after_open = false,
        .lang_in_parens  = false,
    },
    // block-level
    .hr                         = "----\n\n",
    .paragraph_suffix           = "\n\n",
    .blockquote_open            = NULL,
    .blockquote_close           = NULL,
    .blockquote_prefix_each_line = false,
    // table
    .emit_table = emit_table_wiki,
    // escaping
    .escape_config = &WIKI_ESCAPE_CONFIG,
    // custom handler
    .custom_element_handler = NULL,
    // container tags
    .container_tags = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    .skip_tags      = {NULL, NULL, NULL, NULL},
    // link tag
    .link_tag = "a",
};

const MarkupOutputRules TEXTILE_RULES = {
    // heading
    .heading = {
        .type = MarkupOutputRules::HeadingStyle::INDEXED_PREFIX,
        .repeated_char = 0,
        .prefix = {"h1. ", "h2. ", "h3. ", "h4. ", "h5. ", "h6. "},
        .underline_chars = {0, 0, 0, 0, 0, 0},
    },
    // inline markup
    .inline_markup = {
        .bold_open   = "*",   .bold_close   = "*",
        .italic_open = "_",   .italic_close = "_",
        .code_open   = "@",   .code_close   = "@",
        .strikethrough_open  = "-",   .strikethrough_close = "-",
        .underline_open      = "+",   .underline_close     = "+",
        .superscript_open    = "^",   .superscript_close   = "^",
        .subscript_open      = "~",   .subscript_close     = "~",
        .verbatim_open       = NULL,  .verbatim_close      = NULL,
    },
    // tag names
    .tag_names = {
        .bold_tags      = {"strong", "b", NULL, NULL},
        .italic_tags    = {"em", "i", NULL, NULL},
        .code_tag       = "code",
        .strike_tags    = {"s", "del", "strike", NULL},
        .underline_tags = {"u", "ins", NULL, NULL},
        .sup_tag        = "sup",
        .sub_tag        = "sub",
        .verbatim_tag   = NULL,
    },
    // links and images
    .emit_link  = emit_link_textile,
    .emit_image = emit_image_textile,
    // lists
    .list = {
        .unordered_marker      = NULL,
        .ordered_format        = NULL,
        .ordered_repeat_char   = '#',
        .unordered_repeat_char = '*',
        .use_depth_repetition  = true,
        .indent_spaces         = 0,
    },
    // code block
    .code_block = {
        .type          = MarkupOutputRules::CodeBlockStyle::DOT_PREFIX,
        .open_prefix   = "bc.",
        .close_text    = "\n\n",
        .lang_after_open = false,
        .lang_in_parens  = true,
    },
    // block-level
    .hr                         = "\n---\n\n",
    .paragraph_suffix           = "\n\n",
    .blockquote_open            = "bq. ",
    .blockquote_close           = "\n\n",
    .blockquote_prefix_each_line = false,
    // table
    .emit_table = emit_table_textile,
    // escaping
    .escape_config = NULL,
    // custom handler
    .custom_element_handler = textile_custom_handler,
    // container tags
    .container_tags = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    .skip_tags      = {NULL, NULL, NULL, NULL},
    // link tag
    .link_tag = "a",
};

// lookup rules by format name
const MarkupOutputRules* get_markup_rules(const char* format_name) {
    if (!format_name) return NULL;
    if (strcmp(format_name, "markdown") == 0 || strcmp(format_name, "md") == 0) return &MARKDOWN_RULES;
    if (strcmp(format_name, "rst") == 0 || strcmp(format_name, "restructuredtext") == 0) return &RST_RULES;
    if (strcmp(format_name, "org") == 0 || strcmp(format_name, "orgmode") == 0) return &ORG_RULES;
    if (strcmp(format_name, "wiki") == 0 || strcmp(format_name, "mediawiki") == 0) return &WIKI_RULES;
    if (strcmp(format_name, "textile") == 0) return &TEXTILE_RULES;
    return NULL;
}
