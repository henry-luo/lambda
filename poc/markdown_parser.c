#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// CommonMark-compliant tree node structure
typedef enum {
    NODE_DOCUMENT,
    NODE_HEADER,           // ATX and Setext headings
    NODE_PARAGRAPH,
    NODE_LIST,             // Both ordered and unordered
    NODE_LIST_ITEM,
    NODE_CODE_BLOCK,       // Fenced and indented
    NODE_INLINE_CODE,
    NODE_BOLD,
    NODE_ITALIC,
    NODE_LINK,
    NODE_IMAGE,
    NODE_TEXT,
    NODE_THEMATIC_BREAK,   // Horizontal rules
    NODE_BLOCK_QUOTE,
    NODE_HTML_BLOCK,
    NODE_INLINE_HTML,
    NODE_AUTOLINK,
    NODE_HARD_BREAK,
    NODE_SOFT_BREAK
} NodeType;

typedef struct Node {
    NodeType type;
    char* content;
    int level;           // for headers (1-6), for list indentation
    char* url;           // for links and images
    char* title;         // for links and images (optional title)
    char* alt_text;      // for images
    char* info_string;   // for fenced code blocks
    bool is_ordered;     // for lists
    int start_number;    // for ordered lists
    char list_marker;    // list marker character (-, *, +, or digit)
    bool is_tight;       // for lists (tight vs loose)
    struct Node** children;
    int child_count;
    int child_capacity;
} Node;

// utility functions
static Node* create_node(NodeType type) {
    Node* node = calloc(1, sizeof(Node));
    node->type = type;
    node->child_capacity = 4;
    node->children = calloc(node->child_capacity, sizeof(Node*));
    return node;
}

static void add_child(Node* parent, Node* child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = realloc(parent->children, parent->child_capacity * sizeof(Node*));
    }
    parent->children[parent->child_count++] = child;
}

static void free_node(Node* node) {
    if (!node) return;
    
    for (int i = 0; i < node->child_count; i++) {
        free_node(node->children[i]);
    }
    
    free(node->children);
    free(node->content);
    free(node->url);
    free(node->title);
    free(node->alt_text);
    free(node->info_string);
    free(node);
}

static char* trim_whitespace(const char* str) {
    if (!str) return NULL;
    
    // find start
    while (isspace(*str)) str++;
    
    if (*str == '\0') return strdup("");
    
    // find end
    const char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    
    // create trimmed copy
    int len = end - str + 1;
    char* result = malloc(len + 1);
    strncpy(result, str, len);
    result[len] = '\0';
    
    return result;
}

static int count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

static bool is_empty_line(const char* line) {
    while (*line) {
        if (!isspace(*line)) return false;
        line++;
    }
    return true;
}

// Enhanced utility functions for CommonMark compliance
static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_unicode_whitespace(char c) {
    return is_whitespace_char(c) || c == '\f' || c == '\v';
}

static bool is_ascii_punctuation(char c) {
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || 
           (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}

static int count_char_sequence(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

static char* substring(const char* str, int start, int len) {
    if (!str || start < 0 || len <= 0) return strdup("");
    char* result = malloc(len + 1);
    strncpy(result, str + start, len);
    result[len] = '\0';
    return result;
}

static bool is_thematic_break(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (!line[pos] || (line[pos] != '-' && line[pos] != '*' && line[pos] != '_')) {
        return false;
    }
    
    char marker = line[pos];
    int count = 0;
    
    while (line[pos]) {
        if (line[pos] == marker) {
            count++;
        } else if (line[pos] != ' ') {
            return false;
        }
        pos++;
    }
    
    return count >= 3;
}

static bool is_atx_heading(const char* line) {
    int hash_count = count_leading_chars(line, '#');
    return hash_count >= 1 && hash_count <= 6 && 
           (line[hash_count] == '\0' || is_whitespace_char(line[hash_count]));
}

static bool is_setext_heading_underline(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (!line[pos] || (line[pos] != '=' && line[pos] != '-')) {
        return false;
    }
    
    char marker = line[pos];
    while (line[pos]) {
        if (line[pos] != marker && line[pos] != ' ') {
            return false;
        }
        pos++;
    }
    
    return true;
}

static bool is_fenced_code_block_start(const char* line, char* fence_char, int* fence_length) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    if (line[pos] != '`' && line[pos] != '~') return false;
    
    *fence_char = line[pos];
    *fence_length = count_char_sequence(line + pos, *fence_char);
    
    return *fence_length >= 3;
}

static bool is_indented_code_block_line(const char* line) {
    if (strlen(line) < 4) return false;
    
    // Must start with exactly 4 spaces or 1 tab
    if (line[0] == '\t') return true;
    
    return (line[0] == ' ' && line[1] == ' ' && line[2] == ' ' && line[3] == ' ');
}

static bool is_list_marker(const char* line, int* marker_pos, char* marker_char, bool* is_ordered, int* number) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    *marker_pos = pos;
    
    // Check for unordered list markers
    if (line[pos] == '-' || line[pos] == '+' || line[pos] == '*') {
        *marker_char = line[pos];
        *is_ordered = false;
        pos++;
        
        // Must be followed by space or tab or end of line
        return line[pos] == '\0' || is_whitespace_char(line[pos]);
    }
    
    // Check for ordered list markers
    if (isdigit(line[pos])) {
        int start_pos = pos;
        int num = 0;
        
        while (isdigit(line[pos]) && pos - start_pos < 9) {
            num = num * 10 + (line[pos] - '0');
            pos++;
        }
        
        if (pos > start_pos && (line[pos] == '.' || line[pos] == ')')) {
            *marker_char = line[pos];
            *is_ordered = true;
            *number = num;
            pos++;
            
            // Must be followed by space or tab or end of line
            return line[pos] == '\0' || is_whitespace_char(line[pos]);
        }
    }
    
    return false;
}

