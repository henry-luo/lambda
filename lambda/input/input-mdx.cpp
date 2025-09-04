#include "input.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Forward declarations
static Element* parse_mdx_content(Input* input, const char* content);
static Element* parse_mdx_element(Input* input, const char** pos, const char* end);
static bool is_jsx_component_tag(const char* tag_name);
static bool is_html_element_tag(const char* tag_name);
static const char* extract_tag_name(const char* pos, const char* end, char* buffer, size_t buffer_size);
static Element* parse_jsx_component(Input* input, const char** pos, const char* end);
static Element* parse_html_element(Input* input, const char** pos, const char* end);
static void skip_whitespace(const char** pos, const char* end);

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

// Skip whitespace characters
static void skip_whitespace(const char** pos, const char* end) {
    while (*pos < end && (**pos == ' ' || **pos == '\t' || **pos == '\n' || **pos == '\r')) {
        (*pos)++;
    }
}

// Parse JSX component using existing JSX parser
static Element* parse_jsx_component(Input* input, const char** pos, const char* end) {
    // Use the existing JSX parsing functionality
    // For now, create a simple JSX element and delegate to JSX parser
    const char* jsx_start = *pos;
    
    // Find the end of this JSX component
    int bracket_count = 0;
    const char* jsx_end = jsx_start;
    
    while (jsx_end < end) {
        if (*jsx_end == '<') {
            bracket_count++;
        } else if (*jsx_end == '>') {
            bracket_count--;
            if (bracket_count == 0) {
                jsx_end++; // Include the closing >
                break;
            }
        }
        jsx_end++;
    }
    
    // Create JSX element
    Element* jsx_elem = input_create_element(input, "jsx_element");
    
    // Store the JSX content as text for now
    size_t jsx_len = jsx_end - jsx_start;
    char* jsx_buffer = (char*)malloc(jsx_len + 1);
    if (jsx_buffer) {
        strncpy(jsx_buffer, jsx_start, jsx_len);
        jsx_buffer[jsx_len] = '\0';
        String* jsx_content = input_create_string(input, jsx_buffer);
        Item jsx_item = {.raw_pointer = jsx_content};
        input_add_attribute_item_to_element(input, jsx_elem, "content", jsx_item);
        free(jsx_buffer);
    }
    
    *pos = jsx_end;
    return jsx_elem;
}

// Parse HTML element using existing HTML parser
static Element* parse_html_element(Input* input, const char** pos, const char* end) {
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
    Element* html_elem = input_create_element(input, "html_element");
    
    // Store the HTML content as text for now
    size_t html_len = html_end - html_start;
    char* html_buffer = (char*)malloc(html_len + 1);
    if (html_buffer) {
        strncpy(html_buffer, html_start, html_len);
        html_buffer[html_len] = '\0';
        String* html_content = input_create_string(input, html_buffer);
        Item html_item = {.raw_pointer = html_content};
        input_add_attribute_item_to_element(input, html_elem, "content", html_item);
        free(html_buffer);
    }
    
    *pos = html_end;
    return html_elem;
}

// Parse MDX element (either JSX component or HTML element)
static Element* parse_mdx_element(Input* input, const char** pos, const char* end) {
    char tag_name[256];
    const char* tag = extract_tag_name(*pos, end, tag_name, sizeof(tag_name));
    
    if (!tag) {
        return NULL;
    }
    
    if (is_jsx_component_tag(tag)) {
        // Uppercase tag -> JSX component
        return parse_jsx_component(input, pos, end);
    } else if (is_html_element_tag(tag)) {
        // Lowercase tag -> HTML element
        return parse_html_element(input, pos, end);
    }
    
    return NULL;
}

// Parse MDX content with mixed markdown, HTML, and JSX
static Element* parse_mdx_content(Input* input, const char* content) {
    Element* root = input_create_element(input, "mdx_document");
    Element* body = input_create_element(input, "body");
    
    const char* pos = content;
    const char* end = content + strlen(content);
    const char* text_start = pos;
    
    while (pos < end) {
        if (*pos == '<') {
            // Found a potential element
            
            // First, process any preceding markdown text
            if (pos > text_start) {
                size_t text_len = pos - text_start;
                char* text_buffer = (char*)malloc(text_len + 1);
                if (text_buffer) {
                    strncpy(text_buffer, text_start, text_len);
                    text_buffer[text_len] = '\0';
                    
                    // Parse the text as markdown
                    Item markdown_item = input_markup(input, text_buffer);
                    if (markdown_item.item != ITEM_NULL && get_type_id(markdown_item) == LMD_TYPE_ELEMENT) {
                        // Add markdown content to body as an attribute
                        input_add_attribute_item_to_element(input, body, "markdown_content", markdown_item);
                    }
                    
                    free(text_buffer);
                }
            }
            
            // Parse the element (JSX or HTML)
            Element* element = parse_mdx_element(input, &pos, end);
            if (element) {
                Item element_item = {.element = element};
                // Add element as attribute for now
                static int element_count = 0;
                char attr_name[32];
                snprintf(attr_name, sizeof(attr_name), "element_%d", element_count++);
                input_add_attribute_item_to_element(input, body, attr_name, element_item);
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
        char* text_buffer = (char*)malloc(text_len + 1);
        if (text_buffer) {
            strncpy(text_buffer, text_start, text_len);
            text_buffer[text_len] = '\0';
            
            // Parse the text as markdown
            Item markdown_item = input_markup(input, text_buffer);
            if (markdown_item.item != ITEM_NULL && get_type_id(markdown_item) == LMD_TYPE_ELEMENT) {
                // Add final markdown content to body
                input_add_attribute_item_to_element(input, body, "final_markdown", markdown_item);
            }
            
            free(text_buffer);
        }
    }
    
    // Add body to root
    Item body_item = {.element = body};
    input_add_attribute_item_to_element(input, root, "content", body_item);
    
    return root;
}

// Enhanced MDX document creation with proper parsing
static Element* create_mdx_document(Input* input, const char* content) {
    return parse_mdx_content(input, content);
}

// Main MDX parsing function
void parse_mdx(Input* input, const char* mdx_string) {
    if (!mdx_string || !input) return;
    
    Element* root = create_mdx_document(input, mdx_string);
    if (root) {
        input->root = (Item){.element = root};
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
