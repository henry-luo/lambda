#include "../transpiler.h"

// forward declarations
static Item parse_rst_content(Input *input, char** lines, int line_count);
static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_inline_content(Input *input, const char* text);
static Item parse_directive(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_table(Input *input, char** lines, int* current_line, int total_lines);
static Item parse_grid_table(Input *input, char** lines, int* current_line, int total_lines);
static bool is_heading_underline(const char* line, char* marker);
static bool is_table_separator(const char* line);
static bool is_grid_table_line(const char* line);
static Element* create_rst_element(Input *input, const char* tag_name);
static void add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value);

// utility functions
static void skip_whitespace(const char **text) {
    while (**text && (**text == ' ' || **text == '\n' || **text == '\r' || **text == '\t')) {
        (*text)++;
    }
}

static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool is_empty_line(const char* line) {
    while (*line) {
        if (!isspace(*line)) return false;
        line++;
    }
    return true;
}

static int count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

static int count_leading_spaces(const char* str) {
    int count = 0;
    while (str[count] == ' ') count++;
    return count;
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

static String* create_string(Input *input, const char* text) {
    if (!text) return NULL;
    strbuf_append_str(input->sb, text);
    return strbuf_to_string(input->sb);
}

static char** split_lines(const char* text, int* line_count) {
    *line_count = 0;
    
    // count lines
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') (*line_count)++;
        ptr++;
    }
    if (ptr > text && *(ptr-1) != '\n') {
        (*line_count)++; // last line without \n
    }
    
    // allocate array
    char** lines = malloc(*line_count * sizeof(char*));
    
    // split into lines
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
    
    // handle last line if it doesn't end with newline
    if (line_index < *line_count && line_start < ptr) {
        int len = ptr - line_start;
        lines[line_index] = malloc(len + 1);
        strncpy(lines[line_index], line_start, len);
        lines[line_index][len] = '\0';
        line_index++;
    }
    
    // adjust line count to actual lines created
    *line_count = line_index;
    
    return lines;
}

static void free_lines(char** lines, int line_count) {
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}

// block parsing functions
static bool is_heading_underline(const char* line, char* marker) {
    if (!line || strlen(line) < 3) return false;
    
    char ch = line[0];
    // rst underline characters: = - ` : ' " ~ ^ _ * + # < >
    if (ch != '=' && ch != '-' && ch != '`' && ch != ':' && 
        ch != '\'' && ch != '"' && ch != '~' && ch != '^' && 
        ch != '_' && ch != '*' && ch != '+' && ch != '#' && 
        ch != '<' && ch != '>') {
        return false;
    }
    
    // check if entire line is same character
    for (int i = 0; line[i]; i++) {
        if (line[i] != ch && !isspace(line[i])) return false;
    }
    
    if (marker) *marker = ch;
    return true;
}

static bool is_transition_line(const char* line) {
    if (!line || strlen(line) < 4) return false;
    
    int dash_count = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '-') {
            dash_count++;
        } else if (!isspace(line[i])) {
            return false;
        }
    }
    
    return dash_count >= 4;
}

static bool is_bullet_list_item(const char* line) {
    if (!line) return false;
    
    int spaces = count_leading_spaces(line);
    if (spaces >= strlen(line)) return false;
    
    char marker = line[spaces];
    return (marker == '*' || marker == '+' || marker == '-') && 
           (line[spaces + 1] == ' ' || line[spaces + 1] == '\t' || line[spaces + 1] == '\0');
}

