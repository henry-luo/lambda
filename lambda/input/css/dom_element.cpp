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
#include "../../../lib/arena.h"
#include "../../lambda.h"
#include "../../lambda-data.hpp"  // For get_type_id, and proper type definitions
#include "../../mark_reader.hpp"  // For ElementReader
#include "../../mark_editor.hpp"  // For MarkEditor
#include "../../mark_builder.hpp" // For MarkBuilder

// Forward declaration
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* document, DomElement* parent);
DomElement* build_dom_tree_from_element_with_input(Element* elem, DomDocument* document, DomElement* parent);

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
// DOM Document Creation and Destruction
// ============================================================================

DomDocument* dom_document_create(Input* input) {
    if (!input) {
        log_error("dom_document_create: input is required");
        return nullptr;
    }

    // Allocate document structure
    DomDocument* document = (DomDocument*)calloc(1, sizeof(DomDocument));
    if (!document) {
        log_error("dom_document_create: failed to allocate document");
        return nullptr;
    }

    // Create pool for arena chunks
    document->pool = pool_create();
    if (!document->pool) {
        log_error("dom_document_create: failed to create pool");
        free(document);
        return nullptr;
    }

    // Create arena for all DOM node allocations
    document->arena = arena_create_default(document->pool);
    if (!document->arena) {
        log_error("dom_document_create: failed to create arena");
        pool_destroy(document->pool);
        free(document);
        return nullptr;
    }

    document->input = input;
    document->root = nullptr;

    log_debug("dom_document_create: created document with arena");
    return document;
}

void dom_document_destroy(DomDocument* document) {
    if (!document) {
        return;
    }

    // Note: root and all DOM nodes are allocated from arena,
    // so they will be freed when arena is destroyed
    if (document->arena) {
        arena_destroy(document->arena);
    }

    if (document->pool) {
        pool_destroy(document->pool);
    }

    // Note: Input* is not owned by document, don't free it
    free(document);
    log_debug("dom_document_destroy: destroyed document and arena");
}

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

DomElement* dom_element_create(DomDocument* doc, const char* tag_name, Element* native_element) {
    if (!doc || !tag_name || !native_element) {
        return NULL;
    }

    // Allocate raw memory from arena
    DomElement* element = (DomElement*)arena_calloc(doc->arena, sizeof(DomElement));
    if (!element) {
        return NULL;
    }

    // Initialize the element (this sets up the base DomNode fields and derived fields)
    if (!dom_element_init(element, doc, tag_name, native_element)) {
        return NULL;
    }

    return element;
}

