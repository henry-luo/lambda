// #define _POSIX_C_SOURCE 200809L
#include "dom_element.hpp"
#include "css_formatter.hpp"
#include "css_style_node.hpp"
#include "css_parser.hpp"
#include "../../../lib/hashmap.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/stringbuf.h"
#include "../../../lib/string.h"
#include "../../../lib/log.h"
#include "../../../lib/strview.h"
#include "../../../lib/str.h"
#include "../../../lib/arena.h"
#include "../../../lib/memtrack.h"
#include "../../lambda-data.hpp"  // For get_type_id, and proper type definitions
#include "../../mark_reader.hpp"  // For ElementReader
#include "../../mark_editor.hpp"  // For MarkEditor
#include "../../mark_builder.hpp" // For MarkBuilder
#include "../../../radiant/view.hpp"  // For HTM_TAG_* constants

// Forward declarations for counter system (avoid circular dependency)
typedef struct CounterContext CounterContext;
int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                  char* buffer, size_t buffer_size);
int counters_format(CounterContext* ctx, const char* name, const char* separator,
                   uint32_t style, char* buffer, size_t buffer_size);

// Timing accumulators for cascade profiling
static thread_local int64_t g_apply_decl_count = 0;

void reset_dom_element_timing() {
    g_apply_decl_count = 0;
}

void log_dom_element_timing() {
    log_info("[TIMING] cascade detail: decl_count: %lld", g_apply_decl_count);
}

// Forward declaration
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* document, DomElement* parent);
DomElement* build_dom_tree_from_element_with_input(Element* elem, DomDocument* document, DomElement* parent);

// helper: append a DomNode child to a parent's sibling chain
static void dom_append_to_sibling_chain(DomElement* parent, DomNode* child) {
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        DomNode* last = parent->last_child;
        if (!last) {
            last = parent->first_child;
            while (last->next_sibling) last = last->next_sibling;
        }
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
        parent->last_child = child;
    }
}

// helper: extract a name string from a CssValue (works for counter names, attr names, etc.)
static const char* css_value_extract_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) return value->data.custom_property.name;
    return nullptr;
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
    DomDocument* document = (DomDocument*)mem_calloc(1, sizeof(DomDocument), MEM_CAT_INPUT_CSS);
    if (!document) {
        log_error("dom_document_create: failed to allocate document");
        return nullptr;
    }

    // Create pool for arena chunks
    document->pool = pool_create();
    if (!document->pool) {
        log_error("dom_document_create: failed to create pool");
        mem_free(document);
        return nullptr;
    }

    // Create arena for all DOM node allocations
    document->arena = arena_create_default(document->pool);
    if (!document->arena) {
        log_error("dom_document_create: failed to create arena");
        pool_destroy(document->pool);
        mem_free(document);
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
    mem_free(document);
    log_debug("dom_document_destroy: destroyed document and arena");
}

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

