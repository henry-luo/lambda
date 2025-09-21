#include "input.h"
#include "../../lib/url.h"
#include "../../lib/stringbuf.h"
#include "mime-detect.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <algorithm>  // for std::max

void parse_json(Input* input, const char* json_string);
void parse_csv(Input* input, const char* csv_string);
void parse_ini(Input* input, const char* ini_string);
void parse_properties(Input* input, const char* prop_string);
void parse_toml(Input* input, const char* toml_string);
void parse_yaml(Input *input, const char* yaml_str);
void parse_xml(Input* input, const char* xml_string);
void parse_html(Input* input, const char* html_string);
void parse_latex(Input* input, const char* latex_string);
void parse_rtf(Input* input, const char* rtf_string);
void parse_pdf(Input* input, const char* pdf_string);
void parse_asciidoc(Input* input, const char* asciidoc_string);
void parse_man(Input* input, const char* man_string);
void parse_eml(Input* input, const char* eml_string);
void parse_vcf(Input* input, const char* vcf_string);
void parse_ics(Input* input, const char* ics_string);
void parse_mark(Input* input, const char* mark_string);
void parse_org(Input* input, const char* org_string);
void parse_css(Input* input, const char* css_string);
void parse_jsx(Input* input, const char* jsx_string);
void parse_math(Input* input, const char* math_string, const char* flavor);
Item input_markup(Input *input, const char* content);

// Import MarkupFormat enum from markup-parser.h
#include "markup-parser.h"
Item input_markup_with_format(Input *input, const char* content, MarkupFormat format);

// Helper function to create string from char content
String* create_input_string(Input* input, const char* text, int start, int len) {
    // Allocate string from pool instead of using shared StrBuf
    String* str = (String*)pool_calloc(input->pool, sizeof(String) + len + 1);
    if (!str) return &EMPTY_STRING;
    str->len = len;
    str->ref_cnt = 0;
    memcpy(str->chars, text + start, len);
    str->chars[len] = '\0';  // Null-terminate the string
    return str;
}

