#include "input.hpp"
#include "../../lib/memtrack.h"
#include "../windows_compat.h"  // For Windows compatibility functions like strndup
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../mark_builder.hpp"
#include <string.h>
#include <ctype.h>

using namespace lambda;

// Forward declarations
static Element* parse_inline_text(Input* input, const char* text);

// Local helper functions to replace macros
static inline String* create_string(Input* input, const char* str) {
    MarkBuilder builder(input);
    return builder.createString(str);
}

static inline Element* create_org_element(Input* input, const char* tag_name) {
    MarkBuilder builder(input);
    return builder.element(tag_name).final().element;
}

// Helper: add plain text to container
static void add_plain_text(Input* input, Element* container, const char* start, const char* end) {
    if (end <= start) return;

    size_t len = end - start;
    char* text_copy = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ORG);
    if (!text_copy) return;

    strncpy(text_copy, start, len);
    text_copy[len] = '\0';

    Element* plain = create_org_element(input, "plain_text");
    if (plain) {
        String* str = create_string(input, text_copy);
        if (str) {
            list_push((List*)plain, {.item = s2it(str)});
            ((TypeElmt*)plain->type)->content_length++;
            list_push((List*)container, {.item = (uint64_t)plain});
            ((TypeElmt*)container->type)->content_length++;
        }
    }
    mem_free(text_copy);
}

// Helper: parse simple inline formatting (bold, italic, code, etc.)
static const char* parse_simple_format(Input* input, Element* container, const char* current, char marker) {
    const char* format_type = "plain_text";
    switch (marker) {
        case '*': format_type = "bold"; break;
        case '/': format_type = "italic"; break;
        case '=': format_type = "verbatim"; break;
        case '~': format_type = "code"; break;
        case '+': format_type = "strikethrough"; break;
        case '_': format_type = "underline"; break;
    }

    current++; // skip opening marker
    const char* content_start = current;

    // find closing marker
    while (*current && *current != marker) {
        current++;
    }

    if (*current == marker && current > content_start) {
        size_t len = current - content_start;
        char* content = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ORG);
        if (content) {
            strncpy(content, content_start, len);
            content[len] = '\0';

            Element* formatted = create_org_element(input, format_type);
            if (formatted) {
                String* str = create_string(input, content);
                if (str) {
                    list_push((List*)formatted, {.item = s2it(str)});
                    ((TypeElmt*)formatted->type)->content_length++;
                    list_push((List*)container, {.item = (uint64_t)formatted});
                    ((TypeElmt*)container->type)->content_length++;
                }
            }
            mem_free(content);
        }
        return current + 1; // skip closing marker
    }

    return NULL; // no matching closer
}

// Helper: parse math expression (shared logic for $...$ and \(...\))
static const char* parse_math_expr(Input* input, Element* container, const char* current,
                                   const char* open_delim, const char* close_delim, bool is_display) {
    int open_len = strlen(open_delim);
    int close_len = strlen(close_delim);

    current += open_len; // skip opening
    const char* content_start = current;

    // find closing delimiter
    while (*current) {
        if (strncmp(current, close_delim, close_len) == 0) {
            break;
        }
        current++;
    }

    if (strncmp(current, close_delim, close_len) != 0) {
        return NULL; // no matching closer
    }

    size_t len = current - content_start;
    char* math_content = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_ORG);
    if (!math_content) return current + close_len;

    strncpy(math_content, content_start, len);
    math_content[len] = '\0';

    // parse math using input-math.cpp
    Input* math_input = InputManager::create_input((Url*)input->url);
    if (math_input) {
        parse_math(math_input, math_content, "latex");

        if (math_input->root.item != ITEM_ERROR && math_input->root.item != ITEM_NULL) {
            Element* math_elem = create_org_element(input, is_display ? "display_math" : "inline_math");
            if (math_elem) {
                // add raw content
                String* raw = create_string(input, math_content);
                if (raw) {
                    Element* raw_elem = create_org_element(input, "raw_content");
                    if (raw_elem) {
                        list_push((List*)raw_elem, {.item = s2it(raw)});
                        ((TypeElmt*)raw_elem->type)->content_length++;
                        list_push((List*)math_elem, {.item = (uint64_t)raw_elem});
                        ((TypeElmt*)math_elem->type)->content_length++;
                    }
                }

                // add parsed AST
                Element* ast_elem = create_org_element(input, "math_ast");
                if (ast_elem) {
                    list_push((List*)ast_elem, math_input->root);
                    ((TypeElmt*)ast_elem->type)->content_length++;
                    list_push((List*)math_elem, {.item = (uint64_t)ast_elem});
                    ((TypeElmt*)math_elem->type)->content_length++;
                }

                list_push((List*)container, {.item = (uint64_t)math_elem});
                ((TypeElmt*)container->type)->content_length++;
            }
        }
    }
    mem_free(math_content);

    return current + close_len;
}

// Helper function to count leading stars
static int count_leading_stars(const char* line) {
    int count = 0;
    while (line[count] == '*') {
        count++;
    }
    // Make sure there's a space after the stars for a valid heading
    if (count > 0 && line[count] == ' ') {
        return count;
    }
    return 0;
}

// Helper function to check if line starts with list marker
static bool is_list_item(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check for unordered list markers (-, +, *)
    if ((*line == '-' || *line == '+' || *line == '*') &&
        (line[1] == ' ' || line[1] == '\t')) {
        return true;
    }

    // Check for ordered list (number followed by . or ))
    if (isdigit(*line)) {
        line++;
        while (isdigit(*line)) line++;
        if ((*line == '.' || (*line == ')' && line[1] == ')')) &&
            (line[1] == ' ' || line[1] == '\t')) {
            return true;
        }
    }

    return false;
}

// Helper function to check if line starts with directive
static bool is_directive(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check for #+
    if (line[0] == '#' && line[1] == '+') {
        return true;
    }

    return false;
}

