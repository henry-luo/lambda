// pdf parser implementation
#include "../transpiler.h"

static Item parse_pdf_object(Input *input, const char **pdf);
static String* parse_pdf_name(Input *input, const char **pdf);
static String* parse_pdf_string(Input *input, const char **pdf);
static Item parse_pdf_indirect_ref(Input *input, const char **pdf);
static Item parse_pdf_indirect_object(Input *input, const char **pdf);
static Item parse_pdf_stream(Input *input, const char **pdf, Map* dict);
static Item parse_pdf_xref_table(Input *input, const char **pdf);
static Item parse_pdf_trailer(Input *input, const char **pdf);
static Item analyze_pdf_content_stream(Input *input, const char *stream_data, int length);
static Item parse_pdf_font_descriptor(Input *input, Map* font_dict);
static Item extract_pdf_page_info(Input *input, Map* page_dict);
static bool is_valid_pdf_header(const char *pdf_content);
static void advance_safely(const char **pdf, int max_advance);

// PDF object types
typedef enum {
    PDF_OBJ_NULL,
    PDF_OBJ_BOOLEAN,
    PDF_OBJ_NUMBER,
    PDF_OBJ_STRING,
    PDF_OBJ_NAME,
    PDF_OBJ_ARRAY,
    PDF_OBJ_DICT,
    PDF_OBJ_STREAM,
    PDF_OBJ_INDIRECT_REF,
    PDF_OBJ_INDIRECT_OBJ
} PDFObjectType;

// PDF-specific structures
typedef struct {
    int obj_num;
    int gen_num;
} IndirectObjectRef;

typedef struct {
    Map* dict;
    String* stream_data;
    int length;
} StreamObject;

static void skip_whitespace(const char **pdf) {
    while (**pdf && (**pdf == ' ' || **pdf == '\n' || **pdf == '\r' || 
           **pdf == '\t' || **pdf == '\f')) {
        (*pdf)++;
    }
}

static void skip_comments(const char **pdf) {
    while (**pdf == '%') {
        // skip to end of line
        while (**pdf && **pdf != '\n' && **pdf != '\r') {
            (*pdf)++;
        }
        skip_whitespace(pdf);
    }
}

static void skip_whitespace_and_comments(const char **pdf) {
    while (**pdf) {
        skip_whitespace(pdf);
        if (**pdf == '%') {
            skip_comments(pdf);
        } else {
            break;
        }
    }
}

static bool is_valid_pdf_header(const char *pdf_content) {
    if (!pdf_content) return false;
    // Check for PDF header signature
    return strncmp(pdf_content, "%PDF-", 5) == 0;
}

static void advance_safely(const char **pdf, int max_advance) {
    int advance_count = 0;
    while (**pdf && advance_count < max_advance) {
        (*pdf)++;
        advance_count++;
    }
}

static Item parse_pdf_number(Input *input, const char **pdf) {
    double *dval;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(double), (void**)&dval);
    if (err != MEM_POOL_ERR_OK) return ITEM_ERROR;
    
    char* end;
    *dval = strtod(*pdf, &end);
    *pdf = end;
    
    return d2it(dval);
}

static Array* parse_pdf_array(Input *input, const char **pdf) {
    if (**pdf != '[') return NULL;
    
    (*pdf)++; // skip [
    skip_whitespace_and_comments(pdf);
    
    Array* arr = array_pooled(input->pool);
    if (!arr) return NULL;
    
    int item_count = 0;
    while (**pdf && **pdf != ']' && item_count < 10) { // reduced limit for safety
        Item obj = parse_pdf_object(input, pdf);
        if (obj != ITEM_ERROR && obj != ITEM_NULL) {
            LambdaItem lambda_obj = {.item = obj};
            array_append(arr, lambda_obj, input->pool);
            item_count++;
        }
        skip_whitespace_and_comments(pdf);
    }
    
    if (**pdf == ']') {
        (*pdf)++; // skip ]
    }
    
    return arr;
}

static Map* parse_pdf_dictionary(Input *input, const char **pdf) {
    if (!(**pdf == '<' && *(*pdf + 1) == '<')) return NULL;
    
    *pdf += 2; // skip <<
    skip_whitespace_and_comments(pdf);
    
    Map* dict = map_pooled(input->pool);
    if (!dict) return NULL;
    
    TypeMap* dict_type = map_init_cap(dict, input->pool);
    if (!dict->data) return dict;
    
    int pair_count = 0;
    while (**pdf && !(**pdf == '>' && *(*pdf + 1) == '>') && pair_count < 10) { // reduced limit for safety
        // parse key (should be a name)
        if (**pdf != '/') {
            // skip non-name, might be malformed
            (*pdf)++;
            continue;
        }
        
        String* key = parse_pdf_name(input, pdf);
        if (!key) break;
        
        skip_whitespace_and_comments(pdf);
        
        // parse value
        Item value = parse_pdf_object(input, pdf);
        if (value != ITEM_ERROR && value != ITEM_NULL) {
            LambdaItem lambda_value = {.item = value};
            map_put(dict, dict_type, key, lambda_value, input->pool);
            pair_count++;
        }
        
        skip_whitespace_and_comments(pdf);
    }
    
    if (**pdf == '>' && *(*pdf + 1) == '>') {
        *pdf += 2; // skip >>
    }
    
    arraylist_append(input->type_list, dict_type);
    dict_type->type_index = input->type_list->length - 1;
    
    return dict;
}

static String* parse_pdf_name(Input *input, const char **pdf) {
    if (**pdf != '/') return NULL;
    
    (*pdf)++; // skip /
    StrBuf* sb = input->sb;
    int char_count = 0;
    int max_chars = 100; // Safety limit for name length
    
    while (**pdf && char_count < max_chars && 
           !isspace(**pdf) && **pdf != '/' && **pdf != '(' && **pdf != ')' && 
           **pdf != '<' && **pdf != '>' && **pdf != '[' && **pdf != ']' && 
           **pdf != '{' && **pdf != '}' && **pdf != '%' && **pdf != '\0') {
        if (**pdf == '#') {
            // hex escape in name
            (*pdf)++; // skip #
            if (isxdigit(**pdf) && isxdigit(*(*pdf + 1))) {
                char hex_string[3] = {**pdf, *(*pdf + 1), '\0'};
                int char_value = (int)strtol(hex_string, NULL, 16);
                strbuf_append_char(sb, (char)char_value);
                (*pdf) += 2;
            } else {
                // malformed hex escape, just include the #
                strbuf_append_char(sb, '#');
            }
        } else {
            strbuf_append_char(sb, **pdf);
            (*pdf)++;
        }
        char_count++;
    }
    
    return strbuf_to_string(sb);
}