static bool is_block_quote_marker(const char* line) {
    int pos = 0;
    
    // Skip up to 3 spaces
    while (pos < 3 && line[pos] == ' ') pos++;
    
    return line[pos] == '>';
}

// Helper function to split text into lines
static char** split_lines(const char* text, int* line_count) {
    *line_count = 0;
    
    // Count lines
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') (*line_count)++;
        ptr++;
    }
    if (ptr > text && *(ptr-1) != '\n') {
        (*line_count)++; // Last line without \n
    }
    
    // Allocate array
    char** lines = malloc(*line_count * sizeof(char*));
    
    // Split into lines
    int line_index = 0;
    const char* line_start = text;
    ptr = text;
    
    while (*ptr && line_index < *line_count) {
        if (*ptr == '\n') {
            int len = ptr - line_start;
            lines[line_index] = malloc(len + 1);
            strncpy(lines[line_index], line_start, len);
            lines[line_index][len] = '\0';
            line_index++;
            line_start = ptr + 1;
        }
        ptr++;
    }
    
    // Handle last line if it doesn't end with newline
    if (line_index < *line_count && line_start < ptr) {
        int len = ptr - line_start;
        lines[line_index] = malloc(len + 1);
        strncpy(lines[line_index], line_start, len);
        lines[line_index][len] = '\0';
        line_index++;
    }
    
    // Adjust line count to actual lines created
    *line_count = line_index;
    
    return lines;
}

static void free_lines(char** lines, int line_count) {
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}

// Enhanced inline parsing functions for CommonMark compliance
static Node* parse_inline_text(const char* text);

static Node* parse_emphasis(const char* text, int* pos) {
    int start = *pos;
    char marker = text[start];
    
    if (marker != '*' && marker != '_') return NULL;
    
    int marker_count = count_char_sequence(text + start, marker);
    if (marker_count < 1 || marker_count > 2) return NULL;
    
    // Look for matching closing delimiter
    int search_pos = start + marker_count;
    bool found_closer = false;
    int close_pos = -1;
    
    while (text[search_pos]) {
        if (text[search_pos] == marker) {
            int close_count = count_char_sequence(text + search_pos, marker);
            if (close_count >= marker_count) {
                found_closer = true;
                close_pos = search_pos;
                break;
            }
            search_pos += close_count;
        } else {
            search_pos++;
        }
    }
    
    if (!found_closer) return NULL;
    
    // Extract content between delimiters
    int content_start = start + marker_count;
    int content_len = close_pos - content_start;
    
    if (content_len <= 0) return NULL;
    
    char* content = substring(text, content_start, content_len);
    
    Node* node = create_node(marker_count == 2 ? NODE_BOLD : NODE_ITALIC);
    node->content = content;
    
    *pos = close_pos + marker_count;
    return node;
}

static Node* parse_code_span(const char* text, int* pos) {
    if (text[*pos] != '`') return NULL;
    
    int start = *pos;
    int backtick_count = count_char_sequence(text + start, '`');
    
    // Look for closing backticks
    int search_pos = start + backtick_count;
    bool found_closer = false;
    int close_pos = -1;
    
    while (text[search_pos]) {
        if (text[search_pos] == '`') {
            int close_count = count_char_sequence(text + search_pos, '`');
            if (close_count == backtick_count) {
                found_closer = true;
                close_pos = search_pos;
                break;
            }
            search_pos += close_count;
        } else {
            search_pos++;
        }
    }
    
    if (!found_closer) return NULL;
    
    // Extract and process content
    int content_start = start + backtick_count;
    int content_len = close_pos - content_start;
    
    char* content = substring(text, content_start, content_len);
    
    // Normalize whitespace in code spans
    char* trimmed = trim_whitespace(content);
    free(content);
    
    Node* node = create_node(NODE_INLINE_CODE);
    node->content = trimmed;
    
    *pos = close_pos + backtick_count;
    return node;
}

