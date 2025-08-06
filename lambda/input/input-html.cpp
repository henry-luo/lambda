#include "input.h"

static Item parse_element(Input *input, const char **html);

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
        printf("WARNING: Hit whitespace limit, possible infinite loop in skip_whitespace\n");
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
    StrBuf* sb = input->sb;
    int char_count = 0;
    const int max_string_chars = 10000; // Safety limit
    
    // Handle empty string case - if we immediately encounter the end_char, just return empty string
    if (**html == end_char) {
        return strbuf_to_string(sb);
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
                        strbuf_append_char(sb, (char)code);
                    } else if (code < 0x800) {
                        strbuf_append_char(sb, (char)(0xC0 | (code >> 6)));
                        strbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x10000) {
                        strbuf_append_char(sb, (char)(0xE0 | (code >> 12)));
                        strbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        strbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x110000) {
                        // 4-byte UTF-8 encoding for code points up to U+10FFFF
                        strbuf_append_char(sb, (char)(0xF0 | (code >> 18)));
                        strbuf_append_char(sb, (char)(0x80 | ((code >> 12) & 0x3F)));
                        strbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        strbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else {
                        strbuf_append_char(sb, '?'); // Invalid code point
                    }
                } else {
                    strbuf_append_char(sb, '&');
                    strbuf_append_char(sb, '#');
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
                        strbuf_append_str(sb, entity_value);
                        *html = entity_end + 1; // Skip past the semicolon
                    } else {
                        // Unknown entity, output as-is with replacement character
                        strbuf_append_char(sb, '&');
                        for (const char* p = entity_start; p <= entity_end; p++) {
                            strbuf_append_char(sb, *p);
                        }
                        *html = entity_end + 1;
                    }
                } else {
                    // Invalid entity format, just append the &
                    strbuf_append_char(sb, '&');
                }
            }
        } else {
            strbuf_append_char(sb, **html);
            (*html)++;
        }
        char_count++;
    }

    return strbuf_to_string(sb);
}

static String* parse_attribute_value(Input *input, const char **html) {
    skip_whitespace(html);
    
    if (**html == '"') {
        (*html)++; // Skip opening quote
        
        String* value = parse_string_content(input, html, '"');
        
        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '"') {
            (*html)++; // Skip closing quote
        }
        
        return value;
    } else if (**html == '\'') {
        (*html)++; // Skip opening quote
        
        String* value = parse_string_content(input, html, '\'');
        
        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '\'') {
            (*html)++; // Skip closing quote
        }
        
        return value;
    } else {
        // Unquoted attribute value
        StrBuf* sb = input->sb;
        int char_count = 0;
        const int max_unquoted_chars = 1000; // Safety limit
        
        while (**html && **html != ' ' && **html != '\t' && **html != '\n' && 
               **html != '\r' && **html != '>' && **html != '/' && **html != '=' &&
               char_count < max_unquoted_chars) {
            strbuf_append_char(sb, **html);
            (*html)++;
            char_count++;
        }
        
        if (char_count >= max_unquoted_chars) {
            printf("WARNING: Hit unquoted attribute value limit (%d)\n", max_unquoted_chars);
        }
        
        return strbuf_to_string(sb);
    }
}

