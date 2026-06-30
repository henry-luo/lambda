#include "layout.hpp"
#include "layout_grid_multipass.hpp"
#include "layout_alignment.hpp"
#include "grid.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "intrinsic_sizing.hpp"
#include "layout_mode.hpp"
#include "layout_cache.hpp"
#include "layout_pass.hpp"
#include "layout_abs_children.hpp"
#include "layout_measure.hpp"
#include "layout_box.hpp"
#include "grid_baseline.hpp"
#include "../lib/tagged.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/memtrack.h"
}

// Multi-pass grid layout implementation
// Follows the same pattern as layout_flex_multipass.cpp

// External function declarations
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);
void layout_flow_node(LayoutContext* lycon, DomNode* node);
void line_init(LayoutContext* lycon, float left, float right);
void line_break(LayoutContext* lycon);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);

// Forward declarations for static functions
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item);

static CssValue* grid_spacing_shorthand_side_value(const CssValue* value, int side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) {
        return (CssValue*)value;
    }
    int cnt = value->data.list.count;
    CssValue** vals = value->data.list.values;
    if (cnt <= 0 || !vals) return nullptr;

    int idx = 0;
    if (side == 0) {
        idx = 0;  // top
    } else if (side == 1) {
        idx = (cnt >= 2) ? 1 : 0;  // right
    } else if (side == 2) {
        idx = (cnt >= 3) ? 2 : 0;  // bottom
    } else {
        idx = (cnt >= 4) ? 3 : ((cnt >= 2) ? 1 : 0);  // left
    }
    return (idx < cnt) ? vals[idx] : nullptr;
}

static bool grid_resolve_percentage_spacing_value(const CssValue* value, float inline_base, float* out) {
    if (!value || !out || inline_base < 0.0f) return false;
    if (value->type != CSS_VALUE_TYPE_PERCENTAGE) return false;
    *out = (float)(value->data.percentage.value / 100.0) * inline_base;
    return true;
}

static CssValue* grid_pair_spacing_value(const CssValue* value, bool end_side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) return (CssValue*)value;
    int cnt = value->data.list.count;
    CssValue** vals = value->data.list.values;
    if (cnt <= 0 || !vals) return nullptr;
    int idx = (end_side && cnt >= 2) ? 1 : 0;
    return (idx < cnt) ? vals[idx] : nullptr;
}

typedef struct GridPaddingCandidate {
    CssDeclaration* decl;
    CssValue* value;
    int64_t priority;
} GridPaddingCandidate;

static void grid_consider_padding_candidate(GridPaddingCandidate* candidate,
                                            CssDeclaration* decl, CssValue* value) {
    if (!candidate || !decl || !value) return;
    int64_t priority = get_cascade_priority(decl);
    if (!candidate->decl || priority >= candidate->priority) {
        candidate->decl = decl;
        candidate->value = value;
        candidate->priority = priority;
    }
}

static void grid_apply_padding_candidate(ViewBlock* item, int side,
                                         GridPaddingCandidate* candidate, float inline_base) {
    if (!item || !item->bound || !candidate || !candidate->decl || !candidate->value) return;
    float resolved = 0.0f;
    if (!grid_resolve_percentage_spacing_value(candidate->value, inline_base, &resolved)) return;

    switch (side) {
        case 0:
            item->bound->padding.top = resolved;
            item->bound->padding.top_specificity = candidate->priority;
            break;
        case 1:
            item->bound->padding.right = resolved;
            item->bound->padding.right_specificity = candidate->priority;
            break;
        case 2:
            item->bound->padding.bottom = resolved;
            item->bound->padding.bottom_specificity = candidate->priority;
            break;
        case 3:
            item->bound->padding.left = resolved;
            item->bound->padding.left_specificity = candidate->priority;
            break;
    }
}

static void grid_re_resolve_item_percentage_padding(ViewBlock* item, float inline_base) {
    if (!item || !item->bound || !item->specified_style || inline_base < 0.0f) return;

    GridPaddingCandidate top = {};
    GridPaddingCandidate right = {};
    GridPaddingCandidate bottom = {};
    GridPaddingCandidate left = {};

    CssDeclaration* padding = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING);
    if (padding && padding->value) {
        grid_consider_padding_candidate(&top, padding, grid_spacing_shorthand_side_value(padding->value, 0));
        grid_consider_padding_candidate(&right, padding, grid_spacing_shorthand_side_value(padding->value, 1));
        grid_consider_padding_candidate(&bottom, padding, grid_spacing_shorthand_side_value(padding->value, 2));
        grid_consider_padding_candidate(&left, padding, grid_spacing_shorthand_side_value(padding->value, 3));
    }

    CssDeclaration* pt = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_TOP);
    grid_consider_padding_candidate(&top, pt, pt ? pt->value : nullptr);
    CssDeclaration* pr = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_RIGHT);
    grid_consider_padding_candidate(&right, pr, pr ? pr->value : nullptr);
    CssDeclaration* pb = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_BOTTOM);
    grid_consider_padding_candidate(&bottom, pb, pb ? pb->value : nullptr);
    CssDeclaration* pl = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_LEFT);
    grid_consider_padding_candidate(&left, pl, pl ? pl->value : nullptr);

    CssDeclaration* p_inline = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_INLINE);
    if (p_inline && p_inline->value) {
        grid_consider_padding_candidate(&left, p_inline, grid_pair_spacing_value(p_inline->value, false));
        grid_consider_padding_candidate(&right, p_inline, grid_pair_spacing_value(p_inline->value, true));
    }
    CssDeclaration* pis = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_INLINE_START);
    grid_consider_padding_candidate(&left, pis, pis ? pis->value : nullptr);
    CssDeclaration* pie = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_INLINE_END);
    grid_consider_padding_candidate(&right, pie, pie ? pie->value : nullptr);

    CssDeclaration* p_block = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_BLOCK);
    if (p_block && p_block->value) {
        grid_consider_padding_candidate(&top, p_block, grid_pair_spacing_value(p_block->value, false));
        grid_consider_padding_candidate(&bottom, p_block, grid_pair_spacing_value(p_block->value, true));
    }
    CssDeclaration* pbs = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_BLOCK_START);
    grid_consider_padding_candidate(&top, pbs, pbs ? pbs->value : nullptr);
    CssDeclaration* pbe = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_PADDING_BLOCK_END);
    grid_consider_padding_candidate(&bottom, pbe, pbe ? pbe->value : nullptr);

    grid_apply_padding_candidate(item, 0, &top, inline_base);
    grid_apply_padding_candidate(item, 1, &right, inline_base);
    grid_apply_padding_candidate(item, 2, &bottom, inline_base);
    grid_apply_padding_candidate(item, 3, &left, inline_base);
}

typedef GridPaddingCandidate GridMarginCandidate;

static void grid_apply_margin_candidate(ViewBlock* item, int side,
                                        GridMarginCandidate* candidate, float inline_base) {
    if (!item || !item->bound || !candidate || !candidate->decl || !candidate->value) return;
    float resolved = 0.0f;
    if (!grid_resolve_percentage_spacing_value(candidate->value, inline_base, &resolved)) return;

    switch (side) {
        case 0:
            item->bound->margin.top = resolved;
            item->bound->margin.top_specificity = candidate->priority;
            item->bound->margin.top_type = CSS_VALUE__PERCENTAGE;
            break;
        case 1:
            item->bound->margin.right = resolved;
            item->bound->margin.right_specificity = candidate->priority;
            item->bound->margin.right_type = CSS_VALUE__PERCENTAGE;
            break;
        case 2:
            item->bound->margin.bottom = resolved;
            item->bound->margin.bottom_specificity = candidate->priority;
            item->bound->margin.bottom_type = CSS_VALUE__PERCENTAGE;
            break;
        case 3:
            item->bound->margin.left = resolved;
            item->bound->margin.left_specificity = candidate->priority;
            item->bound->margin.left_type = CSS_VALUE__PERCENTAGE;
            break;
    }
}