static Array* parse_pdf_array(Input *input, const char **pdf);
static Map* parse_pdf_dictionary(Input *input, const char **pdf);

static String* parse_pdf_string(Input *input, const char **pdf) {
    if (**pdf != '(' && **pdf != '<') return NULL;
    
    StrBuf* sb = input->sb;
    
    if (**pdf == '(') {
        // literal string
        (*pdf)++; // skip (
        int paren_count = 1;
        int char_count = 0;
        int max_chars = 500; // Safety limit
        
        while (**pdf && paren_count > 0 && char_count < max_chars) {
            if (**pdf == '\\') {
                (*pdf)++; // skip backslash
                if (**pdf) {
                    switch (**pdf) {
                        case 'n': strbuf_append_char(sb, '\n'); break;
                        case 'r': strbuf_append_char(sb, '\r'); break;
                        case 't': strbuf_append_char(sb, '\t'); break;
                        case 'b': strbuf_append_char(sb, '\b'); break;
                        case 'f': strbuf_append_char(sb, '\f'); break;
                        case '(': strbuf_append_char(sb, '('); break;
                        case ')': strbuf_append_char(sb, ')'); break;
                        case '\\': strbuf_append_char(sb, '\\'); break;
                        case '\n': /* ignore escaped newline */ break;
                        case '\r': /* ignore escaped carriage return */ break;
                        default: strbuf_append_char(sb, **pdf); break;
                    }
                    (*pdf)++;
                    char_count++;
                }
            } else if (**pdf == '(') {
                paren_count++;
                strbuf_append_char(sb, **pdf);
                (*pdf)++;
                char_count++;
            } else if (**pdf == ')') {
                paren_count--;
                if (paren_count > 0) {
                    strbuf_append_char(sb, **pdf);
                    char_count++;
                }
                (*pdf)++;
            } else {
                strbuf_append_char(sb, **pdf);
                (*pdf)++;
                char_count++;
            }
        }
    } else if (**pdf == '<') {
        // hexadecimal string
        (*pdf)++; // skip <
        int char_count = 0;
        int max_chars = 500; // Safety limit
        
        while (**pdf && **pdf != '>' && char_count < max_chars) {
            if (isxdigit(**pdf)) {
                char hex_char = **pdf;
                (*pdf)++;
                if (isxdigit(**pdf)) {
                    char hex_string[3] = {hex_char, **pdf, '\0'};
                    int char_value = (int)strtol(hex_string, NULL, 16);
                    strbuf_append_char(sb, (char)char_value);
                    (*pdf)++;
                } else {
                    // single hex digit, treat as 0X
                    char hex_string[3] = {hex_char, '0', '\0'};
                    int char_value = (int)strtol(hex_string, NULL, 16);
                    strbuf_append_char(sb, (char)char_value);
                }
                char_count++;
            } else {
                (*pdf)++; // skip non-hex chars
            }
        }
        if (**pdf == '>') {
            (*pdf)++; // skip >
        }
    }
    
    return strbuf_to_string(sb);
}

static bool is_digit_or_space_ahead(const char *pdf, int max_lookahead) {
    for (int i = 0; i < max_lookahead && pdf[i]; i++) {
        if (isdigit(pdf[i]) || pdf[i] == ' ' || pdf[i] == 'R') {
            return true;
        }
        if (!isspace(pdf[i])) {
            return false;
        }
    }
    return false;
}

static Item parse_pdf_object(Input *input, const char **pdf) {
    static int call_count = 0;
    call_count++;
    
    // prevent runaway recursion - much lower limit for safety
    if (call_count > 10) {
        printf("Warning: too many parse calls, stopping recursion\n");
        call_count--;
        return ITEM_NULL;
    }
    
    skip_whitespace_and_comments(pdf);
    
    if (!**pdf) {
        call_count--;
        return ITEM_NULL;
    }
    
    Item result = ITEM_ERROR;
    
    // Check for special PDF keywords first
    if (strncmp(*pdf, "endobj", 6) == 0) {
        *pdf += 6;
        result = ITEM_NULL; // Signal end of object
    }
    // Check for stream keywords
    else if (strncmp(*pdf, "stream", 6) == 0) {
        result = ITEM_NULL; // Stream handling is done elsewhere
    }
    else if (strncmp(*pdf, "endstream", 9) == 0) {
        *pdf += 9;
        result = ITEM_NULL; // Signal end of stream
    }
    // check for null
    else if (strncmp(*pdf, "null", 4) == 0 && (!isalnum(*(*pdf + 4)))) {
        *pdf += 4;
        result = ITEM_NULL;
    }
    // check for boolean values
    else if (strncmp(*pdf, "true", 4) == 0 && (!isalnum(*(*pdf + 4)))) {
        *pdf += 4;
        result = b2it(true);
    }
    else if (strncmp(*pdf, "false", 5) == 0 && (!isalnum(*(*pdf + 5)))) {
        *pdf += 5;
        result = b2it(false);
    }
    // check for names
    else if (**pdf == '/') {
        String* name = parse_pdf_name(input, pdf);
        result = name ? s2it(name) : ITEM_ERROR;
    }
    // check for simple strings (no complex nesting)
    else if (**pdf == '(' || (**pdf == '<' && *(*pdf + 1) != '<')) {
        String* str = parse_pdf_string(input, pdf);
        result = str ? s2it(str) : ITEM_ERROR;
    }
    // check for indirect references (n m R) before numbers
    else if ((**pdf >= '0' && **pdf <= '9') && is_digit_or_space_ahead(*pdf + 1, 10)) {
        const char* saved_pos = *pdf;
        // Try to parse as indirect reference first
        Item ref = parse_pdf_indirect_ref(input, pdf);
        if (ref != ITEM_ERROR) {
            result = ref;
        } else {
            // if not a reference, parse as number
            *pdf = saved_pos;
            result = parse_pdf_number(input, pdf);
        }
    }
    // check for numbers
    else if ((**pdf >= '0' && **pdf <= '9') || **pdf == '-' || **pdf == '+' || **pdf == '.') {
        result = parse_pdf_number(input, pdf);
    }
    // check for simple arrays (limited depth)
    else if (**pdf == '[' && call_count <= 3) {
        Array* arr = parse_pdf_array(input, pdf);
        result = arr ? (Item)arr : ITEM_ERROR;
    }
    // check for simple dictionaries (limited depth) 
    else if (**pdf == '<' && *(*pdf + 1) == '<' && call_count <= 3) {
        Map* dict = parse_pdf_dictionary(input, pdf);
        if (dict) {
            // Check if this dictionary is followed by a stream
            const char* saved_pos = *pdf;
            skip_whitespace_and_comments(pdf);
            if (strncmp(*pdf, "stream", 6) == 0) {
                // Parse as stream with dictionary
                Item stream = parse_pdf_stream(input, pdf, dict);
                result = stream != ITEM_ERROR ? stream : (Item)dict;
            } else {
                // Just a dictionary
                *pdf = saved_pos;
                result = (Item)dict;
            }
        } else {
            result = ITEM_ERROR;
        }
    }
    // skip complex structures (streams and other complex cases)
    else {
        advance_safely(pdf, 1);
        result = ITEM_NULL;
    }
    
    call_count--;
    return result;
}

