#define _POSIX_C_SOURCE 200809L
#include "dom_element.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/strbuf.h"
#include <string.h>
#include <stdio.h>

// Forward declaration for strtok_r (POSIX function)
#ifndef strtok_r
extern char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

// ============================================================================
// Attribute Storage Implementation (Hybrid Array/HashMap)
// ============================================================================

/**
 * Hash function for attribute names (strings)
 */
static uint64_t attribute_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const AttributePair* pair = (const AttributePair*)item;
    return hashmap_murmur(pair->name, strlen(pair->name), seed0, seed1);
}

/**
 * Compare function for attribute names
 */
static int attribute_compare(const void* a, const void* b, void* udata) {
    const AttributePair* pair_a = (const AttributePair*)a;
    const AttributePair* pair_b = (const AttributePair*)b;
    return strcmp(pair_a->name, pair_b->name);
}

AttributeStorage* attribute_storage_create(Pool* pool) {
    if (!pool) {
        return NULL;
    }

    AttributeStorage* storage = (AttributeStorage*)pool_calloc(pool, sizeof(AttributeStorage));
    if (!storage) {
        return NULL;
    }

    storage->count = 0;
    storage->use_hashmap = false;
    storage->pool = pool;

    // Start with array storage
    storage->storage.array = (AttributePair*)pool_calloc(pool, ATTRIBUTE_HASHMAP_THRESHOLD * sizeof(AttributePair));
    if (!storage->storage.array) {
        return NULL;
    }

    return storage;
}

void attribute_storage_destroy(AttributeStorage* storage) {
    if (!storage) {
        return;
    }

    if (storage->use_hashmap && storage->storage.hashmap) {
        hashmap_free(storage->storage.hashmap);
    }
    // Array and storage struct will be freed by pool
}

/**
 * Pool allocator wrapper for HashMap
 */
static void* pool_malloc_wrapper(size_t size) {
    // Note: This is a limitation - HashMap needs malloc/free, not pool allocation
    return malloc(size);
}

static void* pool_realloc_wrapper(void* ptr, size_t size) {
    return realloc(ptr, size);
}

static void pool_free_wrapper(void* ptr) {
    free(ptr);
}

/**
 * Convert array storage to hashmap storage (called when threshold is reached)
 */
static bool attribute_storage_convert_to_hashmap(AttributeStorage* storage) {
    if (!storage || storage->use_hashmap) {
        return false;
    }

    // Create HashMap
    HashMap* map = hashmap_new_with_allocator(
        pool_malloc_wrapper,
        pool_realloc_wrapper,
        pool_free_wrapper,
        sizeof(AttributePair),
        16,  // initial capacity
        0, 0,  // seeds
        attribute_hash,
        attribute_compare,
        NULL,  // no free function
        NULL   // no udata
    );

    if (!map) {
        return false;
    }

    // Copy all attributes from array to hashmap
    for (int i = 0; i < storage->count; i++) {
        hashmap_set(map, &storage->storage.array[i]);
    }

    // Switch to hashmap storage
    storage->storage.hashmap = map;
    storage->use_hashmap = true;

    return true;
}

bool attribute_storage_set(AttributeStorage* storage, const char* name, const char* value) {
    if (!storage || !name || !value) {
        return false;
    }

    // Copy strings to pool
    size_t name_len = strlen(name);
    size_t value_len = strlen(value);
    char* name_copy = (char*)pool_alloc(storage->pool, name_len + 1);
    char* value_copy = (char*)pool_alloc(storage->pool, value_len + 1);

    if (!name_copy || !value_copy) {
        return false;
    }

    strcpy(name_copy, name);
    strcpy(value_copy, value);

    if (storage->use_hashmap) {
        // HashMap storage
        AttributePair pair = { name_copy, value_copy };

        // Check if attribute exists (for update)
        AttributePair search = { name, NULL };
        const AttributePair* existing = (const AttributePair*)hashmap_get(storage->storage.hashmap, &search);

        if (existing) {
            // Update existing attribute - need to remove and reinsert
            hashmap_delete(storage->storage.hashmap, &search);
            hashmap_set(storage->storage.hashmap, &pair);
            return true;
        } else {
            // Insert new attribute
            hashmap_set(storage->storage.hashmap, &pair);
            storage->count++;
            return true;
        }
    } else {
        // Array storage - linear search
        for (int i = 0; i < storage->count; i++) {
            if (strcmp(storage->storage.array[i].name, name) == 0) {
                // Update existing attribute
                storage->storage.array[i].value = value_copy;
                return true;
            }
        }

        // Add new attribute
        if (storage->count < ATTRIBUTE_HASHMAP_THRESHOLD) {
            storage->storage.array[storage->count].name = name_copy;
            storage->storage.array[storage->count].value = value_copy;
            storage->count++;
            return true;
        } else {
            // Convert to hashmap storage
            if (!attribute_storage_convert_to_hashmap(storage)) {
                return false;
            }

            // Now insert using hashmap
            AttributePair pair = { name_copy, value_copy };
            hashmap_set(storage->storage.hashmap, &pair);
            storage->count++;
            return true;
        }
    }
}

const char* attribute_storage_get(AttributeStorage* storage, const char* name) {
    if (!storage || !name) {
        return NULL;
    }

    if (storage->use_hashmap) {
        // HashMap storage
        AttributePair search = { name, NULL };
        const AttributePair* pair = (const AttributePair*)hashmap_get(storage->storage.hashmap, &search);
        return pair ? pair->value : NULL;
    } else {
        // Array storage - linear search
        for (int i = 0; i < storage->count; i++) {
            if (strcmp(storage->storage.array[i].name, name) == 0) {
                return storage->storage.array[i].value;
            }
        }
        return NULL;
    }
}