static void grid_re_resolve_item_percentage_margin(ViewBlock* item, float inline_base) {
    if (!item || !item->bound || !item->specified_style || inline_base < 0.0f) return;

    GridMarginCandidate top = {};
    GridMarginCandidate right = {};
    GridMarginCandidate bottom = {};
    GridMarginCandidate left = {};

    CssDeclaration* margin = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN);
    if (margin && margin->value) {
        grid_consider_padding_candidate(&top, margin, grid_spacing_shorthand_side_value(margin->value, 0));
        grid_consider_padding_candidate(&right, margin, grid_spacing_shorthand_side_value(margin->value, 1));
        grid_consider_padding_candidate(&bottom, margin, grid_spacing_shorthand_side_value(margin->value, 2));
        grid_consider_padding_candidate(&left, margin, grid_spacing_shorthand_side_value(margin->value, 3));
    }

    CssDeclaration* mt = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_TOP);
    grid_consider_padding_candidate(&top, mt, mt ? mt->value : nullptr);
    CssDeclaration* mr = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
    grid_consider_padding_candidate(&right, mr, mr ? mr->value : nullptr);
    CssDeclaration* mb = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_BOTTOM);
    grid_consider_padding_candidate(&bottom, mb, mb ? mb->value : nullptr);
    CssDeclaration* ml = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_LEFT);
    grid_consider_padding_candidate(&left, ml, ml ? ml->value : nullptr);

    CssDeclaration* m_inline = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_INLINE);
    if (m_inline && m_inline->value) {
        grid_consider_padding_candidate(&left, m_inline, grid_pair_spacing_value(m_inline->value, false));
        grid_consider_padding_candidate(&right, m_inline, grid_pair_spacing_value(m_inline->value, true));
    }
    CssDeclaration* mis = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_INLINE_START);
    grid_consider_padding_candidate(&left, mis, mis ? mis->value : nullptr);
    CssDeclaration* mie = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_INLINE_END);
    grid_consider_padding_candidate(&right, mie, mie ? mie->value : nullptr);

    CssDeclaration* m_block = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_BLOCK);
    if (m_block && m_block->value) {
        grid_consider_padding_candidate(&top, m_block, grid_pair_spacing_value(m_block->value, false));
        grid_consider_padding_candidate(&bottom, m_block, grid_pair_spacing_value(m_block->value, true));
    }
    CssDeclaration* mbs = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_BLOCK_START);
    grid_consider_padding_candidate(&top, mbs, mbs ? mbs->value : nullptr);
    CssDeclaration* mbe = style_tree_get_declaration(item->specified_style, CSS_PROPERTY_MARGIN_BLOCK_END);
    grid_consider_padding_candidate(&bottom, mbe, mbe ? mbe->value : nullptr);

    grid_apply_margin_candidate(item, 0, &top, inline_base);
    grid_apply_margin_candidate(item, 1, &right, inline_base);
    grid_apply_margin_candidate(item, 2, &bottom, inline_base);
    grid_apply_margin_candidate(item, 3, &left, inline_base);
}

static void grid_re_resolve_item_percentage_box(ViewBlock* item, float inline_base) {
    grid_re_resolve_item_percentage_padding(item, inline_base);
    grid_re_resolve_item_percentage_margin(item, inline_base);
}

static float grid_container_content_width_for_item_percentages(LayoutContext* lycon, ViewBlock* grid_container) {
    if (!grid_container) return 0.0f;

    float content_width = layout_content_width_from_border_box(grid_container, grid_container->width);
    if (grid_container->blk && grid_container->blk->given_width >= 0.0f) {
        if (grid_container->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            content_width = layout_content_width_from_border_box(grid_container, grid_container->blk->given_width);
        } else {
            content_width = grid_container->blk->given_width;
        }
        return content_width;
    }

    CssDeclaration* width_decl = grid_container->specified_style
        ? style_tree_get_declaration(grid_container->specified_style, CSS_PROPERTY_WIDTH) : nullptr;
    if (width_decl && width_decl->value) {
        float declared_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
        if (!isnan(declared_width) && declared_width >= 0.0f) {
            if (grid_container->blk && grid_container->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                content_width = layout_content_width_from_border_box(grid_container, declared_width);
            } else {
                content_width = declared_width;
            }
        }
    }
    return content_width;
}

static float grid_container_content_height_for_item_percentages(ViewBlock* grid_container) {
    if (!grid_container) return -1.0f;

    float content_height = layout_content_height_from_border_box(grid_container, grid_container->height);
    if (grid_container->blk && grid_container->blk->given_height >= 0.0f) {
        if (grid_container->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
            content_height = layout_content_height_from_border_box(grid_container, grid_container->blk->given_height);
        } else {
            content_height = grid_container->blk->given_height;
        }
    }
    return content_height > 0.0f ? content_height : -1.0f;
}

static float grid_item_percentage_base_from_parent(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item || !grid_item->parent || !grid_item->parent->is_element()) {
        return grid_item ? grid_item->width : 0.0f;
    }
    ViewBlock* grid_container = lam::view_as_block(grid_item->parent->as_element());
    if (!grid_container) return grid_item->width;
    return grid_container_content_width_for_item_percentages(lycon, grid_container);
}

static bool grid_node_has_non_whitespace_text(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    if (!text) return false;
    for (const char* p = text; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f') {
            return true;
        }
    }
    return false;
}

static float grid_flex_container_auto_border_height(ViewBlock* flex_container,
                                                    float fallback_content_height) {
    if (!flex_container) return 0.0f;

    bool has_explicit_height = flex_container->blk && flex_container->blk->given_height >= 0.0f;
    if (has_explicit_height) {
        return flex_container->height;
    }

    BoxMetrics box = layout_box_metrics(flex_container);
    float child_extent = 0.0f;
    bool has_child = false;
    bool is_row = true;
    float gap = 0.0f;
    if (flex_container->embed && flex_container->embed->flex) {
        FlexProp* flex = flex_container->embed->flex;
        is_row = (flex->direction == DIR_ROW || flex->direction == DIR_ROW_REVERSE);
        gap = is_row ? flex->row_gap : flex->column_gap;
    }

    if (is_row) {
        for (DomNode* child = flex_container->first_child; child; child = child->next_sibling) {
            if (grid_node_has_non_whitespace_text(child)) {
                float text_extent = fallback_content_height + box.pad_border_v;
                if (text_extent > child_extent) child_extent = text_extent;
                has_child = true;
                continue;
            }
            if (!child->is_element()) continue;
            ViewBlock* child_block = lam::view_as_block(child->as_element());
            if (!child_block) continue;
            if (child_block->display.outer == CSS_VALUE_NONE ||
                child_block->display.inner == CSS_VALUE_NONE ||
                layout_view_is_abs_or_fixed(child_block)) {
                continue;
            }
            BoxMetrics child_box = layout_box_metrics(child_block);
            float outer_height = child_box.margin.top + child_block->height + child_box.margin.bottom;
            if (outer_height > child_extent) child_extent = outer_height;
            has_child = true;
        }
    } else {
        for (DomNode* child = flex_container->first_child; child; child = child->next_sibling) {
            if (grid_node_has_non_whitespace_text(child)) {
                if (has_child && gap > 0.0f) child_extent += gap;
                child_extent += fallback_content_height + box.pad_border_v;
                has_child = true;
                continue;
            }
            if (!child->is_element()) continue;
            ViewBlock* child_block = lam::view_as_block(child->as_element());
            if (!child_block) continue;
            if (child_block->display.outer == CSS_VALUE_NONE ||
                child_block->display.inner == CSS_VALUE_NONE ||
                layout_view_is_abs_or_fixed(child_block)) {
                continue;
            }
            BoxMetrics child_box = layout_box_metrics(child_block);
            if (has_child && gap > 0.0f) child_extent += gap;
            child_extent += child_box.margin.top + child_block->height + child_box.margin.bottom;
            has_child = true;
        }
    }

    return child_extent + box.pad_border_v;
}

