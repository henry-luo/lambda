// pdf parser implementation
#include "../transpiler.h"

static Item parse_pdf_object(Input *input, const char **pdf);
static String* parse_pdf_name(Input *input, const char **pdf);
static String* parse_pdf_string(Input *input, const char **pdf);
static Item parse_pdf_indirect_ref(Input *input, const char **pdf);
static Item parse_pdf_indirect_object(Input *input, const char **pdf);
static Item parse_pdf_stream(Input *input, const char **pdf, Map* dict);

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
    
    while (**pdf && !isspace(**pdf) && **pdf != '/' && **pdf != '(' && **pdf != ')' && 
           **pdf != '<' && **pdf != '>' && **pdf != '[' && **pdf != ']' && 
           **pdf != '{' && **pdf != '}' && **pdf != '%') {
        if (**pdf == '#') {
            // hex escape in name
            (*pdf)++; // skip #
            if (isxdigit(**pdf) && isxdigit(*(*pdf + 1))) {
                char hex_string[3] = {**pdf, *(*pdf + 1), '\0'};
                int char_value = (int)strtol(hex_string, NULL, 16);
                strbuf_append_char(sb, (char)char_value);
                (*pdf) += 2;
            }
        } else {
            strbuf_append_char(sb, **pdf);
            (*pdf)++;
        }
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
        
        while (**pdf && paren_count > 0) {
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
                        default: strbuf_append_char(sb, **pdf); break;
                    }
                    (*pdf)++;
                }
            } else if (**pdf == '(') {
                paren_count++;
                strbuf_append_char(sb, **pdf);
                (*pdf)++;
            } else if (**pdf == ')') {
                paren_count--;
                if (paren_count > 0) {
                    strbuf_append_char(sb, **pdf);
                }
                (*pdf)++;
            } else {
                strbuf_append_char(sb, **pdf);
                (*pdf)++;
            }
        }
    } else if (**pdf == '<') {
        // hexadecimal string
        (*pdf)++; // skip <
        while (**pdf && **pdf != '>') {
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
    
    // Only handle simple cases to avoid recursion issues
    // check for null
    if (strncmp(*pdf, "null", 4) == 0 && (!isalnum(*(*pdf + 4)))) {
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
        result = dict ? (Item)dict : ITEM_ERROR;
    }
    // skip complex structures (streams and other complex cases)
    else {
        (*pdf)++;
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
        }
    }
    
    arraylist_append(input->type_list, stream_type);
    stream_type->type_index = input->type_list->length - 1;
    
    *pdf = end_stream + 9; // skip endstream
    
    return (Item)stream_map;
}

void parse_pdf(Input* input, const char* pdf_string) {
    printf("pdf_parse\n");
    input->sb = strbuf_new_pooled(input->pool);
    
    const char* pdf = pdf_string;
    
    // check PDF header
    if (strncmp(pdf, "%PDF-", 5) != 0) {
        printf("Error: Invalid PDF format - must start with %%PDF-\n");
        input->root = ITEM_ERROR;
        return;
    }
    
    // Create a simple map with basic PDF info
    Map* pdf_info = map_pooled(input->pool);
    if (!pdf_info) {
        input->root = ITEM_ERROR;
        return;
    }
    
    TypeMap* info_type = map_init_cap(pdf_info, input->pool);
    if (!pdf_info->data) {
        input->root = (Item)pdf_info;
        return;
    }
    
    // Parse and store version
    pdf += 5; // skip "%PDF-"
    StrBuf* version_sb = strbuf_new_pooled(input->pool);
    int counter = 0;
    while (*pdf && *pdf != '\n' && *pdf != '\r' && counter < 10) {
        strbuf_append_char(version_sb, *pdf);
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
    
    // Parse a few simple objects safely
    Array* objects = array_pooled(input->pool);
    if (objects) {
        int obj_count = 0;
        int max_objects = 5; // Very conservative limit
        
        while (*pdf && obj_count < max_objects) {
            skip_whitespace_and_comments(&pdf);
            if (!*pdf) break;
            
            Item obj = ITEM_NULL;
            
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
            }
            
            // Safety check - stop if we hit known PDF structure markers
            if (strncmp(pdf, "xref", 4) == 0 || strncmp(pdf, "trailer", 7) == 0) {
                break;
            }
            
            // Advance if we're not making progress
            if (obj == ITEM_NULL || obj == ITEM_ERROR) {
                pdf++;
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
    
    arraylist_append(input->type_list, info_type);
    info_type->type_index = input->type_list->length - 1;
    
    input->root = (Item)pdf_info;
}