static bool parse_attributes(Input *input, Element *element, const char **html) {
    skip_whitespace(html);
    
    int attr_count = 0;
    const int max_attributes = 50; // Safety limit
    
    while (**html && **html != '>' && **html != '/' && attr_count < max_attributes) {
        attr_count++;
        
        // Parse attribute name
        StrBuf* sb = input->sb;
        const char* attr_start = *html;
        const char* name_start = *html;
        
        while (**html && **html != '=' && **html != ' ' && **html != '\t' && 
               **html != '\n' && **html != '\r' && **html != '>' && **html != '/') {
            strbuf_append_char(sb, tolower(**html));
            (*html)++;
        }
        
        if (sb->length == sizeof(uint32_t)) {
            // No attribute name found
            strbuf_full_reset(sb);
            break;
        }
        
        String *attr_name = strbuf_to_string(sb);        
        skip_whitespace(html);
        
        String* attr_value;
        if (**html == '=') {
            (*html)++; // Skip =
            attr_value = parse_attribute_value(input, html);
            
            // Check if parse_attribute_value returned NULL
            if (!attr_value) {
                // Create empty string as fallback
                StrBuf* fallback_sb = input->sb;
                if (!fallback_sb || !fallback_sb->str) {
                    return false;
                }
                
                String *empty_string = (String*)fallback_sb->str;
                empty_string->len = 0;
                empty_string->ref_cnt = 0;
                strbuf_full_reset(fallback_sb);
                attr_value = empty_string;
            }
        } else {
            // Boolean attribute (no value)
            StrBuf* empty_sb = input->sb;
            
            if (!empty_sb || !empty_sb->str) {
                return false;
            }
            
            String *empty_string = (String*)empty_sb->str;
            empty_string->len = 0;
            empty_string->ref_cnt = 0;
            strbuf_full_reset(empty_sb);
            attr_value = empty_string;
        }
        
        // Double-check that attr_value is not NULL before using it
        if (attr_value) {
            Item value = {.item = s2it(attr_value)};
            elmt_put(element, attr_name, value, input->pool);
        }
        
        skip_whitespace(html);
    }
    
    if (attr_count >= max_attributes) {
        printf("WARNING: Hit attribute limit (%d), possible infinite loop\n", max_attributes);
    }
    
    return true;
}

static String* parse_tag_name(Input *input, const char **html) {
    StrBuf* sb = input->sb;
    while (**html && **html != ' ' && **html != '\t' && **html != '\n' && 
           **html != '\r' && **html != '>' && **html != '/') {
        strbuf_append_char(sb, tolower(**html));
        (*html)++;
    }
    return strbuf_to_string(sb);
}