// ============================================================================
// Main Entry Point
// ============================================================================

void layout_grid_content(LayoutContext* lycon, ViewBlock* grid_container) {
    if (!grid_container) return;

    // guard against stack overflow from deeply nested grid containers (fuzzer-found).
    // nested grid items recurse here via layout_grid_item_final_content_multipass
    // without passing through layout_flow_node, so they bypass that depth guard.
    // uses a dedicated, small limit because each grid multipass frame is very large
    // and overflows the stack at far shallower nesting than MAX_LAYOUT_DEPTH.
    if (lycon->grid_depth >= MAX_GRID_DEPTH) {
        log_error("layout_grid_content: grid_depth=%d at limit (%d), skipping nested grid %s",
                  lycon->grid_depth, MAX_GRID_DEPTH, grid_container->node_name());
        return;
    }
    struct GridDepthGuard {
        LayoutContext* lycon;
        GridDepthGuard(LayoutContext* lycon) : lycon(lycon) { lycon->grid_depth++; }
        ~GridDepthGuard() { lycon->grid_depth--; }
    } depth_guard(lycon);

    log_enter();
    log_info("GRID LAYOUT START: container=%p (%s) width=%.1f height=%.1f", grid_container, grid_container->node_name(), grid_container->width, grid_container->height);

    DomNode* saved_elmt = lycon->elmt;
    View* saved_view = lycon->view;
    lycon->elmt = static_cast<DomNode*>(grid_container);
    lycon->view = static_cast<View*>(grid_container);
    struct GridLayoutContextGuard {
        LayoutContext* lycon;
        DomNode* saved_elmt;
        View* saved_view;
        GridLayoutContextGuard(LayoutContext* lycon, DomNode* saved_elmt, View* saved_view)
            : lycon(lycon), saved_elmt(saved_elmt), saved_view(saved_view) {}
        ~GridLayoutContextGuard() {
            lycon->elmt = saved_elmt;
            lycon->view = saved_view;
        }
    } context_guard(lycon, saved_elmt, saved_view);

    // =========================================================================
    // CACHE LOOKUP: Check if we have a cached result for these constraints
    // This avoids redundant layout for repeated measurements with same inputs
    // =========================================================================
    DomElement* dom_elem = lam::dom_require<DOM_NODE_ELEMENT>(grid_container);
    radiant::KnownDimensions known_dims = radiant::layout_known_dimensions_from_context(lycon);

    // Try cache lookup
    radiant::SizeF cached_size;
    if (radiant::layout_pass_cache_get(lycon, dom_elem, known_dims, &cached_size, "GRID")) {
        grid_container->width = cached_size.width;
        grid_container->height = cached_size.height;
        log_leave();
        return;
    }

    // =========================================================================
    // EARLY BAILOUT: For ComputeSize mode, check if dimensions are already known
    // This optimization avoids redundant layout when only measurements are needed
    // =========================================================================
    if (lycon->run_mode == radiant::RunMode::ComputeSize) {
        // Check if both dimensions are explicitly set via CSS
        bool has_definite_width = (lycon->block.given_width >= 0);
        bool has_definite_height = (lycon->block.given_height >= 0);

        if (has_definite_width && has_definite_height) {
            // Both dimensions known - can skip full layout
            grid_container->width = lycon->block.given_width;
            grid_container->height = lycon->block.given_height;
            log_info("GRID EARLY BAILOUT: Both dimensions known (%.1fx%.1f), skipping full layout",
                     grid_container->width, grid_container->height);
            log_leave();
            return;
        }
        log_debug("GRID: ComputeSize mode but dimensions not fully known (w=%d, h=%d)",
                  has_definite_width, has_definite_height);
    }

    // Save parent grid context (for nested grids)
    GridContainerLayout* pa_grid = lycon->grid_container;

    // Initialize grid container
    init_grid_container(lycon, grid_container);

    // Note: Grid properties (grid-template-columns/rows) may not be populated in embed->grid
    // at this point if they haven't been resolved in resolve_css_style.cpp.
    // The grid algorithm will use defaults in this case.

    // ========================================================================
    // PASS 0: Style Resolution and View Initialization
    // ========================================================================
    log_info("=== GRID PASS 0: Style resolution and view initialization ===");
    int item_count = resolve_grid_item_styles(lycon, grid_container);
    log_info("=== GRID PASS 0 COMPLETE: %d items initialized ===", item_count);

    if (item_count == 0) {
        // Check if there are absolutely positioned children that use grid lines
        // for their containing block (CSS Grid §9.1 - grid container is containing block).
        // If so, we still need to run track sizing to produce correct grid line positions,
        // then layout those absolute children in Pass 4.
        bool has_absolute_children = false;
        DomNode* ch = grid_container->first_child;
        while (ch) {
            if (ch->is_element()) {
                DomElement* ce = ch->as_element();
                if (ce->position &&
                    (ce->position->position == CSS_VALUE_ABSOLUTE ||
                     ce->position->position == CSS_VALUE_FIXED)) {
                    // Styles were already resolved in resolve_grid_item_styles above
                    // (init_grid_item_view was called for all children). Only call again
                    // if for some reason gi was not populated yet (defensive guard).
                    ViewBlock* ce_block = lam::view_as_block(ce);
                    if (!grid_item_prop(ce_block)) {
                        init_grid_item_view(lycon, ch);
                    }
                    has_absolute_children = true;
                }
            }
            ch = ch->next_sibling;
        }
        if (!has_absolute_children) {
            log_debug("No grid items and no absolute children - skipping");
            cleanup_grid_container(lycon);
            lycon->grid_container = pa_grid;
            log_leave();
            return;
        }
        // Fall through: run Passes 2 and 4 only
        log_info("=== GRID: no in-flow items, running track sizing for absolute children ===");
        layout_grid_container(lycon, grid_container);
        layout_grid_absolute_children(lycon, grid_container);
        cleanup_grid_container(lycon);
        lycon->grid_container = pa_grid;
        log_leave();
        return;
    }

    // ========================================================================
    // PASS 1: Content Measurement (for intrinsic track sizing)
    // ========================================================================
    log_info("=== GRID PASS 1: Content measurement ===");
    measure_grid_items(lycon, lycon->grid_container);
    log_info("=== GRID PASS 1 COMPLETE ===");

    // ========================================================================
    // PASS 2: Grid Algorithm Execution
    // ========================================================================
    log_info("=== GRID PASS 2: Grid algorithm execution ===");
    log_debug("GRID PASS 2 PRE: grid_container=%p width=%d height=%d", grid_container, grid_container->width, grid_container->height);
    layout_grid_container(lycon, grid_container);
    log_info("=== GRID PASS 2 COMPLETE ===");

    // ========================================================================
    // PASS 3: Final Content Layout
    // ========================================================================
    log_info("=== GRID PASS 3: Final content layout ===");
    layout_final_grid_content(lycon, lycon->grid_container);

    // Update row heights based on actual content heights from Pass 3.
    // The intrinsic height measurement in Pass 2 may underestimate because it doesn't
    // fully handle complex layouts (flex, margins, etc.). After content layout, items
    // have accurate content_height values.
    {
        GridContainerLayout* gl = lycon->grid_container;
        bool rows_changed = false;
        for (int r = 0; r < gl->computed_row_count; r++) {
            float max_content_h = 0;
            for (int i = 0; i < gl->item_count; i++) {
                ViewBlock* item = gl->grid_items[i];
                GridItemProp* gi = grid_item_prop(item);
                if (!item || !gi) continue;
                int rs = gi->computed_grid_row_start - 1;
                int re = gi->computed_grid_row_end - 1;
                if (rs != r || re != r + 1) continue; // only single-row-span items
                float h = item->content_height;
                // CSS Grid: if item has max-height, its contribution to the row is capped
                if (item->blk && item->blk->given_max_height >= 0) {
                    float bb_max = item->blk->given_max_height;
                    bool is_border_box = (item->blk->box_sizing == CSS_VALUE_BORDER_BOX);
                    if (!is_border_box) {
                        bb_max = layout_border_height_from_content_box(item, bb_max);
                    }
                    if (is_border_box) {
                        bb_max = layout_floor_border_box_height(item, bb_max);
                    }
                    if (h > bb_max) h = bb_max;
                }
                if (h > max_content_h) max_content_h = h;
            }
            if (max_content_h > gl->computed_rows[r].computed_size + 0.5f) {
                // CSS Grid §7.2.1: Fixed-size tracks (length, percentage) are definite
                // and should NOT be changed from content — content overflows instead.
                // Only auto/intrinsic/flexible tracks reconcile to laid-out content.
                GridTrack* row_track = &gl->computed_rows[r];
                if (row_track->size &&
                    (row_track->size->type == GRID_TRACK_SIZE_LENGTH ||
                     row_track->size->type == GRID_TRACK_SIZE_PERCENTAGE)) {
                    continue;
                }
                log_debug("GRID row[%d] height updated: %.1f -> %.1f (from content)",
                          r, gl->computed_rows[r].computed_size, max_content_h);
                gl->computed_rows[r].computed_size = max_content_h;
                gl->computed_rows[r].base_size = max_content_h;
                rows_changed = true;
            }
        }
        if (rows_changed) {
            position_grid_items(gl, grid_container, &lycon->scratch);
        }
    }

    // Re-align items after content is laid out (now items have final heights)
    // This is needed for align-items: center/end to work correctly
    align_grid_items(lycon->grid_container);

    // Apply relative positioning offsets (position:relative + top/left/bottom/right)
    // Must be done AFTER final alignment so offsets are relative to the aligned position
    {
        GridContainerLayout* gl = lycon->grid_container;
        float parent_w = (float)grid_container->width;
        float parent_h = (float)grid_container->height;
        for (int i = 0; i < gl->item_count; i++) {
            ViewBlock* item = gl->grid_items[i];
            if (!item || !item->position) continue;
            if (item->position->position == CSS_VALUE_STICKY) {
                layout_sticky_positioned(lycon, item);
                continue;
            }
            if (item->position->position != CSS_VALUE_RELATIVE) continue;
            float ox = 0, oy = 0;
            if (item->position->has_left) {
                ox = isnan(item->position->left_percent) ? item->position->left
                    : item->position->left_percent * parent_w / 100.0f;
            } else if (item->position->has_right) {
                ox = isnan(item->position->right_percent) ? -item->position->right
                    : -(item->position->right_percent * parent_w / 100.0f);
            }
            if (item->position->has_top) {
                oy = isnan(item->position->top_percent) ? item->position->top
                    : item->position->top_percent * parent_h / 100.0f;
            } else if (item->position->has_bottom) {
                oy = isnan(item->position->bottom_percent) ? -item->position->bottom
                    : -(item->position->bottom_percent * parent_h / 100.0f);
            }
            if (ox != 0 || oy != 0) {
                log_debug("GRID relative offset: item %d (%.0f, %.0f)", i, ox, oy);
                item->x += ox;
                item->y += oy;
            }
        }
    }

    // Apply baseline alignment: shift items within each row to a shared baseline
    radiant::grid::resolve_and_apply_grid_baselines(lycon->grid_container);

    log_info("=== GRID PASS 3 COMPLETE ===");

    // ========================================================================
    // Update container height based on actual item positions and sizes
    // This is needed because content layout may cause items to exceed their
    // track-allocated sizes (e.g., when item content is larger than track)
    // Only do this for containers with auto height (no explicit height set)
    // ========================================================================
    GridContainerLayout* grid_layout = lycon->grid_container;
    bool has_explicit_height = grid_container->blk && grid_container->blk->given_height > 0;

    if (grid_layout && grid_layout->item_count > 0 && !has_explicit_height) {
        // Find the maximum extent of all grid items
        float max_item_bottom = 0;
        for (int i = 0; i < grid_layout->item_count; i++) {
            ViewBlock* item = grid_layout->grid_items[i];
            if (!item) continue;

            float item_bottom = item->y + item->height;
            if (item_bottom > max_item_bottom) {
                max_item_bottom = item_bottom;
            }
        }

        // Add container's bottom padding and border
        float required_height = max_item_bottom;
        if (grid_container->bound) {
            required_height += grid_container->bound->padding.bottom;
            if (grid_container->bound->border) {
                required_height += grid_container->bound->border->width.bottom;
            }
        }

        // Update container height if needed
        if (required_height > grid_container->height) {
            // When percentage row tracks overflow the intrinsic height, cap the
            // container height at the intrinsic row height (items overflow the container)
            if (grid_layout->row_intrinsic_height > 0.0f) {
                float capped_height = grid_layout->row_intrinsic_height;
                if (grid_container->bound) {
                    capped_height += grid_container->bound->padding.top + grid_container->bound->padding.bottom;
                    if (grid_container->bound->border) {
                        capped_height += grid_container->bound->border->width.top +
                                         grid_container->bound->border->width.bottom;
                    }
                }
                if (capped_height > grid_container->height) {
                    grid_container->height = capped_height;
                }
            } else {
                log_info("GRID: Updating container height from %.1f to %.1f (based on item extents)",
                         grid_container->height, required_height);
                grid_container->height = required_height;
            }

            // Also fix any item with negative y position (pushed above due to centering)
            // by shifting all items down
            float min_item_y = 0;
            for (int i = 0; i < grid_layout->item_count; i++) {
                ViewBlock* item = grid_layout->grid_items[i];
                if (!item) continue;
                if (item->y < min_item_y) {
                    min_item_y = item->y;
                }
            }
            if (min_item_y < 0) {
                // Shift all items down by the negative offset
                float shift = -min_item_y;
                for (int i = 0; i < grid_layout->item_count; i++) {
                    ViewBlock* item = grid_layout->grid_items[i];
                    if (!item) continue;
                    item->y += shift;
                }
                grid_container->height += shift;
                log_info("GRID: Shifted items down by %.1f to fix negative y positions", shift);
            }
        }
    }

    // Fallback: also check row-based calculation for containers without items
    // Only apply if container height is auto (not explicitly set)
    if (grid_layout && grid_layout->computed_row_count > 0 && !has_explicit_height) {
        // Calculate total height from row sizes plus gaps
        float total_row_height = 0;
        for (int i = 0; i < grid_layout->computed_row_count; i++) {
            total_row_height += grid_layout->computed_rows[i].base_size;
        }
        // Add gaps between rows
        total_row_height += grid_layout->row_gap * (grid_layout->computed_row_count - 1);

        // When percentage row tracks were re-resolved against intrinsic height,
        // the resolved tracks may overflow the intrinsic height. Use the intrinsic
        // height to cap the container so it matches browser behavior.
        if (grid_layout->row_intrinsic_height > 0.0f) {
            total_row_height = grid_layout->row_intrinsic_height;
        }

        // Add padding and border
        float container_height = total_row_height;
        if (grid_container->bound) {
            container_height += grid_container->bound->padding.top + grid_container->bound->padding.bottom;
            if (grid_container->bound->border) {
                container_height += grid_container->bound->border->width.top +
                                   grid_container->bound->border->width.bottom;
            }
        }

        // Only update if calculated height is greater (content overflow case)
        if (container_height > grid_container->height) {
            log_info("GRID: Updating container height from %.1f to %.1f (rows=%.1f, gaps=%.1f)",
                     grid_container->height, container_height, total_row_height,
                     grid_layout->row_gap * (grid_layout->computed_row_count - 1));
            grid_container->height = container_height;
        }
    }

    // ========================================================================
    // Update container width based on grid content (for shrink-to-fit containers)
    // NOTE: layout_grid_container (Pass 2) already sets the container width for
    // shrink-to-fit grids using the correct first-pass intrinsic width (before
    // percentage re-resolution). This secondary update is only needed as a
    // fallback when layout_grid_container didn't handle it (e.g., empty grid).
    // When percentage tracks exist, the container width must be the first-pass
    // max-content width, NOT the sum of resolved column sizes (which would be
    // smaller due to percentage clamping).
    // ========================================================================
    if (grid_layout && grid_layout->computed_column_count > 0) {
        // Check if container is shrink-to-fit (inline-grid, or absolutely
        // positioned with no explicit width)
        bool is_shrink_to_fit = false;
        if (grid_container->display.outer == CSS_VALUE_INLINE_BLOCK &&
            (!grid_container->blk || grid_container->blk->given_width < 0)) {
            is_shrink_to_fit = true;
        } else if (grid_container->position &&
            (grid_container->position->position == CSS_VALUE_ABSOLUTE ||
             grid_container->position->position == CSS_VALUE_FIXED)) {
            // Check if width is auto (not explicitly set)
            bool has_explicit_width = grid_container->blk && grid_container->blk->given_width > 0;
            bool has_left_right = grid_container->position->has_left && grid_container->position->has_right;
            if (!has_explicit_width && !has_left_right) {
                is_shrink_to_fit = true;
            }
        }

        if (is_shrink_to_fit && grid_layout->is_shrink_to_fit_width) {
            // layout_grid_container already set the correct width in Pass 2
            // (including the first-pass intrinsic width for pct tracks).
            // Use grid_layout->content_width which is already correct.
            float container_width = (float)grid_layout->content_width;
            if (grid_container->bound) {
                container_width += grid_container->bound->padding.left + grid_container->bound->padding.right;
                if (grid_container->bound->border) {
                    container_width += grid_container->bound->border->width.left +
                                       grid_container->bound->border->width.right;
                }
            }
            grid_container->width = container_width;
        }
    }

    // ========================================================================
    // PASS 4: Absolute Positioned Children
    // ========================================================================
    log_info("=== GRID PASS 4: Absolute positioned children ===");
    layout_grid_absolute_children(lycon, grid_container);
    log_info("=== GRID PASS 4 COMPLETE ===");

    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    radiant::SizeF result = radiant::size_f(grid_container->width, grid_container->height);
    radiant::layout_pass_cache_store(lycon, dom_elem, known_dims, result, "GRID");

    // Cleanup and restore parent context
    cleanup_grid_container(lycon);
    lycon->grid_container = pa_grid;

    log_info("GRID LAYOUT END: container=%p", grid_container);
    log_leave();
}

