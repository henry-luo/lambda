#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_positioned.hpp"
#include "layout_cache.hpp"
#include "font_face.h"
#include "../lib/font/font.h"

#include <chrono>

extern "C" {
#include "../lib/memtrack.h"
}

#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/lambda-data.hpp"

using namespace std::chrono;

// ============================================================================
// Layout timing accumulators
// ============================================================================
double g_style_resolve_time = 0;
double g_text_layout_time = 0;
double g_block_layout_time = 0;
double g_inline_layout_time = 0;
double g_table_layout_time = 0;
double g_flex_layout_time = 0;
double g_grid_layout_time = 0;
int64_t g_style_resolve_count = 0;
int64_t g_style_resolve_full = 0;  // full resolutions (not cached)
int64_t g_style_resolve_measure = 0;  // resolutions during measurement
int64_t g_text_layout_count = 0;
int64_t g_block_layout_count = 0;
int64_t g_inline_layout_count = 0;

// ============================================================================
// Layout cache statistics (Taffy-inspired)
// ============================================================================
int64_t g_layout_cache_hits = 0;
int64_t g_layout_cache_misses = 0;
int64_t g_layout_cache_stores = 0;

void reset_layout_timing() {
    g_style_resolve_time = 0;
    g_text_layout_time = 0;
    g_block_layout_time = 0;
    g_inline_layout_time = 0;
    g_table_layout_time = 0;
    g_flex_layout_time = 0;
    g_grid_layout_time = 0;
    g_style_resolve_count = 0;
    g_style_resolve_full = 0;
    g_style_resolve_measure = 0;
    g_text_layout_count = 0;
    g_block_layout_count = 0;
    g_inline_layout_count = 0;
    // Reset cache statistics
    g_layout_cache_hits = 0;
    g_layout_cache_misses = 0;
    g_layout_cache_stores = 0;
}

void log_layout_timing_summary() {
    log_info("[TIMING] layout breakdown: style_resolve=%.1fms (%lld calls, %lld full, %lld measure), text=%.1fms (%lld), block=%.1fms (%lld)",
        g_style_resolve_time, g_style_resolve_count, g_style_resolve_full, g_style_resolve_measure,
        g_text_layout_time, g_text_layout_count,
        g_block_layout_time, g_block_layout_count);
    log_info("[TIMING] layout breakdown: table=%.1fms, flex=%.1fms, grid=%.1fms",
        g_table_layout_time, g_flex_layout_time, g_grid_layout_time);
    // Log cache statistics if any cache activity occurred
    if (g_layout_cache_hits > 0 || g_layout_cache_misses > 0) {
        int64_t total = g_layout_cache_hits + g_layout_cache_misses;
        double hit_rate = total > 0 ? (100.0 * g_layout_cache_hits / total) : 0.0;
        log_info("[CACHE] layout cache: hits=%lld, misses=%lld, stores=%lld, hit_rate=%.1f%%",
            g_layout_cache_hits, g_layout_cache_misses, g_layout_cache_stores, hit_rate);
    }
}

void view_pool_init(ViewTree* tree);
void view_pool_destroy(ViewTree* tree);
// Function declaration moved to layout.hpp
char* read_text_file(const char *filename);
void finalize_block_flow(LayoutContext* lycon, ViewBlock* block, CssEnum display);
// Forward declarations
void layout_inline(LayoutContext* lycon, DomNode *elmt, DisplayValue display);
void adjust_text_bounds(ViewText* text);
// resolve default style for HTML inline elements
void apply_element_default_style(LayoutContext* lycon, DomNode* elmt);

bool is_space(char c) {
    return c == ' ' || c == '\t' || c== '\r' || c == '\n';
}

bool is_only_whitespace(const char* str) {
    if (!str) return true;
    while (*str) {
        if (!is_space(*str)) return false;
        str++;
    }
    return true;
}

/**
 * Check if a node is a block-level element that participates in block formatting.
 * Block-level elements include: block, list-item, table, table-row, table-cell, etc.
 *
 * Note: inline-block is NOT included because it's an inline-level element
 * that creates a block formatting context internally. Whitespace between
 * inline-block elements should be preserved (as spaces), not collapsed.
 */
static bool is_block_level_element(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DisplayValue display = resolve_display_value(node);
    // Only true block-level elements - NOT inline-block (which is inline-level)
    return display.outer == CSS_VALUE_BLOCK ||
           display.outer == CSS_VALUE_LIST_ITEM ||
           display.outer == CSS_VALUE_TABLE ||
           display.outer == CSS_VALUE_TABLE_ROW ||
           display.outer == CSS_VALUE_TABLE_CELL;
}

/**
 * Check if a whitespace-only text node should be collapsed according to CSS rules.
 * CSS 2.2: "When white space is contained at the end of a block's content, or at
 * the start, or between block-level elements, it is rendered as nothing."
 *
 * This function returns true if the text node should be skipped during layout.
 */
