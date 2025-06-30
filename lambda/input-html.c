#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
    while (**html && (**html == ' ' || **html == '\n' || **html == '\r' || **html == '\t')) {
        (*html)++;
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

static void to_lowercase(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

static String* parse_string_content(Input *input, const char **html, char end_char) {
    StrBuf* sb = input->sb;
    
    while (**html && **html != end_char) {
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
    }

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static String* parse_text_content(Input *input, const char **html) {
    return parse_string_content(input, html, '<');
}

static String* parse_attribute_value(Input *input, const char **html) {
    skip_whitespace(html);
    
    if (**html == '"') {
        (*html)++; // Skip opening quote
        String* value = parse_string_content(input, html, '"');
        if (**html == '"') (*html)++; // Skip closing quote
        return value;
    } else if (**html == '\'') {
        (*html)++; // Skip opening quote
        String* value = parse_string_content(input, html, '\'');
        if (**html == '\'') (*html)++; // Skip closing quote
        return value;
    } else {
        // Unquoted attribute value
        StrBuf* sb = input->sb;
        while (**html && **html != ' ' && **html != '\t' && **html != '\n' && 
               **html != '\r' && **html != '>' && **html != '/' && **html != '=') {
            strbuf_append_char(sb, **html);
            (*html)++;
        }
        
        String *string = (String*)sb->str;
        string->len = sb->length - sizeof(uint32_t);
        string->ref_cnt = 0;
        strbuf_full_reset(sb);
        return string;
    }
}

static Map* parse_attributes(Input *input, const char **html) {
    Map* attributes = map_pooled(input->pool);
    if (!attributes) return NULL;
    
    TypeMap* attr_type = map_init_cap(attributes, input->pool);
    if (!attributes->data) return attributes;
    
    ShapeEntry* shape_entry = NULL;
    
    skip_whitespace(html);
    
    while (**html && **html != '>' && **html != '/') {
        // Parse attribute name
        StrBuf* sb = input->sb;
        const char* attr_start = *html;
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
        
        String *attr_name = (String*)sb->str;
        attr_name->len = sb->length - sizeof(uint32_t);
        attr_name->ref_cnt = 0;
        strbuf_full_reset(sb);
        
        skip_whitespace(html);
        
        String* attr_value;
        if (**html == '=') {
            (*html)++; // Skip =
            attr_value = parse_attribute_value(input, html);
        } else {
            // Boolean attribute (no value)
            StrBuf* empty_sb = input->sb;
            
            if (!empty_sb || !empty_sb->str) {
                return attributes;
            }
            
            String *empty_string = (String*)empty_sb->str;
            empty_string->len = 0;
            empty_string->ref_cnt = 0;
            strbuf_full_reset(empty_sb);
            attr_value = empty_string;
        }
        
        LambdaItem value = (LambdaItem)s2it(attr_value);
        map_put(attributes, attr_type, attr_name, value, input->pool, &shape_entry);
        
        skip_whitespace(html);
    }
    
    arraylist_append(input->type_list, attr_type);
    attr_type->type_index = input->type_list->length - 1;
    return attributes;
}

static String* parse_tag_name(Input *input, const char **html) {
    StrBuf* sb = input->sb;
    
    while (**html && **html != ' ' && **html != '\t' && **html != '\n' && 
           **html != '\r' && **html != '>' && **html != '/') {
        strbuf_append_char(sb, tolower(**html));
        (*html)++;
    }
    
    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
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
    
    printf("DEBUG: parse_element depth %d, char: '%c', html: %.50s\n", 
           parse_depth, **html, *html);
    
    if (parse_depth > 20) {  // Reduced limit for faster debugging
        printf("ERROR: Parse depth exceeded 20, possible infinite recursion\n");
        parse_depth--;
        return ITEM_ERROR;
    }
    
    if (**html != '<') {
        parse_depth--;
        return ITEM_ERROR;
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
        return ITEM_NULL;
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
        return ITEM_NULL;
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
        return ITEM_NULL;
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
        return ITEM_NULL;
    }
    
    (*html)++; // Skip <
    
    // Check for closing tag
    if (**html == '/') {
        // This is a closing tag, skip it and return null
        printf("DEBUG: Found closing tag at depth %d, skipping: ", parse_depth);
        const char* start = *html;
        while (**html && **html != '>') {
            (*html)++;
        }
        if (**html) (*html)++; // Skip >
        printf("%.*s\n", (int)(*html - start), start);
        parse_depth--;
        return ITEM_NULL;
    }
    
    String* tag_name = parse_tag_name(input, html);
    if (!tag_name || tag_name->len == 0) {
        parse_depth--;
        return ITEM_ERROR;
    }
    
    Map* attributes = parse_attributes(input, html);
    if (!attributes) {
        parse_depth--;
        return ITEM_ERROR;
    }
    
    // Check for self-closing tag
    bool is_self_closing = false;
    if (**html == '/') {
        is_self_closing = true;
        (*html)++; // Skip /
    }
    
    if (**html != '>') {
        parse_depth--;
        return ITEM_ERROR;
    }
    (*html)++; // Skip >
    
    // Create element map
    Map* element = map_pooled(input->pool);
    if (!element) {
        parse_depth--;
        return ITEM_ERROR;
    }
    
    TypeMap* elem_type = map_init_cap(element, input->pool);
    if (!element->data) {
        parse_depth--;
        return ITEM_ERROR;
    }
    
    ShapeEntry* elem_shape_entry = NULL;
    
    // Add tag name
    StrBuf* tag_key_sb = input->sb;
    strbuf_full_reset(tag_key_sb); // Ensure buffer is clean before use
    strbuf_append_str(tag_key_sb, "tag");
    String *tag_key = (String*)tag_key_sb->str;
    tag_key->len = tag_key_sb->length - sizeof(uint32_t);
    tag_key->ref_cnt = 0;
    strbuf_full_reset(tag_key_sb);
    
    LambdaItem tag_value = (LambdaItem)s2it(tag_name);
    map_put(element, elem_type, tag_key, tag_value, input->pool, &elem_shape_entry);
    
    // Add attributes only if there are any
    if (attributes->type && ((TypeMap*)attributes->type)->length > 0) {
        StrBuf* attr_key_sb = input->sb;
        strbuf_full_reset(attr_key_sb); // Ensure buffer is clean before use
        strbuf_append_str(attr_key_sb, "attributes");
        String *attr_key = (String*)attr_key_sb->str;
        attr_key->len = attr_key_sb->length - sizeof(uint32_t);
        attr_key->ref_cnt = 0;
        strbuf_full_reset(attr_key_sb);
        
        LambdaItem attr_value = {.raw_pointer = attributes};
        map_put(element, elem_type, attr_key, attr_value, input->pool, &elem_shape_entry);
    }
    
    // Handle content for non-void elements
    if (!is_self_closing && !is_void_element(tag_name->chars)) {
        Array* children = array_pooled(input->pool);
        if (children) {
            skip_whitespace(html);
            
            // Parse content until closing tag
            char closing_tag[256];
            snprintf(closing_tag, sizeof(closing_tag), "</%s>", tag_name->chars);
            
            // Add safety counter to prevent infinite loops
            int max_iterations = 100;  // Further reduced for faster debugging
            int iteration_count = 0;
            
            printf("DEBUG: Starting content parsing for tag '%s', closing_tag: '%s'\n", 
                   tag_name->chars, closing_tag);
            
            while (**html && strncasecmp(*html, closing_tag, strlen(closing_tag)) != 0 && 
                   iteration_count < max_iterations) {
                iteration_count++;
                const char* html_before = *html; // Track position to prevent infinite loops
                
                // Debug output to track parsing progress
                printf("DEBUG: iter %d, depth %d, char: '%c', html: %.30s\n", 
                       iteration_count, parse_depth, **html, *html);
                
                if (**html == '<') {
                    // Check if it's the closing tag
                    if (strncasecmp(*html, closing_tag, strlen(closing_tag)) == 0) {
                        printf("DEBUG: Found closing tag: %s\n", closing_tag);
                        break;
                    }
                    
                    // Handle script, style, and template tags specially (preserve content as-is)
                    if (strcasecmp(tag_name->chars, "script") == 0 || 
                        strcasecmp(tag_name->chars, "style") == 0 ||
                        strcasecmp(tag_name->chars, "template") == 0) {
                        printf("DEBUG: Parsing special content for %s tag\n", tag_name->chars);
                        StrBuf* content_sb = input->sb;
                        while (**html && strncasecmp(*html, closing_tag, strlen(closing_tag)) != 0) {
                            strbuf_append_char(content_sb, **html);
                            (*html)++;
                        }
                        
                        String *content_string = (String*)content_sb->str;
                        content_string->len = content_sb->length - sizeof(uint32_t);
                        content_string->ref_cnt = 0;
                        strbuf_full_reset(content_sb);
                        
                        if (content_string->len > 0) {
                            LambdaItem content_item = (LambdaItem)s2it(content_string);
                            array_append(children, content_item, input->pool);
                        }
                        break;
                    }
                    
                    printf("DEBUG: Calling parse_element recursively from depth %d\n", parse_depth);
                    Item child = parse_element(input, html);
                    printf("DEBUG: parse_element returned, child=%llu, new char: '%c'\n", 
                           child, **html ? **html : '\0');
                    
                    if (child == ITEM_ERROR) {
                        // If we hit an error, try to recover by skipping this character
                        printf("Warning: parse_element returned error, skipping character '%c'\n", **html);
                        if (**html) (*html)++;
                        break;
                    } else if (child != ITEM_NULL) {
                        LambdaItem child_item = {.item = child};
                        array_append(children, child_item, input->pool);
                    }
                    // Note: ITEM_NULL means we hit a closing tag or skipped element
                    // The parse_element function should have advanced the HTML pointer
                    
                    // Safety check: if HTML pointer didn't advance, force it to avoid infinite loop
                    if (*html == html_before) {
                        printf("Warning: parse_element didn't advance HTML pointer from char '%c', forcing advance\n", **html);
                        (*html)++; // Skip problematic character
                    }
                } else {
                    // Parse text content
                    String* text = parse_text_content(input, html);
                    if (text && text->len > 0) {
                        // For pre, code, textarea, template tags, preserve whitespace
                        bool preserve_whitespace = (strcasecmp(tag_name->chars, "pre") == 0 ||
                                                   strcasecmp(tag_name->chars, "code") == 0 ||
                                                   strcasecmp(tag_name->chars, "textarea") == 0 ||
                                                   strcasecmp(tag_name->chars, "template") == 0);
                        
                        if (preserve_whitespace) {
                            LambdaItem text_item = (LambdaItem)s2it(text);
                            array_append(children, text_item, input->pool);
                        } else {
                            // Trim whitespace for other elements
                            char* start = text->chars;
                            char* end = text->chars + text->len - 1;
                            while (start <= end && isspace(*start)) start++;
                            while (end >= start && isspace(*end)) end--;
                            
                            if (start <= end) {
                                // Create trimmed string
                                size_t trimmed_len = end - start + 1;
                                StrBuf* trimmed_sb = input->sb;
                                for (size_t i = 0; i < trimmed_len; i++) {
                                    strbuf_append_char(trimmed_sb, start[i]);
                                }
                                
                                String *trimmed_string = (String*)trimmed_sb->str;
                                trimmed_string->len = trimmed_sb->length - sizeof(uint32_t);
                                trimmed_string->ref_cnt = 0;
                                strbuf_full_reset(trimmed_sb);
                                
                                LambdaItem text_item = (LambdaItem)s2it(trimmed_string);
                                array_append(children, text_item, input->pool);
                            }
                        }
                    } else {
                        // No text content found, and we're not at a < character
                        // This means we're likely at some other character that needs to be handled
                        // Skip this character to avoid infinite loop
                        if (**html && **html != '<') {
                            (*html)++;
                        }
                    }
                    
                    // Safety check: if HTML pointer didn't advance, force it to avoid infinite loop
                    if (*html == html_before) {
                        printf("Warning: HTML pointer stuck at char '%c', forcing advance\n", **html);
                        (*html)++; // Skip problematic character
                    }
                }
                skip_whitespace(html);
            }
            
            // Check if we exited due to iteration limit
            if (iteration_count >= max_iterations) {
                printf("Warning: HTML parser hit iteration limit, possible infinite loop detected\n");
            }
            
            // Skip closing tag
            if (**html && strncasecmp(*html, closing_tag, strlen(closing_tag)) == 0) {
                *html += strlen(closing_tag);
            }
            
            // Add children to element only if there are any
            if (children->length > 0) {
                StrBuf* children_key_sb = input->sb;
                strbuf_full_reset(children_key_sb); // Ensure buffer is clean before use
                strbuf_append_str(children_key_sb, "children");
                String *children_key = (String*)children_key_sb->str;
                children_key->len = children_key_sb->length - sizeof(uint32_t);
                children_key->ref_cnt = 0;
                strbuf_full_reset(children_key_sb);
                
                LambdaItem children_value = {.raw_pointer = children};
                map_put(element, elem_type, children_key, children_value, input->pool, &elem_shape_entry);
            }
        }
    }
    
    arraylist_append(input->type_list, elem_type);
    elem_type->type_index = input->type_list->length - 1;
    parse_depth--;
    printf("DEBUG: parse_element depth %d EXITING, returning element for tag '%s'\n", 
           parse_depth, tag_name->chars);
    return (Item)element;
}

Input* html_parse(const char* html_string) {
    Input* input = malloc(sizeof(Input));
    input->path = NULL;
    size_t grow_size = 1024;
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { 
        free(input);  
        return NULL; 
    }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);

    const char *html = html_string;
    
    // Skip any leading whitespace
    skip_whitespace(&html);
    
    // Parse the root element
    if (*html) {
        input->root = parse_element(input, &html);
    }
    
    return input;
}
