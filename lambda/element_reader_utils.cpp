#include "element_reader.h" 
#include "../lib/arraylist.h"
#include <cstring>
#include <cstdlib>



// Forward declarations for helper functions
// static void _extract_text_recursive(const ElementReader* reader, StringBuf* sb); // unused
static Item _iter_children_next(ElementIterator* iter);
static Item _iter_depth_first_next(ElementIterator* iter);
static Item _iter_breadth_first_next(ElementIterator* iter);
static Item _iter_text_only_next(ElementIterator* iter);
static void _debug_element_recursive(const ElementReader* reader, StringBuf* sb, int depth);

// ==============================================================================
// ElementIterator Implementation
// ==============================================================================

typedef struct IteratorState {
    ArrayList* stack;           // Stack for depth-first traversal
    ArrayList* queue;           // Queue for breadth-first traversal
    int64_t current_depth;      // Current traversal depth
    bool finished;              // Iteration complete flag
} IteratorState;

typedef struct StackFrame {
    const ElementReader* reader;
    int64_t child_index;
    int64_t depth;
} StackFrame;

ElementIterator* element_iterator_create(const ElementReader* root, IteratorMode mode, Pool* pool) {
    if (!root || !pool) return NULL;
    
    ElementIterator* iter = (ElementIterator*)pool_alloc(pool, sizeof(ElementIterator));
    if (!iter) return NULL;
    
    iter->root = root;
    iter->mode = mode;
    iter->current_index = 0;
    iter->max_depth = -1; // Unlimited by default
    iter->pool = pool;
    
    // Initialize state based on mode
    IteratorState* state = (IteratorState*)pool_alloc(pool, sizeof(IteratorState));
    if (!state) return NULL;
    
    state->stack = NULL;
    state->queue = NULL;
    state->current_depth = 0;
    state->finished = false;
    
    if (mode == ITER_DEPTH_FIRST || mode == ITER_ELEMENTS_ONLY) {
        state->stack = arraylist_new(0);
        if (!state->stack) return NULL;
        
        // Push root onto stack
        StackFrame* frame = (StackFrame*)pool_alloc(pool, sizeof(StackFrame));
        if (!frame) return NULL;
        frame->reader = root;
        frame->child_index = -1; // Start before first child
        frame->depth = 0;
        arraylist_append(state->stack, frame);
    } else if (mode == ITER_BREADTH_FIRST) {
        state->queue = arraylist_new(0);
        if (!state->queue) return NULL;
        
        // Add root to queue
        arraylist_append(state->queue, (void*)root);
    }
    
    iter->state = state;
    return iter;
}

void element_iterator_set_max_depth(ElementIterator* iter, int64_t max_depth) {
    if (iter) {
        iter->max_depth = max_depth;
    }
}

Item element_iterator_next(ElementIterator* iter) {
    if (!iter || !iter->state) return ItemNull;
    
    IteratorState* state = (IteratorState*)iter->state;
    if (state->finished) return ItemNull;
    
    switch (iter->mode) {
        case ITER_CHILDREN_ONLY:
            return _iter_children_next(iter);
        case ITER_DEPTH_FIRST:
        case ITER_ELEMENTS_ONLY:
            return _iter_depth_first_next(iter);
        case ITER_BREADTH_FIRST:
            return _iter_breadth_first_next(iter);
        case ITER_TEXT_ONLY:
            return _iter_text_only_next(iter);
        default:
            return ItemNull;
    }
}

ElementReader* element_iterator_next_element(ElementIterator* iter) {
    Item item = element_iterator_next(iter);
    if (get_type_id(item) == LMD_TYPE_ELEMENT) {
        return element_reader_from_item(item, iter->pool);
    }
    return NULL;
}

void element_iterator_reset(ElementIterator* iter) {
    if (!iter || !iter->state) return;
    
    IteratorState* state = (IteratorState*)iter->state;
    iter->current_index = 0;
    state->current_depth = 0;
    state->finished = false;
    
    // Reset based on mode
    if (state->stack) {
        arraylist_clear(state->stack);
        // Re-add root
        StackFrame* frame = (StackFrame*)pool_alloc(iter->pool, sizeof(StackFrame));
        if (frame) {
            frame->reader = iter->root;
            frame->child_index = -1;
            frame->depth = 0;
            arraylist_append(state->stack, frame);
        }
    }
    
    if (state->queue) {
        arraylist_clear(state->queue);
        arraylist_append(state->queue, (void*)iter->root);
    }
}

