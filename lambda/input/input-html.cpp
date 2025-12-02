#include "input.hpp"
#include "input-html-scan.h"
#include "input-html-tokens.h"
#include "input-html-tree.h"
#include "input-html-context.h"
#include "../mark_builder.hpp"
#include "html_input_context.hpp"
#include "source_tracker.hpp"
#include <stdarg.h>

using namespace lambda;

static Item parse_element(HtmlInputContext& ctx, const char **html, const char *html_start);

// Global length limit for text content, strings, and raw text elements
static const int MAX_CONTENT_CHARS = 256 * 1024; // 256KB

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

// Element type arrays and scanning functions now in input-html-tokens.cpp and input-html-scan.cpp

// Wrapper function for compatibility - now calls html_is_void_element from input-html-tokens.cpp
static bool is_void_element(const char* tag_name) {
    return html_is_void_element(tag_name);
}

// Wrapper function for compatibility - now calls html_is_raw_text_element from input-html-tokens.cpp
static bool is_raw_text_element(const char* tag_name) {
    return html_is_raw_text_element(tag_name);
}

// Wrapper function for compatibility - now calls html_parse_string_content
static String* parse_string_content(HtmlInputContext& ctx, const char **html, char end_char) {
    return html_parse_string_content(ctx.sb, html, end_char);
}

// Continue with parse_attribute_value (kept here as it needs Input context)
static String* parse_attribute_value(HtmlInputContext& ctx, const char **html, const char *html_start) {
    return html_parse_attribute_value(ctx.sb, html, html_start);
}

static bool parse_attributes(HtmlInputContext& ctx, ElementBuilder& element, const char **html, const char *html_start) {
    skip_whitespace(html);

    int attr_count = 0;
    const int max_attributes = 500; // Safety limit
    log_debug("Parsing attributes at char: %d, '%c'", (int)(*html - html_start), **html);

    while (**html && **html != '>' && **html != '/' && attr_count < max_attributes) {
        attr_count++;
        log_debug("Parsing attribute %d, at char: %d, '%c'", attr_count, (int)(*html - html_start), **html);

        // Parse attribute name
        StringBuf* sb = ctx.sb;
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

        Item attr_value;
        if (**html == '=') {
            (*html)++; // Skip =
            skip_whitespace(html); // Skip whitespace after =
            String* str_value = parse_attribute_value(ctx, html, html_start);
            // Store attribute value (NULL for empty strings like class="")
            attr_value = (Item){.item = s2it(str_value)};
            // Type will be LMD_TYPE_NULL if str_value is NULL, LMD_TYPE_STRING otherwise
        } else {
            // Boolean attribute (no value) - store as boolean true
            attr_value = (Item){.item = b2it(true)};
        }

        // Add attribute to element using ElementBuilder
        element.attr(attr_name->chars, attr_value);

        skip_whitespace(html);
    }

    if (attr_count >= max_attributes) {
        log_error("Hit attribute limit (%d), possible infinite loop", max_attributes);
    }

    return true;
}

static String* parse_tag_name(HtmlInputContext& ctx, const char **html) {
    return html_parse_tag_name(ctx.sb, html);
}

// Parse HTML comment and return it as an element with tag name "!--"
static Item parse_comment(HtmlInputContext& ctx, const char **html, const char* html_start) {
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

    // Create element with tag name "!--" using ElementBuilder
    ElementBuilder element = ctx.builder.element("!--");

    // Add comment content as a text node child (if not empty)
    if (comment_len > 0) {
        String* comment_text = ctx.builder.createString(comment_start, comment_len);
        Item text_item = {.item = s2it(comment_text)};
        element.child(text_item);
    }

    *html += 3; // Skip -->

    return element.final();
}