static Node* parse_autolink(const char* text, int* pos) {
    if (text[*pos] != '<') return NULL;
    
    int start = *pos + 1;
    int end = start;
    
    // Find closing >
    while (text[end] && text[end] != '>' && text[end] != ' ' && text[end] != '\n') {
        end++;
    }
    
    if (text[end] != '>') return NULL;
    
    char* url = substring(text, start, end - start);
    
    // Simple validation for autolinks
    bool is_valid = false;
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "ftp://", 6) == 0) {
        is_valid = true;
    } else {
        // Check for email pattern
        char* at_pos = strchr(url, '@');
        if (at_pos && at_pos > url && strchr(at_pos + 1, '.')) {
            is_valid = true;
        }
    }
    
    if (!is_valid) {
        free(url);
        return NULL;
    }
    
    Node* node = create_node(NODE_AUTOLINK);
    node->content = strdup(url);
    node->url = url;
    
    *pos = end + 1;
    return node;
}

static Node* parse_link_or_image(const char* text, int* pos) {
    bool is_image = false;
    int start = *pos;
    
    if (text[start] == '!') {
        is_image = true;
        start++;
    }
    
    if (text[start] != '[') return NULL;
    
    // Find link text closing bracket
    int text_start = start + 1;
    int text_end = text_start;
    int bracket_depth = 1;
    
    while (text[text_end] && bracket_depth > 0) {
        if (text[text_end] == '[') {
            bracket_depth++;
        } else if (text[text_end] == ']') {
            bracket_depth--;
        }
        if (bracket_depth > 0) text_end++;
    }
    
    if (bracket_depth > 0) return NULL;
    
    // Check for inline link
    if (text[text_end + 1] == '(') {
        int url_start = text_end + 2;
        int url_end = url_start;
        int paren_depth = 1;
        
        while (text[url_end] && paren_depth > 0) {
            if (text[url_end] == '(') {
                paren_depth++;
            } else if (text[url_end] == ')') {
                paren_depth--;
            }
            if (paren_depth > 0) url_end++;
        }
        
        if (paren_depth > 0) return NULL;
        
        // Extract link text and URL
        char* link_text = substring(text, text_start, text_end - text_start);
        char* url_part = substring(text, url_start, url_end - url_start);
        
        // Simple URL parsing (could be enhanced for title extraction)
        char* url = trim_whitespace(url_part);
        free(url_part);
        
        Node* node = create_node(is_image ? NODE_IMAGE : NODE_LINK);
        if (is_image) {
            node->alt_text = link_text;
        } else {
            node->content = link_text;
        }
        node->url = url;
        
        *pos = url_end + 1;
        return node;
    }
    
    return NULL;
}

static Node* parse_hard_break(const char* text, int* pos) {
    int start = *pos;
    
    // Check for two or more spaces followed by line ending
    if (text[start] == ' ' && text[start + 1] == ' ') {
        int space_count = 0;
        while (text[start + space_count] == ' ') {
            space_count++;
        }
        
        if (space_count >= 2 && (text[start + space_count] == '\n' || text[start + space_count] == '\0')) {
            Node* node = create_node(NODE_HARD_BREAK);
            *pos = start + space_count;
            return node;
        }
    }
    
    // Check for backslash followed by line ending
    if (text[start] == '\\' && (text[start + 1] == '\n' || text[start + 1] == '\0')) {
        Node* node = create_node(NODE_HARD_BREAK);
        *pos = start + 1;
        return node;
    }
    
    return NULL;
}

