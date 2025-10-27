#include "element_reader.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"
#include <cstring>
#include <cstdlib>



// ==============================================================================
// ElementReader Implementation  
// ==============================================================================

ElementReader* element_reader_create(const Element* element, Pool* pool) {
    if (!element || !pool) return NULL;
    
    ElementReader* reader = (ElementReader*)pool_alloc(pool, sizeof(ElementReader));
    if (!reader) return NULL;
    
    reader->element = element;
    reader->element_type = (const TypeElmt*)element->type;
    
    if (reader->element_type) {
        reader->tag_name = reader->element_type->name.str;
        reader->tag_name_len = reader->element_type->name.length;
    } else {
        reader->tag_name = NULL;
        reader->tag_name_len = 0;
    }
    
    // Cache child count (Element inherits from List)
    const List* list = (const List*)element;
    reader->child_count = list->length;
    
    // Cache attribute count from the map shape
    reader->attr_count = 0;
    if (reader->element_type) {
        const TypeMap* map_type = (const TypeMap*)reader->element_type;
        reader->attr_count = map_type->length;
    }
    
    return reader;
}

ElementReader* element_reader_from_item(Item item, Pool* pool) {
    if (get_type_id(item) != LMD_TYPE_ELEMENT) return NULL;
    return element_reader_create(item.element, pool);
}

void element_reader_free(ElementReader* reader, Pool* pool) {
    // Since we're using pool allocation, no explicit free needed
    // Pool cleanup will handle memory management
    (void)reader;
    (void)pool;
}

// ==============================================================================
// Element Property Access
// ==============================================================================

const char* element_reader_tag_name(const ElementReader* reader) {
    return reader ? reader->tag_name : NULL;
}

int64_t element_reader_tag_name_len(const ElementReader* reader) {
    return reader ? reader->tag_name_len : 0;
}

bool element_reader_has_tag(const ElementReader* reader, const char* tag_name) {
    if (!reader || !reader->tag_name || !tag_name) return false;
    return strcmp(reader->tag_name, tag_name) == 0;
}

bool element_reader_has_tag_n(const ElementReader* reader, const char* tag_name, int64_t len) {
    if (!reader || !reader->tag_name || !tag_name) return false;
    if (reader->tag_name_len != len) return false;
    return strncmp(reader->tag_name, tag_name, len) == 0;
}

int64_t element_reader_child_count(const ElementReader* reader) {
    return reader ? reader->child_count : 0;
}

int64_t element_reader_attr_count(const ElementReader* reader) {
    return reader ? reader->attr_count : 0;
}

bool element_reader_is_empty(const ElementReader* reader) {
    if (!reader) return true;
    
    // Check if has no children
    if (reader->child_count == 0) return true;
    
    // Check if all children are empty strings
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_ELEMENT) {
            return false; // Has child elements
        } else if (type == LMD_TYPE_STRING) {
            String* str = get_string(child);
            if (str && str->len > 0) {
                return false; // Has non-empty text
            }
        } else if (type != LMD_TYPE_NULL) {
            return false; // Has other content
        }
    }
    
    return true;
}

bool element_reader_is_text_only(const ElementReader* reader) {
    if (!reader || reader->child_count == 0) return false;
    
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_ELEMENT) {
            return false; // Has child elements
        }
    }
    
    return true; // Only text/non-element content
}

// ==============================================================================
// Child Access
// ==============================================================================

Item element_reader_child_at(const ElementReader* reader, int64_t index) {
    if (!reader || index < 0 || index >= reader->child_count) {
        return ItemNull;
    }
    
    const List* list = (const List*)reader->element;
    return list->items[index];
}

