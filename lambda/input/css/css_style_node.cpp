#include "css_style_node.hpp"
#include "css_style.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Forward declarations for callback functions
static bool collect_nodes_callback(AvlNode* avl_node, void* context);
static bool collect_computed_callback(AvlNode* avl_node, void* context);
#ifndef NDEBUG
static bool print_tree_callback(StyleNode* node, void* context);
#endif
static bool validate_tree_callback(StyleNode* node, void* context);
static bool merge_tree_callback(StyleNode* node, void* context);
static bool clone_tree_callback(StyleNode* node, void* context);
static bool wrapper_callback(AvlNode* avl_node, void* ctx);
static int css_get_cascade_level(const CssDeclaration* decl);

// Context structures for callbacks
struct CollectContext {
    StyleNode** nodes;
    size_t* count;
    size_t capacity;
};

struct ComputedContext {
    StyleNode** computed;
    size_t* count;
    size_t capacity;
};

struct ValidationContext {
    bool* is_valid;
};

struct CollectSelectorsContext {
    char** selectors;
    size_t* count;
    size_t capacity;
};

struct MergeContext {
    StyleTree* target;
    int* merged_count;
};

struct CloneContext {
    StyleTree* target;
    int* cloned_count;
};

struct CallbackWrapper {
    style_tree_callback_t user_callback;
    void* user_context;
    int count;
};

// ============================================================================
// CSS Specificity Implementation
// ============================================================================

CssSpecificity css_specificity_create(uint8_t inline_style,
                                     uint8_t ids,
                                     uint8_t classes,
                                     uint8_t elements,
                                     bool important) {
    CssSpecificity spec = {0};
    spec.inline_style = inline_style > 0 ? 1 : 0;
    spec.ids = ids;
    spec.classes = classes;
    spec.elements = elements;
    spec.important = important;
    return spec;
}

uint32_t css_specificity_to_value(CssSpecificity specificity) {
    // CSS specificity is not a base-10 number, but for comparison we can use:
    // important flag as highest bit, then inline, ids, classes, elements
    uint32_t value = 0;

    if (specificity.important) {
        value |= 0x80000000; // Set highest bit for !important
    }

    value |= (uint32_t)(specificity.inline_style & 0x1) << 24;
    value |= (uint32_t)(specificity.ids & 0xFF) << 16;
    value |= (uint32_t)(specificity.classes & 0xFF) << 8;
    value |= (uint32_t)(specificity.elements & 0xFF);

    return value;
}

int css_specificity_compare(CssSpecificity a, CssSpecificity b) {
    uint32_t value_a = css_specificity_to_value(a);
    uint32_t value_b = css_specificity_to_value(b);

    if (value_a < value_b) return -1;
    if (value_a > value_b) return 1;
    return 0;
}

void css_specificity_print(CssSpecificity specificity) {
#ifndef NDEBUG
    printf("(%d,%d,%d,%d)%s",
           specificity.inline_style,
           specificity.ids,
           specificity.classes,
           specificity.elements,
           specificity.important ? "!" : "");
#endif
}

// ============================================================================
// CSS Declaration Implementation
// ============================================================================

CssDeclaration* css_declaration_create(CssPropertyId property_id,
                                      void* value,
                                      CssSpecificity specificity,
                                      CssOrigin origin,
                                      Pool* pool) {
    CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
    if (!decl) return NULL;

    decl->property_id = property_id;
    decl->value = static_cast<CssValue*>(value);
    decl->specificity = specificity;
    decl->origin = origin;
    decl->source_order = 0; // Will be set by caller
    decl->valid = true;
    decl->ref_count = 1;

    return decl;
}

CssDeclaration* css_declaration_clone_for_cascade(
    const CssDeclaration* source, CssSpecificity specificity,
    CssOrigin origin, Pool* pool) {
    if (!source || !pool) return NULL;
    CssDeclaration* decl = (CssDeclaration*)pool_calloc(pool, sizeof(CssDeclaration));
    if (!decl) return NULL;
    *decl = *source;
    decl->specificity = specificity;
    decl->specificity.important = source->important;
    decl->origin = origin;
    // A cascade clone owns only its declaration record; value/source payload
    // remains tied to the stylesheet that supplied the immutable declaration.
    decl->owns_payload = false;
    decl->tree_owned_record = true;
    decl->ref_count = 1;
    return decl;
}

static bool css_value_can_clone_owned(const CssValue* value) {
    if (!value) return true;
    switch (value->type) {
        case CSS_VALUE_TYPE_KEYWORD:
        case CSS_VALUE_TYPE_LENGTH:
        case CSS_VALUE_TYPE_PERCENTAGE:
        case CSS_VALUE_TYPE_NUMBER:
        case CSS_VALUE_TYPE_STRING:
        case CSS_VALUE_TYPE_URL:
        case CSS_VALUE_TYPE_ANGLE:
        case CSS_VALUE_TYPE_TIME:
        case CSS_VALUE_TYPE_FREQUENCY:
            return true;
        case CSS_VALUE_TYPE_COLOR:
            return value->data.color.type >= CSS_COLOR_KEYWORD &&
                value->data.color.type <= CSS_COLOR_SYSTEM;
        case CSS_VALUE_TYPE_CUSTOM:
            return css_value_can_clone_owned(value->data.custom_property.fallback);
        case CSS_VALUE_TYPE_LIST:
            if (value->data.list.count < 0 ||
                (value->data.list.count > 0 && !value->data.list.values)) return false;
            for (int i = 0; i < value->data.list.count; i++) {
                if (!css_value_can_clone_owned(value->data.list.values[i])) return false;
            }
            return true;
        case CSS_VALUE_TYPE_FUNCTION:
            if (!value->data.function || value->data.function->arg_count < 0 ||
                (value->data.function->arg_count > 0 &&
                 !value->data.function->args)) return false;
            for (int i = 0; i < value->data.function->arg_count; i++) {
                if (!css_value_can_clone_owned(value->data.function->args[i])) return false;
            }
            return true;
        case CSS_VALUE_TYPE_VAR:
            return value->data.var_ref &&
                css_value_can_clone_owned(value->data.var_ref->fallback);
        case CSS_VALUE_TYPE_ENV:
            return value->data.env_ref &&
                css_value_can_clone_owned(value->data.env_ref->fallback);
        case CSS_VALUE_TYPE_ATTR:
            return value->data.attr_ref &&
                css_value_can_clone_owned(value->data.attr_ref->fallback);
        case CSS_VALUE_TYPE_COLOR_MIX:
            return value->data.color_mix &&
                css_value_can_clone_owned(value->data.color_mix->color1) &&
                css_value_can_clone_owned(value->data.color_mix->color2);
        case CSS_VALUE_TYPE_CALC:
            // CssCalcNode has no leaf/operator discriminant, so its pointer union
            // cannot be traversed safely until that representation is corrected.
            return false;
        case CSS_VALUE_TYPE_UNKNOWN:
            return false;
    }
    return false;
}