DomElement* dom_element_create(DomDocument* doc, const char* tag_name, Element* native_element) {
    if (!doc || !tag_name) {
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
    if (!element || !doc || !tag_name) {
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
    element->last_child = NULL;
    element->doc = doc;

    // Copy Element data into the embedded field and redirect native_element
    if (native_element && native_element != &element->elmt) {
        element->elmt = *native_element;  // shallow copy (items[], type, data pointers are shared)
        element->native_element = &element->elmt;
    } else if (native_element) {
        // Self-reference (ui_mode: Element is already embedded in DomElement)
        element->native_element = &element->elmt;
    } else {
        element->native_element = NULL;
    }

    // Explicitly initialize display to {0, 0} to ensure no garbage values
    // This is critical for table elements where display resolution depends on this field
    element->display = {CSS_VALUE__UNDEF, CSS_VALUE__UNDEF};
    element->styles_resolved = false;

    // Copy tag name
    size_t tag_len = strlen(tag_name);
    char* tag_copy = (char*)arena_alloc(doc->arena, tag_len + 1);
    if (!tag_copy) {
        return false;
    }
    str_copy(tag_copy, tag_len + 1, tag_name, tag_len);
    element->tag_name = tag_copy;

    // Convert tag name to Lexbor tag ID for fast comparison
    element->tag_id = DomNode::tag_name_to_id(tag_name);

    // Create style trees (still use pool for AVL nodes)
    element->specified_style = style_tree_create(doc->pool);
    if (!element->specified_style) {
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
                    str_copy(class_copy, strlen(class_str) + 1, class_str, strlen(class_str));

                    int index = 0;
                    char* token = strtok(class_copy, " \t\n\r");
                    while (token && index < count) {
                        // Allocate permanent copy of each class from arena
                        size_t token_len = strlen(token);
                        char* class_perm = (char*)arena_alloc(doc->arena, token_len + 1);
                        if (class_perm) {
                            str_copy(class_perm, token_len + 1, token, token_len);
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

// Helper: lowercase attribute name into buffer (HTML5 spec: attribute names are case-insensitive)
static const char* lowercase_attr_name(const char* name, char* buf, size_t buf_size) {
    size_t i = 0;
    for (; name[i] && i < buf_size - 1; i++) {
        buf[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 0x20) : name[i];
    }
    buf[i] = '\0';
    return buf;
}

bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (!element || !name || !value) {
        log_debug("dom_element_set_attribute: invalid parameters");
        return false;
    }

    // HTML5: attribute names are case-insensitive, store lowercased
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    // If native_element exists, use MarkEditor for updates
    if (element->native_element && element->doc) {
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);

        // Create string value item
        Item value_item = editor.builder()->createStringItem(value);

        // Update attribute via MarkEditor
        Item result = editor.elmt_update_attr(
            {.element = element->native_element},
            lower_name,
            value_item
        );

        if (result.element) {
            // In INLINE mode, element pointer remains the same (in-place mutation)
            // In IMMUTABLE mode, a new element would be created
            // Since we're using INLINE mode, this assignment is a no-op but kept for consistency
            element->native_element = result.element;

            // Handle special attributes
            if (strcmp(lower_name, "id") == 0) {
                // Cache ID for fast access
                ElementReader reader(element->native_element);
                element->id = reader.get_attr_string("id");
            } else if (strcmp(lower_name, "class") == 0) {
                // Parse space-separated classes
                // First, clear existing classes
                element->class_count = 0;
                element->class_names = nullptr;

                // Parse and add each class
                if (value && strlen(value) > 0) {
                    char* class_copy = (char*)arena_alloc(element->doc->arena, strlen(value) + 1);
                    if (class_copy) {
                        str_copy(class_copy, strlen(value) + 1, value, strlen(value));

                        // Split by spaces and add each class
                        char* token = strtok(class_copy, " \t\n\r");
                        while (token) {
                            if (strlen(token) > 0) {
                                dom_element_add_class(element, token);
                            }
                            token = strtok(nullptr, " \t\n\r");
                        }
                    }
                }
            } else if (strcmp(lower_name, "style") == 0) {
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

    // HTML5 stores attribute names lowercased. CSS attr() may pass mixed-case.
    // Lowercase the lookup key for case-insensitive matching per CSS spec.
    char lower_name[128];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower_name) - 1; i++) {
        lower_name[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 0x20) : name[i];
    }
    lower_name[i] = '\0';

    // Use ElementReader for read-only access
    if (element->native_element) {
        // Try shape-typed fast path first (covers fields with compile-time LMD_TYPE_STRING)
        ElementReader reader(element->native_element);
        const char* result = reader.get_attr_string(lower_name);
        if (result) return result;

        // Fallback: check runtime Item type (handles fields where compile-time type
        // differs from runtime type, e.g. state-bound template attributes)
        ConstItem attr_value = element->native_element->get_attr(lower_name);
        String* string_value = attr_value.string();
        if (string_value) return string_value->chars;
    }

    return nullptr;
}

bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (!element || !name) {
        return false;
    }

    // HTML5: attribute names are case-insensitive
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    if (element->native_element && element->doc) {
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);

        // Delete attribute via MarkEditor
        Item result = editor.elmt_delete_attr(
            {.element = element->native_element},
            lower_name
        );

        if (get_type_id(result) == LMD_TYPE_ELEMENT && result.element) {
            // In INLINE mode, element pointer remains the same
            // This assignment is a no-op but kept for consistency
            element->native_element = result.element;

            // Clear cached fields
            if (strcmp(lower_name, "id") == 0) {
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

    // HTML5: attribute names are case-insensitive
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    if (element->native_element) {
        ElementReader reader(element->native_element);
        return reader.has_attr(lower_name);
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
    str_copy(class_copy, class_len + 1, class_name, class_len);

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
    size_t style_len = strlen(style_text);
    char* text_copy = (char*)arena_alloc(element->doc->arena, style_len + 1);
    if (!text_copy) {
        return 0;
    }

    // Copy text for in-place modification, preserving CSS comments intact.
    // Comments inside custom property values must be preserved per CSS spec.
    // We split by semicolons that are NOT inside comments.
    memcpy(text_copy, style_text, style_len);
    text_copy[style_len] = '\0';

    // Find semicolons not inside comments and replace them with NUL for splitting
    {
        size_t i = 0;
        while (i < style_len) {
            if (i + 1 < style_len && text_copy[i] == '/' && text_copy[i + 1] == '*') {
                // inside comment — skip until closing */
                i += 2;
                while (i + 1 < style_len && !(text_copy[i] == '*' && text_copy[i + 1] == '/')) {
                    i++;
                }
                if (i + 1 < style_len) i += 2; // skip */
            } else if (text_copy[i] == ';') {
                text_copy[i] = '\0';  // split point
                i++;
            } else {
                i++;
            }
        }
    }

    // Iterate over NUL-separated declarations
    size_t offset = 0;
    while (offset < style_len) {
        char* declaration_str = text_copy + offset;
        // advance offset past this segment (to next NUL or end)
        size_t seg_len = strlen(declaration_str);
        offset += seg_len + 1; // +1 for the NUL separator
        // Trim leading whitespace
        while (*declaration_str == ' ' || *declaration_str == '\t' ||
               *declaration_str == '\n' || *declaration_str == '\r') {
            declaration_str++;
        }

        // Skip empty declarations
        if (*declaration_str == '\0') {
            continue;
        }

        // Find the colon separator
        char* colon = strchr(declaration_str, ':');
        if (!colon) {
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

        // Parse the property using the proper CSS tokenizer and parser
        // Format the declaration string for parsing
        size_t decl_str_len = strlen(prop_name) + strlen(prop_value) + 3; // "name: value"
        char* decl_str = (char*)arena_alloc(element->doc->arena, decl_str_len);
        if (decl_str) {
            snprintf(decl_str, decl_str_len, "%s:%s", prop_name, prop_value);

            // Tokenize the declaration
            size_t token_count = 0;
            CssToken* tokens = css_tokenize(decl_str, strlen(decl_str), element->doc->pool, &token_count);

            if (tokens && token_count > 0) {
                int pos = 0;
                CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, token_count, element->doc->pool);

                if (decl) {
                    // Set origin to author (inline styles are author origin)
                    decl->origin = CSS_ORIGIN_AUTHOR;

                    // Set inline style specificity (1,0,0,0)
                    decl->specificity.inline_style = 1;
                    decl->specificity.ids = 0;
                    decl->specificity.classes = 0;
                    decl->specificity.elements = 0;
                    decl->specificity.important = decl->important; // preserve !important for cascade

                    // Apply to element
                    bool applied = dom_element_apply_declaration(element, decl);
                    if (applied) {
                        applied_count++;
                    }
                }
            }
        }
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

    g_apply_decl_count++;

    // Check if this is a custom property (CSS variable)
    if (declaration->property_name &&
        declaration->property_name[0] == '-' &&
        declaration->property_name[1] == '-') {

        // Custom property - store in linked list
        log_info("[CSS] Storing custom property: %s", declaration->property_name);

        CssCustomProp* prop = (CssCustomProp*)pool_calloc(element->doc->pool, sizeof(CssCustomProp));
        if (!prop) {
            log_error("[CSS] Failed to allocate CssCustomProp");
            return false;
        }

        prop->name = declaration->property_name;
        prop->value = declaration->value;
        prop->value_text = declaration->value_text;
        prop->value_text_len = declaration->value_text_len;
        prop->next = element->css_variables;
        element->css_variables = prop;

        // Increment style version to invalidate caches
        element->style_version++;
        element->needs_style_recompute = true;

        return true;
    }

    // Validate the property value before applying
    if (!css_property_validate_value(declaration->property_id, declaration->value)) {
        return false;
    }

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
                decl->specificity.important = decl->important; // preserve !important for cascade
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
// Pseudo-Element Style Management (::before, ::after)
// ============================================================================

int dom_element_apply_pseudo_element_rule(DomElement* element, CssRule* rule,
                                          CssSpecificity specificity, int pseudo_element) {
    log_debug("[CSS-PSEUDO] Applying pseudo-element rule to <%s>, pseudo_type=%d",
              element ? element->tag_name : "NULL", pseudo_element);

    if (!element || !rule || !element->doc) {
        log_debug("[CSS-PSEUDO] Early return due to null element/rule/doc");
        return 0;
    }

    // Get the appropriate style tree for the pseudo-element
    StyleTree** target_style = nullptr;
    const char* pseudo_name = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        target_style = &element->before_styles;
        pseudo_name = "::before";
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        target_style = &element->after_styles;
        pseudo_name = "::after";
    } else if (pseudo_element == 4) {  // PSEUDO_ELEMENT_FIRST_LETTER
        target_style = &element->first_letter_styles;
        pseudo_name = "::first-letter";
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        target_style = &element->marker_styles;
        pseudo_name = "::marker";
    } else {
        log_debug("[CSS] Unknown pseudo-element type: %d", pseudo_element);
        return 0;
    }

    // Create style tree if needed
    if (!*target_style) {
        *target_style = style_tree_create(element->doc->pool);
        if (!*target_style) {
            log_error("[CSS] Failed to create style tree for %s", pseudo_name);
            return 0;
        }
    }

    int applied_count = 0;

    // Apply each declaration from the rule
    if (rule->type == CSS_RULE_STYLE && rule->data.style_rule.declarations) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl) {
                // Update declaration's specificity to match the selector
                decl->specificity = specificity;
                decl->specificity.important = decl->important; // preserve !important for cascade
                decl->origin = rule->origin;

                // Apply to pseudo-element style tree
                if (style_tree_apply_declaration(*target_style, decl)) {
                    applied_count++;
                    log_debug("[CSS] Applied %s property %d to <%s>",
                              pseudo_name, decl->property_id, element->tag_name);
                }
            }
        }
    }

    if (applied_count > 0) {
        element->style_version++;
        element->needs_style_recompute = true;
    }

    return applied_count;
}

CssDeclaration* dom_element_get_pseudo_element_value(DomElement* element,
                                                     CssPropertyId property_id, int pseudo_element) {
    if (!element) {
        return NULL;
    }

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->before_styles;
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->after_styles;
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->marker_styles;
    }

    if (!style) {
        return NULL;
    }

    return style_tree_get_declaration(style, property_id);
}

bool dom_element_has_before_content(DomElement* element) {
    if (!element || !element->before_styles) {
        return false;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(
        element->before_styles, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        return false;
    }

    // Check if content value is not 'none' or 'normal'
    CssValue* value = content_decl->value;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_NONE ||
            value->data.keyword == CSS_VALUE_NORMAL) {
            return false;
        }
    }

    return true;
}

bool dom_element_has_after_content(DomElement* element) {
    if (!element || !element->after_styles) {
        return false;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(
        element->after_styles, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        return false;
    }

    // Check if content value is not 'none' or 'normal'
    CssValue* value = content_decl->value;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_NONE ||
            value->data.keyword == CSS_VALUE_NORMAL) {
            return false;
        }
    }

    return true;
}

/**
 * Resolve a CSS quote character (open-quote or close-quote) for an element.
 * Walks up the DOM tree to find the 'quotes' property, parses quote pairs,
 * and returns the appropriate character at the given depth.
 * CSS 2.1 §12.3.2: Quotes are nested; depth determines which pair to use.
 *
 * @param element The element with the ::before/::after pseudo-element
 * @param is_open_quote true for open-quote, false for close-quote
 * @param depth Quote nesting depth (0 = outermost)
 * @return The quote character string, or default quotes if none specified
 */
static const char* resolve_quote_char(DomElement* element, bool is_open_quote, int depth) {
    // Walk up DOM tree to find the 'quotes' property (it's inherited)
    DomElement* cur = element;
    CssDeclaration* quotes_decl = NULL;
    while (cur) {
        quotes_decl = dom_element_get_specified_value(cur, CSS_PROPERTY_QUOTES);
        if (quotes_decl && quotes_decl->value) break;
        cur = dom_element_get_parent(cur);
    }

    if (!quotes_decl || !quotes_decl->value) {
        // Default quotes per CSS 2.1: use typographic quotes
        return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";  // U+201C / U+201D
    }

    CssValue* qval = quotes_decl->value;

    // quotes: none
    if (qval->type == CSS_VALUE_TYPE_KEYWORD && qval->data.keyword == CSS_VALUE_NONE) {
        return "";
    }

    // quotes: "open1" "close1" "open2" "close2" ...
    // Parsed as a CSS_VALUE_TYPE_LIST of strings
    if (qval->type == CSS_VALUE_TYPE_LIST && qval->data.list.count >= 2) {
        int pair_count = qval->data.list.count / 2;
        int pair_index = depth < pair_count ? depth : pair_count - 1;  // CSS 2.1: use last pair for deeper nesting
        int str_index = pair_index * 2 + (is_open_quote ? 0 : 1);
        if (str_index < qval->data.list.count) {
            CssValue* sv = qval->data.list.values[str_index];
            if (sv && sv->type == CSS_VALUE_TYPE_STRING && sv->data.string) {
                log_debug("[QUOTES] depth=%d, pair=%d, %s='%s'",
                    depth, pair_index, is_open_quote ? "open" : "close", sv->data.string);
                return sv->data.string;
            }
        }
    }

    // quotes: "open" "close" (single pair, may be stored as 2 strings in a list or other format)
    if (qval->type == CSS_VALUE_TYPE_STRING && qval->data.string) {
        // Single string — shouldn't normally happen for quotes, but handle gracefully
        return qval->data.string;
    }

    return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";  // fallback
}

/**
 * Check if a CssValue represents an open-quote or close-quote content value.
 * Returns 1 for open-quote, 2 for close-quote, 0 for neither.
 */
static int check_quote_content(CssValue* value) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        if (strcmp(value->data.custom_property.name, "open-quote") == 0) return 1;
        if (strcmp(value->data.custom_property.name, "close-quote") == 0) return 2;
        if (strcmp(value->data.custom_property.name, "no-open-quote") == 0) return 3;
        if (strcmp(value->data.custom_property.name, "no-close-quote") == 0) return 4;
    }
    return 0;
}