static bool is_enumerated_list_item(const char* line, char* enum_type, int* number) {
    if (!line) return false;
    
    int spaces = count_leading_spaces(line);
    if (spaces >= strlen(line)) return false;
    
    const char* start = line + spaces;
    
    // arabic numerals: 1. 2. etc.
    if (isdigit(*start)) {
        int num = 0;
        const char* ptr = start;
        while (isdigit(*ptr)) {
            num = num * 10 + (*ptr - '0');
            ptr++;
        }
        if (*ptr == '.' && (ptr[1] == ' ' || ptr[1] == '\t' || ptr[1] == '\0')) {
            if (enum_type) *enum_type = '1';
            if (number) *number = num;
            return true;
        }
        if (*ptr == ')' && (ptr[1] == ' ' || ptr[1] == '\t' || ptr[1] == '\0')) {
            if (enum_type) *enum_type = ')';
            if (number) *number = num;
            return true;
        }
    }
    
    // lowercase letters: a. b. etc.
    if (*start >= 'a' && *start <= 'z' && 
        (start[1] == '.' || start[1] == ')') && 
        (start[2] == ' ' || start[2] == '\t' || start[2] == '\0')) {
        if (enum_type) *enum_type = (start[1] == '.') ? 'a' : 'a';
        if (number) *number = *start - 'a' + 1;
        return true;
    }
    
    // uppercase letters: A. B. etc.
    if (*start >= 'A' && *start <= 'Z' && 
        (start[1] == '.' || start[1] == ')') && 
        (start[2] == ' ' || start[2] == '\t' || start[2] == '\0')) {
        if (enum_type) *enum_type = (start[1] == '.') ? 'A' : 'A';
        if (number) *number = *start - 'A' + 1;
        return true;
    }
    
    // roman numerals: i. ii. etc.
    if ((*start == 'i' || *start == 'v' || *start == 'x') && 
        strstr(start, ".") && 
        (strstr(start, ". ") || strstr(start, ".\t") || 
         (strlen(start) >= 2 && start[strlen(start)-1] == '.'))) {
        if (enum_type) *enum_type = 'i';
        if (number) *number = 1; // simplified
        return true;
    }
    
    return false;
}

static bool is_definition_list_item(const char* line) {
    if (!line || is_empty_line(line)) return false;
    
    // definition term should not start with whitespace
    if (isspace(line[0])) return false;
    
    // should contain text
    for (int i = 0; line[i]; i++) {
        if (!isspace(line[i])) return true;
    }
    
    return false;
}

static bool is_definition_list_definition(const char* line) {
    if (!line) return false;
    
    // definition should start with indentation
    return isspace(line[0]) && !is_empty_line(line);
}

static bool is_literal_block_marker(const char* line) {
    if (!line) return false;
    
    char* trimmed = trim_whitespace(line);
    bool is_marker = (strlen(trimmed) == 2 && strcmp(trimmed, "::") == 0);
    free(trimmed);
    return is_marker;
}

static bool is_comment_line(const char* line) {
    if (!line) return false;
    
    int spaces = count_leading_spaces(line);
    return line[spaces] == '.' && line[spaces + 1] == '.' && 
           (line[spaces + 2] == ' ' || line[spaces + 2] == '\t' || line[spaces + 2] == '\0');
}

static bool is_directive_line(const char* line) {
    if (!line) return false;
    
    int spaces = count_leading_spaces(line);
    if (line[spaces] != '.' || line[spaces + 1] != '.') return false;
    
    // skip .. and whitespace
    const char* ptr = line + spaces + 2;
    while (*ptr && isspace(*ptr)) ptr++;
    
    // should have directive name followed by ::
    while (*ptr && !isspace(*ptr) && *ptr != ':') ptr++;
    return *ptr == ':' && *(ptr + 1) == ':';
}

static bool is_table_separator(const char* line) {
    if (!line || strlen(line) < 3) return false;
    
    int eq_count = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '=') {
            eq_count++;
        } else if (!isspace(line[i])) {
            return false;
        }
    }
    
    return eq_count >= 3;
}

static bool is_grid_table_line(const char* line) {
    if (!line || strlen(line) < 3) return false;
    
    // grid table lines contain + and - or | characters
    bool has_plus = false;
    bool has_dash_or_pipe = false;
    
    for (int i = 0; line[i]; i++) {
        if (line[i] == '+') {
            has_plus = true;
        } else if (line[i] == '-' || line[i] == '|') {
            has_dash_or_pipe = true;
        } else if (!isspace(line[i])) {
            return false;
        }
    }
    
    return has_plus && has_dash_or_pipe;
}

static Element* create_rst_element(Input *input, const char* tag_name) {
    Element* element = elmt_pooled(input->pool);
    if (!element) return NULL;
    
    TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!element_type) return element;
    
    element->type = element_type;
    
    // set element name
    String* name_str = create_string(input, tag_name);
    if (name_str) {
        element_type->name.str = name_str->chars;
        element_type->name.length = name_str->len;
    }
    
    // properly initialize the list structure
    List* list = (List*)element;
    list->items = NULL;
    list->length = 0;
    list->extra = 0;
    list->capacity = 0;
    
    // initialize type
    element_type->shape = NULL;
    element_type->length = 0;
    element_type->byte_size = 0;
    element_type->content_length = 0;
    
    arraylist_append(input->type_list, element_type);
    element_type->type_index = input->type_list->length - 1;
    
    return element;
}

