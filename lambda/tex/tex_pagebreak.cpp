// tex_pagebreak.cpp - Optimal Page Breaking Implementation
//
// Implements page breaking following TeXBook Chapter 15.
// Uses a greedy/best-first approach with penalties.
//
// Reference: TeXBook Chapter 15, Appendix H

#include "tex_pagebreak.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// Constants
// ============================================================================

static constexpr int INF_PAGE_PENALTY = 10000;    // Infinite penalty
static constexpr int EJECT_PAGE_PENALTY = -10000; // Force break
static constexpr int AWFUL_PAGE_BAD = 0x3FFFFFFF; // Impossibly bad

// ============================================================================
// Helper Functions
// ============================================================================

float node_vlist_height(TexNode* node) {
    if (!node) return 0;

    switch (node->node_class) {
        case NodeClass::Glue:
            return node->content.glue.spec.space;
        case NodeClass::Kern:
            return node->content.kern.amount;
        case NodeClass::Penalty:
            return 0;  // Penalties have no height
        default:
            return node->height + node->depth;
    }
}

int get_node_penalty(TexNode* node) {
    if (!node) return 0;
    if (node->node_class == NodeClass::Penalty) {
        return node->content.penalty.value;
    }
    return 0;
}

bool is_forced_page_break(TexNode* node) {
    if (!node) return false;
    if (node->node_class == NodeClass::Penalty) {
        return node->content.penalty.value <= EJECT_PAGE_PENALTY;
    }
    return false;
}