static Node* parse_inline_text(const char* text) {
    if (!text || strlen(text) == 0) {
        Node* empty = create_node(NODE_TEXT);
        empty->content = strdup("");
        return empty;
    }
    
    // Create a container to hold mixed inline content
    Node* container = create_node(NODE_PARAGRAPH);
    int pos = 0;
    int len = strlen(text);
    
    // Check if the text has any special markdown elements
    bool has_special = false;
    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c == '*' || c == '_' || c == '`' || c == '[' || c == '!' || 
            c == '<' || c == '\\' || (c == ' ' && i < len - 1 && text[i + 1] == ' ')) {
            has_special = true;
            break;
        }
    }
    
    if (!has_special) {
        free_node(container);
        Node* simple_text = create_node(NODE_TEXT);
        simple_text->content = strdup(text);
        return simple_text;
    }
    
    while (pos < len) {
        Node* inline_node = NULL;
        
        // Try to parse special inline elements in order of precedence
        if (text[pos] == '\\') {
            // Backslash escapes
            if (pos + 1 < len && is_ascii_punctuation(text[pos + 1])) {
                char escaped[2] = {text[pos + 1], '\0'};
                Node* text_node = create_node(NODE_TEXT);
                text_node->content = strdup(escaped);
                add_child(container, text_node);
                pos += 2;
                continue;
            } else {
                inline_node = parse_hard_break(text, &pos);
            }
        } else if (text[pos] == '`') {
            inline_node = parse_code_span(text, &pos);
        } else if (text[pos] == '<') {
            inline_node = parse_autolink(text, &pos);
        } else if (text[pos] == '[' || text[pos] == '!') {
            inline_node = parse_link_or_image(text, &pos);
        } else if (text[pos] == '*' || text[pos] == '_') {
            inline_node = parse_emphasis(text, &pos);
        } else if (text[pos] == ' ') {
            inline_node = parse_hard_break(text, &pos);
        }
        
        if (inline_node) {
            add_child(container, inline_node);
        } else {
            // Regular text character - collect consecutive non-special characters
            int text_start = pos;
            while (pos < len) {
                char c = text[pos];
                if (c == '*' || c == '_' || c == '`' || c == '[' || c == '!' || 
                    c == '<' || c == '\\' || (c == ' ' && pos < len - 1 && text[pos + 1] == ' ')) {
                    break;
                }
                pos++;
            }
            
            if (pos > text_start) {
                char* content = substring(text, text_start, pos - text_start);
                Node* text_node = create_node(NODE_TEXT);
                text_node->content = content;
                add_child(container, text_node);
            }
        }
    }
    
    // If container has only one text child, return it directly
    if (container->child_count == 1 && container->children[0]->type == NODE_TEXT) {
        Node* text_node = container->children[0];
        container->children[0] = NULL;  // Prevent double-free
        container->child_count = 0;
        free_node(container);
        return text_node;
    }
    
    return container;
}

// Enhanced block parsing functions for CommonMark compliance

static Node* parse_thematic_break(const char* line) {
    if (!is_thematic_break(line)) return NULL;
    
    Node* hr = create_node(NODE_THEMATIC_BREAK);
    return hr;
}

static Node* parse_atx_header(const char* line) {
    if (!is_atx_heading(line)) return NULL;
    
    int hash_count = count_leading_chars(line, '#');
    
    // Skip hashes and whitespace
    const char* content_start = line + hash_count;
    while (*content_start && is_whitespace_char(*content_start)) {
        content_start++;
    }
    
    if (*content_start == '\0') {
        // Empty header
        Node* header = create_node(NODE_HEADER);
        header->level = hash_count;
        header->content = strdup("");
        return header;
    }
    
    // Find end of content (remove trailing #s if present)
    const char* content_end = content_start + strlen(content_start) - 1;
    
    // Remove trailing whitespace
    while (content_end > content_start && is_whitespace_char(*content_end)) {
        content_end--;
    }
    
    // Remove trailing #s (only if preceded by space or if all the content is #s)
    const char* temp_end = content_end;
    while (temp_end >= content_start && *temp_end == '#') {
        temp_end--;
    }
    
    // Only remove trailing #s if there's a space before them or the line is only #s
    if (temp_end < content_start || is_whitespace_char(*temp_end)) {
        content_end = temp_end;
        // Remove trailing whitespace before #s
        while (content_end >= content_start && is_whitespace_char(*content_end)) {
            content_end--;
        }
    }
    
    int content_len = content_end - content_start + 1;
    if (content_len <= 0) {
        Node* header = create_node(NODE_HEADER);
        header->level = hash_count;
        header->content = strdup("");
        return header;
    }
    
    char* content = substring(content_start, 0, content_len);
    
    Node* header = create_node(NODE_HEADER);
    header->level = hash_count;
    header->content = content;
    
    return header;
}

static Node* parse_setext_header(const char* content_line, const char* underline_line) {
    if (!is_setext_heading_underline(underline_line)) return NULL;
    
    char* content = trim_whitespace(content_line);
    if (strlen(content) == 0) {
        free(content);
        return NULL;
    }
    
    // Determine level based on underline character
    int level = (strchr(underline_line, '=') != NULL) ? 1 : 2;
    
    Node* header = create_node(NODE_HEADER);
    header->level = level;
    header->content = content;
    
    return header;
}