bool element_iterator_has_next(const ElementIterator* iter) {
    if (!iter || !iter->state) return false;
    
    IteratorState* state = (IteratorState*)iter->state;
    if (state->finished) return false;
    
    // Check if we have more items based on iterator mode
    switch (iter->mode) {
        case ITER_CHILDREN_ONLY:
            return iter->current_index < element_reader_child_count(iter->root);
        default:
            // For other modes, rely on the finished flag
            return !state->finished;
    }
}

int64_t element_iterator_depth(const ElementIterator* iter) {
    if (!iter || !iter->state) return 0;
    
    IteratorState* state = (IteratorState*)iter->state;
    return state->current_depth;
}

void element_iterator_free(ElementIterator* iter) {
    // Pool-based allocation, no explicit free needed
    (void)iter;
}

// ==============================================================================
// Iterator Helper Functions
// ==============================================================================

static Item _iter_children_next(ElementIterator* iter) {
    if (iter->current_index >= element_reader_child_count(iter->root)) {
        IteratorState* state = (IteratorState*)iter->state;
        state->finished = true;
        return ItemNull;
    }
    
    Item child = element_reader_child_at(iter->root, iter->current_index);
    iter->current_index++;
    return child;
}

static Item _iter_depth_first_next(ElementIterator* iter) {
    IteratorState* state = (IteratorState*)iter->state;
    
    while (state->stack && state->stack->length > 0) {
        StackFrame* frame = (StackFrame*)state->stack->data[state->stack->length - 1];
        
        // Check depth limit
        if (iter->max_depth >= 0 && frame->depth > iter->max_depth) {
            arraylist_remove(state->stack, state->stack->length - 1);
            continue;
        }
        
        frame->child_index++;
        
        if (frame->child_index >= element_reader_child_count(frame->reader)) {
            // Finished with this element's children
            arraylist_remove(state->stack, state->stack->length - 1);
            continue;
        }
        
        Item child = element_reader_child_at(frame->reader, frame->child_index);
        state->current_depth = frame->depth + 1;
        
        // If child is an element, push it onto the stack for future traversal
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            ElementReader* child_reader = element_reader_from_item(child, iter->pool);
            if (child_reader) {
                StackFrame* child_frame = (StackFrame*)pool_alloc(iter->pool, sizeof(StackFrame));
                if (child_frame) {
                    child_frame->reader = child_reader;
                    child_frame->child_index = -1;
                    child_frame->depth = frame->depth + 1;
                    arraylist_append(state->stack, child_frame);
                }
            }
        }
        
        // Apply filtering for ITER_ELEMENTS_ONLY
        if (iter->mode == ITER_ELEMENTS_ONLY && get_type_id(child) != LMD_TYPE_ELEMENT) {
            continue; // Skip non-element nodes
        }
        
        return child;
    }
    
    state->finished = true;
    return ItemNull;
}

static Item _iter_breadth_first_next(ElementIterator* iter) {
    IteratorState* state = (IteratorState*)iter->state;
    
    if (!state->queue || state->queue->length == 0) {
        state->finished = true;
        return ItemNull;
    }
    
    // Get next element from queue
    const ElementReader* current = (const ElementReader*)state->queue->data[0];
    arraylist_remove(state->queue, 0);
    
    // Add all children to queue
    for (int64_t i = 0; i < element_reader_child_count(current); i++) {
        Item child = element_reader_child_at(current, i);
        
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            ElementReader* child_reader = element_reader_from_item(child, iter->pool);
            if (child_reader) {
                arraylist_append(state->queue, child_reader);
            }
        }
    }
    
    // Return current element as Item
    return (Item){.element = (Element*)current->element};
}

