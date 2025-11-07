#define _POSIX_C_SOURCE 200809L
#include "dom_element.hpp"
#include "css_formatter.hpp"
#include "css_style_node.hpp"
#include "../../../radiant/view.hpp"  // For HTM_TAG_* constants
#include "../../../lib/hashmap.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/stringbuf.h"
#include "../../../lib/string.h"
#include "../../../lib/log.h"
#include "../../../lib/strview.h"
#include "../../lambda.h"
#include "../../lambda-data.hpp"  // For get_type_id, elmt_get_typed, and proper type definitions
#include <string.h>
#include <stdio.h>
#include <new>  // For placement new

extern "C" {
    int strcasecmp(const char *s1, const char *s2);
}

// Forward declaration for strtok_r (POSIX function)
#ifndef strtok_r
extern char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

// ============================================================================
// Tag Name to ID Conversion
// ============================================================================

/**
 * Convert HTML tag name string to Lexbor tag ID
 * This is called once during element creation to populate the tag_id field
 */
uintptr_t DomNode::tag_name_to_id(const char* tag_name) {
    if (!tag_name) return 0;

    // Use case-insensitive comparison for HTML tags
    // Map common HTML tags to their Lexbor constants
    if (strcasecmp(tag_name, "img") == 0) return HTM_TAG_IMG;
    if (strcasecmp(tag_name, "div") == 0) return HTM_TAG_DIV;
    if (strcasecmp(tag_name, "span") == 0) return HTM_TAG_SPAN;
    if (strcasecmp(tag_name, "p") == 0) return HTM_TAG_P;
    if (strcasecmp(tag_name, "h1") == 0) return HTM_TAG_H1;
    if (strcasecmp(tag_name, "h2") == 0) return HTM_TAG_H2;
    if (strcasecmp(tag_name, "h3") == 0) return HTM_TAG_H3;
    if (strcasecmp(tag_name, "h4") == 0) return HTM_TAG_H4;
    if (strcasecmp(tag_name, "h5") == 0) return HTM_TAG_H5;
    if (strcasecmp(tag_name, "h6") == 0) return HTM_TAG_H6;
    if (strcasecmp(tag_name, "a") == 0) return HTM_TAG_A;
    if (strcasecmp(tag_name, "body") == 0) return HTM_TAG_BODY;
    if (strcasecmp(tag_name, "head") == 0) return HTM_TAG_HEAD;
    if (strcasecmp(tag_name, "html") == 0) return HTM_TAG_HTML;
    if (strcasecmp(tag_name, "title") == 0) return HTM_TAG_TITLE;
    if (strcasecmp(tag_name, "meta") == 0) return HTM_TAG_META;
    if (strcasecmp(tag_name, "link") == 0) return HTM_TAG_LINK;
    if (strcasecmp(tag_name, "style") == 0) return HTM_TAG_STYLE;
    if (strcasecmp(tag_name, "script") == 0) return HTM_TAG_SCRIPT;
    if (strcasecmp(tag_name, "br") == 0) return HTM_TAG_BR;
    if (strcasecmp(tag_name, "hr") == 0) return HTM_TAG_HR;
    if (strcasecmp(tag_name, "ul") == 0) return HTM_TAG_UL;
    if (strcasecmp(tag_name, "ol") == 0) return HTM_TAG_OL;
    if (strcasecmp(tag_name, "li") == 0) return HTM_TAG_LI;
    if (strcasecmp(tag_name, "table") == 0) return HTM_TAG_TABLE;
    if (strcasecmp(tag_name, "tr") == 0) return HTM_TAG_TR;
    if (strcasecmp(tag_name, "td") == 0) return HTM_TAG_TD;
    if (strcasecmp(tag_name, "th") == 0) return HTM_TAG_TH;
    if (strcasecmp(tag_name, "thead") == 0) return HTM_TAG_THEAD;
    if (strcasecmp(tag_name, "tbody") == 0) return HTM_TAG_TBODY;
    if (strcasecmp(tag_name, "tfoot") == 0) return HTM_TAG_TFOOT;
    if (strcasecmp(tag_name, "form") == 0) return HTM_TAG_FORM;
    if (strcasecmp(tag_name, "input") == 0) return HTM_TAG_INPUT;
    if (strcasecmp(tag_name, "button") == 0) return HTM_TAG_BUTTON;
    if (strcasecmp(tag_name, "select") == 0) return HTM_TAG_SELECT;
    if (strcasecmp(tag_name, "option") == 0) return HTM_TAG_OPTION;
    if (strcasecmp(tag_name, "textarea") == 0) return HTM_TAG_TEXTAREA;
    if (strcasecmp(tag_name, "label") == 0) return HTM_TAG_LABEL;
    if (strcasecmp(tag_name, "fieldset") == 0) return HTM_TAG_FIELDSET;
    if (strcasecmp(tag_name, "legend") == 0) return HTM_TAG_LEGEND;
    if (strcasecmp(tag_name, "iframe") == 0) return HTM_TAG_IFRAME;
    if (strcasecmp(tag_name, "embed") == 0) return HTM_TAG_EMBED;
    if (strcasecmp(tag_name, "object") == 0) return HTM_TAG_OBJECT;
    if (strcasecmp(tag_name, "param") == 0) return HTM_TAG_PARAM;
    if (strcasecmp(tag_name, "video") == 0) return HTM_TAG_VIDEO;
    if (strcasecmp(tag_name, "audio") == 0) return HTM_TAG_AUDIO;
    if (strcasecmp(tag_name, "source") == 0) return HTM_TAG_SOURCE;
    if (strcasecmp(tag_name, "track") == 0) return HTM_TAG_TRACK;
    if (strcasecmp(tag_name, "canvas") == 0) return HTM_TAG_CANVAS;
    if (strcasecmp(tag_name, "svg") == 0) return HTM_TAG_SVG;
    if (strcasecmp(tag_name, "lineargradient") == 0) return HTM_TAG_LINEARGRADIENT;
    if (strcasecmp(tag_name, "radialgradient") == 0) return HTM_TAG_RADIALGRADIENT;
    if (strcasecmp(tag_name, "animatetransform") == 0) return HTM_TAG_ANIMATETRANSFORM;
    if (strcasecmp(tag_name, "animatemotion") == 0) return HTM_TAG_ANIMATEMOTION;
    if (strcasecmp(tag_name, "strong") == 0) return HTM_TAG_STRONG;
    if (strcasecmp(tag_name, "em") == 0) return HTM_TAG_EM;
    if (strcasecmp(tag_name, "b") == 0) return HTM_TAG_B;
    if (strcasecmp(tag_name, "i") == 0) return HTM_TAG_I;
    if (strcasecmp(tag_name, "u") == 0) return HTM_TAG_U;
    if (strcasecmp(tag_name, "s") == 0) return HTM_TAG_S;
    if (strcasecmp(tag_name, "small") == 0) return HTM_TAG_SMALL;
    if (strcasecmp(tag_name, "mark") == 0) return HTM_TAG_MARK;
    if (strcasecmp(tag_name, "del") == 0) return HTM_TAG_DEL;
    if (strcasecmp(tag_name, "ins") == 0) return HTM_TAG_INS;
    if (strcasecmp(tag_name, "sub") == 0) return HTM_TAG_SUB;
    if (strcasecmp(tag_name, "sup") == 0) return HTM_TAG_SUP;
    if (strcasecmp(tag_name, "q") == 0) return HTM_TAG_Q;
    if (strcasecmp(tag_name, "cite") == 0) return HTM_TAG_CITE;
    if (strcasecmp(tag_name, "abbr") == 0) return HTM_TAG_ABBR;
    if (strcasecmp(tag_name, "dfn") == 0) return HTM_TAG_DFN;
    if (strcasecmp(tag_name, "time") == 0) return HTM_TAG_TIME;
    if (strcasecmp(tag_name, "code") == 0) return HTM_TAG_CODE;
    if (strcasecmp(tag_name, "var") == 0) return HTM_TAG_VAR;
    if (strcasecmp(tag_name, "samp") == 0) return HTM_TAG_SAMP;
    if (strcasecmp(tag_name, "kbd") == 0) return HTM_TAG_KBD;
    if (strcasecmp(tag_name, "address") == 0) return HTM_TAG_ADDRESS;
    if (strcasecmp(tag_name, "main") == 0) return HTM_TAG_MAIN;
    if (strcasecmp(tag_name, "section") == 0) return HTM_TAG_SECTION;
    if (strcasecmp(tag_name, "article") == 0) return HTM_TAG_ARTICLE;
    if (strcasecmp(tag_name, "aside") == 0) return HTM_TAG_ASIDE;
    if (strcasecmp(tag_name, "nav") == 0) return HTM_TAG_NAV;
    if (strcasecmp(tag_name, "header") == 0) return HTM_TAG_HEADER;
    if (strcasecmp(tag_name, "footer") == 0) return HTM_TAG_FOOTER;
    if (strcasecmp(tag_name, "hgroup") == 0) return HTM_TAG_HGROUP;
    if (strcasecmp(tag_name, "figure") == 0) return HTM_TAG_FIGURE;
    if (strcasecmp(tag_name, "figcaption") == 0) return HTM_TAG_FIGCAPTION;
    if (strcasecmp(tag_name, "details") == 0) return HTM_TAG_DETAILS;
    if (strcasecmp(tag_name, "summary") == 0) return HTM_TAG_SUMMARY;
    if (strcasecmp(tag_name, "dialog") == 0) return HTM_TAG_DIALOG;
    if (strcasecmp(tag_name, "data") == 0) return HTM_TAG_DATA;
    if (strcasecmp(tag_name, "output") == 0) return HTM_TAG_OUTPUT;
    if (strcasecmp(tag_name, "progress") == 0) return HTM_TAG_PROGRESS;
    if (strcasecmp(tag_name, "meter") == 0) return HTM_TAG_METER;
    if (strcasecmp(tag_name, "menu") == 0) return HTM_TAG_MENU;
    if (strcasecmp(tag_name, "center") == 0) return HTM_TAG_CENTER;
    if (strcasecmp(tag_name, "pre") == 0) return HTM_TAG_PRE;
    if (strcasecmp(tag_name, "blockquote") == 0) return HTM_TAG_BLOCKQUOTE;
    if (strcasecmp(tag_name, "dd") == 0) return HTM_TAG_DD;
    if (strcasecmp(tag_name, "dt") == 0) return HTM_TAG_DT;
    if (strcasecmp(tag_name, "dl") == 0) return HTM_TAG_DL;

    // For unknown tags, return 0 (similar to Lexbor's HTM_TAG__UNDEF behavior)
    return 0;
}

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
        log_debug("attribute '%s' not found in array storage", name);
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

