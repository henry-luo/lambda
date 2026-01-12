// tex_event.cpp - Event System Implementation for TexNode Trees
//
// Implements hit testing, caret positioning, and selection support
// for interactive TexNode trees.

#include "tex_event.hpp"
#include "lib/log.h"
#include <cmath>
#include <algorithm>

namespace tex {

// ============================================================================
// Hit Testing Implementation
// ============================================================================

// Check if point (x, y) is within the bounding box of a node
static bool point_in_node(TexNode* node, float x, float y) {
    if (!node) return false;

    // node position is relative to parent
    // x, y are coordinates relative to the same origin as node->x, node->y
    float left = node->x;
    float right = node->x + node->width;
    float top = node->y - node->height;
    float bottom = node->y + node->depth;

    return x >= left && x <= right && y >= top && y <= bottom;
}

// Recursive hit test helper
static TexHitResult hit_test_recursive(TexNode* node, float x, float y, float parent_x, float parent_y) {
    TexHitResult result;

    if (!node) return result;

    // Adjust coordinates to be relative to this node's parent
    float rel_x = x - parent_x;
    float rel_y = y - parent_y;

    // Check if point is within this node's bounds
    if (!point_in_node(node, rel_x, rel_y)) {
        return result;
    }

    // Point is within this node - now check children for deeper hit
    float child_origin_x = parent_x + node->x;
    float child_origin_y = parent_y + node->y;

    // Check children (depth-first, last child first for front-to-back order)
    for (TexNode* child = node->last_child; child; child = child->prev_sibling) {
        TexHitResult child_result = hit_test_recursive(child, x, y, child_origin_x, child_origin_y);
        if (child_result.hit()) {
            return child_result;  // Deeper hit found
        }
    }

    // Also check special content nodes (fraction numerator/denominator, etc.)
    switch (node->node_class) {
        case NodeClass::Fraction:
            if (node->content.frac.numerator) {
                TexHitResult num_result = hit_test_recursive(node->content.frac.numerator, x, y, child_origin_x, child_origin_y);
                if (num_result.hit()) return num_result;
            }
            if (node->content.frac.denominator) {
                TexHitResult denom_result = hit_test_recursive(node->content.frac.denominator, x, y, child_origin_x, child_origin_y);
                if (denom_result.hit()) return denom_result;
            }
            break;

        case NodeClass::Radical:
            if (node->content.radical.radicand) {
                TexHitResult rad_result = hit_test_recursive(node->content.radical.radicand, x, y, child_origin_x, child_origin_y);
                if (rad_result.hit()) return rad_result;
            }
            if (node->content.radical.degree) {
                TexHitResult deg_result = hit_test_recursive(node->content.radical.degree, x, y, child_origin_x, child_origin_y);
                if (deg_result.hit()) return deg_result;
            }
            break;

        case NodeClass::Scripts:
            if (node->content.scripts.nucleus) {
                TexHitResult nuc_result = hit_test_recursive(node->content.scripts.nucleus, x, y, child_origin_x, child_origin_y);
                if (nuc_result.hit()) return nuc_result;
            }
            if (node->content.scripts.subscript) {
                TexHitResult sub_result = hit_test_recursive(node->content.scripts.subscript, x, y, child_origin_x, child_origin_y);
                if (sub_result.hit()) return sub_result;
            }
            if (node->content.scripts.superscript) {
                TexHitResult sup_result = hit_test_recursive(node->content.scripts.superscript, x, y, child_origin_x, child_origin_y);
                if (sup_result.hit()) return sup_result;
            }
            break;

        case NodeClass::Accent:
            if (node->content.accent.base) {
                TexHitResult base_result = hit_test_recursive(node->content.accent.base, x, y, child_origin_x, child_origin_y);
                if (base_result.hit()) return base_result;
            }
            break;

        default:
            break;
    }

    // No deeper hit - this node is the deepest
    result.node = node;
    result.local_x = rel_x - node->x;
    result.local_y = rel_y - node->y;

    // For character nodes, determine if caret should be before or after
    if (node->node_class == NodeClass::Char || node->node_class == NodeClass::MathChar) {
        result.char_index = 0;
        result.is_before = (result.local_x < node->width / 2);
    }

    return result;
}

TexHitResult tex_hit_test(TexNode* root, float x, float y) {
    return hit_test_recursive(root, x, y, 0, 0);
}

// ============================================================================
// Caret Position Implementation
// ============================================================================

TexCaret tex_caret_from_hit(const TexHitResult& hit) {
    TexCaret caret;

    if (!hit.hit()) return caret;

    caret.node = hit.node;
    caret.position = hit.is_before ? 0 : 1;

    // Calculate visual position
    // Walk up to get absolute position
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = hit.node; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }

