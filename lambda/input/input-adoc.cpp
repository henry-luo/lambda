#include "input.hpp"
#include "../../lib/memtrack.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"

using namespace lambda;

// Forward declarations
static Item parse_asciidoc_content(Input *input, char** lines, int line_count);
static Item parse_asciidoc_block(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_asciidoc_inline(Input *input, const char* text);

// Use common utility functions from input.c
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define split_lines input_split_lines
#define free_lines input_free_lines

// Local helper functions to replace macros
static inline String* create_string(Input* input, const char* str) {
    MarkBuilder builder(input);
    return builder.createString(str);
}

static inline Element* create_asciidoc_element(Input* input, const char* tag_name) {
    MarkBuilder builder(input);
    return builder.element(tag_name).final().element;
}

static inline void add_attribute_to_element(Input* input, Element* element, const char* attr_name, const char* attr_value) {
    MarkBuilder builder(input);
    String* key = builder.createString(attr_name);
    String* value = builder.createString(attr_value);
    if (!key || !value) return;
    Item lambda_value = {.item = s2it(value)};
    builder.putToElement(element, key, lambda_value);
}

// AsciiDoc specific parsing functions
static bool is_asciidoc_heading(const char* line) {
    int eq_count = count_leading_chars(line, '=');
    return eq_count >= 1 && eq_count <= 6 &&
           (line[eq_count] == '\0' || line[eq_count] == ' ');
}

static bool is_listing_block_start(const char* line) {
    return strncmp(line, "----", 4) == 0 && strlen(line) >= 4;
}

static bool is_admonition_block(const char* line) {
    return strncmp(line, "NOTE:", 5) == 0 ||
           strncmp(line, "TIP:", 4) == 0 ||
           strncmp(line, "IMPORTANT:", 10) == 0 ||
           strncmp(line, "WARNING:", 8) == 0 ||
           strncmp(line, "CAUTION:", 8) == 0;
}

static bool is_table_start(const char* line) {
    return strncmp(line, "|===", 4) == 0;
}

static bool is_list_item(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (trimmed && strncmp(trimmed, "* ", 2) == 0);
    if (trimmed) mem_free(trimmed);
    return result;
}

static Item parse_asciidoc_heading(Input *input, const char* line) {
    if (!is_asciidoc_heading(line)) return {.item = ITEM_NULL};

    int eq_count = count_leading_chars(line, '=');

    // Skip equals and whitespace
    const char* content_start = line + eq_count;
    while (*content_start && *content_start == ' ') {
        content_start++;
    }

    // Create header element
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", eq_count);
    Element* header = create_asciidoc_element(input, tag_name);
    if (!header) return {.item = ITEM_NULL};

    // Add level attribute (required by schema)
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", eq_count);
    add_attribute_to_element(input, header, "level", level_str);

    // Add content if present
    if (*content_start != '\0') {
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            Item inline_content = parse_asciidoc_inline(input, content);
            // Add as child using list_push
            list_push((List*)header, inline_content);
            ((TypeElmt*)header->type)->content_length++;
        }
        if (content) mem_free(content);
    }

    return {.item = (uint64_t)header};
}

static Item parse_asciidoc_paragraph(Input *input, const char* text) {
    Element* paragraph = create_asciidoc_element(input, "p");
    if (!paragraph) return {.item = ITEM_NULL};

    // Parse inline content
    Item inline_content = parse_asciidoc_inline(input, text);
    list_push((List*)paragraph, inline_content);
    ((TypeElmt*)paragraph->type)->content_length++;

    return {.item = (uint64_t)paragraph};
}