bool is_page_discardable(TexNode* node) {
    if (!node) return false;
    switch (node->node_class) {
        case NodeClass::Glue:
        case NodeClass::Kern:
        case NodeClass::Penalty:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Page Badness Computation
// ============================================================================

int compute_page_badness(
    float natural_height,
    float target_height,
    float stretch,
    float shrink
) {
    float excess = target_height - natural_height;

    if (excess >= 0) {
        // Page is short - need to stretch
        if (stretch <= 0) {
            return (excess > 0.1f) ? AWFUL_PAGE_BAD : 0;
        }
        float ratio = excess / stretch;
        if (ratio > 1.0f) {
            return AWFUL_PAGE_BAD;  // Can't stretch enough
        }
        // badness = 100 * ratio^3
        float r3 = ratio * ratio * ratio;
        return (int)(100.0f * r3);
    } else {
        // Page is too tall - need to shrink
        if (shrink <= 0) {
            return AWFUL_PAGE_BAD;  // Can't shrink
        }
        float ratio = -excess / shrink;
        if (ratio > 1.0f) {
            return AWFUL_PAGE_BAD;  // Can't shrink enough
        }
        float r3 = ratio * ratio * ratio;
        return (int)(100.0f * r3);
    }
}

// ============================================================================
// Break Candidate Finding
// ============================================================================

BreakCandidate* find_break_candidates(
    TexNode* vlist,
    const PageBreakParams& params,
    int* candidate_count,
    Arena* arena
) {
    log_debug("pagebreak: find_break_candidates page_height=%.1f top_skip=%.1f",
              params.page_height, params.top_skip);

    // First pass: count nodes
    int node_count = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        node_count++;
    }
    log_debug("pagebreak: vlist has %d children", node_count);

    // Worst case: can break after every node
    BreakCandidate* candidates = (BreakCandidate*)arena_alloc(
        arena, (node_count + 1) * sizeof(BreakCandidate)
    );

    int count = 0;
    float cumulative_height = params.top_skip;
    float cumulative_stretch = 0;
    float cumulative_shrink = 0;

    int index = 0;
    TexNode* prev = nullptr;

    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        // Log child info
        const char* class_name = "unknown";
        switch (c->node_class) {
            case NodeClass::Glue: class_name = "Glue"; break;
            case NodeClass::Kern: class_name = "Kern"; break;
            case NodeClass::HBox: class_name = "HBox"; break;
            case NodeClass::VBox: class_name = "VBox"; break;
            case NodeClass::HList: class_name = "HList"; break;
            case NodeClass::VList: class_name = "VList"; break;
            case NodeClass::Penalty: class_name = "Penalty"; break;
            default: break;
        }
        log_debug("pagebreak: child[%d] class=%s h=%.1f d=%.1f", 
                  index, class_name, c->height, c->depth);

        // Update cumulative dimensions
        if (c->node_class == NodeClass::Glue) {
            const Glue& g = c->content.glue.spec;
            cumulative_height += g.space;
            if (g.stretch_order == GlueOrder::Normal) {
                cumulative_stretch += g.stretch;
            }
            cumulative_shrink += g.shrink;
        } else if (c->node_class == NodeClass::Kern) {
            cumulative_height += c->content.kern.amount;
        } else {
            cumulative_height += c->height + c->depth;
        }

        // Check if this is a legal break point
        bool can_break = false;
        int penalty = 0;
        PageBreakType type = PageBreakType::Normal;

        if (c->node_class == NodeClass::Penalty) {
            penalty = c->content.penalty.value;
            if (penalty < INF_PAGE_PENALTY) {
                can_break = true;
                type = (penalty <= EJECT_PAGE_PENALTY) ?
                       PageBreakType::Forced : PageBreakType::Penalty;
            }
        } else if (c->node_class == NodeClass::Glue && prev) {
            // Can break at glue if preceded by non-discardable item
            if (!is_page_discardable(prev)) {
                can_break = true;
                type = PageBreakType::Normal;
            }
        }

        if (can_break) {
            BreakCandidate& cand = candidates[count];
            cand.node = c;
            cand.index = index;
            cand.type = type;
            cand.penalty = penalty;
            cand.page_height = cumulative_height;
            cand.page_depth = c->depth;
            cand.page_shrink = cumulative_shrink;
            cand.page_stretch = cumulative_stretch;

            // Compute badness at this break
            cand.badness = compute_page_badness(
                cumulative_height, params.page_height,
                cumulative_stretch, cumulative_shrink
            );

            // Initial cost (will be refined later)
            cand.cost = cand.badness + penalty * penalty;

            count++;
        }

        prev = c;
        index++;
    }

    // Add final break (end of document)
    if (count == 0 || candidates[count - 1].node != vlist->last_child) {
        BreakCandidate& cand = candidates[count];
        cand.node = vlist->last_child;
        cand.index = node_count - 1;
        cand.type = PageBreakType::End;
        cand.penalty = 0;
        cand.page_height = cumulative_height;
        cand.page_depth = vlist->last_child ? vlist->last_child->depth : 0;
        cand.page_shrink = cumulative_shrink;
        cand.page_stretch = cumulative_stretch;
        cand.badness = compute_page_badness(
            cumulative_height, params.page_height,
            cumulative_stretch, cumulative_shrink
        );
        cand.cost = cand.badness;
        count++;
    }

    log_debug("pagebreak: found %d break candidates, total_height=%.1f, total_shrink=%.1f", 
              count, cumulative_height, cumulative_shrink);

    *candidate_count = count;
    return candidates;
}

// ============================================================================
// Cost Computation
// ============================================================================

int compute_page_break_cost(
    const BreakCandidate& candidate,
    const BreakCandidate* prev_break,
    const PageBreakParams& params
) {
    int cost = candidate.badness;

    // Add penalty
    if (candidate.penalty > 0) {
        cost += candidate.penalty * candidate.penalty;
    } else if (candidate.penalty < 0 && candidate.penalty > EJECT_PAGE_PENALTY) {
        cost -= candidate.penalty * candidate.penalty;  // Reward negative penalties
    }

    return cost;
}