bool css_declaration_can_clone_owned(const CssDeclaration* source) {
    return source && css_value_can_clone_owned(source->value);
}

static char* css_owned_strdup(Pool* pool, const char* source) {
    return source ? pool_strdup(pool, source) : NULL;
}

static CssValue* css_value_clone_owned(const CssValue* source, Pool* pool);
static void css_value_destroy_owned(CssValue* value, Pool* pool);

static CssValue** css_value_array_clone_owned(CssValue* const* source,
                                               int count, Pool* pool) {
    if (count <= 0) return NULL;
    CssValue** values = (CssValue**)pool_calloc(
        pool, (size_t)count * sizeof(CssValue*));
    if (!values) return NULL;
    for (int i = 0; i < count; i++) {
        values[i] = css_value_clone_owned(source[i], pool);
        if (source[i] && !values[i]) {
            for (int prior = 0; prior < i; prior++) {
                css_value_destroy_owned(values[prior], pool);
            }
            pool_free(pool, values);
            return NULL;
        }
    }
    return values;
}

static void css_value_destroy_owned(CssValue* value, Pool* pool) {
    if (!value || !pool) return;
    switch (value->type) {
        case CSS_VALUE_TYPE_STRING:
            pool_free(pool, (void*)value->data.string);
            break;
        case CSS_VALUE_TYPE_URL:
            pool_free(pool, (void*)value->data.url);
            break;
        case CSS_VALUE_TYPE_COLOR:
            if (value->data.color.type == CSS_COLOR_KEYWORD) {
                pool_free(pool, (void*)value->data.color.data.keyword);
            } else if (value->data.color.type == CSS_COLOR_HSL ||
                       value->data.color.type == CSS_COLOR_HWB ||
                       value->data.color.type == CSS_COLOR_LAB ||
                       value->data.color.type == CSS_COLOR_LCH ||
                       value->data.color.type == CSS_COLOR_OKLAB ||
                       value->data.color.type == CSS_COLOR_OKLCH ||
                       value->data.color.type == CSS_COLOR_COLOR) {
                pool_free(pool, value->data.color.data.components);
            }
            break;
        case CSS_VALUE_TYPE_CUSTOM:
            pool_free(pool, (void*)value->data.custom_property.name);
            css_value_destroy_owned(value->data.custom_property.fallback, pool);
            break;
        case CSS_VALUE_TYPE_LIST:
            for (int i = 0; i < value->data.list.count; i++) {
                css_value_destroy_owned(value->data.list.values[i], pool);
            }
            pool_free(pool, value->data.list.values);
            break;
        case CSS_VALUE_TYPE_FUNCTION:
            if (value->data.function) {
                for (int i = 0; i < value->data.function->arg_count; i++) {
                    css_value_destroy_owned(value->data.function->args[i], pool);
                }
                pool_free(pool, value->data.function->args);
                pool_free(pool, (void*)value->data.function->name);
                pool_free(pool, value->data.function);
            }
            break;
        case CSS_VALUE_TYPE_VAR:
            if (value->data.var_ref) {
                pool_free(pool, (void*)value->data.var_ref->name);
                css_value_destroy_owned(value->data.var_ref->fallback, pool);
                pool_free(pool, value->data.var_ref);
            }
            break;
        case CSS_VALUE_TYPE_ENV:
            if (value->data.env_ref) {
                pool_free(pool, (void*)value->data.env_ref->name);
                css_value_destroy_owned(value->data.env_ref->fallback, pool);
                pool_free(pool, value->data.env_ref);
            }
            break;
        case CSS_VALUE_TYPE_ATTR:
            if (value->data.attr_ref) {
                pool_free(pool, (void*)value->data.attr_ref->name);
                pool_free(pool, (void*)value->data.attr_ref->type_or_unit);
                css_value_destroy_owned(value->data.attr_ref->fallback, pool);
                pool_free(pool, value->data.attr_ref);
            }
            break;
        case CSS_VALUE_TYPE_COLOR_MIX:
            if (value->data.color_mix) {
                css_value_destroy_owned(value->data.color_mix->color1, pool);
                css_value_destroy_owned(value->data.color_mix->color2, pool);
                pool_free(pool, (void*)value->data.color_mix->method);
                pool_free(pool, value->data.color_mix);
            }
            break;
        default:
            break;
    }
    pool_free(pool, value);
}