static Item _iter_text_only_next(ElementIterator* iter) {
    // This is a simplified text-only iterator
    // For a complete implementation, you'd need to traverse the tree
    // and collect only text nodes
    
    IteratorState* state = (IteratorState*)iter->state;
    
    if (iter->current_index >= element_reader_child_count(iter->root)) {
        state->finished = true;
        return ItemNull;
    }
    
    // Find next text node
    while (iter->current_index < element_reader_child_count(iter->root)) {
        Item child = element_reader_child_at(iter->root, iter->current_index);
        iter->current_index++;
        
        if (get_type_id(child) == LMD_TYPE_STRING) {
            return child;
        }
    }
    
    state->finished = true;
    return ItemNull;
}

// ==============================================================================
// Utility Functions
// ==============================================================================

ElementReader* element_reader_from_input_root(const Input* input, Pool* pool) {
    if (!input || !pool) return NULL;
    
    Item root = input->root;
    TypeId root_type = get_type_id(root);
    
    if (root_type == LMD_TYPE_ELEMENT) {
        return element_reader_from_item(root, pool);
    } else if (root_type == LMD_TYPE_LIST) {
        // Search for first element in the list (skip DOCTYPE, comments, etc.)
        List* root_list = root.list;
        for (int64_t i = 0; i < root_list->length; i++) {
            Item item = root_list->items[i];
            if (get_type_id(item) == LMD_TYPE_ELEMENT) {
                Element* elem = item.element;
                TypeElmt* type = (TypeElmt*)elem->type;
                
                // Skip DOCTYPE and comments
                if (type && type->name.str &&
                    strcmp(type->name.str, "!DOCTYPE") != 0 &&
                    strcmp(type->name.str, "!--") != 0) {
                    return element_reader_from_item(item, pool);
                }
            }
        }
    }
    
    return NULL;
}

ElementReader* element_reader_find_by_id(const ElementReader* root, const char* id, Pool* pool) {
    if (!root || !id || !pool) return NULL;
    
    ElementIterator* iter = element_iterator_create(root, ITER_DEPTH_FIRST, pool);
    if (!iter) return NULL;
    
    while (element_iterator_has_next(iter)) {
        Item item = element_iterator_next(iter);
        
        if (get_type_id(item) == LMD_TYPE_ELEMENT) {
            ElementReader* reader = element_reader_from_item(item, pool);
            if (reader) {
                AttributeReader* attrs = element_reader_attributes(reader, pool);
                if (attrs) {
                    const char* elem_id = attribute_reader_get_cstring(attrs, "id");
                    if (elem_id && strcmp(elem_id, id) == 0) {
                        return reader;
                    }
                }
            }
        }
    }
    
    return NULL;
}

ArrayList* element_reader_find_by_class(const ElementReader* root, const char* class_name, Pool* pool) {
    if (!root || !class_name || !pool) return NULL;
    
    ArrayList* results = arraylist_new(0);
    if (!results) return NULL;
    
    ElementIterator* iter = element_iterator_create(root, ITER_ELEMENTS_ONLY, pool);
    if (!iter) return results;
    
    while (element_iterator_has_next(iter)) {
        ElementReader* reader = element_iterator_next_element(iter);
        if (reader) {
            AttributeReader* attrs = element_reader_attributes(reader, pool);
            if (attrs) {
                const char* class_attr = attribute_reader_get_cstring(attrs, "class");
                if (class_attr && strstr(class_attr, class_name)) {
                    // TODO: More sophisticated class matching (word boundaries)
                    arraylist_append(results, reader);
                }
            }
        }
    }
    
    return results;
}

ArrayList* element_reader_find_by_attribute(const ElementReader* root, const char* attr_name, const char* attr_value, Pool* pool) {
    if (!root || !attr_name || !pool) return NULL;
    
    ArrayList* results = arraylist_new(0);
    if (!results) return NULL;
    
    ElementIterator* iter = element_iterator_create(root, ITER_ELEMENTS_ONLY, pool);
    if (!iter) return results;
    
    while (element_iterator_has_next(iter)) {
        ElementReader* reader = element_iterator_next_element(iter);
        if (reader) {
            AttributeReader* attrs = element_reader_attributes(reader, pool);
            if (attrs) {
                if (attr_value) {
                    const char* value = attribute_reader_get_cstring(attrs, attr_name);
                    if (value && strcmp(value, attr_value) == 0) {
                        arraylist_append(results, reader);
                    }
                } else {
                    // Just check if attribute exists
                    if (attribute_reader_has(attrs, attr_name)) {
                        arraylist_append(results, reader);
                    }
                }
            }
        }
    }
    
    return results;
}