static Item parse_asciidoc_listing_block(Input *input, char** lines, int* current_line, int total_lines) {
    int start_line = *current_line;
    (*current_line)++; // Skip opening ----

    // Find closing ----
    int end_line = -1;
    for (int i = *current_line; i < total_lines; i++) {
        if (is_listing_block_start(lines[i])) {
            end_line = i;
            break;
        }
    }

    if (end_line == -1) {
        // No closing delimiter, treat as regular paragraph
        *current_line = start_line;
        return parse_asciidoc_paragraph(input, lines[start_line]);
    }

    // Create pre block
    Element* pre_block = create_asciidoc_element(input, "pre");
    if (!pre_block) return {.item = ITEM_NULL};

    Element* code_block = create_asciidoc_element(input, "code");
    if (!code_block) return {.item = ITEM_NULL};

    // Concatenate all lines between delimiters
    int content_lines = end_line - *current_line;
    if (content_lines > 0) {
        size_t total_len = 0;
        for (int i = *current_line; i < end_line; i++) {
            total_len += strlen(lines[i]) + 1; // +1 for newline
        }

        char* content = (char*)mem_alloc(total_len + 1, MEM_CAT_INPUT_ADOC);
        content[0] = '\0';

        for (int i = *current_line; i < end_line; i++) {
            strcat(content, lines[i]);
            if (i < end_line - 1) strcat(content, "\n");
        }

        String* content_str = create_string(input, content);
        if (content_str) {
            list_push((List*)code_block, {.item = s2it(content_str)});
            ((TypeElmt*)code_block->type)->content_length++;
        }

        mem_free(content);
    }

    // Add code block to pre block
    list_push((List*)pre_block, {.item = (uint64_t)code_block});
    ((TypeElmt*)pre_block->type)->content_length++;

    *current_line = end_line + 1; // Skip closing ----
    return {.item = (uint64_t)pre_block};
}

static Item parse_asciidoc_list(Input *input, char** lines, int* current_line, int total_lines) {
    Element* list_elem = create_asciidoc_element(input, "ul");
    if (!list_elem) return {.item = ITEM_NULL};

    while (*current_line < total_lines && is_list_item(lines[*current_line])) {
        const char* line = lines[*current_line];
        char* trimmed = trim_whitespace(line);

        if (trimmed && strncmp(trimmed, "* ", 2) == 0) {
            Element* list_item = create_asciidoc_element(input, "li");
            if (list_item) {
                // Skip "* " and parse the rest as inline content
                const char* content = trimmed + 2;
                Item inline_content = parse_asciidoc_inline(input, content);
                if (inline_content .item != ITEM_NULL) {
                    list_push((List*)list_item, inline_content);
                    ((TypeElmt*)list_item->type)->content_length++;
                }
                list_push((List*)list_elem, {.item = (uint64_t)list_item});
                ((TypeElmt*)list_elem->type)->content_length++;
            }
        }

        if (trimmed) mem_free(trimmed);
        (*current_line)++;
    }

    return {.item = (uint64_t)list_elem};
}

static Item parse_asciidoc_admonition(Input *input, const char* line) {
    Element* admonition = create_asciidoc_element(input, "div");
    if (!admonition) return {.item = ITEM_NULL};

    const char* content = NULL;
    const char* type = NULL;

    if (strncmp(line, "NOTE:", 5) == 0) {
        type = "note";
        content = line + 5;
    } else if (strncmp(line, "TIP:", 4) == 0) {
        type = "tip";
        content = line + 4;
    } else if (strncmp(line, "IMPORTANT:", 10) == 0) {
        type = "important";
        content = line + 10;
    } else if (strncmp(line, "WARNING:", 8) == 0) {
        type = "warning";
        content = line + 8;
    } else if (strncmp(line, "CAUTION:", 8) == 0) {
        type = "caution";
        content = line + 8;
    }

    if (type) {
        add_attribute_to_element(input, admonition, "class", type);

        // Skip whitespace after colon
        while (*content && *content == ' ') content++;

        if (*content) {
            Item inline_content = parse_asciidoc_inline(input, content);
            if (inline_content .item != ITEM_NULL) {
                list_push((List*)admonition, inline_content);
                ((TypeElmt*)admonition->type)->content_length++;
            }
        }
    }

    return {.item = (uint64_t)admonition};
}