bool dom_element_init(DomElement* element, DomDocument* doc, const char* tag_name, Element* native_element) {
    if (!element || !doc || !tag_name || !native_element) {
        return false;
    }

    // NOTE: arena_calloc already zeros the memory, so we just need to initialize specific fields
    // Initialize base DomNode fields
    element->node_type = DOM_NODE_ELEMENT;
    element->parent = NULL;
    element->next_sibling = NULL;
    element->prev_sibling = NULL;

    // Initialize DomElement fields
    element->first_child = NULL;
    element->doc = doc;
    element->native_element = native_element;
    
    // Copy tag name
    size_t tag_len = strlen(tag_name);
    char* tag_copy = (char*)arena_alloc(doc->arena, tag_len + 1);
    if (!tag_copy) {
        return false;
    }
    strcpy(tag_copy, tag_name);
    element->tag_name = tag_copy;
    element->tag_name_ptr = (void*)tag_copy;  // Use string pointer as unique ID

    // Convert tag name to Lexbor tag ID for fast comparison
    element->tag_id = DomNode::tag_name_to_id(tag_name);

    // Create style trees (still use pool for AVL nodes)
    element->specified_style = style_tree_create(doc->pool);
    if (!element->specified_style) {
        return false;
    }

    element->computed_style = style_tree_create(doc->pool);
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

    // Initialize cached attribute fields from native element (if exists)
    if (native_element) {
        ElementReader reader(native_element);
        
        // Cache ID attribute
        element->id = reader.get_attr_string("id");
        
        // Parse class attribute into array
        const char* class_str = reader.get_attr_string("class");
        if (class_str && class_str[0] != '\0') {
            // Count classes (space-separated)
            int count = 1;
            for (const char* p = class_str; *p; p++) {
                if (*p == ' ' || *p == '\t') count++;
            }
            
            // Allocate array from arena
            element->class_names = (const char**)arena_alloc(doc->arena, count * sizeof(const char*));
            if (element->class_names) {
                // Parse classes - make a copy for strtok
                char* class_copy = (char*)arena_alloc(doc->arena, strlen(class_str) + 1);
                if (class_copy) {
                    strcpy(class_copy, class_str);
                    
                    int index = 0;
                    char* token = strtok(class_copy, " \t\n\r");
                    while (token && index < count) {
                        // Allocate permanent copy of each class from arena
                        size_t token_len = strlen(token);
                        char* class_perm = (char*)arena_alloc(doc->arena, token_len + 1);
                        if (class_perm) {
                            strcpy(class_perm, token);
                            element->class_names[index++] = class_perm;
                        }
                        token = strtok(NULL, " \t\n\r");
                    }
                    element->class_count = index;
                }
            }
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

    // Clear cached fields (but don't free - owned by pool/element)
    element->id = NULL;
    element->class_names = NULL;
    element->class_count = 0;

    // Note: native_element is not freed here - managed by Input/Arena
    // Note: The element structure itself is pool-allocated,
    // so it will be freed when the pool is destroyed
}

// ============================================================================
// Attribute Management
// ============================================================================

bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (!element || !name || !value) {
        log_debug("dom_element_set_attribute: invalid parameters");
        return false;
    }

    // If native_element exists, use MarkEditor for updates
    if (element->native_element && element->doc) {
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);
        
        // Create string value item
        Item value_item = editor.builder()->createStringItem(value);
        
        // Update attribute via MarkEditor
        Item result = editor.elmt_update_attr(
            {.element = element->native_element}, 
            name, 
            value_item
        );
        
        if (result.element) {
            // In INLINE mode, element pointer remains the same (in-place mutation)
            // In IMMUTABLE mode, a new element would be created
            // Since we're using INLINE mode, this assignment is a no-op but kept for consistency
            element->native_element = result.element;
            
            // Handle special attributes
            if (strcmp(name, "id") == 0) {
                // Cache ID for fast access
                ElementReader reader(element->native_element);
                element->id = reader.get_attr_string("id");
            } else if (strcmp(name, "class") == 0) {
                // Parse class attribute - for now just trigger class cache refresh
                // TODO: Properly parse space-separated classes
                dom_element_add_class(element, value);
            } else if (strcmp(name, "style") == 0) {
                // Parse and apply inline styles
                dom_element_apply_inline_style(element, value);
            }
            
            // Invalidate style cache
            element->style_version++;
            element->needs_style_recompute = true;
            
            return true;
        }
        
        log_error("dom_element_set_attribute: MarkEditor failed to update attribute");
        return false;
    }
    
    // No native element - log warning
    log_warn("dom_element_set_attribute: element has no native_element or input context");
    return false;
}

const char* dom_element_get_attribute(DomElement* element, const char* name) {
    if (!element || !name || name[0] == '\0') {
        return nullptr;
    }
    
    // Use ElementReader for read-only access
    if (element->native_element) {
        ElementReader reader(element->native_element);
        return reader.get_attr_string(name);
    }
    
    return nullptr;
}

bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (!element || !name) {
        return false;
    }

    if (element->native_element && element->doc) {
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);
        
        // Delete attribute via MarkEditor
        Item result = editor.elmt_delete_attr(
            {.element = element->native_element}, 
            name
        );
        
        if (result.element) {
            // In INLINE mode, element pointer remains the same
            // This assignment is a no-op but kept for consistency
            element->native_element = result.element;
            
            // Clear cached fields
            if (strcmp(name, "id") == 0) {
                element->id = nullptr;
            }
            
            // Invalidate style cache
            element->style_version++;
            element->needs_style_recompute = true;
            
            return true;
        }
    }

    return false;
}