static bool should_collapse_inter_element_whitespace(DomNode* text_node) {
    if (!text_node || !text_node->parent) return false;

    // Only applies to text nodes in block containers
    if (!text_node->parent->is_block()) return false;

    // Check if text is whitespace-only
    const char* str = (const char*)text_node->text_data();
    if (!is_only_whitespace(str)) return false;

    // Case 1: Whitespace at start of block (no previous sibling)
    // AND there's a block-level sibling after it
    if (!text_node->prev_sibling && text_node->next_sibling) {
        if (is_block_level_element(text_node->next_sibling)) {
            return true;
        }
    }

    // Case 2: Whitespace at end of block (no next sibling)
    // This is already handled, but let's be consistent
    if (!text_node->next_sibling) {
        return true;
    }

    // Case 3: Whitespace between two block-level elements
    if (text_node->prev_sibling && text_node->next_sibling) {
        bool prev_is_block = is_block_level_element(text_node->prev_sibling);
        bool next_is_block = is_block_level_element(text_node->next_sibling);

        // If either neighbor is a block, collapse the whitespace
        // (This follows browser behavior more closely)
        if (prev_is_block || next_is_block) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Run-in box helper functions (CSS 2.1 Section 9.2.3)
// ============================================================================

/**
 * Check if an element contains any block-level child.
 * Used for run-in: if a run-in box contains a block-level element,
 * the run-in box itself becomes a block box.
 */
static bool run_in_contains_block_child(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            DisplayValue child_display = resolve_display_value(child);
            // Check for block-level elements (block, list-item, table, etc.)
            if (child_display.outer == CSS_VALUE_BLOCK ||
                child_display.outer == CSS_VALUE_LIST_ITEM ||
                child_display.outer == CSS_VALUE_RUN_IN ||  // nested run-in counts as block
                child_display.inner == CSS_VALUE_TABLE) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Find the next sibling element, considering "immediately followed" semantics.
 * For run-in: we need to find if there's a block box immediately following.
 *
 * CSS 2.1: "immediately followed" means no intervening content, except
 * for whitespace that would be collapsed in normal formatting.
 * With white-space: pre, whitespace is NOT collapsed and blocks merging.
 *
 * Since checking white-space at this point is complex (styles may not be resolved),
 * we take a conservative approach: only skip whitespace text nodes that would
 * be collapsed in normal flow (whitespace-only text between elements).
 */
static DomNode* get_next_element_sibling(DomNode* node) {
    if (!node) return nullptr;
    DomNode* sibling = node->next_sibling;
    while (sibling) {
        if (sibling->is_element()) {
            return sibling;
        } else if (sibling->is_text()) {
            // Check if the text would be collapsed in normal formatting
            // Whitespace between block elements is collapsed, but we need
            // to be conservative here since we don't know the final display
            // of siblings yet
            const char* text = (const char*)sibling->text_data();
            if (!is_only_whitespace(text)) {
                // Non-whitespace text blocks run-in from merging
                return nullptr;
            }
            // Check if parent has white-space: pre/pre-wrap/pre-line/break-spaces
            // which would preserve this whitespace
            DomNode* parent = node->parent;
            if (parent && parent->is_element()) {
                DomElement* parent_elem = parent->as_element();
                if (parent_elem->blk && parent_elem->blk->white_space != 0) {
                    CssEnum ws = parent_elem->blk->white_space;
                    // white-space values that preserve whitespace
                    if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
                        ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES) {
                        // Whitespace is preserved, blocks run-in merging
                        return nullptr;
                    }
                }
            }
            // Whitespace would be collapsed, continue looking
        }
        sibling = sibling->next_sibling;
    }
    return nullptr;
}

/**
 * Check if the next sibling is a block box that run-in can merge into.
 * CSS 2.1: The run-in becomes inline if immediately followed by a block box.
 */
static bool run_in_should_merge_with_next(DomNode* run_in_node) {
    DomNode* next = get_next_element_sibling(run_in_node);
    if (!next) return false;

    DisplayValue next_display = resolve_display_value(next);

    // Can only merge with a block-level flow container (not flex/grid/table)
    if (next_display.outer == CSS_VALUE_BLOCK &&
        (next_display.inner == CSS_VALUE_FLOW || next_display.inner == CSS_VALUE_FLOW_ROOT)) {
        return true;
    }

    return false;
}

/**
 * Merge run-in element's children into the following block as first inline content.
 * This modifies the DOM tree by:
 * 1. Moving all children of run-in to the beginning of the following block
 * 2. Removing the run-in element from the tree (it becomes empty/display:none)
 */
static void merge_run_in_with_next_block(LayoutContext* lycon, DomElement* run_in, DomElement* next_block) {
    if (!lycon || !run_in || !next_block) return;

    log_debug("[RUN-IN] Merging <%s> into <%s>",
              run_in->tag_name ? run_in->tag_name : "unknown",
              next_block->tag_name ? next_block->tag_name : "unknown");

    // Get run-in's children
    DomNode* first_run_in_child = run_in->first_child;
    DomNode* last_run_in_child = run_in->last_child;

    if (!first_run_in_child) {
        // Empty run-in - just hide it
        run_in->display.outer = CSS_VALUE_NONE;
        run_in->display.inner = CSS_VALUE_NONE;
        return;
    }

    // Save next block's first child
    DomNode* next_block_first_child = next_block->first_child;

    // Update parent pointers for all run-in children
    for (DomNode* child = first_run_in_child; child; child = child->next_sibling) {
        child->parent = next_block;
    }

    // Link run-in children as first children of next_block
    if (next_block_first_child) {
        // Insert before existing children
        last_run_in_child->next_sibling = next_block_first_child;
        next_block_first_child->prev_sibling = last_run_in_child;
    } else {
        // Block was empty, run-in children become only children
        next_block->last_child = last_run_in_child;
    }
    next_block->first_child = first_run_in_child;
    first_run_in_child->prev_sibling = nullptr;

    // Clear run-in's children and hide the element
    run_in->first_child = nullptr;
    run_in->last_child = nullptr;
    run_in->display.outer = CSS_VALUE_NONE;
    run_in->display.inner = CSS_VALUE_NONE;

    log_debug("[RUN-IN] Merge complete, run-in now hidden");
}

/**
 * Resolve run-in display for an element.
 * Called during layout to determine if a run-in box should:
 * 1. Become a block (contains block child or not followed by block)
 * 2. Merge into following block (become inline)
 *
 * Returns the effective display value after run-in resolution.
 */
static DisplayValue resolve_run_in_display(LayoutContext* lycon, DomNode* node) {
    DisplayValue result = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};  // default: becomes block

    if (!node || !node->is_element()) return result;
    DomElement* elem = node->as_element();

    // CSS 2.1: If run-in contains a block-level element, it becomes block
    if (run_in_contains_block_child(node)) {
        log_debug("[RUN-IN] <%s> contains block child, becomes BLOCK",
                  elem->tag_name ? elem->tag_name : "unknown");
        return result;
    }

    // CSS 2.1: If run-in is immediately followed by a block box, merge into it
    DomNode* next = get_next_element_sibling(node);
    if (next && run_in_should_merge_with_next(node)) {
        DomElement* next_elem = next->as_element();

        // Perform the merge - this moves run-in children into next block
        merge_run_in_with_next_block(lycon, elem, next_elem);

        // Return NONE since run-in is now hidden (children moved to next block)
        result.outer = CSS_VALUE_NONE;
        result.inner = CSS_VALUE_NONE;
        return result;
    }

    // CSS 2.1: Otherwise, run-in becomes a block box
    log_debug("[RUN-IN] <%s> not followed by block, becomes BLOCK",
              elem->tag_name ? elem->tag_name : "unknown");
    return result;
}

// Read OS/2 table metrics using FontHandle
// Reference: Chrome Blink simple_font_data.cc TypoAscenderAndDescender()
TypoMetrics get_os2_typo_metrics(FontHandle* handle) {
    TypoMetrics result = {0, 0, 0, false, false};

    if (!handle) {
        log_error("get_os2_typo_metrics called with NULL handle");
        return result;
    }

    const FontMetrics* m = font_get_metrics(handle);
    if (!m) return result;

    // check if OS/2 typo metrics differ from hhea (indicating a real OS/2 table)
    if (m->typo_ascender == 0 && m->typo_descender == 0) {
        return result;  // no OS/2 table or no meaningful typo metrics
    }

    result.ascender  = m->typo_ascender;
    result.descender = m->typo_descender;  // already positive from FontMetrics
    result.line_gap  = m->typo_line_gap;
    result.valid = true;

    // USE_TYPO_METRICS flag is already resolved in FontMetrics
    result.use_typo_metrics = m->use_typo_metrics;

    return result;
}

// Calculate normal line height following Chrome Blink — delegates to font module
float calc_normal_line_height(FontHandle* handle) {
    return font_calc_normal_line_height(handle);
}

CssValue inherit_line_height(LayoutContext* lycon, ViewBlock* block) {
    // Inherit line height from parent
    INHERIT:
    ViewElement* parent = block->parent_view();
    if (parent) { // parent can be block or span
        // inherit the specified css value, not the resolved value
        if (parent->blk && parent->blk->line_height) {
            if (parent->blk->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
                parent->blk->line_height->data.keyword == CSS_VALUE_INHERIT) {
                block = (ViewBlock*)parent;
                goto INHERIT;
            }
            return *parent->blk->line_height;
        }
        block = (ViewBlock*)parent;
        goto INHERIT;
    }
    else { // initial value - 'normal'
        CssValue normal_value;
        normal_value.type = CSS_VALUE_TYPE_KEYWORD;
        normal_value.data.keyword = CSS_VALUE_NORMAL;
        return normal_value;
    }
}

void setup_line_height(LayoutContext* lycon, ViewBlock* block) {
    CssValue value;
    if (block->blk && block->blk->line_height) {
        if (block->blk->line_height->type == CSS_VALUE_TYPE_KEYWORD &&
            block->blk->line_height->data.keyword == CSS_VALUE_INHERIT) {
            value = inherit_line_height(lycon, block);
        } else {
            value = *block->blk->line_height;
        }
    } else { // normal initial value
        value.type = CSS_VALUE_TYPE_KEYWORD;
        value.data.keyword = CSS_VALUE_NORMAL;
    }
    if (value.type == CSS_VALUE_TYPE_KEYWORD && value.data.keyword == CSS_VALUE_NORMAL) {
        // 'normal' line height
        lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
        lycon->block.line_height_is_normal = true;
        log_debug("normal lineHeight: %f", lycon->block.line_height);
    } else {
        // Resolve var() if present
        const CssValue* resolved_value = resolve_var_function(lycon, &value);
        if (!resolved_value) {
            // var() couldn't be resolved, use normal
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
            log_debug("line-height var() unresolved, using normal: %f", lycon->block.line_height);
            return;
        }

        // resolve length/number/percentage
        float resolved_height =
        resolved_value->type == CSS_VALUE_TYPE_NUMBER ?
            resolved_value->data.number.value * lycon->font.current_font_size :
            resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, resolved_value);

        // CSS 2.1 §10.8.1: "Negative values are not allowed" for line-height
        // Zero is a valid computed value; only negative/NaN falls back to 'normal'
        if (resolved_height < 0 || std::isnan(resolved_height)) {
            log_debug("invalid line-height: %f, falling back to normal", resolved_height);
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
        } else {
            lycon->block.line_height = resolved_height;
            lycon->block.line_height_is_normal = false;
            log_debug("resolved line height: %f", lycon->block.line_height);
        }
    }
}