static Item parse_asciidoc_table(Input *input, char** lines, int* current_line, int total_lines) {
    (*current_line)++; // Skip opening |===

    Element* table = create_asciidoc_element(input, "table");
    if (!table) return {.item = ITEM_NULL};

    Element* tbody = create_asciidoc_element(input, "tbody");
    if (!tbody) return {.item = ITEM_NULL};

    bool header_parsed = false;
    Element* thead = NULL;

    while (*current_line < total_lines) {
        const char* line = lines[*current_line];

        // Check for table end
        if (strncmp(line, "|===", 4) == 0) {
            (*current_line)++; // Skip closing |===
            break;
        }

        // Skip empty lines
        if (is_empty_line(line)) {
            (*current_line)++;
            continue;
        }

        // Parse table row
        if (line[0] == '|') {
            Element* row = create_asciidoc_element(input, "tr");
            if (!row) {
                (*current_line)++;
                continue;
            }

            // Split line by |
            const char* ptr = line + 1; // Skip first |
            const char* cell_start = ptr;

            while (*ptr) {
                if (*ptr == '|' || *(ptr + 1) == '\0') {
                    // End of cell
                    int cell_len = ptr - cell_start;
                    if (*(ptr + 1) == '\0' && *ptr != '|') {
                        cell_len++; // Include last character if not |
                    }

                    char* cell_text = (char*)mem_alloc(cell_len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(cell_text, cell_start, cell_len);
                    cell_text[cell_len] = '\0';

                    char* trimmed_cell = trim_whitespace(cell_text);
                    mem_free(cell_text);

                    // Create cell element
                    const char* cell_tag = (!header_parsed) ? "th" : "td";
                    Element* cell = create_asciidoc_element(input, cell_tag);
                    if (cell && trimmed_cell && strlen(trimmed_cell) > 0) {
                        Item cell_content = parse_asciidoc_inline(input, trimmed_cell);
                        if (cell_content .item != ITEM_NULL) {
                            list_push((List*)cell, cell_content);
                            ((TypeElmt*)cell->type)->content_length++;
                        }
                        list_push((List*)row, {.item = (uint64_t)cell});
                        ((TypeElmt*)row->type)->content_length++;
                    }

                    if (trimmed_cell) mem_free(trimmed_cell);

                    if (*ptr == '|') {
                        ptr++;
                        cell_start = ptr;
                    } else {
                        break;
                    }
                } else {
                    ptr++;
                }
            }

            // Add row to appropriate section
            if (!header_parsed) {
                if (!thead) {
                    thead = create_asciidoc_element(input, "thead");
                }
                if (thead) {
                    list_push((List*)thead, {.item = (uint64_t)row});
                    ((TypeElmt*)thead->type)->content_length++;
                }
                header_parsed = true;
            } else {
                list_push((List*)tbody, {.item = (uint64_t)row});
                ((TypeElmt*)tbody->type)->content_length++;
            }
        }

        (*current_line)++;
    }

    // Add sections to table
    if (thead && ((TypeElmt*)thead->type)->content_length > 0) {
        list_push((List*)table, {.item = (uint64_t)thead});
        ((TypeElmt*)table->type)->content_length++;
    }

    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, {.item = (uint64_t)tbody});
        ((TypeElmt*)table->type)->content_length++;
    }

    return {.item = (uint64_t)table};
}

