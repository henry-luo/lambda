#include "input.hpp"
#include "../../lib/memtrack.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

using namespace lambda;

// Forward declarations
static Element* parse_mdx_content(InputContext& ctx, const char* content);
static Element* parse_mdx_element(InputContext& ctx, const char** pos, const char* end);
static bool is_jsx_component_tag(const char* tag_name);
static bool is_html_element_tag(const char* tag_name);
static const char* extract_tag_name(const char* pos, const char* end, char* buffer, size_t buffer_size);
static Element* parse_jsx_component(InputContext& ctx, const char** pos, const char* end);
static Element* parse_html_element(InputContext& ctx, const char** pos, const char* end);
static Element* create_mdx_document(InputContext& ctx, const char* content);

// Utility function to check if a tag name represents a JSX component (starts with uppercase)
static bool is_jsx_component_tag(const char* tag_name) {
    return tag_name && tag_name[0] >= 'A' && tag_name[0] <= 'Z';
}

// Utility function to check if a tag name represents an HTML element (starts with lowercase)
static bool is_html_element_tag(const char* tag_name) {
    return tag_name && tag_name[0] >= 'a' && tag_name[0] <= 'z';
}

// Extract tag name from current position
static const char* extract_tag_name(const char* pos, const char* end, char* buffer, size_t buffer_size) {
    if (!pos || !buffer || buffer_size == 0 || pos >= end || *pos != '<') {
        return NULL;
    }

    pos++; // Skip '<'
    size_t i = 0;

    // Extract tag name until we hit whitespace, '>', or '/'
    while (pos < end && i < buffer_size - 1 &&
           *pos != ' ' && *pos != '\t' && *pos != '\n' && *pos != '>' && *pos != '/') {
        buffer[i++] = *pos++;
    }

    buffer[i] = '\0';
    return i > 0 ? buffer : NULL;
}

// Parse JSX component using existing JSX parser
static Element* parse_jsx_component(InputContext& ctx, const char** pos, const char* end) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;
    // Use the existing JSX parsing functionality
    // For now, create a simple JSX element and delegate to JSX parser
    const char* jsx_start = *pos;

    // Find the end of this JSX component
    int bracket_count = 0;
    const char* jsx_end = jsx_start;
    bool in_opening_tag = true;
    bool is_self_closing = false;
    const char* tag_name_start = NULL;
    size_t tag_name_len = 0;

    // Skip initial '<'
    if (*jsx_end == '<') {
        jsx_end++;
        tag_name_start = jsx_end;

        // Extract tag name
        while (jsx_end < end && *jsx_end != '>' && *jsx_end != '/' && !isspace(*jsx_end)) {
            jsx_end++;
        }
        tag_name_len = jsx_end - tag_name_start;
        jsx_end = jsx_start; // Reset for full parsing
    }

    while (jsx_end < end) {
        if (*jsx_end == '<') {
            if (jsx_end + 1 < end && jsx_end[1] == '/') {
                // Closing tag - check if it matches our tag name
                const char* closing_tag_start = jsx_end + 2;
                if (tag_name_len > 0 && strncmp(closing_tag_start, tag_name_start, tag_name_len) == 0 &&
                    closing_tag_start[tag_name_len] == '>') {
                    jsx_end = closing_tag_start + tag_name_len + 1; // Include </TagName>
                    break;
                }
            }
            bracket_count++;
        } else if (*jsx_end == '>') {
            if (in_opening_tag && jsx_end > jsx_start && jsx_end[-1] == '/') {
                // Self-closing tag like <Button />
                jsx_end++;
                break;
            }
            bracket_count--;
            if (bracket_count == 0 && in_opening_tag) {
                in_opening_tag = false;
                jsx_end++; // Include the closing >
                continue;
            }
        }
        jsx_end++;
    }

    // Create JSX element
    ElementBuilder jsx_elem = builder.element("jsx_element");

    // Store the JSX content as text for now
    size_t jsx_len = jsx_end - jsx_start;
    char* jsx_buffer = (char*)mem_alloc(jsx_len + 1, MEM_CAT_INPUT_MDX);
    if (jsx_buffer) {
        strncpy(jsx_buffer, jsx_start, jsx_len);
        jsx_buffer[jsx_len] = '\0';
        String* jsx_content = builder.createString(jsx_buffer);
        Item jsx_item = {.item = s2it(jsx_content)};
        jsx_elem.attr("content", jsx_item);
        mem_free(jsx_buffer);
    }

    *pos = jsx_end;
    // Return raw Element* for compatibility with existing list_push code
    return jsx_elem.final().element;
}