// ============================================================================
// Pass 0: Style Resolution and View Initialization
// ============================================================================

int resolve_grid_item_styles(LayoutContext* lycon, ViewBlock* grid_container) {
    log_enter();
    log_debug("Resolving styles for grid items in container %s", grid_container->node_name());

    // CSS Grid §11.4: percentage margins/paddings of grid items resolve against the
    // inline size of their grid area (their containing block for percentage purposes).
    // Since grid areas are not yet known, we use the grid container's content-box width
    // as the initial resolution base. This matches CSS Grid spec behaviour where percentage
    // widths/margins/paddings on grid items use the container's definite inline size.
    // (Same pattern as flex layout — see layout_flex.cpp and CSS §8.3.)
    // For an indefinite container (content_width == 0), percentages resolve to 0 (= auto),
    // which is correct per CSS Grid spec §7.2.1.
    float container_content_width = grid_container_content_width_for_item_percentages(lycon, grid_container);

    // Temporarily override lycon->block.parent so that percentage resolution
    // inside dom_node_resolve_style uses the grid container as the containing block,
    // not the grid container's parent.
    BlockContext grid_parent_ctx = {};
    BlockContext* saved_parent = lycon->block.parent;
    if (saved_parent) {
        grid_parent_ctx = *saved_parent;
    }
    grid_parent_ctx.content_width = container_content_width;
    float container_content_height = grid_container_content_height_for_item_percentages(grid_container);
    if (container_content_height > 0.0f) {
        grid_parent_ctx.content_height = container_content_height;
        grid_parent_ctx.given_height = container_content_height;
    }
    lycon->block.parent = &grid_parent_ctx;

    int item_count = 0;
    DomNode* child = grid_container->first_child;

    while (child) {
        if (child->is_element()) {
            DomElement* elem = child->as_element();

            // Always resolve styles first (position:absolute may not be known until after cascade)
            init_grid_item_view(lycon, child);
            grid_re_resolve_item_percentage_box(lam::view_as_block(elem), container_content_width);

            // Re-check absolute positioning AFTER style resolution
            ViewBlock* elem_block = lam::view_as_block(elem);
            bool is_absolute = layout_view_is_abs_or_fixed(elem_block);

            if (!is_absolute) {
                // check display:none AFTER style resolution (display may be set via cascade)
                if (elem->display.outer == CSS_VALUE_NONE) {
                    log_debug("Skipping display:none child after style resolution: %s", child->node_name());
                } else {
                    item_count++;
                    log_debug("Initialized grid item %d: %s", item_count, child->node_name());
                }
            } else {
                // Absolute item: gi was populated (for grid-placement containing block),
                // but do not count as an in-flow grid item.
                log_debug("Absolute positioned child detected after style resolution: %s", child->node_name());
            }
        }
        child = child->next_sibling;
    }

    // Restore original parent block context
    lycon->block.parent = saved_parent;

    log_debug("Resolved styles for %d grid items", item_count);
    log_leave();
    return item_count;
}