static void add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value) {
    TypeElmt *element_type = (TypeElmt*)element->type;
    
    // create key and value strings
    String* key = create_string(input, attr_name);
    String* value = create_string(input, attr_value);
    if (!key || !value) return;
    
    // always create a fresh map structure
    Map* attr_map = map_pooled(input->pool);
    if (!attr_map) return;
    
    // initialize the map type
    TypeMap* map_type = map_init_cap(attr_map, input->pool);
    if (!map_type) return;
    
    // just add the new attribute
    LambdaItem lambda_value = (LambdaItem)s2it(value);
    map_put(attr_map, map_type, key, lambda_value, input->pool);
    
    // update element with the new map data
    element->data = attr_map->data;
    element->data_cap = attr_map->data_cap;
    element_type->shape = map_type->shape;
    element_type->length = map_type->length;
    element_type->byte_size = map_type->byte_size;
}

static Item parse_heading(Input *input, char** lines, int* current_line, int total_lines) {
    if (*current_line >= total_lines - 1) return ITEM_NULL;
    
    const char* title_line = lines[*current_line];
    const char* underline = lines[*current_line + 1];
    
    char marker;
    if (!is_heading_underline(underline, &marker)) return ITEM_NULL;
    
    // determine heading level based on marker
    int level = 1;
    switch (marker) {
        case '=': level = 1; break;
        case '-': level = 2; break;
        case '`': level = 3; break;
        case ':': level = 4; break;
        case '\'': level = 5; break;
        case '"': level = 6; break;
        default: level = 6; break;
    }
    
    // create header element
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", level);
    Element* header = create_rst_element(input, tag_name);
    if (!header) return ITEM_NULL;
    
    // add content
    char* content = trim_whitespace(title_line);
    if (content && strlen(content) > 0) {
        Item text_content = parse_inline_content(input, content);
        if (text_content != ITEM_NULL) {
            list_push((List*)header, text_content);
            ((TypeElmt*)header->type)->content_length++;
        }
    }
    free(content);
    
    *current_line += 2; // skip title and underline
    return (Item)header;
}

static Item parse_transition(Input *input) {
    Element* hr = create_rst_element(input, "hr");
    return (Item)hr;
}

static Item parse_bullet_list(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_bullet_list_item(lines[*current_line])) return ITEM_NULL;
    
    Element* list = create_rst_element(input, "ul");
    if (!list) return ITEM_NULL;
    
    while (*current_line < total_lines && is_bullet_list_item(lines[*current_line])) {
        const char* line = lines[*current_line];
        
        // create list item
        Element* list_item = create_rst_element(input, "li");
        if (!list_item) break;
        
        // extract content after bullet
        int spaces = count_leading_spaces(line);
        const char* content_start = line + spaces + 1; // skip bullet
        while (*content_start && isspace(*content_start)) content_start++;
        
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            Item text_content = parse_inline_content(input, content);
            if (text_content != ITEM_NULL) {
                list_push((List*)list_item, text_content);
                ((TypeElmt*)list_item->type)->content_length++;
            }
        }
        free(content);
        
        list_push((List*)list, (Item)list_item);
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
        
        // handle continued content on following lines
        while (*current_line < total_lines) {
            const char* next_line = lines[*current_line];
            if (is_empty_line(next_line)) {
                (*current_line)++;
                continue;
            }
            
            int next_spaces = count_leading_spaces(next_line);
            if (next_spaces > spaces + 1 && !is_bullet_list_item(next_line)) {
                // continued content
                char* continued = trim_whitespace(next_line);
                if (continued && strlen(continued) > 0) {
                    Item continued_content = parse_inline_content(input, continued);
                    if (continued_content != ITEM_NULL) {
                        list_push((List*)list_item, continued_content);
                        ((TypeElmt*)list_item->type)->content_length++;
                    }
                }
                free(continued);
                (*current_line)++;
            } else {
                break;
            }
        }
    }
    
    return (Item)list;
}