String* input_create_string(Input *input, const char* text) {
    if (!text) return NULL;
    return create_input_string(input, text, 0, strlen(text));
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

extern TypeMap EmptyMap;
void map_put(Map* mp, String* key, Item value, Input *input) {
    TypeMap *map_type = (TypeMap*)mp->type;
    if (map_type == &EmptyMap) {
        // alloc map type and data chunk
        map_type = (TypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!map_type) { return; }
        mp->type = map_type;
        arraylist_append(input->type_list, map_type);
        map_type->type_index = input->type_list->length - 1;        
        int byte_cap = 64;
        mp->data = pool_calloc(input->pool, byte_cap);  mp->data_cap = byte_cap;
        if (!mp->data) return;
    }
    
    TypeId type_id = get_type_id(value);
    ShapeEntry* shape_entry = alloc_shape_entry(input->pool, key, type_id, map_type->last);
    if (!map_type->shape) { map_type->shape = shape_entry; }
    map_type->last = shape_entry;
    map_type->length++;

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (byte_offset > mp->data_cap) { // resize map data
        assert(mp->data_cap > 0);
        int byte_cap = mp->data_cap * 2;
        void* new_data = pool_calloc(input->pool, byte_cap);
        if (!new_data) return;
        memcpy(new_data, mp->data, byte_offset - bsize);
        pool_variable_free(input->pool, mp->data);
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
        *(int64_t*)field_ptr = value.int_val;
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = *(int64_t*)value.pointer;
        break;        
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = *(double*)value.pointer;
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY:
        *(String**)field_ptr = (String*)value.pointer;
        ((String*)value.pointer)->ref_cnt++;
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_MAP:  case LMD_TYPE_LIST:
        *(Map**)field_ptr = (Map*)value.raw_pointer;
        break;
    default:
        printf("unknown type %d\n", value.type_id);
    }
}

Element* input_create_element(Input *input, const char* tag_name) {
    Element* element = elmt_pooled(input->pool);
    if (!element) return NULL;
    
    TypeElmt *element_type = (TypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!element_type) return NULL;
    element->type = element_type;
    arraylist_append(input->type_list, element_type);
    element_type->type_index = input->type_list->length - 1;    
    // initialize with no attributes
    
    // Set element name
    String* name_str = input_create_string(input, tag_name);
    if (name_str) {
        element_type->name.str = name_str->chars;
        element_type->name.length = name_str->len;
    }
    return element;
}

extern TypeElmt EmptyElmt;
void elmt_put(Element* elmt, String* key, Item value, VariableMemPool* pool) {
    assert(elmt->type != &EmptyElmt);
    TypeId type_id = get_type_id(value);
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    ShapeEntry* shape_entry = alloc_shape_entry(pool, key, type_id, elmt_type->last);
    if (!elmt_type->shape) { elmt_type->shape = shape_entry; }
    elmt_type->last = shape_entry;
    elmt_type->length++;

    // ensure data capacity
    int bsize = type_info[type_id].byte_size;
    int byte_offset = shape_entry->byte_offset + bsize;
    if (byte_offset > elmt->data_cap) { // resize map data
        // elmt->data_cap could be 0
        int byte_cap = std::max(elmt->data_cap, byte_offset) * 2;
        void* new_data = pool_calloc(pool, byte_cap);
        if (!new_data) return;
        if (elmt->data) {
            memcpy(new_data, elmt->data, byte_offset - bsize);
            pool_variable_free(pool, elmt->data);
        }
        elmt->data = new_data;  elmt->data_cap = byte_cap;
    }
    elmt_type->byte_size = byte_offset;

    // store the value
    void* field_ptr = (char*)elmt->data + byte_offset - bsize;
    switch (type_id) {
    case LMD_TYPE_NULL:
        *(void**)field_ptr = NULL;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;             
        break;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.int_val;
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = *(int64_t*)value.pointer;
        break;        
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = *(double*)value.pointer;
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)field_ptr = *(DateTime*)value.pointer;
        break;
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY:
        *(String**)field_ptr = (String*)value.pointer;
        ((String*)value.pointer)->ref_cnt++;
        break;
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_INT64:  case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:  case LMD_TYPE_LIST:  case LMD_TYPE_MAP:  case LMD_TYPE_ELEMENT: {
        Container *container = value.container;
        *(void**)field_ptr = container;  container->ref_cnt;
        break;
    }
    default:
        printf("unknown type %d\n", value.type_id);
    }
}

Input* input_new(Url* abs_url) {
    Input* input = (Input*)malloc(sizeof(Input));
    input->url = abs_url;
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) { free(input);  return NULL; }
    input->type_list = arraylist_new(16);
    input->root = {.item = ITEM_NULL};
    input->sb = stringbuf_new(input->pool);  // Always allocate StringBuf
    return input;
}

// Helper function to map MIME types to parser types
static const char* mime_to_parser_type(const char* mime_type) {
    if (!mime_type) return "text";
    
    // Direct mappings
    if (strcmp(mime_type, "application/json") == 0) return "json";
    if (strcmp(mime_type, "text/csv") == 0) return "csv";
    if (strcmp(mime_type, "application/xml") == 0) return "xml";
    if (strcmp(mime_type, "text/html") == 0) return "html";
    if (strcmp(mime_type, "text/markdown") == 0) return "markdown";
    if (strcmp(mime_type, "text/mdx") == 0) return "mdx";
    if (strcmp(mime_type, "text/x-rst") == 0) return "rst";
    if (strcmp(mime_type, "application/rtf") == 0) return "rtf";
    if (strcmp(mime_type, "application/pdf") == 0) return "pdf";
    if (strcmp(mime_type, "application/x-tex") == 0) return "latex";
    if (strcmp(mime_type, "application/x-latex") == 0) return "latex";
    if (strcmp(mime_type, "application/toml") == 0) return "toml";
    if (strcmp(mime_type, "application/x-yaml") == 0) return "yaml";
    if (strcmp(mime_type, "text/x-java-properties") == 0) return "properties";
    if (strcmp(mime_type, "application/x-java-properties") == 0) return "properties";
    if (strcmp(mime_type, "message/rfc822") == 0) return "eml";
    if (strcmp(mime_type, "application/eml") == 0) return "eml";
    if (strcmp(mime_type, "message/eml") == 0) return "eml";
    if (strcmp(mime_type, "text/vcard") == 0) return "vcf";
    if (strcmp(mime_type, "text/calendar") == 0) return "ics";
    if (strcmp(mime_type, "application/ics") == 0) return "ics";
    if (strcmp(mime_type, "text/textile") == 0) return "textile";
    if (strcmp(mime_type, "application/textile") == 0) return "textile";
    if (strcmp(mime_type, "text/x-mark") == 0) return "mark";
    if (strcmp(mime_type, "application/x-mark") == 0) return "mark";
    if (strcmp(mime_type, "text/css") == 0) return "css";
    if (strcmp(mime_type, "application/css") == 0) return "css";
    
    // Check for XML-based formats
    if (strstr(mime_type, "+xml") || strstr(mime_type, "xml")) return "xml";
    
    // Check for text formats
    if (strstr(mime_type, "text/") == mime_type) {
        // Handle specific text subtypes
        if (strstr(mime_type, "x-c") || strstr(mime_type, "x-java") || 
            strstr(mime_type, "javascript") || strstr(mime_type, "x-python")) {
            return "text";
        }
        if (strstr(mime_type, "ini")) return "ini";
        return "text";
    }
    
    // For unsupported formats, default to text if it might be readable
    if (strstr(mime_type, "application/") == mime_type) {
        if (strstr(mime_type, "javascript") || strstr(mime_type, "typescript") ||
            strstr(mime_type, "x-sh") || strstr(mime_type, "x-bash")) {
            return "text";
        }
    }
    
    // Default fallback
    return "text";
}