// DomNode style resolution function
// Ensures styles are resolved only once per layout pass using styles_resolved flag
// NOTE: Measurement mode (is_measuring=true) must NOT mark styles as resolved,
// because percentage-based values need to be re-resolved against the actual
// containing block dimensions during the real layout pass.
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    auto t_start = high_resolution_clock::now();

    if (node && node->is_element()) {
        DomElement* dom_elem = node->as_element();

        if (dom_elem && dom_elem->specified_style) {
            // Check if styles already resolved in this layout pass
            // IMPORTANT: Skip this check during measurement mode (is_measuring=true)
            // because measurement passes should not permanently mark styles as resolved
            // and percentage values may need different containing block dimensions
            if (dom_elem->styles_resolved && !lycon->is_measuring) {
                log_debug("[CSS] Skipping style resolution for <%s> - already resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
                g_style_resolve_count++;
                auto t_end = high_resolution_clock::now();
                g_style_resolve_time += duration<double, std::milli>(t_end - t_start).count();
                return;  // early return - reuse existing styles
            }

            // Invalidate layout cache when styles are being re-resolved
            // This ensures cached measurements are recomputed with new styles
            if (dom_elem->layout_cache) {
                radiant::layout_cache_clear(dom_elem->layout_cache);
            }

            // Apply element default styles ONLY if not already resolved
            // This must happen BEFORE CSS resolution so CSS can override defaults
            // (e.g., anchor default blue color overridden by .btn-primary { color: white })
            apply_element_default_style(lycon, dom_elem);

            // Track measurement vs full resolution
            if (lycon->is_measuring) {
                g_style_resolve_measure++;
            } else {
                g_style_resolve_full++;
            }

            // Lambda CSS: use the full implementation from resolve_css_style.cpp
            resolve_css_styles(dom_elem, lycon);

            // Mark as resolved for this layout pass
            // Don't mark as resolved during measurement mode - let the actual layout pass do that
            if (!lycon->is_measuring) {
                dom_elem->styles_resolved = true;
                log_debug("[CSS] Resolved styles for <%s> - marked as resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            } else {
                log_debug("[CSS] Resolved styles for <%s> in measurement mode - not marking resolved",
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            }
        } else {
            // No specified_style - still apply element default styles for HTML attributes
            apply_element_default_style(lycon, dom_elem);
        }
    }

    auto t_end = high_resolution_clock::now();
    g_style_resolve_time += duration<double, std::milli>(t_end - t_start).count();
    g_style_resolve_count++;
}

float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline, float valign_offset) {
    log_debug("calculate vertical align: align=%d, item_height=%f, line_height=%f, baseline_pos=%f, item_baseline=%f, offset=%f",
        align, item_height, line_height, baseline_pos, item_baseline, valign_offset);
    switch (align) {
    case CSS_VALUE_BASELINE:
        // For length/percentage vertical-align, offset shifts baseline (positive = raise = lower y)
        return baseline_pos - item_baseline - valign_offset;
    case CSS_VALUE_TOP:
        return 0;
    case CSS_VALUE_MIDDLE:
        return (line_height - item_height) / 2;
    case CSS_VALUE_BOTTOM:
        log_debug("bottom-aligned-text: line %d", line_height);
        return line_height - item_height;
    case CSS_VALUE_TEXT_TOP:
        // align with the top of the parent's font
        return baseline_pos - lycon->block.init_ascender;
    case CSS_VALUE_TEXT_BOTTOM:
        // align with the bottom of the parent's font
        return baseline_pos + lycon->block.init_descender - item_height;
    case CSS_VALUE_SUB:
        // Subscript position (approximately 0.3em lower)
        return baseline_pos - item_baseline + 0.3 * line_height;
    case CSS_VALUE_SUPER:
        // Superscript position (approximately 0.3em higher)
        return baseline_pos - item_baseline - 0.3 * line_height;
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    log_debug("span_vertical_align");
    View* child = span->first_child;
    if (child) {
        if (span->font) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
        if (span->in_line && span->in_line->vertical_align) {
            lycon->line.vertical_align = span->in_line->vertical_align;
            lycon->line.vertical_align_offset = span->in_line->vertical_align_offset;
        }
        do {
            view_vertical_align(lycon, child);
            child = child->next();
        } while (child);
    }
    lycon->font = pa_font;  lycon->line.vertical_align = pa_line_align;
    lycon->line.vertical_align_offset = pa_valign_offset;
}

// apply vertical alignment to a view
void view_vertical_align(LayoutContext* lycon, View* view) {
    log_debug("view_vertical_align: view=%d", view->view_type);
    float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        TextRect* rect = text_view->rect;
        while (rect) {
            float item_height = rect->height;
            // for text, baseline is at font.ascender
            log_debug("text view font: %p", text_view->font);
            float item_baseline = text_view->font ? text_view->font->ascender : item_height;
            float vertical_offset = calculate_vertical_align_offset(lycon, lycon->line.vertical_align, item_height,
                line_height, lycon->line.max_ascender, item_baseline, lycon->line.vertical_align_offset);
            log_debug("vertical-adjusted-text: y=%d, adv=%d, offset=%f, line=%f, hg=%f, txt='%.*t'",
                rect->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height,
                rect->length, text_view->text_data() + rect->start_index);
            rect->y = lycon->block.advance_y + max(vertical_offset, 0);
            rect = rect->next;
        }
        adjust_text_bounds(text_view);
    }
    else if (view->view_type == RDT_VIEW_INLINE_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        float item_height = block->height + (block->bound ?
            block->bound->margin.top + block->bound->margin.bottom : 0);
        // For replaced elements (like img), baseline is at bottom margin edge
        // item_baseline = distance from top of margin-box to baseline = entire height
        float item_baseline = item_height;
        CssEnum align = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align : lycon->line.vertical_align;
        float valign_offset = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align_offset : lycon->line.vertical_align_offset;
        float vertical_offset = calculate_vertical_align_offset(lycon, align, item_height,
            line_height, lycon->line.max_ascender, item_baseline, valign_offset);
        block->y = lycon->block.advance_y + max(vertical_offset, 0) + (block->bound ? block->bound->margin.top : 0);
        log_debug("vertical-adjusted-inline-block: y=%f, adv_y=%f, offset=%f, line=%f, blk=%f, max_asc=%f, max_desc=%f",
            block->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height, lycon->line.max_ascender, lycon->line.max_descender);
    }
    else if (view->view_type == RDT_VIEW_INLINE) {
        // for inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        span_vertical_align(lycon, span);
    }
    else {
        log_debug("view_vertical_align: unknown view type %d", view->view_type);
    }
}

void view_line_align(LayoutContext* lycon, float offset, View* view) {
    while (view) {
        log_debug("view line align: %d", view->view_type);
        view->x += offset;
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            text->x += offset;
            TextRect* rect = text->rect;
            while (rect) {
                rect->x += offset;
                rect = rect->next;
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            if (sp->first_child) view_line_align(lycon, offset, sp->first_child);
        }
        // else if (view->is_block()) {
        //     view->x += offset;
        // }
        // else {} // br
        view = view->next();
    }
}

// Count spaces in a text view for justify alignment
static int count_spaces_in_view(View* view) {
    int count = 0;
    while (view) {
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            const char* text_data = (const char*)text->text_data();
            if (text_data) {
                TextRect* rect = text->rect;
                while (rect) {
                    const char* str = text_data + rect->start_index;
                    for (int i = 0; i < rect->length; i++) {
                        if (str[i] == ' ') count++;
                    }
                    rect = rect->next;
                }
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            if (sp->first_child) {
                count += count_spaces_in_view(sp->first_child);
            }
        }
        view = view->next();
    }
    return count;
}

// Apply justify alignment by distributing space between words
static void view_line_justify(LayoutContext* lycon, float space_per_gap, View* view) {
    float cumulative_offset = 0;
    View* last_view = nullptr;
    TextRect* last_rect = nullptr;

    while (view) {
        view->x += cumulative_offset;
        last_view = view;

        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            text->x += cumulative_offset;

            const char* text_data = (const char*)text->text_data();
            TextRect* rect = text->rect;
            while (rect) {
                rect->x += cumulative_offset;
                last_rect = rect;

                // Add space after each space character
                if (text_data) {
                    const char* str = text_data + rect->start_index;
                    int space_count = 0;
                    for (int i = 0; i < rect->length; i++) {
                        if (str[i] == ' ') {
                            space_count++;
                        }
                    }
                    // Expand this rect's width by the space added within it
                    if (space_count > 0) {
                        float added_space = space_count * space_per_gap;
                        rect->width += added_space;
                        cumulative_offset += added_space;
                    }
                }

                rect = rect->next;
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* sp = (ViewSpan*)view;
            if (sp->first_child) {
                // Recursively justify inline children
                // Note: This is simplified - a full implementation would need to track
                // cumulative offset across the recursion
                view_line_justify(lycon, space_per_gap, sp->first_child);
            }
        }

        view = view->next();
    }

    // Extend the last text rect to fill any remaining space
    // This handles rounding errors and ensures the line is fully justified
    if (last_rect && last_view && last_view->view_type == RDT_VIEW_TEXT) {
        float line_end = lycon->block.content_width;
        float current_end = last_rect->x + last_rect->width;
        if (current_end < line_end) {
            last_rect->width += (line_end - current_end);
            log_debug("view_line_justify: extended last rect width by %.2fpx to fill line",
                      line_end - current_end);
        }
    }
}

void line_align(LayoutContext* lycon) {
    // horizontal text alignment: left, right, center, justify, start, end
    // Convert logical values (start/end) to physical values (left/right) for LTR text
    // Note: For RTL support in future, this would need to be reversed
    CssEnum text_align = lycon->block.text_align;
    if (text_align == CSS_VALUE_START) {
        text_align = CSS_VALUE_LEFT;  // LTR: start = left
    } else if (text_align == CSS_VALUE_END) {
        text_align = CSS_VALUE_RIGHT;  // LTR: end = right
    }

    if (text_align != CSS_VALUE_LEFT) {
        // Skip centering/right alignment only when laying out content INSIDE an inline-block
        // with shrink-to-fit width. In that case, the inline-block's width will shrink to fit
        // its content, so centering/right alignment would have no effect.
        //
        // Important: Check the CONTAINER (establishing_element), not the current view.
        // We want to center inline-blocks ON a line, just not content INSIDE a shrink-to-fit inline-block.
        ViewBlock* container = lycon->block.establishing_element;
        bool container_is_shrink_inline_block = container &&
            container->view_type == RDT_VIEW_INLINE_BLOCK &&
            lycon->block.given_width < 0;
        if (container_is_shrink_inline_block &&
            (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT)) {
            log_debug("line_align: skipping center/right align for content inside shrink-to-fit inline-block");
            return;
        }

        View* view = lycon->line.start_view;

        // Special handling for wrapped text continuation lines:
        // When text wraps within the same text node, start_view is NULL but we need to align
        // Check if we have a text view with multiple TextRects (= wrapped text)
        bool is_wrapped_continuation = false;
        if (!view && lycon->view && lycon->view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)lycon->view;
            TextRect* rect = text->rect;
            int rect_count = 0;
            while (rect && rect_count < 2) {  // Only need to count up to 2
                rect_count++;
                rect = rect->next;
            }
            if (rect_count > 1) {
                is_wrapped_continuation = true;
                view = lycon->view;
            }
        }

        // For justify, always use current view if start_view is NULL
        if (!view && text_align == CSS_VALUE_JUSTIFY) {
            view = lycon->view;
        }

        // For center/right without wrapped text, return if no start_view
        // (table cells and other blocks handle alignment themselves)
        if (!view) {
            return;
        }

        float line_width = lycon->line.advance_x - lycon->line.left;
        float offset = 0;

        if (text_align == CSS_VALUE_CENTER) {
            offset = (lycon->block.content_width - line_width) / 2;
        }
        else if (text_align == CSS_VALUE_RIGHT) {
            offset = lycon->block.content_width - line_width;
        }

        // For center/right alignment
        if (offset > 0 && (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT)) {
            // For wrapped text continuation lines, only align the current line's TextRect
            if (is_wrapped_continuation) {
                ViewText* text = (ViewText*)view;
                TextRect* rect = text->rect;
                TextRect* last_rect = rect;
                while (rect) {
                    last_rect = rect;
                    rect = rect->next;
                }

                if (last_rect) {
                    // Shift only this rect
                    last_rect->x += offset;
                }
            } else {
                // Normal case: align all views in the line
                view_line_align(lycon, offset, view);
            }
            return;
        }

        if (text_align == CSS_VALUE_JUSTIFY) {
                // For text nodes that wrap across multiple lines, we need to find the
                // TextRect that corresponds to this line and justify it
                if (view->view_type == RDT_VIEW_TEXT) {
                    ViewText* text = (ViewText*)view;
                    // Find the last TextRect (most recently created = current line)
                    TextRect* rect = text->rect;
                    TextRect* last_rect = rect;
                    while (rect) {
                        last_rect = rect;
                        rect = rect->next;
                    }

                    if (last_rect) {
                        // Check if this is the last line: only true if we're at the end of the text node
                        const char* text_data = (const char*)text->text_data();
                        bool is_last_line = false;

                        if (text_data) {
                            size_t text_len = strlen(text_data);
                            size_t rect_end = last_rect->start_index + last_rect->length;

                            // Only consider it the last line if we're at the very end of the text
                            // (rect_end == text_len means we've consumed all text)
                            if (rect_end >= text_len) {
                                is_last_line = true;
                            }
                        }

                        // Don't justify the last line per CSS spec
                        if (is_last_line) {
                            return;
                        }

                        // Count spaces in this specific rect
                        int num_spaces = 0;
                        if (text_data) {
                            const char* str = text_data + last_rect->start_index;
                            for (int i = 0; i < last_rect->length; i++) {
                                if (str[i] == ' ') num_spaces++;
                            }
                        }

                        float extra_width = lycon->block.content_width - line_width;

                        if (num_spaces > 0 && extra_width > 0) {
                            // Expand the width of this specific TextRect to fill the line
                            last_rect->width += extra_width;
                            return;
                        }
                    }
                }
                else {
                    // Multi-view line (has start_view), use the original logic
                    int num_spaces = count_spaces_in_view(view);
                    float extra_width = lycon->block.content_width - line_width;
                    if (num_spaces > 0 && extra_width > 0) {
                        float space_per_gap = extra_width / num_spaces;
                        view_line_justify(lycon, space_per_gap, view);
                        return;
                    }
                }
                return;
            }
    }
}