bool dom_element_has_attribute(DomElement* element, const char* name) {
    if (!element || !name) {
        return false;
    }
    
    if (element->native_element) {
        ElementReader reader(element->native_element);
        return reader.has_attr(name);
    }
    
    return false;
}

const char** dom_element_get_attribute_names(DomElement* element, int* count) {
    if (!element || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    
    *count = 0;
    if (!element->native_element) return nullptr;
    
    ElementReader reader(element->native_element);
    int attr_count = reader.attrCount();
    if (attr_count == 0) return nullptr;
    
    // Allocate array from arena
    const char** names = (const char**)arena_alloc(
        element->doc->arena, 
        attr_count * sizeof(const char*)
    );
    if (!names) return nullptr;
    
    // Iterate through shape to collect names
    const TypeElmt* type = (const TypeElmt*)element->native_element->type;
    if (!type || !type->shape) {
        *count = 0;
        return nullptr;
    }
    
    const ShapeEntry* field = type->shape;
    int index = 0;
    
    while (field && index < attr_count) {
        if (field->name && field->name->str) {
            names[index++] = field->name->str;
        }
        field = field->next;
    }
    
    *count = index;
    return names;
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
    const char** new_classes = (const char**)arena_alloc(element->doc->arena,
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
    char* class_copy = (char*)arena_alloc(element->doc->arena, class_len + 1);
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
    if (!element || !style_text || !element->doc) {
        return 0;
    }

    int applied_count = 0;

    // Parse the style text - split by semicolons
    // Example: "color: red; font-size: 14px; background: blue"
    char* text_copy = (char*)arena_alloc(element->doc->arena, strlen(style_text) + 1);
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
        CssDeclaration* decl = css_parse_property(prop_name, prop_value, element->doc->pool);
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
    if (!element) {
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

    // All DomElements must have backing Lambda element
    if (!source->native_element || !source->doc) {
        log_error("dom_element_clone: source element must have native_element and doc");
        return NULL;
    }

    // Use MarkBuilder to deep copy the backing Lambda element
    MarkBuilder builder(source->doc->input);
    Item cloned_elem = builder.deep_copy({.element = source->native_element});
    
    if (!cloned_elem.element) {
        log_error("dom_element_clone: MarkBuilder deep_copy failed");
        return NULL;
    }

    // Create a new document for the clone (using the same input)
    DomDocument* clone_doc = dom_document_create(source->doc->input);
    if (!clone_doc) {
        log_error("dom_element_clone: failed to create document for clone");
        return NULL;
    }

    // Build DomElement wrapper from the cloned Lambda element
    DomElement* clone = build_dom_tree_from_element(cloned_elem.element, clone_doc, nullptr);
    if (!clone) {
        log_error("dom_element_clone: build_dom_tree_from_element failed");
        dom_document_destroy(clone_doc);
        return NULL;
    }

    // Copy classes (if not already copied by build_dom_tree_from_element)
    for (int i = 0; i < source->class_count; i++) {
        if (!dom_element_has_class(clone, source->class_names[i])) {
            dom_element_add_class(clone, source->class_names[i]);
        }
    }

    // Deep copy style trees using style_tree_clone
    if (source->specified_style) {
        clone->specified_style = style_tree_clone(source->specified_style, pool);
    }

    if (source->computed_style) {
        clone->computed_style = style_tree_clone(source->computed_style, pool);
    }

    // Copy pseudo state
    clone->pseudo_state = source->pseudo_state;

    // Note: Children are not cloned - caller should handle that if needed

    return clone;
}// ============================================================================
// DOM Text Node Implementation
// ============================================================================

DomText* dom_text_create(String* native_string, DomElement* parent_element, int64_t child_index) {
    if (!native_string || !parent_element) {
        log_error("dom_text_create: native_string and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("dom_text_create: parent_element has no document");
        return nullptr;
    }

    // Allocate from parent's document arena
    DomText* text_node = (DomText*)arena_calloc(parent_element->doc->arena, sizeof(DomText));
    if (!text_node) {
        log_error("dom_text_create: arena_calloc failed");
        return nullptr;
    }

    // Initialize base DomNode fields
    text_node->node_type = DOM_NODE_TEXT;
    text_node->parent = parent_element;
    text_node->next_sibling = nullptr;
    text_node->prev_sibling = nullptr;

    // Set Lambda backing (REFERENCE, not copy)
    text_node->native_string = native_string;
    text_node->text = native_string->chars;  // Reference Lambda String's chars
    text_node->length = native_string->len;
    text_node->parent_element = parent_element;
    text_node->child_index = child_index;

    log_debug("dom_text_create: created backed text node at index %lld, text='%s'", 
              child_index, native_string->chars);

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

bool dom_text_set_content(DomText* text_node, const char* new_content) {
    if (!text_node || !new_content) {
        log_error("dom_text_set_content: invalid parameters");
        return false;
    }

    if (!text_node->native_string || !text_node->parent_element) {
        log_error("dom_text_set_content: text node not backed by Lambda");
        return false;
    }

    if (!text_node->parent_element->doc) {
        log_error("dom_text_set_content: parent element has no document");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_text_get_child_index(text_node);
    if (child_idx < 0) {
        log_error("dom_text_set_content: failed to get child index");
        return false;
    }

    // Create new String via MarkBuilder
    MarkEditor editor(text_node->parent_element->doc->input, EDIT_MODE_INLINE);
    Item new_string_item = editor.builder()->createStringItem(new_content);

    if (!new_string_item.pointer || get_type_id(new_string_item) != LMD_TYPE_STRING) {
        log_error("dom_text_set_content: failed to create string");
        return false;
    }

    // Replace child in parent Element's items array
    Item result = editor.elmt_replace_child(
        {.element = text_node->parent_element->native_element},
        child_idx,
        new_string_item
    );

    if (!result.element) {
        log_error("dom_text_set_content: failed to replace child");
        return false;
    }

    // Update DomText to point to new String
    text_node->native_string = (String*)new_string_item.pointer;
    text_node->text = text_node->native_string->chars;
    text_node->length = text_node->native_string->len;

    // In INLINE mode, parent element pointer unchanged, but update for consistency
    text_node->parent_element->native_element = result.element;

    log_debug("dom_text_set_content: updated text at index %lld to '%s'", child_idx, new_content);

    return true;
}

bool dom_text_is_backed(DomText* text_node) {
    return text_node && text_node->native_string && text_node->parent_element;
}

int64_t dom_text_get_child_index(DomText* text_node) {
    if (!text_node || !text_node->parent_element || !text_node->native_string) {
        log_error("dom_text_get_child_index: text node not backed");
        return -1;
    }

    Element* parent_elem = text_node->parent_element->native_element;
    if (!parent_elem) {
        log_error("dom_text_get_child_index: parent has no native_element");
        return -1;
    }

    // Try cached index first (optimization)
    if (text_node->child_index >= 0 && text_node->child_index < parent_elem->length) {
        Item cached_item = parent_elem->items[text_node->child_index];
        if (get_type_id(cached_item) == LMD_TYPE_STRING && (String*)cached_item.pointer == text_node->native_string) {
            log_debug("dom_text_get_child_index: cache hit at index %lld", text_node->child_index);
            return text_node->child_index;  // Cache hit
        }
    }

    // Cache miss - scan for correct index
    log_debug("dom_text_get_child_index: cache miss, scanning parent children");
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING && (String*)item.pointer == text_node->native_string) {
            text_node->child_index = i;  // Update cache
            log_debug("dom_text_get_child_index: found at index %lld", i);
            return i;
        }
    }

    log_error("dom_text_get_child_index: native_string not found in parent (may have been removed)");
    return -1;
}

bool dom_text_remove(DomText* text_node) {
    if (!text_node) {
        log_error("dom_text_remove: null text node");
        return false;
    }

    if (!dom_text_is_backed(text_node)) {
        log_error("dom_text_remove: text node not backed");
        return false;
    }

    if (!text_node->parent_element->doc) {
        log_error("dom_text_remove: parent element has no document");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_text_get_child_index(text_node);
    if (child_idx < 0) {
        log_error("dom_text_remove: failed to get child index");
        return false;
    }

    // Remove from Lambda parent Element's children array
    MarkEditor editor(text_node->parent_element->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = text_node->parent_element->native_element},
        child_idx
    );

    if (!result.element) {
        log_error("dom_text_remove: failed to delete child");
        return false;
    }

    // Update parent
    text_node->parent_element->native_element = result.element;

    // Remove from DOM sibling chain
    if (text_node->prev_sibling) {
        text_node->prev_sibling->next_sibling = text_node->next_sibling;
    } else if (text_node->parent && text_node->parent->is_element()) {
        DomElement* parent_elem = static_cast<DomElement*>(text_node->parent);
        parent_elem->first_child = text_node->next_sibling;
    }

    if (text_node->next_sibling) {
        text_node->next_sibling->prev_sibling = text_node->prev_sibling;
    }

    // Update sibling text nodes' child indices (they shifted after removal)
    DomNode* sibling = text_node->next_sibling;
    while (sibling) {
        if (sibling->is_text()) {
            DomText* text_sibling = static_cast<DomText*>(sibling);
            if (dom_text_is_backed(text_sibling) && text_sibling->child_index > child_idx) {
                text_sibling->child_index--;
                log_debug("dom_text_remove_backed: updated sibling index from %lld to %lld", 
                          text_sibling->child_index + 1, text_sibling->child_index);
            }
        }
        sibling = sibling->next_sibling;
    }

    // Clear references
    text_node->parent = nullptr;
    text_node->native_string = nullptr;
    text_node->parent_element = nullptr;
    text_node->child_index = -1;

    log_debug("dom_text_remove: removed text node at index %lld", child_idx);

    return true;
}

DomText* dom_element_append_text(DomElement* parent, const char* text_content) {
    if (!parent || !text_content) {
        log_error("dom_element_append_text: invalid parameters");
        return nullptr;
    }

    if (!parent->native_element || !parent->doc) {
        log_error("dom_element_append_text: parent element must be backed");
        return nullptr;
    }

    // Create String item via MarkBuilder
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item string_item = editor.builder()->createStringItem(text_content);

    if (!string_item.pointer || get_type_id(string_item) != LMD_TYPE_STRING) {
        log_error("dom_element_append_text: failed to create string");
        return nullptr;
    }

    // Append to parent Element's children via MarkEditor
    Item result = editor.elmt_append_child(
        {.element = parent->native_element},
        string_item
    );

    if (!result.element) {
        log_error("dom_element_append_text: failed to append child");
        return nullptr;
    }

    // Calculate child index (last position)
    int64_t child_index = parent->native_element->length - 1;

    // Create DomText wrapper with Lambda backing
    DomText* text_node = dom_text_create(
        (String*)string_item.pointer,
        parent,
        child_index
    );

    if (!text_node) {
        log_error("dom_element_append_text: failed to create DomText");
        return nullptr;
    }

    // Add to DOM sibling chain
    text_node->parent = parent;
    if (!parent->first_child) {
        // First child
        parent->first_child = text_node;
        text_node->prev_sibling = nullptr;
        text_node->next_sibling = nullptr;
    } else {
        // Find last child and append
        DomNode* last = parent->first_child;
        while (last->next_sibling) {
            last = last->next_sibling;
        }
        last->next_sibling = text_node;
        text_node->prev_sibling = last;
        text_node->next_sibling = nullptr;
    }

    // Update parent element pointer (INLINE mode: no-op, but kept for consistency)
    parent->native_element = result.element;

    log_debug("dom_element_append_text: appended text '%s' at index %lld", text_content, child_index);

    return text_node;
}

// ============================================================================
// DOM Comment/DOCTYPE Node Implementation
// ============================================================================

DomComment* dom_comment_create(Element* native_element, DomElement* parent_element, int64_t child_index) {
    if (!native_element || !parent_element) {
        log_error("dom_comment_create: native_element and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("dom_comment_create: parent_element has no document");
        return nullptr;
    }

    // Get tag name and verify it's a comment or DOCTYPE
    TypeElmt* type = (TypeElmt*)native_element->type;
    const char* tag_name = type ? type->name.str : nullptr;
    if (!tag_name) {
        log_error("dom_comment_create: no tag name");
        return nullptr;
    }

    // Determine node type
    DomNodeType node_type;
    if (strcasecmp(tag_name, "!DOCTYPE") == 0) {
        node_type = DOM_NODE_DOCTYPE;
    } else if (strcmp(tag_name, "!--") == 0) {
        node_type = DOM_NODE_COMMENT;
    } else {
        log_error("dom_comment_create: not a comment or DOCTYPE: %s", tag_name);
        return nullptr;
    }

    // Allocate from parent's document arena
    DomComment* comment_node = (DomComment*)arena_calloc(parent_element->doc->arena, sizeof(DomComment));
    if (!comment_node) {
        log_error("dom_comment_create: arena_calloc failed");
        return nullptr;
    }

    // Initialize base DomNode
    comment_node->node_type = node_type;
    comment_node->parent = parent_element;
    comment_node->next_sibling = nullptr;
    comment_node->prev_sibling = nullptr;

    // Set Lambda backing
    comment_node->native_element = native_element;
    comment_node->parent_element = parent_element;
    comment_node->child_index = child_index;
    comment_node->tag_name = tag_name;  // Reference type name (no copy needed)

    // Extract content from first String child (if exists)
    if (native_element->length > 0) {
        Item first_item = native_element->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* content_str = (String*)first_item.pointer;
            comment_node->content = content_str->chars;  // Reference, not copy
            comment_node->length = content_str->len;
        }
    }

    if (!comment_node->content) {
        comment_node->content = "";
        comment_node->length = 0;
    }

    log_debug("dom_comment_create: created backed comment (tag=%s, content='%s', index=%lld)",
              tag_name, comment_node->content, child_index);

    return comment_node;
}

void dom_comment_destroy(DomComment* comment_node) {
    if (!comment_node) {
        return;
    }
    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

// ============================================================================
// Backed DomComment Operations (Lambda Integration)
// ============================================================================

bool dom_comment_is_backed(DomComment* comment_node) {
    return comment_node && comment_node->native_element && comment_node->parent_element;
}

bool dom_comment_set_content(DomComment* comment_node, const char* new_content) {
    if (!comment_node || !new_content) {
        log_error("dom_comment_set_content: invalid arguments");
        return false;
    }

    if (!comment_node->native_element || !comment_node->parent_element) {
        log_error("dom_comment_set_content: comment not backed by Lambda");
        return false;
    }

    if (!comment_node->parent_element->doc) {
        log_error("dom_comment_set_content: parent element has no document");
        return false;
    }

    // Create new String via MarkBuilder
    MarkEditor editor(comment_node->parent_element->doc->input, EDIT_MODE_INLINE);
    Item new_string_item = editor.builder()->createStringItem(new_content);

    if (!new_string_item.pointer || get_type_id(new_string_item) != LMD_TYPE_STRING) {
        log_error("dom_comment_set_content: failed to create string");
        return false;
    }

    // Replace or append String child in comment Element
    Item result;
    if (comment_node->native_element->length > 0) {
        // Replace existing content (child at index 0)
        result = editor.elmt_replace_child(
            {.element = comment_node->native_element},
            0,  // Content is always first child
            new_string_item
        );
    } else {
        // Append content (comment was empty)
        result = editor.elmt_append_child(
            {.element = comment_node->native_element},
            new_string_item
        );
    }

    if (!result.element) {
        log_error("dom_comment_set_content: failed to update content");
        return false;
    }

    // Update DomComment to point to new String
    comment_node->native_element = result.element;
    String* new_string = (String*)new_string_item.pointer;
    comment_node->content = new_string->chars;
    comment_node->length = new_string->len;

    log_debug("dom_comment_set_content: updated content to '%s'", new_content);

    return true;
}

DomComment* dom_element_append_comment(DomElement* parent, const char* comment_content) {
    if (!parent || !comment_content) {
        log_error("dom_element_append_comment: invalid arguments");
        return nullptr;
    }

    if (!parent->native_element || !parent->doc) {
        log_error("dom_element_append_comment: parent not backed");
        return nullptr;
    }

    // Create Lambda comment Element with tag "!--"
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    ElementBuilder comment_elem = editor.builder()->element("!--");

    // Add content as String child
    if (strlen(comment_content) > 0) {
        Item content_item = editor.builder()->createStringItem(comment_content);
        comment_elem.child(content_item);
    }

    Item comment_item = comment_elem.final();
    if (!comment_item.element) {
        log_error("dom_element_append_comment: failed to create comment element");
        return nullptr;
    }

    // Append to parent Element's children
    Item result = editor.elmt_append_child(
        {.element = parent->native_element},
        comment_item
    );

    if (!result.element) {
        log_error("dom_element_append_comment: failed to append");
        return nullptr;
    }

    // Update parent element pointer (INLINE mode keeps it stable, but update for consistency)
    parent->native_element = result.element;

    // Create DomComment wrapper
    int64_t child_index = result.element->length - 1;
    DomComment* comment_node = dom_comment_create(
        comment_item.element,
        parent,
        child_index
    );

    if (!comment_node) {
        log_error("dom_element_append_comment: failed to create DomComment");
        return nullptr;
    }

    // Add to DOM sibling chain
    comment_node->parent = parent;
    if (!parent->first_child) {
        parent->first_child = comment_node;
    } else {
        DomNode* last = parent->first_child;
        while (last->next_sibling) last = last->next_sibling;
        last->next_sibling = comment_node;
        comment_node->prev_sibling = last;
    }

    log_debug("dom_element_append_comment: appended comment '%s' at index %lld",
              comment_content, child_index);

    return comment_node;
}

bool dom_comment_remove(DomComment* comment_node) {
    if (!comment_node) {
        log_error("dom_comment_remove: null comment");
        return false;
    }

    DomElement* parent_elem = comment_node->parent_element;
    if (!comment_node->native_element || !parent_elem || !parent_elem->doc) {
        log_error("dom_comment_remove: comment not backed");
        return false;
    }

    // Remove from Lambda parent Element's children array
    MarkEditor editor(parent_elem->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = parent_elem->native_element},
        comment_node->child_index
    );

    if (!result.element) {
        log_error("dom_comment_remove: failed to delete child");
        return false;
    }

    // Update parent
    parent_elem->native_element = result.element;

    // Remove from DOM sibling chain
    if (comment_node->prev_sibling) {
        comment_node->prev_sibling->next_sibling = comment_node->next_sibling;
    } else if (comment_node->parent) {
        DomElement* elem_parent = static_cast<DomElement*>(comment_node->parent);
        elem_parent->first_child = comment_node->next_sibling;
    }

    if (comment_node->next_sibling) {
        comment_node->next_sibling->prev_sibling = comment_node->prev_sibling;
    }

    // Clear references
    comment_node->parent = nullptr;
    comment_node->native_element = nullptr;

    log_debug("dom_comment_remove: removed comment at index %lld", comment_node->child_index);

    return true;
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
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena) {
    if (!elem || !attr_name) return nullptr;
    ConstItem attr_value = elem->get_attr(attr_name);
    String* string_value = attr_value.string();
    return string_value ? string_value->chars : nullptr;
}

DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent) {
    if (!elem || !doc) {
        log_debug("build_dom_tree_from_element: Invalid arguments\n");
        return nullptr;
    }

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;
    log_debug("build element: <%s> (parent: %s)", tag_name,
              parent ? parent->tag_name : "none");

    // Skip comments and DOCTYPE - they will be created as DomComment nodes below
    if (strcmp(tag_name, "!--") == 0 || strcasecmp(tag_name, "!DOCTYPE") == 0) {
        return nullptr;  // Not a layout element, processed as child below
    }

    // Skip XML declarations
    if (strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip XML declarations
    }

    // skip script elements - they should not participate in layout
    // script elements have display: none by default in browser user-agent stylesheets
    if (strcasecmp(tag_name, "script") == 0) {
        return nullptr;  // Skip script elements during DOM tree building
    }

    // create DomElement
    DomElement* dom_elem = dom_element_create(doc, tag_name, elem);
    if (!dom_elem) return nullptr;

    // Cache id attribute from native element (if present)
    const char* id_value = extract_element_attribute(elem, "id", doc->arena);
    if (id_value) {
        dom_elem->id = id_value;  // Cache ID for fast access
    }

    const char* class_value = extract_element_attribute(elem, "class", doc->arena);
    if (class_value) {
        // parse multiple classes separated by spaces
        char* class_copy = (char*)arena_alloc(doc->arena, strlen(class_value) + 1);
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

    // Parse and apply inline style attribute
    const char* style_value = extract_element_attribute(elem, "style", doc->arena);
    if (style_value) {
        dom_element_apply_inline_style(dom_elem, style_value);
    }

    // extract rowspan and colspan attributes for table cells (td, th)
    if (strcasecmp(tag_name, "td") == 0 || strcasecmp(tag_name, "th") == 0) {
        const char* rowspan_value = extract_element_attribute(elem, "rowspan", doc->arena);
        if (rowspan_value) {
            dom_element_set_attribute(dom_elem, "rowspan", rowspan_value);
        }

        const char* colspan_value = extract_element_attribute(elem, "colspan", doc->arena);
        if (colspan_value) {
            dom_element_set_attribute(dom_elem, "colspan", colspan_value);
        }
    }

    // set parent relationship if provided
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

            // Check if this is a comment or DOCTYPE
            if (strcmp(child_tag_name, "!--") == 0 || strcasecmp(child_tag_name, "!DOCTYPE") == 0) {
                // Create DomComment node backed by Lambda Element
                DomComment* comment_node = dom_comment_create(child_elem, dom_elem, i);
                if (comment_node) {
                    comment_node->parent = dom_elem;

                    // Add to DOM sibling chain
                    if (!dom_elem->first_child) {
                        dom_elem->first_child = comment_node;
                        comment_node->prev_sibling = nullptr;
                        comment_node->next_sibling = nullptr;
                    } else {
                        DomNode* last = dom_elem->first_child;
                        while (last->next_sibling) last = last->next_sibling;
                        last->next_sibling = comment_node;
                        comment_node->prev_sibling = last;
                    }

                    log_debug("  Created comment node at index %lld: '%s'",
                              i, comment_node->content);
                }
                continue;  // Don't try to build as DomElement
            }

            log_debug("  Building child element: <%s> for parent <%s> (parent_dom=%p)", child_tag_name, tag_name, (void*)dom_elem);
            DomElement* child_dom = build_dom_tree_from_element(child_elem, doc, dom_elem);

            // skip if nullptr (e.g., script, XML declarations)
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
            // Text node - create DomText that references Lambda String
            String* text_str = (String*)child_item.pointer;
            if (text_str && text_str->len > 0) {
                // Create text node (preserves Lambda String reference)
                DomText* text_node = dom_text_create(text_str, dom_elem, i);
                if (text_node) {
                    text_node->parent = dom_elem;

                    // Add text node to DOM sibling chain
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

                    log_debug("  Created text node at index %lld: '%s' (len=%zu)", 
                              i, text_str->chars, text_str->len);
                }
            }
        }
    }

    return dom_elem;
}