int widow_orphan_penalty(
    const BreakCandidate& candidate,
    TexNode* vlist,
    const PageBreakParams& params
) {
    // Count lines before and after break
    // This is a simplified check
    int lines_before = 0;
    int lines_after = 0;

    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c->node_class == NodeClass::HBox || c->node_class == NodeClass::HList) {
            if (c == candidate.node || !candidate.node) {
                // At break point
                break;
            }
            lines_before++;
        }
    }

    bool at_node = false;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (c == candidate.node) {
            at_node = true;
            continue;
        }
        if (at_node && (c->node_class == NodeClass::HBox || c->node_class == NodeClass::HList)) {
            lines_after++;
        }
    }

    int extra_penalty = 0;

    // Widow: single line at top of next page
    if (lines_after == 1) {
        extra_penalty += params.widow_penalty;
    }

    // Club: single line at bottom of this page
    // (hard to detect without paragraph structure)

    return extra_penalty;
}

// ============================================================================
// Main Page Breaking Algorithm
// ============================================================================

PageBreakResult break_into_pages(
    TexNode* vlist,
    const PageBreakParams& params,
    Arena* arena
) {
    PageBreakResult result;

    if (!vlist || !vlist->first_child) {
        result.page_count = 0;
        result.success = false;
        return result;
    }

    // Find all break candidates
    int candidate_count;
    BreakCandidate* candidates = find_break_candidates(
        vlist, params, &candidate_count, arena
    );

    if (candidate_count == 0) {
        // No valid breaks - put everything on one page
        result.break_indices = (int*)arena_alloc(arena, sizeof(int));
        result.break_indices[0] = -1;  // End marker
        result.page_count = 1;
        result.success = true;
        return result;
    }

    // Greedy algorithm: find breaks that minimize cost
    // This is simpler than Knuth-Plass but good enough for most documents

    // Allocate arrays for result
    int max_pages = candidate_count + 1;
    result.break_indices = (int*)arena_alloc(arena, max_pages * sizeof(int));
    result.page_penalties = (int*)arena_alloc(arena, max_pages * sizeof(int));

    int page_count = 0;
    float current_page_start_height = params.top_skip;
    float current_page_start_shrink = 0.0f;
    int last_break_index = -1;

    for (int i = 0; i < candidate_count; ++i) {
        BreakCandidate& cand = candidates[i];

        // Calculate page dimensions if we break here
        float page_height = cand.page_height - current_page_start_height;
        float page_shrink = cand.page_shrink - current_page_start_shrink;
        
        // Calculate how much shrink we'd need to fit this page
        float excess = page_height - params.page_height;
        float shrink_ratio = (excess > 0 && page_shrink > 0) ? excess / page_shrink : 0;
        
        // Also look ahead: can the remaining document fit on the current page with shrink?
        BreakCandidate& final_cand = candidates[candidate_count - 1];
        float total_remaining_height = final_cand.page_height - current_page_start_height;
        float total_remaining_shrink = final_cand.page_shrink - current_page_start_shrink;
        float final_excess = total_remaining_height - params.page_height;
        float final_shrink_ratio = (final_excess > 0 && total_remaining_shrink > 0) ? 
                                   final_excess / total_remaining_shrink : 0;
        
        // Maximum acceptable shrink ratio - TeX allows using all shrink (ratio up to 1.0)
        // If ratio > 1.0, the page is truly overfull and must break
        const float MAX_SHRINK_RATIO = 1.0f;
        
        log_debug("pagebreak: candidate[%d] page_height=%.1f excess=%.1f shrink=%.1f ratio=%.2f final_ratio=%.2f type=%d",
                  i, page_height, excess, page_shrink, shrink_ratio, final_shrink_ratio, (int)cand.type);

        // Check if this is a good break point
        bool should_break = false;

        // Forced break or document end
        if (cand.type == PageBreakType::Forced || cand.type == PageBreakType::End) {
            should_break = true;
        }
        // Page would overflow - but check if everything fits with acceptable shrink
        else if (page_height > params.page_height) {
            // Key decision: if ALL remaining content fits with acceptable shrink, don't break
            if (final_shrink_ratio <= MAX_SHRINK_RATIO) {
                // Everything fits on one page with reasonable shrink - don't break yet
                log_debug("pagebreak: skipping break - final content fits with ratio=%.2f", final_shrink_ratio);
            } else {
                // Can't fit everything - need to break
                // Find the last candidate that fits without any shrink (natural height <= page)
                for (int j = i - 1; j > last_break_index; --j) {
                    float j_height = candidates[j].page_height - current_page_start_height;
                    if (j_height <= params.page_height) {
                        // Found a break point where natural height fits
                        i = j;
                        should_break = true;
                        log_debug("pagebreak: backing up to candidate %d (height=%.1f fits)", j, j_height);
                        break;
                    }
                }
                if (!should_break) {
                    // No previous break fits naturally - use shrink if acceptable
                    if (shrink_ratio <= MAX_SHRINK_RATIO) {
                        should_break = true;
                        log_debug("pagebreak: breaking at overfull with acceptable shrink ratio=%.2f", shrink_ratio);
                    } else if (shrink_ratio <= 1.0f) {
                        // High shrink but still valid - break anyway
                        should_break = true;
                        log_debug("pagebreak: breaking with high shrink ratio=%.2f", shrink_ratio);
                    } else {
                        // Overfull page - break anyway
                        log_debug("pagebreak: forced overfull break at height=%.1f", page_height);
                        should_break = true;
                    }
                }
            }
        }
        // For negative penalties, break even if page isn't full (e.g., \newpage)
        else if (cand.penalty < 0) {
            should_break = true;
        }

        if (should_break) {
            result.break_indices[page_count] = cand.index;
            result.page_penalties[page_count] = cand.penalty;
            log_debug("pagebreak: BREAK at candidate %d, page_count=%d, index=%d",
                      i, page_count + 1, cand.index);
            page_count++;

            // Update for next page
            current_page_start_height = cand.page_height;
            current_page_start_shrink = cand.page_shrink;
            last_break_index = i;

            // Stop at end
            if (cand.type == PageBreakType::End) {
                break;
            }
        }
    }

    // Add final page for any remaining content after the last break
    // Only add if there was a real page break (not just the End marker being processed)
    if (page_count > 0 && last_break_index < candidate_count - 1) {
        // There's content after the last break - add it as the final page
        // But only if the last break wasn't at the very end already
        BreakCandidate& last_cand = candidates[candidate_count - 1];
        float remaining_height = last_cand.page_height - current_page_start_height;
        float remaining_shrink = last_cand.page_shrink - current_page_start_shrink;
        float remaining_min = remaining_height - remaining_shrink;
        if (remaining_min > 0.1f) {  // Have meaningful remaining content (after shrink)
            result.break_indices[page_count] = last_cand.index;
            result.page_penalties[page_count] = 0;
            log_debug("pagebreak: adding final page for remaining content (%.1fpt min, natural=%.1fpt), page_count=%d", 
                      remaining_min, remaining_height, page_count + 1);
            page_count++;
        }
    }

    // Ensure we have at least one page
    if (page_count == 0) {
        result.break_indices[0] = candidate_count > 0 ?
            candidates[candidate_count - 1].index : 0;
        result.page_penalties[0] = 0;
        page_count = 1;
    }

    result.page_count = page_count;
    result.success = true;

    return result;
}