bool attribute_storage_has(AttributeStorage* storage, const char* name) {
    return attribute_storage_get(storage, name) != NULL;
}

bool attribute_storage_remove(AttributeStorage* storage, const char* name) {
    if (!storage || !name) {
        return false;
    }

    if (storage->use_hashmap) {
        // HashMap storage
        AttributePair search = { name, NULL };
        if (hashmap_delete(storage->storage.hashmap, &search)) {
            storage->count--;
            return true;
        }
        return false;
    } else {
        // Array storage - find and remove
        for (int i = 0; i < storage->count; i++) {
            if (strcmp(storage->storage.array[i].name, name) == 0) {
                // Shift remaining elements
                for (int j = i; j < storage->count - 1; j++) {
                    storage->storage.array[j] = storage->storage.array[j + 1];
                }
                storage->count--;
                return true;
            }
        }
        return false;
    }
}

/**
 * Iterator callback for get_names
 */
typedef struct {
    const char** names;
    int index;
} GetNamesContext;

static bool get_names_iter(const void* item, void* udata) {
    const AttributePair* pair = (const AttributePair*)item;
    GetNamesContext* ctx = (GetNamesContext*)udata;
    ctx->names[ctx->index++] = pair->name;
    return true;  // continue iteration
}

const char** attribute_storage_get_names(AttributeStorage* storage, int* count) {
    if (!storage || !count) {
        return NULL;
    }

    *count = storage->count;
    if (storage->count == 0) {
        return NULL;
    }

    const char** names = (const char**)pool_alloc(storage->pool, storage->count * sizeof(const char*));
    if (!names) {
        return NULL;
    }

    if (storage->use_hashmap) {
        // HashMap storage - iterate and collect names
        GetNamesContext ctx = { names, 0 };
        hashmap_scan(storage->storage.hashmap, get_names_iter, &ctx);
        return names;
    } else {
        // Array storage - copy names
        for (int i = 0; i < storage->count; i++) {
            names[i] = storage->storage.array[i].name;
        }
        return names;
    }
}

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

DomElement* dom_element_create(Pool* pool, const char* tag_name, void* native_element) {
    if (!pool || !tag_name) {
        return NULL;
    }

    DomElement* element = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (!element) {
        return NULL;
    }

    if (!dom_element_init(element, pool, tag_name, native_element)) {
        return NULL;
    }

    return element;
}

bool dom_element_init(DomElement* element, Pool* pool, const char* tag_name, void* native_element) {
    if (!element || !pool || !tag_name) {
        return false;
    }

    memset(element, 0, sizeof(DomElement));

    element->node_type = DOM_NODE_ELEMENT;
    element->pool = pool;
    element->native_element = native_element;

    // Copy tag name
    size_t tag_len = strlen(tag_name);
    char* tag_copy = (char*)pool_alloc(pool, tag_len + 1);
    if (!tag_copy) {
        return false;
    }
    strcpy(tag_copy, tag_name);
    element->tag_name = tag_copy;
    element->tag_name_ptr = (void*)tag_copy;  // Use string pointer as unique ID

    // Create style trees
    element->specified_style = style_tree_create(pool);
    if (!element->specified_style) {
        return false;
    }

    element->computed_style = style_tree_create(pool);
    if (!element->computed_style) {
        return false;
    }

    // Initialize version tracking
    element->style_version = 1;
    element->needs_style_recompute = true;

    // Initialize arrays
    element->class_names = NULL;
    element->class_count = 0;
    element->pseudo_state = 0;

    // Create attribute storage
    element->attributes = attribute_storage_create(pool);
    if (!element->attributes) {
        return false;
    }

    return true;
}

void dom_element_clear(DomElement* element) {
    if (!element) {
        return;
    }

    // Clear style trees
    if (element->specified_style) {
        style_tree_clear(element->specified_style);
    }
    if (element->computed_style) {
        style_tree_clear(element->computed_style);
    }

    // Clear attribute storage
    if (element->attributes) {
        attribute_storage_destroy(element->attributes);
        element->attributes = attribute_storage_create(element->pool);
    }

    // Reset version tracking
    element->style_version++;
    element->needs_style_recompute = true;

    // Note: We don't free memory here since it's pool-allocated
    // The pool will handle cleanup
}

void dom_element_destroy(DomElement* element) {
    if (!element) {
        return;
    }

    // Destroy style trees
    if (element->specified_style) {
        style_tree_destroy(element->specified_style);
    }
    if (element->computed_style) {
        style_tree_destroy(element->computed_style);
    }

    // Destroy attribute storage
    if (element->attributes) {
        attribute_storage_destroy(element->attributes);
    }

    // Note: The element structure itself is pool-allocated,
    // so it will be freed when the pool is destroyed
}

// ============================================================================
// Attribute Management
// ============================================================================

bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (!element || !name || !value || !element->attributes) {
        return false;
    }

    // Use AttributeStorage to set the attribute
    if (!attribute_storage_set(element->attributes, name, value)) {
        return false;
    }

    // Handle special attributes
    if (strcmp(name, "id") == 0) {
        // Copy value for cached ID
        size_t value_len = strlen(value);
        char* value_copy = (char*)pool_alloc(element->pool, value_len + 1);
        if (value_copy) {
            strcpy(value_copy, value);
            element->id = value_copy;
        }
    } else if (strcmp(name, "class") == 0) {
        // Parse class attribute and update class_names array
        dom_element_add_class(element, value);
    } else if (strcmp(name, "style") == 0) {
        // Parse and apply inline styles
        dom_element_apply_inline_style(element, value);
    }

    return true;
}