static Item parse_pdf_indirect_ref(Input *input, const char **pdf) {
    // expects format "n m R" where n and m are numbers
    const char* start_pos = *pdf;
    int obj_num = 0, gen_num = 0;
    char* end;
    
    // Parse object number
    obj_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return ITEM_ERROR; // no conversion
    
    *pdf = end;
    skip_whitespace_and_comments(pdf);
    if (!**pdf) return ITEM_ERROR;
    
    // Parse generation number
    gen_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return ITEM_ERROR; // no conversion
    
    *pdf = end;
    skip_whitespace_and_comments(pdf);
    if (**pdf != 'R') return ITEM_ERROR;
    (*pdf)++; // skip R
    
    // Create a map to represent the indirect reference (more serializable than custom struct)
    Map* ref_map = map_pooled(input->pool);
    if (!ref_map) return ITEM_ERROR;
    
    TypeMap* ref_type = map_init_cap(ref_map, input->pool);
    if (!ref_map->data) return (Item)ref_map;
    
    // Store type identifier
    String* type_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 14, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "indirect_ref");
            type_value->len = 12;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(ref_map, ref_type, type_key, type_item, input->pool);
        }
    }
    
    // Store object number
    String* obj_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 12, (void**)&obj_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(obj_key->chars, "object_num");
        obj_key->len = 10;
        obj_key->ref_cnt = 0;
        
        double* obj_val;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&obj_val);
        if (err == MEM_POOL_ERR_OK) {
            *obj_val = (double)obj_num;
            LambdaItem obj_item = {.item = d2it(obj_val)};
            map_put(ref_map, ref_type, obj_key, obj_item, input->pool);
        }
    }
    
    // Store generation number
    String* gen_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&gen_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(gen_key->chars, "gen_num");
        gen_key->len = 7;
        gen_key->ref_cnt = 0;
        
        double* gen_val;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&gen_val);
        if (err == MEM_POOL_ERR_OK) {
            *gen_val = (double)gen_num;
            LambdaItem gen_item = {.item = d2it(gen_val)};
            map_put(ref_map, ref_type, gen_key, gen_item, input->pool);
        }
    }
    
    arraylist_append(input->type_list, ref_type);
    ref_type->type_index = input->type_list->length - 1;
    
    return (Item)ref_map;
}

static Item parse_pdf_indirect_object(Input *input, const char **pdf) {
    // expects format "n m obj ... endobj"
    int obj_num = 0, gen_num = 0;
    char* end;
    
    // Parse object number
    obj_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return ITEM_ERROR;
    
    *pdf = end;
    skip_whitespace_and_comments(pdf);
    
    // Parse generation number
    gen_num = strtol(*pdf, &end, 10);
    if (end == *pdf) return ITEM_ERROR;
    
    *pdf = end;
    skip_whitespace_and_comments(pdf);
    
    // Check for "obj" keyword
    if (strncmp(*pdf, "obj", 3) != 0) return ITEM_ERROR;
    *pdf += 3;
    skip_whitespace_and_comments(pdf);
    
    // Parse the object content
    Item content = parse_pdf_object(input, pdf);
    
    // Skip to endobj (optional - for safety)
    const char* endobj_pos = strstr(*pdf, "endobj");
    if (endobj_pos) {
        *pdf = endobj_pos + 6; // skip "endobj"
    }
    
    // Create a map to represent the indirect object
    Map* obj_map = map_pooled(input->pool);
    if (!obj_map) return content; // return content if we can't create wrapper
    
    TypeMap* obj_type = map_init_cap(obj_map, input->pool);
    if (!obj_map->data) return content;
    
    // Store type identifier
    String* type_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 16, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "indirect_object");
            type_value->len = 15;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(obj_map, obj_type, type_key, type_item, input->pool);
        }
    }
    
    // Store object number
    String* obj_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 12, (void**)&obj_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(obj_key->chars, "object_num");
        obj_key->len = 10;
        obj_key->ref_cnt = 0;
        
        double* obj_val;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&obj_val);
        if (err == MEM_POOL_ERR_OK) {
            *obj_val = (double)obj_num;
            LambdaItem obj_item = {.item = d2it(obj_val)};
            map_put(obj_map, obj_type, obj_key, obj_item, input->pool);
        }
    }
    
    // Store generation number
    String* gen_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&gen_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(gen_key->chars, "gen_num");
        gen_key->len = 7;
        gen_key->ref_cnt = 0;
        
        double* gen_val;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&gen_val);
        if (err == MEM_POOL_ERR_OK) {
            *gen_val = (double)gen_num;
            LambdaItem gen_item = {.item = d2it(gen_val)};
            map_put(obj_map, obj_type, gen_key, gen_item, input->pool);
        }
    }
    
    // Store content
    if (content != ITEM_ERROR && content != ITEM_NULL) {
        String* content_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&content_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(content_key->chars, "content");
            content_key->len = 7;
            content_key->ref_cnt = 0;
            
            LambdaItem content_item = {.item = content};
            map_put(obj_map, obj_type, content_key, content_item, input->pool);
        }
    }
    
    arraylist_append(input->type_list, obj_type);
    obj_type->type_index = input->type_list->length - 1;
    
    return (Item)obj_map;
}

