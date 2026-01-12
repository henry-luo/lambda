// tex_event.hpp - Event System for TexNode Trees
//
// Provides hit testing, caret positioning, and selection support
// for interactive TexNode trees rendered via RDT_VIEW_TEXNODE.
//
// Design principles:
// - TexNode IS the view tree (no conversion needed)
// - Coordinates in CSS pixels (consistent with Radiant)
// - Supports keyboard navigation through math structures
// - Selection ranges can span multiple nodes

#ifndef LAMBDA_TEX_EVENT_HPP
#define LAMBDA_TEX_EVENT_HPP

#include "tex_node.hpp"
#include <cstdint>

namespace tex {

// ============================================================================
// Hit Test Result
// ============================================================================

/**
 * Result of a hit test on a TexNode tree.
 * Identifies the deepest node containing the point and provides
 * information for caret positioning.
 */
struct TexHitResult {
    TexNode* node;          // Deepest node containing the point (nullptr if miss)
    int char_index;         // Character index within node (for Char/MathChar nodes)
    float local_x;          // Hit position relative to node origin (CSS px)
    float local_y;          // Hit position relative to node baseline (CSS px)
    bool is_before;         // Caret should be placed before (true) or after (false) char_index

    TexHitResult()
        : node(nullptr), char_index(0), local_x(0), local_y(0), is_before(true) {}

    bool hit() const { return node != nullptr; }
};

/**
 * Perform hit testing on a TexNode tree.
 *
 * @param root  Root of the TexNode tree
 * @param x     X coordinate relative to root origin (CSS px)
 * @param y     Y coordinate relative to root baseline (CSS px)
 * @return      Hit result with deepest node and position info
 */
TexHitResult tex_hit_test(TexNode* root, float x, float y);

// ============================================================================
// Caret Position
// ============================================================================

/**
 * Caret position within a TexNode tree.
 * Represents the insertion point for editing operations.
 */
struct TexCaret {
    TexNode* node;          // Node containing caret (nullptr if invalid)
    int position;           // Position within node (0 = before first element)
    float x;                // Visual X position for rendering cursor (CSS px)
    float y;                // Visual Y position at baseline (CSS px)
    float height;           // Cursor height (CSS px)
    float depth;            // Cursor depth below baseline (CSS px)

    TexCaret()
        : node(nullptr), position(0), x(0), y(0), height(0), depth(0) {}

    bool valid() const { return node != nullptr; }
};

/**
 * Get caret position from a hit test result.
 *
 * @param hit   Hit test result
 * @return      Caret position at the hit location
 */
TexCaret tex_caret_from_hit(const TexHitResult& hit);

/**
 * Get caret at the beginning of a TexNode tree.
 *
 * @param root  Root of the TexNode tree
 * @return      Caret at the start position
 */
TexCaret tex_caret_start(TexNode* root);

/**
 * Get caret at the end of a TexNode tree.
 *
 * @param root  Root of the TexNode tree
 * @return      Caret at the end position
 */
TexCaret tex_caret_end(TexNode* root);

// ============================================================================
// Caret Navigation
// ============================================================================

/**
 * Move caret left (toward start of expression).
 * Handles navigation through subscripts, superscripts, fractions, etc.
 *
 * @param root      Root of the TexNode tree (for boundary checking)
 * @param current   Current caret position
 * @return          New caret position (same as current if at start)
 */
TexCaret tex_caret_move_left(TexNode* root, const TexCaret& current);

/**
 * Move caret right (toward end of expression).
 *
 * @param root      Root of the TexNode tree
 * @param current   Current caret position
 * @return          New caret position (same as current if at end)
 */
TexCaret tex_caret_move_right(TexNode* root, const TexCaret& current);

/**
 * Move caret up (into superscript, numerator, or previous line).
 *
 * @param root      Root of the TexNode tree
 * @param current   Current caret position
 * @return          New caret position (same as current if no up navigation)
 */
TexCaret tex_caret_move_up(TexNode* root, const TexCaret& current);

/**
 * Move caret down (into subscript, denominator, or next line).
 *
 * @param root      Root of the TexNode tree
 * @param current   Current caret position
 * @return          New caret position (same as current if no down navigation)
 */
TexCaret tex_caret_move_down(TexNode* root, const TexCaret& current);

// ============================================================================
// Selection Range
// ============================================================================

/**
 * Selection range within a TexNode tree.
 * Represents a contiguous selection from start to end caret.
 */
struct TexSelection {
    TexCaret start;         // Start of selection (anchor)
    TexCaret end;           // End of selection (focus)