// Common utility functions for input parsers
void input_skip_whitespace(const char **text) {
    while (**text && (**text == ' ' || **text == '\n' || **text == '\r' || **text == '\t')) {
        (*text)++;
    }
}

bool input_is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool input_is_empty_line(const char* line) {
    while (*line) {
        if (!isspace(*line)) return false;
        line++;
    }
    return true;
}

int input_count_leading_chars(const char* str, char ch) {
    int count = 0;
    while (str[count] == ch) count++;
    return count;
}

char* input_trim_whitespace(const char* str) {
    if (!str) return NULL;
    
    // Find start
    while (isspace(*str)) str++;
    
    if (*str == '\0') return strdup("");
    
    // Find end
    const char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    
    // Create trimmed copy
    int len = end - str + 1;
    char* result = (char*)malloc(len + 1);
    strncpy(result, str, len);
    result[len] = '\0';
    
    return result;
}

char** input_split_lines(const char* text, int* line_count) {
    *line_count = 0;
    
    if (!text) {
        return NULL;
    }
    
    // Count lines
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') (*line_count)++;
        ptr++;
    }
    if (ptr > text && *(ptr-1) != '\n') {
        (*line_count)++; // Last line without \n
    }
    
    if (*line_count == 0) {
        return NULL;
    }
    
    // Allocate array
    char** lines = (char**)malloc(*line_count * sizeof(char*));
    
    // Split into lines
    int line_index = 0;
    const char* line_start = text;
    ptr = text;
    
    while (*ptr && line_index < *line_count) {
        if (*ptr == '\n') {
            int len = ptr - line_start;
            lines[line_index] = (char*)malloc(len + 1);
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
        lines[line_index] = (char*)malloc(len + 1);
        strncpy(lines[line_index], line_start, len);
        lines[line_index][len] = '\0';
        line_index++;
    }
    
    // Adjust line count to actual lines created
    *line_count = line_index;
    
    return lines;
}

void input_free_lines(char** lines, int line_count) {
    if (!lines) return;
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
}

void input_add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value) {
    // Create key and value strings
    String* key = input_create_string(input, attr_name);
    String* value = input_create_string(input, attr_value);
    if (!key || !value) return;
    Item lambda_value = {.item = s2it(value)};
    elmt_put(element, key, lambda_value, input->pool);
}

void input_add_attribute_item_to_element(Input *input, Element* element, const char* attr_name, Item attr_value) {
    // Create key string
    String* key = input_create_string(input, attr_name);
    if (!key) return;
    elmt_put(element, key, attr_value, input->pool);
}