const char* dom_element_get_attribute(DomElement* element, const char* name) {
    if (!element || !name || name[0] == '\0' || !element->attributes) {
        return NULL;  // Empty attribute names never match
    }

    return attribute_storage_get(element->attributes, name);
}

bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (!element || !name || !element->attributes) {
        return false;
    }

    bool removed = attribute_storage_remove(element->attributes, name);

    if (removed) {
        // Handle special attributes
        if (strcmp(name, "id") == 0) {
            element->id = NULL;
        }
    }

    return removed;
}

bool dom_element_has_attribute(DomElement* element, const char* name) {
    return dom_element_get_attribute(element, name) != NULL;
}

// ============================================================================
// Class Management
// ============================================================================

bool dom_element_add_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    // Allow empty class names to be added (permissive), but they won't match later
    // Check if class already exists
    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            return true; // Already exists
        }
    }

    // Add new class
    int new_count = element->class_count + 1;
    const char** new_classes = (const char**)pool_alloc(element->pool,
                                                        new_count * sizeof(char*));
    if (!new_classes) {
        return false;
    }

    // Copy existing classes
    if (element->class_count > 0) {
        memcpy(new_classes, element->class_names, element->class_count * sizeof(char*));
    }

    // Add new class
    size_t class_len = strlen(class_name);
    char* class_copy = (char*)pool_alloc(element->pool, class_len + 1);
    if (!class_copy) {
        return false;
    }
    strcpy(class_copy, class_name);

    new_classes[element->class_count] = class_copy;
    element->class_names = new_classes;
    element->class_count = new_count;

    return true;
}

bool dom_element_remove_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            // Found the class - shift remaining classes down
            if (i < element->class_count - 1) {
                memmove((void*)&element->class_names[i],
                       (void*)&element->class_names[i + 1],
                       (element->class_count - i - 1) * sizeof(char*));
            }
            element->class_count--;
            return true;
        }
    }

    return false;
}

bool dom_element_has_class(DomElement* element, const char* class_name) {
    if (!element || !class_name || class_name[0] == '\0') {
        return false;  // Empty class names never match
    }

    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            return true;
        }
    }

    return false;
}

bool dom_element_toggle_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    if (dom_element_has_class(element, class_name)) {
        dom_element_remove_class(element, class_name);
        return false;
    } else {
        dom_element_add_class(element, class_name);
        return true;
    }
}

// ============================================================================
// Inline Style Support
// ============================================================================

/**
 * Parse and apply inline style attribute to an element
 * Format: "property: value; property: value;"
 * Inline styles have specificity (1,0,0,0) - highest non-!important specificity
 */
int dom_element_apply_inline_style(DomElement* element, const char* style_text) {
    if (!element || !style_text || !element->pool) {
        return 0;
    }

    int applied_count = 0;

    // Parse the style text - split by semicolons
    // Example: "color: red; font-size: 14px; background: blue"
    char* text_copy = (char*)pool_alloc(element->pool, strlen(style_text) + 1);
    if (!text_copy) {
        return 0;
    }
    strcpy(text_copy, style_text);

    char* saveptr = NULL;
    char* declaration_str = strtok_r(text_copy, ";", &saveptr);

    while (declaration_str) {
        // Trim leading whitespace
        while (*declaration_str == ' ' || *declaration_str == '\t' ||
               *declaration_str == '\n' || *declaration_str == '\r') {
            declaration_str++;
        }

        // Skip empty declarations
        if (*declaration_str == '\0') {
            declaration_str = strtok_r(NULL, ";", &saveptr);
            continue;
        }

        // Find the colon separator
        char* colon = strchr(declaration_str, ':');
        if (!colon) {
            declaration_str = strtok_r(NULL, ";", &saveptr);
            continue;
        }

        // Split into property name and value
        *colon = '\0';
        char* prop_name = declaration_str;
        char* prop_value = colon + 1;

        // Trim property name
        char* prop_end = colon - 1;
        while (prop_end >= prop_name && (*prop_end == ' ' || *prop_end == '\t')) {
            *prop_end = '\0';
            prop_end--;
        }

        // Trim property value
        while (*prop_value == ' ' || *prop_value == '\t') {
            prop_value++;
        }
        size_t value_len = strlen(prop_value);
        while (value_len > 0 && (prop_value[value_len - 1] == ' ' ||
                                 prop_value[value_len - 1] == '\t')) {
            prop_value[value_len - 1] = '\0';
            value_len--;
        }

        // Parse the property using css_parse_property
        CssDeclaration* decl = css_parse_property(prop_name, prop_value, element->pool);
        if (decl) {
            // Set inline style specificity (1,0,0,0)
            decl->specificity.inline_style = 1;
            decl->specificity.ids = 0;
            decl->specificity.classes = 0;
            decl->specificity.elements = 0;
            decl->specificity.important = false;

            // Apply to element
            if (dom_element_apply_declaration(element, decl)) {
                applied_count++;
            }
        }

        declaration_str = strtok_r(NULL, ";", &saveptr);
    }

    return applied_count;
}