static Item parse_pdf_stream(Input *input, const char **pdf, Map* dict) {
    // expects stream data after dictionary
    if (strncmp(*pdf, "stream", 6) != 0) return ITEM_ERROR;
    
    *pdf += 6; // skip stream
    skip_whitespace_and_comments(pdf);
    
    // find end of stream
    const char* end_stream = strstr(*pdf, "endstream");
    if (!end_stream) return ITEM_ERROR;
    
    // calculate data length
    int data_length = end_stream - *pdf;
    if (data_length > 1000) { // Safety limit for stream size
        data_length = 1000;
    }
    
    // Create a map to represent the stream (more serializable than custom struct)
    Map* stream_map = map_pooled(input->pool);
    if (!stream_map) return ITEM_ERROR;
    
    TypeMap* stream_type = map_init_cap(stream_map, input->pool);
    if (!stream_map->data) return (Item)stream_map;
    
    // Store type identifier
    String* type_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 7, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "stream");
            type_value->len = 6;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(stream_map, stream_type, type_key, type_item, input->pool);
        }
    }
    
    // Store dictionary if provided
    if (dict) {
        String* dict_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&dict_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(dict_key->chars, "dictionary");
            dict_key->len = 10;
            dict_key->ref_cnt = 0;
            
            LambdaItem dict_item = {.item = (Item)dict};
            map_put(stream_map, stream_type, dict_key, dict_item, input->pool);
        }
    }
    
    // Store data length
    String* length_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 7, (void**)&length_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(length_key->chars, "length");
        length_key->len = 6;
        length_key->ref_cnt = 0;
        
        double* length_val;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&length_val);
        if (err == MEM_POOL_ERR_OK) {
            *length_val = (double)data_length;
            LambdaItem length_item = {.item = d2it(length_val)};
            map_put(stream_map, stream_type, length_key, length_item, input->pool);
        }
    }
    
    // Store stream data as a string (truncated for safety)
    String* data_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&data_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(data_key->chars, "data");
        data_key->len = 4;
        data_key->ref_cnt = 0;
        
        String* stream_data;
        err = pool_variable_alloc(input->pool, sizeof(String) + data_length + 1, (void**)&stream_data);
        if (err == MEM_POOL_ERR_OK) {
            memcpy(stream_data->chars, *pdf, data_length);
            stream_data->chars[data_length] = '\0';
            stream_data->len = data_length;
            stream_data->ref_cnt = 0;
            
            LambdaItem data_item = {.item = s2it(stream_data)};
            map_put(stream_map, stream_type, data_key, data_item, input->pool);
            
            // Add content analysis for potential content streams
            if (data_length > 10 && data_length < 2000) { // Only analyze reasonably sized streams
                Item content_analysis = analyze_pdf_content_stream(input, *pdf, data_length);
                if (content_analysis != ITEM_NULL) {
                    String* analysis_key;
                    err = pool_variable_alloc(input->pool, sizeof(String) + 9, (void**)&analysis_key);
                    if (err == MEM_POOL_ERR_OK) {
                        strcpy(analysis_key->chars, "analysis");
                        analysis_key->len = 8;
                        analysis_key->ref_cnt = 0;
                        LambdaItem analysis_item = {.item = content_analysis};
                        map_put(stream_map, stream_type, analysis_key, analysis_item, input->pool);
                    }
                }
            }
        }
    }
    
    arraylist_append(input->type_list, stream_type);
    stream_type->type_index = input->type_list->length - 1;
    
    *pdf = end_stream + 9; // skip endstream
    
    return (Item)stream_map;
}