extern "C" Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor) {
    const char* effective_type = NULL;
    
    // Determine the effective type to use
    if (!type || strcmp(type->chars, "auto") == 0) {
        // Auto-detect MIME type
        MimeDetector* detector = mime_detector_init();
        if (detector) {
            const char* detected_mime = detect_mime_type(detector, abs_url->pathname ? abs_url->pathname->chars : "", source, strlen(source));
            if (detected_mime) {
                effective_type = mime_to_parser_type(detected_mime);
                printf("Auto-detected MIME type: %s -> parser type: %s\n", detected_mime, effective_type);
            } else {
                effective_type = "text";
                printf("MIME detection failed, defaulting to text\n");
            }
            mime_detector_destroy(detector);
        } else {
            effective_type = "text";
            printf("Failed to initialize MIME detector, defaulting to text\n");
        }
    } else {
        effective_type = type->chars;
    }
    printf("Effective type for input: %s\n", effective_type);
    
    Input* input = NULL;
    if (!effective_type || strcmp(effective_type, "text") == 0) { // treat as plain text
        input = (Input*)calloc(1, sizeof(Input));
        input->url = abs_url;
        String *str = (String*)malloc(sizeof(String) + strlen(source) + 1);
        str->len = strlen(source);  str->ref_cnt = 0;
        strcpy(str->chars, source);
        input->root = {.item = s2it(str)};
    }
    else {
        input = input_new(abs_url);
        if (strcmp(effective_type, "json") == 0) {
            parse_json(input, source);
        }
        else if (strcmp(effective_type, "csv") == 0) {
            parse_csv(input, source);
        }
        else if (strcmp(effective_type, "ini") == 0) {
            parse_ini(input, source);
        }
        else if (strcmp(effective_type, "properties") == 0) {
            parse_properties(input, source);
        }
        else if (strcmp(effective_type, "toml") == 0) {
            parse_toml(input, source);
        }
        else if (strcmp(effective_type, "yaml") == 0) {
            parse_yaml(input, source);
        }
        else if (strcmp(effective_type, "xml") == 0) {
            parse_xml(input, source);
        }
        else if (strcmp(effective_type, "markdown") == 0) {
            input->root = input_markup(input, source);
        }
        else if (strcmp(effective_type, "rst") == 0) {
            input->root = input_markup(input, source);
        }
        else if (strcmp(effective_type, "html") == 0) {
            parse_html(input, source);
        }
        else if (strcmp(effective_type, "latex") == 0) {
            parse_latex(input, source);
        }
        else if (strcmp(effective_type, "rtf") == 0) {
            parse_rtf(input, source);
        }
        else if (strcmp(effective_type, "pdf") == 0) {
            parse_pdf(input, source);
        }
        else if (strcmp(effective_type, "wiki") == 0) {
            input->root = input_markup(input, source);
        }
        else if (strcmp(effective_type, "asciidoc") == 0 || strcmp(effective_type, "adoc") == 0) {
            input->root = input_markup_with_format(input, source, MARKUP_ASCIIDOC);
        }
        else if (strcmp(effective_type, "man") == 0) {
            parse_man(input, source);
        }
        else if (strcmp(effective_type, "eml") == 0) {
            parse_eml(input, source);
        }
        else if (strcmp(effective_type, "vcf") == 0) {
            parse_vcf(input, source);
        }
        else if (strcmp(effective_type, "ics") == 0) {
            parse_ics(input, source);
        }
        else if (strcmp(effective_type, "textile") == 0) {
            input->root = input_markup(input, source);
        }
        else if (strcmp(effective_type, "mark") == 0) {
            parse_mark(input, source);
        }
        else if (strcmp(effective_type, "org") == 0) {
            parse_org(input, source);
        }
        else if (strcmp(effective_type, "css") == 0) {
            parse_css(input, source);
        }
        else if (strcmp(effective_type, "jsx") == 0) {
            parse_jsx(input, source);
        }
        else if (strcmp(effective_type, "mdx") == 0) {
            input->root = input_mdx(input, source);
        }
        else if (strcmp(effective_type, "math") == 0) {
            const char* math_flavor = (flavor && flavor->chars) ? flavor->chars : "latex";
            if (strcmp(math_flavor, "ascii") == 0) {
                // Use standalone ASCII math parser
                input->root = input_ascii_math(input, source);
            } else {
                // Use existing LaTeX/Typst math parser
                parse_math(input, source, math_flavor);
            }
        }
        else if (strncmp(effective_type, "math-", 5) == 0) {
            // Handle compound math formats like "math-ascii", "math-latex", etc.
            const char* math_flavor = effective_type + 5; // Skip "math-" prefix
            parse_math(input, source, math_flavor);
        }
        else if (strncmp(effective_type, "math-", 5) == 0) {
            // Handle compound math formats like "math-ascii", "math-latex", etc.
            const char* math_flavor = effective_type + 5; // Skip "math-" prefix
            parse_math(input, source, math_flavor);
        }
        else if (strcmp(effective_type, "markup") == 0) {
            input->root = input_markup(input, source);
        }
        else {
            printf("Unknown input type: %s\n", effective_type);
        }
    }
    // Note: don't free(source) here - it's the caller's responsibility
    return input;
}