const char* dom_element_get_pseudo_element_content(DomElement* element, int pseudo_element) {
    log_info("[PSEUDO CONTENT GET ENTRY] element=%p, pseudo=%d, tag=%s",
        (void*)element, pseudo_element,
        element ? (element->tag_name ? element->tag_name : "?") : "NULL");

    if (!element) {
        log_info("[PSEUDO CONTENT GET] element is NULL, returning NULL");
        return NULL;
    }

    log_info("[PSEUDO CONTENT GET] Called for element <%s>, pseudo=%d",
        element->tag_name ? element->tag_name : "?", pseudo_element);

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->before_styles;
        log_info("[PSEUDO CONTENT GET] before_styles=%p", (void*)style);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->after_styles;
        log_info("[PSEUDO CONTENT GET] after_styles=%p", (void*)style);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->marker_styles;
        log_info("[PSEUDO CONTENT GET] marker_styles=%p", (void*)style);
    }    if (!style) {
        log_info("[PSEUDO CONTENT GET] No style tree found");
        return NULL;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(style, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        log_info("[PSEUDO CONTENT GET] No content declaration found");
        return NULL;
    }

    CssValue* value = content_decl->value;
    log_info("[PSEUDO CONTENT GET] Found content value, type=%d", value->type);

    // Return the string content
    if (value->type == CSS_VALUE_TYPE_STRING) {
        const char* str = value->data.string;
        size_t len = str ? strlen(str) : 0;
        log_info("[PSEUDO CONTENT] Extracted STRING content, len=%zu, bytes=[%02x %02x %02x]",
            len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
        return str;
    }

    // Handle attr() function: content: attr(attribute-name)
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* func = value->data.function;
        if (func && func->name && strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
            // extract the attribute name from the first argument
            const char* attr_name = NULL;
            CssValue* arg0 = func->args[0];
            if (arg0) {
                if (arg0->type == CSS_VALUE_TYPE_STRING) {
                    attr_name = arg0->data.string;
                } else if (arg0->type == CSS_VALUE_TYPE_KEYWORD) {
                    // known keyword used as attribute name (e.g., "class")
                    const CssEnumInfo* info = css_enum_info(arg0->data.keyword);
                    attr_name = info ? info->name : NULL;
                } else if (arg0->type == CSS_VALUE_TYPE_CUSTOM) {
                    // unknown ident parsed as custom property (e.g., "data-val")
                    attr_name = arg0->data.custom_property.name;
                }
            }
            if (attr_name) {
                const char* attr_value = dom_element_get_attribute(element, attr_name);
                log_info("[PSEUDO CONTENT] attr(%s) => '%s'", attr_name, attr_value ? attr_value : "NULL");
                return attr_value ? attr_value : "";
            }
        }
    }

    // Handle attr() via CSS_VALUE_TYPE_ATTR (parsed by CSS value parser)
    if (value->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = value->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = dom_element_get_attribute(element, attr_ref->name);
            log_info("[PSEUDO CONTENT] attr(%s) => '%s'", attr_ref->name, attr_value ? attr_value : "NULL");
            return attr_value ? attr_value : "";
        }
    }

    // Handle open-quote / close-quote (CSS_VALUE_TYPE_CUSTOM with ident name)
    int quote_type = check_quote_content(value);
    if (quote_type == 1 || quote_type == 2) {
        return resolve_quote_char(element, quote_type == 1, 0);
    }
    if (quote_type == 3 || quote_type == 4) {
        return "";  // no-open-quote / no-close-quote: affect depth only, generate nothing
    }

    // Handle list of values (for content with multiple parts)
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        // For now, return the first string value
        CssValue* first = value->data.list.values[0];
        if (first && first->type == CSS_VALUE_TYPE_STRING) {
            const char* str = first->data.string;
            size_t len = str ? strlen(str) : 0;
            log_info("[PSEUDO CONTENT] Extracted LIST content, len=%zu, bytes=[%02x %02x %02x]",
                len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
            return str;
        }
    }

    log_info("[PSEUDO CONTENT] No string content found (type=%d)", value->type);
    return NULL;
}

/**
 * Get pseudo-element content with counter resolution
 * This version handles counter() and counters() functions
 */