static Item parse_pdf_xref_table(Input *input, const char **pdf) {
    // expects "xref" followed by cross-reference entries
    if (strncmp(*pdf, "xref", 4) != 0) return ITEM_ERROR;
    
    *pdf += 4; // skip "xref"
    skip_whitespace_and_comments(pdf);
    
    // Create a map to represent the xref table
    Map* xref_map = map_pooled(input->pool);
    if (!xref_map) return ITEM_ERROR;
    
    TypeMap* xref_type = map_init_cap(xref_map, input->pool);
    if (!xref_map->data) return (Item)xref_map;
    
    // Store type identifier
    String* type_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "xref_table");
            type_value->len = 10;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(xref_map, xref_type, type_key, type_item, input->pool);
        }
    }
    
    // Parse xref entries (limit for safety)
    Array* entries = array_pooled(input->pool);
    if (entries) {
        int entry_count = 0;
        int max_entries = 20; // Conservative limit
        
        while (*pdf && entry_count < max_entries) {
            skip_whitespace_and_comments(pdf);
            
            // Check if we've hit the trailer
            if (strncmp(*pdf, "trailer", 7) == 0) break;
            
            // Try to parse a subsection header (starting_obj_num count)
            if (isdigit(**pdf)) {
                int start_num = strtol(*pdf, (char**)pdf, 10);
                skip_whitespace_and_comments(pdf);
                
                if (isdigit(**pdf)) {
                    int count = strtol(*pdf, (char**)pdf, 10);
                    skip_whitespace_and_comments(pdf);
                    
                    // Limit the number of entries we process
                    if (count > 10) count = 10;
                    
                    // Parse individual entries
                    for (int i = 0; i < count && entry_count < max_entries; i++) {
                        skip_whitespace_and_comments(pdf);
                        
                        // Parse entry: offset generation flag
                        if (isdigit(**pdf)) {
                            int offset = strtol(*pdf, (char**)pdf, 10);
                            skip_whitespace_and_comments(pdf);
                            
                            if (isdigit(**pdf)) {
                                int generation = strtol(*pdf, (char**)pdf, 10);
                                skip_whitespace_and_comments(pdf);
                                
                                // Parse flag (n or f)
                                char flag = **pdf;
                                if (flag == 'n' || flag == 'f') {
                                    (*pdf)++;
                                    
                                    // Create entry map
                                    Map* entry_map = map_pooled(input->pool);
                                    if (entry_map) {
                                        TypeMap* entry_type = map_init_cap(entry_map, input->pool);
                                        if (entry_map->data) {
                                            // Store object number
                                            String* obj_key;
                                            err = pool_variable_alloc(input->pool, sizeof(String) + 7, (void**)&obj_key);
                                            if (err == MEM_POOL_ERR_OK) {
                                                strcpy(obj_key->chars, "object");
                                                obj_key->len = 6;
                                                obj_key->ref_cnt = 0;
                                                
                                                double* obj_val;
                                                err = pool_variable_alloc(input->pool, sizeof(double), (void**)&obj_val);
                                                if (err == MEM_POOL_ERR_OK) {
                                                    *obj_val = (double)(start_num + i);
                                                    LambdaItem obj_item = {.item = d2it(obj_val)};
                                                    map_put(entry_map, entry_type, obj_key, obj_item, input->pool);
                                                }
                                            }
                                            
                                            // Store offset
                                            String* offset_key;
                                            err = pool_variable_alloc(input->pool, sizeof(String) + 7, (void**)&offset_key);
                                            if (err == MEM_POOL_ERR_OK) {
                                                strcpy(offset_key->chars, "offset");
                                                offset_key->len = 6;
                                                offset_key->ref_cnt = 0;
                                                
                                                double* offset_val;
                                                err = pool_variable_alloc(input->pool, sizeof(double), (void**)&offset_val);
                                                if (err == MEM_POOL_ERR_OK) {
                                                    *offset_val = (double)offset;
                                                    LambdaItem offset_item = {.item = d2it(offset_val)};
                                                    map_put(entry_map, entry_type, offset_key, offset_item, input->pool);
                                                }
                                            }
                                            
                                            // Store flag
                                            String* flag_key;
                                            err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&flag_key);
                                            if (err == MEM_POOL_ERR_OK) {
                                                strcpy(flag_key->chars, "flag");
                                                flag_key->len = 4;
                                                flag_key->ref_cnt = 0;
                                                
                                                String* flag_val;
                                                err = pool_variable_alloc(input->pool, sizeof(String) + 2, (void**)&flag_val);
                                                if (err == MEM_POOL_ERR_OK) {
                                                    flag_val->chars[0] = flag;
                                                    flag_val->chars[1] = '\0';
                                                    flag_val->len = 1;
                                                    flag_val->ref_cnt = 0;
                                                    LambdaItem flag_item = {.item = s2it(flag_val)};
                                                    map_put(entry_map, entry_type, flag_key, flag_item, input->pool);
                                                }
                                            }
                                            
                                            arraylist_append(input->type_list, entry_type);
                                            entry_type->type_index = input->type_list->length - 1;
                                            
                                            LambdaItem entry_item = {.item = (Item)entry_map};
                                            array_append(entries, entry_item, input->pool);
                                            entry_count++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    break; // malformed subsection header
                }
            } else {
                break; // not a valid entry
            }
        }
        
        // Store entries in xref map
        String* entries_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&entries_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(entries_key->chars, "entries");
            entries_key->len = 7;
            entries_key->ref_cnt = 0;
            LambdaItem entries_item = {.item = (Item)entries};
            map_put(xref_map, xref_type, entries_key, entries_item, input->pool);
        }
    }
    
    arraylist_append(input->type_list, xref_type);
    xref_type->type_index = input->type_list->length - 1;
    
    return (Item)xref_map;
}

static Item parse_pdf_trailer(Input *input, const char **pdf) {
    // expects "trailer" followed by a dictionary
    if (strncmp(*pdf, "trailer", 7) != 0) return ITEM_ERROR;
    
    *pdf += 7; // skip "trailer"
    skip_whitespace_and_comments(pdf);
    
    // Parse the trailer dictionary
    Map* trailer_dict = parse_pdf_dictionary(input, pdf);
    if (!trailer_dict) return ITEM_ERROR;
    
    // Create a wrapper map to indicate this is a trailer
    Map* trailer_map = map_pooled(input->pool);
    if (!trailer_map) return (Item)trailer_dict;
    
    TypeMap* trailer_type = map_init_cap(trailer_map, input->pool);
    if (!trailer_map->data) return (Item)trailer_dict;
    
    // Store type identifier
    String* type_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "trailer");
            type_value->len = 7;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(trailer_map, trailer_type, type_key, type_item, input->pool);
        }
    }
    
    // Store the dictionary
    String* dict_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&dict_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(dict_key->chars, "dictionary");
        dict_key->len = 10;
        dict_key->ref_cnt = 0;
        LambdaItem dict_item = {.item = (Item)trailer_dict};
        map_put(trailer_map, trailer_type, dict_key, dict_item, input->pool);
    }
    
    arraylist_append(input->type_list, trailer_type);
    trailer_type->type_index = input->type_list->length - 1;
    
    return (Item)trailer_map;
}

// Analyze PDF content streams for basic information
static Item analyze_pdf_content_stream(Input *input, const char *stream_data, int length) {
    if (!stream_data || length <= 0) return ITEM_NULL;
    
    Map* analysis_map = map_pooled(input->pool);
    if (!analysis_map) return ITEM_NULL;
    
    TypeMap* analysis_type = map_init_cap(analysis_map, input->pool);
    if (!analysis_map->data) return ITEM_NULL;
    
    MemPoolError err;
    
    // Count text objects (BT...ET blocks)
    int text_objects = 0;
    const char* pos = stream_data;
    const char* end = stream_data + length;
    
    while (pos < end - 2) {
        if (pos[0] == 'B' && pos[1] == 'T' && (pos[2] == ' ' || pos[2] == '\n' || pos[2] == '\r')) {
            text_objects++;
            if (text_objects >= 20) break; // Safety limit
        }
        pos++;
    }
    
    // Count basic operators (simplified analysis)
    int drawing_ops = 0;
    pos = stream_data;
    while (pos < end - 1) {
        if ((pos[0] == ' ' || pos[0] == '\n') && pos < end - 2) {
            char op = pos[1];
            if (op == 'l' || op == 'm' || op == 'c' || op == 'h') { // line, move, curve, close path
                drawing_ops++;
                if (drawing_ops >= 50) break; // Safety limit
            }
        }
        pos++;
    }
    
    // Store analysis results
    String* type_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 17, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "content_analysis");
            type_value->len = 16;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(analysis_map, analysis_type, type_key, type_item, input->pool);
        }
    }
    
    // Text objects count
    String* text_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 13, (void**)&text_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(text_key->chars, "text_objects");
        text_key->len = 12;
        text_key->ref_cnt = 0;
        
        double* text_count;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&text_count);
        if (err == MEM_POOL_ERR_OK) {
            *text_count = (double)text_objects;
            LambdaItem text_item = {.item = d2it(text_count)};
            map_put(analysis_map, analysis_type, text_key, text_item, input->pool);
        }
    }
    
    // Drawing operations count
    String* draw_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 12, (void**)&draw_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(draw_key->chars, "drawing_ops");
        draw_key->len = 11;
        draw_key->ref_cnt = 0;
        
        double* draw_count;
        err = pool_variable_alloc(input->pool, sizeof(double), (void**)&draw_count);
        if (err == MEM_POOL_ERR_OK) {
            *draw_count = (double)drawing_ops;
            LambdaItem draw_item = {.item = d2it(draw_count)};
            map_put(analysis_map, analysis_type, draw_key, draw_item, input->pool);
        }
    }
    
    arraylist_append(input->type_list, analysis_type);
    analysis_type->type_index = input->type_list->length - 1;
    
    return (Item)analysis_map;
}

