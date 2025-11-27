#include "css_style_node.hpp"
#include "css_style.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Forward declarations for callback functions
static bool collect_nodes_callback(AvlNode* avl_node, void* context);
static bool collect_computed_callback(AvlNode* avl_node, void* context);
static bool print_cascade_callback(AvlNode* avl_node, void* context);
static bool print_tree_callback(StyleNode* node, void* context);
static bool validate_tree_callback(StyleNode* node, void* context);
static bool collect_selectors_callback(StyleNode* node, void* context);
static bool merge_tree_callback(StyleNode* node, void* context);
static bool clone_tree_callback(StyleNode* node, void* context);
static bool wrapper_callback(AvlNode* avl_node, void* ctx);
static int css_get_cascade_level(CssDeclaration* decl);

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
    printf("(%d,%d,%d,%d)%s",
           specificity.inline_style,
           specificity.ids,
           specificity.classes,
           specificity.elements,
           specificity.important ? "!" : "");
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

int css_declaration_cascade_compare(CssDeclaration* a, CssDeclaration* b) {
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
static int css_get_cascade_level(CssDeclaration* decl) {
    if (decl->specificity.important) {
        // Important declarations (reverse origin order)
        switch (decl->origin) {
            case CSS_ORIGIN_USER_AGENT: return 7;
            case CSS_ORIGIN_USER: return 6;
            case CSS_ORIGIN_AUTHOR: return 5;
            case CSS_ORIGIN_ANIMATION: return 4; // Animations don't have !important
            case CSS_ORIGIN_TRANSITION: return 4; // Transitions don't have !important
        }
    } else {
        // Normal declarations
        switch (decl->origin) {
            case CSS_ORIGIN_USER_AGENT: return 1;
            case CSS_ORIGIN_USER: return 2;
            case CSS_ORIGIN_AUTHOR: return 3;
            case CSS_ORIGIN_ANIMATION: return 4;
            case CSS_ORIGIN_TRANSITION: return 4;
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

    // Destroy all nodes
    if (style_tree->tree) {
        avl_tree_foreach_inorder(style_tree->tree, collect_nodes_callback, NULL);

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
    if (!style_tree) {
        printf("StyleTree: NULL\n");
        return;
    }

    printf("StyleTree: %d declarations, %d properties\n",
           style_tree->declaration_count, avl_tree_size(style_tree->tree));

    style_tree_foreach(style_tree, print_tree_callback, NULL);
}

void style_node_print_cascade(StyleNode* node) {
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

            // Copy winning declaration
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

static bool print_cascade_callback(AvlNode* avl_node, void* context) {
    StyleNode* node = (StyleNode*)avl_node->declaration;
    CssPropertyId property_id = static_cast<CssPropertyId>(avl_node->property_id);
    const char* property_name = css_property_get_name(property_id);

    printf("    %s: ", property_name ? property_name : "unknown");

    if (node->winning_decl) {
        printf("(declaration present, specificity: %u, source: %d)\n",
               css_specificity_to_value(node->winning_decl->specificity),
               node->winning_decl->source_order);
    } else {
        printf("(no value)\n");
    }
    return true;
}

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

static bool validate_tree_callback(StyleNode* node, void* context) {
    int* total_weak = (int*)context;

    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        (*total_weak)++;
        weak = weak->next;
    }

    return true;
}

static bool collect_selectors_callback(StyleNode* node, void* context) {
    struct CollectSelectorsContext* ctx = (struct CollectSelectorsContext*)context;

    // Simplified - we don't have selector_str in our structure
    if (node->winning_decl) {
        if (ctx->count && *(ctx->count) < ctx->capacity) {
            ctx->selectors[*(ctx->count)] = (char*)"<selector>";
            (*(ctx->count))++;
        }
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