const char* dom_element_get_pseudo_element_content_with_counters(
    DomElement* element, int pseudo_element, void* counter_context, Arena* arena) {

    log_info("[PSEUDO CONTENT WITH COUNTERS] Called: element=%p <%s>, pseudo=%d",
        (void*)element, element ? (element->tag_name ? element->tag_name : "?") : "NULL", pseudo_element);

    if (!element || !arena) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] element or arena is NULL");
        return NULL;
    }

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->before_styles;
        log_info("[PSEUDO CONTENT WITH COUNTERS] before_styles=%p", (void*)style);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->after_styles;
        log_info("[PSEUDO CONTENT WITH COUNTERS] after_styles=%p", (void*)style);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->marker_styles;
        log_info("[PSEUDO CONTENT WITH COUNTERS] marker_styles=%p", (void*)style);
    }

    if (!style) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] No style tree");
        return NULL;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(style, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] No content declaration");
        return NULL;
    }

    CssValue* value = content_decl->value;
    log_info("[PSEUDO CONTENT WITH COUNTERS] Found content value, type=%d", value->type);

    // Return string content directly
    if (value->type == CSS_VALUE_TYPE_STRING) {
        const char* str = value->data.string;
        size_t len = str ? strlen(str) : 0;
        log_info("[PSEUDO CONTENT WITH COUNTERS] STRING content, len=%zu, bytes=[%02x %02x %02x]",
            len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
        return str;
    }

    // Handle attr() via CSS_VALUE_TYPE_ATTR
    if (value->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = value->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = dom_element_get_attribute(element, attr_ref->name);
            log_info("[PSEUDO CONTENT WITH COUNTERS] attr(%s) => '%s'", attr_ref->name, attr_value ? attr_value : "NULL");
            return attr_value ? attr_value : "";
        }
    }

    // Handle open-quote / close-quote (CSS_VALUE_TYPE_CUSTOM with ident name)
    {
        int quote_type = check_quote_content(value);
        if (quote_type == 1 || quote_type == 2) {
            return resolve_quote_char(element, quote_type == 1, 0);
        }
        if (quote_type == 3 || quote_type == 4) {
            return "";  // no-open-quote / no-close-quote
        }
    }

    // Handle counter() or counters() function
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* func = value->data.function;
        log_debug("[Counter] Found function in content: %s (arg_count=%d)",
                 func ? func->name : "NULL", func ? func->arg_count : 0);
        if (func && func->name && counter_context) {
            if (strcmp(func->name, "counter") == 0) {
                // counter(name) or counter(name, style)
                // Parse arguments: name (identifier), optional style (keyword)
                if (func->arg_count >= 1) {
                    log_debug("[Counter] counter() arg[0] type=%d", (int)func->args[0]->type);

                    const char* counter_name = css_value_extract_name(func->args[0]);

                    uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)

                    if (func->arg_count >= 2 && func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                        style_type = func->args[1]->data.keyword;
                        const CssEnumInfo* style_info = css_enum_info((CssEnum)style_type);
                        log_debug("[Counter] counter style keyword: %u (%s)",
                                style_type, style_info ? style_info->name : "unknown");
                    }

                    // Format counter value
                    char* buffer = (char*)arena_alloc(arena, 64);
                    if (buffer && counter_name) {
                        counter_format((CounterContext*)counter_context, counter_name,
                                     style_type, buffer, 64);
                        log_debug("[Counter] counter(%s, style=%u) = '%s'", counter_name, style_type, buffer);
                        return buffer;
                    }
                }
            } else if (strcmp(func->name, "counters") == 0) {
                // counters(name, separator) or counters(name, separator, style)
                if (func->arg_count >= 2) {
                    const char* counter_name = css_value_extract_name(func->args[0]);

                    const char* separator = func->args[1]->data.string;
                    uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)

                    if (func->arg_count >= 3 && func->args[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                        style_type = func->args[2]->data.keyword;
                    }

                    // Format counters with separator
                    char* buffer = (char*)arena_alloc(arena, 128);
                    if (buffer && counter_name) {
                        counters_format((CounterContext*)counter_context, counter_name,
                                      separator ? separator : ".", style_type, buffer, 128);
                        log_debug("[Counter] counters(%s, \"%s\") = %s",
                                counter_name, separator ? separator : ".", buffer);
                        return buffer;
                    }
                }
            } else if (strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
                // attr(attribute-name) in content property
                const char* attr_name = css_value_extract_name(func->args[0]);
                if (attr_name) {
                    const char* attr_value = dom_element_get_attribute(element, attr_name);
                    log_info("[PSEUDO CONTENT WITH COUNTERS] attr(%s) => '%s'", attr_name, attr_value ? attr_value : "NULL");
                    return attr_value ? attr_value : "";
                }
            }
        }
    }

    // Handle list of values (for content with multiple parts, e.g., counter(c) "text")
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        log_debug("[Counter] Processing content list with %d values", value->data.list.count);

        // Use a fixed-size buffer for concatenation
        char result_buffer[512];
        result_buffer[0] = '\0';
        int result_len = 0;

        // Concatenate all values in the list
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item) continue;

            if (item->type == CSS_VALUE_TYPE_STRING && item->data.string) {
                // Append string content
                int str_len = strlen(item->data.string);
                if (result_len + str_len < (int)sizeof(result_buffer) - 1) {
                    memcpy(result_buffer + result_len, item->data.string, str_len);
                    result_len += str_len;
                    result_buffer[result_len] = '\0';
                    log_debug("[Counter] Appended string: '%s'", item->data.string);
                }
            } else if (item->type == CSS_VALUE_TYPE_FUNCTION) {
                // Handle counter() or counters() in list
                CssFunction* func = item->data.function;
                log_debug("[Counter] Processing function in list: %s", func ? func->name : "NULL");
                if (func && func->name && counter_context) {
                    char temp_buffer[128];
                    temp_buffer[0] = '\0';

                    if (strcmp(func->name, "counter") == 0 && func->arg_count >= 1) {
                        const char* counter_name = css_value_extract_name(func->args[0]);

                        uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)
                        if (func->arg_count >= 2 && func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                            style_type = func->args[1]->data.keyword;
                            const CssEnumInfo* style_info = css_enum_info((CssEnum)style_type);
                            log_debug("[Counter] counter style keyword: %u (%s)",
                                    style_type, style_info ? style_info->name : "unknown");
                        }

                        if (counter_name) {
                            counter_format((CounterContext*)counter_context, counter_name,
                                         style_type, temp_buffer, sizeof(temp_buffer));
                            int temp_len = strlen(temp_buffer);
                            if (result_len + temp_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, temp_buffer, temp_len);
                                result_len += temp_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] counter(%s, style=%u) = '%s'", counter_name, style_type, temp_buffer);
                        }
                    } else if (strcmp(func->name, "counters") == 0 && func->arg_count >= 2) {
                        const char* counter_name = css_value_extract_name(func->args[0]);

                        const char* separator = func->args[1]->data.string;
                        uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)
                        if (func->arg_count >= 3 && func->args[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                            style_type = func->args[2]->data.keyword;
                        }

                        if (counter_name) {
                            counters_format((CounterContext*)counter_context, counter_name,
                                          separator ? separator : ".", style_type,
                                          temp_buffer, sizeof(temp_buffer));
                            int temp_len = strlen(temp_buffer);
                            if (result_len + temp_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, temp_buffer, temp_len);
                                result_len += temp_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] counters(%s, '%s', style=%u) = '%s'",
                                    counter_name, separator ? separator : ".", style_type, temp_buffer);
                        }
                    }
                }
                if (func && func->name && strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
                    // Handle attr() function in list
                    const char* attr_name = css_value_extract_name(func->args[0]);
                    if (attr_name) {
                        const char* attr_value = dom_element_get_attribute(element, attr_name);
                        if (attr_value) {
                            int attr_len = strlen(attr_value);
                            if (result_len + attr_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, attr_value, attr_len);
                                result_len += attr_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] Appended attr(%s) = '%s'", attr_name, attr_value);
                        }
                    }
                }
            } else if (item->type == CSS_VALUE_TYPE_ATTR) {
                // Handle attr() in list: content: "text" attr(class) "more text"
                CSSAttrRef* attr_ref = item->data.attr_ref;
                if (attr_ref && attr_ref->name) {
                    const char* attr_value = dom_element_get_attribute(element, attr_ref->name);
                    if (attr_value) {
                        int attr_len = strlen(attr_value);
                        if (result_len + attr_len < (int)sizeof(result_buffer) - 1) {
                            memcpy(result_buffer + result_len, attr_value, attr_len);
                            result_len += attr_len;
                            result_buffer[result_len] = '\0';
                        }
                        log_debug("[Counter] Appended attr(%s) = '%s'", attr_ref->name, attr_value);
                    }
                }
            } else {
                // Handle open-quote / close-quote in list
                int qt = check_quote_content(item);
                if (qt == 1 || qt == 2) {
                    // Track depth based on position of quote in the list
                    int quote_depth = 0;
                    for (int j = 0; j < i; j++) {
                        int prev_qt = check_quote_content(value->data.list.values[j]);
                        if (prev_qt == 1 || prev_qt == 3) quote_depth++;
                    }
                    const char* qc = resolve_quote_char(element, qt == 1, quote_depth);
                    if (qc) {
                        int qlen = strlen(qc);
                        if (result_len + qlen < (int)sizeof(result_buffer) - 1) {
                            memcpy(result_buffer + result_len, qc, qlen);
                            result_len += qlen;
                            result_buffer[result_len] = '\0';
                        }
                        log_debug("[Counter] Appended %s = '%s'", qt == 1 ? "open-quote" : "close-quote", qc);
                    }
                }
                // no-open-quote / no-close-quote: generate nothing but affect depth
            }
        }

        // Copy result to arena-allocated buffer
        if (result_len > 0) {
            char* result = (char*)arena_alloc(arena, result_len + 1);
            if (result) {
                memcpy(result, result_buffer, result_len);
                result[result_len] = '\0';
                log_debug("[Counter] Final content: '%s'", result);
                return result;
            }
        }
    }

    return NULL;
}

// ============================================================================
// Pseudo-Class State Management
// ============================================================================

void dom_element_set_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return;
    }

    uint32_t old_state = element->pseudo_state;
    element->pseudo_state |= pseudo_state;

    // If pseudo-state actually changed, invalidate styles
    if (element->pseudo_state != old_state) {
        element->needs_style_recompute = true;
        element->style_version++;
        log_debug("dom_element_set_pseudo_state: %s added state 0x%x, invalidated style",
                  element->tag_name, pseudo_state);
    }
}

