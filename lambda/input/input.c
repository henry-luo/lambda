#include "../transpiler.h"
#include <lexbor/url/url.h>

lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* read_text_doc(lxb_url_t *url);
void parse_json(Input* input, const char* json_string);
void parse_csv(Input* input, const char* csv_string);
void parse_ini(Input* input, const char* ini_string);
void parse_toml(Input* input, const char* toml_string);
void parse_yaml(Input *input, const char* yaml_str);
void parse_xml(Input* input, const char* xml_string);
void parse_markdown(Input* input, const char* markdown_string);
void parse_html(Input* input, const char* html_string);
void parse_latex(Input* input, const char* latex_string);
void parse_rtf(Input* input, const char* rtf_string);
void parse_pdf(Input* input, const char* pdf_string);

String* strbuf_to_string(StrBuf *sb) {
    String *string = (String*)sb->str;
    if (string) {
        string->len = sb->length - sizeof(uint32_t);  string->ref_cnt = 0;
        strbuf_full_reset(sb);
        return string;        
    }
    return &EMPTY_STRING;
}

ShapeEntry* alloc_shape_entry(VariableMemPool* pool, String* key, TypeId type_id, ShapeEntry* prev_entry) {
    ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry) + sizeof(StrView));
    StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
    nv->str = key->chars;  nv->length = key->len;
    shape_entry->name = nv;
    shape_entry->type = type_info[type_id].type;
    if (prev_entry) {
        prev_entry->next = shape_entry;
        shape_entry->byte_offset = prev_entry->byte_offset + type_info[prev_entry->type->type_id].byte_size;
    }
    else { shape_entry->byte_offset = 0; }
    return shape_entry;
}

TypeMap* map_init_cap(Map* mp, VariableMemPool* pool) {
    // alloc map type and data chunk
    TypeMap *map_type = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!map_type) { return NULL; }
    mp->type = map_type;
    int byte_cap = 64;
    mp->data = pool_calloc(pool, byte_cap);  mp->data_cap = byte_cap;
    if (!mp->data) return NULL;
    return map_type;
}

void map_put(Map* mp, TypeMap *map_type, String* key, LambdaItem value, VariableMemPool* pool) {
    TypeId type_id = value.type_id ? value.type_id : *((TypeId*)value.raw_pointer);
    ShapeEntry* shape_entry = alloc_shape_entry(pool, key, type_id, map_type->last);
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (byte_offset > mp->data_cap) { // resize map data
        int byte_cap = mp->data_cap * 2;
        void* new_data = pool_calloc(pool, byte_cap);
        if (!new_data) return;
        memcpy(new_data, mp->data, byte_offset - bsize);
        pool_variable_free(pool, mp->data);
        mp->data = new_data;  mp->data_cap = byte_cap;
    }
    map_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)mp->data + byte_offset - bsize;
    switch (type_id) {
    case LMD_TYPE_NULL:
        *(void**)field_ptr = NULL;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;             
        break;
    case LMD_TYPE_INT:
        *(long*)field_ptr = value.long_val;
        break;
    case LMD_TYPE_INT64:
        *(long*)field_ptr = *(long*)value.pointer;
        break;        
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = *(double*)value.pointer;
        break;
    case LMD_TYPE_STRING:
        *(String**)field_ptr = (String*)value.pointer;
        ((String*)value.pointer)->ref_cnt++;
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_MAP:
        *(Map**)field_ptr = (Map*)value.raw_pointer;
        break;
    default:
        printf("unknown type %d\n", value.type_id);
    }
}

Input* input_new(lxb_url_t* abs_url) {
    Input* input = malloc(sizeof(Input));
    input->url = abs_url;
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { free(input);  return NULL; }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    return input;
}

Input* input_data(Context* ctx, String* url, String* type) {
    printf("input_data at: %s, type: %s\n", url->chars, type ? type->chars : "null");
    lxb_url_t* abs_url = parse_url((lxb_url_t*)ctx->cwd, url->chars);
    if (!abs_url) { printf("Failed to parse URL\n");  return NULL; }
    char* source = read_text_doc(abs_url);
    if (!source) {
        printf("Failed to read document at URL: %s\n", url->chars);
        lxb_url_destroy(abs_url);
        return NULL;
    }
    Input* input = NULL;
    if (!type || strcmp(type->chars, "text") == 0) { // treat as plain text
        input = (Input*)calloc(1, sizeof(Input));
        input->url = abs_url;
        String *str = (String*)malloc(sizeof(String) + strlen(source) + 1);
        str->len = strlen(source);  str->ref_cnt = 0;
        strcpy(str->chars, source);
        input->root = s2it(str);
        printf("input text: %s\n", str->chars);
    }
    else {
        input = input_new(abs_url);
        if (strcmp(type->chars, "json") == 0) {
            parse_json(input, source);
        }
        else if (strcmp(type->chars, "csv") == 0) {
            parse_csv(input, source);
        }
        else if (strcmp(type->chars, "ini") == 0) {
            parse_ini(input, source);
        }
        else if (strcmp(type->chars, "toml") == 0) {
            parse_toml(input, source);
        }
        else if (strcmp(type->chars, "yaml") == 0) {
            parse_yaml(input, source);
        }
        else if (strcmp(type->chars, "xml") == 0) {
            parse_xml(input, source);
        }
        else if (strcmp(type->chars, "markdown") == 0) {
            parse_markdown(input, source);
        }
        else if (strcmp(type->chars, "html") == 0) {
            parse_html(input, source);
        }
        else if (strcmp(type->chars, "latex") == 0) {
            parse_latex(input, source);
        }
        else if (strcmp(type->chars, "rtf") == 0) {
            parse_rtf(input, source);
        }
        else if (strcmp(type->chars, "pdf") == 0) {
            parse_pdf(input, source);
        }
        else {
            printf("Unknown input type: %s\n", type->chars);
        }
    }
    free(source);
    return input;
}