static Item parse_enumerated_list(Input *input, char** lines, int* current_line, int total_lines) {
    char enum_type;
    int number;
    
    if (!is_enumerated_list_item(lines[*current_line], &enum_type, &number)) return ITEM_NULL;
    
    Element* list = create_rst_element(input, "ol");
    if (!list) return ITEM_NULL;
    
    // add enumeration type attribute
    char enum_str[20];
    switch (enum_type) {
        case '1': strcpy(enum_str, "decimal"); break;
        case 'a': strcpy(enum_str, "lower-alpha"); break;
        case 'A': strcpy(enum_str, "upper-alpha"); break;
        case 'i': strcpy(enum_str, "lower-roman"); break;
        default: strcpy(enum_str, "decimal"); break;
    }
    add_attribute_to_element(input, list, "type", enum_str);
    
    while (*current_line < total_lines && 
           is_enumerated_list_item(lines[*current_line], NULL, NULL)) {
        const char* line = lines[*current_line];
        
        // create list item
        Element* list_item = create_rst_element(input, "li");
        if (!list_item) break;
        
        // extract content after enumeration
        int spaces = count_leading_spaces(line);
        const char* ptr = line + spaces;
        while (*ptr && !isspace(*ptr)) ptr++; // skip enumeration
        while (*ptr && isspace(*ptr)) ptr++; // skip whitespace
        
        char* content = trim_whitespace(ptr);
        if (content && strlen(content) > 0) {
            Item text_content = parse_inline_content(input, content);
            if (text_content != ITEM_NULL) {
                list_push((List*)list_item, text_content);
                ((TypeElmt*)list_item->type)->content_length++;
            }
        }
        free(content);
        
        list_push((List*)list, (Item)list_item);
        ((TypeElmt*)list->type)->content_length++;
        
        (*current_line)++;
    }
    
    return (Item)list;
}

static Item parse_definition_list(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_definition_list_item(lines[*current_line])) return ITEM_NULL;
    
    Element* def_list = create_rst_element(input, "dl");
    if (!def_list) return ITEM_NULL;
    
    while (*current_line < total_lines && is_definition_list_item(lines[*current_line])) {
        const char* term_line = lines[*current_line];
        
        // create definition term
        Element* dt = create_rst_element(input, "dt");
        if (!dt) break;
        
        char* term_content = trim_whitespace(term_line);
        if (term_content && strlen(term_content) > 0) {
            Item term_text = parse_inline_content(input, term_content);
            if (term_text != ITEM_NULL) {
                list_push((List*)dt, term_text);
                ((TypeElmt*)dt->type)->content_length++;
            }
        }
        free(term_content);
        
        list_push((List*)def_list, (Item)dt);
        ((TypeElmt*)def_list->type)->content_length++;
        
        (*current_line)++;
        
        // parse definition(s)
        while (*current_line < total_lines && 
               is_definition_list_definition(lines[*current_line])) {
            const char* def_line = lines[*current_line];
            
            Element* dd = create_rst_element(input, "dd");
            if (!dd) break;
            
            char* def_content = trim_whitespace(def_line);
            if (def_content && strlen(def_content) > 0) {
                Item def_text = parse_inline_content(input, def_content);
                if (def_text != ITEM_NULL) {
                    list_push((List*)dd, def_text);
                    ((TypeElmt*)dd->type)->content_length++;
                }
            }
            free(def_content);
            
            list_push((List*)def_list, (Item)dd);
            ((TypeElmt*)def_list->type)->content_length++;
            
            (*current_line)++;
        }
    }
    
    return (Item)def_list;
}