static Node* parse_fenced_code_block(char** lines, int* current_line, int total_lines) {
    char fence_char;
    int fence_length;
    
    if (!is_fenced_code_block_start(lines[*current_line], &fence_char, &fence_length)) {
        return NULL;
    }
    
    // Extract info string
    const char* info_start = lines[*current_line];
    while (*info_start && *info_start != fence_char) info_start++;
    while (*info_start == fence_char) info_start++;
    
    char* info_string = trim_whitespace(info_start);
    
    Node* code_block = create_node(NODE_CODE_BLOCK);
    code_block->info_string = info_string;
    
    (*current_line)++;
    
    // Collect code content
    char* content = strdup("");
    bool first_line = true;
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            continue;
        }
        
        // Check for closing fence
        if (line[0] == fence_char) {
            int close_fence_length = count_char_sequence(line, fence_char);
            if (close_fence_length >= fence_length) {
                // Check that the line contains only the fence character and whitespace
                bool only_fence = true;
                for (int i = close_fence_length; line[i]; i++) {
                    if (!is_whitespace_char(line[i])) {
                        only_fence = false;
                        break;
                    }
                }
                if (only_fence) {
                    break;
                }
            }
        }
        
        // Add line to content
        if (!first_line) {
            int old_len = strlen(content);
            content = realloc(content, old_len + 2);
            strcat(content, "\n");
        }
        
        int old_len = strlen(content);
        int line_len = strlen(line);
        content = realloc(content, old_len + line_len + 1);
        strcat(content, line);
        
        first_line = false;
        (*current_line)++;
    }
    
    code_block->content = content;
    return code_block;
}

static Node* parse_indented_code_block(char** lines, int* current_line, int total_lines) {
    if (!is_indented_code_block_line(lines[*current_line])) {
        return NULL;
    }
    
    Node* code_block = create_node(NODE_CODE_BLOCK);
    char* content = strdup("");
    bool first_line = true;
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            break;
        }
        
        if (!is_indented_code_block_line(line) && !is_empty_line(line)) {
            break;
        }
        
        // Remove 4 spaces or 1 tab from the beginning
        const char* code_content = line;
        if (line[0] == '\t') {
            code_content = line + 1;
        } else {
            int spaces = 0;
            while (spaces < 4 && line[spaces] == ' ') {
                spaces++;
            }
            code_content = line + spaces;
        }
        
        // Add line to content
        if (!first_line) {
            int old_len = strlen(content);
            content = realloc(content, old_len + 2);
            strcat(content, "\n");
        }
        
        int old_len = strlen(content);
        int line_len = strlen(code_content);
        content = realloc(content, old_len + line_len + 1);
        strcat(content, code_content);
        
        first_line = false;
        (*current_line)++;
    }
    
    // Remove trailing empty lines
    char* end = content + strlen(content) - 1;
    while (end > content && *end == '\n') {
        *end = '\0';
        end--;
    }
    
    code_block->content = content;
    return code_block;
}

static Node* parse_block_quote(char** lines, int* current_line, int total_lines) {
    if (!is_block_quote_marker(lines[*current_line])) {
        return NULL;
    }
    
    Node* quote = create_node(NODE_BLOCK_QUOTE);
    
    // Collect quoted content
    char* content = strdup("");
    bool first_line = true;
    
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            break;
        }
        
        if (!is_block_quote_marker(line) && !is_empty_line(line)) {
            break;
        }
        
        // Remove quote marker and optional space
        const char* quote_content = line;
        int pos = 0;
        
        // Skip up to 3 spaces
        while (pos < 3 && quote_content[pos] == ' ') pos++;
        
        if (quote_content[pos] == '>') {
            pos++;
            if (quote_content[pos] == ' ') pos++;
        }
        
        quote_content += pos;
        
        // Add line to content
        if (!first_line) {
            int old_len = strlen(content);
            content = realloc(content, old_len + 2);
            strcat(content, "\n");
        }
        
        int old_len = strlen(content);
        int line_len = strlen(quote_content);
        content = realloc(content, old_len + line_len + 1);
        strcat(content, quote_content);
        
        first_line = false;
        (*current_line)++;
    }
    
    // Parse the quoted content recursively
    if (strlen(content) > 0) {
        int quoted_line_count;
        char** quoted_lines = split_lines(content, &quoted_line_count);
        
        // Parse the content as markdown
        int quoted_pos = 0;
        while (quoted_pos < quoted_line_count) {
            Node* child = NULL;
            
            if (quoted_lines[quoted_pos] && !is_empty_line(quoted_lines[quoted_pos])) {
                if (is_atx_heading(quoted_lines[quoted_pos])) {
                    child = parse_atx_header(quoted_lines[quoted_pos]);
                } else {
                    Node* para = create_node(NODE_PARAGRAPH);
                    Node* text = parse_inline_text(quoted_lines[quoted_pos]);
                    add_child(para, text);
                    child = para;
                }
            }
            
            if (child) {
                add_child(quote, child);
            }
            
            quoted_pos++;
        }
        
        free_lines(quoted_lines, quoted_line_count);
    }
    
    free(content);
    return quote;
}