void dom_element_clear_pseudo_state(DomElement* element, uint32_t pseudo_state) {
    if (!element) {
        return;
    }

    uint32_t old_state = element->pseudo_state;
    element->pseudo_state &= ~pseudo_state;

    // If pseudo-state actually changed, invalidate styles
    if (element->pseudo_state != old_state) {
        element->needs_style_recompute = true;
        element->style_version++;
        log_debug("dom_element_clear_pseudo_state: %s removed state 0x%x, invalidated style",
                  element->tag_name, pseudo_state);
    }
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

DomElement* dom_element_get_last_child(DomElement* element) {
    return element ? static_cast<DomElement*>(element->last_child) : NULL;
}

DomElement* dom_element_get_next_sibling(DomElement* element) {
    return element ? static_cast<DomElement*>(element->next_sibling) : NULL;
}

DomElement* dom_element_get_prev_sibling(DomElement* element) {
    return element ? static_cast<DomElement*>(element->prev_sibling) : NULL;
}

/**
 * Link child element to parent in DOM sibling chain only.
 * Use this when the child is ALREADY in the parent's Lambda tree.
 * Does NOT modify the Lambda tree - only updates DOM navigation pointers.
 *
 * @param parent Parent element
 * @param child Child element to link (must already exist in parent's Lambda tree)
 * @return true on success, false on error
 */
bool dom_element_link_child(DomElement* parent, DomElement* child) {
    if (!parent || !child) {
        log_error("dom_element_link_child: invalid arguments");
        return false;
    }

    // Add to parent's DOM sibling chain
    dom_append_to_sibling_chain(parent, child);

    log_debug("dom_element_link_child: linked child to DOM chain (Lambda tree unchanged)");
    return true;
}

/**
 * Append child element to parent, updating BOTH Lambda tree AND DOM sibling chain.
 * Use this when adding a NEW child that is NOT yet in the parent's Lambda tree.
 *
 * For children already in the Lambda tree (e.g., when building DOM wrappers from
 * existing Lambda structures), use dom_element_link_child() instead.
 *
 * @param parent Parent element (must have Lambda backing)
 * @param child Child element (must have Lambda backing)
 * @return true on success, false on error
 */
bool dom_element_append_child(DomElement* parent, DomElement* child) {
    if (!parent || !child) {
        log_error("dom_element_append_child: invalid arguments");
        return false;
    }

    // Require Lambda backing for both parent and child
    if (!parent->native_element || !child->native_element || !parent->doc) {
        log_error("dom_element_append_child: parent and child must have Lambda backing");
        return false;
    }

    log_debug("dom_element_append_child: appending to Lambda tree (length before=%lld)", parent->native_element->length);

    // Append to Lambda tree using MarkEditor
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_append_child(
        {.element = parent->native_element},
        {.element = child->native_element}
    );

    if (!result.element) {
        log_error("dom_element_append_child: failed to append to Lambda tree");
        return false;
    }

    // Update parent element pointer (INLINE mode modifies in place, but update for consistency)
    parent->native_element = result.element;
    log_debug("dom_element_append_child: Lambda tree updated (length after=%lld)", parent->native_element->length);

    // Update DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already linked)
    if (!parent->doc->input->ui_mode) {
        dom_append_to_sibling_chain(parent, child);
    }

    log_debug("dom_element_append_child: appended element to parent (both Lambda tree and DOM chain updated)");

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
    } else {
        // Child was last child
        parent->last_child = child->prev_sibling;
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

    // If inserting before first child, update last_child if needed
    if (!new_child->next_sibling) {
        parent->last_child = new_child;
    }

    // Invalidate new child's computed values
    // dom_element_invalidate_computed_values(new_child, true);

    return true;
}

bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child) {
    if (!parent || !old_child || !new_child) return false;
    if (old_child->parent != parent) return false;

    // splice new_child into old_child's position in the linked list
    new_child->parent = parent;
    new_child->prev_sibling = old_child->prev_sibling;
    new_child->next_sibling = old_child->next_sibling;

    if (old_child->prev_sibling) {
        old_child->prev_sibling->next_sibling = new_child;
    } else {
        parent->first_child = new_child;
    }

    if (old_child->next_sibling) {
        old_child->next_sibling->prev_sibling = new_child;
    } else {
        parent->last_child = new_child;
    }

    old_child->parent = nullptr;
    old_child->prev_sibling = nullptr;
    old_child->next_sibling = nullptr;
    return true;
}

// ============================================================================
// Structural Queries
// ============================================================================

bool dom_element_is_first_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    // CSS 2.1 §5.11.1: :first-child matches an element that is the first
    // child ELEMENT of its parent. Text nodes are not counted.
    DomElement* parent = static_cast<DomElement*>(element->parent);
    DomNode* child = parent->first_child;
    while (child) {
        if (child->is_element()) {
            return child == (DomNode*)element;
        }
        child = child->next_sibling;
    }
    return false;
}

bool dom_element_is_last_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    // CSS 2.1 §5.11.1: :last-child matches an element that is the last
    // child ELEMENT of its parent. Text nodes after it are not counted.
    DomNode* sibling = element->next_sibling;
    while (sibling) {
        if (sibling->is_element()) {
            return false;
        }
        sibling = sibling->next_sibling;
    }
    return true;
}

bool dom_element_is_only_child(DomElement* element) {
    if (!element || !element->parent) {
        return false;
    }

    // CSS Selectors §6.6.1.6: :only-child matches when the element is the
    // only child ELEMENT of its parent. Equivalent to :first-child:last-child.
    return dom_element_is_first_child(element) && dom_element_is_last_child(element);
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

    // Copy pseudo state
    clone->pseudo_state = source->pseudo_state;

    // Note: Children are not cloned - caller should handle that if needed

    return clone;
}// ============================================================================
// DOM Text Node Implementation
// ============================================================================

DomText* dom_text_create(String* native_string, DomElement* parent_element) {
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
    text_node->content_type = DOM_TEXT_STRING;  // Plain text
    text_node->parent = parent_element;

    log_debug("dom_text_create: created backed text node, text='%s'", native_string->chars);
    return text_node;
}

DomText* dom_text_create_detached(String* native_string, DomDocument* doc) {
    if (!native_string) {
        log_error("dom_text_create_detached: native_string required");
        return nullptr;
    }
    if (!doc || !doc->arena) {
        log_error("dom_text_create_detached: doc with arena required");
        return nullptr;
    }

    DomText* text_node = (DomText*)arena_calloc(doc->arena, sizeof(DomText));
    if (!text_node) {
        log_error("dom_text_create_detached: arena_calloc failed");
        return nullptr;
    }

    text_node->node_type = DOM_NODE_TEXT;
    text_node->parent = nullptr;
    text_node->next_sibling = nullptr;
    text_node->prev_sibling = nullptr;

    text_node->native_string = native_string;
    text_node->text = native_string->chars;
    text_node->length = native_string->len;
    text_node->content_type = DOM_TEXT_STRING;

    log_debug("dom_text_create_detached: created text node, text='%s'", native_string->chars);
    return text_node;
}