static Item parse_asciidoc_inline(Input *input, const char* text) {
    if (!text || strlen(text) == 0) return {.item = ITEM_NULL};

    // Check if text contains any formatting characters
    bool has_formatting = false;
    const char* check_ptr = text;
    while (*check_ptr) {
        if (*check_ptr == '*' || *check_ptr == '_' || *check_ptr == '`' ||
            strncmp(check_ptr, "http://", 7) == 0 || strncmp(check_ptr, "https://", 8) == 0) {
            has_formatting = true;
            break;
        }
        check_ptr++;
    }

    // If no formatting, just return the text as a string properly boxed as Item
    if (!has_formatting) {
        return {.item = s2it(create_string(input, text))};
    }

    Element* container = create_asciidoc_element(input, "span");
    if (!container) return {.item = s2it(create_string(input, text))};

    const char* ptr = text;
    const char* start = text;

    while (*ptr) {
        if (*ptr == '*' && ptr[1] != '\0' && ptr != text) {  // Don't match at start
            // Bold text - find closing *
            const char* bold_start = ptr + 1;
            const char* bold_end = strchr(bold_start, '*');
            if (bold_end && bold_end > bold_start) {
                // Add text before bold
                if (ptr > start) {
                    int len = ptr - start;
                    char* before_text = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(before_text, start, len);
                    before_text[len] = '\0';
                    String* before_str = create_string(input, before_text);
                    if (before_str) {
                        list_push((List*)container, {.item = s2it(before_str)});
                        ((TypeElmt*)container->type)->content_length++;
                    }
                    mem_free(before_text);
                }

                // Create bold element
                Element* bold = create_asciidoc_element(input, "strong");
                if (bold) {
                    int bold_len = bold_end - bold_start;
                    char* bold_text = (char*)mem_alloc(bold_len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(bold_text, bold_start, bold_len);
                    bold_text[bold_len] = '\0';
                    String* bold_str = create_string(input, bold_text);
                    if (bold_str) {
                        list_push((List*)bold, {.item = s2it(bold_str)});
                        ((TypeElmt*)bold->type)->content_length++;
                    }
                    list_push((List*)container, {.item = (uint64_t)bold});
                    ((TypeElmt*)container->type)->content_length++;
                    mem_free(bold_text);
                }

                ptr = bold_end + 1;
                start = ptr;
                continue;
            }
        } else if (*ptr == '_' && ptr[1] != '\0' && ptr != text) {  // Don't match at start
            // Italic text - find closing _
            const char* italic_start = ptr + 1;
            const char* italic_end = strchr(italic_start, '_');
            if (italic_end && italic_end > italic_start) {
                // Add text before italic
                if (ptr > start) {
                    int len = ptr - start;
                    char* before_text = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(before_text, start, len);
                    before_text[len] = '\0';
                    String* before_str = create_string(input, before_text);
                    if (before_str) {
                        list_push((List*)container, {.item = s2it(before_str)});
                        ((TypeElmt*)container->type)->content_length++;
                    }
                    mem_free(before_text);
                }

                // Create italic element
                Element* italic = create_asciidoc_element(input, "em");
                if (italic) {
                    int italic_len = italic_end - italic_start;
                    char* italic_text = (char*)mem_alloc(italic_len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(italic_text, italic_start, italic_len);
                    italic_text[italic_len] = '\0';
                    String* italic_str = create_string(input, italic_text);
                    if (italic_str) {
                        list_push((List*)italic, {.item = s2it(italic_str)});
                        ((TypeElmt*)italic->type)->content_length++;
                    }
                    list_push((List*)container, {.item = (uint64_t)italic});
                    ((TypeElmt*)container->type)->content_length++;
                    mem_free(italic_text);
                }

                ptr = italic_end + 1;
                start = ptr;
                continue;
            }
        } else if (*ptr == '`' && ptr[1] != '\0') {
            // Inline code - find closing `
            const char* code_start = ptr + 1;
            const char* code_end = strchr(code_start, '`');
            if (code_end && code_end > code_start) {
                // Add text before code
                if (ptr > start) {
                    int len = ptr - start;
                    char* before_text = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(before_text, start, len);
                    before_text[len] = '\0';
                    String* before_str = create_string(input, before_text);
                    if (before_str) {
                        list_push((List*)container, {.item = s2it(before_str)});
                        ((TypeElmt*)container->type)->content_length++;
                    }
                    mem_free(before_text);
                }

                // Create code element
                Element* code = create_asciidoc_element(input, "code");
                if (code) {
                    int code_len = code_end - code_start;
                    char* code_text = (char*)mem_alloc(code_len + 1, MEM_CAT_INPUT_ADOC);
                    strncpy(code_text, code_start, code_len);
                    code_text[code_len] = '\0';
                    String* code_str = create_string(input, code_text);
                    if (code_str) {
                        list_push((List*)code, {.item = s2it(code_str)});
                        ((TypeElmt*)code->type)->content_length++;
                    }
                    list_push((List*)container, {.item = (uint64_t)code});
                    ((TypeElmt*)container->type)->content_length++;
                    mem_free(code_text);
                }

                ptr = code_end + 1;
                start = ptr;
                continue;
            }
        }

        ptr++;
    }

    // Add remaining text
    if (ptr > start) {
        int len = ptr - start;
        char* remaining_text = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ADOC);
        strncpy(remaining_text, start, len);
        remaining_text[len] = '\0';
        String* remaining_str = create_string(input, remaining_text);
        if (remaining_str) {
            list_push((List*)container, {.item = s2it(remaining_str)});
            ((TypeElmt*)container->type)->content_length++;
        }
        mem_free(remaining_text);
    }

    // If container has only one child, return the child directly
    if (((TypeElmt*)container->type)->content_length == 1) {
        List* container_list = (List*)container;
        return container_list->items[0];
    }

    // If container is empty, return a simple string
    if (((TypeElmt*)container->type)->content_length == 0) {
        return {.item = s2it(create_string(input, text))};
    }

    return {.item = (uint64_t)container};
}

static Item parse_asciidoc_block(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines) return {.item = ITEM_NULL};

    const char* line = lines[*current_line];

    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return {.item = ITEM_NULL};
    }

    // Check for different block types
    if (is_asciidoc_heading(line)) {
        Item result = parse_asciidoc_heading(input, line);
        (*current_line)++;
        return result;
    }

    if (is_listing_block_start(line)) {
        return parse_asciidoc_listing_block(input, lines, current_line, total_lines);
    }

    if (is_list_item(line)) {
        return parse_asciidoc_list(input, lines, current_line, total_lines);
    }

    if (is_admonition_block(line)) {
        Item result = parse_asciidoc_admonition(input, line);
        (*current_line)++;
        return result;
    }

    if (is_table_start(line)) {
        return parse_asciidoc_table(input, lines, current_line, total_lines);
    }

    // Default: treat as paragraph
    Item result = parse_asciidoc_paragraph(input, line);
    (*current_line)++;
    return result;
}