// ============================================================================
// Page Building
// ============================================================================

TexNode* build_page_vbox(
    TexNode* vlist,
    int start_index,
    int end_index,
    const PageBreakParams& params,
    Arena* arena
) {
    TexNode* page = make_vbox(arena, params.page_height);

    // Add top skip
    TexNode* top_skip_glue = make_glue(arena, Glue::fixed(params.top_skip), "topskip");
    page->append_child(top_skip_glue);

    // Copy nodes from vlist
    int index = 0;
    bool started = false;
    float total_height = params.top_skip;

    for (TexNode* c = vlist->first_child; c; ) {
        // Save next sibling BEFORE append_child modifies it
        TexNode* next = c->next_sibling;

        if (index >= start_index) {
            started = true;
        }

        if (started && index <= end_index) {
            // Skip discardable items at page start
            if (index == start_index && is_page_discardable(c)) {
                index++;
                c = next;
                continue;
            }

            // Clone node for this page
            // For now, we just use the same node (shallow copy semantics)
            // In a full implementation, we'd deep clone
            page->append_child(c);

            total_height += node_vlist_height(c);
        }

        if (index > end_index) {
            break;
        }

        index++;
        c = next;
    }

    // Set page dimensions
    page->height = total_height;
    page->depth = 0;

    // Adjust depth
    adjust_page_depth(page, params.max_depth, arena);

    return page;
}