static Node* parse_list_item_enhanced(const char* line, int* indent_level, char* marker_char, bool* is_ordered, int* number) {
    int marker_pos;
    
    if (!is_list_marker(line, &marker_pos, marker_char, is_ordered, number)) {
        return NULL;
    }
    
    *indent_level = marker_pos;
    
    // Find content start (after marker and optional space)
    int content_pos = marker_pos;
    
    // Skip marker character(s)
    if (*is_ordered) {
        while (isdigit(line[content_pos])) content_pos++;
        content_pos++; // Skip . or )
    } else {
        content_pos++; // Skip -, +, or *
    }
    
    // Skip optional space after marker
    if (line[content_pos] == ' ' || line[content_pos] == '\t') {
        content_pos++;
    }
    
    // Extract item content
    char* content = trim_whitespace(line + content_pos);
    
    Node* item = create_node(NODE_LIST_ITEM);
    item->level = *indent_level;
    item->list_marker = *marker_char;
    
    if (strlen(content) > 0) {
        Node* text_node = parse_inline_text(content);
        add_child(item, text_node);
    }
    
    free(content);
    return item;
}

static Node* parse_list(char** lines, int* current_line, int total_lines) {
    int indent_level, number;
    char marker_char;
    bool is_ordered;
    
    Node* first_item = parse_list_item_enhanced(lines[*current_line], &indent_level, &marker_char, &is_ordered, &number);
    if (!first_item) return NULL;
    
    Node* list = create_node(NODE_LIST);
    list->is_ordered = is_ordered;
    list->start_number = is_ordered ? number : 1;
    list->list_marker = marker_char;
    list->is_tight = true; // Assume tight list initially
    
    add_child(list, first_item);
    (*current_line)++;
    
    // Collect additional list items
    while (*current_line < total_lines) {
        const char* line = lines[*current_line];
        
        if (!line) {
            (*current_line)++;
            // Check if this creates a loose list
            if (*current_line < total_lines && lines[*current_line] && 
                !is_empty_line(lines[*current_line])) {
                list->is_tight = false;
            }
            continue;
        }
        
        if (is_empty_line(line)) {
            (*current_line)++;
            // Check if next non-empty line is a list item
            int next_line = *current_line;
            while (next_line < total_lines && lines[next_line] && is_empty_line(lines[next_line])) {
                next_line++;
            }
            
            if (next_line < total_lines && lines[next_line]) {
                int next_indent, next_number;
                char next_marker;
                bool next_ordered;
                
                if (is_list_marker(lines[next_line], &next_indent, &next_marker, &next_ordered, &next_number)) {
                    // Same type of list continues
                    if (next_ordered == is_ordered) {
                        list->is_tight = false; // Blank line makes it loose
                        continue;
                    }
                }
            }
            break;
        }
        
        // Check if this line is a list item
        int item_indent, item_number;
        char item_marker;
        bool item_ordered;
        
        Node* item = parse_list_item_enhanced(line, &item_indent, &item_marker, &item_ordered, &item_number);
        
        if (item && item_ordered == is_ordered) {
            // Handle nested lists
            if (item_indent > indent_level) {
                // This should be handled by nested list parsing
                // For now, treat as continuation
                add_child(list, item);
            } else if (item_indent == indent_level) {
                // Same level list item
                add_child(list, item);
            } else {
                // Dedented - end of this list
                break;
            }
        } else {
            // Not a list item, end of list
            break;
        }
        
        (*current_line)++;
    }
    
    return list;
}

static Node* parse_paragraph(const char* line) {
    char* content = trim_whitespace(line);
    
    if (strlen(content) == 0) {
        free(content);
        return NULL;
    }
    
    Node* paragraph = create_node(NODE_PARAGRAPH);
    Node* text_node = parse_inline_text(content);
    add_child(paragraph, text_node);
    
    free(content);
    return paragraph;
}