    if (hit.is_before) {
        caret.x = abs_x;
    } else {
        caret.x = abs_x + hit.node->width;
    }
    caret.y = abs_y;
    caret.height = hit.node->height;
    caret.depth = hit.node->depth;

    return caret;
}

// Find the leftmost leaf node in a subtree
static TexNode* find_leftmost_leaf(TexNode* node) {
    if (!node) return nullptr;

    while (node->first_child) {
        node = node->first_child;
    }

    // Also check content nodes for special cases
    switch (node->node_class) {
        case NodeClass::Fraction:
            if (node->content.frac.numerator) {
                return find_leftmost_leaf(node->content.frac.numerator);
            }
            break;
        case NodeClass::Scripts:
            if (node->content.scripts.nucleus) {
                return find_leftmost_leaf(node->content.scripts.nucleus);
            }
            break;
        case NodeClass::Radical:
            if (node->content.radical.radicand) {
                return find_leftmost_leaf(node->content.radical.radicand);
            }
            break;
        default:
            break;
    }

    return node;
}

// Find the rightmost leaf node in a subtree
static TexNode* find_rightmost_leaf(TexNode* node) {
    if (!node) return nullptr;

    while (node->last_child) {
        node = node->last_child;
    }

    // Also check content nodes for special cases
    switch (node->node_class) {
        case NodeClass::Fraction:
            if (node->content.frac.denominator) {
                return find_rightmost_leaf(node->content.frac.denominator);
            }
            break;
        case NodeClass::Scripts:
            if (node->content.scripts.superscript) {
                return find_rightmost_leaf(node->content.scripts.superscript);
            } else if (node->content.scripts.subscript) {
                return find_rightmost_leaf(node->content.scripts.subscript);
            } else if (node->content.scripts.nucleus) {
                return find_rightmost_leaf(node->content.scripts.nucleus);
            }
            break;
        case NodeClass::Radical:
            if (node->content.radical.radicand) {
                return find_rightmost_leaf(node->content.radical.radicand);
            }
            break;
        default:
            break;
    }

    return node;
}

TexCaret tex_caret_start(TexNode* root) {
    TexCaret caret;
    if (!root) return caret;

    TexNode* leftmost = find_leftmost_leaf(root);
    if (!leftmost) return caret;

    caret.node = leftmost;
    caret.position = 0;

    // Calculate absolute position
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = leftmost; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }
    caret.x = abs_x;
    caret.y = abs_y;
    caret.height = leftmost->height;
    caret.depth = leftmost->depth;

    return caret;
}

TexCaret tex_caret_end(TexNode* root) {
    TexCaret caret;
    if (!root) return caret;

    TexNode* rightmost = find_rightmost_leaf(root);
    if (!rightmost) return caret;

    caret.node = rightmost;
    caret.position = 1;  // After the node

    // Calculate absolute position
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = rightmost; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }
    caret.x = abs_x + rightmost->width;
    caret.y = abs_y;
    caret.height = rightmost->height;
    caret.depth = rightmost->depth;

    return caret;
}

// ============================================================================
// Caret Navigation Implementation
// ============================================================================

// Find next sibling or go up to parent's next sibling
static TexNode* find_next_node(TexNode* node, TexNode* root) {
    if (!node || node == root) return nullptr;

    // If has next sibling, go to leftmost of it
    if (node->next_sibling) {
        return find_leftmost_leaf(node->next_sibling);
    }

    // Otherwise, go up and try again
    return find_next_node(node->parent, root);
}

// Find previous sibling or go up to parent's previous sibling
static TexNode* find_prev_node(TexNode* node, TexNode* root) {
    if (!node || node == root) return nullptr;

    // If has previous sibling, go to rightmost of it
    if (node->prev_sibling) {
        return find_rightmost_leaf(node->prev_sibling);
    }

    // Otherwise, go up and try again
    return find_prev_node(node->parent, root);
}

TexCaret tex_caret_move_left(TexNode* root, const TexCaret& current) {
    if (!current.valid()) return current;

    TexCaret result = current;

    // If position is after the node, move to before
    if (current.position > 0) {
        result.position = 0;
        result.x -= current.node->width;
        return result;
    }

    // Find previous node
    TexNode* prev = find_prev_node(current.node, root);
    if (!prev) return current;  // At start

    result.node = prev;
    result.position = 1;  // After prev node

    // Calculate new visual position
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = prev; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }
    result.x = abs_x + prev->width;
    result.y = abs_y;
    result.height = prev->height;
    result.depth = prev->depth;

    return result;
}