static CssValue* css_value_clone_owned(const CssValue* source, Pool* pool) {
    if (!source) return NULL;
    if (!css_value_can_clone_owned(source)) return NULL;
    CssValue* clone = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    if (!clone) return NULL;
    *clone = *source;

    switch (source->type) {
        case CSS_VALUE_TYPE_STRING:
            clone->data.string = css_owned_strdup(pool, source->data.string);
            if (source->data.string && !clone->data.string) goto clone_failed;
            break;
        case CSS_VALUE_TYPE_URL:
            clone->data.url = css_owned_strdup(pool, source->data.url);
            if (source->data.url && !clone->data.url) goto clone_failed;
            break;
        case CSS_VALUE_TYPE_COLOR:
            if (source->data.color.type == CSS_COLOR_KEYWORD) {
                clone->data.color.data.keyword = css_owned_strdup(
                    pool, source->data.color.data.keyword);
                if (source->data.color.data.keyword &&
                    !clone->data.color.data.keyword) goto clone_failed;
            } else if (source->data.color.type == CSS_COLOR_HSL ||
                       source->data.color.type == CSS_COLOR_HWB ||
                       source->data.color.type == CSS_COLOR_LAB ||
                       source->data.color.type == CSS_COLOR_LCH ||
                       source->data.color.type == CSS_COLOR_OKLAB ||
                       source->data.color.type == CSS_COLOR_OKLCH ||
                       source->data.color.type == CSS_COLOR_COLOR) {
                clone->data.color.data.components = NULL;
                if (source->data.color.data.components) {
                    clone->data.color.data.components =
                        (CssColorComponents*)pool_alloc(
                            pool, sizeof(CssColorComponents));
                    if (!clone->data.color.data.components) goto clone_failed;
                    *clone->data.color.data.components =
                        *source->data.color.data.components;
                }
            }
            break;
        case CSS_VALUE_TYPE_CUSTOM:
            clone->data.custom_property.name = css_owned_strdup(
                pool, source->data.custom_property.name);
            if (source->data.custom_property.name &&
                !clone->data.custom_property.name) goto clone_failed;
            clone->data.custom_property.fallback = css_value_clone_owned(
                source->data.custom_property.fallback, pool);
            if (source->data.custom_property.fallback &&
                !clone->data.custom_property.fallback) goto clone_failed;
            break;
        case CSS_VALUE_TYPE_LIST:
            clone->data.list.values = css_value_array_clone_owned(
                source->data.list.values, source->data.list.count, pool);
            if (source->data.list.count > 0 && !clone->data.list.values) {
                clone->data.list.count = 0;
                goto clone_failed;
            }
            break;
        case CSS_VALUE_TYPE_FUNCTION: {
            clone->data.function = NULL;
            CssFunction* function = (CssFunction*)pool_calloc(pool, sizeof(CssFunction));
            if (!function) goto clone_failed;
            clone->data.function = function;
            function->name = css_owned_strdup(pool, source->data.function->name);
            if (source->data.function->name && !function->name) goto clone_failed;
            function->arg_count = source->data.function->arg_count;
            function->args = css_value_array_clone_owned(
                source->data.function->args, function->arg_count, pool);
            if (function->arg_count > 0 && !function->args) {
                function->arg_count = 0;
                goto clone_failed;
            }
            break;
        }
        case CSS_VALUE_TYPE_VAR: {
            clone->data.var_ref = NULL;
            CSSVarRef* ref = (CSSVarRef*)pool_calloc(pool, sizeof(CSSVarRef));
            if (!ref) goto clone_failed;
            clone->data.var_ref = ref;
            *ref = *source->data.var_ref;
            ref->name = css_owned_strdup(pool, source->data.var_ref->name);
            ref->fallback = css_value_clone_owned(source->data.var_ref->fallback, pool);
            if ((source->data.var_ref->name && !ref->name) ||
                (source->data.var_ref->fallback && !ref->fallback)) goto clone_failed;
            break;
        }
        case CSS_VALUE_TYPE_ENV: {
            clone->data.env_ref = NULL;
            CSSEnvRef* ref = (CSSEnvRef*)pool_calloc(pool, sizeof(CSSEnvRef));
            if (!ref) goto clone_failed;
            clone->data.env_ref = ref;
            *ref = *source->data.env_ref;
            ref->name = css_owned_strdup(pool, source->data.env_ref->name);
            ref->fallback = css_value_clone_owned(source->data.env_ref->fallback, pool);
            if ((source->data.env_ref->name && !ref->name) ||
                (source->data.env_ref->fallback && !ref->fallback)) goto clone_failed;
            break;
        }
        case CSS_VALUE_TYPE_ATTR: {
            clone->data.attr_ref = NULL;
            CSSAttrRef* ref = (CSSAttrRef*)pool_calloc(pool, sizeof(CSSAttrRef));
            if (!ref) goto clone_failed;
            clone->data.attr_ref = ref;
            *ref = *source->data.attr_ref;
            ref->name = css_owned_strdup(pool, source->data.attr_ref->name);
            ref->type_or_unit = css_owned_strdup(
                pool, source->data.attr_ref->type_or_unit);
            ref->fallback = css_value_clone_owned(source->data.attr_ref->fallback, pool);
            if ((source->data.attr_ref->name && !ref->name) ||
                (source->data.attr_ref->type_or_unit && !ref->type_or_unit) ||
                (source->data.attr_ref->fallback && !ref->fallback)) goto clone_failed;
            break;
        }
        case CSS_VALUE_TYPE_COLOR_MIX: {
            clone->data.color_mix = NULL;
            CSSColorMix* mix = (CSSColorMix*)pool_calloc(pool, sizeof(CSSColorMix));
            if (!mix) goto clone_failed;
            clone->data.color_mix = mix;
            *mix = *source->data.color_mix;
            mix->color1 = css_value_clone_owned(source->data.color_mix->color1, pool);
            mix->color2 = css_value_clone_owned(source->data.color_mix->color2, pool);
            mix->method = css_owned_strdup(pool, source->data.color_mix->method);
            if ((source->data.color_mix->color1 && !mix->color1) ||
                (source->data.color_mix->color2 && !mix->color2) ||
                (source->data.color_mix->method && !mix->method)) goto clone_failed;
            break;
        }
        default:
            break;
    }
    return clone;

clone_failed:
    css_value_destroy_owned(clone, pool);
    return NULL;
}

CssDeclaration* css_declaration_clone_owned(
    const CssDeclaration* source, CssSpecificity specificity,
    CssOrigin origin, Pool* target_pool) {
    if (!source || !target_pool || !css_declaration_can_clone_owned(source)) return NULL;
    CssDeclaration* clone = (CssDeclaration*)pool_calloc(
        target_pool, sizeof(CssDeclaration));
    if (!clone) return NULL;
    *clone = *source;
    clone->owns_payload = true;
    clone->tree_owned_record = true;
    clone->specificity = specificity;
    clone->specificity.important = source->important;
    clone->origin = origin;
    clone->value = NULL;
    clone->source_file = NULL;
    clone->property_name = NULL;
    clone->value_text = NULL;
    clone->value = css_value_clone_owned(source->value, target_pool);
    if (source->value && !clone->value) goto declaration_clone_failed;
    clone->source_file = css_owned_strdup(target_pool, source->source_file);
    clone->property_name = css_owned_strdup(target_pool, source->property_name);
    if (source->value_text) {
        char* value_text = (char*)pool_alloc(target_pool, source->value_text_len + 1u);
        if (!value_text) goto declaration_clone_failed;
        memcpy(value_text, source->value_text, source->value_text_len);
        value_text[source->value_text_len] = '\0';
        clone->value_text = value_text;
    }
    if ((source->source_file && !clone->source_file) ||
        (source->property_name && !clone->property_name)) goto declaration_clone_failed;
    clone->ref_count = 1;
    return clone;

declaration_clone_failed:
    css_value_destroy_owned(clone->value, target_pool);
    pool_free(target_pool, (void*)clone->source_file);
    pool_free(target_pool, (void*)clone->property_name);
    pool_free(target_pool, (void*)clone->value_text);
    pool_free(target_pool, clone);
    return NULL;
}

void css_declaration_ref(CssDeclaration* declaration) {
    if (declaration) {
        declaration->ref_count++;
    }
}

void css_declaration_unref(CssDeclaration* declaration) {
    if (declaration && --declaration->ref_count <= 0) {
        // Declaration memory is managed by pool, so just mark as unused
        declaration->valid = false;
    }
}