static Item parse_literal_block(Input *input, char** lines, int* current_line, int total_lines) {
    // literal block starts with :: on its own line or at end of paragraph
    const char* line = lines[*current_line];
    
    bool is_marker_line = is_literal_block_marker(line);
    bool ends_with_double_colon = false;
    
    if (!is_marker_line) {
        // check if line ends with ::
        char* trimmed = trim_whitespace(line);
        int len = strlen(trimmed);
        ends_with_double_colon = len >= 2 && trimmed[len-2] == ':' && trimmed[len-1] == ':';
        free(trimmed);
        
        if (!ends_with_double_colon) return ITEM_NULL;
    }
    
    Element* pre = create_rst_element(input, "pre");
    if (!pre) return ITEM_NULL;
    
    (*current_line)++;
    
    // collect literal content
    StrBuf* sb = input->sb;
    bool first_line = true;
    int base_indent = -1;
    
    while (*current_line < total_lines) {
        const char* content_line = lines[*current_line];
        
        if (is_empty_line(content_line)) {
            if (!first_line) {
                strbuf_append_char(sb, '\n');
            }
            first_line = false;
            (*current_line)++;
            continue;
        }
        
        int indent = count_leading_spaces(content_line);
        
        // first non-empty line sets base indentation
        if (base_indent == -1) {
            base_indent = indent;
        }
        
        // if line is not indented more than base, end literal block
        if (indent < base_indent) {
            break;
        }
        
        // add line to content
        if (!first_line) {
            strbuf_append_char(sb, '\n');
        }
        
        // add content with base indentation removed
        const char* content_start = content_line + base_indent;
        int content_len = strlen(content_start);
        for (int i = 0; i < content_len; i++) {
            strbuf_append_char(sb, content_start[i]);
        }
        
        first_line = false;
        (*current_line)++;
    }
    
    // create string content
    String *content_str = (String*)sb->str;
    content_str->len = sb->length - sizeof(uint32_t);
    content_str->ref_cnt = 0;
    strbuf_full_reset(sb);
    
    // add content as text
    if (content_str->len > 0) {
        list_push((List*)pre, s2it(content_str));
        ((TypeElmt*)pre->type)->content_length++;
    }
    
    return (Item)pre;
}

static Item parse_comment(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_comment_line(lines[*current_line])) return ITEM_NULL;
    
    Element* comment = create_rst_element(input, "comment");
    if (!comment) return ITEM_NULL;
    
    const char* line = lines[*current_line];
    int spaces = count_leading_spaces(line);
    const char* content_start = line + spaces + 2; // skip ..
    while (*content_start && isspace(*content_start)) content_start++;
    
    char* content = trim_whitespace(content_start);
    if (content && strlen(content) > 0) {
        String* comment_text = create_string(input, content);
        if (comment_text) {
            list_push((List*)comment, s2it(comment_text));
            ((TypeElmt*)comment->type)->content_length++;
        }
    }
    free(content);
    
    (*current_line)++;
    return (Item)comment;
}

static Item parse_directive(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_directive_line(lines[*current_line])) return ITEM_NULL;
    
    const char* line = lines[*current_line];
    int spaces = count_leading_spaces(line);
    const char* ptr = line + spaces + 2; // skip ..
    while (*ptr && isspace(*ptr)) ptr++;
    
    // extract directive name
    const char* name_start = ptr;
    while (*ptr && !isspace(*ptr) && *ptr != ':') ptr++;
    
    int name_len = ptr - name_start;
    char* directive_name = malloc(name_len + 1);
    strncpy(directive_name, name_start, name_len);
    directive_name[name_len] = '\0';
    
    // skip ::
    while (*ptr && *ptr != ':') ptr++;
    if (*ptr == ':') ptr++;
    if (*ptr == ':') ptr++;
    while (*ptr && isspace(*ptr)) ptr++;
    
    // create directive element
    Element* directive = create_rst_element(input, "directive");
    if (!directive) {
        free(directive_name);
        return ITEM_NULL;
    }
    
    add_attribute_to_element(input, directive, "name", directive_name);
    
    // extract arguments if any
    if (*ptr != '\0') {
        char* args = trim_whitespace(ptr);
        if (args && strlen(args) > 0) {
            add_attribute_to_element(input, directive, "arguments", args);
        }
        free(args);
    }
    
    free(directive_name);
    (*current_line)++;
    
    // parse directive content (indented block)
    while (*current_line < total_lines) {
        const char* content_line = lines[*current_line];
        
        if (is_empty_line(content_line)) {
            (*current_line)++;
            continue;
        }
        
        int indent = count_leading_spaces(content_line);
        if (indent <= spaces) break; // end of directive content
        
        char* content = trim_whitespace(content_line);
        if (content && strlen(content) > 0) {
            Item content_item = parse_inline_content(input, content);
            if (content_item != ITEM_NULL) {
                list_push((List*)directive, content_item);
                ((TypeElmt*)directive->type)->content_length++;
            }
        }
        free(content);
        
        (*current_line)++;
    }
    
    return (Item)directive;
}