TexCaret tex_caret_move_right(TexNode* root, const TexCaret& current) {
    if (!current.valid()) return current;

    TexCaret result = current;

    // If position is before the node, move to after
    if (current.position == 0) {
        result.position = 1;
        result.x += current.node->width;
        return result;
    }

    // Find next node
    TexNode* next = find_next_node(current.node, root);
    if (!next) return current;  // At end

    result.node = next;
    result.position = 0;  // Before next node

    // Calculate new visual position
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = next; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }
    result.x = abs_x;
    result.y = abs_y;
    result.height = next->height;
    result.depth = next->depth;

    return result;
}

TexCaret tex_caret_move_up(TexNode* root, const TexCaret& current) {
    if (!current.valid()) return current;

    // Look for superscript or numerator in parent chain
    for (TexNode* n = current.node; n && n != root; n = n->parent) {
        if (n->parent && n->parent->node_class == NodeClass::Fraction) {
            // If in denominator, move to numerator
            if (n == n->parent->content.frac.denominator) {
                TexNode* num = n->parent->content.frac.numerator;
                if (num) {
                    return tex_caret_start(num);
                }
            }
        }
        if (n->parent && n->parent->node_class == NodeClass::Scripts) {
            // If in subscript, move to superscript or nucleus
            if (n == n->parent->content.scripts.subscript) {
                if (n->parent->content.scripts.superscript) {
                    return tex_caret_start(n->parent->content.scripts.superscript);
                } else if (n->parent->content.scripts.nucleus) {
                    return tex_caret_end(n->parent->content.scripts.nucleus);
                }
            }
            // If in nucleus, move to superscript
            if (n == n->parent->content.scripts.nucleus) {
                if (n->parent->content.scripts.superscript) {
                    return tex_caret_start(n->parent->content.scripts.superscript);
                }
            }
        }
    }

    return current;  // No up navigation available
}

TexCaret tex_caret_move_down(TexNode* root, const TexCaret& current) {
    if (!current.valid()) return current;

    // Look for subscript or denominator in parent chain
    for (TexNode* n = current.node; n && n != root; n = n->parent) {
        if (n->parent && n->parent->node_class == NodeClass::Fraction) {
            // If in numerator, move to denominator
            if (n == n->parent->content.frac.numerator) {
                TexNode* denom = n->parent->content.frac.denominator;
                if (denom) {
                    return tex_caret_start(denom);
                }
            }
        }
        if (n->parent && n->parent->node_class == NodeClass::Scripts) {
            // If in superscript, move to subscript or nucleus
            if (n == n->parent->content.scripts.superscript) {
                if (n->parent->content.scripts.subscript) {
                    return tex_caret_start(n->parent->content.scripts.subscript);
                } else if (n->parent->content.scripts.nucleus) {
                    return tex_caret_end(n->parent->content.scripts.nucleus);
                }
            }
            // If in nucleus, move to subscript
            if (n == n->parent->content.scripts.nucleus) {
                if (n->parent->content.scripts.subscript) {
                    return tex_caret_start(n->parent->content.scripts.subscript);
                }
            }
        }
    }

    return current;  // No down navigation available
}

// ============================================================================
// Selection Implementation
// ============================================================================

TexSelection TexSelection::normalized() const {
    // TODO: Proper comparison of caret positions in tree order
    // For now, compare x positions as approximation
    if (start.x <= end.x) {
        return *this;
    }
    return TexSelection(end, start);
}

TexSelection tex_select_word(TexNode* root, const TexCaret& at) {
    // For math, "word" is typically a single atom or group
    // Just return selection around current node for now
    if (!at.valid()) return TexSelection();

    TexCaret start, end;
    start.node = at.node;
    start.position = 0;
    end.node = at.node;
    end.position = 1;

    // Calculate visual positions
    float abs_x = 0, abs_y = 0;
    for (TexNode* n = at.node; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }

    start.x = abs_x;
    start.y = abs_y;
    start.height = at.node->height;
    start.depth = at.node->depth;

    end.x = abs_x + at.node->width;
    end.y = abs_y;
    end.height = at.node->height;
    end.depth = at.node->depth;

    return TexSelection(start, end);
}

TexSelection tex_select_all(TexNode* root) {
    return TexSelection(tex_caret_start(root), tex_caret_end(root));
}

TexSelection tex_extend_selection(const TexSelection& sel, const TexCaret& focus) {
    return TexSelection(sel.start, focus);
}

// ============================================================================
// Selection Rendering (Stub - needs RenderContext)
// ============================================================================

void tex_render_selection(RenderContext* ctx, TexNode* root, const TexSelection& sel, uint32_t color) {
    // TODO: Implement when RenderContext is available
    // Will need to iterate through selected nodes and draw highlight rectangles
    log_debug("tex_render_selection: stub implementation");
}

