#include "../transpiler.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// helper function to check if output needs proper YAML formatting
static void convert_json_to_yaml(StrBuf* sb, const char* json_str) {
    // this converts the JSON-like output from print_named_items to YAML format
    const char* ptr = json_str;
    bool first_item = true;
    
    while (*ptr) {
        if (*ptr == '{') {
            // skip opening brace
            ptr++;
        } else if (*ptr == '}') {
            // skip closing brace  
            ptr++;
        } else if (*ptr == ',') {
            // replace comma with newline
            strbuf_append_char(sb, '\n');
            first_item = false;
            ptr++;
        } else if (*ptr == '"' && ptr[1] && strchr(ptr + 1, '"')) {
            // handle quoted strings - copy as-is
            strbuf_append_char(sb, *ptr);
            ptr++;
        } else if (*ptr == ':') {
            // replace colon with ": " for proper YAML
            strbuf_append_str(sb, ": ");
            ptr++;
        } else {
            // copy character as-is
            strbuf_append_char(sb, *ptr);
            ptr++;
        }
    }
}

// yaml formatter that produces proper YAML output
String* format_yaml(VariableMemPool* pool, Item root_item) {
    printf("format_yaml: ENTRY - enhanced version\n");
    fflush(stdout);
    
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    // start with YAML document marker
    strbuf_append_str(sb, "---\n");
    
    // add lowercase comment as requested
    strbuf_append_str(sb, "# yaml formatted output\n");
    
    TypeId type = get_type_id((LambdaItem)root_item);
    
    if (type == LMD_TYPE_MAP) {
        Map* mp = (Map*)root_item;
        if (mp && mp->type) {
            TypeMap* map_type = (TypeMap*)mp->type;
            
            // get JSON-like output from print_named_items
            StrBuf* temp_sb = strbuf_new_pooled(pool);
            print_named_items(temp_sb, map_type, mp->data);
            String* temp_str = strbuf_to_string(temp_sb);
            
            // convert to proper YAML format
            convert_json_to_yaml(sb, temp_str->chars);
            
            strbuf_free(temp_sb);
        } else {
            strbuf_append_str(sb, "{}");
        }
    } else {
        // for non-map types, just append a simple representation
        strbuf_append_str(sb, "null");
    }
    
    printf("format_yaml: completed enhanced version\n");
    fflush(stdout);
    
    return strbuf_to_string(sb);
}