void init_grid_item_view(LayoutContext* lycon, DomNode* child) {
    if (!child || !child->is_element()) return;

    log_debug("Initializing grid item view for %s", child->node_name());

    DomElement* elem = child->as_element();

    // Resolve and store display value for this element
    // This is crucial for detecting nested grid/flex containers
    elem->display = resolve_display_value((void*)child);
    log_debug("Grid item display: outer=%d, inner=%d", elem->display.outer, elem->display.inner);

    // Set up the view type based on display
    // Grid items are blockified - treat as block
    elem->view_type = RDT_VIEW_BLOCK;

    // Initialize dimensions (will be set by grid algorithm)
    elem->x = 0;
    elem->y = 0;
    elem->width = 0;
    elem->height = 0;

    // Force boundary properties allocation for proper measurement
    if (!elem->bound) {
        Pool* pool = lycon->doc->view_tree->pool;
        elem->bound = (BoundaryProp*)pool_calloc(pool, sizeof(BoundaryProp));
    }

    // Ensure grid item properties are allocated
    // IMPORTANT: fi and gi are in a union! Check item_prop_type, not just gi pointer
    ViewBlock* elem_block = lam::view_as_block(elem);
    GridItemProp* existing_gi = grid_item_prop(elem_block);
    if (!existing_gi) {
        Pool* pool = lycon->doc->view_tree->pool;
        GridItemProp* gi = (GridItemProp*)pool_calloc(pool, sizeof(GridItemProp));
        if (gi) {
            if (elem->item_prop_type == DomElement::ITEM_PROP_FORM && elem->form) {
                elem->form->grid_item = gi;
            } else {
                elem->gi = gi;
                elem->item_prop_type = DomElement::ITEM_PROP_GRID;
            }
            // Initialize with auto placement defaults
            gi->is_grid_auto_placed = true;
            gi->justify_self = CSS_VALUE_AUTO;
            gi->align_self_grid = CSS_VALUE_AUTO;
        }
    }

    // CRITICAL: Set lycon->view to this element so style resolution
    // applies properties to this element, not some other view
    View* saved_view = lycon->view;
    lycon->view = static_cast<View*>(elem);

    // Resolve styles for this element (CSS cascade, inheritance, etc.)
    // This will now correctly apply padding/margin/border to elem->bound
    dom_node_resolve_style(child, lycon);

    // Restore previous view
    lycon->view = saved_view;

    log_debug("Grid item view initialized: %s (view_type=%d, bound=%p)",
              child->node_name(), elem->view_type, (void*)elem->bound);
}

// ============================================================================
// Pass 1: Content Measurement
// ============================================================================