int64_t element_reader_count_elements(const ElementReader* root) {
    if (!root) return 0;
    
    int64_t count = 0;
    
    // Count this element
    count++;
    
    // Recursively count child elements
    for (int64_t i = 0; i < element_reader_child_count(root); i++) {
        Item child = element_reader_child_at(root, i);
        
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            ElementReader child_reader;
            child_reader.element = child.element;
            child_reader.element_type = (const TypeElmt*)child.element->type;
            if (child_reader.element_type) {
                child_reader.tag_name = child_reader.element_type->name.str;
                child_reader.tag_name_len = child_reader.element_type->name.length;
            }
            child_reader.child_count = ((const List*)child.element)->length;
            
            count += element_reader_count_elements(&child_reader);
        }
    }
    
    return count;
}

int64_t element_reader_tree_depth(const ElementReader* root) {
    if (!root) return 0;
    
    int64_t max_depth = 0;
    
    for (int64_t i = 0; i < element_reader_child_count(root); i++) {
        Item child = element_reader_child_at(root, i);
        
        if (get_type_id(child) == LMD_TYPE_ELEMENT) {
            ElementReader child_reader;
            child_reader.element = child.element;
            child_reader.element_type = (const TypeElmt*)child.element->type;
            if (child_reader.element_type) {
                child_reader.tag_name = child_reader.element_type->name.str;
                child_reader.tag_name_len = child_reader.element_type->name.length;
            }
            child_reader.child_count = ((const List*)child.element)->length;
            
            int64_t child_depth = element_reader_tree_depth(&child_reader);
            if (child_depth > max_depth) {
                max_depth = child_depth;
            }
        }
    }
    
    return max_depth + 1;
}

String* element_reader_debug_string(const ElementReader* root, Pool* pool) {
    if (!root || !pool) return NULL;
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return NULL;
    
    _debug_element_recursive(root, sb, 0);
    
    // Convert to String
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

// Helper function for debug output
static void _debug_element_recursive(const ElementReader* reader, StringBuf* sb, int depth) {
    if (!reader || !sb) return;
    
    // Add indentation
    for (int i = 0; i < depth * 2; i++) {
        stringbuf_append_char(sb, ' ');
    }
    
    // Add element tag
    stringbuf_append_char(sb, '<');
    if (reader->tag_name) {
        stringbuf_append_str_n(sb, reader->tag_name, reader->tag_name_len);
    } else {
        stringbuf_append_str(sb, "unknown");
    }
    stringbuf_append_char(sb, '>');
    stringbuf_append_char(sb, '\n');
    
    // Add children
    for (int64_t i = 0; i < element_reader_child_count(reader); i++) {
        Item child = element_reader_child_at(reader, i);
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_ELEMENT) {
            ElementReader child_reader;
            child_reader.element = child.element;
            child_reader.element_type = (const TypeElmt*)child.element->type;
            if (child_reader.element_type) {
                child_reader.tag_name = child_reader.element_type->name.str;
                child_reader.tag_name_len = child_reader.element_type->name.length;
            }
            child_reader.child_count = ((const List*)child.element)->length;
            
            _debug_element_recursive(&child_reader, sb, depth + 1);
        } else if (type == LMD_TYPE_STRING) {
            // Add indentation for text
            for (int j = 0; j < (depth + 1) * 2; j++) {
                stringbuf_append_char(sb, ' ');
            }
            stringbuf_append_str(sb, "\"");
            
            String* str = get_string(child);
            if (str && str->len > 0) {
                stringbuf_append_str_n(sb, str->chars, str->len);
            }
            stringbuf_append_str(sb, "\"\n");
        }
    }
}