PageContent* build_pages(
    TexNode* vlist,
    const PageBreakResult& result,
    const PageBreakParams& params,
    Arena* arena
) {
    if (result.page_count == 0) {
        return nullptr;
    }

    PageContent* pages = (PageContent*)arena_alloc(
        arena, result.page_count * sizeof(PageContent)
    );

    // Initialize mark state for tracking across pages
    MarkState mark_state;

    int start_index = 0;

    for (int p = 0; p < result.page_count; ++p) {
        int end_index = result.break_indices[p];

        pages[p].vlist = build_page_vbox(vlist, start_index, end_index, params, arena);
        pages[p].height = pages[p].vlist->height;
        pages[p].depth = pages[p].vlist->depth;
        pages[p].break_penalty = result.page_penalties[p];
        pages[p].marks_first = nullptr;
        pages[p].marks_top = nullptr;
        pages[p].marks_bot = nullptr;
        pages[p].inserts = nullptr;
        pages[p].deferred_floats = nullptr;
        pages[p].deferred_float_count = 0;

        // Extract marks with state tracking
        extract_page_marks_with_state(pages[p], vlist, start_index, end_index, mark_state);

        // Collect and place floats
        int insert_count = 0;
        TexNode** inserts = collect_inserts(vlist, start_index, end_index, &insert_count, arena);
        if (insert_count > 0) {
            place_floats(pages[p], inserts, insert_count, params, arena);
        }

        // Advance mark state for next page
        mark_state.advance_page();

        start_index = end_index + 1;
    }

    return pages;
}

PageContent* paginate(
    TexNode* vlist,
    const PageBreakParams& params,
    int* page_count_out,
    Arena* arena
) {
    PageBreakResult result = break_into_pages(vlist, params, arena);

    if (!result.success) {
        *page_count_out = 0;
        return nullptr;
    }

    *page_count_out = result.page_count;
    return build_pages(vlist, result, params, arena);
}

// ============================================================================
// Insert/Float Handling
// ============================================================================

TexNode** collect_inserts(
    TexNode* vlist,
    int start_index,
    int end_index,
    int* insert_count,
    Arena* arena
) {
    // Count inserts
    int count = 0;
    int index = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (index >= start_index && index <= end_index) {
            if (c->node_class == NodeClass::Insert) {
                count++;
            }
        }
        index++;
    }

    if (count == 0) {
        *insert_count = 0;
        return nullptr;
    }

    TexNode** inserts = (TexNode**)arena_alloc(arena, count * sizeof(TexNode*));

    index = 0;
    int i = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (index >= start_index && index <= end_index) {
            if (c->node_class == NodeClass::Insert) {
                inserts[i++] = c;
            }
        }
        index++;
    }

    *insert_count = count;
    return inserts;
}

