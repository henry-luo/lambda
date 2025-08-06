#include "input.h"

// Forward declarations
static Item parse_textile_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static bool is_textile_heading(const char* line, int* level);
static bool is_textile_list_item(const char* line, char* list_type);
static bool is_textile_block_code(const char* line);
static bool is_textile_block_quote(const char* line);
static bool is_textile_pre(const char* line);
static bool is_textile_comment(const char* line);
static bool is_textile_notextile(const char* line);
static char* parse_textile_modifiers(const char* line, int* start_pos);

// Use common utility functions from input.c
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define create_string input_create_string
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_textile_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element
#define add_attribute_item_to_element input_add_attribute_item_to_element

// Block parsing functions
static bool is_textile_heading(const char* line, int* level) {
    if (!line || strlen(line) < 3) return false;
    
    // Check for h1. to h6. patterns
    if (line[0] == 'h' && line[1] >= '1' && line[1] <= '6' && line[2] == '.') {
        if (level) *level = line[1] - '0';
        return true;
    }
    return false;
}

static bool is_textile_list_item(const char* line, char* list_type) {
    if (!line) return false;
    
    // Skip leading whitespace to check for indentation
    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t') indent++;
    
    if (line[indent] == '*' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        if (list_type) *list_type = '*'; // bulleted list
        return true;
    }
    if (line[indent] == '#' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        if (list_type) *list_type = '#'; // numbered list
        return true;
    }
    if (line[indent] == '-' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        // Check if it's a definition list (has := in the line)
        if (strstr(line, ":=")) {
            if (list_type) *list_type = '-'; // definition list
            return true;
        }
    }
    return false;
}

static bool is_textile_block_code(const char* line) {
    return strncmp(line, "bc.", 3) == 0 || strncmp(line, "bc..", 4) == 0;
}

static bool is_textile_block_quote(const char* line) {
    return strncmp(line, "bq.", 3) == 0 || strncmp(line, "bq..", 4) == 0;
}

static bool is_textile_pre(const char* line) {
    return strncmp(line, "pre.", 4) == 0 || strncmp(line, "pre..", 5) == 0;
}

static bool is_textile_comment(const char* line) {
    return strncmp(line, "###.", 4) == 0;
}

static bool is_textile_notextile(const char* line) {
    return strncmp(line, "notextile.", 10) == 0 || strncmp(line, "notextile..", 11) == 0;
}

static char* parse_textile_modifiers(const char* line, int* start_pos) {
    // Parse Textile formatting modifiers like (class), {style}, [lang], <>, etc.
    // This is a simplified version - full implementation would be more complex
    int pos = *start_pos;
    
    // Skip the block signature (e.g., "h1.", "p.", etc.)
    while (line[pos] && line[pos] != '.' && !isspace(line[pos])) pos++;
    if (line[pos] == '.') pos++; // Skip the dot
    
    // Look for modifiers
    char* modifiers = NULL;
    int mod_len = 0;
    
    while (line[pos] && !isalnum(line[pos])) {
        if (line[pos] == '(' || line[pos] == '{' || line[pos] == '[' ||
            line[pos] == '<' || line[pos] == '>' || line[pos] == '=') {
            // Found modifier start, collect until we find the content
            int mod_start = pos;
            while (line[pos] && line[pos] != ' ') pos++;
            mod_len = pos - mod_start;
            if (mod_len > 0) {
                modifiers = (char*)malloc(mod_len + 1);
                strncpy(modifiers, line + mod_start, mod_len);
                modifiers[mod_len] = '\0';
            }
            break;
        }
        pos++;
    }
    
    // Skip to content
    while (line[pos] && isspace(line[pos])) pos++;
    *start_pos = pos;
    
    return modifiers;
}