/**
 * Get inline style text from an element
 * Returns the style attribute value or NULL if none
 */
const char* dom_element_get_inline_style(DomElement* element) {
    if (!element || !element->attributes) {
        return NULL;
    }

    return dom_element_get_attribute(element, "style");
}

/**
 * Remove inline styles from an element
 * Removes all declarations with inline_style specificity
 */
bool dom_element_remove_inline_styles(DomElement* element) {
    if (!element || !element->specified_style) {
        return false;
    }

    // Remove the style attribute
    dom_element_remove_attribute(element, "style");

    // Remove all inline style declarations from the style tree
    // This is a simplified implementation - ideally we'd track which
    // declarations came from inline styles
    // For now, we rely on the style attribute being the source of truth

    return true;
}

// ============================================================================
// Style Management
// ============================================================================

bool dom_element_apply_declaration(DomElement* element, CssDeclaration* declaration) {
    if (!element || !declaration) {
        return false;
    }

    // DEBUG: Log which element is receiving the declaration
    fprintf(stderr, "[APPLY_DECL] Element <%s> receiving property %d (spec:%u, order:%d)\n",
            element->tag_name ? element->tag_name : "null",
            declaration->property_id,
            css_specificity_to_value(declaration->specificity),
            declaration->source_order);

    // Apply to specified style tree
    StyleNode* node = style_tree_apply_declaration(element->specified_style, declaration);
    if (!node) {
        return false;
    }

    // Increment style version to invalidate caches
    element->style_version++;
    element->needs_style_recompute = true;

    return true;
}

int dom_element_apply_rule(DomElement* element, CssRule* rule, CssSpecificity specificity) {
    if (!element || !rule) {
        return 0;
    }

    int applied_count = 0;

    // Apply each declaration from the rule
    if (rule->type == CSS_RULE_STYLE && rule->data.style_rule.declarations) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl) {
                // Update declaration's specificity to match the selector
                decl->specificity = specificity;
                decl->origin = rule->origin;

                if (dom_element_apply_declaration(element, decl)) {
                    applied_count++;
                }
            }
        }
    }

    return applied_count;
}

CssDeclaration* dom_element_get_specified_value(DomElement* element, CssPropertyId property_id) {
    if (!element || !element->specified_style) {
        return NULL;
    }

    return style_tree_get_declaration(element->specified_style, property_id);
}

bool dom_element_remove_property(DomElement* element, CssPropertyId property_id) {
    if (!element || !element->specified_style) {
        return false;
    }

    bool removed = style_tree_remove_property(element->specified_style, property_id);

    if (removed) {
        element->style_version++;
        element->needs_style_recompute = true;
    }

    return removed;
}

// ============================================================================
// Pseudo-Class State Management
// ============================================================================

void dom_element_set_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return;
    }

    element->pseudo_state |= pseudo_state;
}

void dom_element_clear_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return;
    }

    element->pseudo_state &= ~pseudo_state;
}

bool dom_element_has_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return false;
    }

    return (element->pseudo_state & pseudo_state) != 0;
}

bool dom_element_toggle_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return false;
    }

    if (dom_element_has_pseudo_state(element, pseudo_state)) {
        dom_element_clear_pseudo_state(element, pseudo_state);
        return false;
    } else {
        dom_element_set_pseudo_state(element, pseudo_state);
        return true;
    }
}

// ============================================================================
// DOM Tree Navigation
// ============================================================================

DomElement* dom_element_get_parent(DomElement* element) {
    return element ? element->parent : NULL;
}

DomElement* dom_element_get_first_child(DomElement* element) {
    return element ? element->first_child : NULL;
}

DomElement* dom_element_get_next_sibling(DomElement* element) {
    return element ? element->next_sibling : NULL;
}

DomElement* dom_element_get_prev_sibling(DomElement* element) {
    return element ? element->prev_sibling : NULL;
}

bool dom_element_append_child(DomElement* parent, DomElement* child) {
    if (!parent || !child) {
        return false;
    }

    // Set parent relationship
    child->parent = parent;

    // Add to parent's child list
    if (!parent->first_child) {
        // First child
        parent->first_child = child;
        child->prev_sibling = NULL;
        child->next_sibling = NULL;
    } else {
        // Find last child - need to handle mixed node types (DomElement, DomText, etc.)
        void* last_child_node = parent->first_child;

        while (last_child_node) {
            void* next_sibling = NULL;

            // Get next_sibling based on the actual node type
            DomNodeType node_type = dom_node_get_type(last_child_node);
            if (node_type == DOM_NODE_ELEMENT) {
                next_sibling = ((DomElement*)last_child_node)->next_sibling;
            } else if (node_type == DOM_NODE_TEXT) {
                next_sibling = ((DomText*)last_child_node)->next_sibling;
            } else if (node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE) {
                next_sibling = ((DomComment*)last_child_node)->next_sibling;
            }

            if (!next_sibling) {
                // This is the last child, append the new element after it
                if (node_type == DOM_NODE_ELEMENT) {
                    ((DomElement*)last_child_node)->next_sibling = child;
                } else if (node_type == DOM_NODE_TEXT) {
                    ((DomText*)last_child_node)->next_sibling = child;
                } else if (node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE) {
                    ((DomComment*)last_child_node)->next_sibling = child;
                }
                child->prev_sibling = last_child_node;
                child->next_sibling = NULL;
                break;
            }

            last_child_node = next_sibling;
        }
    }

    return true;
}

