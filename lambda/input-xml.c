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

    // Create element
    Element* element = elmt_pooled(input->pool);
    if (!element) return ITEM_ERROR;
    
    LambdaTypeElmt *element_type = (LambdaTypeElmt*)alloc_type(input->pool, LMD_TYPE_ELEMENT, sizeof(LambdaTypeElmt));
    if (!element_type) return (Item)element;
    element->type = element_type;
    
    // Set element name
    element_type->name.str = tag_name->chars;
    element_type->name.length = tag_name->len;
    
    // Set up attributes in the Element's Map data
    if (attributes->type && ((LambdaTypeMap*)attributes->type)->length > 0) {
        LambdaTypeMap* attr_map_type = (LambdaTypeMap*)attributes->type;
        element_type->shape = attr_map_type->shape;
        element_type->length = attr_map_type->length;
        element_type->byte_size = attr_map_type->byte_size;
        
        // Allocate and copy attributes data to element
        element->data = pool_calloc(input->pool, element_type->byte_size);
        element->data_cap = element_type->byte_size;
        if (element->data) {
            memcpy(element->data, attributes->data, element_type->byte_size);
        }
    } else {
        // No attributes
        element->data = NULL;
        element->data_cap = 0;
        element_type->shape = NULL;
        element_type->length = 0;
        element_type->byte_size = 0;
    }
    arraylist_append(input->type_list, element_type);
    element_type->type_index = input->type_list->length - 1;
        
    if (!self_closing) {
        // Parse content and add to Element's List part
        skip_whitespace(xml);
        
        while (**xml && !(**xml == '<' && *(*xml + 1) == '/')) {
            if (**xml == '<') {
                // Child element
                Item child = parse_element(input, xml);
                if (child != ITEM_ERROR) {
                    list_push((List*)element, child);
                    element_type->content_length++;
                }
            } else {
                // Text content
                String* text_content = parse_string_content(input, xml, '<');
                if (text_content && text_content->len > 0) {
                    list_push((List*)element, s2it(text_content));
                    element_type->content_length++;
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
    }    
    return (Item)element;
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