int css_declaration_cascade_compare(const CssDeclaration* a, const CssDeclaration* b) {
    if (!a || !b) return 0;

    // CSS Cascade Order (per CSS Cascading and Inheritance Level 4):
    // 1. User-agent normal declarations
    // 2. User normal declarations
    // 3. Author normal declarations
    // 4. Animation declarations
    // 5. Author !important declarations
    // 6. User !important declarations
    // 7. User-agent !important declarations

    // Calculate cascade level for each declaration
    int level_a = css_get_cascade_level(a);
    int level_b = css_get_cascade_level(b);

    if (level_a != level_b) {
        return (level_a < level_b) ? -1 : 1;
    }

    // Within the same cascade level, compare specificity
    int spec_cmp = css_specificity_compare(a->specificity, b->specificity);
    if (spec_cmp != 0) {
        return spec_cmp;
    }

    // Finally, source order comparison (later wins)
    if (a->source_order < b->source_order) return -1;
    if (a->source_order > b->source_order) return 1;

    return 0; // Equal
}

// Helper function to determine cascade level
static int css_get_cascade_level(const CssDeclaration* decl) {
    if (decl->specificity.important) {
        // Important declarations (reverse origin order)
        // Note: Inline !important is still author !important but with inline_style flag
        switch (decl->origin) {
            case CSS_ORIGIN_USER_AGENT: return 8;
            case CSS_ORIGIN_USER: return 7;
            case CSS_ORIGIN_AUTHOR: return 6;
            case CSS_ORIGIN_ANIMATION: return 5; // Animations don't typically have !important
            case CSS_ORIGIN_TRANSITION: return 5; // Transitions don't have !important
        }
    } else {
        // Normal declarations
        // CSS Cascade Order:
        // 1. User-agent normal
        // 2. User normal
        // 3. Author normal (stylesheet)
        // 4. Inline styles (author origin with inline_style=1)
        // 5. Animations/Transitions
        
        // Check for inline styles first (part of author origin but higher priority)
        if (decl->origin == CSS_ORIGIN_AUTHOR && decl->specificity.inline_style) {
            return 4; // Inline styles
        }
        
        switch (decl->origin) {
            case CSS_ORIGIN_USER_AGENT: return 1;
            case CSS_ORIGIN_USER: return 2;
            case CSS_ORIGIN_AUTHOR: return 3;
            case CSS_ORIGIN_ANIMATION: return 5;
            case CSS_ORIGIN_TRANSITION: return 5;
        }
    }
    return 3; // Default to author normal
}

// ============================================================================
// Weak Declaration List Implementation
// ============================================================================

static WeakDeclaration* weak_declaration_create(CssDeclaration* declaration, Pool* pool) {
    WeakDeclaration* weak = (WeakDeclaration*)pool_calloc(pool, sizeof(WeakDeclaration));
    if (!weak) return NULL;

    weak->declaration = declaration;
    weak->next = NULL;
    css_declaration_ref(declaration); // Add reference

    return weak;
}

static void weak_declaration_destroy(WeakDeclaration* weak) {
    if (weak) {
        css_declaration_unref(weak->declaration);
        // Memory managed by pool
    }
}

static void weak_list_insert(WeakDeclaration** head, WeakDeclaration* new_weak) {
    if (!head || !new_weak) return;

    // Insert in specificity order (highest first)
    WeakDeclaration** current = head;

    while (*current &&
           css_declaration_cascade_compare((*current)->declaration, new_weak->declaration) >= 0) {
        current = &((*current)->next);
    }

    new_weak->next = *current;
    *current = new_weak;
}

static WeakDeclaration* weak_list_remove(WeakDeclaration** head, CssDeclaration* declaration) {
    if (!head || !*head || !declaration) return NULL;

    WeakDeclaration** current = head;

    while (*current) {
        if ((*current)->declaration == declaration) {
            WeakDeclaration* removed = *current;
            *current = removed->next;
            removed->next = NULL;
            return removed;
        }
        current = &((*current)->next);
    }

    return NULL;
}

// ============================================================================
// Style Node Implementation
// ============================================================================

static StyleNode* style_node_create(CssPropertyId property_id, Pool* pool) {
    StyleNode* node = (StyleNode*)pool_calloc(pool, sizeof(StyleNode));
    if (!node) return NULL;

    // Initialize base AVL node
    node->base.property_id = property_id;
    node->base.declaration = node; // Point to self for easy casting
    node->base.height = 1;
    node->base.left = NULL;
    node->base.right = NULL;
    node->base.parent = NULL;

    // Initialize style-specific fields
    node->winning_decl = NULL;
    node->weak_list = NULL;
    node->needs_recompute = true;
    node->computed_value = NULL;
    node->compute_version = 0;

    return node;
}

static void style_node_destroy(StyleNode* node) {
    if (!node) return;

    // Unreference winning declaration
    if (node->winning_decl) {
        css_declaration_unref(node->winning_decl);
    }

    // Unreference weak declarations
    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        WeakDeclaration* next = weak->next;
        weak_declaration_destroy(weak);
        weak = next;
    }

    // Memory managed by pool
}

CssDeclaration* style_node_resolve_cascade(StyleNode* node) {
    if (!node) return NULL;

    // The winning declaration is the highest priority one
    return node->winning_decl;
}

static bool style_node_apply_declaration(StyleNode* node, CssDeclaration* declaration, Pool* pool) {
    if (!node || !declaration) return false;

    // Compare with current winning declaration
    if (node->winning_decl) {
        int cmp = css_declaration_cascade_compare(declaration, node->winning_decl);
        // DEBUG: Log cascade comparison
        log_debug("[CASCADE] Prop %d: new(spec:%u,ord:%d) vs cur(spec:%u,ord:%d) => cmp=%d",
            declaration->property_id, css_specificity_to_value(declaration->specificity),
            declaration->source_order, css_specificity_to_value(node->winning_decl->specificity),
            node->winning_decl->source_order, cmp);

        if (cmp > 0) {
            // New declaration wins - demote current to weak list
            WeakDeclaration* weak = weak_declaration_create(node->winning_decl, pool);
            if (weak) {
                weak_list_insert(&node->weak_list, weak);
            }
            css_declaration_unref(node->winning_decl);

            node->winning_decl = declaration;
            css_declaration_ref(declaration);
        } else if (cmp < 0) {
            // New declaration loses - add to weak list
            WeakDeclaration* weak = weak_declaration_create(declaration, pool);
            if (weak) {
                weak_list_insert(&node->weak_list, weak);
            }
        } else {
            // Equal specificity - replace existing
            css_declaration_unref(node->winning_decl);
            node->winning_decl = declaration;
            css_declaration_ref(declaration);
        }
    } else {
        // First declaration for this property
        node->winning_decl = declaration;
        css_declaration_ref(declaration);
    }

    // Mark for recomputation
    node->needs_recompute = true;
    return true;
}

