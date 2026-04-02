#include "layout.hpp"
#include "layout_flex.hpp"
#include "layout_flex_measurement.hpp"
#include "layout_positioned.hpp"
#include "layout_cache.hpp"
#include "layout_counters.hpp"
#include "form_control.hpp"
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

    // CSS 2.1 §9.2.2.1: Whitespace between/around block-level elements is always
    // collapsed regardless of white-space property (inter-element whitespace rule).
    // Case 1: Whitespace at start of block, followed by a block-level element
    if (!text_node->prev_sibling && text_node->next_sibling) {
        if (is_block_level_element(text_node->next_sibling)) {
            return true;
        }
    }

    // Case 3: Whitespace between two elements where either neighbor is block-level
    if (text_node->prev_sibling && text_node->next_sibling) {
        bool prev_is_block = is_block_level_element(text_node->prev_sibling);
        bool next_is_block = is_block_level_element(text_node->next_sibling);

        if (prev_is_block || next_is_block) {
            return true;
        }
    }

    // Case 2: Whitespace at end of block (no next sibling)
    // CSS 2.1 §16.6.1: When white-space preserves spaces (pre, pre-wrap, break-spaces),
    // this whitespace is meaningful content — do NOT collapse it.
    if (!text_node->next_sibling) {
        if (text_node->parent->is_element()) {
            DomElement* parent_elem = text_node->parent->as_element();
            if (parent_elem->blk && parent_elem->blk->white_space != 0) {
                CssEnum ws = parent_elem->blk->white_space;
                if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
                    ws == CSS_VALUE_BREAK_SPACES) {
                    return false;
                }
            }
        }
        return true;
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

    log_debug("%s [RUN-IN] Merging <%s> into <%s>", run_in->source_loc(),
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

    log_debug("%s [RUN-IN] Merge complete, run-in now hidden", run_in->source_loc());
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
        log_debug("%s [RUN-IN] <%s> contains block child, becomes BLOCK", node->source_loc(),
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
    log_debug("%s [RUN-IN] <%s> not followed by block, becomes BLOCK", node->source_loc(),
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
            CssValue value = *parent->blk->line_height;
            // CSS 2.1 §10.8.1: <length> and <percentage> line-height values are
            // computed at the declaring element and inherited as the computed px
            // value. Only unitless <number> inherits the multiplier.
            // Font-relative units (em, ex, ch) must be resolved against the
            // declaring parent's font-size, not the inheriting child's.
            if (value.type == CSS_VALUE_TYPE_LENGTH) {
                CssUnit unit = value.data.length.unit;
                if (unit == CSS_UNIT_EM || unit == CSS_UNIT_EX || unit == CSS_UNIT_CH) {
                    float parent_fs = parent->font ? parent->font->font_size : 0;
                    if (parent_fs > 0) {
                        float multiplier = (float)value.data.length.value;
                        float computed_px;
                        if (unit == CSS_UNIT_EM) {
                            computed_px = multiplier * parent_fs;
                        } else if (unit == CSS_UNIT_EX) {
                            float x_ratio = font_get_x_height_ratio(lycon->font.font_handle);
                            computed_px = multiplier * parent_fs * x_ratio;
                        } else { // CSS_UNIT_CH
                            computed_px = multiplier * parent_fs * 0.5f;
                        }
                        log_debug("inherit line-height: resolved %.2f%s against parent font-size %.1f → %.2fpx",
                                  multiplier, unit == CSS_UNIT_EM ? "em" : unit == CSS_UNIT_EX ? "ex" : "ch",
                                  parent_fs, computed_px);
                        value.data.length.value = computed_px;
                        value.data.length.unit = CSS_UNIT_PX;
                    }
                }
            } else if (value.type == CSS_VALUE_TYPE_PERCENTAGE) {
                float parent_fs = parent->font ? parent->font->font_size : 0;
                if (parent_fs > 0) {
                    float computed_px = (float)(value.data.percentage.value * parent_fs / 100.0);
                    log_debug("inherit line-height: resolved %.1f%% against parent font-size %.1f → %.2fpx",
                              value.data.percentage.value, parent_fs, computed_px);
                    value.type = CSS_VALUE_TYPE_LENGTH;
                    value.data.length.value = computed_px;
                    value.data.length.unit = CSS_UNIT_PX;
                }
            }
            return value;
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
    } else { // no explicit value → inherit from parent (line-height is an inherited property)
        value = inherit_line_height(lycon, block);
    }
    if (value.type == CSS_VALUE_TYPE_KEYWORD && value.data.keyword == CSS_VALUE_NORMAL) {
        // 'normal' line height
        lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
        lycon->block.line_height_is_normal = true;
        log_debug("%s normal lineHeight: %f", block->source_loc(), lycon->block.line_height);
    } else {
        // Resolve var() if present
        const CssValue* resolved_value = resolve_var_function(lycon, &value);
        if (!resolved_value) {
            // var() couldn't be resolved, use normal
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
            log_debug("%s line-height var() unresolved, using normal: %f", block->source_loc(), lycon->block.line_height);
            return;
        }

        // resolve length/number/percentage
        float font_size_for_lh = lycon->font.current_font_size;
        // CSS 2.1 §10.8.1: Number line-height values are multiplied by the element's
        // own font-size. When current_font_size is unresolved (-1), use the computed
        // font-size from the font style (inherited from parent if not explicitly set).
        if (font_size_for_lh < 0 && lycon->font.style) {
            font_size_for_lh = lycon->font.style->font_size;
        }
        float resolved_height =
        resolved_value->type == CSS_VALUE_TYPE_NUMBER ?
            resolved_value->data.number.value * font_size_for_lh :
            resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, resolved_value);

        // CSS 2.1 §10.8.1: "Negative values are not allowed" for line-height
        // Zero is a valid computed value; only negative/NaN falls back to 'normal'
        if (resolved_height < 0 || std::isnan(resolved_height)) {
            log_debug("%s invalid line-height: %f, falling back to normal", block->source_loc(), resolved_height);
            lycon->block.line_height = calc_normal_line_height(lycon->font.font_handle);
            lycon->block.line_height_is_normal = true;
        } else {
            lycon->block.line_height = resolved_height;
            lycon->block.line_height_is_normal = false;
            log_debug("%s resolved line height: %f", block->source_loc(), lycon->block.line_height);
        }
    }
}

