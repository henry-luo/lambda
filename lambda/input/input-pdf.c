// pdf parser implementation
#include "../transpiler.h"

static Item parse_pdf_object(Input *input, const char **pdf);
static String* parse_pdf_name(Input *input, const char **pdf);

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
    while (**pdf && **pdf != ']' && item_count < 50) { // limit array size for safety
        Item obj = parse_pdf_object(input, pdf);
        if (obj != ITEM_ERROR) {
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
    while (**pdf && !(**pdf == '>' && *(*pdf + 1) == '>') && pair_count < 20) { // limit dict size for safety
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
        if (value != ITEM_ERROR) {
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

static Item parse_pdf_object(Input *input, const char **pdf) {
    static int call_count = 0;
    call_count++;
    
    // prevent runaway recursion - reduced limit for more safety
    if (call_count > 100) {
        printf("Warning: too many parse calls, stopping\n");
        call_count = 0;
        return ITEM_ERROR;
    }
    
    skip_whitespace_and_comments(pdf);
    
    if (!**pdf) {
        call_count--;
        return ITEM_NULL;
    }
    
    Item result = ITEM_ERROR;
    
    // check for arrays
    if (**pdf == '[') {
        Array* arr = parse_pdf_array(input, pdf);
        result = arr ? (Item)arr : ITEM_ERROR;
    }
    // check for dictionaries
    else if (**pdf == '<' && *(*pdf + 1) == '<') {
        Map* dict = parse_pdf_dictionary(input, pdf);
        result = dict ? (Item)dict : ITEM_ERROR;
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
    // check for strings
    else if (**pdf == '(' || (**pdf == '<' && *(*pdf + 1) != '<')) {
        String* str = parse_pdf_string(input, pdf);
        result = str ? s2it(str) : ITEM_ERROR;
    }
    // check for numbers (also handles indirect references like "1 0 R")
    else if ((**pdf >= '0' && **pdf <= '9') || **pdf == '-' || **pdf == '+' || **pdf == '.') {
        result = parse_pdf_number(input, pdf);
    }
    // skip unknown content
    else {
        (*pdf)++;
        result = ITEM_ERROR;
    }
    
    call_count--;
    return result;
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
    
    // create root PDF document structure
    Map* pdf_doc = map_pooled(input->pool);
    if (!pdf_doc) {
        input->root = ITEM_ERROR;
        return;
    }
    
    TypeMap* doc_type = map_init_cap(pdf_doc, input->pool);
    if (!pdf_doc->data) {
        input->root = (Item)pdf_doc;
        return;
    }
    
    // parse version from header
    pdf += 5; // skip "%PDF-"
    StrBuf* version_sb = strbuf_new_pooled(input->pool);
    while (*pdf && *pdf != '\n' && *pdf != '\r') {
        strbuf_append_char(version_sb, *pdf);
        pdf++;
    }
    String* version = strbuf_to_string(version_sb);
    
    // store version
    LambdaItem version_item = {.item = s2it(version)};
    String* version_key;
    MemPoolError err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&version_key);
    if (err == MEM_POOL_ERR_OK) {
        strcpy(version_key->chars, "version");
        version_key->len = 7;
        version_key->ref_cnt = 0;
        map_put(pdf_doc, doc_type, version_key, version_item, input->pool);
    }
    
    skip_whitespace_and_comments(&pdf);
    
    // parse a few objects
    Array* objects = array_pooled(input->pool);
    if (objects) {
        int obj_count = 0;
        while (*pdf && obj_count < 10) { // limit parsing for safety
            Item obj = parse_pdf_object(input, &pdf);
            if (obj != ITEM_ERROR && obj != ITEM_NULL) {
                LambdaItem obj_item = {.item = obj};
                array_append(objects, obj_item, input->pool);
                obj_count++;
            }
            skip_whitespace_and_comments(&pdf);
            
            // stop if we hit xref or EOF
            if (strncmp(pdf, "xref", 4) == 0 || !*pdf) break;
        }
        
        // store objects
        LambdaItem objects_item = {.item = (Item)objects};
        String* objects_key;
        err = pool_variable_alloc(input->pool, sizeof(String) + 8, (void**)&objects_key);
        if (err == MEM_POOL_ERR_OK) {
            strcpy(objects_key->chars, "objects");
            objects_key->len = 7;
            objects_key->ref_cnt = 0;
            map_put(pdf_doc, doc_type, objects_key, objects_item, input->pool);
        }
    }
    
    arraylist_append(input->type_list, doc_type);
    doc_type->type_index = input->type_list->length - 1;
    
    input->root = (Item)pdf_doc;
}