static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return {.item = s2it(create_string(input, ""))};
    }
    
    // Create a container element for mixed content
    Element* container = create_textile_element(input, "span");
    if (!container) return {.item = ITEM_NULL};
    
    const char* ptr = text;
    const char* start = text;
    
    while (*ptr) {
        bool found_markup = false;
        
        // Check for various inline formatting
        if (*ptr == '*' && *(ptr + 1) == '*') {
            // **bold**
            const char* end = strstr(ptr + 2, "**");
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add bold element
                Element* bold = create_textile_element(input, "b");
                char* bold_text = (char*)malloc(end - (ptr + 2) + 1);
                strncpy(bold_text, ptr + 2, end - (ptr + 2));
                bold_text[end - (ptr + 2)] = '\0';
                String* bold_str = create_string(input, bold_text);
                input_add_attribute_item_to_element(input, bold, "text", {.item = s2it(bold_str)});
                input_add_attribute_item_to_element(input, container, "bold", {.item = (uint64_t)bold});
                free(bold_text);
                
                ptr = end + 2;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '*') {
            // *strong*
            const char* end = strchr(ptr + 1, '*');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add strong element
                Element* strong = create_textile_element(input, "strong");
                char* strong_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(strong_text, ptr + 1, end - (ptr + 1));
                strong_text[end - (ptr + 1)] = '\0';
                String* strong_str = create_string(input, strong_text);
                input_add_attribute_item_to_element(input, strong, "text", {.item = s2it(strong_str)});
                input_add_attribute_item_to_element(input, container, "strong", {.item = (uint64_t)strong});
                free(strong_text);
                
                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '_' && *(ptr + 1) == '_') {
            // __italic__
            const char* end = strstr(ptr + 2, "__");
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add italic element
                Element* italic = create_textile_element(input, "i");
                char* italic_text = (char*)malloc(end - (ptr + 2) + 1);
                strncpy(italic_text, ptr + 2, end - (ptr + 2));
                italic_text[end - (ptr + 2)] = '\0';
                String* italic_str = create_string(input, italic_text);
                input_add_attribute_item_to_element(input, italic, "text", {.item = s2it(italic_str)});
                input_add_attribute_item_to_element(input, container, "italic", {.item = (uint64_t)italic});
                free(italic_text);
                
                ptr = end + 2;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '_') {
            // _emphasis_
            const char* end = strchr(ptr + 1, '_');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add emphasis element
                Element* em = create_textile_element(input, "em");
                char* em_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(em_text, ptr + 1, end - (ptr + 1));
                em_text[end - (ptr + 1)] = '\0';
                String* em_str = create_string(input, em_text);
                input_add_attribute_item_to_element(input, em, "text", {.item = s2it(em_str)});
                input_add_attribute_item_to_element(input, container, "emphasis", {.item = (uint64_t)em});
                free(em_text);
                
                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '@') {
            // @code@
            const char* end = strchr(ptr + 1, '@');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add code element
                Element* code = create_textile_element(input, "code");
                char* code_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(code_text, ptr + 1, end - (ptr + 1));
                code_text[end - (ptr + 1)] = '\0';
                String* code_str = create_string(input, code_text);
                input_add_attribute_item_to_element(input, code, "text", {.item = s2it(code_str)});
                input_add_attribute_item_to_element(input, container, "code", {.item = (uint64_t)code});
                free(code_text);
                
                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '^') {
            // ^superscript^
            const char* end = strchr(ptr + 1, '^');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add superscript element
                Element* sup = create_textile_element(input, "sup");
                char* sup_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sup_text, ptr + 1, end - (ptr + 1));
                sup_text[end - (ptr + 1)] = '\0';
                String* sup_str = create_string(input, sup_text);
                input_add_attribute_item_to_element(input, sup, "text", {.item = s2it(sup_str)});
                input_add_attribute_item_to_element(input, container, "superscript", {.item = (uint64_t)sup});
                free(sup_text);
                
                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '~') {
            // ~subscript~
            const char* end = strchr(ptr + 1, '~');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(input, before);
                    input_add_attribute_item_to_element(input, container, "text", {.item = s2it(before_str)});
                    free(before);
                }
                
                // Add subscript element
                Element* sub = create_textile_element(input, "sub");
                char* sub_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sub_text, ptr + 1, end - (ptr + 1));
                sub_text[end - (ptr + 1)] = '\0';
                String* sub_str = create_string(input, sub_text);
                input_add_attribute_item_to_element(input, sub, "text", {.item = s2it(sub_str)});
                input_add_attribute_item_to_element(input, container, "subscript", {.item = (uint64_t)sub});
                free(sub_text);
                
                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        }
        
        if (!found_markup) {
            ptr++;
        }
    }
    
    // Add any remaining text
    if (ptr > start) {
        char* remaining = (char*)malloc(ptr - start + 1);
        strncpy(remaining, start, ptr - start);
        remaining[ptr - start] = '\0';
        String* remaining_str = create_string(input, remaining);
        input_add_attribute_item_to_element(input, container, "text", {.item = s2it(remaining_str)});
        free(remaining);
    }
    
    return {.item = (uint64_t)container};
}