// Parse HTML element using existing HTML parser
static Element* parse_html_element(InputContext& ctx, const char** pos, const char* end) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;
    // Use existing HTML parsing functionality
    const char* html_start = *pos;

    // Find the end of this HTML element
    int bracket_count = 0;
    const char* html_end = html_start;

    while (html_end < end) {
        if (*html_end == '<') {
            bracket_count++;
        } else if (*html_end == '>') {
            bracket_count--;
            if (bracket_count == 0) {
                html_end++; // Include the closing >
                break;
            }
        }
        html_end++;
    }

    // Create HTML element
    ElementBuilder html_elem = builder.element("html_element");

    // Store the HTML content as text for now
    size_t html_len = html_end - html_start;
    char* html_buffer = (char*)mem_alloc(html_len + 1, MEM_CAT_INPUT_MDX);
    if (html_buffer) {
        strncpy(html_buffer, html_start, html_len);
        html_buffer[html_len] = '\0';
        String* html_content = builder.createString(html_buffer);
        Item html_item = {.item = s2it(html_content)};
        html_elem.attr("content", html_item);
        mem_free(html_buffer);
    }

    *pos = html_end;
    // Return raw Element* for compatibility with existing list_push code
    return html_elem.final().element;
}

// Parse MDX element (either JSX component or HTML element)
static Element* parse_mdx_element(InputContext& ctx, const char** pos, const char* end) {
    char tag_name[256];
    const char* tag = extract_tag_name(*pos, end, tag_name, sizeof(tag_name));

    if (!tag) {
        return NULL;
    }

    if (is_jsx_component_tag(tag)) {
        // Uppercase tag -> JSX component
        return parse_jsx_component(ctx, pos, end);
    } else if (is_html_element_tag(tag)) {
        // Lowercase tag -> HTML element
        return parse_html_element(ctx, pos, end);
    }

    return NULL;
}

// Parse MDX content with mixed markdown, HTML, and JSX
static Element* parse_mdx_content(InputContext& ctx, const char* content) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;
    ElementBuilder root = builder.element("mdx_document");
    ElementBuilder body = builder.element("body");

    const char* pos = content;
    const char* end = content + strlen(content);
    const char* text_start = pos;

    while (pos < end) {
        if (*pos == '<') {
            // Found a potential element

            // First, process any preceding markdown text
            if (pos > text_start) {
                size_t text_len = pos - text_start;
                char* text_buffer = (char*)mem_alloc(text_len + 1, MEM_CAT_INPUT_MDX);
                if (text_buffer) {
                    strncpy(text_buffer, text_start, text_len);
                    text_buffer[text_len] = '\0';

                    // Parse the text as markdown
                    Item markdown_item = input_markup(input, text_buffer);
                    if (markdown_item.item != ITEM_NULL && get_type_id(markdown_item) == LMD_TYPE_ELEMENT) {
                        // Add markdown content to body as children
                        body.child(markdown_item);
                    }

                    mem_free(text_buffer);
                }
            }

            // Parse the element (JSX or HTML)
            Element* element = parse_mdx_element(ctx, &pos, end);
            if (element) {
                Item element_item = {.element = element};
                // Add element as child
                body.child(element_item);
            } else {
                // If parsing failed, treat as regular text
                pos++;
            }

            text_start = pos;
        } else {
            pos++;
        }
    }

    // Process any remaining text
    if (pos > text_start) {
        size_t text_len = pos - text_start;
        char* text_buffer = (char*)mem_alloc(text_len + 1, MEM_CAT_INPUT_MDX);
        if (text_buffer) {
            strncpy(text_buffer, text_start, text_len);
            text_buffer[text_len] = '\0';

            // Parse the text as markdown
            Item markdown_item = input_markup(input, text_buffer);
            if (markdown_item.item != ITEM_NULL && get_type_id(markdown_item) == LMD_TYPE_ELEMENT) {
                // Add final markdown content to body as child
                body.child(markdown_item);
            }

            mem_free(text_buffer);
        }
    }

    // Add body to root as child
    Item body_item = body.final();
    root.child(body_item);

    // Return raw Element* for compatibility
    return root.final().element;
}

// Enhanced MDX document creation with proper parsing
static Element* create_mdx_document(InputContext& ctx, const char* content) {
    return parse_mdx_content(ctx, content);
}

// Main MDX parsing function
void parse_mdx(Input* input, const char* mdx_string) {
    if (!mdx_string || !input) return;

    // create error tracking context with integrated source tracking
    InputContext ctx(input, mdx_string, strlen(mdx_string));

    Element* root = create_mdx_document(ctx, mdx_string);
    if (root) {
        input->root = (Item){.element = root};
    } else {
        ctx.addError(ctx.tracker.location(), "Failed to parse MDX document");
    }

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}

// Public interface function
Item input_mdx(Input* input, const char* mdx_string) {
    if (!input || !mdx_string) {
        return (Item){.item = ITEM_NULL};
    }

    parse_mdx(input, mdx_string);
    return input->root;
}