static Item parse_table(Input *input, char** lines, int* current_line, int total_lines) {
    if (!is_table_separator(lines[*current_line])) return ITEM_NULL;
    
    // simple table format:
    // ====== ====== ======
    // Header Header Header
    // ====== ====== ======
    // Data   Data   Data
    // ====== ====== ======
    
    Element* table = create_rst_element(input, "table");
    if (!table) return ITEM_NULL;
    
    (*current_line)++; // skip first separator
    
    if (*current_line >= total_lines) return (Item)table;
    
    // parse header row
    const char* header_line = lines[*current_line];
    if (is_table_separator(header_line)) {
        // no header, just data
        (*current_line)--;
    } else {
        Element* thead = create_rst_element(input, "thead");
        Element* header_row = create_rst_element(input, "tr");
        
        if (thead && header_row) {
            // split header by whitespace
            char* header_copy = strdup(header_line);
            char* token = strtok(header_copy, " \t");
            
            while (token) {
                Element* th = create_rst_element(input, "th");
                if (th) {
                    Item cell_content = parse_inline_content(input, token);
                    if (cell_content != ITEM_NULL) {
                        list_push((List*)th, cell_content);
                        ((TypeElmt*)th->type)->content_length++;
                    }
                    list_push((List*)header_row, (Item)th);
                    ((TypeElmt*)header_row->type)->content_length++;
                }
                token = strtok(NULL, " \t");
            }
            
            free(header_copy);
            
            list_push((List*)thead, (Item)header_row);
            ((TypeElmt*)thead->type)->content_length++;
            
            list_push((List*)table, (Item)thead);
            ((TypeElmt*)table->type)->content_length++;
        }
        
        (*current_line)++;
        
        // skip separator after header
        if (*current_line < total_lines && is_table_separator(lines[*current_line])) {
            (*current_line)++;
        }
    }
    
    // parse data rows
    Element* tbody = create_rst_element(input, "tbody");
    if (tbody) {
        while (*current_line < total_lines && !is_table_separator(lines[*current_line])) {
            const char* row_line = lines[*current_line];
            
            if (is_empty_line(row_line)) {
                (*current_line)++;
                continue;
            }
            
            Element* row = create_rst_element(input, "tr");
            if (row) {
                // split row by whitespace
                char* row_copy = strdup(row_line);
                char* token = strtok(row_copy, " \t");
                
                while (token) {
                    Element* td = create_rst_element(input, "td");
                    if (td) {
                        Item cell_content = parse_inline_content(input, token);
                        if (cell_content != ITEM_NULL) {
                            list_push((List*)td, cell_content);
                            ((TypeElmt*)td->type)->content_length++;
                        }
                        list_push((List*)row, (Item)td);
                        ((TypeElmt*)row->type)->content_length++;
                    }
                    token = strtok(NULL, " \t");
                }
                
                free(row_copy);
                
                list_push((List*)tbody, (Item)row);
                ((TypeElmt*)tbody->type)->content_length++;
            }
            
            (*current_line)++;
        }
        
        if (((TypeElmt*)tbody->type)->content_length > 0) {
            list_push((List*)table, (Item)tbody);
            ((TypeElmt*)table->type)->content_length++;
        }
        
        // skip final separator
        if (*current_line < total_lines && is_table_separator(lines[*current_line])) {
            (*current_line)++;
        }
    }
    
    return (Item)table;
}

static Item parse_paragraph(Input *input, const char* line) {
    char* content = trim_whitespace(line);
    if (!content || strlen(content) == 0) {
        free(content);
        return ITEM_NULL;
    }
    
    Element* paragraph = create_rst_element(input, "p");
    if (!paragraph) {
        free(content);
        return ITEM_NULL;
    }
    
    Item text_content = parse_inline_content(input, content);
    if (text_content != ITEM_NULL) {
        list_push((List*)paragraph, text_content);
        ((TypeElmt*)paragraph->type)->content_length++;
    }
    
    free(content);
    return (Item)paragraph;
}