void place_floats(
    PageContent& page,
    TexNode** floats,
    int float_count,
    const PageBreakParams& params,
    Arena* arena
) {
    if (float_count == 0) return;

    // Calculate available space for floats
    float max_top_height = params.page_height * params.top_fraction;
    float max_bottom_height = params.page_height * params.bottom_fraction;
    float min_text_height = params.page_height * params.text_fraction;

    // Current usage
    float top_used = 0;
    float bottom_used = 0;

    // Separate floats into top and bottom placement
    TexNode* top_floats_head = nullptr;
    TexNode* top_floats_tail = nullptr;
    TexNode* bottom_floats_head = nullptr;
    TexNode* bottom_floats_tail = nullptr;

    for (int i = 0; i < float_count; ++i) {
        TexNode* f = floats[i];
        if (!f || f->node_class != NodeClass::Insert) continue;

        float fh = f->content.insert.natural_height;

        // Try top placement first
        if (top_used + fh <= max_top_height) {
            // Clone for top placement
            TexNode* clone = f->content.insert.content;
            if (clone) {
                if (!top_floats_head) {
                    top_floats_head = top_floats_tail = clone;
                } else {
                    top_floats_tail->next_sibling = clone;
                    clone->prev_sibling = top_floats_tail;
                    top_floats_tail = clone;
                }
                top_used += fh;
            }
        }
        // Try bottom placement
        else if (bottom_used + fh <= max_bottom_height) {
            TexNode* clone = f->content.insert.content;
            if (clone) {
                if (!bottom_floats_head) {
                    bottom_floats_head = bottom_floats_tail = clone;
                } else {
                    bottom_floats_tail->next_sibling = clone;
                    clone->prev_sibling = bottom_floats_tail;
                    bottom_floats_tail = clone;
                }
                bottom_used += fh;
            }
        }
        // Float doesn't fit - will be deferred to next page
        else {
            log_debug("pagebreak: deferring float of height %.1f", fh);
        }
    }

    // Add floats to page
    if (top_floats_head) {
        // Insert top floats after topskip
        add_page_inserts(page.vlist, top_floats_head, nullptr, params, arena);
    }
    if (bottom_floats_head) {
        // Add bottom floats at end
        add_page_inserts(page.vlist, nullptr, bottom_floats_head, params, arena);
    }
}

// ============================================================================
// Insertion Class Processing
// ============================================================================

// Process all inserts and organize by class
void collect_inserts_by_class(
    TexNode* vlist,
    int start_index,
    int end_index,
    InsertionState& state
) {
    int index = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (index >= start_index && index <= end_index) {
            if (c->node_class == NodeClass::Insert) {
                int cls = c->content.insert.insert_class;
                float h = c->content.insert.natural_height;
                TexNode* content = c->content.insert.content;
                state.add_insert(cls, content, h);
            }
        }
        index++;
    }
}

// Place all insertions on a page by class
void place_insertions_by_class(
    PageContent& page,
    InsertionState& state,
    const PageBreakParams& params,
    Arena* arena
) {
    // Build footnote vbox (class 254) - goes at bottom
    TexNode* footnote_content = state.class_content[INSERT_CLASS_FOOTNOTE];
    if (footnote_content) {
        // Create footnote separator rule
        TexNode* sep_rule = make_rule(arena, params.page_height * 0.3f, 0.4f, 0);
        TexNode* sep_skip = make_kern(arena, 3.0f);

        // Build footnote box
        TexNode* footnote_vbox = make_vbox(arena);
        footnote_vbox->append_child(sep_rule);
        footnote_vbox->append_child(sep_skip);

        // Add footnote content
        for (TexNode* fn = footnote_content; fn; fn = fn->next_sibling) {
            footnote_vbox->append_child(fn);
        }

        // Calculate dimensions
        float fn_height = sep_rule->height + sep_skip->height;
        for (TexNode* fn = footnote_content; fn; fn = fn->next_sibling) {
            fn_height += fn->height + fn->depth;
        }
        footnote_vbox->height = fn_height;

        // Add to bottom of page
        page.vlist->append_child(footnote_vbox);
        page.height += fn_height;
    }

    // Top floats (class 253) - already handled by place_floats
    // Bottom floats (class 255) - already handled by place_floats

    // Clear state for next page
    state.reset();
}

// ============================================================================
// Mark Handling
// ============================================================================