static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines) return {.item = ITEM_NULL};
    
    const char* line = lines[*current_line];
    int level;
    char list_type;
    
    // Skip empty lines
    if (is_empty_line(line)) {
        (*current_line)++;
        return {.item = ITEM_NULL};
    }
    
    // Check for headings (h1. to h6.)
    if (is_textile_heading(line, &level)) {
        Element* heading = create_textile_element(input, "heading");
        if (!heading) return {.item = ITEM_NULL};
        
        add_attribute_to_element(input, heading, "level", (level == 1) ? "1" : 
                                (level == 2) ? "2" : (level == 3) ? "3" : 
                                (level == 4) ? "4" : (level == 5) ? "5" : "6");
        
        // Parse modifiers and extract content
        int start_pos = 0;
        char* modifiers = parse_textile_modifiers(line, &start_pos);
        if (modifiers) {
            add_attribute_to_element(input, heading, "modifiers", modifiers);
            free(modifiers);
        }
        
        const char* content = line + start_pos;
        Item inline_content = parse_inline_content(input, content);
        input_add_attribute_item_to_element(input, heading, "content", inline_content);
        
        (*current_line)++;
        return {.item = (uint64_t)heading};
    }
    
    // Check for block code (bc. or bc..)
    if (is_textile_block_code(line)) {
        Element* code_block = create_textile_element(input, "code_block");
        if (!code_block) return {.item = ITEM_NULL};
        
        bool extended = strncmp(line, "bc..", 4) == 0;
        add_attribute_to_element(input, code_block, "extended", extended ? "true" : "false");
        
        // Parse modifiers and extract content
        int start_pos = 0;
        char* modifiers = parse_textile_modifiers(line, &start_pos);
        if (modifiers) {
            add_attribute_to_element(input, code_block, "modifiers", modifiers);
            free(modifiers);
        }
        
        const char* content = line + start_pos;
        String* code_content = create_string(input, content);
        input_add_attribute_item_to_element(input, code_block, "content", {.item = s2it(code_content)});
        
        (*current_line)++;
        
        // For extended blocks, collect until we find another block signature
        if (extended) {
            while (*current_line < total_lines) {
                const char* next_line = lines[*current_line];
                
                // Check if this line starts a new block
                if (is_textile_heading(next_line, NULL) || is_textile_block_code(next_line) ||
                    is_textile_block_quote(next_line) || is_textile_pre(next_line) ||
                    strncmp(next_line, "p.", 2) == 0) {
                    break;
                }
                
                String* line_content = create_string(input, next_line);
                input_add_attribute_item_to_element(input, code_block, "line", {.item = s2it(line_content)});
                (*current_line)++;
            }
        }
        
        return {.item = (uint64_t)code_block};
    }
    
    // Check for block quotes (bq. or bq..)
    if (is_textile_block_quote(line)) {
        Element* quote_block = create_textile_element(input, "blockquote");
        if (!quote_block) return {.item = ITEM_NULL};
        
        bool extended = strncmp(line, "bq..", 4) == 0;
        add_attribute_to_element(input, quote_block, "extended", extended ? "true" : "false");
        
        // Parse modifiers and extract content
        int start_pos = 0;
        char* modifiers = parse_textile_modifiers(line, &start_pos);
        if (modifiers) {
            add_attribute_to_element(input, quote_block, "modifiers", modifiers);
            free(modifiers);
        }
        
        const char* content = line + start_pos;
        Item inline_content = parse_inline_content(input, content);
        input_add_attribute_item_to_element(input, quote_block, "content", inline_content);
        
        (*current_line)++;
        
        // For extended blocks, collect until we find another block signature
        if (extended) {
            while (*current_line < total_lines) {
                const char* next_line = lines[*current_line];
                
                // Check if this line starts a new block
                if (is_textile_heading(next_line, NULL) || is_textile_block_code(next_line) ||
                    is_textile_block_quote(next_line) || is_textile_pre(next_line) ||
                    strncmp(next_line, "p.", 2) == 0) {
                    break;
                }
                
                Item line_content = parse_inline_content(input, next_line);
                input_add_attribute_item_to_element(input, quote_block, "line", line_content);
                (*current_line)++;
            }
        }
        
        return {.item = (uint64_t)quote_block};
    }
    
    // Check for pre-formatted text (pre.)
    if (is_textile_pre(line)) {
        Element* pre_block = create_textile_element(input, "pre");
        if (!pre_block) return {.item = ITEM_NULL};
        
        // Parse modifiers and extract content
        int start_pos = 0;
        char* modifiers = parse_textile_modifiers(line, &start_pos);
        if (modifiers) {
            add_attribute_to_element(input, pre_block, "modifiers", modifiers);
            free(modifiers);
        }
        
        const char* content = line + start_pos;
        String* pre_content = create_string(input, content);
        input_add_attribute_item_to_element(input, pre_block, "content", {.item = s2it(pre_content)});
        
        (*current_line)++;
        return {.item = (uint64_t)pre_block};
    }
    
    // Check for comments (###.)
    if (is_textile_comment(line)) {
        Element* comment = create_textile_element(input, "comment");
        if (!comment) return {.item = ITEM_NULL};
        
        const char* content = line + 4; // Skip "###."
        String* comment_content = create_string(input, content);
        input_add_attribute_item_to_element(input, comment, "content", {.item = s2it(comment_content)});
        
        (*current_line)++;
        return {.item = (uint64_t)comment};
    }
    
    // Check for notextile blocks
    if (is_textile_notextile(line)) {
        Element* notextile = create_textile_element(input, "notextile");
        if (!notextile) return {.item = ITEM_NULL};
        
        bool extended = strncmp(line, "notextile..", 11) == 0;
        const char* content = extended ? line + 11 : line + 10;
        while (*content && isspace(*content)) content++;
        
        String* raw_content = create_string(input, content);
        input_add_attribute_item_to_element(input, notextile, "content", {.item = s2it(raw_content)});
        add_attribute_to_element(input, notextile, "extended", extended ? "true" : "false");
        
        (*current_line)++;
        return {.item = (uint64_t)notextile};
    }
    
    // Check for list items
    if (is_textile_list_item(line, &list_type)) {
        Element* list_item = create_textile_element(input, "list_item");
        if (!list_item) return {.item = ITEM_NULL};
        
        const char* type_str = (list_type == '*') ? "bulleted" : 
                              (list_type == '#') ? "numbered" : 
                              (list_type == '-') ? "definition" : "unknown";
        add_attribute_to_element(input, list_item, "type", type_str);
        
        // Find the content after the list marker
        const char* content = line;
        while (*content && (*content == ' ' || *content == '\t')) content++;
        content++; // Skip the marker (* # -)
        while (*content && (*content == ' ' || *content == '\t')) content++;
        
        if (list_type == '-') {
            // Definition list - split on ":="
            const char* def_sep = strstr(content, ":=");
            if (def_sep) {
                // Term
                char* term = (char*)malloc(def_sep - content + 1);
                strncpy(term, content, def_sep - content);
                term[def_sep - content] = '\0';
                char* trimmed_term = trim_whitespace(term);
                String* term_str = create_string(input, trimmed_term);
                input_add_attribute_item_to_element(input, list_item, "term", {.item = s2it(term_str)});
                free(term);
                free(trimmed_term);
                
                // Definition
                const char* definition = def_sep + 2;
                while (*definition && isspace(*definition)) definition++;
                Item def_content = parse_inline_content(input, definition);
                input_add_attribute_item_to_element(input, list_item, "definition", def_content);
            }
        } else {
            Item item_content = parse_inline_content(input, content);
            input_add_attribute_item_to_element(input, list_item, "content", item_content);
        }
        
        (*current_line)++;
        return {.item = (uint64_t)list_item};
    }
    
    // Default: treat as paragraph
    Element* paragraph = create_textile_element(input, "paragraph");
    if (!paragraph) return {.item = ITEM_NULL};
    
    // Check if it starts with "p." explicitly
    const char* content = line;
    if (strncmp(line, "p.", 2) == 0) {
        int start_pos = 0;
        char* modifiers = parse_textile_modifiers(line, &start_pos);
        if (modifiers) {
            add_attribute_to_element(input, paragraph, "modifiers", modifiers);
            free(modifiers);
        }
        content = line + start_pos;
    }
    
    Item inline_content = parse_inline_content(input, content);
    input_add_attribute_item_to_element(input, paragraph, "content", inline_content);
    
    (*current_line)++;
    return {.item = (uint64_t)paragraph};
}

static Item parse_textile_content(Input *input, char** lines, int line_count) {
    Element* document = create_textile_element(input, "document");
    if (!document) return {.item = ITEM_NULL};
    
    add_attribute_to_element(input, document, "format", "textile");
    
    int current_line = 0;
    while (current_line < line_count) {
        Item block = parse_block_element(input, lines, &current_line, line_count);
        if (block .item != ITEM_NULL) {
            // Add block to document with a unique key
            char key[32];
            snprintf(key, sizeof(key), "block_%d", current_line);
            input_add_attribute_item_to_element(input, document, key, block);
        }
    }
    
    return {.item = (uint64_t)document};
}

void parse_textile(Input* input, const char* textile_string) {
    if (!input || !textile_string) return;
    
    // Initialize string buffer if not already done
    if (!input->sb) {
        input->sb = strbuf_new_pooled(input->pool);
        if (!input->sb) return;
    }
    
    // Split into lines
    int line_count;
    char** lines = split_lines(textile_string, &line_count);
    if (!lines) {
        // Create empty document
        input->root = {.item = s2it(create_string(input, ""))};
        return;
    }
    
    // Parse the textile content
    input->root = parse_textile_content(input, lines, line_count);
    
    // Clean up
    free_lines(lines, line_count);
}