// inline parsing functions
static Item parse_emphasis(Input *input, const char* text, int* pos) {
    if (text[*pos] != '*') return ITEM_NULL;
    
    int start_pos = *pos;
    int star_count = 0;
    
    // count stars at start
    while (text[*pos] == '*') {
        star_count++;
        (*pos)++;
    }
    
    if (star_count == 0) return ITEM_NULL;
    
    int content_start = *pos;
    int content_end = -1;
    
    // find closing stars
    while (text[*pos] != '\0') {
        if (text[*pos] == '*') {
            int close_star_count = 0;
            int temp_pos = *pos;
            
            while (text[temp_pos] == '*') {
                close_star_count++;
                temp_pos++;
            }
            
            if (close_star_count >= star_count) {
                content_end = *pos;
                *pos = temp_pos;
                break;
            }
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // no closing marker found, revert
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    // create element
    const char* tag_name = (star_count >= 2) ? "strong" : "em";
    Element* emphasis_elem = create_rst_element(input, tag_name);
    if (!emphasis_elem) return ITEM_NULL;
    
    // extract content between markers
    int content_len = content_end - content_start;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        Item text_content = parse_inline_content(input, content);
        if (text_content != ITEM_NULL) {
            list_push((List*)emphasis_elem, text_content);
            ((TypeElmt*)emphasis_elem->type)->content_length++;
        }
    }
    
    free(content);
    return (Item)emphasis_elem;
}

static Item parse_literal(Input *input, const char* text, int* pos) {
    if (text[*pos] != '`' || text[*pos + 1] != '`') return ITEM_NULL;
    
    int start_pos = *pos;
    *pos += 2; // skip opening ``
    
    int content_start = *pos;
    int content_end = -1;
    
    // find closing ``
    while (text[*pos] != '\0' && text[*pos + 1] != '\0') {
        if (text[*pos] == '`' && text[*pos + 1] == '`') {
            content_end = *pos;
            *pos += 2;
            break;
        }
        (*pos)++;
    }
    
    if (content_end == -1) {
        // no closing marker found, revert
        *pos = start_pos;
        return ITEM_NULL;
    }
    
    Element* code_elem = create_rst_element(input, "code");
    if (!code_elem) return ITEM_NULL;
    
    // extract content between markers
    int content_len = content_end - content_start;
    char* content = malloc(content_len + 1);
    strncpy(content, text + content_start, content_len);
    content[content_len] = '\0';
    
    String* code_str = create_string(input, content);
    if (code_str) {
        list_push((List*)code_elem, s2it(code_str));
        ((TypeElmt*)code_elem->type)->content_length++;
    }
    
    free(content);
    return (Item)code_elem;
}

static Item parse_reference(Input *input, const char* text, int* pos) {
    if (text[*pos] != '_' || *pos == 0) return ITEM_NULL;
    
    // find start of reference (work backwards)
    int ref_start = *pos - 1;
    while (ref_start > 0 && !isspace(text[ref_start - 1])) {
        ref_start--;
    }
    
    // extract reference text
    int ref_len = *pos - ref_start;
    char* ref_text = malloc(ref_len + 1);
    strncpy(ref_text, text + ref_start, ref_len);
    ref_text[ref_len] = '\0';
    
    Element* ref_elem = create_rst_element(input, "a");
    if (!ref_elem) {
        free(ref_text);
        return ITEM_NULL;
    }
    
    add_attribute_to_element(input, ref_elem, "href", ref_text);
    
    String* link_text = create_string(input, ref_text);
    if (link_text) {
        list_push((List*)ref_elem, s2it(link_text));
        ((TypeElmt*)ref_elem->type)->content_length++;
    }
    
    free(ref_text);
    (*pos)++; // skip _
    return (Item)ref_elem;
}

static Item parse_inline_content(Input *input, const char* text) {
    if (!text || strlen(text) == 0) {
        return s2it(create_string(input, ""));
    }
    
    int len = strlen(text);
    int pos = 0;
    
    // create a span to hold mixed content
    Element* span = create_rst_element(input, "span");
    if (!span) return s2it(create_string(input, text));
    
    StrBuf* text_buffer = strbuf_new_cap(256);
    
    while (pos < len) {
        char ch = text[pos];
        
        // check for various inline elements
        if (ch == '*') {
            // flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item emphasis = parse_emphasis(input, text, &pos);
            if (emphasis != ITEM_NULL) {
                list_push((List*)span, emphasis);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '`' && pos < len - 1 && text[pos + 1] == '`') {
            // flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item literal = parse_literal(input, text, &pos);
            if (literal != ITEM_NULL) {
                list_push((List*)span, literal);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        } else if (ch == '_' && pos > 0 && !isspace(text[pos - 1])) {
            // flush any accumulated text
            if (text_buffer->length > 0) {
                strbuf_append_char(text_buffer, '\0');
                String* text_str = create_string(input, text_buffer->str);
                if (text_str && text_str->len > 0) {
                    list_push((List*)span, s2it(text_str));
                    ((TypeElmt*)span->type)->content_length++;
                }
                strbuf_reset(text_buffer);
            }
            
            Item reference = parse_reference(input, text, &pos);
            if (reference != ITEM_NULL) {
                list_push((List*)span, reference);
                ((TypeElmt*)span->type)->content_length++;
                continue;
            }
        }
        
        // if no special parsing occurred, add character to text buffer
        strbuf_append_char(text_buffer, ch);
        pos++;
    }
    
    // flush any remaining text
    if (text_buffer->length > 0) {
        strbuf_append_char(text_buffer, '\0');
        String* text_str = create_string(input, text_buffer->str);
        if (text_str && text_str->len > 0) {
            list_push((List*)span, s2it(text_str));
            ((TypeElmt*)span->type)->content_length++;
        }
    }
    
    strbuf_free(text_buffer);
    
    // if span has only one text child, return just the text
    if (((TypeElmt*)span->type)->content_length == 1) {
        List* span_list = (List*)span;
        return list_get(span_list, 0);
    }
    
    return (Item)span;
}

static Item parse_block_element(Input *input, char** lines, int* current_line, int total_lines) {
    const char* line = lines[*current_line];
    
    if (!line || is_empty_line(line)) {
        return ITEM_NULL;
    }
    
    // try heading (need to check next line for underline)
    if (*current_line < total_lines - 1) {
        char marker;
        if (is_heading_underline(lines[*current_line + 1], &marker)) {
            return parse_heading(input, lines, current_line, total_lines);
        }
    }
    
    // try different block types
    if (is_transition_line(line)) {
        (*current_line)++; // single line element
        return parse_transition(input);
    } else if (is_comment_line(line)) {
        return parse_comment(input, lines, current_line, total_lines);
    } else if (is_directive_line(line)) {
        return parse_directive(input, lines, current_line, total_lines);
    } else if (is_literal_block_marker(line) || 
               (strlen(line) >= 2 && line[strlen(line)-2] == ':' && line[strlen(line)-1] == ':')) {
        return parse_literal_block(input, lines, current_line, total_lines);
    } else if (is_table_separator(line)) {
        return parse_table(input, lines, current_line, total_lines);
    } else if (is_bullet_list_item(line)) {
        return parse_bullet_list(input, lines, current_line, total_lines);
    } else if (is_enumerated_list_item(line, NULL, NULL)) {
        return parse_enumerated_list(input, lines, current_line, total_lines);
    } else if (is_definition_list_item(line) && 
               *current_line < total_lines - 1 && 
               is_definition_list_definition(lines[*current_line + 1])) {
        return parse_definition_list(input, lines, current_line, total_lines);
    } else {
        Item result = parse_paragraph(input, line);
        (*current_line)++; // single line element
        return result;
    }
}

static Item parse_rst_content(Input *input, char** lines, int line_count) {
    Element* document = create_rst_element(input, "document");
    if (!document) return ITEM_NULL;
    
    int current_line = 0;
    
    while (current_line < line_count) {
        // skip empty lines
        if (!lines[current_line] || is_empty_line(lines[current_line])) {
            current_line++;
            continue;
        }
        
        Item element = parse_block_element(input, lines, &current_line, line_count);
        
        if (element != ITEM_NULL) {
            list_push((List*)document, element);
            ((TypeElmt*)document->type)->content_length++;
        } else {
            // if no element was parsed, advance to avoid infinite loop
            current_line++;
        }
    }
    
    return (Item)document;
}

void parse_rst(Input* input, const char* rst_string) {
    input->sb = strbuf_new_pooled(input->pool);
    int line_count;
    char** lines = split_lines(rst_string, &line_count);
    input->root = parse_rst_content(input, lines, line_count);
    free_lines(lines, line_count);
}