TypedItem element_reader_child_typed_at(const ElementReader* reader, int64_t index) {
    Item item = element_reader_child_at(reader, index);
    
    // Simple local implementation to avoid linking issues
    TypeId type_id = get_type_id(item);
    TypedItem result = {.type_id = type_id};

    switch (type_id) {
    case LMD_TYPE_NULL:
        result.type_id = LMD_TYPE_NULL;
        return result;
    case LMD_TYPE_STRING:
        result.string = (String*)item.pointer;
        return result;
    case LMD_TYPE_ELEMENT:
        result.element = item.element;
        return result;
    case LMD_TYPE_LIST:
        result.list = item.list;
        return result;
    case LMD_TYPE_MAP:
        result.map = item.map;
        return result;
    default:
        result.type_id = LMD_TYPE_ERROR;
        return result;
    }
}

Item element_reader_find_child(const ElementReader* reader, const char* tag_name) {
    if (!reader || !tag_name) return ItemNull;
    
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            TypeElmt* child_type = (TypeElmt*)child_elem->type;
            
            if (child_type && child_type->name.str &&
                strcmp(child_type->name.str, tag_name) == 0) {
                return child;
            }
        }
    }
    
    return ItemNull;
}

ArrayList* element_reader_find_children(const ElementReader* reader, const char* tag_name, Pool* pool) {
    if (!reader || !tag_name || !pool) return NULL;
    
    ArrayList* results = arraylist_new(0);
    if (!results) return NULL;
    
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            TypeElmt* child_type = (TypeElmt*)child_elem->type;
            
            if (child_type && child_type->name.str &&
                strcmp(child_type->name.str, tag_name) == 0) {
                arraylist_append(results, (void*)(uintptr_t)child.item);
            }
        }
    }
    
    return results;
}

// Forward declaration
static void _extract_text_recursive(const ElementReader* reader, StringBuf* sb);