void tex_render_caret(RenderContext* ctx, const TexCaret& caret, uint32_t color) {
    // TODO: Implement when RenderContext is available
    // Will draw a vertical line at caret.x, from caret.y - caret.height to caret.y + caret.depth
    log_debug("tex_render_caret: stub implementation");
}

// ============================================================================
// Event Handler Implementation
// ============================================================================

TexNodeEventHandler::TexNodeEventHandler(TexNode* r)
    : root(r), mouse_down(false) {
    if (root) {
        caret = tex_caret_start(root);
        selection = TexSelection(caret, caret);
    }
}

bool TexNodeEventHandler::on_mouse_down(float x, float y, int button, bool shift) {
    if (!root || button != 0) return false;

    mouse_down = true;

    TexHitResult hit = tex_hit_test(root, x, y);
    TexCaret new_caret = tex_caret_from_hit(hit);

    if (!new_caret.valid()) {
        // Click outside - position at nearest edge
        if (x < root->width / 2) {
            new_caret = tex_caret_start(root);
        } else {
            new_caret = tex_caret_end(root);
        }
    }

    if (shift && selection.valid()) {
        // Extend selection
        selection = tex_extend_selection(selection, new_caret);
    } else {
        // New selection
        selection = TexSelection(new_caret, new_caret);
    }

    caret = selection.end;
    return true;
}

bool TexNodeEventHandler::on_mouse_move(float x, float y) {
    if (!root || !mouse_down) return false;

    TexHitResult hit = tex_hit_test(root, x, y);
    TexCaret new_focus = tex_caret_from_hit(hit);

    if (new_focus.valid()) {
        selection = tex_extend_selection(selection, new_focus);
        caret = selection.end;
    }

    return true;
}

bool TexNodeEventHandler::on_mouse_up(float x, float y, int button) {
    if (button != 0) return false;
    mouse_down = false;
    return true;
}

bool TexNodeEventHandler::on_key_down(int key, int mods) {
    if (!root) return false;

    // Key codes (platform-agnostic approximation)
    constexpr int KEY_LEFT = 263;
    constexpr int KEY_RIGHT = 262;
    constexpr int KEY_UP = 265;
    constexpr int KEY_DOWN = 264;
    constexpr int KEY_HOME = 268;
    constexpr int KEY_END = 269;

    bool shift = (mods & 0x01) != 0;  // Shift modifier

    TexCaret new_caret = caret;

    switch (key) {
        case KEY_LEFT:
            new_caret = tex_caret_move_left(root, caret);
            break;
        case KEY_RIGHT:
            new_caret = tex_caret_move_right(root, caret);
            break;
        case KEY_UP:
            new_caret = tex_caret_move_up(root, caret);
            break;
        case KEY_DOWN:
            new_caret = tex_caret_move_down(root, caret);
            break;
        case KEY_HOME:
            new_caret = tex_caret_start(root);
            break;
        case KEY_END:
            new_caret = tex_caret_end(root);
            break;
        default:
            return false;
    }

    if (shift) {
        selection = tex_extend_selection(selection, new_caret);
    } else {
        selection = TexSelection(new_caret, new_caret);
    }
    caret = new_caret;

    return true;
}

// Stub implementations for editing operations
void TexNodeEventHandler::insert_char(int32_t codepoint) {
    log_debug("tex insert_char: not implemented (codepoint=%d)", codepoint);
}

void TexNodeEventHandler::delete_backward() {
    log_debug("tex delete_backward: not implemented");
}

void TexNodeEventHandler::delete_forward() {
    log_debug("tex delete_forward: not implemented");
}

void TexNodeEventHandler::delete_selection() {
    log_debug("tex delete_selection: not implemented");
}

const char* TexNodeEventHandler::copy_selection() {
    log_debug("tex copy_selection: not implemented");
    return nullptr;
}

void TexNodeEventHandler::update_caret_visual() {
    // Recalculate caret visual position from caret.node and caret.position
    if (!caret.valid()) return;

    float abs_x = 0, abs_y = 0;
    for (TexNode* n = caret.node; n; n = n->parent) {
        abs_x += n->x;
        abs_y += n->y;
    }

    if (caret.position == 0) {
        caret.x = abs_x;
    } else {
        caret.x = abs_x + caret.node->width;
    }
    caret.y = abs_y;
    caret.height = caret.node->height;
    caret.depth = caret.node->depth;
}

void TexNodeEventHandler::collapse_selection_to_caret() {
    selection = TexSelection(caret, caret);
}

} // namespace tex