void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    log_debug("layout node %s, advance_y: %f", node->node_name(), lycon->block.advance_y);

    // Log for IMG elements
    uintptr_t node_tag = node->tag();
    if (node_tag == HTM_TAG_IMG) {
        log_debug("[FLOW_NODE IMG] Processing IMG element: %s", node->node_name());
    }

    // Skip HTML comments (Lambda CSS parser creates these as elements with name "!--")
    const char* node_name = node->node_name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        log_debug("skipping HTML comment node");
        return;
    }

    if (node->is_element()) {
        DomElement* elem = node->as_element();

        // Handle ViewMarker (list bullet/number) with fixed width
        // These are created with view_type = RDT_VIEW_MARKER in layout_block.cpp
        if (elem->view_type == RDT_VIEW_MARKER) {
            // Get marker properties from blk (which stores MarkerProp* for markers)
            MarkerProp* marker_prop = (MarkerProp*)elem->blk;
            if (marker_prop) {
                // Create inline view for the marker with fixed width
                ViewSpan* marker_span = (ViewSpan*)set_view(lycon, RDT_VIEW_MARKER, elem);
                if (marker_span) {
                    // FreeType metrics are in physical pixels, divide by pixel_ratio for CSS pixels
                    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                    marker_span->width = marker_prop->width;
                    marker_span->height = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle)->hhea_line_height : 16.0f;

                    // Set marker position
                    marker_span->x = lycon->line.advance_x;
                    marker_span->y = lycon->block.advance_y;

                    // Advance inline position by fixed marker width
                    lycon->line.advance_x += marker_prop->width;

                    // Update line metrics (marker contributes to line height)
                    float ascender = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle)->hhea_ascender : 12.0f;
                    float descender = lycon->font.font_handle ? -(font_get_metrics(lycon->font.font_handle)->hhea_descender) : 4.0f;
                    if (ascender > lycon->line.max_ascender) lycon->line.max_ascender = ascender;
                    if (descender > lycon->line.max_descender) lycon->line.max_descender = descender;

                    log_debug("[MARKER] Laid out marker with fixed width=%.1f, height=%.1f at (%.1f, %.1f)",
                             marker_prop->width, marker_span->height, marker_span->x, marker_span->y);
                }
            }
            return;
        }

        // Skip floats that were pre-laid in the float pre-pass
        if (elem->float_prelaid) {
            log_debug("skipping pre-laid float: %s", node->node_name());
            return;
        }

        // Use resolve_display_value which handles both Lexbor and Lambda CSS nodes
        DisplayValue display = resolve_display_value(node);
        log_debug("processing element: %s, with display: outer=%d, inner=%d", node->node_name(), display.outer, display.inner);

        // Log IMG display resolution
        if (node_tag == HTM_TAG_IMG) {
            log_debug("[FLOW_NODE IMG] Resolved display for IMG: outer=%d, inner=%d (INLINE_BLOCK=%d, INLINE=%d)",
                     display.outer, display.inner, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_INLINE);
        }

        // CSS 2.2 Section 9.7: When float is not 'none', display is computed as 'block'
        // Check float property from specified styles (before view is created)
        CssEnum float_value = CSS_VALUE_NONE;

        // First check if position is already resolved
        if (elem->position) {
            float_value = elem->position->float_prop;
        } else if (elem->specified_style && elem->specified_style->tree) {
            // Check float property from CSS style tree
            AvlNode* float_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_FLOAT);
            if (float_node) {
                StyleNode* style_node = (StyleNode*)float_node->declaration;
                if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                    CssValue* val = style_node->winning_decl->value;
                    if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        float_value = val->data.keyword;
                    }
                }
            }
        }

        if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
            // Float transforms most display values to block
            if (display.outer != CSS_VALUE_NONE) {
                log_debug("Float on %s: transforming display from outer=%d to BLOCK (float=%d)",
                          node->node_name(), display.outer, float_value);
                display.outer = CSS_VALUE_BLOCK;
                // Keep inner display but treat as flow for layout purposes if it's a table type
                if (display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                    display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                    display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                    display.inner == CSS_VALUE_TABLE_ROW ||
                    display.inner == CSS_VALUE_TABLE_COLUMN ||
                    display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
                    display.inner == CSS_VALUE_TABLE_CAPTION) {
                    display.inner = CSS_VALUE_FLOW;
                }
            }
        }

        // CSS 2.2 Section 9.7: Absolutely positioned (position: absolute/fixed) elements
        // are blockified - 'display: inline' becomes 'display: block', etc.
        CssEnum position_value = CSS_VALUE_STATIC;
        if (elem->position) {
            position_value = elem->position->position;
        } else if (elem->specified_style && elem->specified_style->tree) {
            AvlNode* pos_node = avl_tree_search(elem->specified_style->tree, CSS_PROPERTY_POSITION);
            if (pos_node) {
                StyleNode* style_node = (StyleNode*)pos_node->declaration;
                if (style_node && style_node->winning_decl && style_node->winning_decl->value) {
                    CssValue* val = style_node->winning_decl->value;
                    if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        position_value = val->data.keyword;
                    }
                }
            }
        }

        if (position_value == CSS_VALUE_ABSOLUTE || position_value == CSS_VALUE_FIXED) {
            // Absolutely positioned elements become block-level
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_RUN_IN) {
                log_debug("Position absolute/fixed on %s: transforming display from outer=%d to BLOCK",
                          node->node_name(), display.outer);
                display.outer = CSS_VALUE_BLOCK;
            }
        }

        // CSS 2.1 Section 9.2.3: Run-in boxes
        // A run-in box behaves as follows:
        // - If it contains a block-level box, becomes block
        // - If immediately followed by a block box, merges into that block as inline
        // - Otherwise, becomes block
        if (display.outer == CSS_VALUE_RUN_IN) {
            DisplayValue resolved = resolve_run_in_display(lycon, node);
            if (resolved.outer == CSS_VALUE_NONE) {
                // Run-in was merged into following block, skip layout
                log_debug("run-in merged into following block, skipping");
                return;
            }
            // Run-in becomes block
            display = resolved;
        }

        switch (display.outer) {
        case CSS_VALUE_BLOCK:  case CSS_VALUE_INLINE_BLOCK:  case CSS_VALUE_LIST_ITEM:
        case CSS_VALUE_TABLE_CELL:  // CSS display: table-cell on non-table elements
            layout_block(lycon, node, display);
            break;
        case CSS_VALUE_INLINE:
            // CSS 2.1 Section 10.3.2: Inline replaced elements (img, video, etc.)
            // are laid out like inline-block because they have intrinsic dimensions
            if (display.inner == RDT_DISPLAY_REPLACED) {
                // Treat inline replaced elements as inline-block for layout
                display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, display);
            } else if (display.inner == CSS_VALUE_TABLE) {
                // CSS 2.1 Section 17.2: inline-table elements
                // Outer display is inline (participates in inline flow)
                // but inner display is table (creates table formatting context)
                // Treat as inline-block for positioning, with table inner layout
                display.outer = CSS_VALUE_INLINE_BLOCK;
                layout_block(lycon, node, display);
            } else {
                layout_inline(lycon, node, display);
            }
            break;
        case CSS_VALUE_NONE:
            log_debug("skipping element of display: none");
            break;
        default:
            log_debug("unknown display type: outer=%d", display.outer);
            // skip the element
        }
    }
    else if (node->is_text()) {
        const unsigned char* str = node->text_data();
        log_debug("layout_text: '%t'", str);
        // Skip inter-element whitespace (whitespace between/around block elements)
        // CSS 2.2: "When white space is contained at the end of a block's content,
        // or at the start, or between block-level elements, it is rendered as nothing."
        if (should_collapse_inter_element_whitespace(node)) {
            node->view_type = RDT_VIEW_NONE;
            log_debug("skipping inter-element whitespace text");
        }
        else {
            layout_text(lycon, node);
        }
    }
    else {
        log_debug("layout unknown node type: %d", node->node_type);
        // skip the node
    }
    log_debug("end flow node, block advance_y: %d", lycon->block.advance_y);
}