// DomNode style resolution function
// Ensures styles are resolved only once per layout pass using styles_resolved flag
// NOTE: Measurement mode (run_mode==ComputeSize) must NOT mark styles as resolved,
// because percentage-based values need to be re-resolved against the actual
// containing block dimensions during the real layout pass.
void dom_node_resolve_style(DomNode* node, LayoutContext* lycon) {
    auto t_start = high_resolution_clock::now();

    if (node && node->is_element()) {
        DomElement* dom_elem = node->as_element();

        if (dom_elem && dom_elem->specified_style) {
            // Check if styles already resolved in this layout pass
            // IMPORTANT: Skip this check during measurement mode (run_mode==ComputeSize)
            // because measurement passes should not permanently mark styles as resolved
            // and percentage values may need different containing block dimensions
            if (dom_elem->styles_resolved && !layout_context_is_measuring(lycon)) {
                log_debug("%s [CSS] Skipping style resolution for <%s> - already resolved", node->source_loc(),
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
                // Restore lycon->block dimensions from stored CSS values.
                // layout_block resets lycon->block.given_width/height to -1 before
                // calling us. When we skip resolution, these must be restored from
                // the block properties that were populated in the first pass.
                ViewBlock* block = (ViewBlock*)dom_elem;
                if (block->blk) {
                    if (block->blk->given_width >= 0) {
                        lycon->block.given_width = block->blk->given_width;
                    }
                    if (block->blk->given_height >= 0) {
                        lycon->block.given_height = block->blk->given_height;
                    }
                }
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
            if (layout_context_is_measuring(lycon)) {
                g_style_resolve_measure++;
            } else {
                g_style_resolve_full++;
            }

            // Lambda CSS: use the full implementation from resolve_css_style.cpp
            // Save the UA-assumed font-size before CSS cascade. For elements with
            // UA-default font-size (headings), this is their computed font-size.
            // For others (<p>, <ul>, etc.), they inherit the parent's font-size.
            float ua_font_size = (dom_elem->font && dom_elem->font->font_size > 0)
                                 ? dom_elem->font->font_size
                                 : lycon->font.style->font_size;

            resolve_css_styles(dom_elem, lycon);

            // CSS 2.1 §15.2: When CSS changes an element's font-size, UA-default
            // margins specified in 'em' units (specificity -1) must be re-resolved
            // using the element's computed font-size, not the inherited parent font-size.
            // Only applies to elements whose UA stylesheet uses em-based margins:
            // <p>, <ul>, <ol>, <h1>-<h6>, <pre>, <blockquote>, <dl>, <fieldset>
            // NOT <body> (which has absolute 8px margins).
            if (dom_elem->bound && ua_font_size > 0 && dom_elem->is_element()) {
                uintptr_t tag = dom_elem->tag();
                bool has_em_margins = (tag == HTM_TAG_P || tag == HTM_TAG_UL || tag == HTM_TAG_OL ||
                    tag == HTM_TAG_PRE || tag == HTM_TAG_BLOCKQUOTE || tag == HTM_TAG_DL ||
                    (tag >= HTM_TAG_H1 && tag <= HTM_TAG_H6));
                if (has_em_margins) {
                    ViewSpan* span = (ViewSpan*)dom_elem;
                    float css_font_size = (span->font && span->font->font_size > 0)
                                          ? span->font->font_size
                                          : lycon->font.style->font_size;
                    if (css_font_size != ua_font_size) {
                        float ratio = css_font_size / ua_font_size;
                        if (dom_elem->bound->margin.top_specificity == -1) {
                            dom_elem->bound->margin.top *= ratio;
                        }
                        if (dom_elem->bound->margin.bottom_specificity == -1) {
                            dom_elem->bound->margin.bottom *= ratio;
                        }
                    }
                }
            }

            // Mark as resolved for this layout pass
            // Don't mark as resolved during measurement mode - let the actual layout pass do that
            if (!layout_context_is_measuring(lycon)) {
                dom_elem->styles_resolved = true;
                log_debug("%s [CSS] Resolved styles for <%s> - marked as resolved", node->source_loc(),
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            } else {
                log_debug("%s [CSS] Resolved styles for <%s> in measurement mode - not marking resolved", node->source_loc(),
                    dom_elem->tag_name ? dom_elem->tag_name : "unknown");
            }
        } else {
            // No specified_style - still apply element default styles for HTML attributes
            apply_element_default_style(lycon, dom_elem);

            // CSS 2.1: Elements without specified styles still have computed values
            // from inheritance. Propagate font from the current layout context so
            // computed style queries return the correct inherited font properties.
            if (!dom_elem->font && lycon->font.style) {
                Pool* pool = lycon->doc ? lycon->doc->view_tree->pool : nullptr;
                if (pool) {
                    dom_elem->font = (FontProp*)pool_calloc(pool, sizeof(FontProp));
                    if (dom_elem->font) {
                        dom_elem->font->family = lycon->font.style->family;
                        dom_elem->font->font_size = lycon->font.style->font_size;
                        dom_elem->font->font_style = lycon->font.style->font_style;
                        dom_elem->font->font_weight = lycon->font.style->font_weight;
                        dom_elem->font->font_variant = lycon->font.style->font_variant;
                        dom_elem->font->text_deco = lycon->font.style->text_deco;
                        dom_elem->font->letter_spacing = lycon->font.style->letter_spacing;
                        dom_elem->font->word_spacing = lycon->font.style->word_spacing;
                    }
                }
            }
        }
    }

    auto t_end = high_resolution_clock::now();
    g_style_resolve_time += duration<double, std::milli>(t_end - t_start).count();
    g_style_resolve_count++;
}

float calculate_vertical_align_offset(LayoutContext* lycon, CssEnum align, float item_height, float line_height, float baseline_pos, float item_baseline, float valign_offset) {
    log_debug("calculate vertical align: align=%d, item_height=%f, line_height=%f, baseline_pos=%f, item_baseline=%f, offset=%f",
        align, item_height, line_height, baseline_pos, item_baseline, valign_offset);
    // CSS 2.1 §10.8.1: text-top/text-bottom/middle/super/sub reference the PARENT element's
    // font metrics, not the block container's. parent_font_* is set by span_vertical_align
    // before recursing into children; defaults to block init values for top-level content.
    float pa_asc = lycon->line.parent_font_ascender;
    float pa_desc = lycon->line.parent_font_descender;
    float pa_fsize = lycon->line.parent_font_size;
    switch (align) {
    case CSS_VALUE_BASELINE:
        // For length/percentage vertical-align, offset shifts baseline (positive = raise = lower y)
        return baseline_pos - item_baseline - valign_offset;
    case CSS_VALUE_TOP:
        return 0;
    case CSS_VALUE_MIDDLE: {
        // CSS 2.1 §10.8.1: "Align the vertical midpoint of the box with the baseline
        // of the parent box plus half the x-height of the parent."
        // Use actual x-height from font metrics when available.
        float x_height_half;
        if (lycon->line.parent_font_handle) {
            float x_ratio = font_get_x_height_ratio(lycon->line.parent_font_handle);
            x_height_half = pa_fsize * x_ratio / 2.0f;
        } else {
            x_height_half = pa_fsize * 0.25f; // fallback: ~0.5em x-height
        }
        // midpoint of box = baseline - x_height/2 → offset = baseline - x_height/2 - item_height/2
        return baseline_pos - x_height_half - item_height / 2.0f;
    }
    case CSS_VALUE_BOTTOM:
        log_debug("bottom-aligned-text: line %d", line_height);
        return line_height - item_height;
    case CSS_VALUE_TEXT_TOP:
        // CSS 2.1 §10.8.1: "Align the top of the box with the top of the parent's content area."
        // The parent's content top is at (baseline_pos - parent_ascender).
        return baseline_pos - pa_asc;
    case CSS_VALUE_TEXT_BOTTOM:
        // CSS 2.1 §10.8.1: "Align the bottom of the box with the bottom of the parent's content area."
        // The parent's content bottom is at (baseline_pos + parent_descender).
        return baseline_pos + pa_desc - item_height;
    case CSS_VALUE_SUB:
        // CSS 2.1 §10.8.1: "Lower the baseline of the box" by UA amount.
        // Use ~0.3em of the parent's font size.
        return baseline_pos - item_baseline + 0.3f * pa_fsize;
    case CSS_VALUE_SUPER:
        // CSS 2.1 §10.8.1: "Raise the baseline of the box" by UA amount.
        // Use ~0.3em of the parent's font size.
        return baseline_pos - item_baseline - 0.3f * pa_fsize;
    default:
        return baseline_pos - item_baseline; // Default to baseline
    }
}

void span_vertical_align(LayoutContext* lycon, ViewSpan* span) {
    FontBox pa_font = lycon->font;  CssEnum pa_line_align = lycon->line.vertical_align;
    float pa_valign_offset = lycon->line.vertical_align_offset;
    // Save current parent font metrics to restore after processing children
    float saved_pa_asc = lycon->line.parent_font_ascender;
    float saved_pa_desc = lycon->line.parent_font_descender;
    float saved_pa_fsize = lycon->line.parent_font_size;
    struct FontHandle* saved_pa_handle = lycon->line.parent_font_handle;
    log_debug("span_vertical_align");
    View* child = span->first_child;
    if (child) {
        // CSS 2.1 §10.8.1: Before updating to the span's own font, capture current font
        // as the "parent font" for vertical-align keywords in children.
        // lycon->font is the parent (caller) font at this point.
        if (lycon->font.style) {
            lycon->line.parent_font_ascender = lycon->font.style->ascender;
            lycon->line.parent_font_descender = lycon->font.style->descender;
            lycon->line.parent_font_size = lycon->font.style->font_size;
            lycon->line.parent_font_handle = lycon->font.font_handle;
        }
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
    lycon->line.parent_font_ascender = saved_pa_asc;
    lycon->line.parent_font_descender = saved_pa_desc;
    lycon->line.parent_font_size = saved_pa_fsize;
    lycon->line.parent_font_handle = saved_pa_handle;
}

// apply vertical alignment to a view
void view_vertical_align(LayoutContext* lycon, View* view) {
    log_debug("view_vertical_align: view=%d", view->view_type);
    float line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
    // CSS 2.1 §10.8.1: The strut is an invisible zero-width inline box with the
    // block element's font and line-height. Its half-leading-adjusted ascender
    // defines the minimum baseline position for the line. When only a smaller
    // inline font is present, max_ascender may be less than the strut's baseline,
    // but the baseline must still be at the strut's position.
    float strut_baseline = lycon->block.init_ascender + lycon->block.lead_y;
    float baseline_pos = max(lycon->line.max_ascender, strut_baseline);
    // CSS 2.1 §10.8.1: text-top/text-bottom/middle/super/sub use parent element's font.
    // parent_font_* fields in lycon->line are set by span_vertical_align before recursing.
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text_view = (ViewText*)view;
        TextRect* rect = text_view->rect;
        while (rect) {
            float item_height = rect->height;
            // for text, baseline is at font.ascender
            log_debug("text view font: %p", text_view->font);
            float item_baseline = text_view->font ? text_view->font->ascender : item_height;
            float vertical_offset = calculate_vertical_align_offset(lycon, lycon->line.vertical_align, item_height,
                line_height, baseline_pos, item_baseline, lycon->line.vertical_align_offset);
            const unsigned char* td = text_view->text_data();
            log_debug("vertical-adjusted-text: y=%d, adv=%d, offset=%f, line=%f, hg=%f, txt='%.*t'",
                (int)rect->y, (int)lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height,
                rect->length, td ? td + rect->start_index : (const unsigned char*)"(null)");
            // CSS 2.1 §10.8.1: Content area may overflow the line box when
            // line-height < content-height (negative half-leading). Allow negative
            // offsets so the content area extends above the line box top.
            rect->y = lycon->block.advance_y + vertical_offset;
            rect = rect->next;
        }
        adjust_text_bounds(text_view);
    }
    else if (view->view_type == RDT_VIEW_INLINE_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        float item_height = block->height + (block->bound ?
            block->bound->margin.top + block->bound->margin.bottom : 0);
        // CSS 2.1 §10.8.1: For inline-blocks, the baseline depends on content:
        // - Replaced elements / overflow != visible with no content: bottom margin edge
        // - Non-replaced with overflow:visible and in-flow line boxes: last line baseline
        // - Form text controls (input, select): content text baseline
        float item_baseline = item_height; // default: bottom margin edge
        if (block->blk && block->blk->last_line_max_ascender > 0) {
            bool is_replaced_elem = (block->tag() == HTM_TAG_IMG || block->tag() == HTM_TAG_IFRAME ||
                block->tag() == HTM_TAG_VIDEO || block->tag() == HTM_TAG_EMBED ||
                (block->tag() == HTM_TAG_OBJECT && block->get_attribute("data")) ||
                block->tag() == HTM_TAG_TEXTAREA ||
                block->tag() == HTM_TAG_SELECT);
            bool overflow_visible = !block->scroller ||
                (block->scroller->overflow_x == CSS_VALUE_VISIBLE &&
                 block->scroller->overflow_y == CSS_VALUE_VISIBLE);
            bool is_form_text_ctl = (block->item_prop_type == DomElement::ITEM_PROP_FORM &&
                block->form && block->form->control_type != FORM_CONTROL_HIDDEN &&
                block->form->control_type != FORM_CONTROL_IMAGE &&
                block->form->control_type != FORM_CONTROL_SELECT);
            if (!is_replaced_elem && (overflow_visible || is_form_text_ctl)) {
                item_baseline = (block->bound ? block->bound->margin.top : 0) +
                    block->blk->last_line_max_ascender;
            }
        }
        CssEnum align = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align : lycon->line.vertical_align;
        float valign_offset = block->in_line && block->in_line->vertical_align ?
            block->in_line->vertical_align_offset : lycon->line.vertical_align_offset;
        // Ensure max_ascender accommodates raised inline-blocks before computing offset
        if (align == CSS_VALUE_BASELINE && valign_offset != 0) {
            float asc_contribution = item_baseline + valign_offset;
            lycon->line.max_ascender = max(lycon->line.max_ascender, asc_contribution);
            // Recompute line_height with updated max_ascender
            line_height = max(lycon->block.line_height, lycon->line.max_ascender + lycon->line.max_descender);
        }
        float vertical_offset = calculate_vertical_align_offset(lycon, align, item_height,
            line_height, baseline_pos, item_baseline, valign_offset);
        block->y = lycon->block.advance_y + max(vertical_offset, 0) + (block->bound ? block->bound->margin.top : 0);
        log_debug("vertical-adjusted-inline-block: y=%f, adv_y=%f, offset=%f, line=%f, blk=%f, max_asc=%f, max_desc=%f",
            block->y, lycon->block.advance_y, vertical_offset, lycon->block.line_height, item_height, lycon->line.max_ascender, lycon->line.max_descender);
    }
    else if (view->view_type == RDT_VIEW_INLINE) {
        // for inline elements, apply to all children
        ViewSpan* span = (ViewSpan*)view;
        span_vertical_align(lycon, span);
        // CSS 2.1 §10.8.1: After vertical alignment adjusts children's positions,
        // recompute the span's bounding box. The bounding box was computed earlier
        // in layout_inline() before vertical alignment, so child y positions may
        // have shifted (e.g., baseline alignment with half-leading offsets).
        // Always recompute to ensure the span's bounds reflect final child positions.
        struct FontHandle* span_fh = span->font ? span->font->font_handle : lycon->font.font_handle;
        compute_span_bounding_box(span, false, span_fh);
        // CSS 2.1 §10.8.1: Empty/collapsed inline spans (no visible children)
        // get height=0 from compute_span_bounding_box, but should report their
        // line-height height when on a line with visible content. content_height
        // was set as a marker during layout_inline()/line_break().
        if (span->height == 0 && span->content_height > 0) {
            span->height = (int)span->content_height;
        }
        float span_asc = 0, span_desc = 0;
        if (span->font) {
            span_asc = span->font->ascender;
            span_desc = span->font->descender;
        } else if (lycon->font.style) {
            span_asc = lycon->font.style->ascender;
            span_desc = lycon->font.style->descender;
        }
        if (span->content_height > 0 && span_fh && (span_asc > 0 || span_desc > 0)) {
            float content_area = font_get_cell_height(span_fh);
            float bt = 0, bb = 0, pt = 0, pb = 0;
            if (span->bound) {
                if (span->bound->border) {
                    bt = span->bound->border->width.top;
                    bb = span->bound->border->width.bottom;
                }
                pt = span->bound->padding.top > 0 ? span->bound->padding.top : 0;
                pb = span->bound->padding.bottom > 0 ? span->bound->padding.bottom : 0;
            }
            int expected_height = (int)(content_area + bt + pt + pb + bb);
            // Check if any child inline span overflows the expected height
            bool child_overflows = false;
            if (span->height > expected_height) {
                View* ch = span->first_placed_child();
                while (ch) {
                    if (ch->view_type == RDT_VIEW_INLINE && ch->height > expected_height) {
                        child_overflows = true;
                        break;
                    }
                    ch = (View*)ch->next_sibling;
                }
            }
            if (span->height > expected_height && !child_overflows) {
                // Children extend beyond the font content area (e.g., tall image).
                // Override both Y and height. Y is computed from the baseline position
                // using the half-leading model.
                float span_lh = span->content_height;
                float hhea_content = span_asc + span_desc;
                float half_leading = (span_lh - hhea_content) / 2.0f;
                float item_baseline = span_asc + half_leading;

                CssEnum align = (span->in_line && span->in_line->vertical_align) ?
                    span->in_line->vertical_align : lycon->line.vertical_align;
                float valign_offset = (span->in_line && span->in_line->vertical_align) ?
                    span->in_line->vertical_align_offset : lycon->line.vertical_align_offset;
                float line_height = max(lycon->block.line_height,
                    lycon->line.max_ascender + lycon->line.max_descender);
                float vertical_offset = calculate_vertical_align_offset(lycon, align,
                    span_lh, line_height, baseline_pos, item_baseline, valign_offset);

                // Position the content area top: inline_box_top + half_leading
                // The baseline is at inline_box_top + item_baseline
                // Content area top = baseline - ascender_for_content_area
                // For matching browser: content_area_top ≈ inline_box_top + half_leading
                // Since cell_height may differ from hhea, adjust by centering difference
                span->y = (int)(lycon->block.advance_y + vertical_offset + half_leading - bt - pt);
                span->height = expected_height;
                log_debug("inline box override (tall child): y=%d, h=%d, area=%.1f",
                         span->y, span->height, content_area);
            }
            // Else: text-only content — compute_span_bounding_box result is correct
        }
    }
    else {
        log_debug("view_vertical_align: unknown view type %d", view->view_type);
    }
}

// CSS 2.1 §16.2: Shift current-line text rects inside a span that was laid out
// on a previous line but has continuation content on the current line.
static void shift_span_current_line_rects(float offset, float line_y, ViewSpan* span) {
    View* child = (View*)span->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            TextRect* rect = text->rect;
            while (rect) {
                if (rect->y >= line_y - 1.0f) {
                    rect->x += offset;
                }
                rect = rect->next;
            }
        } else if (child->view_type == RDT_VIEW_INLINE) {
            shift_span_current_line_rects(offset, line_y, (ViewSpan*)child);
        }
        child = child->next();
    }
    if (span->y >= line_y - 1.0f) {
        span->x += offset;
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

    // Extend the last text rect to fill any remaining space due to rounding errors.
    // Only do this if justification actually distributed space (cumulative_offset > 0),
    // otherwise a single-word line would be incorrectly stretched to fill the line.
    if (cumulative_offset > 0 && last_rect && last_view && last_view->view_type == RDT_VIEW_TEXT) {
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
    // Convert logical values (start/end) to physical values (left/right)
    // CSS 2.1 §16.2: 'start' maps to 'left' for LTR and 'right' for RTL
    bool is_rtl = lycon->block.direction == CSS_VALUE_RTL;
    CssEnum text_align = lycon->block.text_align;

    // CSS Text 3 §7.2: text-align-last overrides text-align on the last line
    // or the line immediately before a forced line break.
    // (when text_align_last is not 'auto' and not unset)
    bool text_align_last_applied = false;
    if (lycon->line.is_last_line && lycon->block.text_align_last != 0 &&
        lycon->block.text_align_last != CSS_VALUE_AUTO) {
        text_align = lycon->block.text_align_last;
        text_align_last_applied = true;
    }

    // CSS Text 3 §7.1: justify-all computes to justify but eliminates the
    // special last-line behavior. All lines are justified including the last.
    if (text_align == CSS_VALUE_JUSTIFY_ALL) {
        text_align = CSS_VALUE_JUSTIFY;
        text_align_last_applied = true;  // prevent last-line skip
    }

    if (text_align == CSS_VALUE_START) {
        text_align = is_rtl ? CSS_VALUE_RIGHT : CSS_VALUE_LEFT;
    } else if (text_align == CSS_VALUE_END) {
        text_align = is_rtl ? CSS_VALUE_LEFT : CSS_VALUE_RIGHT;
    }

    if (text_align != CSS_VALUE_LEFT) {
        // Compute line metrics early — needed for shrink-to-fit overflow check
        float line_width = lycon->line.advance_x - lycon->line.left;
        // CSS 2.1 §16.1: RTL text-indent narrows the available width for alignment
        float available_width = lycon->block.content_width - lycon->line.text_indent_offset;


        // Skip centering/right alignment only when laying out content INSIDE an inline-block
        // with shrink-to-fit width AND content fits. When content overflows (e.g., due to
        // max-width constraint), alignment still matters for overflow direction.
        //
        // Important: Check the CONTAINER (establishing_element), not the current view.
        // We want to center inline-blocks ON a line, just not content INSIDE a shrink-to-fit inline-block.
        ViewBlock* container = lycon->block.establishing_element;
        bool container_is_shrink_inline_block = container &&
            container->view_type == RDT_VIEW_INLINE_BLOCK &&
            lycon->block.given_width < 0;
        if (container_is_shrink_inline_block && line_width <= available_width &&
            (text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT)) {
            log_debug("line_align: skipping center/right align for content inside shrink-to-fit inline-block");
            return;
        }

        View* view = lycon->line.start_view;

        // Special handling for wrapped text continuation lines:
        // When text wraps within the same text node, start_view may point to a text view
        // whose first rects belong to previous lines. Detect this by checking if the text
        // node has multiple rects at different y positions AND the first rect's y is well
        // below the current advance_y.
        // Note: A negative first_rect->y (due to font ascender above the baseline) does NOT
        // indicate wrapping — it's normal for the first line where advance_y starts at 0.
        bool is_wrapped_continuation = false;
        if (view && view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            TextRect* first_rect = text->rect;
            if (first_rect && first_rect->y < lycon->block.advance_y - 1.0f) {
                // Only consider this a wrapped continuation if the text actually wraps
                // (has multiple rects at different y positions). A single rect or rects
                // all at the same y cannot be a continuation of a previous line.
                TextRect* r = first_rect->next;
                while (r) {
                    if (r->y > first_rect->y + 1.0f) {
                        is_wrapped_continuation = true;
                        break;
                    }
                    r = r->next;
                }
            }
        }
        // Fallback: start_view is NULL but current view has wrapped text
        if (!is_wrapped_continuation && !view && lycon->view &&
            lycon->view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)lycon->view;
            TextRect* rect = text->rect;
            int rect_count = 0;
            while (rect && rect_count < 2) {
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

        float offset = 0;

        if (text_align == CSS_VALUE_CENTER) {
            offset = (available_width - line_width) / 2;
        }
        else if (text_align == CSS_VALUE_RIGHT) {
            offset = available_width - line_width;
        }
        // For center/right alignment
        // CSS 2.1 §16.2 + CSS3 Text §7.1 overflow alignment:
        // When content overflows (offset < 0), fall back to start alignment.
        // For RTL: start = right, so negative offset correctly right-aligns text (overflows left).
        // For LTR: start = left, so negative offset is clamped to 0 (text stays at left edge).
        if ((text_align == CSS_VALUE_CENTER || text_align == CSS_VALUE_RIGHT) &&
            (offset > 0 || (is_rtl && offset < 0))) {
            // For wrapped text continuation lines, only align current-line rects
            if (is_wrapped_continuation) {
                ViewText* text = (ViewText*)view;
                float line_y = lycon->block.advance_y;
                TextRect* rect = text->rect;
                while (rect) {
                    if (rect->y >= line_y - 1.0f) {
                        rect->x += offset;
                    }
                    rect = rect->next;
                }
                // Also shift any sibling views that follow on the current line
                View* next = view->next();
                if (next) {
                    view_line_align(lycon, offset, next);
                }
            } else {
                // CSS 2.1 §16.2: Before aligning from start_view, check preceding siblings
                // for text continuation rects on the current line. This happens when a text
                // node wraps and a sibling inline element (e.g., <span>) follows on the same
                // line — start_view points to the span, but the wrapped text rect precedes it.
                float line_y = lycon->block.advance_y;
                View* prev = (View*)((DomNode*)view)->prev_sibling;
                while (prev) {
                    if (prev->view_type == RDT_VIEW_TEXT) {
                        ViewText* text = (ViewText*)prev;
                        TextRect* rect = text->rect;
                        while (rect) {
                            if (rect->y >= line_y - 1.0f) {
                                rect->x += offset;
                            }
                            rect = rect->next;
                        }
                    } else if (prev->view_type == RDT_VIEW_INLINE) {
                        shift_span_current_line_rects(offset, line_y, (ViewSpan*)prev);
                    }
                    prev = (View*)((DomNode*)prev)->prev_sibling;
                }
                // Normal case: align all views in the line
                view_line_align(lycon, offset, view);
            }
            return;
        }

        if (text_align == CSS_VALUE_JUSTIFY) {
                // CSS 2.1 §16.2: "If 'text-align' is set to 'justify', the UA adjusts spacing
                // in inline boxes to fit each line... except for the last line of the block."
                // CSS Text 3 §7.2: But text-align-last: justify explicitly requests justify
                // on the last line / line before forced break, so do NOT skip in that case.
                if (lycon->line.is_last_line && !text_align_last_applied) {
                    // Last line: fall back to start alignment (left for LTR, right for RTL)
                    if (is_rtl) {
                        float offset = available_width - line_width;
                        if (offset > 0) {
                            view_line_align(lycon, offset, view);
                        }
                    }
                    return;
                }

                // Not the last line: distribute extra space across word gaps
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
                        const char* text_data = (const char*)text->text_data();
                        // Count spaces in this specific rect
                        int num_spaces = 0;
                        if (text_data) {
                            const char* str = text_data + last_rect->start_index;
                            for (int i = 0; i < last_rect->length; i++) {
                                if (str[i] == ' ') num_spaces++;
                            }
                        }

                        float extra_width = available_width - line_width;

                        if (num_spaces > 0 && extra_width > 0) {
                            // Expand the width of this specific TextRect to fill the line
                            last_rect->width += extra_width;
                            return;
                        }
                    }
                }
                else {
                    // Multi-view line: distribute space across all text views
                    int num_spaces = count_spaces_in_view(view);
                    float extra_width = available_width - line_width;
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
    log_debug("layout node %s, advance_y: %f", node->source_loc(), lycon->block.advance_y);

    // Log for IMG elements
    uintptr_t node_tag = node->tag();
    if (node_tag == HTM_TAG_IMG) {
        log_debug("%s [FLOW_NODE IMG] Processing IMG element: %s", node->source_loc(), node->node_name());
    }

    // Skip HTML comments (Lambda CSS parser creates these as elements with name "!--")
    const char* node_name = node->node_name();
    if (node_name && (strcmp(node_name, "!--") == 0 || strcmp(node_name, "#comment") == 0)) {
        log_debug("%s skipping HTML comment node", node->source_loc());
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
                    // Use CSS computed line-height for marker height (not raw font metrics)
                    marker_span->height = lycon->block.line_height;

                    if (marker_prop->is_outside) {
                        // Outside marker: position to the left of content area
                        // CSS Lists 3 §4: formatted as a zero-width inline box on the first line
                        // Contributes to line height but does NOT advance inline position
                        marker_span->x = lycon->line.advance_x - marker_prop->width;
                        marker_span->y = lycon->block.advance_y;
                    } else {
                        // Inside marker: position at current inline position and advance
                        marker_span->x = lycon->line.advance_x;
                        marker_span->y = lycon->block.advance_y;
                        lycon->line.advance_x += marker_prop->width;
                    }

                    if (!marker_prop->is_outside) {
                        // Inside markers contribute to line height and mark the line as non-empty
                        // Apply half-leading model same as inline text (CSS 2.1 §10.8.1)
                        float ascender = 0, descender = 0;
                        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
                            font_get_normal_lh_split(lycon->font.font_handle, &ascender, &descender);
                        } else {
                            TypoMetrics typo = get_os2_typo_metrics(lycon->font.font_handle);
                            if (typo.valid && typo.use_typo_metrics) {
                                ascender = typo.ascender;
                                descender = typo.descender;
                            } else if (lycon->font.font_handle) {
                                ascender = font_get_metrics(lycon->font.font_handle)->hhea_ascender;
                                descender = -(font_get_metrics(lycon->font.font_handle)->hhea_descender);
                            } else {
                                ascender = 12.0f; descender = 4.0f;
                            }
                            float content_height = ascender + descender;
                            float half_leading = (lycon->block.line_height - content_height) / 2.0f;
                            ascender += half_leading;
                            descender += half_leading;
                        }
                        if (ascender > lycon->line.max_ascender) lycon->line.max_ascender = ascender;
                        if (descender > lycon->line.max_descender) lycon->line.max_descender = descender;

                        // Mark line as non-empty so line_break() accounts for marker height
                        if (!lycon->line.start_view) lycon->line.start_view = (View*)marker_span;
                        lycon->line.is_line_start = false;

                        // Track normal line-height for platform metrics
                        if (lycon->block.line_height_is_normal && lycon->font.font_handle) {
                            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
                            lycon->line.max_normal_line_height = max(lycon->line.max_normal_line_height, normal_lh);
                        }
                    }
                    // Outside markers: positioned outside content area, don't create a line box
                    // They are placed at advance_y and will align with the first content line

                    log_debug("%s [MARKER] Laid out %s marker width=%.1f, height=%.1f at (%.1f, %.1f)", node->source_loc(),
                             marker_prop->is_outside ? "outside" : "inside",
                             marker_prop->width, marker_span->height, marker_span->x, marker_span->y);
                }
            }
            return;
        }

        // Skip floats that were pre-laid in the float pre-pass
        if (elem->float_prelaid) {
            log_debug("%s skipping pre-laid float: %s", node->source_loc(), node->node_name());
            return;
        }

        // Use resolve_display_value which handles both Lexbor and Lambda CSS nodes
        DisplayValue display = resolve_display_value(node);
        log_debug("processing element: %s, with display: outer=%d, inner=%d", node->source_loc(), display.outer, display.inner);

        // Log IMG display resolution
        if (node_tag == HTM_TAG_IMG) {
            log_debug("%s [FLOW_NODE IMG] Resolved display for IMG: outer=%d, inner=%d (INLINE_BLOCK=%d, INLINE=%d)", node->source_loc(),
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
                log_debug("%s Float on %s: transforming display from outer=%d to BLOCK (float=%d)", node->source_loc(),
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
            // Save pre-blockification display on the element for static position calculation
            // layout_block uses this to determine if a line break is needed
            elem->display = display;
            // Absolutely positioned elements become block-level
            if (display.outer == CSS_VALUE_INLINE || display.outer == CSS_VALUE_RUN_IN) {
                log_debug("%s Position absolute/fixed on %s: transforming display from outer=%d to BLOCK", node->source_loc(),
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
                log_debug("%s run-in merged into following block, skipping", node->source_loc());
                return;
            }
            // Run-in becomes block
            display = resolved;
        }

        // CSS 2.1 §17.2.1: table-column and table-column-group elements do not
        // generate boxes. They only serve to define column properties for table layout.
        // When orphaned (outside a table context), they should not be rendered.
        if (display.inner == CSS_VALUE_TABLE_COLUMN || display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) {
            log_debug("%s skipping table-column/table-column-group element (no visual rendering)", node->source_loc());
            return;
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
            log_debug("%s skipping element of display: none", node->source_loc());
            break;
        case CSS_VALUE_CONTENTS: {
            // CSS Display Level 3: display: contents
            // Element does not generate a box, but its children are laid out
            // as if they were children of the element's parent.
            // Counter properties do NOT apply (no box generated).
            log_debug("%s display:contents for <%s> - no box, layout children directly", node->source_loc(), node->node_name());

            // Mark element in the view tree with zero dimensions (no box generated)
            // Don't use set_view() — avoid affecting line start view
            elem->view_type = RDT_VIEW_INLINE;
            elem->display.outer = CSS_VALUE_CONTENTS;
            elem->display.inner = CSS_VALUE_CONTENTS;
            elem->x = 0;
            elem->y = 0;
            elem->width = 0;
            elem->height = 0;

            // Set lycon->view for style resolution (resolve_css_styles uses it)
            View* saved_view = lycon->view;
            lycon->view = (View*)elem;

            // Resolve CSS styles so counter properties are populated on elem->blk
            dom_node_resolve_style(node, lycon);

            // Restore view context
            lycon->view = saved_view;

            // CSS Lists 3: "An element that does not generate a box... also does not
            // increment, set, or reset any counters." display:contents does not
            // generate a box, so skip all counter operations.

            // Layout children directly in parent's formatting context
            for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            break;
        }
        default:
            log_debug("%s unknown display type: outer=%d", node->source_loc(), display.outer);
            // skip the element
        }
    }
    else if (node->is_text()) {
        const unsigned char* str = node->text_data();
        log_debug("%s layout_text: '%t'", node->source_loc(), str);
        // Skip inter-element whitespace (whitespace between/around block elements)
        // CSS 2.2: "When white space is contained at the end of a block's content,
        // or at the start, or between block-level elements, it is rendered as nothing."
        if (should_collapse_inter_element_whitespace(node)) {
            node->view_type = RDT_VIEW_NONE;
            log_debug("%s skipping inter-element whitespace text", node->source_loc());
        }
        else {
            layout_text(lycon, node);
        }
    }
    else {
        log_debug("%s layout unknown node type: %d", node->source_loc(), node->node_type);
        // skip the node
    }
    log_debug("%s end flow node, block advance_y: %.0f", node->source_loc(), lycon->block.advance_y);
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
    log_info("%s [TIMING] layout: context init: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_init - t_start).count());

    // resolve CSS style
    log_debug("DEBUG: About to resolve style for elmt of name=%s", elmt->source_loc());
    dom_node_resolve_style(elmt, lycon);
    log_debug("DEBUG: After resolve style");

    auto t_style = high_resolution_clock::now();
    log_info("%s [TIMING] layout: root style resolve: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_style - t_init).count());

    if (html->font) {
        setup_font(lycon->ui_context, &lycon->font, html->font);
    }
    if (lycon->root_font_size < 0) {
        lycon->root_font_size = lycon->font.current_font_size < 0 ?
            lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
    }
    // Use platform-aware split for normal line-height to match browser behavior
    // font_get_normal_lh_split handles USE_TYPO_METRICS, CoreText, and hhea fallback
    if (lycon->font.font_handle) {
        float split_asc = 0, split_desc = 0;
        font_get_normal_lh_split(lycon->font.font_handle, &split_asc, &split_desc);
        lycon->block.init_ascender = split_asc;
        lycon->block.init_descender = split_desc;
    } else {
        // Fallback when no font face is available - use reasonable defaults
        log_error("No font face available for layout, using fallback metrics");
        lycon->block.init_ascender = 12.0;  // Default ascender
        lycon->block.init_descender = 3.0;  // Default descender
    }

    // navigate DomNode tree to find body
    DomNode* body_node = nullptr;
    log_debug("Searching for body element in Lambda CSS document");

    // CSS 2.1 §10.3, §9.3: Root element explicit sizing and positioning.
    // Detect if the root <html> element has explicit CSS width/height
    // or non-static positioning, and apply accordingly.
    bool root_is_abspos = html->position &&
        (html->position->position == CSS_VALUE_ABSOLUTE || html->position->position == CSS_VALUE_FIXED);
    bool root_is_relative = html->position && (html->position->position == CSS_VALUE_RELATIVE ||
                                                    html->position->position == CSS_VALUE_STICKY);

    // Compute border+padding dimensions for the root element
    float root_bp_left = 0, root_bp_right = 0, root_bp_top = 0, root_bp_bottom = 0;
    if (html->bound) {
        if (html->bound->border) {
            root_bp_left += html->bound->border->width.left;
            root_bp_right += html->bound->border->width.right;
            root_bp_top += html->bound->border->width.top;
            root_bp_bottom += html->bound->border->width.bottom;
        }
        root_bp_left += html->bound->padding.left;
        root_bp_right += html->bound->padding.right;
        root_bp_top += html->bound->padding.top;
        root_bp_bottom += html->bound->padding.bottom;
    }

    // Check for explicit CSS width on the root element
    bool root_has_explicit_width = false;
    float root_css_width = -1;  // content-box width from CSS
    if (html->blk) {
        if (html->blk->given_width > 0) {
            root_css_width = html->blk->given_width;
            root_has_explicit_width = true;
        } else if (!isnan(html->blk->given_width_percent)) {
            // Percentage width should have been resolved against viewport in style resolution
            // but may have resolved to 0 if parent context was lost; re-resolve here
            root_css_width = physical_width * html->blk->given_width_percent / 100.0f;
            root_has_explicit_width = (root_css_width > 0);
        }
    }

    // Check for explicit CSS height on the root element
    bool root_has_explicit_height = false;
    float root_css_height = -1;  // content-box height from CSS
    if (html->blk) {
        if (html->blk->given_height > 0) {
            root_css_height = html->blk->given_height;
            root_has_explicit_height = true;
        } else if (!isnan(html->blk->given_height_percent)) {
            root_css_height = physical_height * html->blk->given_height_percent / 100.0f;
            root_has_explicit_height = (root_css_height > 0);
        }
    }

    if (root_has_explicit_width) {
        // CSS 2.1 §10.3: Root element with explicit width.
        // Set html->width to the border-box width so the existing border/padding
        // subtraction code correctly computes content_width = root_css_width.
        float border_box_width = root_css_width + root_bp_left + root_bp_right;
        html->width = border_box_width;
        html->content_width = border_box_width;
        lycon->block.content_width = border_box_width;
        lycon->block.max_width = border_box_width;
        lycon->block.given_width = root_css_width;
        lycon->block.float_right_edge = border_box_width;
        line_init(lycon, 0, border_box_width);
        log_debug("[CSS] Root explicit width: css_width=%.1f, border_box=%.1f", root_css_width, border_box_width);
    }

    if (root_has_explicit_height) {
        // CSS 2.1 §10.6: Root element with explicit height.
        // Set the border-box height so finalize_block_flow doesn't override.
        float border_box_height = root_css_height + root_bp_top + root_bp_bottom;
        html->height = border_box_height;
        lycon->block.given_height = root_css_height;
        if (html->blk) html->blk->given_height = root_css_height;
        log_debug("[CSS] Root explicit height: css_height=%.1f, border_box=%.1f", root_css_height, border_box_height);
    }

    if (root_is_abspos) {
        // CSS 2.1 §10.3.7: Absolutely/fixed positioned root element.
        // Position from left/top offsets (containing block = initial containing block/viewport).
        // Margins do not reduce viewport width for abspos elements.
        if (html->position->has_left) {
            html->x = html->position->left;
        }
        if (html->position->has_top) {
            html->y = html->position->top;
        }
        // For abspos, margins shift position (if left/top not set, margins auto-resolve)
        if (!html->position->has_left && html->bound && html->bound->margin.left != 0) {
            html->x += html->bound->margin.left;
        }
        if (!html->position->has_top && html->bound && html->bound->margin.top != 0) {
            html->y += html->bound->margin.top;
        }

        // If no explicit width was set, use viewport width for abspos
        if (!root_has_explicit_width) {
            // Same as default: use viewport - margins (but margins don't reduce abspos width)
            // Keep the default viewport width
        }
        log_debug("[CSS] Root abspos: x=%.1f, y=%.1f", html->x, html->y);
    } else {
        // Static or relative positioning: apply margins normally
        if (html->bound && html->bound->margin.left != 0) {
            html->x = html->bound->margin.left;
        }
        if (html->bound && html->bound->margin.top != 0) {
            html->y = html->bound->margin.top;
        }

        if (!root_has_explicit_width) {
            // No explicit width: reduce by margins (existing behavior)
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
    }

    // CSS 2.1 §10.3.3: Apply root element border and padding to reduce content area
    // Children are placed inside the border+padding box, not at the margin edge
    {
        float bp_h = root_bp_left + root_bp_right;
        if (bp_h > 0) {
            float new_cw = lycon->block.content_width - bp_h;
            if (new_cw < 0) new_cw = 0;
            lycon->block.content_width = new_cw;
            lycon->block.max_width = new_cw;
            lycon->block.given_width = new_cw;
            lycon->block.float_right_edge = new_cw;
            log_debug("[CSS] Root border+padding: reducing content_width by %.1f to %.1f", bp_h, new_cw);
        }
        if (root_bp_top > 0) {
            lycon->block.advance_y += root_bp_top;
            log_debug("[CSS] Root border+padding: advance_y offset by %.1f to %.1f", root_bp_top, lycon->block.advance_y);
        }
        line_init(lycon, root_bp_left, lycon->block.content_width + root_bp_left);
    }

    // CSS 2.1 §12.2: Generate pseudo-elements for the root <html> element
    // Supports html:before and html:after in CSS conformance tests
    if (elmt->is_element()) {
        html->pseudo = alloc_pseudo_content_prop(lycon, html);
        generate_pseudo_element_content(lycon, html, true);   // ::before
        generate_pseudo_element_content(lycon, html, false);  // ::after
        if (html->pseudo) {
            if (html->pseudo->before) {
                insert_pseudo_into_dom((DomElement*)elmt, html->pseudo->before, true);
            }
            if (html->pseudo->after) {
                insert_pseudo_into_dom((DomElement*)elmt, html->pseudo->after, false);
            }
        }
    }

    // CSS 2.1 §9.2: Lay out ALL visible children of <html>, not just <body>.
    // Elements like <head> can have display:block set via CSS, in which case
    // they generate boxes and participate in layout. The default UA stylesheet
    // sets head to display:none, so this only affects pages that override it.
    DomNode* child = nullptr;
    if (elmt->is_element()) {
        child = static_cast<DomElement*>(elmt)->first_child;
    }
    while (child) {
        if (child->is_element()) {
            const char* tag_name = child->node_name();
            log_debug("  Checking html child element: %s", tag_name);
            DisplayValue child_display = resolve_display_value(child);
            if (child_display.outer != CSS_VALUE_NONE) {
                log_debug("  Laying out html child <%s> (display outer=%d, inner=%d)",
                    tag_name, child_display.outer, child_display.inner);
                layout_block(lycon, child, child_display);
            }
            if (strcmp(tag_name, "body") == 0) {
                body_node = child;
                log_debug("Found Lambda CSS body element");
            }
        }
        child = child->next_sibling;
    }

    auto t_body_find = high_resolution_clock::now();
    log_info("%s [TIMING] layout: body find: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_body_find - t_style).count());

    if (body_node) {
        // After layout, find the body view for scroll height calculation
        View* child = html->first_placed_child();
        ViewBlock* body_view = nullptr;
        while (child) {
            if (child->is_block()) {
                ViewBlock* vb = (ViewBlock*)child;
                if (vb->tag() == HTM_TAG_BODY) {
                    body_view = vb;
                    break;
                }
            }
            child = child->next();
        }

        if (body_view) {
            log_debug("Body layout done: body->height=%.1f, advance_y=%.1f",
                body_view->height, lycon->block.advance_y);
        } else {
            log_debug("Could not find body view in html children");
        }
    } else {
        log_debug("No body element found in DOM tree");
    }

    auto t_layout_block = high_resolution_clock::now();
    log_info("%s [TIMING] layout: layout_block: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_layout_block - t_body_find).count());

    finalize_block_flow(lycon, html, CSS_VALUE_BLOCK);

    // CSS 2.1 §9.4.3: Apply position:relative offsets to root element after layout
    if (root_is_relative && html->position) {
        if (html->position->position == CSS_VALUE_RELATIVE) {
            if (html->position->has_left) {
                html->x += html->position->left;
                log_debug("[CSS] Root relative offset: x += %.1f = %.1f", html->position->left, html->x);
            }
            if (html->position->has_top) {
                html->y += html->position->top;
                log_debug("[CSS] Root relative offset: y += %.1f = %.1f", html->position->top, html->y);
            }
        }
        // sticky on root element: no scroll container above root, so no clamping applies
    }

    // Set up viewport-level scrolling when document content exceeds the viewport height.
    // This enables mouse wheel scrolling for long documents that don't have explicit CSS
    // overflow properties on the root element. Only applies to auto-height root elements.
    if (!root_has_explicit_height && html->height > physical_height) {
        float content_height = html->height;
        html->content_height = content_height;
        html->height = physical_height;   // constrain root block to viewport height
        if (!html->scroller) {
            html->scroller = alloc_scroll_prop(lycon);
        }
        html->scroller->overflow_y = CSS_VALUE_AUTO;
        html->scroller->has_vt_scroll = true;
        html->scroller->has_vt_overflow = true;
        html->scroller->has_clip = true;
        html->scroller->clip.left = 0;
        html->scroller->clip.top = 0;
        html->scroller->clip.right = html->width;
        html->scroller->clip.bottom = physical_height;
        html->scroller->pane->v_max_scroll = content_height - physical_height;
        log_info("%s viewport scroll: content_height=%.1f, viewport_height=%.1f, v_max_scroll=%.1f", elmt->source_loc(),
            content_height, physical_height, html->scroller->pane->v_max_scroll);
    }

    auto t_finalize = high_resolution_clock::now();
    log_info("%s [TIMING] layout: finalize_block_flow: %.1fms", elmt->source_loc(), duration<double, std::milli>(t_finalize - t_layout_block).count());
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
    advance_measurement_cache_generation();

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

    // Serialization is handled by the caller (cmd_layout.cpp layout_single_file).
    // print_view_tree is NOT called here to avoid redundant file I/O.

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] print_view_tree: %.1fms", duration<double, std::milli>(t_end - t_layout).count());
    log_layout_timing_summary();
    log_info("[TIMING] layout_html_doc total: %.1fms", duration<double, std::milli>(t_end - t_start).count());
    log_debug("layout_html_doc complete");
}
