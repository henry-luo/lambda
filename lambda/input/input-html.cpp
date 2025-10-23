#include "input.h"

static Item parse_element(Input *input, const char **html, const char *html_start);

#include "input.h"
#include <stdarg.h>

static Item parse_element(Input *input, const char **html, const char *html_start);

// Position tracking helper functions
static void get_line_col(const char *html_start, const char *current, int *line, int *col) {
    *line = 1;
    *col = 1;

    for (const char *p = html_start; p < current; p++) {
        if (*p == '\n') {
            (*line)++;
            *col = 1;
        } else {
            (*col)++;
        }
    }
}

static void log_parse_error(const char *html_start, const char *current, const char *format, ...) {
    int line, col;
    get_line_col(html_start, current, &line, &col);

    char msg[512];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    log_error("HTML parse error at line %d, column %d: %s", line, col, msg);
}

// HTML5 entity definitions for better compatibility
typedef struct {
    const char* name;
    const char* utf8;
} HTMLEntity;

static const HTMLEntity html_entities[] = {
    // Basic HTML entities
    {"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"quot", "\""}, {"apos", "'"},
    {"nbsp", " "}, {"copy", "©"}, {"reg", "®"}, {"trade", "™"},

    // Currency symbols
    {"euro", "€"}, {"pound", "£"}, {"yen", "¥"}, {"cent", "¢"}, {"dollar", "$"},

    // Mathematical symbols
    {"times", "×"}, {"divide", "÷"}, {"plusmn", "±"}, {"minus", "−"},
    {"sup2", "²"}, {"sup3", "³"}, {"frac14", "¼"}, {"frac12", "½"}, {"frac34", "¾"},

    // Arrows
    {"larr", "←"}, {"uarr", "↑"}, {"rarr", "→"}, {"darr", "↓"},
    {"harr", "↔"}, {"crarr", "↵"},

    // Greek letters (common ones)
    {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"},
    {"epsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"},
    {"pi", "π"}, {"sigma", "σ"}, {"tau", "τ"}, {"phi", "φ"},
    {"chi", "χ"}, {"psi", "ψ"}, {"omega", "ω"},

    // Accented characters (common ones)
    {"agrave", "à"}, {"aacute", "á"}, {"acirc", "â"}, {"atilde", "ã"},
    {"auml", "ä"}, {"aring", "å"}, {"ccedil", "ç"}, {"egrave", "è"},
    {"eacute", "é"}, {"ecirc", "ê"}, {"euml", "ë"}, {"igrave", "ì"},
    {"iacute", "í"}, {"icirc", "î"}, {"iuml", "ï"}, {"ntilde", "ñ"},
    {"ograve", "ò"}, {"oacute", "ó"}, {"ocirc", "ô"}, {"otilde", "õ"},
    {"ouml", "ö"}, {"ugrave", "ù"}, {"uacute", "ú"}, {"ucirc", "û"},
    {"uuml", "ü"}, {"yuml", "ÿ"},

    // Quotation marks
    {"lsquo", "'"}, {"rsquo", "'"}, {"ldquo", "\""}, {"rdquo", "\""},
    {"sbquo", "‚"}, {"bdquo", "„"},

    // Miscellaneous
    {"sect", "§"}, {"para", "¶"}, {"middot", "·"}, {"cedil", "¸"},
    {"ordm", "º"}, {"ordf", "ª"}, {"laquo", "«"}, {"raquo", "»"},
    {"iquest", "¿"}, {"iexcl", "¡"}, {"brvbar", "¦"}, {"shy", "­"},
    {"macr", "¯"}, {"deg", "°"}, {"acute", "´"}, {"micro", "µ"},
    {"not", "¬"}, {"curren", "¤"},

    {NULL, NULL} // Sentinel
};

static const char* find_html_entity(const char* name, size_t len) {
    for (int i = 0; html_entities[i].name; i++) {
        if (strlen(html_entities[i].name) == len &&
            strncmp(html_entities[i].name, name, len) == 0) {
            return html_entities[i].utf8;
        }
    }
    return NULL;
}

static void skip_whitespace(const char **html) {
    int whitespace_count = 0;
    const int max_whitespace = 1000; // Safety limit

    while (**html && (**html == ' ' || **html == '\n' || **html == '\r' || **html == '\t') &&
           whitespace_count < max_whitespace) {
        (*html)++;
        whitespace_count++;
    }

    if (whitespace_count >= max_whitespace) {
        log_warn("Hit whitespace limit, possible infinite loop in skip_whitespace");
    }
}

// HTML5 void elements (self-closing tags)
static const char* void_elements[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", "command",
    "keygen", "menuitem", "slot", NULL
};

// HTML5 semantic elements that should be parsed as containers
static const char* semantic_elements[] = {
    "article", "aside", "details", "figcaption", "figure", "footer",
    "header", "main", "mark", "nav", "section", "summary", "time",
    "audio", "video", "canvas", "svg", "math", "datalist", "dialog",
    "meter", "output", "progress", "template", "search", "hgroup",
    NULL
};

// HTML5 elements that contain raw text (like script, style)
static const char* raw_text_elements[] = {
    "script", "style", "textarea", "title", "xmp", "iframe", "noembed",
    "noframes", "noscript", "plaintext", NULL
};

// HTML5 elements that should preserve whitespace
static const char* preformatted_elements[] = {
    "pre", "code", "kbd", "samp", "var", "listing", "xmp", "plaintext", NULL
};

// HTML5 block-level elements
static const char* block_elements[] = {
    "address", "article", "aside", "blockquote", "details", "dialog", "dd", "div",
    "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
    "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "li", "main", "nav", "ol",
    "p", "pre", "section", "table", "ul", "canvas", "audio", "video", NULL
};

// HTML5 inline elements
static const char* inline_elements[] = {
    "a", "abbr", "acronym", "b", "bdi", "bdo", "big", "br", "button", "cite",
    "code", "dfn", "em", "i", "img", "input", "kbd", "label", "map", "mark",
    "meter", "noscript", "object", "output", "progress", "q", "ruby", "s",
    "samp", "script", "select", "small", "span", "strong", "sub", "sup",
    "textarea", "time", "tt", "u", "var", "wbr", NULL
};

static bool is_semantic_element(const char* tag_name) {
    for (int i = 0; semantic_elements[i]; i++) {
        if (strcasecmp(tag_name, semantic_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_void_element(const char* tag_name) {
    for (int i = 0; void_elements[i]; i++) {
        if (strcasecmp(tag_name, void_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_raw_text_element(const char* tag_name) {
    for (int i = 0; raw_text_elements[i]; i++) {
        if (strcasecmp(tag_name, raw_text_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_preformatted_element(const char* tag_name) {
    for (int i = 0; preformatted_elements[i]; i++) {
        if (strcasecmp(tag_name, preformatted_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_block_element(const char* tag_name) {
    for (int i = 0; block_elements[i]; i++) {
        if (strcasecmp(tag_name, block_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_inline_element(const char* tag_name) {
    for (int i = 0; inline_elements[i]; i++) {
        if (strcasecmp(tag_name, inline_elements[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void to_lowercase(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

static String* parse_string_content(Input *input, const char **html, char end_char) {
    StringBuf* sb = input->sb;
    int char_count = 0;
    const int max_string_chars = 10000; // Safety limit

    // Handle empty string case - if we immediately encounter the end_char, just return empty string
    if (**html == end_char) {
        return stringbuf_to_string(sb);
    }

    while (**html && **html != end_char && char_count < max_string_chars) {
        if (**html == '&') {
            (*html)++; // Skip &

            if (*html[0] == '#') {
                // Numeric character reference
                (*html)++; // Skip #
                int code = 0;
                bool hex = false;

                if (**html == 'x' || **html == 'X') {
                    hex = true;
                    (*html)++;
                }

                while (**html && **html != ';') {
                    if (hex) {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 16 + (**html - '0');
                        } else if (**html >= 'a' && **html <= 'f') {
                            code = code * 16 + (**html - 'a' + 10);
                        } else if (**html >= 'A' && **html <= 'F') {
                            code = code * 16 + (**html - 'A' + 10);
                        } else {
                            break;
                        }
                    } else {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 10 + (**html - '0');
                        } else {
                            break;
                        }
                    }
                    (*html)++;
                }

                if (**html == ';') {
                    (*html)++;
                    // Convert Unicode code point to UTF-8
                    if (code < 0x80) {
                        stringbuf_append_char(sb, (char)code);
                    } else if (code < 0x800) {
                        stringbuf_append_char(sb, (char)(0xC0 | (code >> 6)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x10000) {
                        stringbuf_append_char(sb, (char)(0xE0 | (code >> 12)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x110000) {
                        // 4-byte UTF-8 encoding for code points up to U+10FFFF
                        stringbuf_append_char(sb, (char)(0xF0 | (code >> 18)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 12) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else {
                        stringbuf_append_char(sb, '?'); // Invalid code point
                    }
                } else {
                    stringbuf_append_char(sb, '&');
                    stringbuf_append_char(sb, '#');
                }
            } else {
                // Named entity reference
                const char *entity_start = *html;
                const char *entity_end = *html;

                // Find the end of the entity name
                while (*entity_end && *entity_end != ';' && *entity_end != ' ' &&
                       *entity_end != '<' && *entity_end != '&') {
                    entity_end++;
                }

                if (*entity_end == ';') {
                    size_t entity_len = entity_end - entity_start;
                    const char* entity_value = find_html_entity(entity_start, entity_len);

                    if (entity_value) {
                        stringbuf_append_str(sb, entity_value);
                        *html = entity_end + 1; // Skip past the semicolon
                    } else {
                        // Unknown entity, preserve as-is for round-trip compatibility
                        stringbuf_append_char(sb, '&');
                        for (const char* p = entity_start; p < entity_end; p++) {
                            stringbuf_append_char(sb, *p);
                        }
                        stringbuf_append_char(sb, ';');
                        *html = entity_end + 1;
                    }
                } else {
                    // Invalid entity format, just append the &
                    stringbuf_append_char(sb, '&');
                }
            }
        } else {
            stringbuf_append_char(sb, **html);
            (*html)++;
        }
        char_count++;
    }

    return stringbuf_to_string(sb);
}

static String* parse_attribute_value(Input *input, const char **html, const char *html_start) {
    skip_whitespace(html);

    log_debug("Parsing attr value at char: %d, '%c'", (int)(*html - html_start), **html);
    if (**html == '"') {
        (*html)++; // Skip opening quote

        stringbuf_reset(input->sb); // Reset buffer before parsing quoted content
        String* value = parse_string_content(input, html, '"');

        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '"') {
            (*html)++; // Skip closing quote
        }

        return value;
    } else if (**html == '\'') {
        (*html)++; // Skip opening quote

        stringbuf_reset(input->sb); // Reset buffer before parsing quoted content
        String* value = parse_string_content(input, html, '\'');

        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '\'') {
            (*html)++; // Skip closing quote
        }

        return value;
    } else {
        // Unquoted attribute value
        StringBuf* sb = input->sb;
        stringbuf_reset(sb); // Reset buffer before parsing unquoted value
        int char_count = 0;
        const int max_unquoted_chars = 10000; // Safety limit

        while (**html && **html != ' ' && **html != '\t' && **html != '\n' &&
               **html != '\r' && **html != '>' && **html != '/' && **html != '=' &&
               char_count < max_unquoted_chars) {
            stringbuf_append_char(sb, **html);
            (*html)++;
            char_count++;
        }

        if (char_count >= max_unquoted_chars) {
            log_warn("Hit unquoted attribute value limit (%d)", max_unquoted_chars);
        }

        return stringbuf_to_string(sb);
    }
}

static bool parse_attributes(Input *input, Element *element, const char **html, const char *html_start) {
    skip_whitespace(html);

    int attr_count = 0;
    const int max_attributes = 500; // Safety limit
    log_debug("Parsing attributes at char: %d, '%c'", (int)(*html - html_start), **html);

    while (**html && **html != '>' && **html != '/' && attr_count < max_attributes) {
        attr_count++;
        log_debug("Parsing attribute %d, at char: %d, '%c'", attr_count, (int)(*html - html_start), **html);

        // Parse attribute name
        StringBuf* sb = input->sb;
        stringbuf_reset(sb); // Reset buffer before parsing attribute name
        const char* attr_start = *html;
        const char* name_start = *html;

        while (**html && **html != '=' && **html != ' ' && **html != '\t' &&
            **html != '\n' && **html != '\r' && **html != '>' && **html != '/') {
            stringbuf_append_char(sb, tolower(**html));
            (*html)++;
        }

        if (!sb->length) { // No attribute name found
            log_error("No attribute name found at char: %d, '%c'", (int)(*html - html_start), **html);
            break;
        }

        String *attr_name = stringbuf_to_string(sb);
        skip_whitespace(html);

        String* attr_value;
        if (**html == '=') {
            (*html)++; // Skip =
            skip_whitespace(html); // Skip whitespace after =
            attr_value = parse_attribute_value(input, html, html_start);
        } else {
            // Boolean attribute (no value)
            attr_value = NULL;
        }

        // Double-check that attr_value is not NULL before using it
        if (attr_value) {
            Item value = {.item = s2it(attr_value)};
            elmt_put(element, attr_name, value, input->pool);
        }

        skip_whitespace(html);
    }

    if (attr_count >= max_attributes) {
        log_error("Hit attribute limit (%d), possible infinite loop", max_attributes);
    }

    return true;
}

static String* parse_tag_name(Input *input, const char **html) {
    StringBuf* sb = input->sb;
    while (**html && **html != ' ' && **html != '\t' && **html != '\n' &&
           **html != '\r' && **html != '>' && **html != '/') {
        stringbuf_append_char(sb, tolower(**html));
        (*html)++;
    }
    return stringbuf_to_string(sb);
}

// Parse HTML comment and return it as an element with tag name "!--"
static Item parse_comment(Input* input, const char **html, const char* html_start) {
    if (strncmp(*html, "<!--", 4) != 0) {
        return {.item = ITEM_ERROR};
    }

    *html += 4; // Skip <!--
    const char* comment_start = *html;

    // Find end of comment
    while (**html && strncmp(*html, "-->", 3) != 0) {
        (*html)++;
    }

    if (!**html) {
        log_parse_error(html_start, *html, "Unclosed HTML comment");
        return {.item = ITEM_ERROR};
    }

    // Extract comment content (preserve all whitespace)
    size_t comment_len = *html - comment_start;

    // Create element with tag name "!--"
    Element* element = input_create_element(input, "!--");
    if (!element) {
        return {.item = ITEM_ERROR};
    }

    // Add comment content as a text node child (if not empty)
    if (comment_len > 0) {
        StringBuf* sb = input->sb;
        stringbuf_reset(sb);
        for (size_t i = 0; i < comment_len; i++) {
            stringbuf_append_char(sb, comment_start[i]);
        }
        String* comment_text = stringbuf_to_string(sb);
        Item text_item = {.item = s2it(comment_text)};
        list_push((List*)element, text_item);
    }

    // Set content length
    ((TypeElmt*)element->type)->content_length = ((List*)element)->length;

    *html += 3; // Skip -->

    return {.element = element};
}

// Parse DOCTYPE declaration and return it as an element with tag name "!DOCTYPE" or "!doctype"
static Item parse_doctype(Input* input, const char **html, const char* html_start) {
    if (strncasecmp(*html, "<!doctype", 9) != 0) {
        return {.item = ITEM_ERROR};
    }

    // Preserve the case of "doctype" from source
    const char* doctype_start = *html + 2; // After "<!"
    bool is_uppercase_DOCTYPE = (doctype_start[0] == 'D');

    *html += 9; // Skip "<!doctype" or "<!DOCTYPE"

    // Skip whitespace after doctype
    while (**html && isspace(**html)) {
        (*html)++;
    }

    const char* content_start = *html;

    // Find end of doctype declaration
    while (**html && **html != '>') {
        (*html)++;
    }

    if (!**html) {
        log_parse_error(html_start, *html, "Unclosed DOCTYPE declaration");
        return {.item = ITEM_ERROR};
    }

    // Extract DOCTYPE content (e.g., "html" or "html PUBLIC ...")
    size_t content_len = *html - content_start;

    // Create element with tag name "!DOCTYPE" or "!doctype" to preserve source case
    Element* element = input_create_element(input, is_uppercase_DOCTYPE ? "!DOCTYPE" : "!doctype");
    if (!element) {
        return {.item = ITEM_ERROR};
    }

    // Add DOCTYPE content as a text node child (if not empty)
    if (content_len > 0) {
        StringBuf* sb = input->sb;
        stringbuf_reset(sb);
        for (size_t i = 0; i < content_len; i++) {
            stringbuf_append_char(sb, content_start[i]);
        }
        String* doctype_text = stringbuf_to_string(sb);
        Item text_item = {.item = s2it(doctype_text)};
        list_push((List*)element, text_item);
    }

    // Set content length
    ((TypeElmt*)element->type)->content_length = ((List*)element)->length;

    *html += 1; // Skip '>'

    return {.element = element};
}

static void skip_doctype(const char **html) {
    if (strncasecmp(*html, "<!doctype", 9) == 0) {
        while (**html && **html != '>') {
            (*html)++;
        }
        if (**html) (*html)++; // Skip >
    }
}

static void skip_processing_instruction(const char **html) {
    if (strncmp(*html, "<?", 2) == 0) {
        *html += 2;
        while (**html && strncmp(*html, "?>", 2) != 0) {
            (*html)++;
        }
        if (**html) *html += 2; // Skip ?>
    }
}

static void skip_cdata(const char **html) {
    if (strncmp(*html, "<![CDATA[", 9) == 0) {
        *html += 9;
        while (**html && strncmp(*html, "]]>", 3) != 0) {
            (*html)++;
        }
        if (**html) *html += 3; // Skip ]]>
    }
}

// HTML5 custom element validation (simplified)
static bool is_valid_custom_element_name(const char* name) {
    if (!name || strlen(name) == 0) return false;

    // Custom elements must contain a hyphen and start with a lowercase letter
    bool has_hyphen = false;
    if (name[0] < 'a' || name[0] > 'z') return false;

    for (int i = 1; name[i]; i++) {
        if (name[i] == '-') {
            has_hyphen = true;
        } else if (!((name[i] >= 'a' && name[i] <= 'z') ||
                     (name[i] >= '0' && name[i] <= '9') ||
                     name[i] == '-' || name[i] == '.' || name[i] == '_')) {
            return false;
        }
    }

    return has_hyphen;
}

// Check if attribute is a data attribute (HTML5 feature)
static bool is_data_attribute(const char* attr_name) {
    return strncmp(attr_name, "data-", 5) == 0;
}

// Check if attribute is an ARIA attribute (accessibility)
static bool is_aria_attribute(const char* attr_name) {
    return strncmp(attr_name, "aria-", 5) == 0;
}

static Item parse_element(Input *input, const char **html, const char *html_start) {
    static int parse_depth = 0;
    parse_depth++;

    if (**html != '<') {
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected character '%c' at beginning of element", **html);
        return {.item = ITEM_ERROR};
    }

    // Parse comments as special elements
    if (strncmp(*html, "<!--", 4) == 0) {
        Item comment = parse_comment(input, html, html_start);
        parse_depth--;
        return comment;
    }

    // Skip DOCTYPE
    if (strncasecmp(*html, "<!doctype", 9) == 0) {
        skip_doctype(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html, html_start); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input after doctype");
        return {.item = ITEM_NULL};
    }

    // Skip processing instructions
    if (strncmp(*html, "<?", 2) == 0) {
        skip_processing_instruction(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html, html_start); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input after processing instruction");
        return {.item = ITEM_NULL};
    }

    // Skip CDATA
    if (strncmp(*html, "<![CDATA[", 9) == 0) {
        skip_cdata(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html, html_start); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input after cdata");
        return {.item = ITEM_NULL};
    }

    log_debug("Parsing element at depth %d, at char: %d, '%c'",
        parse_depth, (int)(*html - html_start), **html);
    (*html)++; // Skip <

    // Check for closing tag
    if (**html == '/') {
        // This is a closing tag, skip it and return null
        while (**html && **html != '>') {
            (*html)++;
        }
        if (**html) (*html)++; // Skip >
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input after end tag");
        return {.item = ITEM_NULL};
    }

    String* tag_name = parse_tag_name(input, html);
    if (!tag_name || tag_name->len == 0) {
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input after start tag");
        return {.item = ITEM_ERROR};
    }

    // Create element using shared function
    Element* element = input_create_element(input, tag_name->chars);
    if (!element) {
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected end of input");
        return {.item = ITEM_ERROR};
    }

    // Parse attributes directly into the element
    if (!parse_attributes(input, element, html, html_start)) {
        parse_depth--;
        log_parse_error(html_start, *html, "Failed to parse attribute");
        return {.item = ITEM_ERROR};
    }

    // Check for self-closing tag
    bool is_self_closing = false;
    if (**html == '/') {
        is_self_closing = true;
        (*html)++; // Skip /
    }

    if (**html != '>') {
        parse_depth--;
        log_parse_error(html_start, *html, "Unexpected character '%c' while parsing element", **html);
        return {.item = ITEM_ERROR};
    }
    (*html)++; // Skip >

    // Handle content for non-void elements
    if (!is_self_closing && !is_void_element(tag_name->chars)) {
        // Parse content until closing tag (preserve all whitespace)
        char closing_tag[256];
        snprintf(closing_tag, sizeof(closing_tag), "</%s>", tag_name->chars);

        // Handle raw text elements (script, style, textarea, etc.) specially
        if (is_raw_text_element(tag_name->chars)) {
            StringBuf* content_sb = input->sb;
            stringbuf_reset(content_sb); // Ensure clean state

            // For raw text elements, we need to find the exact closing tag
            // and preserve all content as-is, including HTML tags within
            int content_chars = 0;
            const int max_content_chars = 10000; // Reduced safety limit
            size_t closing_tag_len = strlen(closing_tag);

            while (**html && content_chars < max_content_chars) {
                // Check if we found the closing tag (case-insensitive for robustness)
                if (strncasecmp(*html, closing_tag, closing_tag_len) == 0) {
                    break;
                }

                stringbuf_append_char(content_sb, **html);
                (*html)++;
                content_chars++;
            }

            // Check if we hit the safety limit
            if (content_chars < max_content_chars && content_sb->length > 0) {
                String *content_string = stringbuf_to_string(content_sb);
                Item content_item = {.item = s2it(content_string)};
                list_push((List*)element, content_item);
            } else {
                stringbuf_reset(content_sb);
            }
        } else {
            // regular content parsing for non-raw-text elements
            size_t closing_tag_len = strlen(closing_tag);
            while (**html) {
                const char* html_before = *html; // Track position to prevent infinite loops

                // Check for closing tag at the beginning of each iteration
                if (strncasecmp(*html, closing_tag, closing_tag_len) == 0) {
                    break;
                }

                if (**html == '<') {
                    // Check if it's the closing tag again (redundant check for safety)
                    if (strncasecmp(*html, closing_tag, closing_tag_len) == 0) {
                        break;
                    }

                    // Add safety check for recursion depth
                    if (parse_depth >= 15) {
                        // Skip to next '>' to avoid getting stuck
                        while (**html && **html != '>') {
                            (*html)++;
                        }
                        if (**html == '>') {
                            (*html)++;
                        }
                    } else {
                        // Parse child element
                        const char* before_child_parse = *html;
                        Item child = parse_element(input, html, html_start);

                        if (child.type_id == LMD_TYPE_ERROR) {
                            // If we hit an error, try to recover by skipping this character
                            if (**html) (*html)++;
                            break;
                        }
                        else if (child.type_id != LMD_TYPE_NULL) {
                            list_push((List*)element, child);
                        }

                        // Additional safety check for child parsing
                        if (*html == before_child_parse) {
                            (*html)++;
                        }
                    }
                }
                else {
                    // Parse text content including whitespace
                    // Start building text content
                    StringBuf* text_sb = input->sb;
                    stringbuf_reset(text_sb);

                    // Collect text until we hit '<' or closing tag
                    int text_chars = 0;
                    const int max_text_chars = 10000; // Limit text content size
                    while (**html && **html != '<' && text_chars < max_text_chars &&
                           strncasecmp(*html, closing_tag, closing_tag_len) != 0) {
                        stringbuf_append_char(text_sb, **html);
                        (*html)++;
                        text_chars++;
                    }

                    // Create text string if we have content (preserve all whitespace)
                    if (text_chars > 0) {
                        String *text_string = stringbuf_to_string(text_sb);
                        Item text_item = {.item = s2it(text_string)};
                        list_push((List*)element, text_item);
                    }
                }

                // Safety check: if HTML pointer didn't advance, force it to avoid infinite loop
                if (*html == html_before) {
                    if (**html) {
                        (*html)++; // Skip problematic character
                    } else {
                        break; // End of string
                    }
                }
            }
        }

        // Skip closing tag
        if (**html && strncasecmp(*html, closing_tag, strlen(closing_tag)) == 0) {
            *html += strlen(closing_tag);
        }

        // Set content length based on element's list length
        ((TypeElmt*)element->type)->content_length = ((List*)element)->length;
    }

    parse_depth--;
    return {.element = element};
}

void parse_html(Input* input, const char* html_string) {
    input->sb = stringbuf_new(input->pool);
    const char *html = html_string;

    // Create a root-level list to collect DOCTYPE, comments, and the main element
    List* root_list = (List*)pool_calloc(input->pool, sizeof(List));
    if (root_list) {
        root_list->type_id = LMD_TYPE_LIST;
        root_list->length = 0;
        root_list->capacity = 0;
        root_list->items = NULL;
    }

    // Skip leading whitespace (optional - could preserve as text node if needed)
    while (*html && isspace(*html)) {
        html++;
    }

    // Parse root-level items (DOCTYPE, comments, and elements)
    while (*html) {
        // Skip whitespace between root-level items
        while (*html && isspace(*html)) {
            html++;
        }

        if (!*html) break;

        // Parse DOCTYPE
        if (strncasecmp(html, "<!doctype", 9) == 0) {
            Item doctype_item = parse_doctype(input, &html, html_string);
            if (doctype_item.item != ITEM_ERROR) {
                list_push(root_list, doctype_item);
            }
            continue;
        }

        // Parse comments
        if (strncmp(html, "<!--", 4) == 0) {
            Item comment_item = parse_comment(input, &html, html_string);
            if (comment_item.item != ITEM_ERROR) {
                list_push(root_list, comment_item);
            }
            continue;
        }

        // Skip processing instructions
        if (strncmp(html, "<?", 2) == 0) {
            skip_processing_instruction(&html);
            continue;
        }

        // Skip CDATA (shouldn't appear at root level, but handle it)
        if (strncmp(html, "<![CDATA[", 9) == 0) {
            skip_cdata(&html);
            continue;
        }

        // Parse regular element (should be <html> or similar)
        if (*html == '<' && *(html + 1) != '/' && *(html + 1) != '!') {
            Item element_item = parse_element(input, &html, html_string);
            if (element_item.item != ITEM_ERROR && element_item.item != ITEM_NULL) {
                list_push(root_list, element_item);
            }
            continue;
        }

        // If we get here, there's unexpected content - skip it
        if (*html) {
            html++;
        }
    }

    // If list contains only one item, return that item and free the list
    if (root_list->length == 1) {
        input->root = root_list->items[0];
        // Note: We could free the list here, but the pool will handle cleanup
    } else if (root_list->length > 1) {
        // Return the list as the root
        input->root = (Item){.list = root_list};
    } else {
        // Empty document - return null
        input->root = (Item){.item = ITEM_NULL};
    }
}