void layout_html_root(LayoutContext* lycon, DomNode* elmt) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    log_debug("layout html root");
    log_debug("DEBUG: elmt=%p, type=%d", (void*)elmt, elmt ? (int)elmt->node_type : -1);
    //log_debug("DEBUG: About to call apply_header_style");
    //apply_header_style(lycon);
    log_debug("DEBUG: apply_header_style complete");

    // init context
    log_debug("DEBUG: Initializing layout context");
    lycon->elmt = elmt;
    lycon->root_font_size = lycon->font.current_font_size = -1;  // unresolved yet
    // Layout uses physical pixels (lycon->width/height) for rendering surface compatibility.
    // Font sizes are already scaled by pixel_ratio during style resolution.
    float physical_width = lycon->width;
    float physical_height = lycon->height;
    lycon->block.max_width = lycon->block.content_width = physical_width;
    // Set root element height to viewport to enable scrollbars when content overflows
    lycon->block.content_height = physical_height;
    lycon->block.advance_y = 0;  lycon->block.line_height = -1;  lycon->block.line_height_is_normal = true;
    lycon->block.text_align = CSS_VALUE_LEFT;

    // Set available space to viewport dimensions (physical pixels for layout)
    lycon->available_space = AvailableSpace::make_width_definite(physical_width);

    line_init(lycon, 0, lycon->block.content_width);

    // Initialize parent block context - save current state
    BlockContext saved_block = lycon->block;
    lycon->block.parent = &saved_block;

    ViewBlock* html = (ViewBlock*)set_view(lycon, RDT_VIEW_BLOCK, elmt);
    html->width = lycon->block.content_width;
    // Don't pre-set html->height - let it be determined by content (auto height)
    // The viewport height will be used for scrollbar calculations via scroller->viewport_height
    lycon->doc->view_tree->root = (View*)html;  lycon->elmt = elmt;

    // html->scroller->viewport_height = physical_height;  // For scrollbar calculations
    lycon->block.given_width = physical_width;
    // Don't set given_height - let html use auto (content-based) height
    lycon->block.given_height = -1;  // -1 means auto height
    html->position = alloc_position_prop(lycon);

    // Create the initial Block Formatting Context for the root element
    // CSS 2.2: The root element establishes the initial BFC
    html->content_width = physical_width;
    Pool* layout_pool = lycon->doc->view_tree->pool;
    log_debug("[BlockContext] Initializing root BFC for HTML element");

    // Initialize the unified BlockContext for the root element
    block_context_init(&lycon->block, html, layout_pool);
    lycon->block.content_width = physical_width;
    lycon->block.float_right_edge = physical_width;
    log_debug("[BlockContext] Root BFC created (width=%.1f)", html->content_width);

    auto t_init = high_resolution_clock::now();
    log_info("[TIMING] layout: context init: %.1fms", duration<double, std::milli>(t_init - t_start).count());

    // resolve CSS style
    log_debug("DEBUG: About to resolve style for elmt of name=%s", elmt->node_name());
    dom_node_resolve_style(elmt, lycon);
    log_debug("DEBUG: After resolve style");

    auto t_style = high_resolution_clock::now();
    log_info("[TIMING] layout: root style resolve: %.1fms", duration<double, std::milli>(t_style - t_init).count());

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ?
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    // Use OS/2 sTypo metrics only when USE_TYPO_METRICS flag is set (Chrome behavior)
    TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
    if (typo.valid && typo.use_typo_metrics) {
        lycon->block.init_ascender = typo.ascender;
        lycon->block.init_descender = typo.descender;
    } else if (lycon->font.font_handle) {
        const FontMetrics* m = font_get_metrics(lycon->font.font_handle);
        if (m) {
            lycon->block.init_ascender = m->hhea_ascender;
            lycon->block.init_descender = -(m->hhea_descender);
        }
    } else {
        // Fallback when no font face is available - use reasonable defaults
        log_error("No font face available for layout, using fallback metrics");
        lycon->block.init_ascender = 12.0;  // Default ascender
        lycon->block.init_descender = 3.0;  // Default descender
    }

    // navigate DomNode tree to find body
    DomNode* body_node = nullptr;
    log_debug("Searching for body element in Lambda CSS document");

    // CSS 2.1 §10.3.3: Apply root element margins to position and sizing.
    // The HTML root element should be offset by its margins and its width
    // reduced to viewport_width - margin_left - margin_right.
    if (html->bound && html->bound->margin.left != 0) {
        html->x = html->bound->margin.left;
    }
    if (html->bound && html->bound->margin.top != 0) {
        html->y = html->bound->margin.top;
        lycon->block.advance_y = html->bound->margin.top;
    }
    {
        float margin_h = 0;
        if (html->bound) margin_h = html->bound->margin.left + html->bound->margin.right;
        if (margin_h > 0) {
            float new_width = physical_width - margin_h;
            html->width = new_width;
            html->content_width = new_width;
            lycon->block.content_width = new_width;
            lycon->block.max_width = new_width;
            lycon->block.given_width = new_width;
            lycon->block.float_right_edge = new_width;
            line_init(lycon, 0, new_width);
            log_debug("[CSS] Root element margins: left=%.1f right=%.1f, width adjusted to %.1f",
                      html->bound->margin.left, html->bound->margin.right, new_width);
        }
    }

    // CSS 2.1 §10.3.3: Apply root element border and padding to reduce content area
    // Children are placed inside the border+padding box, not at the margin edge
    {
        float bp_left = 0, bp_right = 0, bp_top = 0;
        if (html->bound) {
            if (html->bound->border) {
                bp_left += html->bound->border->width.left;
                bp_right += html->bound->border->width.right;
                bp_top += html->bound->border->width.top;
            }
            bp_left += html->bound->padding.left;
            bp_right += html->bound->padding.right;
            bp_top += html->bound->padding.top;
        }
        float bp_h = bp_left + bp_right;
        if (bp_h > 0) {
            float new_cw = lycon->block.content_width - bp_h;
            if (new_cw < 0) new_cw = 0;
            lycon->block.content_width = new_cw;
            lycon->block.max_width = new_cw;
            lycon->block.given_width = new_cw;
            lycon->block.float_right_edge = new_cw;
            log_debug("[CSS] Root border+padding: reducing content_width by %.1f to %.1f", bp_h, new_cw);
        }
        if (bp_top > 0) {
            lycon->block.advance_y += bp_top;
            log_debug("[CSS] Root border+padding: advance_y offset by %.1f to %.1f", bp_top, lycon->block.advance_y);
        }
        line_init(lycon, bp_left, lycon->block.content_width + bp_left);
    }

    DomNode* child = nullptr;
    if (elmt->is_element()) {
        child = static_cast<DomElement*>(elmt)->first_child;
    }
    while (child) {
        if (child->is_element()) {
            const char* tag_name = child->node_name();
            log_debug("  Checking child element: %s", tag_name);
            if (strcmp(tag_name, "body") == 0) {
                body_node = child;
                log_debug("Found Lambda CSS body element");
                break;
            }
        }
        child = child->next_sibling;
    }

    auto t_body_find = high_resolution_clock::now();
    log_info("[TIMING] layout: body find: %.1fms", duration<double, std::milli>(t_body_find - t_style).count());

    if (body_node) {
        log_debug("Laying out body element: %p", (void*)body_node);
        // Resolve body's actual display value from CSS (may be flex, grid, etc.)
        DisplayValue body_display = resolve_display_value(body_node);
        log_debug("Body element display resolved: outer=%d, inner=%d (FLEX=%d)",
            body_display.outer, body_display.inner, CSS_VALUE_FLEX);
        layout_block(lycon, body_node, body_display);

        // After body layout, update html's advance_y from body's height
        // This is critical for scroll height calculation in iframes
        // Find the body view by iterating through html's children
        View* child = html->first_placed_child();
        ViewBlock* body_view = nullptr;
        while (child) {
            if (child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                // Check if this is the body element (tag() == HTM_TAG_BODY)
                if (vb->tag() == HTM_TAG_BODY) {
                    body_view = vb;
                    break;
                }
            }
            child = child->next();
        }

        if (body_view) {
            float body_total_height = body_view->height;
            if (body_view->bound) {
                body_total_height += body_view->bound->margin.top + body_view->bound->margin.bottom;
            }
            // Include html's border+padding top so finalize_block_flow gets the full content height
            float html_bp_top = 0;
            if (html->bound) {
                if (html->bound->border) html_bp_top += html->bound->border->width.top;
                html_bp_top += html->bound->padding.top;
            }
            lycon->block.advance_y = html_bp_top + body_total_height;
            log_debug("Body layout done: body->height=%.1f, total=%.1f, advance_y=%.1f",
                body_view->height, body_total_height, lycon->block.advance_y);
        } else {
            log_debug("Could not find body view in html children");
        }
    } else {
        log_debug("No body element found in DOM tree");
    }

    auto t_layout_block = high_resolution_clock::now();
    log_info("[TIMING] layout: layout_block: %.1fms", duration<double, std::milli>(t_layout_block - t_body_find).count());

    finalize_block_flow(lycon, html, CSS_VALUE_BLOCK);

    auto t_finalize = high_resolution_clock::now();
    log_info("[TIMING] layout: finalize_block_flow: %.1fms", duration<double, std::milli>(t_finalize - t_layout_block).count());
}