bool dom_element_remove_child(DomElement* parent, DomElement* child) {
    if (!parent || !child || child->parent != parent) {
        return false;
    }

    // Update sibling links - need to handle void* pointers properly
    if (child->prev_sibling) {
        // Get the type of previous sibling to access its next_sibling correctly
        DomNodeType prev_type = dom_node_get_type(child->prev_sibling);
        if (prev_type == DOM_NODE_ELEMENT) {
            ((DomElement*)child->prev_sibling)->next_sibling = child->next_sibling;
        } else if (prev_type == DOM_NODE_TEXT) {
            ((DomText*)child->prev_sibling)->next_sibling = child->next_sibling;
        } else if (prev_type == DOM_NODE_COMMENT || prev_type == DOM_NODE_DOCTYPE) {
            ((DomComment*)child->prev_sibling)->next_sibling = child->next_sibling;
        }
    } else {
        // Child was first child
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        // Get the type of next sibling to access its prev_sibling correctly
        DomNodeType next_type = dom_node_get_type(child->next_sibling);
        if (next_type == DOM_NODE_ELEMENT) {
            ((DomElement*)child->next_sibling)->prev_sibling = child->prev_sibling;
        } else if (next_type == DOM_NODE_TEXT) {
            ((DomText*)child->next_sibling)->prev_sibling = child->prev_sibling;
        } else if (next_type == DOM_NODE_COMMENT || next_type == DOM_NODE_DOCTYPE) {
            ((DomComment*)child->next_sibling)->prev_sibling = child->prev_sibling;
        }
    }

    // Clear child's parent relationship
    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    return true;
}

bool dom_element_insert_before(DomElement* parent, DomElement* new_child, DomElement* reference_child) {
    if (!parent || !new_child) {
        return false;
    }

    // If no reference child, append at end
    if (!reference_child) {
        return dom_element_append_child(parent, new_child);
    }

    // Verify reference child is actually a child of parent
    if (reference_child->parent != parent) {
        return false;
    }

    // Set parent relationship
    new_child->parent = parent;

    // Insert before reference child
    new_child->next_sibling = reference_child;
    new_child->prev_sibling = reference_child->prev_sibling;

    if (reference_child->prev_sibling) {
        // Get the type of previous sibling to access its next_sibling correctly
        DomNodeType prev_type = dom_node_get_type(reference_child->prev_sibling);
        if (prev_type == DOM_NODE_ELEMENT) {
            ((DomElement*)reference_child->prev_sibling)->next_sibling = new_child;
        } else if (prev_type == DOM_NODE_TEXT) {
            ((DomText*)reference_child->prev_sibling)->next_sibling = new_child;
        } else if (prev_type == DOM_NODE_COMMENT || prev_type == DOM_NODE_DOCTYPE) {
            ((DomComment*)reference_child->prev_sibling)->next_sibling = new_child;
        }
    } else {
        // Reference child was first child
        parent->first_child = new_child;
    }

    reference_child->prev_sibling = new_child;

    // Invalidate new child's computed values
    // dom_element_invalidate_computed_values(new_child, true);

    return true;
}

// ============================================================================
// Structural Queries
// ============================================================================

bool dom_element_is_first_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    return element->parent->first_child == element;
}

bool dom_element_is_last_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    return element->next_sibling == NULL;
}

bool dom_element_is_only_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    return element->parent->first_child == element && element->next_sibling == NULL;
}

int dom_element_get_child_index(DomElement* element) {
    if (!element || !element->parent) {
        return -1;
    }

    int index = 0;
    DomElement* sibling = element->parent->first_child;

    while (sibling && sibling != element) {
        index++;
        sibling = sibling->next_sibling;
    }

    return sibling == element ? index : -1;
}

int dom_element_count_children(DomElement* element) {
    if (!element) {
        return 0;
    }

    int count = 0;
    DomElement* child = element->first_child;

    while (child) {
        count++;
        child = child->next_sibling;
    }

    return count;
}