// ============================================================================
// Style Tree Implementation
// ============================================================================

StyleTree* style_tree_create(Pool* pool) {
    StyleTree* style_tree = (StyleTree*)pool_calloc(pool, sizeof(StyleTree));
    if (!style_tree) return NULL;

    style_tree->tree = avl_tree_create(pool);
    if (!style_tree->tree) {
        return NULL;
    }

    style_tree->pool = pool;
    style_tree->declaration_count = 0;
    style_tree->next_source_order = 1;
    style_tree->compute_version = 1;

    return style_tree;
}

void style_tree_destroy(StyleTree* style_tree) {
    if (!style_tree) return;

    // Destroy all style nodes
    if (style_tree->tree) {
        avl_tree_foreach_inorder(style_tree->tree, collect_nodes_callback, NULL);

        avl_tree_destroy(style_tree->tree);
    }

    // Memory managed by pool
}

void style_tree_clear(StyleTree* style_tree) {
    if (!style_tree) return;

    // This is a logical cascade reset, not an ownership teardown. Pooled
    // declarations can still be referenced by a reflowing pseudo view, so
    // only style_tree_destroy_owned may reclaim their records.
    if (style_tree->tree) {
        avl_tree_clear(style_tree->tree);
    }

    style_tree->declaration_count = 0;
    style_tree->next_source_order = 1;
    style_tree->compute_version++;
}

StyleNode* style_tree_apply_declaration(StyleTree* style_tree, CssDeclaration* declaration) {
    if (!style_tree || !declaration) return NULL;

    // Set source order
    declaration->source_order = style_tree->next_source_order++;

    // Find or create style node for this property
    AvlNode* avl_node = avl_tree_search(style_tree->tree, declaration->property_id);
    StyleNode* node = NULL;

    if (avl_node) {
        // Existing property
        node = (StyleNode*)avl_node->declaration;
    } else {
        // New property
        node = style_node_create(declaration->property_id, style_tree->pool);
        if (!node) return NULL;

        AvlNode* inserted = avl_tree_insert(style_tree->tree, declaration->property_id, node);
        if (!inserted) {
            style_node_destroy(node);
            return NULL;
        }
    }

    // Apply declaration to node
    if (style_node_apply_declaration(node, declaration, style_tree->pool)) {
        style_tree->declaration_count++;
        return node;
    }

    return NULL;
}

CssDeclaration* style_tree_get_declaration(StyleTree* style_tree, CssPropertyId property_id) {
    if (!style_tree) return NULL;

    AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
    if (!avl_node) return NULL;

    StyleNode* node = (StyleNode*)avl_node->declaration;
    return style_node_resolve_cascade(node);
}

void* style_tree_get_computed_value(StyleTree* style_tree,
                                   CssPropertyId property_id,
                                   StyleTree* parent_tree) {
    if (!style_tree) return NULL;

    AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
    if (!avl_node) {
        // Check for inheritance
        if (css_property_is_inherited(property_id) && parent_tree) {
            return style_tree_get_computed_value(parent_tree, property_id, NULL);
        }

        // Return initial value
        return css_property_get_initial_value(property_id, style_tree->pool);
    }

    StyleNode* node = (StyleNode*)avl_node->declaration;
    return style_node_get_computed_value(node, parent_tree);
}

bool style_tree_remove_property(StyleTree* style_tree, CssPropertyId property_id) {
    if (!style_tree) return false;

    AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
    if (!avl_node) return false;

    StyleNode* node = (StyleNode*)avl_node->declaration;
    style_node_destroy(node);

    void* removed = avl_tree_remove(style_tree->tree, property_id);
    return removed != NULL;
}

bool style_tree_remove_declaration(StyleTree* style_tree, CssDeclaration* declaration) {
    if (!style_tree || !declaration) return false;

    AvlNode* avl_node = avl_tree_search(style_tree->tree, declaration->property_id);
    if (!avl_node) return false;

    StyleNode* node = (StyleNode*)avl_node->declaration;

    // Check if this is the winning declaration
    if (node->winning_decl == declaration) {
        css_declaration_unref(node->winning_decl);
        node->winning_decl = NULL;

        // Promote the highest weak declaration
        if (node->weak_list) {
            WeakDeclaration* promoted = node->weak_list;
            node->weak_list = promoted->next;

            node->winning_decl = promoted->declaration;
            // Don't unref - we're transferring ownership

            // Free the weak declaration struct
            promoted->next = NULL;
            promoted->declaration = NULL; // Don't unref
        }

        node->needs_recompute = true;
        return true;
    }

    // Check weak list
    WeakDeclaration* removed = weak_list_remove(&node->weak_list, declaration);
    if (removed) {
        weak_declaration_destroy(removed);
        return true;
    }

    return false;
}

bool style_tree_remove_inline_declarations(StyleTree* style_tree) {
    if (!style_tree) return false;

    bool removed_any = false;

    for (int property_id = CSS_PROPERTY_DISPLAY; property_id < CSS_PROPERTY_COUNT; property_id++) {
        bool property_changed = true;

        while (property_changed) {
            property_changed = false;

            AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
            if (!avl_node) break;

            StyleNode* node = (StyleNode*)avl_node->declaration;
            if (!node) break;

            CssDeclaration* inline_decl = NULL;
            if (node->winning_decl && node->winning_decl->specificity.inline_style) {
                inline_decl = node->winning_decl;
            } else {
                WeakDeclaration* weak = node->weak_list;
                while (weak) {
                    if (weak->declaration && weak->declaration->specificity.inline_style) {
                        inline_decl = weak->declaration;
                        break;
                    }
                    weak = weak->next;
                }
            }

            if (!inline_decl) break;

            if (style_tree_remove_declaration(style_tree, inline_decl)) {
                if (style_tree->declaration_count > 0) {
                    style_tree->declaration_count--;
                }
                removed_any = true;
                property_changed = true;
            }
        }

        AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
        if (avl_node) {
            StyleNode* node = (StyleNode*)avl_node->declaration;
            if (node && !node->winning_decl && !node->weak_list) {
                style_node_destroy(node);
                avl_tree_remove(style_tree->tree, property_id);
            }
        }
    }

    if (removed_any) {
        style_tree_invalidate_computed_values(style_tree);
    }

    return removed_any;
}