// Analyze font information from font dictionaries
static Item parse_pdf_font_descriptor(Input *input, Map* font_dict) {
    if (!font_dict) return ITEM_NULL;
    
    Map* font_analysis = map_pooled(input->pool);
    if (!font_analysis) return ITEM_NULL;
    
    TypeMap* font_type = map_init_cap(font_analysis, input->pool);
    if (!font_analysis->data) return ITEM_NULL;
    
    MemPoolError err;
    
    // Extract font information from the dictionary
    String* type_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 14, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "font_analysis");
            type_value->len = 13;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(font_analysis, font_type, type_key, type_item, input->pool);
        }
    }
    
    // Copy relevant font properties (Type, Subtype, BaseFont, etc.)
    String* original_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 9, (void**)&original_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(original_key->chars, "original");
        original_key->len = 8;
        original_key->ref_cnt = 0;
        LambdaItem original_item = {.item = (Item)font_dict};
        map_put(font_analysis, font_type, original_key, original_item, input->pool);
    }
    
    arraylist_append(input->type_list, font_type);
    font_type->type_index = input->type_list->length - 1;
    
    return (Item)font_analysis;
}

// Extract basic page information
static Item extract_pdf_page_info(Input *input, Map* page_dict) {
    if (!page_dict) return ITEM_NULL;
    
    Map* page_analysis = map_pooled(input->pool);
    if (!page_analysis) return ITEM_NULL;
    
    TypeMap* page_type = map_init_cap(page_analysis, input->pool);
    if (!page_analysis->data) return ITEM_NULL;
    
    MemPoolError err;
    
    // Store type
    String* type_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 5, (void**)&type_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(type_key->chars, "type");
        type_key->len = 4;
        type_key->ref_cnt = 0;
        
        String* type_value;
        err = pool_variable_alloc(input->pool, sizeof(String) + 13, (void**)&type_value);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(type_value->chars, "page_analysis");
            type_value->len = 12;
            type_value->ref_cnt = 0;
            
            LambdaItem type_item = {.item = s2it(type_value)};
            map_put(page_analysis, page_type, type_key, type_item, input->pool);
        }
    }
    
    // Copy the original page dictionary for reference
    String* original_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 9, (void**)&original_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(original_key->chars, "original");
        original_key->len = 8;
        original_key->ref_cnt = 0;
        LambdaItem original_item = {.item = (Item)page_dict};
        map_put(page_analysis, page_type, original_key, original_item, input->pool);
    }
    
    arraylist_append(input->type_list, page_type);
    page_type->type_index = input->type_list->length - 1;
    
    return (Item)page_analysis;
}