// Parse DOCTYPE declaration and return it as an element with tag name "!DOCTYPE" or "!doctype"
static Item parse_doctype(HtmlInputContext& ctx, const char **html, const char* html_start) {
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

    // Create element with tag name "!DOCTYPE" or "!doctype" to preserve source case using ElementBuilder
    ElementBuilder element = ctx.builder.element(is_uppercase_DOCTYPE ? "!DOCTYPE" : "!doctype");

    // Add DOCTYPE content as a text node child (if not empty)
    if (content_len > 0) {
        String* doctype_text = ctx.builder.createString(content_start, content_len);
        Item text_item = {.item = s2it(doctype_text)};
        element.child(text_item);
    }

    *html += 1; // Skip '>'

    return element.final();
}

// Parse XML declaration and return it as an element with tag name "?xml"
static Item parse_xml_declaration(HtmlInputContext& ctx, const char **html, const char* html_start) {
    if (strncmp(*html, "<?xml", 5) != 0) {
        return {.item = ITEM_ERROR};
    }

    const char* decl_start = *html;
    *html += 5; // Skip "<?xml"

    // Find end of XML declaration
    while (**html && strncmp(*html, "?>", 2) != 0) {
        (*html)++;
    }

    if (!**html) {
        log_parse_error(html_start, *html, "Unclosed XML declaration");
        return {.item = ITEM_ERROR};
    }

    *html += 2; // Skip '?>'

    // Extract the entire XML declaration including <?xml and ?>
    size_t decl_len = *html - decl_start;

    // Create element with tag name "?xml" using ElementBuilder
    ElementBuilder element = ctx.builder.element("?xml");

    // Store the entire XML declaration as a text child (for easy roundtrip)
    if (decl_len > 0) {
        String* decl_text = ctx.builder.createString(decl_start, decl_len);
        Item text_item = {.item = s2it(decl_text)};
        element.child(text_item);
    }

    return element.final();
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

// Peek at the next opening tag name without consuming input
// Returns a temporary static buffer with the lowercase tag name, or NULL if not a start tag
// This is used for auto-close detection (dt/dd, li, p, etc.)
static const char* peek_next_tag_name(const char* html) {
    static char tag_buffer[64];  // Static buffer for tag name

    if (!html || *html != '<') return NULL;
    html++; // Skip '<'

    // Not a start tag if it's a closing tag, comment, doctype, etc.
    if (*html == '/' || *html == '!' || *html == '?') return NULL;

    // Extract tag name (up to space, >, /, or end of buffer)
    int i = 0;
    while (*html && i < 63 && *html != ' ' && *html != '\t' &&
           *html != '\n' && *html != '\r' && *html != '>' && *html != '/') {
        tag_buffer[i++] = tolower(*html);
        html++;
    }
    tag_buffer[i] = '\0';

    return i > 0 ? tag_buffer : NULL;
}

// Compatibility wrappers for custom element and attribute checks (now in input-html-tokens.cpp)
static bool is_valid_custom_element_name(const char* name) {
    return html_is_valid_custom_element_name(name);
}

static bool is_data_attribute(const char* attr_name) {
    return html_is_data_attribute(attr_name);
}

static bool is_aria_attribute(const char* attr_name) {
    return html_is_aria_attribute(attr_name);
}

static Item parse_element(HtmlInputContext& ctx, const char **html, const char *html_start) {
    html_enter_element();
    int parse_depth = html_get_parse_depth();

    if (**html != '<') {
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected character '%c' at beginning of element", **html);
        return {.item = ITEM_ERROR};
    }

    // Parse comments as special elements
    if (strncmp(*html, "<!--", 4) == 0) {
        Item comment = parse_comment(ctx, html, html_start);
        html_exit_element();
        return comment;
    }

    // Skip DOCTYPE
    if (strncasecmp(*html, "<!doctype", 9) == 0) {
        skip_doctype(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(ctx, html, html_start); // Try next element
            html_exit_element();
            return result;
        }
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected end of input after doctype");
        return {.item = ITEM_NULL};
    }

    // Skip processing instructions
    if (strncmp(*html, "<?", 2) == 0) {
        skip_processing_instruction(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(ctx, html, html_start); // Try next element
            html_exit_element();
            return result;
        }
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected end of input after processing instruction");
        return {.item = ITEM_NULL};
    }

    // Skip CDATA
    if (strncmp(*html, "<![CDATA[", 9) == 0) {
        skip_cdata(html);
        skip_whitespace(html);
        if (**html) {
            Item result = parse_element(ctx, html, html_start); // Try next element
            html_exit_element();
            return result;
        }
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected end of input after cdata");
        return {.item = ITEM_NULL};
    }

    log_debug("Parsing element at depth %d, at char: %d, '%c'",
        parse_depth, (int)(*html - html_start), **html);
    (*html)++; // Skip <

    // Check for closing tag
    if (**html == '/') {
        // This is a closing tag - parse the tag name and handle mode transition
        (*html)++; // Skip /

        const char* tag_start = *html;
        String* closing_tag_name = parse_tag_name(ctx, html);

        // Phase 4.2: Transition insertion mode for closing tag
        HtmlParserContext* context = ctx.htmlContext();
        if (context && closing_tag_name && closing_tag_name->len > 0) {
            html_context_transition_mode(context, closing_tag_name->chars, true);

            // Phase 5: Pop element from stack for standalone closing tag
            if (context->open_elements) {
                html_stack_pop_until_tag(context->open_elements, closing_tag_name->chars);
            }
        }

        // Skip to end of closing tag
        while (**html && **html != '>') {
            (*html)++;
        }
        if (**html) (*html)++; // Skip >

        html_exit_element();
        return {.item = ITEM_NULL};
    }

    String* tag_name = parse_tag_name(ctx, html);
    if (!tag_name || tag_name->len == 0) {
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected end of input after start tag");
        return {.item = ITEM_ERROR};
    }

    // Create element using MarkBuilder
    ElementBuilder elem_builder = ctx.builder.element(tag_name->chars);
    Element* element = elem_builder.final().element;
    if (!element) {
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected end of input");
        return {.item = ITEM_ERROR};
    }

    // Get HTML parser context for state management
    HtmlParserContext* context = ctx.htmlContext();

    // Phase 5: Push element onto open element stack
    if (context && context->open_elements) {
        html_stack_push(context->open_elements, element);
    }

    // Phase 6: Track formatting elements in active formatting list
    if (context && context->active_formatting && html_is_formatting_element(tag_name->chars)) {
        size_t stack_depth = context->open_elements ? context->open_elements->length : 0;
        html_formatting_push(context->active_formatting, element, stack_depth);
        log_debug("Added formatting element <%s> to active list (depth=%zu)", tag_name->chars, stack_depth);
    }

    // Phase 8: Reconstruct formatting elements when opening a block element
    if (context && context->active_formatting && html_is_block_element(tag_name->chars)) {
        if (html_formatting_length(context->active_formatting) > 0) {
            log_debug("Block element <%s> opened with %zu active formatting elements - reconstructing",
                      tag_name->chars, html_formatting_length(context->active_formatting));
            html_reconstruct_formatting(context, element);
        }
    }

    // Phase 4.1: Track explicit html/head/body tags in context
    if (context) {
        if (strcasecmp(tag_name->chars, "html") == 0) {
            html_context_set_html(context, element);
        } else if (strcasecmp(tag_name->chars, "head") == 0) {
            html_context_set_head(context, element);
        } else if (strcasecmp(tag_name->chars, "body") == 0) {
            html_context_set_body(context, element);
        }

        // Phase 4.2: Transition insertion mode for opening tag
        html_context_transition_mode(context, tag_name->chars, false);
    }

    // Parse attributes - need to create temporary ElementBuilder wrapper for parse_attributes
    // Since parse_attributes expects ElementBuilder& but we have raw Element*, we'll
    // keep using direct element manipulation here for compatibility with HTML5 context
    // Parse attributes using builder string buffer
    skip_whitespace(html);

    int attr_count = 0;
    const int max_attributes = 500; // Safety limit
    log_debug("Parsing attributes at char: %d, '%c'", (int)(*html - html_start), **html);

    while (**html && **html != '>' && **html != '/' && attr_count < max_attributes) {
        attr_count++;
        log_debug("Parsing attribute %d, at char: %d, '%c'", attr_count, (int)(*html - html_start), **html);

        // Parse attribute name
        StringBuf* sb = ctx.sb;
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

        Item attr_value;
        if (**html == '=') {
            (*html)++; // Skip =
            skip_whitespace(html); // Skip whitespace after =
            String* str_value = parse_attribute_value(ctx, html, html_start);
            // Store attribute value (NULL for empty strings like class="")
            attr_value = (Item){.item = s2it(str_value)};
            // Type will be LMD_TYPE_NULL if str_value is NULL, LMD_TYPE_STRING otherwise
        } else {
            // Boolean attribute (no value) - store as boolean true
            attr_value = (Item){.item = b2it(true)};
        }

        // Add attribute to element using builder
        ctx.builder.putToElement(element, attr_name, attr_value);

        skip_whitespace(html);
    }

    if (attr_count >= max_attributes) {
        log_error("Hit attribute limit (%d), possible infinite loop", max_attributes);
    }

    // Check for self-closing syntax (HTML5: only meaningful for void elements)
    bool has_self_closing_slash = false;
    if (**html == '/') {
        has_self_closing_slash = true;
        (*html)++; // Skip /
    }

    if (**html != '>') {
        html_exit_element();
        log_parse_error(html_start, *html, "Unexpected character '%c' while parsing element", **html);
        return {.item = ITEM_ERROR};
    }
    (*html)++; // Skip >

    // HTML5 spec: Void elements are ALWAYS self-closing regardless of syntax
    // Self-closing slash on non-void elements is ignored
    bool is_void = is_void_element(tag_name->chars);

    // Log when self-closing slash is present but ignored (HTML5 compliance)
    if (has_self_closing_slash && !is_void) {
        log_debug("Ignoring self-closing slash on non-void element <%s> per HTML5 spec", tag_name->chars);
    }

    // Handle content only for non-void elements
    if (!is_void) {
        // Parse content until closing tag (preserve all whitespace)
        char closing_tag[256];
        snprintf(closing_tag, sizeof(closing_tag), "</%s>", tag_name->chars);

        // Handle raw text elements (script, style, textarea, etc.) specially
        if (is_raw_text_element(tag_name->chars)) {
            StringBuf* content_sb = ctx.sb;
            stringbuf_reset(content_sb); // Ensure clean state

            // For raw text elements, we need to find the exact closing tag
            // and preserve all content as-is, including HTML tags within
            int content_chars = 0;
            size_t closing_tag_len = strlen(closing_tag);

            while (**html && content_chars < MAX_CONTENT_CHARS) {
                // Check if we found the closing tag (case-insensitive for robustness)
                if (strncasecmp(*html, closing_tag, closing_tag_len) == 0) {
                    break;
                }

                stringbuf_append_char(content_sb, **html);
                (*html)++;
                content_chars++;
            }

            // Check if we hit the safety limit
            if (content_chars < MAX_CONTENT_CHARS && content_sb->length > 0) {
                String *content_string = stringbuf_to_string(content_sb);
                Item content_item = {.item = s2it(content_string)};
                html_append_child(element, content_item);
            } else if (content_chars >= MAX_CONTENT_CHARS) {
                log_warn("Raw text content exceeded limit (%d chars) in <%s> element", MAX_CONTENT_CHARS, tag_name->chars);
                stringbuf_reset(content_sb);
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

                    // Auto-close detection: check if next tag implicitly closes current element
                    // Per HTML spec: dt/dd close each other, li closes li, p closes p, etc.
                    const char* next_tag = peek_next_tag_name(*html);
                    if (next_tag && html_tag_closes_parent(tag_name->chars, next_tag)) {
                        log_debug("Auto-close: <%s> closes <%s>, breaking out of content loop",
                                  next_tag, tag_name->chars);
                        // Don't consume the tag - let the parent parse it as a sibling
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
                        Item child = parse_element(ctx, html, html_start);

                        TypeId child_type = get_type_id(child);
                        if (child_type == LMD_TYPE_ERROR) {
                            // If we hit an error, try to recover by skipping this character
                            if (**html) (*html)++;
                            break;
                        }
                        else if (child_type != LMD_TYPE_NULL) {
                            html_append_child(element, child);
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
                    StringBuf* text_sb = ctx.sb;
                    stringbuf_reset(text_sb);

                    // Collect text until we hit '<' or closing tag
                    int text_chars = 0;
                    while (**html && **html != '<' && text_chars < MAX_CONTENT_CHARS &&
                        strncasecmp(*html, closing_tag, closing_tag_len) != 0) {
                        stringbuf_append_char(text_sb, **html);
                        (*html)++;  text_chars++;
                    }

                    // Create text string if we have content (preserve all whitespace)
                    if (text_chars > 0) {
                        String *text_string = stringbuf_to_string(text_sb);
                        log_debug("got text content: '%t'", text_string->chars);
                        Item text_item = {.item = s2it(text_string)};
                        log_debug("pushing text to element %p", element);
                        html_append_child(element, text_item);
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

            // Phase 4.2: Transition insertion mode for closing tag
            if (context) {
                html_context_transition_mode(context, tag_name->chars, true);
            }

            // Phase 5: Pop element from stack when it's closed
            if (context && context->open_elements) {
                html_stack_pop_until(context->open_elements, element);
            }

            // Phase 6: Remove formatting element from active list when closed
            if (context && context->active_formatting && html_is_formatting_element(tag_name->chars)) {
                if (html_formatting_remove(context->active_formatting, element)) {
                    log_debug("Removed formatting element <%s> from active list", tag_name->chars);
                }
            }
        }

        // Set content length based on element's list length
        html_set_content_length(element);
    } else {
        // Phase 5: For void elements, pop immediately since they have no closing tag
        if (context && context->open_elements) {
            Element* popped = html_stack_pop(context->open_elements);
            if (popped != element) {
                log_warn("Stack mismatch: expected %p but got %p for void element <%s>",
                         element, popped, tag_name->chars);
            }
        }
    }

    html_exit_element();
    return {.element = element};
}

// HTML5 spec requires implicit tbody creation for direct tr children of table
static void create_implicit_tbody(Input* input, Element* table_element) {
    if (!table_element) return;

    List* table_list = (List*)table_element;
    if (table_list->length == 0) return;

    log_debug("Checking table for implicit tbody - has %zu children", table_list->length);

    // Track if we need to create an implicit tbody
    bool has_direct_tr = false;
    bool has_tbody = false;

    // First pass: check if we have direct <tr> children or existing tbody/thead/tfoot
    for (size_t i = 0; i < table_list->length; i++) {
        Item child = table_list->items[i];
        TypeId child_type = get_type_id(child);
        if (child_type == LMD_TYPE_ELEMENT) {
            Element* child_element = child.element;
            TypeElmt* child_type_elmt = (TypeElmt*)child_element->type;
            if (!child_type_elmt) continue;
            const char* tag = child_type_elmt->name.str;

            log_debug("  Table child[%zu]: <%s>", i, tag);

            if (strcasecmp(tag, "tr") == 0) {
                has_direct_tr = true;
                log_debug("    Found direct <tr> child");
            } else if (strcasecmp(tag, "tbody") == 0 ||
                       strcasecmp(tag, "thead") == 0 ||
                       strcasecmp(tag, "tfoot") == 0) {
                has_tbody = true;
                log_debug("    Found existing <%s>", tag);
            }
        }
    }

    log_debug("Table analysis: has_direct_tr=%d, has_tbody=%d", has_direct_tr, has_tbody);

    // If we have direct <tr> children without a tbody, create implicit tbody
    if (has_direct_tr && !has_tbody) {
        log_info("Creating implicit <tbody> element for table with direct <tr> children");

        // Create implicit tbody element using MarkBuilder
        MarkBuilder builder(input);
        ElementBuilder tbody_builder = builder.element("tbody");
        Element* tbody = tbody_builder.final().element;
        if (!tbody) {
            log_error("Failed to create implicit tbody element");
            return;
        }

        // Create a new list for the table's children
        List* new_table_children = (List*)pool_calloc(input->pool, sizeof(List));
        if (!new_table_children) {
            log_error("Failed to allocate list for table children");
            return;
        }
        new_table_children->type_id = LMD_TYPE_LIST;
        new_table_children->length = 0;
        new_table_children->capacity = 0;
        new_table_children->items = NULL;

        // Move all <tr>, <td>, <th> children into tbody, keep others in table
        for (size_t i = 0; i < table_list->length; i++) {
            Item child = table_list->items[i];
            TypeId child_type = get_type_id(child);
            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_element = child.element;
                TypeElmt* child_type_elmt = (TypeElmt*)child_element->type;
                if (!child_type_elmt) continue;
                const char* tag = child_type_elmt->name.str;

                // HTML5 spec: tr, td, th directly under table should be wrapped in tbody
                if (strcasecmp(tag, "tr") == 0 ||
                    strcasecmp(tag, "td") == 0 ||
                    strcasecmp(tag, "th") == 0) {
                    // Add to tbody
                    log_debug("  Moving <%s> into implicit tbody", tag);
                    list_push((List*)tbody, child);
                } else {
                    // Keep in table (caption, colgroup, col, etc.)
                    log_debug("  Keeping <%s> in table", tag);
                    list_push(new_table_children, child);
                }
            } else {
                // Keep text nodes, comments, etc. in table
                list_push(new_table_children, child);
            }
        }

        // Set tbody content length
        ((TypeElmt*)tbody->type)->content_length = ((List*)tbody)->length;

        // Add tbody to the new table children list
        Item tbody_item = {.element = tbody};
        list_push(new_table_children, tbody_item);

        // Replace table's children with the new list
        table_list->items = new_table_children->items;
        table_list->length = new_table_children->length;
        table_list->capacity = new_table_children->capacity;

        // Update content length
        ((TypeElmt*)table_element->type)->content_length = table_list->length;

        log_info("Created implicit <tbody> element in <table> with %zu row(s)",
                  ((List*)tbody)->length);
    }
}

// Recursively process all elements to add implicit tbody where needed
static void process_implicit_tbody(Input* input, Item item) {
    TypeId item_type = get_type_id(item);

    if (item_type == LMD_TYPE_ELEMENT) {
        Element* element = item.element;
        TypeElmt* type = (TypeElmt*)element->type;
        if (!type) return;
        const char* tag = type->name.str;

        // Check if this is a table element
        if (strcasecmp(tag, "table") == 0) {
            create_implicit_tbody(input, element);
        }

        // Recursively process children
        List* list = (List*)element;
        for (size_t i = 0; i < list->length; i++) {
            process_implicit_tbody(input, list->items[i]);
        }
    } else if (item_type == LMD_TYPE_LIST) {
        // Process list items
        List* list = item.list;
        for (size_t i = 0; i < list->length; i++) {
            process_implicit_tbody(input, list->items[i]);
        }
    }
}

// Internal function - use input_from_source() instead for external API
__attribute__((visibility("hidden")))
void parse_html_impl(Input* input, const char* html_string) {
    const char *html = html_string;

    log_debug("parse_html_impl called with content: '%.50s...'", html_string);

    // Create HTML input context with error tracking and HTML5 state management
    HtmlInputContext ctx(input, html_string, strlen(html_string));

    // Create a root-level list to collect DOCTYPE, comments, and the main element
    List* root_list = (List*)pool_calloc(input->pool, sizeof(List));
    if (root_list) {
        root_list->type_id = LMD_TYPE_LIST;
        root_list->length = 0;
        root_list->capacity = 0;
        root_list->items = NULL;
        log_debug("Created root_list=%p, type_id=%d (should be %d)",
                  (void*)root_list, root_list->type_id, LMD_TYPE_LIST);
    }

    if (!root_list) {
        ctx.addError("Failed to allocate memory for root list");
        input->root = (Item){.item = ITEM_ERROR};
        return;
    }

    // Skip leading whitespace (optional - could preserve as text node if needed)
    while (*html && isspace(*html)) {
        html++;
    }

    log_debug("Starting root parse loop");

    // Parse root-level items (DOCTYPE, comments, and elements)
    while (*html) {
        // Skip whitespace between root-level items
        while (*html && isspace(*html)) {
            html++;
        }

        if (!*html) break;

        log_debug("Root parse loop: position='%.30s...'", html);

        // Parse DOCTYPE
        if (strncasecmp(html, "<!doctype", 9) == 0) {
            Item doctype_item = parse_doctype(ctx, &html, html_string);
            log_debug("After DOCTYPE: position='%.30s...'", html);
            if (doctype_item.item != ITEM_ERROR) {
                list_push(root_list, doctype_item);
            }
            continue;
        }

        // Parse comments
        if (strncmp(html, "<!--", 4) == 0) {
            Item comment_item = parse_comment(ctx, &html, html_string);
            if (comment_item.item != ITEM_ERROR) {
                list_push(root_list, comment_item);
            }
            continue;
        }

        // Parse XML declaration
        if (strncmp(html, "<?xml", 5) == 0) {
            Item xml_decl_item = parse_xml_declaration(ctx, &html, html_string);
            if (xml_decl_item.item != ITEM_ERROR) {
                list_push(root_list, xml_decl_item);
            }
            continue;
        }

        // Skip other processing instructions (not XML declaration)
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
            log_debug("Root parsing: about to parse element starting with: %.20s", html);
            Item element_item = parse_element(ctx, &html, html_string);
            log_debug("Root parsing: parse_element returned type_id=%d", get_type_id(element_item));
            if (element_item.item != ITEM_ERROR && element_item.item != ITEM_NULL) {
                list_push(root_list, element_item);
                log_debug("Root parsing: pushed element to root_list, length=%zu", root_list->length);
            }
            continue;
        }

        // If we get here, there's unexpected content - skip it
        if (*html) {
            html++;
        }
    }

    // HTML5 spec compliance: Create implicit tbody elements for tables with direct tr children
    // This must be done before setting input->root to ensure the DOM tree is compliant
    for (size_t i = 0; i < root_list->length; i++) {
        process_implicit_tbody(input, root_list->items[i]);
    }

    // HTML 1.0 normalization: If root has <HEADER> and <BODY> as siblings (no <html> wrapper),
    // normalize to match browser behavior:
    // - <HEADER> becomes <header> (HTML5 semantic element) inside <body>
    // - <TITLE> moves to implicit <head>
    // - <BODY> content follows <header> in the body
    // Result: <html><head><title>...</title></head><body><header>...</header>...body content...</body></html>
    bool has_header = false;
    bool has_body = false;
    bool has_html = false;
    Element* header_elem = nullptr;
    Element* body_elem = nullptr;

    for (size_t i = 0; i < root_list->length; i++) {
        Item item = root_list->items[i];
        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
            Element* elem = item.element;
            TypeElmt* type = (TypeElmt*)elem->type;
            if (type) {
                if (strcasecmp(type->name.str, "header") == 0) {
                    has_header = true;
                    header_elem = elem;
                } else if (strcasecmp(type->name.str, "body") == 0) {
                    has_body = true;
                    body_elem = elem;
                } else if (strcasecmp(type->name.str, "html") == 0) {
                    has_html = true;
                }
            }
        }
    }

    // HTML 1.0 detection: has HEADER and BODY as root siblings, no html wrapper
    if (has_header && has_body && !has_html) {
        log_info("Detected HTML 1.0 format: normalizing to match browser behavior");

        MarkBuilder builder(input);

        // Create <head> and extract <title> from HEADER
        ElementBuilder head_builder = builder.element("head");

        // Search HEADER children for <title> and move it to <head>
        if (header_elem) {
            List* header_list = (List*)header_elem;
            for (size_t i = 0; i < header_list->length; i++) {
                Item child = header_list->items[i];
                if (get_type_id(child) == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    TypeElmt* child_type = (TypeElmt*)child_elem->type;
                    if (child_type && strcasecmp(child_type->name.str, "title") == 0) {
                        head_builder.child(child);
                        // Mark as moved by setting to null (will be skipped later)
                        header_list->items[i] = (Item){.item = ITEM_NULL};
                        log_debug("Moved <title> to <head>");
                        break;
                    }
                }
            }
        }
        Element* head_elem = head_builder.final().element;

        // Create new <body> that contains:
        // 1. <header> (the original HEADER element, keeping its remaining children)
        // 2. All original <body> children
        ElementBuilder new_body_builder = builder.element("body");

        // Add <header> (original HEADER) as first child of body
        // But first, remove null items (moved title) and whitespace-only text from header
        if (header_elem) {
            List* header_list = (List*)header_elem;
            List* cleaned_header = (List*)pool_calloc(input->pool, sizeof(List));
            cleaned_header->type_id = LMD_TYPE_LIST;
            cleaned_header->length = 0;
            cleaned_header->capacity = 0;
            cleaned_header->items = nullptr;

            for (size_t i = 0; i < header_list->length; i++) {
                Item child = header_list->items[i];
                TypeId child_type_id = get_type_id(child);
                if (child_type_id == LMD_TYPE_NULL) continue;  // Skip moved items
                if (child_type_id == LMD_TYPE_STRING) {
                    // Skip whitespace-only strings
                    String* str = (String*)child.pointer;
                    bool is_whitespace = true;
                    for (size_t j = 0; j < str->len; j++) {
                        if (!isspace(str->chars[j])) {
                            is_whitespace = false;
                            break;
                        }
                    }
                    if (is_whitespace) continue;
                }
                list_push(cleaned_header, child);
            }

            // Update header element's children
            header_list->items = cleaned_header->items;
            header_list->length = cleaned_header->length;
            header_list->capacity = cleaned_header->capacity;

            new_body_builder.child((Item){.element = header_elem});
        }

        // Add all children from original <body>
        if (body_elem) {
            List* body_list = (List*)body_elem;
            for (size_t i = 0; i < body_list->length; i++) {
                new_body_builder.child(body_list->items[i]);
            }
        }
        Element* new_body_elem = new_body_builder.final().element;

        // Create <html> wrapper
        ElementBuilder html_builder = builder.element("html");
        html_builder.child((Item){.element = head_elem});
        html_builder.child((Item){.element = new_body_elem});
        Element* html_elem = html_builder.final().element;

        // Replace root_list contents with just the html element
        root_list->length = 0;
        list_push(root_list, (Item){.element = html_elem});

        log_info("Created normalized HTML structure for HTML 1.0 document");
    }

    // If list contains only one item, return that item and free the list
    if (root_list->length == 1) {
        input->root = root_list->items[0];
        log_debug("Setting input->root to single item, type_id=%d", get_type_id(input->root));
        // Note: We could free the list here, but the pool will handle cleanup
    } else if (root_list->length > 1) {
        // Return the list as the root
        input->root = (Item){.list = root_list};
        log_debug("Setting input->root to list with %zu items, root_list=%p, root_list->type_id=%d",
                  root_list->length, (void*)root_list, root_list->type_id);
        log_debug("Verifying: input->root.item=%llx, input->root.list=%p, type_id=%d",
                  (unsigned long long)input->root.item, (void*)input->root.list, get_type_id(input->root));
    } else {
        // Empty document - return null
        input->root = (Item){.item = ITEM_NULL};
        log_debug("Setting input->root to NULL (empty document)");
    }

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }

    // HtmlInputContext destructor will automatically clean up HtmlParserContext
}