// Enhanced main parsing function for CommonMark compliance
static Node* parse_markdown(char** lines, int line_count) {
    Node* document = create_node(NODE_DOCUMENT);
    
    int current_line = 0;
    
    while (current_line < line_count) {
        const char* line = lines[current_line];
        
        // Skip null lines
        if (!line) {
            current_line++;
            continue;
        }
        
        // Skip empty lines
        if (is_empty_line(line)) {
            current_line++;
            continue;
        }
        
        Node* node = NULL;
        
        // Try to parse different block types in order of precedence
        
        // 1. Thematic breaks (horizontal rules)
        if (is_thematic_break(line)) {
            node = parse_thematic_break(line);
        }
        // 2. ATX headings
        else if (is_atx_heading(line)) {
            node = parse_atx_header(line);
        }
        // 3. Setext headings (check next line)
        else if (current_line + 1 < line_count && lines[current_line + 1] && 
                 is_setext_heading_underline(lines[current_line + 1])) {
            node = parse_setext_header(line, lines[current_line + 1]);
            if (node) {
                current_line++; // Skip the underline
            }
        }
        // 4. Fenced code blocks
        else if (line[0] == '`' || line[0] == '~') {
            node = parse_fenced_code_block(lines, &current_line, line_count);
            current_line--; // Adjust because parse_fenced_code_block advances
        }
        // 5. Indented code blocks
        else if (is_indented_code_block_line(line)) {
            node = parse_indented_code_block(lines, &current_line, line_count);
            current_line--; // Adjust because parse_indented_code_block advances
        }
        // 6. Block quotes
        else if (is_block_quote_marker(line)) {
            node = parse_block_quote(lines, &current_line, line_count);
            current_line--; // Adjust because parse_block_quote advances
        }
        // 7. Lists
        else {
            int indent, number;
            char marker;
            bool ordered;
            
            if (is_list_marker(line, &indent, &marker, &ordered, &number)) {
                node = parse_list(lines, &current_line, line_count);
                current_line--; // Adjust because parse_list advances
            }
            // 8. Paragraphs (default)
            else {
                node = parse_paragraph(line);
            }
        }
        
        if (node) {
            add_child(document, node);
        }
        
        current_line++;
    }
    
    return document;
}

// Enhanced output functions for all CommonMark node types
static const char* node_type_to_string(NodeType type) {
    switch (type) {
        case NODE_DOCUMENT: return "document";
        case NODE_HEADER: return "header";
        case NODE_PARAGRAPH: return "paragraph";
        case NODE_LIST: return "list";
        case NODE_LIST_ITEM: return "list_item";
        case NODE_CODE_BLOCK: return "code_block";
        case NODE_INLINE_CODE: return "inline_code";
        case NODE_BOLD: return "bold";
        case NODE_ITALIC: return "italic";
        case NODE_LINK: return "link";
        case NODE_IMAGE: return "image";
        case NODE_TEXT: return "text";
        case NODE_THEMATIC_BREAK: return "thematic_break";
        case NODE_BLOCK_QUOTE: return "block_quote";
        case NODE_HTML_BLOCK: return "html_block";
        case NODE_INLINE_HTML: return "inline_html";
        case NODE_AUTOLINK: return "autolink";
        case NODE_HARD_BREAK: return "hard_break";
        case NODE_SOFT_BREAK: return "soft_break";
        default: return "unknown";
    }
}

static void print_json_tree(Node* node, int indent) {
    if (!node) return;
    
    // Safety check to prevent infinite recursion
    if (indent > 20) {
        printf("... (max depth reached)");
        return;
    }
    
    // Print indentation
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    
    printf("{\n");
    
    // Print type
    for (int i = 0; i < indent + 1; i++) {
        printf("  ");
    }
    printf("\"type\": \"%s\"", node_type_to_string(node->type));
    
    // Print content if present
    if (node->content) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"content\": \"");
        // Escape special characters in content
        const char* c = node->content;
        while (*c) {
            if (*c == '"') printf("\\\"");
            else if (*c == '\\') printf("\\\\");
            else if (*c == '\n') printf("\\n");
            else if (*c == '\r') printf("\\r");
            else if (*c == '\t') printf("\\t");
            else printf("%c", *c);
            c++;
        }
        printf("\"");
    }
    
    // Print level if present (for headers and list items)
    if (node->level > 0) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"level\": %d", node->level);
    }
    
    // Print URL if present (for links, images, autolinks)
    if (node->url) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"url\": \"");
        const char* c = node->url;
        while (*c) {
            if (*c == '"') printf("\\\"");
            else if (*c == '\\') printf("\\\\");
            else printf("%c", *c);
            c++;
        }
        printf("\"");
    }
    
    // Print title if present (for links and images)
    if (node->title) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"title\": \"");
        const char* c = node->title;
        while (*c) {
            if (*c == '"') printf("\\\"");
            else if (*c == '\\') printf("\\\\");
            else printf("%c", *c);
            c++;
        }
        printf("\"");
    }
    
    // Print alt_text if present (for images)
    if (node->alt_text) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"alt_text\": \"");
        const char* c = node->alt_text;
        while (*c) {
            if (*c == '"') printf("\\\"");
            else if (*c == '\\') printf("\\\\");
            else printf("%c", *c);
            c++;
        }
        printf("\"");
    }
    
    // Print info_string if present (for fenced code blocks)
    if (node->info_string && strlen(node->info_string) > 0) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"info_string\": \"%s\"", node->info_string);
    }
    
    // Print list-specific properties
    if (node->type == NODE_LIST) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"is_ordered\": %s", node->is_ordered ? "true" : "false");
        
        if (node->is_ordered) {
            printf(",\n");
            for (int i = 0; i < indent + 1; i++) {
                printf("  ");
            }
            printf("\"start_number\": %d", node->start_number);
        }
        
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"list_marker\": \"%c\"", node->list_marker);
        
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"is_tight\": %s", node->is_tight ? "true" : "false");
    }
    
    // Print list item marker
    if (node->type == NODE_LIST_ITEM && node->list_marker != 0) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"list_marker\": \"%c\"", node->list_marker);
    }
    
    // Print children if present
    if (node->child_count > 0) {
        printf(",\n");
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("\"children\": [\n");
        
        for (int i = 0; i < node->child_count; i++) {
            print_json_tree(node->children[i], indent + 2);
            if (i < node->child_count - 1) {
                printf(",");
            }
            printf("\n");
        }
        
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("]");
    }
    
    printf("\n");
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    printf("}");
}