void parse_pdf(Input* input, const char* pdf_string) {
    printf("pdf_parse\n");
    input->sb = strbuf_new_pooled(input->pool);
    
    const char* pdf = pdf_string;
    
    // Validate input
    if (!pdf_string || !*pdf_string) {
        printf("Error: Empty PDF content\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    // Enhanced PDF header validation
    if (!is_valid_pdf_header(pdf)) {
        printf("Error: Invalid PDF format - must start with %%PDF-\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    // Create a simple map with basic PDF info
    Map* pdf_info = map_pooled(input->pool);
    if (!pdf_info) {
        printf("Error: Failed to allocate PDF info map\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    TypeMap* info_type = map_init_cap(pdf_info, input->pool);
    if (!pdf_info->data) {
        printf("Error: Failed to initialize PDF info map\n");
        input->root = (Item)pdf_info;
        return;
    }
    
    // Parse and store version with enhanced validation
    pdf += 5; // skip "%PDF-"
    StrBuf* version_sb = strbuf_new_pooled(input->pool);
    if (!version_sb) {
        printf("Error: Failed to allocate version buffer\n");
        input->root = (Item)pdf_info;
        return;
    }
    
    int counter = 0;
    while (*pdf && *pdf != '\n' && *pdf != '\r' && counter < 10) {
        // Validate version format (should be digits and dots)
        if ((*pdf >= '0' && *pdf <= '9') || *pdf == '.') {
            strbuf_append_char(version_sb, *pdf);
        } else {
            printf("Warning: Non-standard character in PDF version: %c\n", *pdf);
        }
        pdf++;
        counter++;
    }
    String* version = strbuf_to_string(version_sb);
    
    // Store version in map
    String* version_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&version_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(version_key->chars, "version");
        version_key->len = 7;
        version_key->ref_cnt = 0;
        LambdaItem version_item = {.item = s2it(version)};
        map_put(pdf_info, info_type, version_key, version_item, input->pool);
    }
    
    skip_whitespace_and_comments(&pdf);
    
    // Parse a few simple objects safely, and look for xref/trailer
    Array* objects = array_pooled(input->pool);
    Item xref_table = ITEM_NULL;
    Item trailer = ITEM_NULL;
    
    if (objects) {
        int obj_count = 0;
        int max_objects = 5; // Very conservative limit
        int consecutive_errors = 0;
        const int max_consecutive_errors = 3;
        
        while (*pdf && obj_count < max_objects && consecutive_errors < max_consecutive_errors) {
            skip_whitespace_and_comments(&pdf);
            if (!*pdf) break;
            
            Item obj = ITEM_NULL;
            const char* position_before_parse = pdf;
            
            // Check for xref table
            if (strncmp(pdf, "xref", 4) == 0) {
                xref_table = parse_pdf_xref_table(input, &pdf);
                if (xref_table != ITEM_ERROR) {
                    consecutive_errors = 0; // Reset error counter on success
                }
                continue; // Continue parsing to look for trailer
            }
            
            // Check for trailer
            if (strncmp(pdf, "trailer", 7) == 0) {
                trailer = parse_pdf_trailer(input, &pdf);
                break; // trailer usually means we're at the end
            }
            
            // Try to parse indirect object first (e.g., "1 0 obj")
            if (isdigit(*pdf)) {
                const char* saved_pos = pdf;
                obj = parse_pdf_indirect_object(input, &pdf);
                if (obj == ITEM_ERROR) {
                    // if not an indirect object, try regular object parsing
                    pdf = saved_pos;
                    obj = parse_pdf_object(input, &pdf);
                }
            } else {
                // Try to parse a simple object
                obj = parse_pdf_object(input, &pdf);
            }
            
            if (obj != ITEM_ERROR && obj != ITEM_NULL) {
                LambdaItem obj_item = {.item = obj};
                array_append(objects, obj_item, input->pool);
                obj_count++;
                consecutive_errors = 0; // Reset error counter on success
            } else {
                consecutive_errors++;
                // If we haven't advanced, force advance to prevent infinite loop
                if (pdf == position_before_parse) {
                    advance_safely(&pdf, 1);
                }
            }
        }
        
        // Always scan for xref and trailer sections, even if we've hit the object limit
        if (xref_table == ITEM_NULL || trailer == ITEM_NULL) {
            // Scan ahead for xref/trailer sections - start from current position if we haven't exceeded object limit,
            // or from the beginning if we need to do a more thorough search
            const char* scan_pdf = (obj_count >= max_objects) ? pdf_string : pdf;
            int scan_limit = 10000; // Increased limit to handle larger PDFs
            int scanned = 0;
            
            while (*scan_pdf && scanned < scan_limit) {
                // Skip whitespace but be more careful about advancement
                if (isspace(*scan_pdf) || *scan_pdf == '%') {
                    skip_whitespace_and_comments(&scan_pdf);
                    if (!*scan_pdf) break;
                    scanned += 10; // Count whitespace skipping as progress
                    continue;
                }
                
                if (xref_table == ITEM_NULL && strncmp(scan_pdf, "xref", 4) == 0) {
                    // Check that it's a standalone keyword (not part of another word)
                    if (scan_pdf == pdf_string || isspace(*(scan_pdf - 1)) || *(scan_pdf - 1) == '\n' || *(scan_pdf - 1) == '\r') {
                        const char* xref_start = scan_pdf;
                        xref_table = parse_pdf_xref_table(input, &scan_pdf);
                        if (xref_table != ITEM_ERROR) {
                            printf("Found and parsed xref table at offset %lld\n", (long long)(xref_start - pdf_string));
                        }
                        continue;
                    }
                }
                
                if (trailer == ITEM_NULL && strncmp(scan_pdf, "trailer", 7) == 0) {
                    // Check that it's a standalone keyword
                    if (scan_pdf == pdf_string || isspace(*(scan_pdf - 1)) || *(scan_pdf - 1) == '\n' || *(scan_pdf - 1) == '\r') {
                        const char* trailer_start = scan_pdf;
                        trailer = parse_pdf_trailer(input, &scan_pdf);
                        if (trailer != ITEM_ERROR) {
                            printf("Found and parsed trailer at offset %lld\n", (long long)(trailer_start - pdf_string));
                        }
                        if (trailer != ITEM_ERROR) break; // Done after finding trailer
                    }
                }
                
                // Check for startxref (PDF pointer to xref table)
                if (strncmp(scan_pdf, "startxref", 9) == 0) {
                    scan_pdf += 9;
                    skip_whitespace_and_comments(&scan_pdf);
                    // Try to parse the xref offset number
                    if (isdigit(*scan_pdf)) {
                        long xref_offset = strtol(scan_pdf, (char**)&scan_pdf, 10);
                        printf("Found startxref pointing to offset %ld\n", xref_offset);
                        // If we haven't found xref yet, try looking at that specific offset
                        if (xref_table == ITEM_NULL && xref_offset > 0 && xref_offset < scan_limit) {
                            const char* xref_pos = pdf_string + xref_offset;
                            if (strncmp(xref_pos, "xref", 4) == 0) {
                                const char* xref_parse_pos = xref_pos;
                                xref_table = parse_pdf_xref_table(input, &xref_parse_pos);
                                if (xref_table != ITEM_ERROR) {
                                    printf("Successfully parsed xref table from startxref offset\n");
                                }
                            }
                        }
                    }
                    continue;
                }
                
                // Check for linearization hint (for information only)
                if (strncmp(scan_pdf, "%%EOF", 5) == 0) {
                    // Found end of file marker - continue scanning a bit more in case there's more after
                    scan_pdf += 5;
                    scanned += 5;
                    continue;
                }
                
                scan_pdf++;
                scanned++;
            }
        }
        
        // Store objects in map
        String* objects_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&objects_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(objects_key->chars, "objects");
            objects_key->len = 7;
            objects_key->ref_cnt = 0;
            LambdaItem objects_item = {.item = (Item)objects};
            map_put(pdf_info, info_type, objects_key, objects_item, input->pool);
        }
    }
    
    // Store xref table if found
    if (xref_table != ITEM_NULL) {
        String* xref_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&xref_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(xref_key->chars, "xref_table");
            xref_key->len = 10;
            xref_key->ref_cnt = 0;
            LambdaItem xref_item = {.item = xref_table};
            map_put(pdf_info, info_type, xref_key, xref_item, input->pool);
        }
    }
    
    // Store trailer if found
    if (trailer != ITEM_NULL) {
        String* trailer_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&trailer_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(trailer_key->chars, "trailer");
            trailer_key->len = 7;
            trailer_key->ref_cnt = 0;
            LambdaItem trailer_item = {.item = trailer};
            map_put(pdf_info, info_type, trailer_key, trailer_item, input->pool);
        }
    }

    // Add basic PDF statistics
    String* stats_key;
    err = pool_variable_alloc(input->pool, sizeof(String) + 11, (void**)&stats_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(stats_key->chars, "statistics");
        stats_key->len = 10;
        stats_key->ref_cnt = 0;
        
        // Create statistics map
        Map* stats_map = map_pooled(input->pool);
        if (stats_map) {
            TypeMap* stats_type = map_init_cap(stats_map, input->pool);
            if (stats_map->data) {
                // Object count (use objects array length if available)
                String* obj_count_key;
                err = pool_variable_alloc(input->pool, sizeof(String) + 13, (void**)&obj_count_key);
                if (err == MEM_POOL_ERR_OK) {
                    strcpy(obj_count_key->chars, "object_count");
                    obj_count_key->len = 12;
                    obj_count_key->ref_cnt = 0;
                    double* obj_count_val;
                    err = pool_variable_alloc(input->pool, sizeof(double), (void**)&obj_count_val);
                    if (err == MEM_POOL_ERR_OK) {
                        *obj_count_val = objects ? (double)objects->length : 0.0;
                        LambdaItem obj_count_item = {.item = d2it(obj_count_val)};
                        map_put(stats_map, stats_type, obj_count_key, obj_count_item, input->pool);
                    }
                }
                
                // Has xref table
                String* has_xref_key;
                err = pool_variable_alloc(input->pool, sizeof(String) + 10, (void**)&has_xref_key);
                if (err == MEM_POOL_ERR_OK) {
                    strcpy(has_xref_key->chars, "has_xref");
                    has_xref_key->len = 8;
                    has_xref_key->ref_cnt = 0;
                    LambdaItem has_xref_item = {.item = b2it(xref_table != ITEM_NULL)};
                    map_put(stats_map, stats_type, has_xref_key, has_xref_item, input->pool);
                }
                
                // Has trailer
                String* has_trailer_key;
                err = pool_variable_alloc(input->pool, sizeof(String) + 12, (void**)&has_trailer_key);
                if (err == MEM_POOL_ERR_OK) {
                    strcpy(has_trailer_key->chars, "has_trailer");
                    has_trailer_key->len = 11;
                    has_trailer_key->ref_cnt = 0;
                    LambdaItem has_trailer_item = {.item = b2it(trailer != ITEM_NULL)};
                    map_put(stats_map, stats_type, has_trailer_key, has_trailer_item, input->pool);
                }
                
                // Count stream objects
                int stream_count = 0;
                int font_count = 0;
                int page_count = 0;
                
                if (objects && objects->length > 0) {
                    for (int i = 0; i < objects->length && i < 20; i++) { // Safety limit
                        // Basic analysis of object types based on parsing results
                        // This is a simplified analysis since we can't easily inspect the object structure
                        stream_count++; // For now, just count all objects as potential streams
                    }
                    // Reset counters for more accurate counting based on what we know
                    stream_count = 0; // Will be updated if we find actual streams
                }
                
                // Stream count
                String* stream_count_key;
                err = pool_variable_alloc(input->pool, sizeof(String) + 13, (void**)&stream_count_key);
                if (err == MEM_POOL_ERR_OK) {
                    strcpy(stream_count_key->chars, "stream_count");
                    stream_count_key->len = 12;
                    stream_count_key->ref_cnt = 0;
                    double* stream_count_val;
                    err = pool_variable_alloc(input->pool, sizeof(double), (void**)&stream_count_val);
                    if (err == MEM_POOL_ERR_OK) {
                        *stream_count_val = (double)stream_count;
                        LambdaItem stream_count_item = {.item = d2it(stream_count_val)};
                        map_put(stats_map, stats_type, stream_count_key, stream_count_item, input->pool);
                    }
                }
                
                // PDF features detected
                String* features_key;
                err = pool_variable_alloc(input->pool, sizeof(String) + 9, (void**)&features_key);
                if (err == MEM_POOL_ERR_OK) {
                    strcpy(features_key->chars, "features");
                    features_key->len = 8;
                    features_key->ref_cnt = 0;
                    
                    // Create a features array
                    Array* features_array = array_pooled(input->pool);
                    if (features_array) {
                        // Add features based on what we've found
                        if (xref_table != ITEM_NULL) {
                            String* xref_feature;
                            err = pool_variable_alloc(input->pool, sizeof(String) + 20, (void**)&xref_feature);
                            if (err == MEM_POOL_ERR_OK) {
                                strcpy(xref_feature->chars, "cross_reference_table");
                                xref_feature->len = 19;
                                xref_feature->ref_cnt = 0;
                                LambdaItem xref_feature_item = {.item = s2it(xref_feature)};
                                array_append(features_array, xref_feature_item, input->pool);
                            }
                        }
                        
                        if (trailer != ITEM_NULL) {
                            String* trailer_feature;
                            err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&trailer_feature);
                            if (err == MEM_POOL_ERR_OK) {
                                strcpy(trailer_feature->chars, "trailer");
                                trailer_feature->len = 7;
                                trailer_feature->ref_cnt = 0;
                                LambdaItem trailer_feature_item = {.item = s2it(trailer_feature)};
                                array_append(features_array, trailer_feature_item, input->pool);
                            }
                        }
                        
                        String* objects_feature;
                        err = pool_variable_alloc(input->pool, sizeof(String) + 16, (void**)&objects_feature);
                        if (err == MEM_POOL_ERR_OK) {
                            strcpy(objects_feature->chars, "indirect_objects");
                            objects_feature->len = 15;
                            objects_feature->ref_cnt = 0;
                            LambdaItem objects_feature_item = {.item = s2it(objects_feature)};
                            array_append(features_array, objects_feature_item, input->pool);
                        }
                        
                        LambdaItem features_item = {.item = (Item)features_array};
                        map_put(stats_map, stats_type, features_key, features_item, input->pool);
                    }
                }
                
                arraylist_append(input->type_list, stats_type);
                stats_type->type_index = input->type_list->length - 1;
            }
            
            LambdaItem stats_item = {.item = (Item)stats_map};
            map_put(pdf_info, info_type, stats_key, stats_item, input->pool);
        }
    }

    arraylist_append(input->type_list, info_type);
    info_type->type_index = input->type_list->length - 1;
    
    input->root = (Item)pdf_info;
}