static Item parse_asciidoc_content(Input *input, char** lines, int line_count) {
    // Create the root document element according to schema
    Element* doc = create_asciidoc_element(input, "doc");
    if (!doc) return {.item = ITEM_NULL};

    // Add version attribute to doc (required by schema)
    add_attribute_to_element(input, doc, "version", "1.0");

    // Create meta element for metadata (required by schema)
    Element* meta = create_asciidoc_element(input, "meta");
    if (!meta) return {.item = (uint64_t)doc};

    // Add default metadata
    add_attribute_to_element(input, meta, "title", "AsciiDoc Document");
    add_attribute_to_element(input, meta, "language", "en");

    // Add meta to doc
    list_push((List*)doc, {.item = (uint64_t)meta});
    ((TypeElmt*)doc->type)->content_length++;

    // Create body element for content (required by schema)
    Element* body = create_asciidoc_element(input, "body");
    if (!body) return {.item = (uint64_t)doc};

    int current_line = 0;
    while (current_line < line_count) {
        Item block = parse_asciidoc_block(input, lines, &current_line, line_count);
        if (block .item != ITEM_NULL) {
            list_push((List*)body, block);
            ((TypeElmt*)body->type)->content_length++;
        }

        // Safety check to prevent infinite loops
        if (current_line >= line_count) break;
    }

    // Add body to doc
    list_push((List*)doc, {.item = (uint64_t)body});
    ((TypeElmt*)doc->type)->content_length++;

    return {.item = (uint64_t)doc};
}

void parse_asciidoc(Input* input, const char* asciidoc_string) {
    if (!input || !asciidoc_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    // create unified InputContext with source tracking
    InputContext ctx(input, asciidoc_string, strlen(asciidoc_string));

    // Split input into lines for processing
    int line_count;
    char** lines = split_lines(asciidoc_string, &line_count);

    if (!lines || line_count == 0) {
        ctx.addError(ctx.tracker.location(), "Failed to split AsciiDoc content into lines");
        input->root = {.item = ITEM_NULL};
        return;
    }

    // Parse content using the full AsciiDoc parser
    input->root = parse_asciidoc_content(input, lines, line_count);

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }

    // Clean up lines
    free_lines(lines, line_count);
}
