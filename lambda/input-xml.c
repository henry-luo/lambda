#include "transpiler.h"
#include "../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Item parse_element(Input *input, const char **xml);

static void skip_whitespace(const char **xml) {
    while (**xml && (**xml == ' ' || **xml == '\n' || **xml == '\r' || **xml == '\t')) {
        (*xml)++;
    }
}

static String* parse_string_content(Input *input, const char **xml, char end_char) {
    StrBuf* sb = input->sb;
    
    while (**xml && **xml != end_char) {
        if (**xml == '&') {
            (*xml)++; // Skip &
            if (strncmp(*xml, "lt;", 3) == 0) {
                strbuf_append_char(sb, '<');
                *xml += 3;
            } else if (strncmp(*xml, "gt;", 3) == 0) {
                strbuf_append_char(sb, '>');  
                *xml += 3;
            } else if (strncmp(*xml, "amp;", 4) == 0) {
                strbuf_append_char(sb, '&');
                *xml += 4;
            } else if (strncmp(*xml, "quot;", 5) == 0) {
                strbuf_append_char(sb, '"');
                *xml += 5;
            } else if (strncmp(*xml, "apos;", 5) == 0) {
                strbuf_append_char(sb, '\'');
                *xml += 5;
            } else {
                // Invalid entity, just append the &
                strbuf_append_char(sb, '&');
            }
        } else {
            strbuf_append_char(sb, **xml);
            (*xml)++;
        }
    }

    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static String* parse_tag_name(Input *input, const char **xml) {
    StrBuf* sb = input->sb;
    
    while (**xml && (isalnum(**xml) || **xml == '_' || **xml == '-' || **xml == ':')) {
        strbuf_append_char(sb, **xml);
        (*xml)++;
    }

    if (sb->length == sizeof(uint32_t)) return NULL; // empty tag name
    
    String *string = (String*)sb->str;
    string->len = sb->length - sizeof(uint32_t);
    string->ref_cnt = 0;
    strbuf_full_reset(sb);
    return string;
}

static Map* parse_attributes(Input *input, const char **xml) {
    Map* mp = map_pooled(input->pool);
    if (!mp) return NULL;
    
    LambdaTypeMap *map_type = (LambdaTypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
    if (!map_type) return mp;
    mp->type = map_type;
    
    int byte_offset = 0, byte_cap = 64;
    ShapeEntry* prev_entry = NULL;
    mp->data = pool_calloc(input->pool, byte_cap);
    mp->data_cap = byte_cap;
    if (!mp->data) return mp;
    
    skip_whitespace(xml);
    
    while (**xml && **xml != '>' && **xml != '/' && **xml != '?') {
        // parse attribute name
        String* attr_name = parse_tag_name(input, xml);
        if (!attr_name) break;
        
        skip_whitespace(xml);
        if (**xml != '=') break;
        (*xml)++; // skip =
        
        skip_whitespace(xml);
        if (**xml != '"' && **xml != '\'') break;
        
        char quote_char = **xml;
        (*xml)++; // skip opening quote

        String* attr_value = parse_string_content(input, xml, quote_char);
        if (!attr_value) break;

        if (**xml == quote_char) { (*xml)++; } // skip closing quote
        
        // Create shape entry for attribute
        ShapeEntry* shape_entry = (ShapeEntry*)pool_calloc(input->pool, 
            sizeof(ShapeEntry) + sizeof(StrView));
        StrView* nv = (StrView*)((char*)shape_entry + sizeof(ShapeEntry));
        nv->str = attr_name->chars;
        nv->length = attr_name->len;
        shape_entry->name = nv;
        shape_entry->type = type_info[LMD_TYPE_STRING].type;
        shape_entry->byte_offset = byte_offset;
        
        if (!prev_entry) {
            map_type->shape = shape_entry;
        } else {
            prev_entry->next = shape_entry;
        }
        prev_entry = shape_entry;
        map_type->length++;
        
        int bsize = type_info[LMD_TYPE_STRING].byte_size;
        byte_offset += bsize;
        if (byte_offset > byte_cap) {
            byte_cap *= 2;
            void* new_data = pool_calloc(input->pool, byte_cap);
            if (!new_data) return mp;
            memcpy(new_data, mp->data, byte_offset - bsize);
            pool_variable_free(input->pool, mp->data);
            mp->data = new_data;
            mp->data_cap = byte_cap;
        }
        
        void* field_ptr = (char*)mp->data + byte_offset - bsize;
        *(String**)field_ptr = attr_value;
        
        skip_whitespace(xml);
    }
    
    map_type->byte_size = byte_offset;
    arraylist_append(input->type_list, map_type);
    map_type->type_index = input->type_list->length - 1;
    
    return mp;
}

static Item parse_element(Input *input, const char **xml) {
    skip_whitespace(xml);
    
    if (**xml != '<') return ITEM_ERROR;
    (*xml)++; // skip <
    
    // check for special tags
    if (strncmp(*xml, "!--", 3) == 0) {
        // skip comments
        *xml += 3;
        while (**xml && strncmp(*xml, "-->", 3) != 0) { (*xml)++; }
        if (**xml) *xml += 3; // skip -->
        return parse_element(input, xml); // parse next element
    }
    
    if (strncmp(*xml, "![CDATA[", 8) == 0) {
        // handle CDATA
        *xml += 8;
        String* content = parse_string_content(input, xml, ']');
        if (**xml == ']' && *(*xml + 1) == ']' && *(*xml + 2) == '>') {
            *xml += 3; // skip ]]>
        }
        return s2it(content);
    }
    
    // check for self-closing or processing instruction
    bool is_processing = (**xml == '?');
    if (is_processing) (*xml)++; // skip ?

    // parse tag name
    String* tag_name = parse_tag_name(input, xml);
    if (!tag_name) return ITEM_ERROR;

    // parse attributes
    Map* attributes = parse_attributes(input, xml);
    if (!attributes) return ITEM_ERROR;
    
    skip_whitespace(xml);

    // check for self-closing tag
    bool self_closing = false;
    if (**xml == '/') {
        self_closing = true;
        (*xml)++; // skip /
    } else if (is_processing && **xml == '?') {
        self_closing = true;
        (*xml)++; // skip ?
    }
    
    if (**xml != '>') return ITEM_ERROR;
    (*xml)++; // skip >

    // create element map
    Map* element_map = map_pooled(input->pool);
    if (!element_map) return ITEM_ERROR;
    
    LambdaTypeMap *element_type = (LambdaTypeMap*)alloc_type(input->pool, LMD_TYPE_MAP, sizeof(LambdaTypeMap));
    if (!element_type) return (Item)element_map;
    element_map->type = element_type;
    
    int byte_offset = 0, byte_cap = 128;
    ShapeEntry* prev_entry = NULL;
    element_map->data = pool_calloc(input->pool, byte_cap);
    element_map->data_cap = byte_cap;
    if (!element_map->data) return (Item)element_map;
    
    // Add tag name field
    ShapeEntry* tag_shape = (ShapeEntry*)pool_calloc(input->pool, 
        sizeof(ShapeEntry) + sizeof(StrView));
    StrView* tag_nv = (StrView*)((char*)tag_shape + sizeof(ShapeEntry));
    tag_nv->str = "tag";
    tag_nv->length = 3;
    tag_shape->name = tag_nv;
    tag_shape->type = type_info[LMD_TYPE_STRING].type;
    tag_shape->byte_offset = byte_offset;
    element_type->shape = tag_shape;
    prev_entry = tag_shape;
    element_type->length++;
    
    int bsize = type_info[LMD_TYPE_STRING].byte_size;
    byte_offset += bsize;
    *(String**)((char*)element_map->data + tag_shape->byte_offset) = tag_name;
    
    // add attributes field if attributes exist
    if (attributes->type && ((LambdaTypeMap*)attributes->type)->length > 0) {
        if (byte_offset + type_info[LMD_TYPE_MAP].byte_size > byte_cap) {
            byte_cap *= 2;
            void* new_data = pool_calloc(input->pool, byte_cap);
            if (!new_data) return (Item)element_map;
            memcpy(new_data, element_map->data, byte_offset);
            pool_variable_free(input->pool, element_map->data);
            element_map->data = new_data;
            element_map->data_cap = byte_cap;
        }
        
        ShapeEntry* attr_shape = (ShapeEntry*)pool_calloc(input->pool, 
            sizeof(ShapeEntry) + sizeof(StrView));
        StrView* attr_nv = (StrView*)((char*)attr_shape + sizeof(ShapeEntry));
        attr_nv->str = "attributes";
        attr_nv->length = 10;
        attr_shape->name = attr_nv;
        attr_shape->type = type_info[LMD_TYPE_MAP].type;
        attr_shape->byte_offset = byte_offset;
        prev_entry->next = attr_shape;
        prev_entry = attr_shape;
        element_type->length++;
        
        byte_offset += type_info[LMD_TYPE_MAP].byte_size;
        *(Map**)((char*)element_map->data + attr_shape->byte_offset) = attributes;
    }
    
    if (!self_closing) {
        // Parse content and children
        Array* children = array_pooled(input->pool);
        if (!children) return (Item)element_map;
        
        String* text_content = NULL;
        bool has_text = false;
        
        skip_whitespace(xml);
        
        while (**xml && !(**xml == '<' && *(*xml + 1) == '/')) {
            if (**xml == '<') {
                // Child element
                Item child = parse_element(input, xml);
                if (child != ITEM_ERROR) {
                    LambdaItem child_item = {.item = child};
                    array_append(children, child_item, input->pool);
                }
            } else {
                // Text content
                if (!has_text) {
                    text_content = parse_string_content(input, xml, '<');
                    has_text = true;
                } else {
                    (*xml)++; // Skip character if we already have text
                }
            }
            skip_whitespace(xml);
        }
        
        // Skip closing tag
        if (**xml == '<' && *(*xml + 1) == '/') {
            *xml += 2; // Skip </
            while (**xml && **xml != '>') {
                (*xml)++; // Skip tag name
            }
            if (**xml == '>') (*xml)++; // Skip >
        }
        
        // Add text content if exists
        if (has_text && text_content && text_content->len > 0) {
            if (byte_offset + type_info[LMD_TYPE_STRING].byte_size > byte_cap) {
                byte_cap *= 2;
                void* new_data = pool_calloc(input->pool, byte_cap);
                if (!new_data) return (Item)element_map;
                memcpy(new_data, element_map->data, byte_offset);
                pool_variable_free(input->pool, element_map->data);
                element_map->data = new_data;
                element_map->data_cap = byte_cap;
            }
            
            ShapeEntry* text_shape = (ShapeEntry*)pool_calloc(input->pool, 
                sizeof(ShapeEntry) + sizeof(StrView));
            StrView* text_nv = (StrView*)((char*)text_shape + sizeof(ShapeEntry));
            text_nv->str = "text";
            text_nv->length = 4;
            text_shape->name = text_nv;
            text_shape->type = type_info[LMD_TYPE_STRING].type;
            text_shape->byte_offset = byte_offset;
            prev_entry->next = text_shape;
            prev_entry = text_shape;
            element_type->length++;
            
            byte_offset += type_info[LMD_TYPE_STRING].byte_size;
            *(String**)((char*)element_map->data + text_shape->byte_offset) = text_content;
        }
        
        // Add children if exist
        if (children->length > 0) {
            if (byte_offset + type_info[LMD_TYPE_ARRAY].byte_size > byte_cap) {
                byte_cap *= 2;
                void* new_data = pool_calloc(input->pool, byte_cap);
                if (!new_data) return (Item)element_map;
                memcpy(new_data, element_map->data, byte_offset);
                pool_variable_free(input->pool, element_map->data);
                element_map->data = new_data;
                element_map->data_cap = byte_cap;
            }
            
            ShapeEntry* children_shape = (ShapeEntry*)pool_calloc(input->pool, 
                sizeof(ShapeEntry) + sizeof(StrView));
            StrView* children_nv = (StrView*)((char*)children_shape + sizeof(ShapeEntry));
            children_nv->str = "children";
            children_nv->length = 8;
            children_shape->name = children_nv;
            children_shape->type = type_info[LMD_TYPE_ARRAY].type;
            children_shape->byte_offset = byte_offset;
            prev_entry->next = children_shape;
            prev_entry = children_shape;
            element_type->length++;
            
            byte_offset += type_info[LMD_TYPE_ARRAY].byte_size;
            *(Array**)((char*)element_map->data + children_shape->byte_offset) = children;
        }
    }
    
    element_type->byte_size = byte_offset;
    arraylist_append(input->type_list, element_type);
    element_type->type_index = input->type_list->length - 1;
    
    return (Item)element_map;
}

Input* xml_parse(const char* xml_string) {
    printf("xml_parse: %s\n", xml_string);
    Input* input = malloc(sizeof(Input));
    input->path = NULL; // path for XML input
    size_t grow_size = 1024;  // 1k
    size_t tolerance_percent = 20;
    MemPoolError err = pool_variable_init(&input->pool, grow_size, tolerance_percent);
    if (err != MEM_POOL_ERR_OK) {
        free(input);
        return NULL;
    }
    input->type_list = arraylist_new(16);
    input->root = ITEM_NULL;
    input->sb = strbuf_new_pooled(input->pool);

    // Skip XML declaration if present
    const char* xml = xml_string;
    skip_whitespace(&xml);
    if (strncmp(xml, "<?xml", 5) == 0) {
        while (*xml && *xml != '>') {
            xml++;
        }
        if (*xml == '>') xml++;
    }
    
    input->root = parse_element(input, &xml);
    return input;
}
