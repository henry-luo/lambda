#include "view_tree.h"
#include "../../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ViewTree implementation

ViewTree* view_tree_create(void) {
    ViewTree* tree = calloc(1, sizeof(ViewTree));
    if (!tree) return NULL;
    
    tree->ref_count = 1;
    tree->page_count = 0;
    tree->pages = NULL;
    tree->creator = strdup("Lambda Typesetting System");
    tree->creation_date = strdup("2025-07-29");
    
    return tree;
}

ViewTree* view_tree_create_with_root(ViewNode* root) {
    ViewTree* tree = view_tree_create();
    if (!tree) return NULL;
    
    tree->root = root;
    if (root) {
        view_node_retain(root);
    }
    
    return tree;
}

void view_tree_retain(ViewTree* tree) {
    if (tree) {
        tree->ref_count++;
    }
}

void view_tree_release(ViewTree* tree) {
    if (!tree) return;
    
    tree->ref_count--;
    if (tree->ref_count <= 0) {
        // Release root node
        if (tree->root) {
            view_node_release(tree->root);
        }
        
        // Release pages
        if (tree->pages) {
            for (int i = 0; i < tree->page_count; i++) {
                if (tree->pages[i]) {
                    free(tree->pages[i]);
                }
            }
            free(tree->pages);
        }
        
        // Free metadata
        free(tree->title);
        free(tree->author);
        free(tree->subject);
        free(tree->creator);
        free(tree->creation_date);
        
        free(tree);
    }
}

// ViewNode implementation

ViewNode* view_node_create(ViewNodeType type) {
    ViewNode* node = calloc(1, sizeof(ViewNode));
    if (!node) return NULL;
    
    node->type = type;
    node->ref_count = 1;
    node->visible = true;
    node->opacity = 1.0;
    
    // Initialize transform to identity
    node->transform.matrix[0] = 1.0; // a
    node->transform.matrix[1] = 0.0; // b
    node->transform.matrix[2] = 0.0; // c
    node->transform.matrix[3] = 1.0; // d
    node->transform.matrix[4] = 0.0; // tx
    node->transform.matrix[5] = 0.0; // ty
    
    return node;
}

ViewNode* view_node_create_text_run(const char* text, ViewFont* font, double font_size) {
    ViewNode* node = view_node_create(VIEW_NODE_TEXT_RUN);
    if (!node) return NULL;
    
    // Allocate text run content
    ViewTextRun* text_run = calloc(1, sizeof(ViewTextRun));
    if (!text_run) {
        view_node_release(node);
        return NULL;
    }
    
    text_run->text = strdup(text ? text : "");
    text_run->text_length = strlen(text_run->text);
    text_run->font = font; // Note: should retain font in real implementation
    text_run->font_size = font_size;
    text_run->color.r = 0.0;
    text_run->color.g = 0.0;
    text_run->color.b = 0.0;
    text_run->color.a = 1.0;
    
    // Basic width estimation (rough approximation)
    text_run->total_width = text_run->text_length * font_size * 0.6;
    text_run->ascent = font_size * 0.8;
    text_run->descent = font_size * 0.2;
    
    node->content.text_run = text_run;
    node->size.width = text_run->total_width;
    node->size.height = font_size;
    
    return node;
}

ViewNode* view_node_create_group(const char* name) {
    ViewNode* node = view_node_create(VIEW_NODE_GROUP);
    if (!node) return NULL;
    
    ViewGroup* group = calloc(1, sizeof(ViewGroup));
    if (!group) {
        view_node_release(node);
        return NULL;
    }
    
    group->name = strdup(name ? name : "group");
    // Initialize group transform to identity
    group->group_transform.matrix[0] = 1.0;
    group->group_transform.matrix[1] = 0.0;
    group->group_transform.matrix[2] = 0.0;
    group->group_transform.matrix[3] = 1.0;
    group->group_transform.matrix[4] = 0.0;
    group->group_transform.matrix[5] = 0.0;
    
    node->content.group = group;
    
    return node;
}

void view_node_retain(ViewNode* node) {
    if (node) {
        node->ref_count++;
    }
}

void view_node_release(ViewNode* node) {
    if (!node) return;
    
    node->ref_count--;
    if (node->ref_count <= 0) {
        // Release children
        ViewNode* child = node->first_child;
        while (child) {
            ViewNode* next = child->next_sibling;
            view_node_release(child);
            child = next;
        }
        
        // Release content based on type
        switch (node->type) {
            case VIEW_NODE_TEXT_RUN:
                if (node->content.text_run) {
                    free(node->content.text_run->text);
                    free(node->content.text_run->language);
                    free(node->content.text_run->glyphs);
                    free(node->content.text_run->glyph_positions);
                    free(node->content.text_run);
                }
                break;
            case VIEW_NODE_GROUP:
                if (node->content.group) {
                    free(node->content.group->name);
                    free(node->content.group);
                }
                break;
            // Add other content types as needed
            default:
                break;
        }
        
        // Free metadata
        free(node->id);
        free(node->class_name);
        free(node->semantic_role);
        
        free(node);
    }
}

// Hierarchy management

void view_node_add_child(ViewNode* parent, ViewNode* child) {
    if (!parent || !child) return;
    
    // Remove child from current parent if any
    view_node_remove_from_parent(child);
    
    // Set parent relationship
    child->parent = parent;
    
    // Add to parent's child list
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }
    
    parent->child_count++;
    view_node_retain(child);
}