int detect_html_version_lambda_css(DomDocument* doc) {
    if (!doc) { return HTML5; } // Default fallback
    // Return the HTML version that was detected during document loading
    log_debug("Using pre-detected HTML version: %d", doc->html_version);
    return doc->html_version;
}

// Reset styles_resolved flag for all elements before layout pass
// This ensures CSS style resolution happens exactly once per element per layout
static void reset_styles_resolved_recursive(DomNode* node) {
    if (!node) return;

    if (node->is_element()) {
        DomElement* elem = node->as_element();
        elem->styles_resolved = false;

        // Recursively process children
        DomNode* child = elem->first_child;
        while (child) {
            reset_styles_resolved_recursive(child);
            child = child->next_sibling;
        }
    }
}

// Public function to reset all styles_resolved flags in the document
void reset_styles_resolved(DomDocument* doc) {
    if (!doc || !doc->root) return;
    log_debug("[CSS] Resetting styles_resolved flags for all elements");
    reset_styles_resolved_recursive(doc->root);
}

void layout_init(LayoutContext* lycon, DomDocument* doc, UiContext* uicon) {
    memset(lycon, 0, sizeof(LayoutContext));
    lycon->doc = doc;  lycon->ui_context = uicon;

    // Initialize run mode to full layout (default for layout_html_doc)
    // Measurement passes will override this to ComputeSize
    lycon->run_mode = radiant::RunMode::PerformLayout;
    lycon->sizing_mode = radiant::SizingMode::InherentSize;

    // Initialize viewport dimensions for layout (in CSS logical pixels)
    // Layout uses CSS logical pixels. Rendering will scale by pixel_ratio
    // to convert CSS pixels to physical surface pixels for HiDPI displays.
    lycon->width = uicon->viewport_width > 0 ? uicon->viewport_width : 1200;
    lycon->height = uicon->viewport_height > 0 ? uicon->viewport_height : 800;
    log_debug("layout_init: uicon=%p, viewport=%.1fx%.1f (CSS logical pixels), pixel_ratio=%.2f",
              uicon, lycon->width, lycon->height, uicon->pixel_ratio);

    // Initialize available space to indefinite (will be set properly during layout)
    lycon->available_space = AvailableSpace::make_indefinite();

    // Clear measurement cache at the start of each layout pass
    // This ensures fresh intrinsic size calculations for each layout
    clear_measurement_cache();

    // Reset styles_resolved flags for all elements before layout
    // This ensures CSS style resolution happens exactly once per element per layout pass
    reset_styles_resolved(doc);

    // Initialize text flow logging
    init_text_flow_logging();

    // Process @font-face rules before layout begins
    // This is a simplified implementation - in a full system, this would be done during CSS parsing
    if (doc) {
        // Detect HTML version based on document type
        doc->view_tree->html_version = (HtmlVersion)detect_html_version_lambda_css(doc);
        clog_info(font_log, "Lambda CSS document - detected HTML version: %d", doc->view_tree->html_version);
    } else {
        doc->view_tree->html_version = HTML5;
    }
    log_debug("Detected HTML version: %d", doc->view_tree->html_version);

    // setup default font
    FontProp* default_font = doc->view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    setup_font(uicon, &lycon->font, default_font);

    // Initialize CSS counter context for counter-reset/counter-increment/counter()/counters()
    lycon->counter_context = counter_context_create(doc->arena);
    log_debug("Initialized counter context");

    // BlockContext floats are already initialized to NULL in memset
    log_debug("DEBUG: Layout context initialized");
}