DomElement* dom_element_create(Pool* pool, const char* tag_name, Element* native_element) {
    if (!pool || !tag_name) {
        return NULL;
    }

    // Allocate raw memory from pool
    void* mem = pool_calloc(pool, sizeof(DomElement));
    if (!mem) {
        return NULL;
    }

    // Use placement new to construct DomElement with proper vtable initialization
    DomElement* element = new (mem) DomElement();

    if (!dom_element_init(element, pool, tag_name, native_element)) {
        return NULL;
    }

    return element;
}

bool dom_element_init(DomElement* element, Pool* pool, const char* tag_name, Element* native_element) {
    if (!element || !pool || !tag_name) {
        return false;
    }

    // NOTE: Do NOT use memset here! DomElement inherits from DomNode which has virtual
    // methods, so it has a vtable pointer that must not be zeroed. pool_calloc already
    // zeroes the memory, so we just need to set specific fields.

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

    // Convert tag name to Lexbor tag ID for fast comparison
    element->tag_id = DomNode::tag_name_to_id(tag_name);

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

    // Copy attributes from native_element if present
    if (native_element && native_element->type && native_element->data) {
        TypeElmt* elmt_type = (TypeElmt*)native_element->type;
        ShapeEntry* field = static_cast<ShapeEntry*>(elmt_type->shape);

        // Iterate through all attribute fields
        while (field) {
            // skip nested maps (fields without names)
            if (!field->name) {
                field = field->next;
                continue;
            }

            // Get field value from packed data
            void* field_ptr = (char*)native_element->data + field->byte_offset;
            TypeId type_id = field->type->type_id;

            // Convert attribute name from StrView to C string
            size_t name_len = field->name->length;
            char* attr_name = (char*)pool_alloc(pool, name_len + 1);
            if (!attr_name) {
                log_warn("failed to allocate attribute name");
                field = field->next;
                continue;
            }
            memcpy(attr_name, field->name->str, name_len);
            attr_name[name_len] = '\0';

            // Convert value to string and set attribute
            char value_buf[256];  // temporary buffer for numeric conversions
            const char* attr_value = NULL;

            switch (type_id) {
            case LMD_TYPE_NULL:
                // skip null attributes
                break;
            case LMD_TYPE_BOOL:
                attr_value = *(bool*)field_ptr ? "true" : "false";
                break;
            case LMD_TYPE_INT:
                snprintf(value_buf, sizeof(value_buf), "%d", *(int*)field_ptr);
                attr_value = value_buf;
                break;
            case LMD_TYPE_INT64:
                snprintf(value_buf, sizeof(value_buf), "%" PRId64, *(int64_t*)field_ptr);
                attr_value = value_buf;
                break;
            case LMD_TYPE_FLOAT:
                snprintf(value_buf, sizeof(value_buf), "%g", *(double*)field_ptr);
                attr_value = value_buf;
                break;
            case LMD_TYPE_STRING:
            case LMD_TYPE_SYMBOL: {
                String* str = *(String**)field_ptr;
                if (str) {
                    attr_value = str->chars;
                }
                break;
            }
            default:
                // for other types, skip or log a warning
                log_debug("skipping attribute '%s' with unsupported type %d", attr_name, type_id);
                break;
            }

            // Set the attribute if we have a value
            if (attr_value) {
                if (!attribute_storage_set(element->attributes, attr_name, attr_value)) {
                    log_warn("failed to set attribute '%s'", attr_name);
                }
            }

            field = field->next;
        }
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

    // Validate the property value before applying
    if (!css_property_validate_value(declaration->property_id, declaration->value)) {
        log_debug("[APPLY_DECL] Invalid value for property %d on <%s>, skipping",
                  declaration->property_id,
                  element->tag_name ? element->tag_name : "null");
        return false;
    }

    // DEBUG: Log which element is receiving the declaration
    log_debug("[APPLY_DECL] Element <%s> receiving property %d (spec:%u, order:%d)",
            element->tag_name ? element->tag_name : "null",
            declaration->property_id,
            css_specificity_to_value(declaration->specificity),
            declaration->source_order);

    // Apply to specified style tree
    StyleNode* node = style_tree_apply_declaration(element->specified_style, declaration);
    if (!node) {
        log_debug("[APPLY_DECL] FAILED to apply property %d to <%s>",
                  declaration->property_id,
                  element->tag_name ? element->tag_name : "null");
        return false;
    }

    log_debug("[APPLY_DECL] Successfully applied property %d to <%s>, style tree now has %d nodes",
              declaration->property_id,
              element->tag_name ? element->tag_name : "null",
              element->specified_style && element->specified_style->tree ? element->specified_style->tree->node_count : 0);

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
    return element ? static_cast<DomElement*>(element->parent) : NULL;
}

DomElement* dom_element_get_first_child(DomElement* element) {
    return element ? static_cast<DomElement*>(element->first_child) : NULL;
}

DomElement* dom_element_get_next_sibling(DomElement* element) {
    return element ? static_cast<DomElement*>(element->next_sibling) : NULL;
}

DomElement* dom_element_get_prev_sibling(DomElement* element) {
    return element ? static_cast<DomElement*>(element->prev_sibling) : NULL;
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
        DomNode* last_child_node = parent->first_child;

        while (last_child_node) {
            DomNode* next_sibling = last_child_node->next_sibling;

            if (!next_sibling) {
                // This is the last child, append the new element after it
                last_child_node->next_sibling = child;
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

    // Update sibling links - polymorphic base class handles this
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        // Child was first child
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
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
        reference_child->prev_sibling->next_sibling = new_child;
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

    DomElement* parent = static_cast<DomElement*>(element->parent);
    return parent->first_child == element;
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

    DomElement* parent = static_cast<DomElement*>(element->parent);
    return parent->first_child == element && element->next_sibling == NULL;
}

int dom_element_get_child_index(DomElement* element) {
    if (!element || !element->parent) {
        return -1;
    }

    // Count only element children (not text nodes or comments)
    // According to CSS spec, :nth-child() counts only element nodes
    int index = 0;
    DomElement* parent = static_cast<DomElement*>(element->parent);
    DomNode* sibling = parent->first_child;

    while (sibling && sibling != element) {
        // Only count element nodes for nth-child
        if (sibling->is_element()) {
            index++;
        }
        sibling = sibling->next_sibling;
    }

    return (sibling == element) ? index : -1;
}

int dom_element_child_count(DomElement* element) {
    if (!element) {
        return 0;
    }

    int count = 0;
    DomNode* child = element->first_child;

    while (child) {
        count++;
        child = child->next_sibling;
    }

    return count;
}

int dom_element_count_child_elements(DomElement* element) {
    if (!element) {
        return 0;
    }

    int count = 0;
    DomNode* child = element->first_child;

    while (child) {
        // Only count element children (not text or comment nodes)
        if (child->is_element()) {
            count++;
        }
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
    printf("  Children: %d\n", dom_element_count_child_elements(element));
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

    // Allocate raw memory and use placement new for proper vtable initialization
    void* mem = pool_calloc(pool, sizeof(DomText));
    if (!mem) {
        return NULL;
    }

    DomText* text_node = new (mem) DomText();

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

    // Allocate raw memory and use placement new for proper vtable initialization
    void* mem = pool_calloc(pool, sizeof(DomComment));
    if (!mem) {
        return NULL;
    }

    DomComment* comment_node = new (mem) DomComment(node_type);

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
// Helper Functions for DOM Tree Building
// ============================================================================

/**
 * Extract string attribute from Lambda Element
 * Returns attribute value or nullptr if not found
 */
extern "C" const char* extract_element_attribute(Element* elem, const char* attr_name, Pool* pool) {
    if (!elem || !attr_name) return nullptr;

    // Create a string key for the attribute
    String* key_str = (String*)pool_alloc(pool, sizeof(String) + strlen(attr_name) + 1);
    if (!key_str) return nullptr;

    key_str->len = strlen(attr_name);
    strcpy(key_str->chars, attr_name);

    Item key;
    key.item = s2it(key_str);

    // Get the attribute value using elmt_get_typed
    TypedItem attr_value = elmt_get_typed(elem, key);

    // Check if it's a string
    if (attr_value.type_id == LMD_TYPE_STRING && attr_value.string) {
        return attr_value.string->chars;
    }

    return nullptr;
}

// ============================================================================
// DOM Element Print Implementation
// ============================================================================

/**
 * Context for style property printing callback
 */
typedef struct {
    StrBuf* buf;
    bool* has_props;
} StylePrintContext;

/**
 * Callback function for printing each style property
 * Called by style_tree_foreach for each property in the style tree
 */
static bool print_style_property_callback(StyleNode* node, void* context) {
    if (!node || !node->winning_decl || !context) {
        return true;  // continue iteration
    }

    StylePrintContext* ctx = (StylePrintContext*)context;
    CssDeclaration* decl = node->winning_decl;

    if (!decl->value) {
        return true;  // skip properties without values
    }

    // Add comma separator if not first property
    if (*ctx->has_props) {
        strbuf_append_str(ctx->buf, ", ");
    }

    // Get property name using the property database
    const char* prop_name = css_get_property_name(decl->property_id);

    if (!prop_name) {
        // If no name available, print with property ID for debugging
        char prop_id_buf[32];
        snprintf(prop_id_buf, sizeof(prop_id_buf), "property-%d", (int)decl->property_id);
        strbuf_append_str(ctx->buf, prop_id_buf);
    } else {
        // Print property name
        strbuf_append_str(ctx->buf, prop_name);
    }
    strbuf_append_char(ctx->buf, ':');

    // Format the value using the CSS formatter
    CssValue* val = (CssValue*)decl->value;

    // Create a temporary pool and formatter for value formatting
    Pool* temp_pool = pool_create();
    if (temp_pool) {
        CssFormatter* formatter = css_formatter_create(temp_pool, CSS_FORMAT_COMPACT);
        if (formatter) {
            // Format the value
            css_format_value(formatter, val);

            // Get the formatted string from the formatter's output buffer
            String* result = stringbuf_to_string(formatter->output);
            if (result && result->len > 0) {
                strbuf_append_str(ctx->buf, result->chars);
            }

            css_formatter_destroy(formatter);
        }
        pool_destroy(temp_pool);
    }

    *ctx->has_props = true;
    return true;  // continue iteration
}

/**
 * Print a DOM element and its children to a string buffer
 * Outputs the element in a tree-like format with proper indentation
 */
/**
 * Print a DOM element and its children to a string buffer
 * Outputs the element in a tree-like format with proper indentation
 *
 * @param element Element to print
 * @param buf String buffer to write to
 * @param indent Base indentation level (number of spaces)
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

    // Print id attribute first if present
    if (element->id && element->id[0] != '\0') {
        strbuf_append_str(buf, " id=\"");
        strbuf_append_str(buf, element->id);
        strbuf_append_char(buf, '"');
    }

    // Print class attribute if present
    if (element->class_count > 0 && element->class_names) {
        strbuf_append_str(buf, " class=\"");
        for (int i = 0; i < element->class_count; i++) {
            if (i > 0) {
                strbuf_append_char(buf, ' ');
            }
            strbuf_append_str(buf, element->class_names[i]);
        }
        strbuf_append_char(buf, '"');
    }

    // Print other attributes
    if (element->attributes) {
        int attr_count = 0;
        const char** attr_names = attribute_storage_get_names(element->attributes, &attr_count);
        if (attr_names) {
            for (int i = 0; i < attr_count; i++) {
                const char* name = attr_names[i];
                const char* value = attribute_storage_get(element->attributes, name);

                // Skip id and class as they're already printed above
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

    // Print pseudo-state information if any (for testing/debugging)
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
        int has_text = false;
        strbuf_append_str(buf, "[");

        // print id
        if (element->id && element->id[0] != '\0') {
            strbuf_append_format(buf, "id:'%s'", element->id);
            has_text = true;
        }

        // print classes
        if (element->class_count > 0 && element->class_names) {
            strbuf_append_str(buf, has_text ? ", classes:" : "classes:");
            strbuf_append_char(buf, '[');
            for (int i = 0; i < element->class_count; i++) {
                strbuf_append_format(buf, "\"%s\"", element->class_names[i]);
                if (i < element->class_count - 1) strbuf_append_char(buf, ',');
            }
            strbuf_append_char(buf, ']');
            has_text = true;
        }

        // print styles generically using style_tree_foreach
        if (element->specified_style && element->specified_style->tree) {
            strbuf_append_str(buf, has_text ? ", styles:{" : "styles:{");

            bool has_props = false;
            StylePrintContext ctx = { buf, &has_props };

            // Iterate through all properties in the style tree
            style_tree_foreach(element->specified_style, print_style_property_callback, &ctx);

            strbuf_append_str(buf, "}");
        }

        strbuf_append_char(buf, ']');
    }

    // Print children
    DomNode* child = element->first_child;
    bool has_element_children = false;

    // Count and print children
    while (child) {
        if (child->is_element()) {
            // Recursively print child elements with newline before each child
            has_element_children = true;
            strbuf_append_char(buf, '\n');
            dom_element_print((DomElement*)child, buf, indent + 2);
        } else if (child->is_text()) {
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
                    strbuf_append_str(buf, "\n");
                    strbuf_append_char_n(buf, ' ', indent + 2);
                    strbuf_append_str(buf, "\"");
                    strbuf_append_str_n(buf, text_node->text, text_node->length);
                    strbuf_append_str(buf, "\"");
                }
            }
        } else if (child->is_comment()) {
            // Print comment/DOCTYPE nodes
            DomComment* comment_node = (DomComment*)child;
            strbuf_append_char(buf, '\n');
            strbuf_append_char_n(buf, ' ', indent + 2);
            strbuf_append_str(buf, "<!-- ");
            if (comment_node->content) {
                strbuf_append_str(buf, comment_node->content);
            }
            strbuf_append_str(buf, " -->");
        }

        // Move to next sibling
        child = child->next_sibling;
    }

    // Print closing tag
    // Add newline and indentation before closing tag only if we had element children
    if (has_element_children) {
        strbuf_append_char(buf, '\n');
        strbuf_append_char_n(buf, ' ', indent);
    }
    strbuf_append_str(buf, "</");
    strbuf_append_str(buf, element->tag_name ? element->tag_name : "unknown");
    strbuf_append_char(buf, '>');

    // Add trailing newline only for root element (indent == 0)
    if (indent == 0) {
        strbuf_append_char(buf, '\n');
    }
}

/**
 * Recursively build DomElement tree from Lambda Element tree
 * Converts HTML parser output (Element) to CSS system format (DomElement)
 * Now includes text nodes, comments, DOCTYPE, and all other node types
 */
extern "C" DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent) {
    if (!elem || !pool) {
        log_debug("build_dom_tree_from_element: Invalid arguments\n");
        return nullptr;
    }

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;
    log_debug("build element: <%s> (parent: %s)", tag_name,
              parent ? parent->tag_name : "none");

    // skip comments and other non-element nodes - they should not participate in CSS cascade or layout
    // use case-insensitive comparison for DOCTYPE to handle both !DOCTYPE and !doctype
    if (strcmp(tag_name, "!--") == 0 ||
        strcasecmp(tag_name, "!DOCTYPE") == 0 ||
        strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip comments, DOCTYPE, and XML declarations
    }

    // skip script elements - they should not participate in layout
    // script elements have display: none by default in browser user-agent stylesheets
    if (strcasecmp(tag_name, "script") == 0) {
        return nullptr;  // Skip script elements during DOM tree building
    }

    // create DomElement
    DomElement* dom_elem = dom_element_create(pool, tag_name, elem);
    if (!dom_elem) return nullptr;

    // extract id and class attributes from Lambda Element
    const char* id_value = extract_element_attribute(elem, "id", pool);
    if (id_value) {
        dom_element_set_attribute(dom_elem, "id", id_value);
    }

    const char* class_value = extract_element_attribute(elem, "class", pool);
    if (class_value) {
        // parse multiple classes separated by spaces
        char* class_copy = (char*)pool_alloc(pool, strlen(class_value) + 1);
        if (class_copy) {
            strcpy(class_copy, class_value);

            // split by spaces and add each class
            char* token = strtok(class_copy, " \t\n");
            while (token) {
                if (strlen(token) > 0) {
                    dom_element_add_class(dom_elem, token);
                }
                token = strtok(nullptr, " \t\n");
            }
        }
    }

    // extract rowspan and colspan attributes for table cells (td, th)
    if (strcasecmp(tag_name, "td") == 0 || strcasecmp(tag_name, "th") == 0) {
        const char* rowspan_value = extract_element_attribute(elem, "rowspan", pool);
        if (rowspan_value) {
            dom_element_set_attribute(dom_elem, "rowspan", rowspan_value);
        }

        const char* colspan_value = extract_element_attribute(elem, "colspan", pool);
        if (colspan_value) {
            dom_element_set_attribute(dom_elem, "colspan", colspan_value);
        }
    }    // set parent relationship if provided
    if (parent) {
        dom_element_append_child(parent, dom_elem);
    }

    // Process all children - including text nodes, comments, and elements
    // Elements are Lists, so iterate through items

    log_debug("Processing %lld children for <%s> (dom_elem=%p)", elem->length, tag_name, (void*)dom_elem);
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        TypeId child_type = get_type_id(child_item);
        log_debug("  Child %lld: type=%d", i, child_type);
        if (child_type == LMD_TYPE_ELEMENT) {
            // element node - recursively build
            Element* child_elem = (Element*)child_item.pointer;
            TypeElmt* child_elem_type = (TypeElmt*)child_elem->type;
            const char* child_tag_name = child_elem_type ? child_elem_type->name.str : "unknown";

            log_debug("  Building child element: <%s> for parent <%s> (parent_dom=%p)", child_tag_name, tag_name, (void*)dom_elem);
            DomElement* child_dom = build_dom_tree_from_element(child_elem, pool, dom_elem);

            // skip if nullptr (e.g., comments, DOCTYPE were filtered out)
            if (!child_dom) {
                log_debug("  Skipped child element: <%s>", child_tag_name);
                continue;
            }

            log_debug("  Successfully built child <%s> with parent <%s>. child_dom=%p, child_dom->parent=%p",
                     child_tag_name, tag_name, (void*)child_dom, (void*)child_dom->parent);

            // The dom_element_append_child was already called in the recursive call,
            // so the parent-child and sibling relationships are already established correctly.
            // No manual linking needed!

        } else if (child_type == LMD_TYPE_STRING) {
            // Text node - create DomText and append manually (no dom_text_append_child function)
            String* text_str = (String*)child_item.pointer;
            if (text_str && text_str->len > 0) {
                DomText* text_node = dom_text_create(pool, text_str->chars);
                if (text_node) {
                    text_node->parent = dom_elem;

                    // Add text node using the same append logic as dom_element_append_child
                    if (!dom_elem->first_child) {
                        // First child
                        dom_elem->first_child = text_node;
                        text_node->prev_sibling = nullptr;
                        text_node->next_sibling = nullptr;
                    } else {
                        // Find last child and append
                        DomNode* last_child_node = dom_elem->first_child;
                        while (last_child_node) {
                            DomNode* next = last_child_node->next_sibling;

                            if (!next) {
                                // This is the last child, append text node here
                                last_child_node->next_sibling = text_node;
                                text_node->prev_sibling = last_child_node;
                                text_node->next_sibling = nullptr;
                                break;
                            }
                            last_child_node = next;
                        }
                    }
                }
            }
        }
    }

    return dom_elem;
}