void view_node_remove_from_parent(ViewNode* node) {
    if (!node || !node->parent) return;
    
    ViewNode* parent = node->parent;
    
    // Update sibling links
    if (node->prev_sibling) {
        node->prev_sibling->next_sibling = node->next_sibling;
    } else {
        parent->first_child = node->next_sibling;
    }
    
    if (node->next_sibling) {
        node->next_sibling->prev_sibling = node->prev_sibling;
    } else {
        parent->last_child = node->prev_sibling;
    }
    
    // Clear node's parent and sibling references
    node->parent = NULL;
    node->prev_sibling = NULL;
    node->next_sibling = NULL;
    
    parent->child_count--;
    view_node_release(node);
}

// Utility functions

ViewTransform view_transform_identity(void) {
    ViewTransform transform;
    transform.matrix[0] = 1.0; // a
    transform.matrix[1] = 0.0; // b
    transform.matrix[2] = 0.0; // c
    transform.matrix[3] = 1.0; // d
    transform.matrix[4] = 0.0; // tx
    transform.matrix[5] = 0.0; // ty
    return transform;
}

ViewTransform view_transform_translate(double dx, double dy) {
    ViewTransform transform = view_transform_identity();
    transform.matrix[4] = dx; // tx
    transform.matrix[5] = dy; // ty
    return transform;
}

ViewTransform view_transform_scale(double sx, double sy) {
    ViewTransform transform = view_transform_identity();
    transform.matrix[0] = sx; // a
    transform.matrix[3] = sy; // d
    return transform;
}

bool view_rect_contains_point(ViewRect rect, ViewPoint point) {
    return point.x >= rect.origin.x && 
           point.x <= rect.origin.x + rect.size.width &&
           point.y >= rect.origin.y && 
           point.y <= rect.origin.y + rect.size.height;
}

ViewRect view_rect_union(ViewRect rect1, ViewRect rect2) {
    ViewRect result;
    
    double left = (rect1.origin.x < rect2.origin.x) ? rect1.origin.x : rect2.origin.x;
    double top = (rect1.origin.y < rect2.origin.y) ? rect1.origin.y : rect2.origin.y;
    double right1 = rect1.origin.x + rect1.size.width;
    double right2 = rect2.origin.x + rect2.size.width;
    double bottom1 = rect1.origin.y + rect1.size.height;
    double bottom2 = rect2.origin.y + rect2.size.height;
    double right = (right1 > right2) ? right1 : right2;
    double bottom = (bottom1 > bottom2) ? bottom1 : bottom2;
    
    result.origin.x = left;
    result.origin.y = top;
    result.size.width = right - left;
    result.size.height = bottom - top;
    
    return result;
}

// Query functions

ViewNode* view_tree_find_node_by_id(ViewTree* tree, const char* id) {
    if (!tree || !tree->root || !id) return NULL;
    
    // Simple recursive search
    return view_node_find_by_id_recursive(tree->root, id);
}

ViewNode* view_node_find_by_id_recursive(ViewNode* node, const char* id) {
    if (!node || !id) return NULL;
    
    if (node->id && strcmp(node->id, id) == 0) {
        return node;
    }
    
    // Search children
    ViewNode* child = node->first_child;
    while (child) {
        ViewNode* result = view_node_find_by_id_recursive(child, id);
        if (result) return result;
        child = child->next_sibling;
    }
    
    return NULL;
}

ViewNode* view_tree_find_node_by_role(ViewTree* tree, const char* role) {
    if (!tree || !tree->root || !role) return NULL;
    
    return view_node_find_by_role_recursive(tree->root, role);
}

ViewNode* view_node_find_by_role_recursive(ViewNode* node, const char* role) {
    if (!node || !role) return NULL;
    
    if (node->semantic_role && strcmp(node->semantic_role, role) == 0) {
        return node;
    }
    
    // Search children
    ViewNode* child = node->first_child;
    while (child) {
        ViewNode* result = view_node_find_by_role_recursive(child, role);
        if (result) return result;
        child = child->next_sibling;
    }
    
    return NULL;
}

// Statistics

ViewStats* view_tree_calculate_stats(ViewTree* tree) {
    if (!tree) return NULL;
    
    ViewStats* stats = &tree->stats;
    memset(stats, 0, sizeof(ViewStats));
    
    if (tree->root) {
        view_node_calculate_stats_recursive(tree->root, stats);
    }
    
    return stats;
}

void view_node_calculate_stats_recursive(ViewNode* node, ViewStats* stats) {
    if (!node || !stats) return;
    
    stats->total_nodes++;
    
    switch (node->type) {
        case VIEW_NODE_TEXT_RUN:
            stats->text_runs++;
            if (node->content.text_run) {
                stats->total_text_length += node->content.text_run->text_length;
            }
            break;
        case VIEW_NODE_MATH_ELEMENT:
            stats->math_elements++;
            break;
        case VIEW_NODE_LINE:
        case VIEW_NODE_RECTANGLE:
        case VIEW_NODE_PATH:
            stats->geometric_elements++;
            break;
        default:
            break;
    }
    
    // Recurse through children
    ViewNode* child = node->first_child;
    while (child) {
        view_node_calculate_stats_recursive(child, stats);
        child = child->next_sibling;
    }
}