void measure_grid_items(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    log_debug("Measuring intrinsic sizes for grid items");

    // Iterate through all grid items and measure their content
    ViewBlock* container = lam::view_as_block(lycon->elmt);
    DomNode* child = container ? container->first_child : nullptr;
    log_debug("measure_grid_items: lycon->elmt=%p (container), grid_container via lycon->elmt width=%d",
              lycon->elmt, container ? container->width : -1);

    float container_content_width = grid_container_content_width_for_item_percentages(lycon, container);
    BlockContext grid_parent_ctx = {};
    BlockContext* saved_parent = lycon->block.parent;
    if (saved_parent) {
        grid_parent_ctx = *saved_parent;
    }
    grid_parent_ctx.content_width = container_content_width;
    float container_content_height = grid_container_content_height_for_item_percentages(container);
    if (container_content_height > 0.0f) {
        grid_parent_ctx.content_height = container_content_height;
        grid_parent_ctx.given_height = container_content_height;
    }
    lycon->block.parent = &grid_parent_ctx;

    while (child) {
        if (child->is_element()) {
            ViewBlock* item = lam::view_require_block(child);

            // Skip absolute positioned and display:none items
            bool is_absolute = layout_view_is_abs_or_fixed(item);
            bool is_display_none = (item->display.outer == CSS_VALUE_NONE);

            if (!is_absolute && !is_display_none) {
                float min_width = 0, max_width = 0, min_height = 0, max_height = 0;
                measure_grid_item_intrinsic(lycon, item, &min_width, &max_width,
                                            &min_height, &max_height);
                grid_re_resolve_item_percentage_box(item, container_content_width);

                // Store only WIDTH measurements in the item for later use
                // HEIGHT measurements are intentionally NOT stored here because:
                // - Heights depend on the actual column width (after column sizing)
                // - Row sizing will calculate heights on-demand using item->width
                // This follows CSS Grid spec §11.5 where row sizing happens after column sizing
                GridItemProp* gi = grid_item_prop(item);
                if (gi) {
                    gi->measured_min_width = min_width;
                    gi->measured_max_width = max_width;
                    // Note: We don't set measured_min/max_height here.
                    // The calculate_grid_item_intrinsic_sizes function will compute
                    // heights on-demand using the actual column width.
                    gi->has_measured_size = true;  // Indicates width measurements are valid
                    log_debug("Stored width measurements for %s (gi=%p): min_w=%.1f, max_w=%.1f",
                              child->node_name(), gi,
                              gi->measured_min_width, gi->measured_max_width);
                } else {
                    log_debug("WARN: No gi for %s to store measurements", child->node_name());
                }

                log_debug("Grid item %s measured: min_w=%d, max_w=%d",
                          child->node_name(), min_width, max_width);
            }
        }
        child = child->next_sibling;
    }

    lycon->block.parent = saved_parent;

    log_leave();
}

void measure_grid_item_intrinsic(LayoutContext* lycon, ViewBlock* item,
                                  float* min_width, float* max_width,
                                  float* min_height, float* max_height) {
    if (!item) {
        *min_width = *max_width = *min_height = *max_height = 0;
        return;
    }

    log_debug("Measuring intrinsic sizes for grid item %s", item->node_name());

    // Check measurement cache first (shared with flex layout)
    MeasurementCacheEntry* cached = get_from_measurement_cache(static_cast<DomNode*>(item));
    if (cached) {
        *min_width = cached->content_width;
        *max_width = cached->measured_width;
        *min_height = cached->content_height;
        *max_height = cached->measured_height;
        log_debug("Using cached measurements for %s", item->node_name());
        return;
    }

    // Initialize output values
    *min_width = *max_width = *min_height = *max_height = 0;

    // Check if item has explicit dimensions from CSS
    bool has_explicit_width = false, has_explicit_height = false;
    if (item->blk) {
        if (item->blk->given_width > 0) {
            float w = item->blk->given_width;
            if (item->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                w = layout_floor_border_box_width(item, w);
            }
            *min_width = *max_width = w;
            has_explicit_width = true;
        }
        if (item->blk->given_height > 0) {
            float h = item->blk->given_height;
            if (item->blk->box_sizing == CSS_VALUE_BORDER_BOX) {
                h = layout_floor_border_box_height(item, h);
            }
            *min_height = *max_height = h;
            has_explicit_height = true;
        }

        // If both dimensions are explicit, we're done
        if (has_explicit_width && has_explicit_height) {
            log_debug("Grid item %s has explicit dimensions: %.1fx%.1f",
                      item->node_name(), *min_width, *min_height);
            return;
        }
    }

    // Use unified intrinsic sizing API (same as flex layout)
    // This uses the shared font backend for accurate text measurement
    if (!has_explicit_width) {
        IntrinsicSizes item_sizes = layout_measure_intrinsic_widths(
            lycon, lam::dom_require<DOM_NODE_ELEMENT>(item), "grid item intrinsic");
        *min_width = item_sizes.min_content;
        *max_width = item_sizes.max_content;
    }

    if (!has_explicit_height) {
        // Height calculation for grid items:
        // - For min-content height: use max-content width (content flows without wrapping)
        // - For max-content height: same as min-content for block containers
        //
        // Note: Counter-intuitively, using max-content WIDTH gives MINIMUM height
        // because text doesn't wrap. Using min-content width causes wrapping = taller.
        //
        // CSS Sizing Level 3 says: For block containers, min-content height == max-content height
        // Both should be calculated at max-content width (no forced wrapping).
        //
        // The actual grid track sizing will use max-content height for auto rows.
        float width_for_height = (float)*max_width;

        // Cap to a reasonable maximum to avoid extremely long single-line text
        if (width_for_height > 2000) {
            width_for_height = 2000;
        }

        // For block containers, min-content height == max-content height
        float content_height = calculate_max_content_height(lycon, static_cast<DomNode*>(item), width_for_height);
        *min_height = content_height;
        *max_height = content_height;
    }

    // NOTE: Padding and border are already included by intrinsic width/height
    // measurement helpers. Do not add padding/border again here.

    // Store in cache
    store_in_measurement_cache(static_cast<DomNode*>(item), *max_width, *max_height,
                               *min_width, *min_height);

    log_debug("Grid item %s measured: min=%.1fx%.1f, max=%.1fx%.1f",
              item->node_name(), *min_width, *min_height, *max_width, *max_height);
}

// ============================================================================
// Pass 3: Final Content Layout
// ============================================================================

void layout_final_grid_content(LayoutContext* lycon, GridContainerLayout* grid_layout) {
    if (!grid_layout) return;

    log_enter();
    log_info("FINAL GRID CONTENT LAYOUT START");
    log_debug("grid_layout=%p, item_count=%d, grid_items=%p",
              grid_layout, grid_layout->item_count, grid_layout->grid_items);

    // DEBUG: Print item pointers for comparison
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        log_debug("Pass3: grid_items[%d]=%p, x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
               i, (void*)item, item ? item->x : -1, item ? item->y : -1,
               item ? item->width : -1, item ? item->height : -1);
    }

    // Layout content within each grid item with their final sizes
    for (int i = 0; i < grid_layout->item_count; i++) {
        ViewBlock* item = grid_layout->grid_items[i];
        if (!item) continue;

        log_debug("Final layout for grid item %d: %s at (%.0f,%.0f) size %.0fx%.0f",
                  i, item->node_name(), item->x, item->y, item->width, item->height);

        layout_grid_item_final_content_multipass(lycon, item);
    }

    log_info("FINAL GRID CONTENT LAYOUT END");
    log_leave();
}