void layout_cleanup(LayoutContext* lycon) {
    // Clean up counter context
    if (lycon->counter_context) {
        counter_context_destroy(lycon->counter_context);
        lycon->counter_context = nullptr;
    }

    // BlockContext cleanup - floats are pool-allocated, no explicit cleanup needed
    (void)lycon;
}

void layout_html_doc(UiContext* uicon, DomDocument *doc, bool is_reflow) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    // Reset layout timing accumulators
    reset_layout_timing();

    LayoutContext lycon;
    if (!doc) return;
    log_debug("layout html doc - start");
    if (is_reflow) {
        // free existing view tree
        log_debug("free existing views");
        // if (doc->view_tree->root) free_view(doc->view_tree, doc->view_tree->root);
        // view_pool_destroy(doc->view_tree);
    } else {
        doc->view_tree = (ViewTree*)mem_calloc(1, sizeof(ViewTree), MEM_CAT_LAYOUT);
        log_debug("allocated view tree");
    }
    view_pool_init(doc->view_tree);
    log_debug("initialized view pool");
    log_debug("calling layout_init...");
    layout_init(&lycon, doc, uicon);
    log_debug("layout_init complete");

    auto t_init = high_resolution_clock::now();

    // Get root node based on document type
    DomNode* root_node = nullptr;
    root_node = doc->root;
    log_debug("DEBUG: Using root directly: %p", root_node);
    if (root_node) {
        // Validate pointer before calling virtual methods
        log_debug("DEBUG: root_node->node_type = %d", root_node->node_type);
        if (root_node->node_type >= DOM_NODE_ELEMENT && root_node->node_type <= DOM_NODE_DOCTYPE) {
            log_debug("layout lambda css html root %s", root_node->node_name());
        } else {
            log_error("Invalid node_type: %d (pointer may be corrupted)", root_node->node_type);
            return;
        }
    }

    if (!root_node) {
        log_error("Failed to get root_node");
        return;
    }

    log_debug("calling layout_html_root...");
    layout_html_root(&lycon, root_node);

    auto t_layout = high_resolution_clock::now();
    log_info("[TIMING] layout_html_root: %.1fms", duration<double, std::milli>(t_layout - t_init).count());

    log_debug("layout_html_root complete");

    log_debug("end layout");

    log_debug("calling layout_cleanup...");
    layout_cleanup(&lycon);
    log_debug("layout_cleanup complete");

    // Print view tree (existing functionality)
    log_debug("checking view tree: %p, root: %p", (void*)doc->view_tree,
              doc->view_tree ? (void*)doc->view_tree->root : NULL);
    if (doc->view_tree && doc->view_tree->root) {
        log_debug("DOM tree: html version %d", doc->view_tree->html_version);
        log_debug("calling print_view_tree...");
        print_view_tree((ViewElement*)doc->view_tree->root, doc->url);
        log_debug("print_view_tree complete");
    } else {
        log_debug("Warning: No view tree generated");
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] print_view_tree: %.1fms", duration<double, std::milli>(t_end - t_layout).count());
    log_layout_timing_summary();
    log_info("[TIMING] layout_html_doc total: %.1fms", duration<double, std::milli>(t_end - t_start).count());
    log_debug("layout_html_doc complete");
}