DomText* dom_text_create_symbol(const char* name, size_t len, DomElement* parent_element) {
    if (!name || len == 0 || !parent_element) {
        log_error("dom_text_create_symbol: name and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("dom_text_create_symbol: parent_element has no document");
        return nullptr;
    }

    // Allocate from parent's document arena
    DomText* text_node = (DomText*)arena_calloc(parent_element->doc->arena, sizeof(DomText));
    if (!text_node) {
        log_error("dom_text_create_symbol: arena_calloc failed");
        return nullptr;
    }

    // Initialize base DomNode fields
    text_node->node_type = DOM_NODE_TEXT;
    text_node->parent = parent_element;
    text_node->next_sibling = nullptr;
    text_node->prev_sibling = nullptr;

    // Set symbol name directly (Symbol and String have different layouts)
    text_node->native_string = nullptr;
    text_node->text = name;
    text_node->length = len;
    text_node->content_type = DOM_TEXT_SYMBOL;
    text_node->parent = parent_element;

    log_debug("dom_text_create_symbol: created symbol node, name='%.*s'", (int)len, name);
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

    if (!text_node->native_string || !text_node->parent) {
        log_error("dom_text_set_content: text node not backed by Lambda");
        return false;
    }

    DomElement* parent = (DomElement*)text_node->parent;
    if (!parent->doc) {
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
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item new_string_item;
    if (parent->doc->input->ui_mode) {
        // In ui_mode, create fat [DomText][String][chars] so dom_relink_children can link it
        String* new_s = editor.builder()->createDomTextString(new_content, strlen(new_content));
        if (!new_s) { log_error("dom_text_set_content: failed to create DomText string"); return false; }
        new_string_item = (Item){.item = s2it(new_s)};
    } else {
        new_string_item = editor.builder()->createStringItem(new_content);
        if (get_type_id(new_string_item) != LMD_TYPE_STRING) {
            log_error("dom_text_set_content: failed to create string");
            return false;
        }
    }

    // Replace child in parent Element's items array
    Item result = editor.elmt_replace_child(
        {.element = parent->native_element},
        child_idx,
        new_string_item
    );

    if (!result.element) {
        log_error("dom_text_set_content: failed to replace child");
        return false;
    }

    if (parent->doc->input->ui_mode) {
        // Copy DOM properties from old text_node to new embedded DomText
        DomText* new_dt = string_to_dom_text(new_string_item.get_string());
        if (new_dt->node_type == DOM_NODE_TEXT) {
            new_dt->content_type = text_node->content_type;
            new_dt->rect = text_node->rect;
            new_dt->font = text_node->font;
            new_dt->color = text_node->color;
            // dom_relink_children already set parent/siblings on new_dt
        }
    }

    // Update text_node fields to point to new String (backward compat for callers)
    text_node->native_string = new_string_item.get_string();
    text_node->text = text_node->native_string->chars;
    text_node->length = text_node->native_string->len;

    // In INLINE mode, parent element pointer unchanged, but update for consistency
    parent->native_element = result.element;
    log_debug("dom_text_set_content: updated text at index %lld to '%s'", child_idx, new_content);
    return true;
}

bool dom_text_is_backed(DomText* text_node) {
    return text_node && text_node->native_string && text_node->parent;
}

int64_t dom_text_get_child_index(DomText* text_node) {
    if (!text_node || !text_node->parent || !text_node->native_string) {
        log_error("dom_text_get_child_index: text node not backed");
        return -1;
    }

    Element* parent_elem = ((DomElement*)text_node->parent)->native_element;
    if (!parent_elem) {
        log_error("dom_text_get_child_index: parent has no native_element");
        return -1;
    }

    // Scan parent's children to find matching native_string
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING && item.get_string() == text_node->native_string) {
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

    DomElement* parent = (DomElement*)text_node->parent;
    if (!parent->doc) {
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
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = parent->native_element},
        child_idx
    );

    if (!result.element) {
        log_error("dom_text_remove: failed to delete child");
        return false;
    }

    // Update parent
    parent->native_element = result.element;

    // Remove from DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already rebuilt)
    if (!parent->doc->input->ui_mode) {
        if (text_node->prev_sibling) {
            text_node->prev_sibling->next_sibling = text_node->next_sibling;
        } else if (text_node->parent && text_node->parent->is_element()) {
            DomElement* parent_elem = static_cast<DomElement*>(text_node->parent);
            parent_elem->first_child = text_node->next_sibling;
        }

        if (text_node->next_sibling) {
            text_node->next_sibling->prev_sibling = text_node->prev_sibling;
        } else if (text_node->parent && text_node->parent->is_element()) {
            // Text node was last child
            DomElement* parent_elem = static_cast<DomElement*>(text_node->parent);
            parent_elem->last_child = text_node->prev_sibling;
        }
    }

    // Clear references
    text_node->parent = nullptr;
    text_node->prev_sibling = nullptr;
    text_node->next_sibling = nullptr;
    text_node->native_string = nullptr;
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
    Item string_item;
    if (parent->doc->input->ui_mode) {
        // In ui_mode, create fat [DomText][String][chars] so dom_relink_children can link it
        String* s = editor.builder()->createDomTextString(text_content, strlen(text_content));
        if (!s) { log_error("dom_element_append_text: failed to create DomText string"); return nullptr; }
        string_item = (Item){.item = s2it(s)};
    } else {
        string_item = editor.builder()->createStringItem(text_content);
        if (get_type_id(string_item) != LMD_TYPE_STRING) {
            log_error("dom_element_append_text: failed to create string");
            return nullptr;
        }
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

    DomText* text_node;
    if (parent->doc->input->ui_mode) {
        // Check if DomText is embedded before the String (arena-allocated)
        DomText* candidate = string_to_dom_text(string_item.get_string());
        if (candidate->node_type == DOM_NODE_TEXT) {
            text_node = candidate;
        } else {
            text_node = dom_text_create(string_item.get_string(), parent);
            if (text_node) dom_append_to_sibling_chain(parent, text_node);
        }
    } else {
        // Create separate DomText wrapper with Lambda backing
        text_node = dom_text_create(string_item.get_string(), parent);
        if (!text_node) {
            log_error("dom_element_append_text: failed to create DomText");
            return nullptr;
        }
        dom_append_to_sibling_chain(parent, text_node);
    }

    // Update parent element pointer (INLINE mode: no-op, but kept for consistency)
    parent->native_element = result.element;

    log_debug("dom_element_append_text: appended text '%s'", text_content);

    return text_node;
}

// ============================================================================
// DOM Comment/DOCTYPE Node Implementation
// ============================================================================

DomComment* dom_comment_create(Element* native_element, DomElement* parent_element) {
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
    if (str_ieq_const(tag_name, strlen(tag_name), "!DOCTYPE")) {
        node_type = DOM_NODE_DOCTYPE;
    } else if (strcmp(tag_name, "!--") == 0 || strcmp(tag_name, "#comment") == 0) {
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
    comment_node->tag_name = tag_name;  // Reference type name (no copy needed)

    // Extract content from first String child (if exists)
    if (native_element->length > 0) {
        Item first_item = native_element->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* content_str = first_item.get_string();
            comment_node->content = content_str->chars;  // Reference, not copy
            comment_node->length = content_str->len;
        }
    }

    if (!comment_node->content) {
        comment_node->content = "";
        comment_node->length = 0;
    }

    log_debug("dom_comment_create: created backed comment (tag=%s, content='%s')",
              tag_name, comment_node->content);

    return comment_node;
}

DomComment* dom_comment_create_detached(Element* native_element, DomDocument* doc) {
    if (!native_element) {
        log_error("dom_comment_create_detached: native_element required");
        return nullptr;
    }
    if (!doc || !doc->arena) {
        log_error("dom_comment_create_detached: doc with arena required");
        return nullptr;
    }

    TypeElmt* type = (TypeElmt*)native_element->type;
    const char* tag_name = type ? type->name.str : nullptr;
    if (!tag_name) {
        log_error("dom_comment_create_detached: no tag name");
        return nullptr;
    }

    DomNodeType node_type;
    if (str_ieq_const(tag_name, strlen(tag_name), "!DOCTYPE")) {
        node_type = DOM_NODE_DOCTYPE;
    } else if (strcmp(tag_name, "!--") == 0) {
        node_type = DOM_NODE_COMMENT;
    } else {
        log_error("dom_comment_create_detached: not a comment or DOCTYPE: %s", tag_name);
        return nullptr;
    }

    DomComment* comment_node = (DomComment*)arena_calloc(doc->arena, sizeof(DomComment));
    if (!comment_node) {
        log_error("dom_comment_create_detached: arena_calloc failed");
        return nullptr;
    }

    comment_node->node_type = node_type;
    comment_node->parent = nullptr;
    comment_node->next_sibling = nullptr;
    comment_node->prev_sibling = nullptr;
    comment_node->native_element = native_element;
    comment_node->tag_name = tag_name;

    if (native_element->length > 0) {
        Item first_item = native_element->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* content_str = first_item.get_string();
            comment_node->content = content_str->chars;
            comment_node->length = content_str->len;
        }
    }

    if (!comment_node->content) {
        comment_node->content = "";
        comment_node->length = 0;
    }

    log_debug("dom_comment_create_detached: created comment (tag=%s, content='%s')",
              tag_name, comment_node->content);
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
    return comment_node && comment_node->native_element && comment_node->parent;
}

int64_t dom_comment_get_child_index(DomComment* comment_node) {
    if (!comment_node || !comment_node->parent || !comment_node->native_element) {
        log_error("dom_comment_get_child_index: comment node not backed");
        return -1;
    }

    Element* parent_elem = ((DomElement*)comment_node->parent)->native_element;
    if (!parent_elem) {
        log_error("dom_comment_get_child_index: parent has no native_element");
        return -1;
    }

    // Scan parent's children to find matching native_element
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_ELEMENT && item.element == comment_node->native_element) {
            log_debug("dom_comment_get_child_index: found at index %lld", i);
            return i;
        }
    }

    log_error("dom_comment_get_child_index: native_element not found in parent (may have been removed)");
    return -1;
}