void extract_page_marks(
    PageContent& page,
    TexNode* vlist,
    int start_index,
    int end_index
) {
    page.marks_first = nullptr;
    page.marks_top = nullptr;
    page.marks_bot = nullptr;

    int index = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (index >= start_index && index <= end_index) {
            if (c->node_class == NodeClass::Mark) {
                if (!page.marks_first) {
                    page.marks_first = c;
                }
                page.marks_bot = c;
            }
        }
        index++;
    }
}

// Extract marks with state tracking across pages
void extract_page_marks_with_state(
    PageContent& page,
    TexNode* vlist,
    int start_index,
    int end_index,
    MarkState& state
) {
    // topmark comes from previous page's botmark
    page.marks_top = state.top_mark;
    page.marks_first = nullptr;
    page.marks_bot = nullptr;

    int index = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        if (index >= start_index && index <= end_index) {
            if (c->node_class == NodeClass::Mark) {
                state.record_mark(c);
                if (!page.marks_first) {
                    page.marks_first = c;
                }
                page.marks_bot = c;
            }
        }
        index++;
    }

    // Update state for next page
    // Note: state.advance_page() should be called BEFORE processing next page
}

// ============================================================================
// Page Depth Adjustment
// ============================================================================

void adjust_page_depth(
    TexNode* page_vbox,
    float max_depth,
    Arena* arena
) {
    if (!page_vbox || !page_vbox->last_child) {
        return;
    }

    float last_depth = page_vbox->last_child->depth;

    if (last_depth > max_depth) {
        // Add kern to shift baseline up
        float kern_amount = last_depth - max_depth;
        TexNode* kern = make_kern(arena, kern_amount);
        page_vbox->append_child(kern);

        // Adjust page dimensions
        page_vbox->height += kern_amount;
        page_vbox->depth = max_depth;
    }
}

void add_page_inserts(
    TexNode* page_vbox,
    TexNode* top_inserts,
    TexNode* bottom_inserts,
    const PageBreakParams& params,
    Arena* arena
) {
    // Add top inserts after top skip
    if (top_inserts) {
        TexNode* first_content = page_vbox->first_child ?
            page_vbox->first_child->next_sibling : nullptr;
        if (first_content) {
            page_vbox->insert_after(page_vbox->first_child, top_inserts);
        }
    }

    // Add bottom inserts before last item
    if (bottom_inserts) {
        page_vbox->append_child(bottom_inserts);
    }
}

// ============================================================================
// Debugging
// ============================================================================

void dump_break_candidates(const BreakCandidate* candidates, int count) {
    log_debug("Page break candidates: %d total", count);
    for (int i = 0; i < count; ++i) {
        const BreakCandidate& c = candidates[i];
        const char* type_str = "unknown";
        switch (c.type) {
            case PageBreakType::Normal: type_str = "normal"; break;
            case PageBreakType::Penalty: type_str = "penalty"; break;
            case PageBreakType::Display: type_str = "display"; break;
            case PageBreakType::Float: type_str = "float"; break;
            case PageBreakType::Forced: type_str = "forced"; break;
            case PageBreakType::End: type_str = "end"; break;
        }
        log_debug("  [%d] idx=%d type=%s pen=%d h=%.1f bad=%d cost=%d",
            i, c.index, type_str, c.penalty, c.page_height, c.badness, c.cost);
    }
}

void dump_page_break_result(const PageBreakResult& result) {
    log_debug("Page break result: %d pages, success=%s",
        result.page_count, result.success ? "true" : "false");
    for (int i = 0; i < result.page_count; ++i) {
        log_debug("  Page %d: break at index %d, penalty=%d",
            i + 1, result.break_indices[i], result.page_penalties[i]);
    }
}

void dump_page_content(const PageContent& page, int page_number) {
    log_debug("Page %d: height=%.1f depth=%.1f penalty=%d",
        page_number, page.height, page.depth, page.break_penalty);

    if (page.marks_first) {
        log_debug("  First mark: %s", page.marks_first->content.mark.text);
    }
    if (page.marks_bot) {
        log_debug("  Bottom mark: %s", page.marks_bot->content.mark.text);
    }
}

} // namespace tex