// Helper function to check if line is BEGIN_SRC
static bool is_begin_src(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for BEGIN_SRC
    if (strncmp(line, "BEGIN_SRC", 9) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is END_SRC
static bool is_end_src(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for END_SRC
    if (strncmp(line, "END_SRC", 7) == 0) {
        return true;
    }

    return false;
}

// Helper function to extract language from BEGIN_SRC line
static const char* extract_src_language(const char* line) {
    // Skip to BEGIN_SRC
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+
    line += 9; // skip BEGIN_SRC

    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Return the language (or empty string)
    return line;
}

// Helper function to check if line is BEGIN_QUOTE
static bool is_begin_quote(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for BEGIN_QUOTE
    if (strncmp(line, "BEGIN_QUOTE", 11) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is END_QUOTE
static bool is_end_quote(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for END_QUOTE
    if (strncmp(line, "END_QUOTE", 9) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is BEGIN_EXAMPLE
static bool is_begin_example(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for BEGIN_EXAMPLE
    if (strncmp(line, "BEGIN_EXAMPLE", 13) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is END_EXAMPLE
static bool is_end_example(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for END_EXAMPLE
    if (strncmp(line, "END_EXAMPLE", 11) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is BEGIN_VERSE
static bool is_begin_verse(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for BEGIN_VERSE
    if (strncmp(line, "BEGIN_VERSE", 11) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is END_VERSE
static bool is_end_verse(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for END_VERSE
    if (strncmp(line, "END_VERSE", 9) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is BEGIN_CENTER
static bool is_begin_center(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for BEGIN_CENTER
    if (strncmp(line, "BEGIN_CENTER", 12) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is END_CENTER
static bool is_end_center(const char* line) {
    if (!is_directive(line)) return false;

    // Skip leading whitespace and #+
    while (*line == ' ' || *line == '\t') line++;
    line += 2; // skip #+

    // Check for END_CENTER
    if (strncmp(line, "END_CENTER", 10) == 0) {
        return true;
    }

    return false;
}

// Helper function to check if line is a drawer start (:NAME:)
static bool is_drawer_start(const char* line, char* drawer_name, size_t name_size) {
    if (!line || line[0] != ':') return false;

    const char* end_colon = strchr(line + 1, ':');
    if (!end_colon) return false;

    // Check if it's just :END: (which is drawer end, not start)
    if (strncmp(line, ":END:", 5) == 0) return false;

    // Extract drawer name
    size_t len = end_colon - (line + 1);
    if (len == 0 || len >= name_size) return false;

    strncpy(drawer_name, line + 1, len);
    drawer_name[len] = '\0';

    // Check if there's only whitespace after the closing colon
    const char* after = end_colon + 1;
    while (*after && (*after == ' ' || *after == '\t')) after++;

    return (*after == '\0' || *after == '\n');
}

// Helper function to check if line is drawer end (:END:)
static bool is_drawer_end(const char* line) {
    if (!line) return false;

    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    if (strncmp(line, ":END:", 5) != 0) return false;

    // Check if there's only whitespace after :END:
    line += 5;
    while (*line && (*line == ' ' || *line == '\t')) line++;

    return (*line == '\0' || *line == '\n');
}

// Helper function to check if line contains a scheduling keyword (SCHEDULED, DEADLINE, CLOSED)
static bool is_scheduling_line(const char* line, char** keyword, char** timestamp) {
    if (!line) return false;

    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    const char* keywords[] = {"SCHEDULED:", "DEADLINE:", "CLOSED:"};
    const char* keyword_names[] = {"scheduled", "deadline", "closed"};

    for (int i = 0; i < 3; i++) {
        int len = strlen(keywords[i]);
        if (strncmp(line, keywords[i], len) == 0) {
            *keyword = strdup(keyword_names[i]);

            // Find timestamp after the keyword
            const char* ts_start = line + len;
            while (*ts_start == ' ' || *ts_start == '\t') ts_start++;

            if (*ts_start == '<' || *ts_start == '[') {
                const char* ts_end = strchr(ts_start + 1, (*ts_start == '<') ? '>' : ']');
                if (ts_end) {
                    *timestamp = strndup(ts_start, ts_end - ts_start + 1);
                    return true;
                }
            }

            mem_free(*keyword);
            return false;
        }
    }

    return false;
}

// Helper function to parse timestamp in text (e.g., <2024-01-15 Mon 10:00>)
static Element* parse_timestamp(Input* input, const char* timestamp_str) {
    if (!timestamp_str || (timestamp_str[0] != '<' && timestamp_str[0] != '[')) return NULL;

    Element* timestamp = create_org_element(input, "timestamp");
    if (!timestamp) return NULL;

    // Add the timestamp content
    String* ts_string = create_string(input, timestamp_str);
    if (ts_string) {
        list_push((List*)timestamp, {.item = s2it(ts_string)});
        ((TypeElmt*)timestamp->type)->content_length++;
    }

    return timestamp;
}

// Helper function to create scheduling element
static Element* create_scheduling(Input* input, const char* keyword, const char* timestamp_str) {
    Element* scheduling = create_org_element(input, "scheduling");
    if (!scheduling) return NULL;

    // Add keyword element
    Element* keyword_elem = create_org_element(input, "keyword");
    if (keyword_elem) {
        String* keyword_string = create_string(input, keyword);
        if (keyword_string) {
            list_push((List*)keyword_elem, {.item = s2it(keyword_string)});
            ((TypeElmt*)keyword_elem->type)->content_length++;
            list_push((List*)scheduling, {.item = (uint64_t)keyword_elem});
            ((TypeElmt*)scheduling->type)->content_length++;
        }
    }

    // Add timestamp element
    Element* timestamp = parse_timestamp(input, timestamp_str);
    if (timestamp) {
        list_push((List*)scheduling, {.item = (uint64_t)timestamp});
        ((TypeElmt*)scheduling->type)->content_length++;
    }

    return scheduling;
}

// Helper function to check if line is a BEGIN block
static bool is_begin_block(const char* line, const char* block_type) {
    if (!is_directive(line)) return false;

    // Check for #+BEGIN_BLOCKTYPE pattern
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "#+BEGIN_%s", block_type);

    // Convert to uppercase for comparison
    char line_upper[256];
    size_t line_len = strlen(line);
    if (line_len >= sizeof(line_upper)) return false;

    for (size_t i = 0; i < line_len && i < sizeof(line_upper) - 1; i++) {
        line_upper[i] = toupper(line[i]);
    }
    line_upper[line_len] = '\0';

    return strstr(line_upper, pattern) == line_upper;
}

// Helper function to check if line is an END block
static bool is_end_block(const char* line, const char* block_type) {
    if (!is_directive(line)) return false;

    // Check for #+END_BLOCKTYPE pattern
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "#+END_%s", block_type);

    // Convert to uppercase for comparison
    char line_upper[256];
    size_t line_len = strlen(line);
    if (line_len >= sizeof(line_upper)) return false;

    for (size_t i = 0; i < line_len && i < sizeof(line_upper) - 1; i++) {
        line_upper[i] = toupper(line[i]);
    }
    line_upper[line_len] = '\0';

    return strstr(line_upper, pattern) == line_upper;
}

// Helper function to check if line is a drawer start
static bool is_drawer_start(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check if line starts with : and ends with :
    if (*line != ':') return false;

    const char* end = line + strlen(line) - 1;
    while (end > line && (*end == ' ' || *end == '\t' || *end == '\n')) end--;

    return *end == ':' && end > line;
}

// Helper function to parse heading with TODO keywords and tags
static void parse_heading_advanced(const char* title, char** todo, char** actual_title, char** tags) {
    *todo = NULL;
    *actual_title = NULL;
    *tags = NULL;

    if (!title) return;

    // Skip leading whitespace
    while (*title == ' ' || *title == '\t') title++;

    const char* start = title;
    const char* current = title;

    // Check for TODO keywords
    const char* todo_keywords[] = {"TODO", "DONE", "NEXT", "WAITING", "CANCELLED", NULL};
    for (int i = 0; todo_keywords[i]; i++) {
        size_t keyword_len = strlen(todo_keywords[i]);
        if (strncmp(current, todo_keywords[i], keyword_len) == 0 &&
            (current[keyword_len] == ' ' || current[keyword_len] == '\t' || current[keyword_len] == '\0')) {
            *todo = strndup(current, keyword_len);
            current += keyword_len;
            while (*current == ' ' || *current == '\t') current++;
            break;
        }
    }

    // Find tags (at the end, format :tag1:tag2:)
    const char* tag_start = strrchr(current, ':');
    const char* title_end = current + strlen(current);

    if (tag_start && tag_start > current) {
        // Check if this looks like tags (ends with :)
        const char* check = title_end - 1;
        while (check > tag_start && (*check == ' ' || *check == '\t' || *check == '\n')) check--;

        if (*check == ':') {
            // Find the start of tags (first : in the sequence)
            const char* tags_start = tag_start;
            while (tags_start > current && *(tags_start - 1) != ' ' && *(tags_start - 1) != '\t') {
                tags_start--;
                if (*tags_start == ':') tag_start = tags_start;
            }

            if (tag_start > current) {
                *tags = strndup(tag_start, title_end - tag_start);
                title_end = tag_start;
                // Remove trailing whitespace from title
                while (title_end > current && (*(title_end - 1) == ' ' || *(title_end - 1) == '\t')) {
                    title_end--;
                }
            }
        }
    }

    // Extract the actual title
    if (title_end > current) {
        *actual_title = strndup(current, title_end - current);
    }
}

// Helper function to create a code block element
static Element* create_code_block(Input* input, const char* language, const char** lines, int line_count) {
    Element* code_block = create_org_element(input, "code_block");
    if (!code_block) return NULL;

    // Add language attribute
    if (language && strlen(language) > 0) {
        String* lang_string = create_string(input, language);
        if (lang_string) {
            Element* lang_elem = create_org_element(input, "language");
            if (lang_elem) {
                list_push((List*)lang_elem, {.item = s2it(lang_string)});
                ((TypeElmt*)lang_elem->type)->content_length++;
                list_push((List*)code_block, {.item = (uint64_t)lang_elem});
                ((TypeElmt*)code_block->type)->content_length++;
            }
        }
    }

    // Add content lines
    for (int i = 0; i < line_count; i++) {
        String* content_string = create_string(input, lines[i]);
        if (content_string) {
            Element* content_elem = create_org_element(input, "content");
            if (content_elem) {
                list_push((List*)content_elem, {.item = s2it(content_string)});
                ((TypeElmt*)content_elem->type)->content_length++;
                list_push((List*)code_block, {.item = (uint64_t)content_elem});
                ((TypeElmt*)code_block->type)->content_length++;
            }
        }
    }

    return code_block;
}

// Helper function to create a quote block element
static Element* create_quote_block(Input* input, const char** lines, int line_count) {
    Element* quote_block = create_org_element(input, "quote_block");
    if (!quote_block) return NULL;

    // Add content lines as paragraphs with inline formatting
    for (int i = 0; i < line_count; i++) {
        if (strlen(lines[i]) == 0) continue; // Skip empty lines

        Element* paragraph = create_org_element(input, "paragraph");
        if (!paragraph) continue;

        // Parse inline formatting in the content
        Element* inline_content = parse_inline_text(input, lines[i]);
        if (inline_content && ((List*)inline_content)->length > 0) {
            // Use the inline content if it has elements
            List* inline_list = (List*)inline_content;
            for (long j = 0; j < inline_list->length; j++) {
                Item inline_item = inline_list->items[j];
                list_push((List*)paragraph, inline_item);
                ((TypeElmt*)paragraph->type)->content_length++;
            }
        } else {
            // Fallback to simple string content
            String* content_string = create_string(input, lines[i]);
            if (content_string) {
                list_push((List*)paragraph, {.item = s2it(content_string)});
                ((TypeElmt*)paragraph->type)->content_length++;
            }
        }

        list_push((List*)quote_block, {.item = (uint64_t)paragraph});
        ((TypeElmt*)quote_block->type)->content_length++;
    }

    return quote_block;
}

// Helper function to create a generic block element (example, verse, center)
static Element* create_generic_block(Input* input, const char* block_type, const char** lines, int line_count, bool preserve_formatting) {
    // Create block element with the specified type
    char block_element_name[64];
    snprintf(block_element_name, sizeof(block_element_name), "%s_block", block_type);

    Element* block = create_org_element(input, block_element_name);
    if (!block) return NULL;

    // Add content lines
    for (int i = 0; i < line_count; i++) {
        if (!preserve_formatting && strlen(lines[i]) == 0) continue; // Skip empty lines for some blocks

        if (preserve_formatting || strcmp(block_type, "example") == 0) {
            // For example and verse blocks, preserve as-is without inline formatting
            String* content_string = create_string(input, lines[i]);
            if (content_string) {
                Element* content_elem = create_org_element(input, "content");
                if (content_elem) {
                    list_push((List*)content_elem, {.item = s2it(content_string)});
                    ((TypeElmt*)content_elem->type)->content_length++;
                    list_push((List*)block, {.item = (uint64_t)content_elem});
                    ((TypeElmt*)block->type)->content_length++;
                }
            }
        } else {
            // For center blocks, parse as paragraph with inline formatting
            Element* paragraph = create_org_element(input, "paragraph");
            if (!paragraph) continue;

            // Parse inline formatting in the content
            Element* inline_content = parse_inline_text(input, lines[i]);
            if (inline_content && ((List*)inline_content)->length > 0) {
                // Use the inline content if it has elements
                List* inline_list = (List*)inline_content;
                for (long j = 0; j < inline_list->length; j++) {
                    Item inline_item = inline_list->items[j];
                    list_push((List*)paragraph, inline_item);
                    ((TypeElmt*)paragraph->type)->content_length++;
                }
            } else {
                // Fallback to simple string content
                String* content_string = create_string(input, lines[i]);
                if (content_string) {
                    list_push((List*)paragraph, {.item = s2it(content_string)});
                    ((TypeElmt*)paragraph->type)->content_length++;
                }
            }

            list_push((List*)block, {.item = (uint64_t)paragraph});
            ((TypeElmt*)block->type)->content_length++;
        }
    }

    return block;
}

// Helper function to create a drawer element
static Element* create_drawer(Input* input, const char* drawer_name, const char** lines, int line_count) {
    Element* drawer = create_org_element(input, "drawer");
    if (!drawer) return NULL;

    // Add drawer name
    String* name_string = create_string(input, drawer_name);
    if (name_string) {
        Element* name_elem = create_org_element(input, "name");
        if (name_elem) {
            list_push((List*)name_elem, {.item = s2it(name_string)});
            ((TypeElmt*)name_elem->type)->content_length++;
            list_push((List*)drawer, {.item = (uint64_t)name_elem});
            ((TypeElmt*)drawer->type)->content_length++;
        }
    }

    // Add content lines (as simple text for now - could be enhanced to parse properties)
    for (int i = 0; i < line_count; i++) {
        if (strlen(lines[i]) == 0) continue; // Skip empty lines

        String* content_string = create_string(input, lines[i]);
        if (content_string) {
            Element* content_elem = create_org_element(input, "content");
            if (content_elem) {
                list_push((List*)content_elem, {.item = s2it(content_string)});
                ((TypeElmt*)content_elem->type)->content_length++;
                list_push((List*)drawer, {.item = (uint64_t)content_elem});
                ((TypeElmt*)drawer->type)->content_length++;
            }
        }
    }

    return drawer;
}

// Helper function to create a directive element
static Element* create_directive(Input* input, const char* line) {
    Element* directive = create_org_element(input, "directive");
    if (!directive) return NULL;

    String* content_string = create_string(input, line);
    if (content_string) {
        list_push((List*)directive, {.item = s2it(content_string)});
        ((TypeElmt*)directive->type)->content_length++;
    }

    return directive;
}

// Helper function to check if line is a footnote definition
static bool is_footnote_definition(const char* line, char** footnote_name, char** footnote_content) {
    if (!line) return false;

    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check for [fn:name] pattern at start of line
    if (line[0] != '[' || line[1] != 'f' || line[2] != 'n' || line[3] != ':') {
        return false;
    }

    const char* name_start = line + 4;
    const char* name_end = name_start;

    // Find end of footnote name (closing ])
    while (*name_end && *name_end != ']') {
        name_end++;
    }

    if (*name_end != ']' || name_end == name_start) return false;

    // Extract footnote name
    size_t name_len = name_end - name_start;
    *footnote_name = (char*)mem_alloc(name_len + 1, MEM_CAT_INPUT_ORG);
    if (!*footnote_name) return false;

    strncpy(*footnote_name, name_start, name_len);
    (*footnote_name)[name_len] = '\0';

    // Skip ] and any whitespace to get to content
    const char* content_start = name_end + 1;
    while (*content_start == ' ' || *content_start == '\t') content_start++;

    // Extract footnote content (rest of the line)
    if (*content_start) {
        *footnote_content = strdup(content_start);
    } else {
        *footnote_content = strdup("");
    }

    return true;
}

// Helper function to create a footnote definition element
static Element* create_footnote_definition(Input* input, const char* name, const char* content) {
    Element* footnote_def = create_org_element(input, "footnote_definition");
    if (!footnote_def) return NULL;

    // Add footnote name
    String* name_string = create_string(input, name);
    if (name_string) {
        Element* name_elem = create_org_element(input, "name");
        if (name_elem) {
            list_push((List*)name_elem, {.item = s2it(name_string)});
            ((TypeElmt*)name_elem->type)->content_length++;
            list_push((List*)footnote_def, {.item = (uint64_t)name_elem});
            ((TypeElmt*)footnote_def->type)->content_length++;
        }
    }

    // Add footnote content (parse as inline text for formatting)
    if (content && strlen(content) > 0) {
        Element* content_elem = create_org_element(input, "content");
        if (content_elem) {
            Element* inline_content = parse_inline_text(input, content);
            if (inline_content && ((List*)inline_content)->length > 0) {
                // Use the inline content if it has elements
                List* inline_list = (List*)inline_content;
                for (long j = 0; j < inline_list->length; j++) {
                    Item inline_item = inline_list->items[j];
                    list_push((List*)content_elem, inline_item);
                    ((TypeElmt*)content_elem->type)->content_length++;
                }
            } else {
                // Fallback to simple string content
                String* content_string = create_string(input, content);
                if (content_string) {
                    list_push((List*)content_elem, {.item = s2it(content_string)});
                    ((TypeElmt*)content_elem->type)->content_length++;
                }
            }

            list_push((List*)footnote_def, {.item = (uint64_t)content_elem});
            ((TypeElmt*)footnote_def->type)->content_length++;
        }
    }

    return footnote_def;
}

// Helper function to create a footnote reference element
static Element* create_footnote_reference(Input* input, const char* name) {
    Element* footnote_ref = create_org_element(input, "footnote_reference");
    if (!footnote_ref) return NULL;

    // Add footnote name
    String* name_string = create_string(input, name);
    if (name_string) {
        Element* name_elem = create_org_element(input, "name");
        if (name_elem) {
            list_push((List*)name_elem, {.item = s2it(name_string)});
            ((TypeElmt*)name_elem->type)->content_length++;
            list_push((List*)footnote_ref, {.item = (uint64_t)name_elem});
            ((TypeElmt*)footnote_ref->type)->content_length++;
        }
    }

    return footnote_ref;
}

// Helper function to create an inline footnote element
static Element* create_inline_footnote(Input* input, const char* name, const char* definition) {
    Element* inline_footnote = create_org_element(input, "inline_footnote");
    if (!inline_footnote) return NULL;

    // Add footnote name (optional, can be empty for anonymous footnotes)
    if (name && strlen(name) > 0) {
        String* name_string = create_string(input, name);
        if (name_string) {
            Element* name_elem = create_org_element(input, "name");
            if (name_elem) {
                list_push((List*)name_elem, {.item = s2it(name_string)});
                ((TypeElmt*)name_elem->type)->content_length++;
                list_push((List*)inline_footnote, {.item = (uint64_t)name_elem});
                ((TypeElmt*)inline_footnote->type)->content_length++;
            }
        }
    }

    // Add footnote definition
    if (definition && strlen(definition) > 0) {
        Element* def_elem = create_org_element(input, "definition");
        if (def_elem) {
            Element* inline_content = parse_inline_text(input, definition);
            if (inline_content && ((List*)inline_content)->length > 0) {
                // Use the inline content if it has elements
                List* inline_list = (List*)inline_content;
                for (long j = 0; j < inline_list->length; j++) {
                    Item inline_item = inline_list->items[j];
                    list_push((List*)def_elem, inline_item);
                    ((TypeElmt*)def_elem->type)->content_length++;
                }
            } else {
                // Fallback to simple string content
                String* def_string = create_string(input, definition);
                if (def_string) {
                    list_push((List*)def_elem, {.item = s2it(def_string)});
                    ((TypeElmt*)def_elem->type)->content_length++;
                }
            }

            list_push((List*)inline_footnote, {.item = (uint64_t)def_elem});
            ((TypeElmt*)inline_footnote->type)->content_length++;
        }
    }

    return inline_footnote;
}

// Helper function to parse inline formatting in text
static Element* parse_inline_text(Input* input, const char* text) {
    if (!text) return NULL;

    Element* text_container = create_org_element(input, "text_content");
    if (!text_container) return NULL;

    const char* current = text;
    const char* start = text;

    while (*current) {
        // Look for inline formatting markers
        if (*current == '*' || *current == '/' || *current == '=' ||
            *current == '~' || *current == '+' || *current == '_') {

            // Add any plain text before the marker
            add_plain_text(input, text_container, start, current);

            // Parse simple format using helper
            const char* marker_start = current;
            const char* next = parse_simple_format(input, text_container, current, *current);

            if (next) {
                // Successfully parsed format
                current = next;
                start = current;
            } else {
                // No matching closing marker, treat as plain text
                current = marker_start + 1;
            }
        } else if (*current == '$') {
            // Handle math expressions $...$ or $$...$$
            add_plain_text(input, text_container, start, current);

            bool is_display_math = (*(current + 1) == '$');
            const char* math_start = current;
            const char* next = parse_math_expr(input, text_container, current,
                                               is_display_math ? "$$" : "$",
                                               is_display_math ? "$$" : "$",
                                               is_display_math);

            if (next) {
                current = next;
                start = current;
            } else {
                // No valid closing delimiter, treat as plain text
                current = math_start + 1;
            }
        } else if (*current == '\\' && (*(current + 1) == '(' || *(current + 1) == '[')) {
            // Handle LaTeX-style math \(...\) or \[...\]
            add_plain_text(input, text_container, start, current);

            bool is_display_math = (*(current + 1) == '[');
            const char* math_start = current;
            const char* next = parse_math_expr(input, text_container, current,
                                               is_display_math ? "\\[" : "\\(",
                                               is_display_math ? "\\]" : "\\)",
                                               is_display_math);

            if (next) {
                current = next;
                start = current;
            } else {
                // No valid closing delimiter, treat as plain text
                current = math_start + 1;
            }
        } else if (*current == '[' && *(current + 1) == '[') {
            // Handle links [[URL][description]] or [[URL]]

            // Add any plain text before the link
            if (current > start) {
                size_t plain_len = current - start;
                char* plain_text = (char*)mem_alloc(plain_len + 1, MEM_CAT_INPUT_ORG);
                if (plain_text) {
                    strncpy(plain_text, start, plain_len);
                    plain_text[plain_len] = '\0';

                    Element* plain = create_org_element(input, "plain_text");
                    if (plain) {
                        String* plain_string = create_string(input, plain_text);
                        if (plain_string) {
                            list_push((List*)plain, {.item = s2it(plain_string)});
                            ((TypeElmt*)plain->type)->content_length++;
                            list_push((List*)text_container, {.item = (uint64_t)plain});
                            ((TypeElmt*)text_container->type)->content_length++;
                        }
                    }
                    mem_free(plain_text);
                }
            }

            // Parse link
            const char* link_start = current;
            current += 2; // skip [[
            const char* url_start = current;

            // Find end of URL (] or ][)
            while (*current && *current != ']') {
                current++;
            }

            if (*current == ']') {
                size_t url_len = current - url_start;
                char* url = (char*)mem_alloc(url_len + 1, MEM_CAT_INPUT_ORG);
                if (url) {
                    strncpy(url, url_start, url_len);
                    url[url_len] = '\0';

                    current++; // skip ]
                    char* description = NULL;

                    // Check for description [description]
                    if (*current == '[') {
                        current++; // skip [
                        const char* desc_start = current;
                        while (*current && *current != ']') {
                            current++;
                        }
                        if (*current == ']') {
                            size_t desc_len = current - desc_start;
                            description = (char*)mem_alloc(desc_len + 1, MEM_CAT_INPUT_ORG);
                            if (description) {
                                strncpy(description, desc_start, desc_len);
                                description[desc_len] = '\0';
                            }
                            current++; // skip ]
                        }
                    }

                    if (*current == ']') {
                        current++; // skip final ]

                        // Create link element
                        Element* link = create_org_element(input, "link");
                        if (link) {
                            Element* url_elem = create_org_element(input, "url");
                            if (url_elem) {
                                String* url_string = create_string(input, url);
                                if (url_string) {
                                    list_push((List*)url_elem, {.item = s2it(url_string)});
                                    ((TypeElmt*)url_elem->type)->content_length++;
                                    list_push((List*)link, {.item = (uint64_t)url_elem});
                                    ((TypeElmt*)link->type)->content_length++;
                                }
                            }

                            if (description) {
                                Element* desc_elem = create_org_element(input, "description");
                                if (desc_elem) {
                                    String* desc_string = create_string(input, description);
                                    if (desc_string) {
                                        list_push((List*)desc_elem, {.item = s2it(desc_string)});
                                        ((TypeElmt*)desc_elem->type)->content_length++;
                                        list_push((List*)link, {.item = (uint64_t)desc_elem});
                                        ((TypeElmt*)link->type)->content_length++;
                                    }
                                }
                            }

                            list_push((List*)text_container, {.item = (uint64_t)link});
                            ((TypeElmt*)text_container->type)->content_length++;
                        }
                        start = current;
                    } else {
                        // Invalid link format, treat as plain text
                        current = link_start + 1;
                    }

                    mem_free(url);
                    mem_free(description);
                }
            } else {
                // No closing ]], treat as plain text
                current = link_start + 1;
            }
        } else if ((*current == '<' || *current == '[') &&
                  (isdigit(*(current + 1)) || *(current + 1) == ' ')) {
            // Handle timestamps <2024-01-15 Mon> or [2024-01-15 Mon]

            // Add any plain text before the timestamp
            if (current > start) {
                size_t plain_len = current - start;
                char* plain_text = (char*)mem_alloc(plain_len + 1, MEM_CAT_INPUT_ORG);
                if (plain_text) {
                    strncpy(plain_text, start, plain_len);
                    plain_text[plain_len] = '\0';

                    Element* plain = create_org_element(input, "plain_text");
                    if (plain) {
                        String* plain_string = create_string(input, plain_text);
                        if (plain_string) {
                            list_push((List*)plain, {.item = s2it(plain_string)});
                            ((TypeElmt*)plain->type)->content_length++;
                            list_push((List*)text_container, {.item = (uint64_t)plain});
                            ((TypeElmt*)text_container->type)->content_length++;
                        }
                    }
                    mem_free(plain_text);
                }
            }

            // Parse timestamp
            char closing_char = (*current == '<') ? '>' : ']';
            const char* ts_start = current;
            current++; // skip opening bracket

            // Find closing bracket
            while (*current && *current != closing_char) {
                current++;
            }

            if (*current == closing_char) {
                current++; // skip closing bracket
                size_t ts_len = current - ts_start;
                char* timestamp_str = (char*)mem_alloc(ts_len + 1, MEM_CAT_INPUT_ORG);
                if (timestamp_str) {
                    strncpy(timestamp_str, ts_start, ts_len);
                    timestamp_str[ts_len] = '\0';

                    Element* timestamp = parse_timestamp(input, timestamp_str);
                    if (timestamp) {
                        list_push((List*)text_container, {.item = (uint64_t)timestamp});
                        ((TypeElmt*)text_container->type)->content_length++;
                    }

                    mem_free(timestamp_str);
                }
                start = current;
            } else {
                // No closing bracket, treat as plain text
                current = ts_start + 1;
            }
        } else if (*current == '[' && *(current + 1) == 'f' &&
                  *(current + 2) == 'n' && *(current + 3) == ':') {
            // Handle footnotes [fn:name], [fn:name:definition], or [fn::definition]

            // Add any plain text before the footnote
            if (current > start) {
                size_t plain_len = current - start;
                char* plain_text = (char*)mem_alloc(plain_len + 1, MEM_CAT_INPUT_ORG);
                if (plain_text) {
                    strncpy(plain_text, start, plain_len);
                    plain_text[plain_len] = '\0';

                    Element* plain = create_org_element(input, "plain_text");
                    if (plain) {
                        String* plain_string = create_string(input, plain_text);
                        if (plain_string) {
                            list_push((List*)plain, {.item = s2it(plain_string)});
                            ((TypeElmt*)plain->type)->content_length++;
                            list_push((List*)text_container, {.item = (uint64_t)plain});
                            ((TypeElmt*)text_container->type)->content_length++;
                        }
                    }
                    mem_free(plain_text);
                }
            }

            // Parse footnote
            const char* footnote_start = current;
            current += 4; // skip [fn:
            const char* name_start = current;

            // Find first : or ] to determine footnote type
            while (*current && *current != ':' && *current != ']') {
                current++;
            }

            if (*current == ':') {
                // This is either [fn:name:definition] or [fn::definition]
                size_t name_len = current - name_start;
                char* name = NULL;
                if (name_len > 0) {
                    name = (char*)mem_alloc(name_len + 1, MEM_CAT_INPUT_ORG);
                    if (name) {
                        strncpy(name, name_start, name_len);
                        name[name_len] = '\0';
                    }
                }

                current++; // skip :
                const char* def_start = current;

                // Find closing ]
                while (*current && *current != ']') {
                    current++;
                }

                if (*current == ']') {
                    size_t def_len = current - def_start;
                    char* definition = NULL;
                    if (def_len > 0) {
                        definition = (char*)mem_alloc(def_len + 1, MEM_CAT_INPUT_ORG);
                        if (definition) {
                            strncpy(definition, def_start, def_len);
                            definition[def_len] = '\0';
                        }
                    }

                    current++; // skip ]

                    // Create inline footnote
                    Element* inline_footnote = create_inline_footnote(input,
                        name ? name : "", definition ? definition : "");
                    if (inline_footnote) {
                        list_push((List*)text_container, {.item = (uint64_t)inline_footnote});
                        ((TypeElmt*)text_container->type)->content_length++;
                    }

                    mem_free(name);
                    mem_free(definition);
                    start = current;
                } else {
                    // No closing ], treat as plain text
                    current = footnote_start + 1;
                    mem_free(name);
                }
            } else if (*current == ']') {
                // This is [fn:name] - a footnote reference
                size_t name_len = current - name_start;
                if (name_len > 0) {
                    char* name = (char*)mem_alloc(name_len + 1, MEM_CAT_INPUT_ORG);
                    if (name) {
                        strncpy(name, name_start, name_len);
                        name[name_len] = '\0';

                        current++; // skip ]

                        // Create footnote reference
                        Element* footnote_ref = create_footnote_reference(input, name);
                        if (footnote_ref) {
                            list_push((List*)text_container, {.item = (uint64_t)footnote_ref});
                            ((TypeElmt*)text_container->type)->content_length++;
                        }

                        mem_free(name);
                        start = current;
                    } else {
                        current = footnote_start + 1;
                    }
                } else {
                    // Empty name, treat as plain text
                    current = footnote_start + 1;
                }
            } else {
                // Invalid footnote format, treat as plain text
                current = footnote_start + 1;
            }
        } else {
            current++;
        }
    }

    // Add any remaining plain text
    if (current > start) {
        size_t plain_len = current - start;
        char* plain_text = (char*)mem_alloc(plain_len + 1, MEM_CAT_INPUT_ORG);
        if (plain_text) {
            strncpy(plain_text, start, plain_len);
            plain_text[plain_len] = '\0';

            Element* plain = create_org_element(input, "plain_text");
            if (plain) {
                String* plain_string = create_string(input, plain_text);
                if (plain_string) {
                    list_push((List*)plain, {.item = s2it(plain_string)});
                    ((TypeElmt*)plain->type)->content_length++;
                    list_push((List*)text_container, {.item = (uint64_t)plain});
                    ((TypeElmt*)text_container->type)->content_length++;
                }
            }
            mem_free(plain_text);
        }
    }

    return text_container;
}

// Helper function to check if line is a table row
static bool is_table_row(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check if line starts with | (table row)
    if (*line == '|') {
        return true;
    }

    return false;
}

// Helper function to check if line is a table separator
static bool is_table_separator(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check if line starts with | and contains mostly - and +
    if (*line == '|') {
        line++;
        while (*line) {
            if (*line != '-' && *line != '+' && *line != '|' &&
                *line != ' ' && *line != '\t') {
                return false;
            }
            line++;
        }
        return true;
    }

    return false;
}

// Helper function to parse table cells from a line
static int parse_table_cells(const char* line, char*** cells) {
    if (!line || !cells) return 0;

    // Skip leading whitespace and |
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '|') line++;

    // Count cells first
    int cell_count = 0;
    const char* temp = line;
    while (*temp) {
        if (*temp == '|') cell_count++;
        temp++;
    }

    if (cell_count == 0) return 0;

    // Allocate cell array
    *cells = (char**)malloc(cell_count * sizeof(char*));
    if (!*cells) return 0;

    // Extract cells
    int current_cell = 0;
    const char* cell_start = line;
    const char* cell_end = line;

    while (*cell_end && current_cell < cell_count) {
        if (*cell_end == '|' || *cell_end == '\0') {
            // Extract cell content
            size_t cell_len = cell_end - cell_start;
            char* cell_content = (char*)mem_alloc(cell_len + 1, MEM_CAT_INPUT_ORG);
            if (cell_content) {
                strncpy(cell_content, cell_start, cell_len);
                cell_content[cell_len] = '\0';

                // Trim whitespace
                char* start = cell_content;
                char* end = cell_content + cell_len - 1;
                while (*start == ' ' || *start == '\t') start++;
                while (end > start && (*end == ' ' || *end == '\t')) end--;
                *(end + 1) = '\0';

                (*cells)[current_cell] = strdup(start);
                mem_free(cell_content);
            }
            current_cell++;
            cell_start = cell_end + 1;
        }
        cell_end++;
    }

    return current_cell;
}

// Helper function to create a table row element
static Element* create_table_row(Input* input, char** cells, int cell_count, bool is_header) {
    Element* row = create_org_element(input, is_header ? "table_header_row" : "table_row");
    if (!row) return NULL;

    for (int i = 0; i < cell_count; i++) {
        if (cells[i]) {
            Element* cell = create_org_element(input, "table_cell");
            if (cell) {
                String* cell_string = create_string(input, cells[i]);
                if (cell_string) {
                    list_push((List*)cell, {.item = s2it(cell_string)});
                    ((TypeElmt*)cell->type)->content_length++;
                    list_push((List*)row, {.item = (uint64_t)cell});
                    ((TypeElmt*)row->type)->content_length++;
                }
            }
        }
    }

    return row;
}

// Helper function to create a table element
static Element* create_table(Input* input, Element** rows, int row_count) {
    Element* table = create_org_element(input, "table");
    if (!table) return NULL;

    for (int i = 0; i < row_count; i++) {
        if (rows[i]) {
            list_push((List*)table, {.item = (uint64_t)rows[i]});
            ((TypeElmt*)table->type)->content_length++;
        }
    }

    return table;
}

// Helper function to create a heading element
static Element* create_heading(Input* input, int level, const char* title) {
    Element* heading = create_org_element(input, "heading");
    if (!heading) return NULL;

    // Add level attribute (we'll store it as a simple numeric string for now)
    char level_str[8];
    snprintf(level_str, sizeof(level_str), "%d", level);
    String* level_string = create_string(input, level_str);
    if (level_string) {
        Element* level_elem = create_org_element(input, "level");
        if (level_elem) {
            list_push((List*)level_elem, {.item = s2it(level_string)});
            ((TypeElmt*)level_elem->type)->content_length++;
            list_push((List*)heading, {.item = (uint64_t)level_elem});
            ((TypeElmt*)heading->type)->content_length++;
        }
    }

    // Parse advanced heading features (TODO, title, tags)
    char* todo = NULL;
    char* actual_title = NULL;
    char* tags = NULL;

    parse_heading_advanced(title, &todo, &actual_title, &tags);

    // Add TODO keyword if present
    if (todo) {
        String* todo_string = create_string(input, todo);
        if (todo_string) {
            Element* todo_elem = create_org_element(input, "todo");
            if (todo_elem) {
                list_push((List*)todo_elem, {.item = s2it(todo_string)});
                ((TypeElmt*)todo_elem->type)->content_length++;
                list_push((List*)heading, {.item = (uint64_t)todo_elem});
                ((TypeElmt*)heading->type)->content_length++;
            }
        }
        mem_free(todo);
    }

    // Add title (use actual_title if parsed, otherwise original title)
    const char* title_to_use = actual_title ? actual_title : title;
    String* title_string = create_string(input, title_to_use);
    if (title_string) {
        Element* title_elem = create_org_element(input, "title");
        if (title_elem) {
            list_push((List*)title_elem, {.item = s2it(title_string)});
            ((TypeElmt*)title_elem->type)->content_length++;
            list_push((List*)heading, {.item = (uint64_t)title_elem});
            ((TypeElmt*)heading->type)->content_length++;
        }
    }

    // Add tags if present
    if (tags) {
        String* tags_string = create_string(input, tags);
        if (tags_string) {
            Element* tags_elem = create_org_element(input, "tags");
            if (tags_elem) {
                list_push((List*)tags_elem, {.item = s2it(tags_string)});
                ((TypeElmt*)tags_elem->type)->content_length++;
                list_push((List*)heading, {.item = (uint64_t)tags_elem});
                ((TypeElmt*)heading->type)->content_length++;
            }
        }
        mem_free(tags);
    }

    if (actual_title) mem_free(actual_title);

    return heading;
}

// Enhanced Org Mode parsing function
void parse_org(Input* input, const char* org_string) {
    if (!org_string || !input) return;

    // create unified InputContext with source tracking
    InputContext ctx(input, org_string, strlen(org_string));

    // Create document structure
    Element* doc = create_org_element(input, "org_document");
    if (!doc) {
        ctx.addError(ctx.tracker.location(), "Failed to create org document element");
        return;
    }

    // Split content into lines for parsing
    const char* line_start = org_string;
    const char* line_end;

    while (*line_start) {
        // Find end of current line
        line_end = line_start;
        while (*line_end && *line_end != '\n') {
            line_end++;
        }

        // Create null-terminated line string
        size_t line_len = line_end - line_start;
        char* line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
        if (!line) break;

        strncpy(line, line_start, line_len);
        line[line_len] = '\0';

        // Skip empty lines for now
        if (line_len > 0 && line[0] != '\0') {
            // Check if this is a code block start
            if (is_begin_src(line)) {
                // Extract language
                const char* language = extract_src_language(line);
                char lang_buffer[64] = {0};
                if (language && strlen(language) > 0) {
                    // Remove trailing whitespace/newline
                    size_t lang_len = strlen(language);
                    while (lang_len > 0 && (language[lang_len-1] == ' ' ||
                           language[lang_len-1] == '\t' || language[lang_len-1] == '\n')) {
                        lang_len--;
                    }
                    if (lang_len > 0 && lang_len < sizeof(lang_buffer)) {
                        strncpy(lang_buffer, language, lang_len);
                        lang_buffer[lang_len] = '\0';
                    }
                }

                // Collect code block lines until END_SRC
                const char** code_lines = (const char**)malloc(1000 * sizeof(char*)); // max 1000 lines
                int code_line_count = 0;

                // Move to next line
                mem_free(line);
                if (*line_end == '\n') {
                    line_start = line_end + 1;
                } else {
                    break;
                }

                // Collect code lines
                while (*line_start && code_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') {
                        line_end++;
                    }

                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;

                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    // Check if this is the end of the code block
                    if (is_end_src(line)) {
                        mem_free(line);
                        break;
                    }

                    // Store the code line
                    code_lines[code_line_count] = line;
                    code_line_count++;

                    // Move to next line
                    if (*line_end == '\n') {
                        line_start = line_end + 1;
                    } else {
                        break;
                    }
                }

                // Create code block element
                Element* code_block = create_code_block(input, lang_buffer[0] ? lang_buffer : NULL,
                                                      code_lines, code_line_count);
                if (code_block) {
                    list_push((List*)doc, {.item = (uint64_t)code_block});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                // Free code lines
                for (int i = 0; i < code_line_count; i++) {
                    mem_free((void*)code_lines[i]);
                }
                mem_free(code_lines);

                // Move to next line
                if (*line_end == '\n') {
                    line_start = line_end + 1;
                } else {
                    break;
                }
                continue;
            }

            // Check if this is a quote block start
            if (is_begin_quote(line)) {
                // Collect quote block lines until END_QUOTE
                const char** quote_lines = (const char**)malloc(1000 * sizeof(char*)); // max 1000 lines
                int quote_line_count = 0;

                // Move to next line
                mem_free(line);
                if (*line_end == '\n') {
                    line_start = line_end + 1;
                } else {
                    break;
                }

                // Collect quote content lines
                while (*line_start && quote_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') {
                        line_end++;
                    }

                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;

                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    // Check if this is the end of the quote block
                    if (is_end_quote(line)) {
                        mem_free(line);
                        break;
                    }

                    // Store the quote line
                    quote_lines[quote_line_count] = line;
                    quote_line_count++;

                    // Move to next line
                    if (*line_end == '\n') {
                        line_start = line_end + 1;
                    } else {
                        break;
                    }
                }

                // Create quote block element
                Element* quote_block = create_quote_block(input, quote_lines, quote_line_count);
                if (quote_block) {
                    list_push((List*)doc, {.item = (uint64_t)quote_block});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                // Free quote lines
                for (int i = 0; i < quote_line_count; i++) {
                    mem_free((void*)quote_lines[i]);
                }
                mem_free(quote_lines);

                // Move to next line
                if (*line_end == '\n') {
                    line_start = line_end + 1;
                } else {
                    break;
                }
                continue;
            }

            // Check if this is an example block start
            if (is_begin_example(line)) {
                const char** block_lines = (const char**)malloc(1000 * sizeof(char*));
                int block_line_count = 0;

                mem_free(line);
                if (*line_end == '\n') line_start = line_end + 1; else break;

                while (*line_start && block_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') line_end++;
                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;
                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    if (is_end_example(line)) {
                        mem_free(line);
                        break;
                    }

                    block_lines[block_line_count++] = line;
                    if (*line_end == '\n') line_start = line_end + 1; else break;
                }

                Element* example_block = create_generic_block(input, "example", block_lines, block_line_count, true);
                if (example_block) {
                    list_push((List*)doc, {.item = (uint64_t)example_block});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                for (int i = 0; i < block_line_count; i++) {
                    mem_free((void*)block_lines[i]);
                }
                mem_free(block_lines);

                if (*line_end == '\n') line_start = line_end + 1; else break;
                continue;
            }

            // Check if this is a verse block start
            if (is_begin_verse(line)) {
                const char** block_lines = (const char**)malloc(1000 * sizeof(char*));
                int block_line_count = 0;

                mem_free(line);
                if (*line_end == '\n') line_start = line_end + 1; else break;

                while (*line_start && block_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') line_end++;
                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;
                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    if (is_end_verse(line)) {
                        mem_free(line);
                        break;
                    }

                    block_lines[block_line_count++] = line;
                    if (*line_end == '\n') line_start = line_end + 1; else break;
                }

                Element* verse_block = create_generic_block(input, "verse", block_lines, block_line_count, true);
                if (verse_block) {
                    list_push((List*)doc, {.item = (uint64_t)verse_block});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                for (int i = 0; i < block_line_count; i++) {
                    mem_free((void*)block_lines[i]);
                }
                mem_free(block_lines);

                if (*line_end == '\n') line_start = line_end + 1; else break;
                continue;
            }

            // Check if this is a center block start
            if (is_begin_center(line)) {
                const char** block_lines = (const char**)malloc(1000 * sizeof(char*));
                int block_line_count = 0;

                mem_free(line);
                if (*line_end == '\n') line_start = line_end + 1; else break;

                while (*line_start && block_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') line_end++;
                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;
                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    if (is_end_center(line)) {
                        mem_free(line);
                        break;
                    }

                    block_lines[block_line_count++] = line;
                    if (*line_end == '\n') line_start = line_end + 1; else break;
                }

                Element* center_block = create_generic_block(input, "center", block_lines, block_line_count, false);
                if (center_block) {
                    list_push((List*)doc, {.item = (uint64_t)center_block});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                for (int i = 0; i < block_line_count; i++) {
                    mem_free((void*)block_lines[i]);
                }
                mem_free(block_lines);

                if (*line_end == '\n') line_start = line_end + 1; else break;
                continue;
            }

            // Check if this is a drawer start
            char drawer_name[64];
            if (is_drawer_start(line, drawer_name, sizeof(drawer_name))) {
                const char** drawer_lines = (const char**)malloc(1000 * sizeof(char*));
                int drawer_line_count = 0;

                mem_free(line);
                if (*line_end == '\n') line_start = line_end + 1; else break;

                while (*line_start && drawer_line_count < 1000) {
                    line_end = line_start;
                    while (*line_end && *line_end != '\n') line_end++;
                    line_len = line_end - line_start;
                    line = (char*)mem_alloc(line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!line) break;
                    strncpy(line, line_start, line_len);
                    line[line_len] = '\0';

                    if (is_drawer_end(line)) {
                        mem_free(line);
                        break;
                    }

                    drawer_lines[drawer_line_count++] = line;
                    if (*line_end == '\n') line_start = line_end + 1; else break;
                }

                Element* drawer = create_drawer(input, drawer_name, drawer_lines, drawer_line_count);
                if (drawer) {
                    list_push((List*)doc, {.item = (uint64_t)drawer});
                    ((TypeElmt*)doc->type)->content_length++;
                }

                for (int i = 0; i < drawer_line_count; i++) {
                    mem_free((void*)drawer_lines[i]);
                }
                mem_free(drawer_lines);

                if (*line_end == '\n') line_start = line_end + 1; else break;
                continue;
            }

            // Check if this is a heading
            int heading_level = count_leading_stars(line);
            if (heading_level > 0) {
                // Extract title (skip stars and space)
                const char* title = line + heading_level + 1;
                Element* heading = create_heading(input, heading_level, title);
                if (heading) {
                    list_push((List*)doc, {.item = (uint64_t)heading});
                    ((TypeElmt*)doc->type)->content_length++;
                }
            } else {
                // Check for other types of lines
                char* sched_keyword = NULL;
                char* sched_timestamp = NULL;
                char* footnote_name = NULL;
                char* footnote_content = NULL;

                if (is_scheduling_line(line, &sched_keyword, &sched_timestamp)) {
                    // Handle standalone scheduling lines
                    Element* scheduling = create_scheduling(input, sched_keyword, sched_timestamp);
                    if (scheduling) {
                        list_push((List*)doc, {.item = (uint64_t)scheduling});
                        ((TypeElmt*)doc->type)->content_length++;
                    }
                    mem_free(sched_keyword);
                    mem_free(sched_timestamp);
                } else if (is_footnote_definition(line, &footnote_name, &footnote_content)) {
                    // Handle footnote definitions
                    Element* footnote_def = create_footnote_definition(input, footnote_name, footnote_content);
                    if (footnote_def) {
                        list_push((List*)doc, {.item = (uint64_t)footnote_def});
                        ((TypeElmt*)doc->type)->content_length++;
                    }
                    mem_free(footnote_name);
                    mem_free(footnote_content);
                } else if (is_list_item(line)) {
                // Create list item element
                Element* list_item = create_org_element(input, "list_item");
                if (list_item) {
                    String* content_string = create_string(input, line);
                    if (content_string) {
                        list_push((List*)list_item, {.item = s2it(content_string)});
                        ((TypeElmt*)list_item->type)->content_length++;
                        list_push((List*)doc, {.item = (uint64_t)list_item});
                        ((TypeElmt*)doc->type)->content_length++;
                    }
                }
            } else if (is_directive(line)) {
                // Create directive element
                Element* directive = create_directive(input, line);
                if (directive) {
                    list_push((List*)doc, {.item = (uint64_t)directive});
                    ((TypeElmt*)doc->type)->content_length++;
                }
            } else if (is_table_row(line)) {
                // Start collecting table rows
                Element* table_rows[100];  // Max 100 rows
                int row_count = 0;
                bool first_row_is_header = false;

                // Process current line first
                char** cells;
                int cell_count = parse_table_cells(line, &cells);

                if (cell_count > 0) {
                    Element* row = create_table_row(input, cells, cell_count, false);  // Will update if header
                    if (row) {
                        table_rows[row_count++] = row;
                    }

                    // Free cell strings
                    for (int j = 0; j < cell_count; j++) {
                        mem_free(cells[j]);
                    }
                    mem_free(cells);
                }

                // Look ahead for more table rows
                const char* next_line_start = line_end;
                if (*next_line_start == '\n') next_line_start++;

                while (*next_line_start && row_count < 100) {
                    // Find the next line
                    const char* next_line_end = next_line_start;
                    while (*next_line_end && *next_line_end != '\n') {
                        next_line_end++;
                    }

                    if (next_line_end == next_line_start) break; // Empty line ends table

                    // Create null-terminated line string
                    size_t next_line_len = next_line_end - next_line_start;
                    char* next_line = (char*)mem_alloc(next_line_len + 1, MEM_CAT_INPUT_ORG);
                    if (!next_line) break;

                    strncpy(next_line, next_line_start, next_line_len);
                    next_line[next_line_len] = '\0';

                    if (!is_table_row(next_line)) {
                        mem_free(next_line);
                        break;
                    }

                    if (is_table_separator(next_line)) {
                        // Mark first row as header if this is a separator
                        if (row_count == 1) {
                            first_row_is_header = true;
                        }
                        mem_free(next_line);
                        // Move past separator
                        line_start = (*next_line_end == '\n') ? next_line_end + 1 : next_line_end;
                        next_line_start = line_start;
                        continue;
                    }

                    // Parse table row
                    char** next_cells;
                    int next_cell_count = parse_table_cells(next_line, &next_cells);

                    if (next_cell_count > 0) {
                        Element* next_row = create_table_row(input, next_cells, next_cell_count, false);
                        if (next_row) {
                            table_rows[row_count++] = next_row;
                        }

                        // Free cell strings
                        for (int j = 0; j < next_cell_count; j++) {
                            mem_free(next_cells[j]);
                        }
                        mem_free(next_cells);
                    }

                    mem_free(next_line);

                    // Move to next line
                    line_start = (*next_line_end == '\n') ? next_line_end + 1 : next_line_end;
                    next_line_start = line_start;
                }

                // Update first row to be header if needed
                if (first_row_is_header && row_count > 0 && table_rows[0]) {
                    // Update the type name to indicate header
                    TypeElmt* type = (TypeElmt*)table_rows[0]->type;
                    if (type && type->name.length > 0) {
                        // Need to replace the type name - this is a bit tricky
                        // For now, we'll rely on the formatter to detect header rows
                    }
                }

                // Create table from collected rows
                if (row_count > 0) {
                    Element* table = create_table(input, table_rows, row_count);
                    if (table) {
                        list_push((List*)doc, {.item = (uint64_t)table});
                        ((TypeElmt*)doc->type)->content_length++;
                    }
                }

                // Continue from where we left off
                continue;
            } else {
                // Regular paragraph text - enhance with inline parsing
                Element* paragraph = create_org_element(input, "paragraph");
                if (paragraph) {
                    // Try to parse inline formatting
                    Element* inline_content = parse_inline_text(input, line);
                    if (inline_content && ((List*)inline_content)->length > 0) {
                        // Use the inline content if it has elements
                        List* inline_list = (List*)inline_content;
                        for (long j = 0; j < inline_list->length; j++) {
                            Item inline_item = inline_list->items[j];
                            list_push((List*)paragraph, inline_item);
                            ((TypeElmt*)paragraph->type)->content_length++;
                        }
                    } else {
                        // Fallback to simple string content
                        String* content_string = create_string(input, line);
                        if (content_string) {
                            list_push((List*)paragraph, {.item = s2it(content_string)});
                            ((TypeElmt*)paragraph->type)->content_length++;
                        }
                    }

                    list_push((List*)doc, {.item = (uint64_t)paragraph});
                    ((TypeElmt*)doc->type)->content_length++;
                }
                }
            }
        }

        mem_free(line);

        // Move to next line
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    input->root = {.item = (uint64_t)doc};

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }
}

// Clean up macros to avoid pollution
#undef create_string
#undef create_org_element