bool style_tree_remove_non_inline_declarations(StyleTree* style_tree) {
    if (!style_tree) return false;

    bool removed_any = false;

    for (int property_id = CSS_PROPERTY_DISPLAY; property_id < CSS_PROPERTY_COUNT; property_id++) {
        bool property_changed = true;

        while (property_changed) {
            property_changed = false;

            AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
            if (!avl_node) break;

            StyleNode* node = (StyleNode*)avl_node->declaration;
            if (!node) break;

            CssDeclaration* stylesheet_decl = NULL;
            if (node->winning_decl && !node->winning_decl->specificity.inline_style) {
                stylesheet_decl = node->winning_decl;
            } else {
                WeakDeclaration* weak = node->weak_list;
                while (weak) {
                    if (weak->declaration && !weak->declaration->specificity.inline_style) {
                        stylesheet_decl = weak->declaration;
                        break;
                    }
                    weak = weak->next;
                }
            }

            if (!stylesheet_decl) break;

            if (style_tree_remove_declaration(style_tree, stylesheet_decl)) {
                if (style_tree->declaration_count > 0) {
                    style_tree->declaration_count--;
                }
                removed_any = true;
                property_changed = true;
            }
        }

        AvlNode* avl_node = avl_tree_search(style_tree->tree, property_id);
        if (avl_node) {
            StyleNode* node = (StyleNode*)avl_node->declaration;
            if (node && !node->winning_decl && !node->weak_list) {
                style_node_destroy(node);
                avl_tree_remove(style_tree->tree, property_id);
            }
        }
    }

    if (removed_any) {
        style_tree_invalidate_computed_values(style_tree);
    }

    return removed_any;
}

// ============================================================================
// Style Inheritance Implementation
// ============================================================================

bool css_should_inherit_property(CssPropertyId property_id, CssDeclaration* declaration) {
    // Check for explicit inherit keyword
    if (declaration && declaration->value) {
        // This would check if the value is the "inherit" keyword
        // For now, simplified implementation
    }

    // Check if property inherits by default
    return css_property_is_inherited(property_id);
}

bool style_tree_inherit_property(StyleTree* child_tree,
                                StyleTree* parent_tree,
                                CssPropertyId property_id) {
    if (!child_tree || !parent_tree) return false;

    // Get parent's computed value
    void* parent_value = style_tree_get_computed_value(parent_tree, property_id, NULL);
    if (!parent_value) return false;

    // Create inherited declaration
    CssSpecificity inherit_spec = css_specificity_create(0, 0, 0, 0, false);
    CssDeclaration* inherit_decl = css_declaration_create(
        property_id, parent_value, inherit_spec, CSS_ORIGIN_AUTHOR, child_tree->pool);

    if (!inherit_decl) return false;

    // Apply to child tree (only if no existing declaration)
    AvlNode* existing = avl_tree_search(child_tree->tree, property_id);
    if (!existing) {
        StyleNode* node = style_tree_apply_declaration(child_tree, inherit_decl);
        return node != NULL;
    }

    css_declaration_unref(inherit_decl);
    return false;
}

int style_tree_apply_inheritance(StyleTree* child_tree, StyleTree* parent_tree) {
    if (!child_tree || !parent_tree) return 0;

    int inherited_count = 0;

    // Iterate through all inherited properties
    // This would typically iterate through all properties that inherit by default
    // For brevity, showing the pattern with a few common inherited properties

    CssPropertyId inherited_props[] = {
        CSS_PROPERTY_COLOR,
        CSS_PROPERTY_FONT_FAMILY,
        CSS_PROPERTY_FONT_SIZE,
        CSS_PROPERTY_FONT_WEIGHT,
        CSS_PROPERTY_FONT_STYLE,
        CSS_PROPERTY_LINE_HEIGHT,
        CSS_PROPERTY_TEXT_ALIGN,
        CSS_PROPERTY_TEXT_TRANSFORM,
        CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_CURSOR
    };

    int prop_count = sizeof(inherited_props) / sizeof(inherited_props[0]);

    for (int i = 0; i < prop_count; i++) {
        if (style_tree_inherit_property(child_tree, parent_tree, inherited_props[i])) {
            inherited_count++;
        }
    }

    return inherited_count;
}

// ============================================================================
// Computed Value Implementation
// ============================================================================

void style_tree_invalidate_computed_values(StyleTree* style_tree) {
    if (!style_tree) return;

    style_tree->compute_version++;

    avl_tree_foreach_inorder(style_tree->tree, collect_computed_callback, NULL);
}

void* style_node_compute_value(StyleNode* node, StyleTree* parent_tree) {
    if (!node || !node->winning_decl) return NULL;

    CssDeclaration* decl = node->winning_decl;

    // For basic properties, just return the declaration's value
    // In a full implementation, this would handle value computation for inherit, initial, etc.
    return decl->value;
}

void* style_node_get_computed_value(StyleNode* node, StyleTree* parent_tree) {
    if (!node) return NULL;

    // Check cache validity
    if (!node->needs_recompute && node->computed_value) {
        return node->computed_value;
    }

    // Compute value
    node->computed_value = style_node_compute_value(node, parent_tree);
    node->needs_recompute = false;

    return node->computed_value;
}

// ============================================================================
// Style Tree Traversal and Debugging
// ============================================================================

int style_tree_foreach(StyleTree* style_tree, style_tree_callback_t callback, void* context) {
    if (!style_tree || !callback) return 0;

    struct CallbackWrapper wrapper = { callback, context, 0 };

    avl_tree_foreach_inorder(style_tree->tree, wrapper_callback, &wrapper);

    return wrapper.count;
}

void style_tree_print(StyleTree* style_tree) {
#ifndef NDEBUG
    if (!style_tree) {
        printf("StyleTree: NULL\n");
        return;
    }

    printf("StyleTree: %d declarations, %d properties\n",
           style_tree->declaration_count, avl_tree_size(style_tree->tree));

    style_tree_foreach(style_tree, print_tree_callback, NULL);
#endif
}

void style_node_print_cascade(StyleNode* node) {
#ifndef NDEBUG
    if (!node) {
        printf("StyleNode: NULL\n");
        return;
    }

    const char* prop_name = css_get_property_name(static_cast<CssPropertyId>(node->base.property_id));
    printf("StyleNode for %s (ID: %lu):\n",
           prop_name ? prop_name : "unknown", node->base.property_id);

    if (node->winning_decl) {
        printf("  Winning: ");
        css_specificity_print(node->winning_decl->specificity);
        printf(" (order: %d)\n", node->winning_decl->source_order);
    } else {
        printf("  No winning declaration\n");
    }

    WeakDeclaration* weak = node->weak_list;
    int weak_index = 0;
    while (weak) {
        printf("  Weak[%d]: ", weak_index++);
        css_specificity_print(weak->declaration->specificity);
        printf(" (order: %d)\n", weak->declaration->source_order);
        weak = weak->next;
    }
#endif
}