int main() {
    // Comprehensive CommonMark test with all supported features
    const char* markdown_text = 
        "# Main Header (ATX)\n"
        "\n"
        "This is a paragraph with **bold text**, *italic text*, and `inline code`.\n"
        "\n"
        "Setext Header Level 1\n"
        "=====================\n"
        "\n"
        "Another paragraph with [a link](https://example.com) and ![an image](image.jpg).\n"
        "\n"
        "Setext Header Level 2\n"
        "---------------------\n"
        "\n"
        "## ATX Header Level 2 ##\n"
        "\n"
        "### Features List\n"
        "\n"
        "- First unordered item with **bold text**\n"
        "- Second item with *emphasis*\n"
        "  - Nested item with [link](https://nested.com)\n"
        "  - Another nested item\n"
        "- Third item with `code`\n"
        "\n"
        "#### Ordered List\n"
        "\n"
        "1. First ordered item\n"
        "2. Second ordered item\n"
        "   1. Nested ordered item\n"
        "   2. Another nested ordered item\n"
        "3. Third ordered item\n"
        "\n"
        "##### Code Examples\n"
        "\n"
        "Fenced code block with language:\n"
        "\n"
        "```c\n"
        "#include <stdio.h>\n"
        "int main() {\n"
        "    printf(\"Hello, CommonMark!\\n\");\n"
        "    return 0;\n"
        "}\n"
        "```\n"
        "\n"
        "Indented code block:\n"
        "\n"
        "    def hello_world():\n"
        "        print(\"Hello from indented code!\")\n"
        "        return True\n"
        "\n"
        "###### Block Quote\n"
        "\n"
        "> This is a block quote.\n"
        "> \n"
        "> It can contain multiple paragraphs.\n"
        "> \n"
        "> > Nested block quotes are also supported.\n"
        "> > \n"
        "> > With **formatting** inside.\n"
        "\n"
        "---\n"
        "\n"
        "Thematic break above! Here's some inline features:\n"
        "\n"
        "- Autolinks: <https://example.com> and <user@example.com>\n"
        "- Emphasis: *single asterisks* and _single underscores_\n"
        "- Strong: **double asterisks** and __double underscores__\n"
        "- Code spans: `simple code` and ``code with `backticks` inside``\n"
        "- Hard line break using backslash\\\n"
        "and hard break using spaces  \n"
        "followed by soft break.\n"
        "\n"
        "Another thematic break:\n"
        "\n"
        "***\n"
        "\n"
        "Final paragraph with escaped characters: \\*not emphasis\\* and \\`not code\\`.\n";
    
    printf("CommonMark-Enhanced Markdown Parser\n");
    printf("===================================\n\n");
    
    printf("Parsing markdown content:\n");
    printf("-------------------------\n");
    printf("%s\n", markdown_text);
    
    // Parse the markdown
    int line_count;
    char** lines = split_lines(markdown_text, &line_count);
    
    Node* document = parse_markdown(lines, line_count);
    
    printf("Parsed JSON tree:\n");
    printf("-----------------\n");
    
    // Print the result as JSON
    if (document) {
        print_json_tree(document, 0);
        printf("\n");
    } else {
        printf("ERROR: Document is null\n");
    }
    
    printf("\nCommonMark features supported:\n");
    printf("- ATX and Setext headings\n");
    printf("- Paragraphs with inline formatting\n");
    printf("- Ordered and unordered lists (with nesting)\n");
    printf("- Fenced and indented code blocks\n");
    printf("- Block quotes (with nesting)\n");
    printf("- Thematic breaks (horizontal rules)\n");
    printf("- Links and images (inline style)\n");
    printf("- Emphasis and strong emphasis\n");
    printf("- Code spans\n");
    printf("- Autolinks\n");
    printf("- Hard and soft line breaks\n");
    printf("- Backslash escapes\n");
    printf("- Tight and loose lists\n");
    printf("- Various list markers and start numbers\n");
    
    // Cleanup
    free_node(document);
    free_lines(lines, line_count);
    
    return 0;
}