bool dom_element_matches_nth_child(DomElement* element, int a, int b) {
    if (!element) {
        return false;
    }

    int index = dom_element_get_child_index(element);
    if (index < 0) {
        return false;
    }

    // nth-child is 1-based
    int n = index + 1;

    // Check if n matches an+b for some non-negative integer
    if (a == 0) {
        return n == b;
    }

    int diff = n - b;
    if (diff < 0) {
        return false;
    }

    return (diff % a) == 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

void dom_element_print_info(DomElement* element) {
    if (!element) {
        printf("DOM Element: NULL\n");
        return;
    }

    printf("DOM Element: <%s", element->tag_name);

    if (element->id) {
        printf(" id=\"%s\"", element->id);
    }

    if (element->class_count > 0) {
        printf(" class=\"");
        for (int i = 0; i < element->class_count; i++) {
            printf("%s%s", i > 0 ? " " : "", element->class_names[i]);
        }
        printf("\"");
    }

    printf(">\n");
    printf("  Style version: %u\n", element->style_version);
    printf("  Needs recompute: %s\n", element->needs_style_recompute ? "yes" : "no");
    printf("  Pseudo-state: 0x%08X\n", element->pseudo_state);
    printf("  Children: %d\n", dom_element_count_children(element));
}

void dom_element_print_styles(DomElement* element) {
    if (!element) {
        printf("DOM Element: NULL\n");
        return;
    }

    printf("Specified styles for <%s>:\n", element->tag_name);
    if (element->specified_style) {
        style_tree_print(element->specified_style);
    } else {
        printf("  (none)\n");
    }

    printf("\nComputed styles for <%s>:\n", element->tag_name);
    if (element->computed_style) {
        style_tree_print(element->computed_style);
    } else {
        printf("  (none)\n");
    }
}

void dom_element_get_style_stats(DomElement* element,
                                 int* specified_count,
                                 int* computed_count,
                                 int* total_declarations) {
    if (!element) {
        if (specified_count) *specified_count = 0;
        if (computed_count) *computed_count = 0;
        if (total_declarations) *total_declarations = 0;
        return;
    }

    int total_nodes = 0;
    int total_decls = 0;
    double avg_weak = 0.0;

    if (element->specified_style) {
        style_tree_get_statistics(element->specified_style, &total_nodes, &total_decls, &avg_weak);
        if (specified_count) *specified_count = total_nodes;
        if (total_declarations) *total_declarations = total_decls;
    }

    if (element->computed_style && computed_count) {
        style_tree_get_statistics(element->computed_style, &total_nodes, &total_decls, &avg_weak);
        *computed_count = total_nodes;
    }
}

DomElement* dom_element_clone(DomElement* source, Pool* pool) {
    if (!source || !pool) {
        return NULL;
    }

    // Create new element with same tag name
    DomElement* clone = dom_element_create(pool, source->tag_name, NULL);
    if (!clone) {
        return NULL;
    }

    // Copy attributes
    if (source->attributes) {
        int attr_count = 0;
        const char** attr_names = attribute_storage_get_names(source->attributes, &attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* value = attribute_storage_get(source->attributes, attr_names[i]);
                if (value) {
                    attribute_storage_set(clone->attributes, attr_names[i], value);
                }
            }
        }
    }

    // Copy classes
    for (int i = 0; i < source->class_count; i++) {
        dom_element_add_class(clone, source->class_names[i]);
    }

    // Deep copy style trees using style_tree_clone
    if (source->specified_style) {
        clone->specified_style = style_tree_clone(source->specified_style, pool);
    }

    if (source->computed_style) {
        clone->computed_style = style_tree_clone(source->computed_style, pool);
    }

    // Note: inline_style field may not exist in all DomElement versions
    // Skip inline_style copy if the field doesn't exist

    // Copy pseudo state
    clone->pseudo_state = source->pseudo_state;

    // Note: Children are not cloned - caller should handle that if needed

    return clone;
}// ============================================================================
// DOM Text Node Implementation
// ============================================================================

DomText* dom_text_create(Pool* pool, const char* text) {
    if (!pool || !text) {
        return NULL;
    }

    DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    if (!text_node) {
        return NULL;
    }

    text_node->node_type = DOM_NODE_TEXT;
    text_node->length = strlen(text);

    // Copy text to pool
    char* text_copy = (char*)pool_alloc(pool, text_node->length + 1);
    if (!text_copy) {
        return NULL;
    }
    strcpy(text_copy, text);
    text_node->text = text_copy;

    text_node->parent = NULL;
    text_node->next_sibling = NULL;
    text_node->prev_sibling = NULL;
    text_node->pool = pool;

    return text_node;
}