// Layout final content of a single grid item (multipass version with nested support)
static void layout_grid_item_final_content_multipass(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;

    log_enter();
    log_info("Layout grid item content: item=%p (%s), size=%.0fx%.0f at (%.0f,%.0f)",
             grid_item, grid_item->node_name(),
             grid_item->width, grid_item->height,
             grid_item->x, grid_item->y);

    // Save parent context
    BlockContext pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;

    float grid_item_percentage_base = grid_item_percentage_base_from_parent(lycon, grid_item);
    grid_re_resolve_item_percentage_box(grid_item, grid_item_percentage_base);

    // Update font context to use the grid item's own computed font
    // This ensures text nodes inside the item inherit the correct font-size
    if (grid_item->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, grid_item->font);
    }

    // Calculate content area dimensions accounting for box model
    float content_width = layout_content_width_from_border_box(grid_item, grid_item->width);
    float content_height = layout_content_height_from_border_box(grid_item, grid_item->height);
    float content_x_offset = 0;
    float content_y_offset = 0;

    if (grid_item->bound) {
        BoxMetrics box = layout_box_metrics(grid_item);
        content_x_offset = box.padding.left + box.border.left;
        content_y_offset = box.padding.top + box.border.top;
    }

    // Set up block formatting context for nested content
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    lycon->block.given_width = content_width;
    lycon->block.given_height = -1;  // Auto height
    lycon->block.advance_y = content_y_offset;  // Start after padding/border top
    lycon->block.max_width = 0;
    lycon->elmt = static_cast<DomNode*>(grid_item);

    // Inherit text alignment from grid item if specified
    if (grid_item->blk) {
        lycon->block.text_align = grid_item->blk->text_align;
    }

    // Set up line formatting context
    // Add a small subpixel tolerance to the right boundary to compensate for integer truncation
    // of float track widths (e.g. 173.28px -> 173px causes 153.28px text to spuriously wrap).
    // This only affects grid item layout, not float/normal flow layouts.
    line_init(lycon, content_x_offset, content_x_offset + content_width);
    lycon->line.right += 0.5f;

    // Check if this grid item is itself a grid or flex container (nested)
    if (grid_item->display.inner == CSS_VALUE_GRID) {
        log_info(">>> NESTED GRID DETECTED: item=%p (%s)", grid_item, grid_item->node_name());

        // Recursively handle nested grid
        BlockContext item_parent_ctx = {};
        BlockContext* saved_parent = lycon->block.parent;
        if (saved_parent) item_parent_ctx = *saved_parent;
        item_parent_ctx.content_width = grid_item_percentage_base;
        lycon->block.parent = &item_parent_ctx;
        layout_grid_content(lycon, grid_item);
        lycon->block.parent = saved_parent;

    } else if (grid_item->display.inner == CSS_VALUE_FLEX) {
        log_info(">>> NESTED FLEX DETECTED: item=%p (%s)", grid_item, grid_item->node_name());

        // Use flex layout for nested flex container
        // The flex layout will initialize its own flex items with init_flex_item_view
        // Do NOT call init_grid_item_view for flex children - they are flex items, not grid items!
        extern void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container);
        BlockContext item_parent_ctx = {};
        BlockContext* saved_parent = lycon->block.parent;
        if (saved_parent) item_parent_ctx = *saved_parent;
        item_parent_ctx.content_width = grid_item_percentage_base;
        lycon->block.parent = &item_parent_ctx;
        layout_flex_container_with_nested_content(lycon, grid_item);
        lycon->block.parent = saved_parent;
        grid_item->content_height = grid_flex_container_auto_border_height(grid_item, lycon->block.advance_y);

    } else {
        // Standard flow layout for grid item content
        log_debug("Layout flow content for grid item %s", grid_item->node_name());

        DomNode* child = grid_item->first_child;
        while (child) {
            layout_flow_node(lycon, child);
            child = child->next_sibling;
        }

        // Finalize any pending line content
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Update grid item content dimensions
    // Note: max_width and advance_y are relative to the content box
    // We need to add padding for the full content dimensions
    grid_re_resolve_item_percentage_box(grid_item, grid_item_percentage_base);
    grid_item->content_width = lycon->block.max_width;
    if (grid_item->display.inner != CSS_VALUE_FLEX) {
        if (grid_item->bound) {
            grid_item->content_width += grid_item->bound->padding.right;
            grid_item->content_height = lycon->block.advance_y + grid_item->bound->padding.bottom;
        } else {
            grid_item->content_height = lycon->block.advance_y;
        }
    }

    // Restore parent context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;

    log_info("Grid item content layout complete: %s, content=%dx%d",
             grid_item->node_name(), grid_item->content_width, grid_item->content_height);
    log_leave();
}

// ============================================================================
// Utility Functions
// ============================================================================

bool grid_item_is_nested_container(ViewBlock* item) {
    if (!item) return false;
    return (item->display.inner == CSS_VALUE_GRID ||
            item->display.inner == CSS_VALUE_FLEX);
}

// ============================================================================
// Grid Absolute Positioning Helpers
// ============================================================================

// Calculate track positions for a given axis
// Returns an array of (track_count + 1) positions representing grid line positions
// Caller must free the returned array
static float* calculate_grid_line_positions(GridContainerLayout* grid_layout, bool is_row_axis,
                                            float container_offset, int* out_line_count) {
    int track_count = is_row_axis ? grid_layout->computed_row_count : grid_layout->computed_column_count;
    GridTrack* tracks = is_row_axis ? grid_layout->computed_rows : grid_layout->computed_columns;
    float gap = is_row_axis ? grid_layout->row_gap : grid_layout->column_gap;

    // We need (track_count + 1) positions for grid lines
    int line_count = track_count + 1;
    float* positions = (float*)mem_calloc(line_count, sizeof(float), MEM_CAT_LAYOUT);
    if (!positions) return nullptr;

    float current_pos = container_offset;
    for (int i = 0; i <= track_count; i++) {
        positions[i] = current_pos;
        if (i < track_count) {
            current_pos += tracks[i].computed_size;
            if (i < track_count - 1) {
                current_pos += gap;
            }
        }
    }

    *out_line_count = line_count;
    return positions;
}

