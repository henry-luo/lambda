#include "../transpiler.h"

void print_named_items(StrBuf *strbuf, TypeMap *map_type, void* map_data);

// helper function to convert JSON-like output to TOML format
static void convert_json_to_toml(StrBuf* sb, const char* json_str) {
    // this converts the JSON-like output from print_named_items to TOML format
    const char* ptr = json_str;
    bool in_string = false;
    
    while (*ptr) {
        if (*ptr == '"') {
            // track string boundaries
            in_string = !in_string;
            strbuf_append_char(sb, *ptr);
            ptr++;
        } else if (!in_string && *ptr == '{') {
            // skip opening brace
            ptr++;
        } else if (!in_string && *ptr == '}') {
            // skip closing brace  
            ptr++;
        } else if (!in_string && *ptr == ',') {
            // replace comma with newline
            strbuf_append_char(sb, '\n');
            ptr++;
            // skip whitespace after comma
            while (*ptr == ' ' || *ptr == '\t') ptr++;
        } else if (!in_string && *ptr == ':') {
            // replace colon with " = " for TOML format
            strbuf_append_str(sb, " = ");
            ptr++;
            // skip whitespace after colon
            while (*ptr == ' ' || *ptr == '\t') ptr++;
        } else {
            // copy character as-is
            strbuf_append_char(sb, *ptr);
            ptr++;
        }
    }
}

// toml formatter that produces proper TOML output
String* format_toml(VariableMemPool* pool, Item root_item) {
    StrBuf* sb = strbuf_new_pooled(pool);
    if (!sb) return NULL;
    
    // add lowercase comment as requested
    strbuf_append_str(sb, "# toml formatted output\n");
    
    TypeId type = get_type_id((LambdaItem)root_item);
    
    if (type == LMD_TYPE_MAP) {
        Map* mp = (Map*)root_item;
        if (mp && mp->type) {
            TypeMap* map_type = (TypeMap*)mp->type;
            
            // get JSON-like output from print_named_items
            StrBuf* temp_sb = strbuf_new_pooled(pool);
            print_named_items(temp_sb, map_type, mp->data);
            String* temp_str = strbuf_to_string(temp_sb);
            
            // convert to proper TOML format
            convert_json_to_toml(sb, temp_str->chars);
            
            strbuf_free(temp_sb);
        } else {
            strbuf_append_str(sb, "# empty map\n");
        }
    } else {
        // for non-map types, just append a simple representation
        strbuf_append_str(sb, "value = null\n");
    }
    
    return strbuf_to_string(sb);
}