static void skip_comment(const char **html) {
    if (strncmp(*html, "<!--", 4) == 0) {
        *html += 4;
        while (**html && strncmp(*html, "-->", 3) != 0) {
            (*html)++;
        }
        if (**html) *html += 3; // Skip -->
    }
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

static Item parse_element(Input *input, const char **html) {
    static int parse_depth = 0;
    parse_depth++;
    
    if (parse_depth > 15) {  // Reduced limit for better debugging
        parse_depth--;
        return {.item = ITEM_ERROR};
    }
    
    if (**html != '<') {
        parse_depth--;
        return {.item = ITEM_ERROR};
    }
    
    // Skip comments
    if (strncmp(*html, "<!--", 4) == 0) {
        skip_comment(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        return {.item = ITEM_NULL};
    }
    
    // Skip DOCTYPE
    if (strncasecmp(*html, "<!doctype", 9) == 0) {
        skip_doctype(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        return {.item = ITEM_NULL};
    }
    
    // Skip processing instructions
    if (strncmp(*html, "<?", 2) == 0) {
        skip_processing_instruction(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        return {.item = ITEM_NULL};
    }
    
    // Skip CDATA
    if (strncmp(*html, "<![CDATA[", 9) == 0) {
        skip_cdata(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(input, html); // Try next element
            parse_depth--;
            return result;
        }
        parse_depth--;
        return {.item = ITEM_NULL};
    }
    
    (*html)++; // Skip <
    
    // Check for closing tag
    if (**html == '/') {
        // This is a closing tag, skip it and return null
        while (**html && **html != '>') {
            (*html)++;
        }
        if (**html) (*html)++; // Skip >
        parse_depth--;
        return {.item = ITEM_NULL};
    }
    
    String* tag_name = parse_tag_name(input, html);
    if (!tag_name || tag_name->len == 0) {
        parse_depth--;
        return {.item = ITEM_ERROR};
    }
    
    // Create element using shared function
    Element* element = input_create_element(input, tag_name->chars);
    if (!element) {
        parse_depth--;
        return {.item = ITEM_ERROR};
    }
    
    // Parse attributes directly into the element
    if (!parse_attributes(input, element, html)) {
        parse_depth--;
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
        return {.item = ITEM_ERROR};
    }
    (*html)++; // Skip >
    
    // Handle content for non-void elements
    if (!is_self_closing && !is_void_element(tag_name->chars)) {
        skip_whitespace(html);
        
        // Parse content until closing tag
        char closing_tag[256];
        snprintf(closing_tag, sizeof(closing_tag), "</%s>", tag_name->chars);
        
        // Handle raw text elements (script, style, textarea, etc.) specially
        if (is_raw_text_element(tag_name->chars)) {
            StrBuf* content_sb = input->sb;
            strbuf_full_reset(content_sb); // Ensure clean state
            
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
                
                strbuf_append_char(content_sb, **html);
                (*html)++;
                content_chars++;
            }
            
            // Check if we hit the safety limit
            if (content_chars >= max_content_chars) {
                strbuf_full_reset(content_sb);
            } else if (content_sb->length > sizeof(uint32_t)) {
                String *content_string = (String*)content_sb->str;
                content_string->len = content_sb->length - sizeof(uint32_t);
                content_string->ref_cnt = 0;
                strbuf_full_reset(content_sb);
                
                if (content_string->len > 0) {
                    Item content_item = {.item = s2it(content_string)};
                    list_push((List*)element, content_item);
                }
            } else {
                strbuf_full_reset(content_sb);
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
                        Item child = parse_element(input, html);
                        
                        if (child == ITEM_ERROR) {
                            // If we hit an error, try to recover by skipping this character
                            if (**html) (*html)++;
                            break;
                        } else if (child .item != ITEM_NULL) {
                            list_push((List*)element, child);
                        }
                        
                        // Additional safety check for child parsing
                        if (*html == before_child_parse) {
                            (*html)++;
                        }
                    }
                } else {
                    // Parse text content character by character for better control
                    if (**html && !isspace(**html)) {
                        // Start building text content
                        StrBuf* text_sb = input->sb;
                        strbuf_full_reset(text_sb);
                        
                        // Collect text until we hit '<' or closing tag
                        int text_chars = 0;
                        const int max_text_chars = 1000; // Limit text content size
                        
                        while (**html && **html != '<' && text_chars < max_text_chars &&
                               strncasecmp(*html, closing_tag, closing_tag_len) != 0) {
                            strbuf_append_char(text_sb, **html);
                            (*html)++;
                            text_chars++;
                        }
                        
                        // Create text string if we have content
                        if (text_chars > 0) {
                            String *text_string = (String*)text_sb->str;
                            text_string->len = text_sb->length - sizeof(uint32_t);
                            text_string->ref_cnt = 0;
                            strbuf_full_reset(text_sb);
                            
                            // Only add non-whitespace text
                            bool has_non_whitespace = false;
                            for (int i = 0; i < text_string->len; i++) {
                                if (!isspace(text_string->chars[i])) {
                                    has_non_whitespace = true;
                                    break;
                                }
                            }
                            
                            if (has_non_whitespace) {
                                Item text_item = {.item = s2it(text_string)};
                                list_push((List*)element, text_item);
                            }
                        } else {
                            strbuf_full_reset(text_sb);
                        }
                    } else {
                        // Skip whitespace
                        (*html)++;
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
                
                // Simplified whitespace skipping
                skip_whitespace(html);
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
    
    return {.item = (uint64_t)element};
}

void parse_html(Input* input, const char* html_string) {
    input->sb = strbuf_new_pooled(input->pool);
    const char *html = html_string;
    // skip any leading whitespace
    skip_whitespace(&html);
    // parse the root element
    if (*html) {
        input->root = parse_element(input, &html);
    }
}