String* element_reader_text_content(const ElementReader* reader, Pool* pool) {
    if (!reader || !pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    _extract_text_recursive(reader, sb);
    
    // Convert StringBuf to String
    String* result = (String*)pool_alloc(pool, sizeof(String) + sb->length + 1);
    if (result) {
        result->len = sb->length;
        result->ref_cnt = 1;
        memcpy(result->chars, sb->str->chars, sb->length);
        result->chars[sb->length] = '\0';
    }
    
    stringbuf_free(sb);
    return result;
}

String* element_reader_immediate_text(const ElementReader* reader, Pool* pool) {
    if (!reader || !pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        
        if (get_type_id(child) == LMD_TYPE_STRING) {
            String* str = get_string(child);
            if (str && str->len > 0) {
                stringbuf_append_str_n(sb, str->chars, str->len);
            }
        }
    }
    
    // Convert StringBuf to String
    String* result = (String*)pool_alloc(pool, sizeof(String) + sb->length + 1);
    if (result) {
        result->len = sb->length;
        result->ref_cnt = 1;
        memcpy(result->chars, sb->str->chars, sb->length);
        result->chars[sb->length] = '\0';
    }
    
    stringbuf_free(sb);
    return result;
}

// Helper function for recursive text extraction
static void _extract_text_recursive(const ElementReader* reader, StringBuf* sb) {
    if (!reader || !sb) return;
    
    const List* list = (const List*)reader->element;
    for (int64_t i = 0; i < list->length; i++) {
        Item child = list->items[i];
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_STRING) {
            String* str = get_string(child);
            if (str && str->len > 0) {
                stringbuf_append_str_n(sb, str->chars, str->len);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            // Recursively extract text from child elements
            Element* child_elem = (Element*)child.pointer;
            if (child_elem) {
                ElementReader child_reader;
                child_reader.element = child_elem;
                child_reader.element_type = (const TypeElmt*)child_elem->type;
                if (child_reader.element_type) {
                    child_reader.tag_name = child_reader.element_type->name.str;
                    child_reader.tag_name_len = child_reader.element_type->name.length;
                } else {
                    child_reader.tag_name = NULL;
                    child_reader.tag_name_len = 0;
                }
                child_reader.child_count = ((const List*)child_elem)->length;
                child_reader.attr_count = 0;
                
                _extract_text_recursive(&child_reader, sb);
            }
        }
    }
}

// ==============================================================================
// Attribute Access
// ==============================================================================

AttributeReader* element_reader_attributes(const ElementReader* reader, Pool* pool) {
    if (!reader || !pool || !reader->element_type) return NULL;
    
    AttributeReader* attr_reader = (AttributeReader*)pool_alloc(pool, sizeof(AttributeReader));
    if (!attr_reader) return NULL;
    
    attr_reader->element_reader = reader;
    attr_reader->map_type = (const TypeMap*)reader->element_type;
    attr_reader->attr_data = reader->element->data;
    attr_reader->shape = attr_reader->map_type->shape;
    
    return attr_reader;
}

void attribute_reader_free(AttributeReader* attr_reader, Pool* pool) {
    // Pool-based allocation, no explicit free needed
    (void)attr_reader;
    (void)pool;
}

bool attribute_reader_has(const AttributeReader* attr_reader, const char* attr_name) {
    if (!attr_reader || !attr_name || !attr_reader->shape) return false;
    
    const ShapeEntry* field = attr_reader->shape;
    size_t attr_name_len = strlen(attr_name);
    
    while (field) {
        if (field->name && field->name->length == attr_name_len &&
            strncmp(field->name->str, attr_name, attr_name_len) == 0) {
            return true;
        }
        field = field->next;
    }
    
    return false;
}

const String* attribute_reader_get_string(const AttributeReader* attr_reader, const char* attr_name) {
    if (!attr_reader || !attr_name || !attr_reader->shape || !attr_reader->attr_data) {
        return NULL;
    }
    
    const ShapeEntry* field = attr_reader->shape;
    size_t attr_name_len = strlen(attr_name);
    
    while (field) {
        if (field->name && field->name->length == attr_name_len &&
            strncmp(field->name->str, attr_name, attr_name_len) == 0) {
            
            if (field->type && field->type->type_id == LMD_TYPE_STRING) {
                const void* data = ((const char*)attr_reader->attr_data) + field->byte_offset;
                return *(const String**)data;
            }
            break;
        }
        field = field->next;
    }
    
    return NULL;
}

const char* attribute_reader_get_cstring(const AttributeReader* attr_reader, const char* attr_name) {
    const String* str = attribute_reader_get_string(attr_reader, attr_name);
    return str ? str->chars : NULL;
}

TypedItem attribute_reader_get_typed(const AttributeReader* attr_reader, const char* attr_name) {
    TypedItem null_result = {0};
    
    if (!attr_reader || !attr_name || !attr_reader->shape || !attr_reader->attr_data) {
        return null_result;
    }
    
    const ShapeEntry* field = attr_reader->shape;
    size_t attr_name_len = strlen(attr_name);
    
    while (field) {
        if (field->name && field->name->length == attr_name_len &&
            strncmp(field->name->str, attr_name, attr_name_len) == 0) {
            
            if (field->type) {
                TypedItem result;
                result.type_id = field->type->type_id;
                
                const void* data = ((const char*)attr_reader->attr_data) + field->byte_offset;
                
                switch (field->type->type_id) {
                    case LMD_TYPE_STRING:
                        result.string = *(String**)data;
                        break;
                    case LMD_TYPE_INT:
                        result.int_val = *(int*)data;
                        break;
                    case LMD_TYPE_INT64:
                        result.long_val = *(int64_t*)data;
                        break;
                    case LMD_TYPE_FLOAT:
                        result.double_val = *(double*)data;
                        break;
                    case LMD_TYPE_BOOL:
                        result.bool_val = *(bool*)data;
                        break;
                    default:
                        result.pointer = *(void**)data;
                        break;
                }
                
                return result;
            }
            break;
        }
        field = field->next;
    }
    
    return null_result;
}

ArrayList* attribute_reader_names(const AttributeReader* attr_reader, Pool* pool) {
    if (!attr_reader || !pool || !attr_reader->shape) return NULL;
    
    ArrayList* names = arraylist_new(0);
    if (!names) return NULL;
    
    const ShapeEntry* field = attr_reader->shape;
    while (field) {
        if (field->name) {
            arraylist_append(names, (void*)field->name);
        }
        field = field->next;
    }
    
    return names;
}