// Compute the containing block for an absolutely positioned grid item
// Based on its grid-column and grid-row properties
// Returns true if grid area was computed, false if should use normal containing block
static bool compute_grid_area_for_absolute(
    GridContainerLayout* grid_layout,
    ViewBlock* container,
    ViewBlock* item,
    float* out_x, float* out_y, float* out_width, float* out_height
) {
    if (!grid_layout || !container || !item) return false;

    // Get grid item properties (if any)
    GridItemProp* gi = grid_item_prop(item);

    // Check if item has any grid placement
    bool has_col_start = gi && gi->has_explicit_grid_column_start && gi->grid_column_start != 0;
    bool has_col_end = gi && gi->has_explicit_grid_column_end && gi->grid_column_end != 0;
    bool has_row_start = gi && gi->has_explicit_grid_row_start && gi->grid_row_start != 0;
    bool has_row_end = gi && gi->has_explicit_grid_row_end && gi->grid_row_end != 0;

    // If no explicit grid placement, use normal containing block (whole grid padding box)
    if (!has_col_start && !has_col_end && !has_row_start && !has_row_end) {
        log_debug("Absolute item has no grid placement, using full grid padding box");
        return false;
    }

    // Calculate container offsets (padding + border)
    float container_offset_x = 0, container_offset_y = 0;
    if (container->bound) {
        container_offset_x += container->bound->padding.left;
        container_offset_y += container->bound->padding.top;
        if (container->bound->border) {
            container_offset_x += container->bound->border->width.left;
            container_offset_y += container->bound->border->width.top;
        }
    }

    // Calculate grid line positions
    int col_line_count = 0, row_line_count = 0;
    float* col_positions = calculate_grid_line_positions(grid_layout, false, container_offset_x, &col_line_count);
    float* row_positions = calculate_grid_line_positions(grid_layout, true, container_offset_y, &row_line_count);

    if (!col_positions || !row_positions) {
        mem_free(col_positions);
        mem_free(row_positions);
        return false;
    }

    // CSS Grid §9.1: For absolutely positioned items, when a start or end is auto,
    // the containing block extends to the outer edge of the padding box at that end.
    // This is the BORDER edge (not the content/padding), so it includes padding but not border.
    float border_left = 0, border_right = 0, border_top = 0, border_bottom = 0;
    if (container->bound && container->bound->border) {
        border_left   = container->bound->border->width.left;
        border_right  = container->bound->border->width.right;
        border_top    = container->bound->border->width.top;
        border_bottom = container->bound->border->width.bottom;
    }
    // Padding-box outer edges in container border-box coordinates
    float col_auto_start_pos = border_left;
    float col_auto_end_pos   = (float)container->width  - border_right;
    float row_auto_start_pos = border_top;
    float row_auto_end_pos   = (float)container->height - border_bottom;

    // Resolve explicit grid lines (1-based CSS to 0-based index into positions array)
    // Handle negative line numbers (count from end)
    int col_start_line = has_col_start ? gi->grid_column_start : 0;
    int col_end_line   = has_col_end   ? gi->grid_column_end   : 0;
    int row_start_line = has_row_start ? gi->grid_row_start    : 0;
    int row_end_line   = has_row_end   ? gi->grid_row_end      : 0;

    if (col_start_line < 0) col_start_line = col_line_count + col_start_line + 1;
    if (col_end_line   < 0) col_end_line   = col_line_count + col_end_line   + 1;
    if (row_start_line < 0) row_start_line = row_line_count + row_start_line + 1;
    if (row_end_line   < 0) row_end_line   = row_line_count + row_end_line   + 1;

    // Resolve start/end positions:
    // - Explicit line → look up in positions array (clamp to valid range)
    // - Auto → use padding-box outer edge per CSS Grid §9.1
    float x_start = has_col_start
        ? col_positions[( col_start_line >= 1 && col_start_line <= col_line_count)
                         ? col_start_line - 1 : 0]
        : col_auto_start_pos;
    float x_end = has_col_end
        ? col_positions[(col_end_line >= 1 && col_end_line <= col_line_count)
                         ? col_end_line - 1 : col_line_count - 1]
        : col_auto_end_pos;
    float y_start = has_row_start
        ? row_positions[(row_start_line >= 1 && row_start_line <= row_line_count)
                         ? row_start_line - 1 : 0]
        : row_auto_start_pos;
    float y_end = has_row_end
        ? row_positions[(row_end_line >= 1 && row_end_line <= row_line_count)
                         ? row_end_line - 1 : row_line_count - 1]
        : row_auto_end_pos;

    // Ensure start <= end
    if (x_start > x_end) { float t = x_start; x_start = x_end; x_end = t; }
    if (y_start > y_end) { float t = y_start; y_start = y_end; y_end = t; }

    // Calculate grid area rectangle
    *out_x = x_start;
    *out_y = y_start;
    *out_width  = x_end - x_start;
    *out_height = y_end - y_start;

    log_debug("Grid area for absolute item: lines col %d-%d, row %d-%d => pos (%.1f, %.1f) size %.1fx%.1f",
              col_start_line, col_end_line, row_start_line, row_end_line,
              *out_x, *out_y, *out_width, *out_height);

    mem_free(col_positions);
    mem_free(row_positions);
    return true;
}

static void layout_grid_abs_prepare_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;
    state->has_grid_area = compute_grid_area_for_absolute(ctx->grid, container,
        state->child_block, &state->grid_area_x, &state->grid_area_y,
        &state->grid_area_width, &state->grid_area_height);
    state->parent_line.left = state->containing_block.padding_x;
    state->parent_block.advance_y = state->containing_block.padding_y;
}

static void layout_grid_abs_after_child(LayoutContext* lycon, ViewBlock* container,
    AbsStaticContext* ctx, AbsChildLayoutState* state) {
    (void)lycon;  (void)container;
    ViewBlock* child_block = state->child_block;
    if (!child_block || !child_block->position) return;

    LayoutContainingBlock cb = state->containing_block;
    GridContainerLayout* grid_layout = ctx->grid;

    if (state->has_grid_area) {
        float old_x = child_block->x;
        float old_y = child_block->y;
        PositionProp* pos = child_block->position;
        float new_x = old_x;
        float new_y = old_y;

        if (pos->has_left) {
            new_x = state->grid_area_x + pos->left;
            if (child_block->bound && child_block->bound->margin.left > 0.0f) {
                new_x += child_block->bound->margin.left;
            }
        } else if (pos->has_right) {
            new_x = state->grid_area_x + state->grid_area_width - pos->right - child_block->width;
            if (child_block->bound && child_block->bound->margin.right > 0.0f) {
                new_x -= child_block->bound->margin.right;
            }
        } else {
            new_x = state->grid_area_x;
        }

        if (pos->has_top) {
            new_y = state->grid_area_y + pos->top;
            if (child_block->bound && child_block->bound->margin.top > 0.0f) {
                new_y += child_block->bound->margin.top;
            }
        } else if (pos->has_bottom) {
            new_y = state->grid_area_y + state->grid_area_height - pos->bottom - child_block->height;
            if (child_block->bound && child_block->bound->margin.bottom > 0.0f) {
                new_y -= child_block->bound->margin.bottom;
            }
        } else {
            new_y = state->grid_area_y;
        }

        if (pos->has_left && pos->has_right) {
            float margin_left = child_block->bound ? child_block->bound->margin.left : 0.0f;
            float margin_right = child_block->bound ? child_block->bound->margin.right : 0.0f;
            child_block->width = state->grid_area_width - pos->left - pos->right -
                margin_left - margin_right;
            new_x = state->grid_area_x + pos->left + margin_left;
        }
        if (pos->has_top && pos->has_bottom) {
            float margin_top = child_block->bound ? child_block->bound->margin.top : 0.0f;
            float margin_bottom = child_block->bound ? child_block->bound->margin.bottom : 0.0f;
            child_block->height = state->grid_area_height - pos->top - pos->bottom -
                margin_top - margin_bottom;
            new_y = state->grid_area_y + pos->top + margin_top;
        }

        child_block->x = new_x;
        child_block->y = new_y;
        log_debug("[LAYOUT_ABS] grid area adjusted: (%.1f, %.1f) -> (%.1f, %.1f)",
                  old_x, old_y, new_x, new_y);
        return;
    }

    GridItemProp* gi = grid_item_prop(child_block);
    if (!gi) return;

    PositionProp* pos = child_block->position;
    bool no_horiz = !pos->has_left && !pos->has_right;
    bool no_vert = !pos->has_top && !pos->has_bottom;
    float ml = child_block->bound ? child_block->bound->margin.left : 0.0f;
    float mr = child_block->bound ? child_block->bound->margin.right : 0.0f;
    float mt = child_block->bound ? child_block->bound->margin.top : 0.0f;
    float mb = child_block->bound ? child_block->bound->margin.bottom : 0.0f;

    if (no_horiz) {
        int justify = radiant::resolve_justify_self(gi->justify_self,
            grid_layout ? grid_layout->justify_items : CSS_VALUE_STRETCH);
        float free_w = cb.padding_width - child_block->width - ml - mr;
        float offset = radiant::compute_alignment_offset_simple(justify, free_w);
        if (offset != 0.0f) child_block->x += offset;
    }
    if (no_vert) {
        int align = radiant::resolve_align_self(gi->align_self_grid,
            grid_layout ? grid_layout->align_items : CSS_VALUE_STRETCH);
        float free_h = cb.padding_height - child_block->height - mt - mb;
        float offset = radiant::compute_alignment_offset_simple(align, free_h);
        if (offset != 0.0f) child_block->y += offset;
    }
}

void layout_grid_absolute_children(LayoutContext* lycon, ViewBlock* container) {
    AbsStaticContext ctx = {};
    ctx.kind = ABS_STATIC_GRID;
    ctx.containing_block = layout_containing_block_for_view(container);
    ctx.grid = lycon ? lycon->grid_container : nullptr;
    ctx.log_context = "grid abs child";
    ctx.prepare_child = layout_grid_abs_prepare_child;
    ctx.after_child = layout_grid_abs_after_child;
    layout_absolute_children_in_context(lycon, container, &ctx);
}