void dom_text_destroy(DomText* text_node) {
    if (!text_node) {
        return;
    }
    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

const char* dom_text_get_content(DomText* text_node) {
    return text_node ? text_node->text : NULL;
}

bool dom_text_set_content(DomText* text_node, const char* text) {
    if (!text_node || !text) {
        return false;
    }

    size_t new_length = strlen(text);
    char* text_copy = (char*)pool_alloc(text_node->pool, new_length + 1);
    if (!text_copy) {
        return false;
    }

    strcpy(text_copy, text);
    text_node->text = text_copy;
    text_node->length = new_length;

    return true;
}

// ============================================================================
// DOM Comment/DOCTYPE Node Implementation
// ============================================================================

DomComment* dom_comment_create(Pool* pool, DomNodeType node_type, const char* tag_name, const char* content) {
    if (!pool || !tag_name) {
        return NULL;
    }

    DomComment* comment_node = (DomComment*)pool_calloc(pool, sizeof(DomComment));
    if (!comment_node) {
        return NULL;
    }

    comment_node->node_type = node_type;

    // Copy tag name to pool
    size_t tag_len = strlen(tag_name);
    char* tag_copy = (char*)pool_alloc(pool, tag_len + 1);
    if (!tag_copy) {
        return NULL;
    }
    strcpy(tag_copy, tag_name);
    comment_node->tag_name = tag_copy;

    // Copy content to pool if provided
    if (content) {
        comment_node->length = strlen(content);
        char* content_copy = (char*)pool_alloc(pool, comment_node->length + 1);
        if (!content_copy) {
            return NULL;
        }
        strcpy(content_copy, content);
        comment_node->content = content_copy;
    } else {
        comment_node->content = "";
        comment_node->length = 0;
    }

    comment_node->parent = NULL;
    comment_node->next_sibling = NULL;
    comment_node->prev_sibling = NULL;
    comment_node->pool = pool;

    return comment_node;
}

void dom_comment_destroy(DomComment* comment_node) {
    if (!comment_node) {
        return;
    }
    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

const char* dom_comment_get_content(DomComment* comment_node) {
    return comment_node ? comment_node->content : NULL;
}

// ============================================================================
// DOM Element Print Implementation
// ============================================================================

/**
 * Print a DOM element and its children to a string buffer
 * Outputs the element in a tree-like format with proper indentation
 */
void dom_element_print(DomElement* element, StrBuf* buf, int indent) {
    if (!element || !buf) {
        log_debug("dom_element_print: Invalid arguments");
        return;
    }
    log_debug("dom_element_print: element <%s>", element->tag_name ? element->tag_name : "#null");

    // Add indentation
    strbuf_append_char_n(buf, ' ', indent);

    // Print opening tag
    strbuf_append_char(buf, '<');
    strbuf_append_str(buf, element->tag_name ? element->tag_name : "unknown");

    // Print ID attribute if present
    // if (element->id && element->id[0] != '\0') {
    //     strbuf_append_str(buf, "id=\"");
    //     strbuf_append_str(buf, element->id);
    //     strbuf_append_char(buf, '"');
    // }

    // // Print class attributes if present
    // if (element->class_count > 0 && element->class_names) {
    //     strbuf_append_str(buf, " class=\"");
    //     for (int i = 0; i < element->class_count; i++) {
    //         if (i > 0) {
    //             strbuf_append_char(buf, ' ');
    //         }
    //         strbuf_append_str(buf, element->class_names[i]);
    //     }
    //     strbuf_append_char(buf, '"');
    // }

    // Print other attributes
    if (element->attributes) {
        int attr_count = 0;
        const char** attr_names = attribute_storage_get_names(element->attributes, &attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* name = attr_names[i];
                const char* value = attribute_storage_get(element->attributes, name);

                // Skip id and class as they're already printed
                if (strcmp(name, "id") != 0 && strcmp(name, "class") != 0 && value) {
                    strbuf_append_char(buf, ' ');
                    strbuf_append_str(buf, name);
                    strbuf_append_str(buf, "=\"");
                    strbuf_append_str(buf, value);
                    strbuf_append_char(buf, '"');
                }
            }
        }
    }

    // Print pseudo-state information if any
    if (element->pseudo_state != 0) {
        strbuf_append_str(buf, " [pseudo:");
        if (element->pseudo_state & PSEUDO_STATE_HOVER) strbuf_append_str(buf, " hover");
        if (element->pseudo_state & PSEUDO_STATE_ACTIVE) strbuf_append_str(buf, " active");
        if (element->pseudo_state & PSEUDO_STATE_FOCUS) strbuf_append_str(buf, " focus");
        if (element->pseudo_state & PSEUDO_STATE_VISITED) strbuf_append_str(buf, " visited");
        if (element->pseudo_state & PSEUDO_STATE_CHECKED) strbuf_append_str(buf, " checked");
        if (element->pseudo_state & PSEUDO_STATE_DISABLED) strbuf_append_str(buf, " disabled");
        strbuf_append_char(buf, ']');
    }

    strbuf_append_char(buf, '>');

    // Print specified CSS styles if present
    if (element->id || element->class_count > 0 || element->specified_style) {
        strbuf_append_str(buf, " [");
        // print id
        if (element->id && element->id[0] != '\0') strbuf_append_format(buf, "id:'%s'", element->id);

        // print classes
        if (element->class_count > 0 && element->class_names) {
            strbuf_append_str(buf, " classes:");
            strbuf_append_char(buf, '[');
            for (int i = 0; i < element->class_count; i++) {
                strbuf_append_format(buf, "\"%s\"", element->class_names[i]);
                if (i < element->class_count - 1) strbuf_append_char(buf, ',');
            }
            strbuf_append_char(buf, ']');
        }

        strbuf_append_str(buf, " styles:{");
        CssDeclaration* decl;  bool has_props = false;

        // Display property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_DISPLAY);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_KEYWORD && val->data.keyword) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, " display:");
                strbuf_append_str(buf, val->data.keyword);
                has_props = true;
            }
        }

        // Width property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_WIDTH);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, " width:");
                char width_str[32];
                snprintf(width_str, sizeof(width_str), "%.2fpx", val->data.length.value);
                strbuf_append_str(buf, width_str);
                has_props = true;
            }
        }

        // Height property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_HEIGHT);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "height:");
                char height_str[32];
                snprintf(height_str, sizeof(height_str), "%.2fpx", val->data.length.value);
                strbuf_append_str(buf, height_str);
                has_props = true;
            }
        }

        // Margin property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH || val->type == CSS_VALUE_NUMBER || val->type == CSS_VALUE_INTEGER) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "margin:");
                char margin_str[32];
                snprintf(margin_str, sizeof(margin_str), "%.2fpx",
                    val->type == CSS_VALUE_LENGTH ? val->data.length.value : val->type == CSS_VALUE_NUMBER ? val->data.number.value : val->data.integer.value);
                strbuf_append_str(buf, margin_str);
                has_props = true;
            }
        }

        // Padding property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH || val->type == CSS_VALUE_NUMBER || val->type == CSS_VALUE_INTEGER) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "padding:");
                char padding_str[32];
                snprintf(padding_str, sizeof(padding_str), "%.2fpx",
                    val->type == CSS_VALUE_LENGTH ? val->data.length.value : val->type == CSS_VALUE_NUMBER ? val->data.number.value : val->data.integer.value);
                strbuf_append_str(buf, padding_str);
                has_props = true;
            }
        }

        // Font size property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "font-size:");
                char size_str[32];
                snprintf(size_str, sizeof(size_str), "%.2fpx", val->data.length.value);
                strbuf_append_str(buf, size_str);
                has_props = true;
            }
        }

        // Font family property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_FONT_FAMILY);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if ((val->type == CSS_VALUE_STRING || val->type == CSS_VALUE_KEYWORD) &&
                (val->data.string || val->data.keyword)) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "font-family:");
                strbuf_append_str(buf, val->data.string ? val->data.string : val->data.keyword);
                has_props = true;
            }
            else if (val->type == CSS_VALUE_LIST && val->data.list.count > 0) {
                // For lists, concatenate the family names
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "font-family:[");
                for (size_t i = 0; i < val->data.list.count; i++) {
                    CssValue* item = val->data.list.values[i];
                    if (item) {
                        if (i > 0) strbuf_append_str(buf, ", ");
                        if (item->type == CSS_VALUE_STRING && item->data.string) {
                            strbuf_append_str(buf, item->data.string);
                        } else if (item->type == CSS_VALUE_KEYWORD && item->data.keyword) {
                            strbuf_append_str(buf, item->data.keyword);
                        }
                    }
                }
                strbuf_append_str(buf, "]");
            }
        }

        // Font weight property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_FONT_WEIGHT);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_INTEGER) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "font-weight:");
                char weight_str[16];
                snprintf(weight_str, sizeof(weight_str), "%d", val->data.integer.value);
                strbuf_append_str(buf, weight_str);
                has_props = true;
            }
            else if (val->type == CSS_VALUE_KEYWORD) {
                if (has_props) strbuf_append_str(buf, ", ");
                // Keywords: normal (400), bold (700), etc.
                strbuf_append_format(buf, "font-weight: %s", val->data.keyword);
                has_props = true;
            }
        }

        // Color property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_COLOR);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_COLOR) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "color:");
                char color_str[64];
                snprintf(color_str, sizeof(color_str), "rgba(%d,%d,%d,%.2f)",
                    val->data.color.data.rgba.r,
                    val->data.color.data.rgba.g,
                    val->data.color.data.rgba.b,
                    val->data.color.data.rgba.a / 255.0);
                strbuf_append_str(buf, color_str);
                has_props = true;
            }
        }

        // Background color property
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BACKGROUND_COLOR);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_COLOR) {
                if (has_props) strbuf_append_str(buf, ", ");
                strbuf_append_str(buf, "background-color:");
                char color_str[64];
                snprintf(color_str, sizeof(color_str), "rgba(%d,%d,%d,%.2f)",
                    val->data.color.data.rgba.r,
                    val->data.color.data.rgba.g,
                    val->data.color.data.rgba.b,
                    val->data.color.data.rgba.a / 255.0);
                strbuf_append_str(buf, color_str);
                has_props = true;
            }
        }

        // Line height property (inherited)
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_LINE_HEIGHT);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_LENGTH && val->data.length.value > 0) {
                strbuf_append_format(buf, ", line-height: %.2f", val->data.length.value);
                has_props = true;
            } else if (val->type == CSS_VALUE_NUMBER && val->data.number.value > 0) {
                strbuf_append_format(buf, ", line-height: %.2f", val->data.number.value);
                has_props = true;
            }
        }

        // Text align property (ID 89)
        decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_TEXT_ALIGN);
        if (decl && decl->value) {
            CssValue* val = (CssValue*)decl->value;
            if (val->type == CSS_VALUE_KEYWORD) {
                if (has_props) strbuf_append_str(buf, ", ");
                // TODO: Extract actual keyword (start, end, left, right, center, justify)
                strbuf_append_format(buf, "text-align: %s", val->data.keyword);
                has_props = true;
            }
        }

        strbuf_append_str(buf, "}]");
    }

    // Check if element has children
    bool has_children = false;
    void* child = element->first_child;

    // Count and print children
    while (child) {
        if (!has_children) {
            strbuf_append_char(buf, '\n');
            has_children = true;
        }

        DomNodeType child_type = dom_node_get_type(child);

        if (child_type == DOM_NODE_ELEMENT) {
            // Recursively print child elements
            dom_element_print((DomElement*)child, buf, indent + 2);
            child = ((DomElement*)child)->next_sibling;
        } else if (child_type == DOM_NODE_TEXT) {
            // Print text nodes (skip whitespace-only text nodes)
            DomText* text_node = (DomText*)child;

            if (text_node->text && text_node->length > 0) {
                // Check if text node contains only whitespace
                bool is_whitespace_only = true;
                for (size_t i = 0; i < text_node->length; i++) {
                    char c = text_node->text[i];
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                        is_whitespace_only = false;
                        break;
                    }
                }

                if (!is_whitespace_only) {
                    strbuf_append_char_n(buf, ' ', indent + 2);
                    strbuf_append_str(buf, "\"");
                    strbuf_append_str_n(buf, text_node->text, text_node->length);
                    strbuf_append_str(buf, "\"\n");
                }
            }
            child = text_node->next_sibling;
        } else if (child_type == DOM_NODE_COMMENT || child_type == DOM_NODE_DOCTYPE) {
            // Print comment/DOCTYPE nodes
            DomComment* comment_node = (DomComment*)child;
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "<!-- ");
            if (comment_node->content) {
                strbuf_append_str(buf, comment_node->content);
            }
            strbuf_append_str(buf, " -->\n");
            child = comment_node->next_sibling;
        } else {
            // Unknown node type, skip
            break;
        }
    }

    // Print closing tag
    if (has_children) {
        strbuf_append_char_n(buf, ' ', indent);
    }
    strbuf_append_str(buf, "</");
    // strbuf_append_str(buf, element->tag_name ? element->tag_name : "unknown");
    strbuf_append_str(buf, ">");
}