// Helper function to read text file from file:// URL
static char* read_file_from_url(Url* url) {
    if (!url || url->scheme != URL_SCHEME_FILE) {
        fprintf(stderr, "Only file:// URLs are supported for file reading\n");
        return NULL;
    }
    
    const char* pathname = url_get_pathname(url);
    if (!pathname) return NULL;
    
    FILE* file = fopen(pathname, "r");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer and read file
    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    
    fclose(file);
    return content;
}

Input* input_from_url(String* url, String* type, String* flavor, Url* cwd) {
    printf("input_data at:: %s, type: %s, cwd:  %s\n", url ? url->chars : "null", type ? type->chars : "null", 
        cwd && cwd->pathname && cwd->pathname->chars ? cwd->pathname->chars : "null");
    
    Url* abs_url;
    if (cwd) {
        abs_url = url_parse_with_base(url->chars, cwd);
    } else {
        abs_url = url_parse(url->chars);
    }
    
    if (!abs_url) { 
        printf("Failed to parse URL\n");  
        return NULL; 
    }
    
    // Handle different URL schemes
    if (abs_url->scheme == URL_SCHEME_FILE) {
        // Check if URL points to a directory (only for file:// URLs)
        const char* pathname = url_get_pathname(abs_url);
        if (pathname) {
            struct stat st;
            if (stat(pathname, &st) == 0 && S_ISDIR(st.st_mode)) {
                // URL points to a directory - use directory listing
                printf("URL points to directory, using input_from_directory\n");
                Input* input = input_from_directory(pathname, false, 1); // non-recursive, single level only
                url_destroy(abs_url);
                return input;
            }
        }
        
        // URL points to a file - read as normal
        char* source = read_file_from_url(abs_url);
        if (!source) {
            printf("Failed to read document at URL: %s\n", url ? url->chars : "null");
            url_destroy(abs_url);
            return NULL;
        }
        
        Input* input = input_from_source(source, abs_url, type, flavor);
        free(source);  // Free the source string after parsing
        url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_HTTP || abs_url->scheme == URL_SCHEME_HTTPS) {
        // Handle HTTP/HTTPS URLs
        printf("HTTP/HTTPS URL detected, using HTTP client\n");
        
        const char* type_str = type ? type->chars : NULL;
        const char* flavor_str = flavor ? flavor->chars : NULL;
        
        Input* input = input_from_http(url->chars, type_str, flavor_str, "./temp/cache");
        url_destroy(abs_url);
        return input;
    }
    else if (abs_url->scheme == URL_SCHEME_SYS) {
        // Handle sys:// URLs for system information
        printf("sys:// URL detected, using system information provider\n");
        
        // Create a variable pool for the input
        VariableMemPool* pool;
        MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
        if (pool_err != MEM_POOL_ERR_OK) {
            printf("Failed to create variable pool for sys:// URL\n");
            url_destroy(abs_url);
            return NULL;
        }
        
        Input* input = input_from_sysinfo(abs_url, pool);
        if (!input) {
            pool_variable_destroy(pool);
        }
        url_destroy(abs_url);
        return input;
    }
    else {
        printf("Unsupported URL scheme for: %s\n", url ? url->chars : "null");
        url_destroy(abs_url);
        return NULL;
    }
}