    TexSelection() = default;
    TexSelection(const TexCaret& s, const TexCaret& e) : start(s), end(e) {}

    /**
     * Check if selection is collapsed (caret with no extent).
     */
    bool is_collapsed() const {
        return start.node == end.node && start.position == end.position;
    }

    /**
     * Check if selection is valid.
     */
    bool valid() const {
        return start.valid() && end.valid();
    }

    /**
     * Get selection with start before end (normalized order).
     */
    TexSelection normalized() const;
};

/**
 * Select the word at the given caret position.
 *
 * @param root  Root of the TexNode tree
 * @param at    Caret position to expand to word
 * @return      Selection covering the word
 */
TexSelection tex_select_word(TexNode* root, const TexCaret& at);

/**
 * Select the entire TexNode tree.
 *
 * @param root  Root of the TexNode tree
 * @return      Selection covering all content
 */
TexSelection tex_select_all(TexNode* root);

/**
 * Extend selection from anchor to new focus position.
 *
 * @param sel   Current selection (anchor is preserved)
 * @param focus New focus caret position
 * @return      Updated selection
 */
TexSelection tex_extend_selection(const TexSelection& sel, const TexCaret& focus);

// ============================================================================
// Selection Rendering
// ============================================================================

// Forward declaration for render context
struct RenderContext;

/**
 * Render selection highlight for a TexNode tree.
 * Draws highlight rectangles behind selected content.
 *
 * @param ctx   Render context
 * @param root  Root of the TexNode tree
 * @param sel   Selection to highlight
 * @param color Selection highlight color (RGBA)
 */
void tex_render_selection(
    RenderContext* ctx,
    TexNode* root,
    const TexSelection& sel,
    uint32_t color
);

/**
 * Render caret (blinking cursor) for a TexNode tree.
 *
 * @param ctx   Render context
 * @param caret Caret position to render
 * @param color Caret color (RGBA)
 */
void tex_render_caret(
    RenderContext* ctx,
    const TexCaret& caret,
    uint32_t color
);

// ============================================================================
// Event Handler
// ============================================================================

/**
 * Event handler for interactive TexNode trees.
 * Manages caret, selection, and input events.
 */
class TexNodeEventHandler {
public:
    TexNode* root;              // Root of the TexNode tree
    TexCaret caret;             // Current caret position
    TexSelection selection;     // Current selection (collapsed = just caret)
    bool mouse_down;            // Mouse button state

    TexNodeEventHandler() : root(nullptr), mouse_down(false) {}
    explicit TexNodeEventHandler(TexNode* r);

    /**
     * Handle mouse button press.
     *
     * @param x         X coordinate relative to root (CSS px)
     * @param y         Y coordinate relative to root (CSS px)
     * @param button    Mouse button (0 = left, 1 = middle, 2 = right)
     * @param shift     Shift key held (extend selection)
     * @return          True if event was handled
     */
    bool on_mouse_down(float x, float y, int button, bool shift = false);

    /**
     * Handle mouse movement (for selection dragging).
     *
     * @param x     X coordinate relative to root (CSS px)
     * @param y     Y coordinate relative to root (CSS px)
     * @return      True if event was handled
     */
    bool on_mouse_move(float x, float y);

    /**
     * Handle mouse button release.
     *
     * @param x         X coordinate relative to root (CSS px)
     * @param y         Y coordinate relative to root (CSS px)
     * @param button    Mouse button
     * @return          True if event was handled
     */
    bool on_mouse_up(float x, float y, int button);

    /**
     * Handle key press for navigation.
     *
     * @param key   Key code (platform-specific)
     * @param mods  Modifier flags (shift, ctrl, etc.)
     * @return      True if event was handled
     */
    bool on_key_down(int key, int mods);

    // ========================================
    // Editing operations (for future implementation)
    // ========================================

    /**
     * Insert a character at caret position.
     * (Future: requires TexNode tree editing support)
     *
     * @param codepoint Unicode codepoint to insert
     */
    void insert_char(int32_t codepoint);

    /**
     * Delete character before caret (backspace).
     */
    void delete_backward();

    /**
     * Delete character after caret (delete key).
     */
    void delete_forward();

    /**
     * Delete current selection.
     */
    void delete_selection();

    /**
     * Copy selection to clipboard.
     *
     * @return LaTeX string representation of selection
     */
    const char* copy_selection();

private:
    // Internal helpers
    void update_caret_visual();
    void collapse_selection_to_caret();
};

} // namespace tex

#endif // LAMBDA_TEX_EVENT_HPP