bool dom_comment_set_content(DomComment* comment_node, const char* new_content) {
    if (!comment_node || !new_content) {
        log_error("dom_comment_set_content: invalid arguments");
        return false;
    }

    if (!comment_node->native_element || !comment_node->parent) {
        log_error("dom_comment_set_content: comment not backed by Lambda");
        return false;
    }

    DomElement* parent = (DomElement*)comment_node->parent;
    if (!parent->doc) {
        log_error("dom_comment_set_content: parent element has no document");
        return false;
    }

    // Create new String via MarkBuilder
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item new_string_item;
    if (parent->doc->input->ui_mode) {
        // In ui_mode, create fat [DomText][String][chars] so dom_relink_children can link it
        String* new_s = editor.builder()->createDomTextString(new_content, strlen(new_content));
        if (!new_s) { log_error("dom_comment_set_content: failed to create DomText string"); return false; }
        new_string_item = (Item){.item = s2it(new_s)};
    } else {
        new_string_item = editor.builder()->createStringItem(new_content);
        if (get_type_id(new_string_item) != LMD_TYPE_STRING) {
            log_error("dom_comment_set_content: failed to create string");
            return false;
        }
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
    String* new_string = new_string_item.get_string();
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
    DomComment* comment_node = dom_comment_create(
        comment_item.element,
        parent
    );

    if (!comment_node) {
        log_error("dom_element_append_comment: failed to create DomComment");
        return nullptr;
    }

    // Add to DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already linked)
    if (!parent->doc->input->ui_mode) {
        dom_append_to_sibling_chain(parent, comment_node);
    }

    log_debug("dom_element_append_comment: appended comment '%s'", comment_content);

    return comment_node;
}

bool dom_comment_remove(DomComment* comment_node) {
    if (!comment_node) {
        log_error("dom_comment_remove: null comment");
        return false;
    }

    DomElement* parent = (DomElement*)comment_node->parent;
    if (!comment_node->native_element || !parent || !parent->doc) {
        log_error("dom_comment_remove: comment not backed");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_comment_get_child_index(comment_node);
    if (child_idx < 0) {
        log_error("dom_comment_remove: failed to get child index");
        return false;
    }

    // Remove from Lambda parent Element's children array
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = parent->native_element},
        child_idx
    );

    if (!result.element) {
        log_error("dom_comment_remove: failed to delete child");
        return false;
    }

    // Update parent
    parent->native_element = result.element;

    // Remove from DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already rebuilt)
    if (!parent->doc->input->ui_mode) {
        if (comment_node->prev_sibling) {
            comment_node->prev_sibling->next_sibling = comment_node->next_sibling;
        } else if (comment_node->parent) {
            DomElement* elem_parent = static_cast<DomElement*>(comment_node->parent);
            elem_parent->first_child = comment_node->next_sibling;
        }

        if (comment_node->next_sibling) {
            comment_node->next_sibling->prev_sibling = comment_node->prev_sibling;
        } else if (comment_node->parent) {
            // Comment node was last child
            DomElement* elem_parent = static_cast<DomElement*>(comment_node->parent);
            elem_parent->last_child = comment_node->prev_sibling;
        }
    }

    // Clear references
    comment_node->parent = nullptr;
    comment_node->prev_sibling = nullptr;
    comment_node->next_sibling = nullptr;
    comment_node->native_element = nullptr;
    log_debug("dom_comment_remove: removed comment at index %lld", child_idx);
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

// ============================================================================
// Element-to-DOM map: Lambda Element* → DomElement*
// Used for incremental DOM rebuild (Phase 12)
// ============================================================================

typedef struct ElementDomMapEntry {
    Element* element;       // key: Lambda Element pointer
    DomElement* dom_elem;   // value: corresponding DomElement
} ElementDomMapEntry;

static uint64_t element_dom_map_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const ElementDomMapEntry* entry = (const ElementDomMapEntry*)item;
    return hashmap_sip(&entry->element, sizeof(Element*), seed0, seed1);
}

static int element_dom_map_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const ElementDomMapEntry* ea = (const ElementDomMapEntry*)a;
    const ElementDomMapEntry* eb = (const ElementDomMapEntry*)b;
    return ea->element == eb->element ? 0 : (ea->element < eb->element ? -1 : 1);
}

HashMap* element_dom_map_create(void) {
    return hashmap_new(sizeof(ElementDomMapEntry), 64, 0, 0,
                       element_dom_map_hash, element_dom_map_compare, NULL, NULL);
}

void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem) {
    if (!map || !elem || !dom_elem) return;
    ElementDomMapEntry entry;
    entry.element = elem;
    entry.dom_elem = dom_elem;
    hashmap_set(map, &entry);
}

DomElement* element_dom_map_lookup(HashMap* map, Element* elem) {
    if (!map || !elem) return nullptr;
    ElementDomMapEntry key;
    key.element = elem;
    key.dom_elem = nullptr;
    const ElementDomMapEntry* found = (const ElementDomMapEntry*)hashmap_get(map, &key);
    return found ? found->dom_elem : nullptr;
}

static const int MAX_DOM_BUILD_DEPTH = 512;
static thread_local int g_dom_build_depth = 0;

struct DomBuildDepthGuard {
    DomBuildDepthGuard() { g_dom_build_depth++; }
    ~DomBuildDepthGuard() { g_dom_build_depth--; }
};

DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent) {
    if (!elem || !doc) {
        log_debug("build_dom_tree_from_element: Invalid arguments\n");
        return nullptr;
    }
    if (g_dom_build_depth > MAX_DOM_BUILD_DEPTH) {
        return nullptr;
    }
    DomBuildDepthGuard depth_guard;

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;
    log_debug("build element: <%s> (parent: %s), elem->length=%lld", tag_name,
              parent ? parent->tag_name : "none", (long long)elem->length);

    // Skip comments and DOCTYPE - they will be created as DomComment nodes below
    // HTML5 parser uses "#comment", CSS/older parsers use "!--"
    if (strcmp(tag_name, "!--") == 0 || strcmp(tag_name, "#comment") == 0 || str_ieq_const(tag_name, strlen(tag_name), "!DOCTYPE")) {
        return nullptr;  // Not a layout element, processed as child below
    }

    // Skip XML declarations
    if (strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip XML declarations
    }

    // Skip <script> elements during DOM tree building. Per HTML spec the UA
    // stylesheet sets `script { display: none }`, so they don't participate
    // in layout. Keeping them out of the DOM also prevents script_runner from
    // re-executing inline scripts during pure layout passes.
    //
    // Exception: when the author explicitly overrides display via inline
    // style="display: ..." (block, inline, etc.), the script's text content
    // becomes part of the rendered/selectable text per WPT
    // selection/script-and-style-elements.html. In that case we keep the
    // element so DomRange.toString() can include its text.
    if (str_ieq_const(tag_name, strlen(tag_name), "script")) {
        const char* style_attr = extract_element_attribute(elem, "style", nullptr);
        bool has_display_override = false;
        if (style_attr) {
            // Crude check: the inline style attribute mentions "display:" with
            // a non-`none` value. css_parse will refine this further when the
            // element is later checked for visibility.
            const char* d = strstr(style_attr, "display");
            if (d) {
                const char* colon = strchr(d, ':');
                if (colon) {
                    const char* v = colon + 1;
                    while (*v == ' ' || *v == '\t') v++;
                    if (strncmp(v, "none", 4) != 0) has_display_override = true;
                }
            }
        }
        if (!has_display_override) {
            return nullptr;  // Skip script elements during DOM tree building
        }
    }

    // create DomElement
    bool ui_mode = doc->input && doc->input->ui_mode;
    DomElement* dom_elem;
    if (ui_mode) {
        // UI mode: DomElement already allocated by MarkBuilder's ElementBuilder
        dom_elem = element_to_dom_element(elem);
        if (!dom_element_init(dom_elem, doc, tag_name, elem)) return nullptr;
    } else {
        dom_elem = dom_element_create(doc, tag_name, elem);
    }
    if (!dom_elem) return nullptr;

    // populate element-to-DOM map if available (for incremental rebuild)
    if (doc->element_dom_map) {
        element_dom_map_insert(doc->element_dom_map, elem, dom_elem);
    }

    // Extract source line number if tracked during HTML5 parsing
    ConstItem sl_attr = elem->get_attr("__source_line");
    if (((Item*)&sl_attr)->_type_id == LMD_TYPE_INT) {
        dom_elem->source_line = (int)((Item*)&sl_attr)->int_val;
    }

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
            str_copy(class_copy, strlen(class_value) + 1, class_value, strlen(class_value));

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
    if (str_ieq_const(tag_name, strlen(tag_name), "td") || str_ieq_const(tag_name, strlen(tag_name), "th")) {
        const char* rowspan_value = extract_element_attribute(elem, "rowspan", doc->arena);
        if (rowspan_value) {
            dom_element_set_attribute(dom_elem, "rowspan", rowspan_value);
        }

        const char* colspan_value = extract_element_attribute(elem, "colspan", doc->arena);
        if (colspan_value) {
            dom_element_set_attribute(dom_elem, "colspan", colspan_value);
        }
    }

    // Set :link pseudo-state for anchor and area elements with href attribute
    // Per HTML spec: :link matches <a> and <area> elements with href that haven't been visited
    if (str_ieq_const(tag_name, strlen(tag_name), "a") || str_ieq_const(tag_name, strlen(tag_name), "area")) {
        const char* href_value = extract_element_attribute(elem, "href", doc->arena);
        if (href_value && strlen(href_value) > 0) {
            dom_element_set_pseudo_state(dom_elem, PSEUDO_STATE_LINK);
            // Store href attribute for later use (e.g., navigation)
            dom_element_set_attribute(dom_elem, "href", href_value);
        }
    }

    // Set :checked, :disabled pseudo-states for input elements
    // These boolean attributes set the initial state from HTML
    if (str_ieq_const(tag_name, strlen(tag_name), "input")) {
        // Store the type attribute for later use
        const char* type_value = extract_element_attribute(elem, "type", doc->arena);
        if (type_value) {
            dom_element_set_attribute(dom_elem, "type", type_value);
        }
        // Store the name attribute for radio button grouping
        const char* name_value = extract_element_attribute(elem, "name", doc->arena);
        if (name_value) {
            dom_element_set_attribute(dom_elem, "name", name_value);
        }
        // checked attribute sets :checked pseudo-state (for checkbox/radio)
        // Use has_attr() since boolean attributes have no string value (stored as ITEM_NULL)
        if (elem->has_attr("checked")) {
            dom_element_set_pseudo_state(dom_elem, PSEUDO_STATE_CHECKED);
        }
        // disabled attribute sets :disabled pseudo-state
        if (elem->has_attr("disabled")) {
            dom_element_set_pseudo_state(dom_elem, PSEUDO_STATE_DISABLED);
        }
    }

    // set parent relationship if provided
    // Use link_child since the Lambda tree already contains this element
    // (we're building DOM wrappers from existing Lambda structure)
    if (parent) {
        dom_element_link_child(parent, dom_elem);
    }

    // Process all children - including text nodes, comments, and elements
    // Elements are Lists, so iterate through items

    if (elem->length > 0 && !elem->items) {
        log_error("build_dom_tree: <%s> has length=%lld but items=NULL", tag_name, (long long)elem->length);
        return dom_elem;
    }
    // Sanity check: reject absurdly large length values
    if (elem->length > 100000) {
        log_error("build_dom_tree: <%s> has suspicious length=%lld, skipping", tag_name, (long long)elem->length);
        return dom_elem;
    }
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        TypeId child_type = get_type_id(child_item);
        // Guard: skip items with invalid type IDs (corrupted memory)
        if (child_type == 0 || child_type > LMD_TYPE_OBJECT) {
            log_error("build_dom_tree: <%s> child %lld has invalid type=%d (raw=0x%llx), skipping",
                      tag_name, (long long)i, child_type, (unsigned long long)child_item.item);
            continue;
        }
        if (child_type == LMD_TYPE_ELEMENT) {
            // element node - recursively build
            Element* child_elem = child_item.element;
            if (!child_elem || (uintptr_t)child_elem < 0x1000) {
                log_error("build_dom_tree: <%s> child %lld has invalid element pointer %p", tag_name, (long long)i, (void*)child_elem);
                continue;
            }
            TypeElmt* child_elem_type = (TypeElmt*)child_elem->type;
            const char* child_tag_name = child_elem_type ? child_elem_type->name.str : "unknown";

            // Check if this is a comment or DOCTYPE
            // HTML5 parser uses "#comment", CSS/older parsers use "!--"
            if (strcmp(child_tag_name, "!--") == 0 || strcmp(child_tag_name, "#comment") == 0 || str_ieq_const(child_tag_name, strlen(child_tag_name), "!DOCTYPE")) {
                // Create DomComment node backed by Lambda Element
                DomComment* comment_node = dom_comment_create(child_elem, dom_elem);
                if (comment_node) {
                    // Add to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, comment_node);

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
            String* text_str = child_item.get_string();
            if (text_str && text_str->len > 0) {
                DomText* text_node;
                if (ui_mode) {
                    // UI mode: check if String was allocated with DomText prefix on arena
                    // (done by ui_copy_string_to_arena / MarkBuilder). Non-arena strings
                    // (GC heap, const pool) do NOT have this prefix — using string_to_dom_text
                    // on them would produce a bogus pointer that corrupts adjacent memory.
                    DomText* candidate = string_to_dom_text(text_str);
                    if (candidate->node_type == DOM_NODE_TEXT) {
                        text_node = candidate;
                        text_node->parent = dom_elem;
                    } else {
                        text_node = dom_text_create(text_str, dom_elem);
                    }
                } else {
                    // Create text node (preserves Lambda String reference)
                    text_node = dom_text_create(text_str, dom_elem);
                }
                if (text_node) {
                    // Add text node to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, text_node);

                    log_debug("  Created text node at index %lld: '%s' (len=%zu)",
                              i, text_str->chars, text_str->len);
                }
            }
        } else if (child_type == LMD_TYPE_SYMBOL) {
            // Symbol node (HTML entity or emoji) - create DomText with symbol type
            Symbol* sym = child_item.get_symbol();
            if (sym && sym->len > 0) {
                // Create symbol text node (will be resolved at render time)
                DomText* text_node = dom_text_create_symbol(sym->chars, sym->len, dom_elem);
                if (text_node) {
                    // Add symbol node to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, text_node);

                    log_debug("  Created symbol node at index %lld: '%.*s' (len=%u)",
                              i, (int)sym->len, sym->chars, sym->len);
                }
            }
        } else if (child_type == LMD_TYPE_ARRAY) {
            // Array child - flatten into parent (Lambda scripts may produce arrays of elements)
            Array* arr = child_item.array;
            if (arr) {
                log_debug("  Flattening array child at index %lld with %lld items", i, (long long)arr->length);
                for (int64_t j = 0; j < arr->length; j++) {
                    Item arr_item = arr->items[j];
                    TypeId arr_item_type = get_type_id(arr_item);
                    if (arr_item_type == LMD_TYPE_ELEMENT) {
                        Element* child_elem = arr_item.element;
                        build_dom_tree_from_element(child_elem, doc, dom_elem);
                    } else if (arr_item_type == LMD_TYPE_STRING) {
                        String* text_str = arr_item.get_string();
                        if (text_str && text_str->len > 0) {
                            DomText* text_node;
                            if (ui_mode) {
                                DomText* candidate = string_to_dom_text(text_str);
                                if (candidate->node_type == DOM_NODE_TEXT) {
                                    text_node = candidate;
                                    text_node->parent = dom_elem;
                                } else {
                                    text_node = dom_text_create(text_str, dom_elem);
                                }
                            } else {
                                text_node = dom_text_create(text_str, dom_elem);
                            }
                            if (text_node) {
                                dom_append_to_sibling_chain(dom_elem, text_node);
                            }
                        }
                    } else if (arr_item_type == LMD_TYPE_SYMBOL) {
                        Symbol* sym = arr_item.get_symbol();
                        if (sym && sym->len > 0) {
                            DomText* text_node = dom_text_create_symbol(sym->chars, sym->len, dom_elem);
                            if (text_node) {
                                dom_append_to_sibling_chain(dom_elem, text_node);
                            }
                        }
                    } else if (arr_item_type == LMD_TYPE_ARRAY) {
                        // nested array - flatten recursively by wrapping in a temporary element iteration
                        Array* nested = arr_item.array;
                        if (nested) {
                            for (int64_t k = 0; k < nested->length; k++) {
                                Item nested_item = nested->items[k];
                                TypeId nested_type = get_type_id(nested_item);
                                if (nested_type == LMD_TYPE_ELEMENT) {
                                    build_dom_tree_from_element(nested_item.element, doc, dom_elem);
                                } else if (nested_type == LMD_TYPE_STRING) {
                                    String* s = nested_item.get_string();
                                    if (s && s->len > 0) {
                                        DomText* tn;
                                        if (ui_mode) {
                                            DomText* candidate = string_to_dom_text(s);
                                            if (candidate->node_type == DOM_NODE_TEXT) {
                                                tn = candidate;
                                                tn->parent = dom_elem;
                                            } else {
                                                tn = dom_text_create(s, dom_elem);
                                            }
                                        } else {
                                            tn = dom_text_create(s, dom_elem);
                                        }
                                        if (tn) dom_append_to_sibling_chain(dom_elem, tn);
                                    }
                                } else if (nested_type == LMD_TYPE_SYMBOL) {
                                    Symbol* sym = nested_item.get_symbol();
                                    if (sym && sym->len > 0) {
                                        DomText* tn = dom_text_create_symbol(sym->chars, sym->len, dom_elem);
                                        if (tn) dom_append_to_sibling_chain(dom_elem, tn);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return dom_elem;
}