void style_tree_get_statistics(StyleTree* style_tree, int* total_nodes,
    int* total_declarations, double* avg_weak_count) {
    if (!style_tree) {
        if (total_nodes) *total_nodes = 0;
        if (total_declarations) *total_declarations = 0;
        if (avg_weak_count) *avg_weak_count = 0.0;
        return;
    }

    if (total_nodes) *total_nodes = avl_tree_size(style_tree->tree);
    if (total_declarations) *total_declarations = style_tree->declaration_count;

    if (avg_weak_count) {
        int total_weak = 0;
        int node_count = 0;

        style_tree_foreach(style_tree, validate_tree_callback, &total_weak);

        node_count = avl_tree_size(style_tree->tree);
        *avg_weak_count = node_count > 0 ? (double)total_weak / node_count : 0.0;
    }
}

// ============================================================================
// Advanced Style Operations
// ============================================================================

// Lifetime contract (CSS value retention audit, Memory_Safety_Template4.md §10
// Phase 4): this is a SHALLOW clone. clone_tree_callback re-applies the source
// node's CssDeclaration* (and therefore its CssValue*) into the cloned tree by
// reference + refcount. Those declarations stay owned by SOURCE's pool, not by
// target_pool. Refcounting does not protect across pools — pool free reclaims
// memory regardless of ref_count. Therefore: the pool backing `source` must
// outlive every tree cloned from it. To produce a self-contained clone whose
// values live in target_pool, a deep CssValue copy would be required (none
// exists yet). Currently only exercised by tests, where source outlives clone.
StyleTree* style_tree_clone(StyleTree* source, Pool* target_pool) {
    if (!source || !target_pool) return NULL;

    StyleTree* cloned = style_tree_create(target_pool);
    if (!cloned) return NULL;

    // Create proper clone context
    int cloned_count = 0;
    struct CloneContext clone_context = { cloned, &cloned_count };

    // Clone all declarations
    style_tree_foreach(source, clone_tree_callback, &clone_context);

    return cloned;
}

struct OwnedCloneCollectContext {
    CssDeclaration** declarations;
    size_t count;
    size_t capacity;
};

static bool owned_clone_count_declarations(StyleNode* node, void* context) {
    size_t* count = (size_t*)context;
    if (node->winning_decl) (*count)++;
    for (WeakDeclaration* weak = node->weak_list; weak; weak = weak->next) (*count)++;
    return true;
}

static bool owned_clone_collect_declarations(StyleNode* node, void* context) {
    OwnedCloneCollectContext* collect = (OwnedCloneCollectContext*)context;
    if (node->winning_decl && collect->count < collect->capacity) {
        collect->declarations[collect->count++] = node->winning_decl;
    }
    for (WeakDeclaration* weak = node->weak_list;
         weak && collect->count < collect->capacity; weak = weak->next) {
        collect->declarations[collect->count++] = weak->declaration;
    }
    return true;
}

static int declaration_source_order_compare(const void* left, const void* right) {
    const CssDeclaration* a = *(CssDeclaration* const*)left;
    const CssDeclaration* b = *(CssDeclaration* const*)right;
    if (a->source_order != b->source_order) {
        return a->source_order < b->source_order ? -1 : 1;
    }
    if (a->property_id != b->property_id) {
        return a->property_id < b->property_id ? -1 : 1;
    }
    return 0;
}

StyleTree* style_tree_clone_owned(StyleTree* source, Pool* target_pool) {
    if (!source || !target_pool) return NULL;
    StyleTree* clone = style_tree_create(target_pool);
    if (!clone) return NULL;

    size_t count = 0;
    style_tree_foreach(source, owned_clone_count_declarations, &count);
    if (count == 0) return clone;
    CssDeclaration** declarations = (CssDeclaration**)pool_alloc(
        target_pool, count * sizeof(CssDeclaration*));
    if (!declarations) {
        style_tree_destroy_owned(clone);
        return NULL;
    }
    OwnedCloneCollectContext collect = {declarations, 0, count};
    style_tree_foreach(source, owned_clone_collect_declarations, &collect);
    qsort(declarations, collect.count, sizeof(CssDeclaration*),
          declaration_source_order_compare);

    for (size_t i = 0; i < collect.count; i++) {
        CssDeclaration* source_decl = declarations[i];
        CssDeclaration* copy = css_declaration_clone_owned(
            source_decl, source_decl->specificity, source_decl->origin, target_pool);
        if (!copy || !style_tree_apply_declaration(clone, copy)) {
            pool_free(target_pool, declarations);
            style_tree_destroy_owned(clone);
            return NULL;
        }
    }
    pool_free(target_pool, declarations);
    return clone;
}

static void css_declaration_destroy_tree_copy(CssDeclaration* declaration,
                                               Pool* pool) {
    if (!declaration || !declaration->tree_owned_record) return;
    if (declaration->owns_payload) {
        css_value_destroy_owned(declaration->value, pool);
        pool_free(pool, (void*)declaration->source_file);
        pool_free(pool, (void*)declaration->property_name);
        pool_free(pool, (void*)declaration->value_text);
    }
    pool_free(pool, declaration);
}

static void style_tree_reclaim_branch(AvlNode* avl_node, Pool* pool) {
    if (!avl_node) return;
    AvlNode* left = avl_node->left;
    AvlNode* right = avl_node->right;
    style_tree_reclaim_branch(left, pool);
    style_tree_reclaim_branch(right, pool);

    StyleNode* node = (StyleNode*)avl_node->declaration;
    if (node) {
        css_declaration_destroy_tree_copy(node->winning_decl, pool);
        WeakDeclaration* weak = node->weak_list;
        while (weak) {
            WeakDeclaration* next = weak->next;
            css_declaration_destroy_tree_copy(weak->declaration, pool);
            pool_free(pool, weak);
            weak = next;
        }
        pool_free(pool, node);
    }
    pool_free(pool, avl_node);
}

void style_tree_destroy_owned(StyleTree* style_tree) {
    if (!style_tree) return;
    // Canonical epoch trees disappear only with their entire epoch pool; an
    // element-level destroy would invalidate every other shared binding.
    assert(style_tree->canonical_owner == NULL);
    Pool* pool = style_tree->pool;
    if (style_tree->tree) {
        style_tree_reclaim_branch(style_tree->tree->root, pool);
        pool_free(pool, style_tree->tree);
    }
    pool_free(pool, style_tree);
}

static bool style_tree_find_inline(StyleNode* node, void* context) {
    bool* found = (bool*)context;
    if (node->winning_decl && node->winning_decl->specificity.inline_style) {
        *found = true;
        return false;
    }
    for (WeakDeclaration* weak = node->weak_list; weak; weak = weak->next) {
        if (weak->declaration && weak->declaration->specificity.inline_style) {
            *found = true;
            return false;
        }
    }
    return true;
}

bool style_tree_has_inline_declarations(StyleTree* style_tree) {
    bool found = false;
    if (style_tree) style_tree_foreach(style_tree, style_tree_find_inline, &found);
    return found;
}

bool style_tree_is_empty(StyleTree* style_tree) {
    return !style_tree || !style_tree->tree || style_tree->tree->node_count == 0;
}

static bool declaration_winner_equal(const CssDeclaration* left,
                                     const CssDeclaration* right) {
    if (left == right) return true;
    if (!left || !right || left->property_id != right->property_id ||
        left->origin != right->origin || left->important != right->important ||
        memcmp(&left->specificity, &right->specificity,
               sizeof(CssSpecificity)) != 0) return false;
    if (left->value_text && right->value_text) {
        return left->value_text_len == right->value_text_len &&
            memcmp(left->value_text, right->value_text,
                   left->value_text_len) == 0;
    }
    // Without a canonical serialization, distinct value graphs are uncertain
    // and intentionally fail toward non-sharing in debug verification.
    return left->value == right->value;
}

bool style_tree_winners_equal(StyleTree* left, StyleTree* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    for (int property = CSS_PROPERTY_DISPLAY; property < CSS_PROPERTY_COUNT; property++) {
        if (!declaration_winner_equal(
                style_tree_get_declaration(left, (CssPropertyId)property),
                style_tree_get_declaration(right, (CssPropertyId)property))) {
            return false;
        }
    }
    return true;
}

int style_tree_merge(StyleTree* target, StyleTree* source) {
    if (!target || !source) return 0;

    int merged_count = 0;

    struct MergeContext merge_context = { target, &merged_count };
    style_tree_foreach(source, merge_tree_callback, &merge_context);

    return merged_count;
}

StyleTree* style_tree_create_subset(StyleTree* source,
                                   CssPropertyId* property_ids,
                                   int property_count,
                                   Pool* target_pool) {
    if (!source || !property_ids || property_count <= 0 || !target_pool) {
        return NULL;
    }

    StyleTree* subset = style_tree_create(target_pool);
    if (!subset) return NULL;

    for (int i = 0; i < property_count; i++) {
        AvlNode* avl_node = avl_tree_search(source->tree, property_ids[i]);
        if (avl_node) {
            StyleNode* node = (StyleNode*)avl_node->declaration;

            // Copy winning declaration.
            // Lifetime contract (CSS value retention audit, Memory_Safety_Template4.md
            // §10 Phase 4): the declaration struct is re-created in target_pool, but
            // its CssValue* is ALIASED from source's pool (no deep value copy exists).
            // The pool backing `source` must therefore outlive `subset`. Only
            // exercised by tests today.
            if (node->winning_decl) {
                CssDeclaration* copied = css_declaration_create(
                    node->winning_decl->property_id,
                    node->winning_decl->value,
                    node->winning_decl->specificity,
                    node->winning_decl->origin,
                    target_pool);

                if (copied) {
                    copied->source_order = node->winning_decl->source_order;
                    style_tree_apply_declaration(subset, copied);
                }
            }

            // Copy weak declarations
            WeakDeclaration* weak = node->weak_list;
            while (weak) {
                CssDeclaration* copied = css_declaration_create(
                    weak->declaration->property_id,
                    weak->declaration->value,
                    weak->declaration->specificity,
                    weak->declaration->origin,
                    target_pool);

                if (copied) {
                    copied->source_order = weak->declaration->source_order;
                    style_tree_apply_declaration(subset, copied);
                }

                weak = weak->next;
            }
        }
    }

    return subset;
}

// ============================================================================
// Callback Function Implementations
// ============================================================================

static bool collect_nodes_callback(AvlNode* avl_node, void* context) {
    StyleNode* node = (StyleNode*)avl_node->declaration;
    style_node_destroy(node);
    return true;
}

static bool collect_computed_callback(AvlNode* avl_node, void* context) {
    StyleNode* node = (StyleNode*)avl_node->declaration;
    node->needs_recompute = true;
    node->computed_value = NULL;
    return true;
}

#ifndef NDEBUG
static bool print_tree_callback(StyleNode* node, void* context) {
    printf("  Property %lu: ", node->base.property_id);

    if (node->winning_decl) {
        printf("winning ");
        css_specificity_print(node->winning_decl->specificity);
    } else {
        printf("no winning declaration");
    }

    // Count weak declarations
    int weak_count = 0;
    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        weak_count++;
        weak = weak->next;
    }

    if (weak_count > 0) {
        printf(", %d weak", weak_count);
    }

    printf("\n");
    return true;
}
#endif

static bool validate_tree_callback(StyleNode* node, void* context) {
    int* total_weak = (int*)context;

    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        (*total_weak)++;
        weak = weak->next;
    }

    return true;
}

static bool merge_tree_callback(StyleNode* node, void* context) {
    struct MergeContext* merge_ctx = (struct MergeContext*)context;

    // Merge the winning declaration from source to target
    if (node && node->winning_decl && merge_ctx->target) {
        style_tree_apply_declaration(merge_ctx->target, node->winning_decl);
        if (merge_ctx->merged_count) {
            (*(merge_ctx->merged_count))++;
        }
    }

    return true;
}

static bool clone_tree_callback(StyleNode* node, void* context) {
    struct CloneContext* ctx = (struct CloneContext*)context;

    // Clone the winning declaration if it exists
    if (node && node->winning_decl && ctx->target) {
        style_tree_apply_declaration(ctx->target, node->winning_decl);
        if (ctx->cloned_count) {
            (*(ctx->cloned_count))++;
        }
    }
    return true;
}

// Helper callback for wrapper pattern
static bool wrapper_callback(AvlNode* avl_node, void* ctx) {
    struct CallbackWrapper* wrapper = (struct CallbackWrapper*)ctx;
    StyleNode* node = (StyleNode*)avl_node->declaration;

    if (wrapper->user_callback(node, wrapper->user_context)) {
        wrapper->count++;
    }

    return true;
}
