// tex_paragraph.cpp - Knuth-Plass Line Breaking Implementation
//
// Implements the optimal line breaking algorithm from TeXBook Appendix H

#include "tex_paragraph.hpp"
#include "../../lib/log.h"
#include <cmath>
#include <climits>
#include <algorithm>

namespace tex {

// ============================================================================
// Constants
// ============================================================================

// Infinity for demerits
static const int AWFUL_BAD = 0x3FFFFFFF;
static const int INF_BAD = 10000;
static const int INF_PENALTY = 10000;
static const int EJECT_PENALTY = -INF_PENALTY;

// Fitness boundaries
static const float TIGHT_BOUND = -0.5f;
static const float NORMAL_BOUND = 0.5f;
static const float LOOSE_BOUND = 1.0f;

// ============================================================================
// Fitness Classification
// ============================================================================

Fitness compute_fitness(float ratio) {
    if (ratio < TIGHT_BOUND) return Fitness::Tight;
    if (ratio < NORMAL_BOUND) return Fitness::Normal;
    if (ratio < LOOSE_BOUND) return Fitness::Loose;
    return Fitness::VeryLoose;
}

// ============================================================================
// Badness Computation (TeXBook p.97)
// ============================================================================

int compute_badness(
    float width,
    float stretch,
    float shrink,
    float target_width
) {
    float excess = target_width - width;

    if (excess >= 0) {
        // Need to stretch
        if (stretch <= 0) {
            return (excess > 0.1f) ? INF_BAD : 0;
        }
        float ratio = excess / stretch;
        if (ratio > 1.0f) {
            return INF_BAD;
        }
        // Badness = 100 * r^3 (approximately)
        float r3 = ratio * ratio * ratio;
        return (int)(100.0f * r3 + 0.5f);
    } else {
        // Need to shrink
        float shrink_needed = -excess;
        if (shrink <= 0) {
            return INF_BAD;
        }
        if (shrink_needed > shrink) {
            return INF_BAD + 1;  // Overfull
        }
        float ratio = shrink_needed / shrink;
        float r3 = ratio * ratio * ratio;
        return (int)(100.0f * r3 + 0.5f);
    }
}

// ============================================================================
// Demerits Computation (TeXBook p.98)
// ============================================================================

int compute_demerits(
    int badness,
    int penalty,
    int line_penalty,
    Fitness fitness,
    Fitness prev_fitness
) {
    int d;

    if (penalty >= 0) {
        d = (line_penalty + badness) * (line_penalty + badness) + penalty * penalty;
    } else if (penalty > EJECT_PENALTY) {
        d = (line_penalty + badness) * (line_penalty + badness) - penalty * penalty;
    } else {
        d = (line_penalty + badness) * (line_penalty + badness);
    }

    // Add adjacent fitness penalty
    int fitness_diff = abs((int)fitness - (int)prev_fitness);
    if (fitness_diff > 1) {
        d += 10000;  // adj_demerits
    }

    return d;
}

// ============================================================================
// Line Width/Indent Computation
// ============================================================================

float line_width_at(int line_number, const LineBreakParams& params) {
    // Check parshape
    if (params.parshape_widths && line_number <= params.parshape_count) {
        return params.parshape_widths[line_number - 1];
    }

    // Check hanging indent
    if (params.hang_indent != 0) {
        bool in_hang_region;
        if (params.hang_after >= 0) {
            in_hang_region = (line_number > params.hang_after);
        } else {
            // hang_after < 0 means last -hang_after lines
            in_hang_region = true;  // Simplified - would need total line count
        }

        if (in_hang_region) {
            return params.line_width - std::abs(params.hang_indent);
        }
    }

    return params.line_width;
}

float line_indent_at(int line_number, const LineBreakParams& params) {
    // Check parshape
    if (params.parshape_indents && line_number <= params.parshape_count) {
        return params.parshape_indents[line_number - 1];
    }

    // First line indent
    if (line_number == 1) {
        return params.par_indent;
    }

    // Check hanging indent
    if (params.hang_indent != 0) {
        bool in_hang_region;
        if (params.hang_after >= 0) {
            in_hang_region = (line_number > params.hang_after);
        } else {
            in_hang_region = true;
        }

        if (in_hang_region && params.hang_indent > 0) {
            return params.hang_indent;
        }
    }

    return 0;
}

// ============================================================================
// Find Break Points
// ============================================================================

BreakPoint* find_break_points(
    TexBox* hlist,
    int* count,
    Arena* arena
) {
    if (hlist->content_type != BoxContentType::HList) {
        *count = 0;
        return nullptr;
    }

    int capacity = 64;
    BreakPoint* breaks = (BreakPoint*)arena_alloc(arena, capacity * sizeof(BreakPoint));
    int n = 0;

    // Add initial break point (start of paragraph)
    breaks[n++] = BreakPoint{
        .index = -1,
        .type = BreakType::Ordinary,
        .penalty = 0,
        .pre_break = nullptr,
        .post_break = nullptr,
        .no_break = nullptr
    };

    for (int i = 0; i < hlist->content.list.count; ++i) {
        TexBox* item = hlist->content.list.children[i];

        bool can_break = false;
        BreakType type = BreakType::Ordinary;
        int penalty = 0;

        switch (item->content_type) {
            case BoxContentType::Glue:
                // Can break before glue (after preceding item)
                if (i > 0) {
                    TexBox* prev = hlist->content.list.children[i - 1];
                    // Don't break after opening delimiter or before closing
                    if (prev->content_type != BoxContentType::Glue &&
                        prev->atom_type != AtomType::Open) {
                        can_break = true;
                    }
                }
                break;

            case BoxContentType::Penalty:
                if (item->content.penalty.value < INF_PENALTY) {
                    can_break = true;
                    type = BreakType::Penalty;
                    penalty = item->content.penalty.value;
                }
                break;

            case BoxContentType::Discretionary:
                can_break = true;
                type = BreakType::Discretionary;
                break;

            default:
                break;
        }

        if (can_break) {
            if (n >= capacity) {
                // Expand
                capacity *= 2;
                BreakPoint* new_breaks = (BreakPoint*)arena_alloc(arena,
                    capacity * sizeof(BreakPoint));
                for (int j = 0; j < n; ++j) {
                    new_breaks[j] = breaks[j];
                }
                breaks = new_breaks;
            }

            breaks[n++] = BreakPoint{
                .index = i,
                .type = type,
                .penalty = penalty,
                .pre_break = (item->content_type == BoxContentType::Discretionary) ?
                    item->content.disc.pre_break : nullptr,
                .post_break = (item->content_type == BoxContentType::Discretionary) ?
                    item->content.disc.post_break : nullptr,
                .no_break = (item->content_type == BoxContentType::Discretionary) ?
                    item->content.disc.no_break : nullptr
            };
        }
    }

    // Add final break point (end of paragraph)
    if (n >= capacity) {
        capacity++;
        BreakPoint* new_breaks = (BreakPoint*)arena_alloc(arena,
            capacity * sizeof(BreakPoint));
        for (int j = 0; j < n; ++j) {
            new_breaks[j] = breaks[j];
        }
        breaks = new_breaks;
    }

    breaks[n++] = BreakPoint{
        .index = hlist->content.list.count,
        .type = BreakType::Penalty,
        .penalty = EJECT_PENALTY,  // Force break at end
        .pre_break = nullptr,
        .post_break = nullptr,
        .no_break = nullptr
    };

    *count = n;
    return breaks;
}

// ============================================================================
// Cumulative Dimensions
// ============================================================================

void compute_cumulative_dims(
    TexBox* hlist,
    CumulativeDims* dims,
    int count
) {
    float width = 0;
    float stretch = 0;
    float shrink = 0;

    int dim_idx = 0;

    // Initial dimensions (before any items)
    dims[dim_idx].width = 0;
    dims[dim_idx].stretch = 0;
    dims[dim_idx].shrink = 0;
    dim_idx++;

    for (int i = 0; i < hlist->content.list.count && dim_idx < count; ++i) {
        TexBox* item = hlist->content.list.children[i];

        float item_width = item->width * item->scale;
        float item_stretch = 0;
        float item_shrink = 0;

        if (item->content_type == BoxContentType::Glue) {
            const Glue& g = item->content.glue;
            item_stretch = g.stretch;
            item_shrink = g.shrink;
        }

        width += item_width;
        stretch += item_stretch;
        shrink += item_shrink;

        // Record cumulative at each break point
        // (Simplified - actual implementation would track break positions)
        dims[dim_idx].width = width;
        dims[dim_idx].stretch = stretch;
        dims[dim_idx].shrink = shrink;
        dim_idx++;
    }
}

// ============================================================================
// Knuth-Plass Algorithm
// ============================================================================

// Internal state for the algorithm
struct BreakState {
    Arena* arena;
    const LineBreakParams* params;

    TexBox* hlist;
    BreakPoint* breaks;
    int break_count;

    CumulativeDims* dims;  // Cumulative dimensions at each break

    ActiveNode* active_head;  // Head of active list
    ActiveNode* passive_head; // Head of passive list (for memory)

    int best_line_count;
    int best_demerits;
    ActiveNode* best_node;

    // Current processing state
    int cur_position;
    float cur_width;
    float cur_stretch;
    float cur_shrink;
};

static ActiveNode* create_active_node(BreakState* state) {
    ActiveNode* node = (ActiveNode*)arena_alloc(state->arena, sizeof(ActiveNode));
    node->prev = nullptr;
    node->link = nullptr;
    return node;
}

static void try_break(BreakState* state, int pos, int penalty) {
    const LineBreakParams& params = *state->params;

    // Get dimensions at this break point
    CumulativeDims& cur_dims = state->dims[pos];

    // Try breaking at this position from each active node
    ActiveNode* prev_active = nullptr;
    ActiveNode* active = state->active_head;

    while (active) {
        // Compute line dimensions from active to cur
        int line_number = active->line_number + 1;
        float target_width = line_width_at(line_number, params);

        float line_width = cur_dims.width - active->total_width;
        float line_stretch = cur_dims.stretch - active->total_stretch;
        float line_shrink = cur_dims.shrink - active->total_shrink;

        // Add indentation
        float indent = line_indent_at(line_number, params);
        line_width += indent;

        // Compute badness
        int badness = compute_badness(line_width, line_stretch, line_shrink, target_width);

        if (badness > INF_BAD) {
            // Line is too tight - deactivate this node if it's getting worse
            if (line_width > target_width + 0.1f) {
                // Deactivate: line will only get worse
                if (prev_active) {
                    prev_active->link = active->link;
                } else {
                    state->active_head = active->link;
                }
                active = active->link;
                continue;
            }
        }

        // Check if this break is feasible
        bool feasible = (badness <= INF_BAD) || (penalty <= EJECT_PENALTY);

        if (feasible) {
            // Compute fitness class
            float ratio = 0;
            if (target_width > line_width && line_stretch > 0) {
                ratio = (target_width - line_width) / line_stretch;
            } else if (target_width < line_width && line_shrink > 0) {
                ratio = -(line_width - target_width) / line_shrink;
            }
            Fitness fitness = compute_fitness(ratio);

            // Compute demerits
            int demerits = compute_demerits(
                badness, penalty, params.line_penalty,
                fitness, active->fitness
            );

            // Add hyphen penalties if applicable
            if (state->breaks[pos].type == BreakType::Hyphen ||
                state->breaks[pos].type == BreakType::Explicit) {
                // Check if previous line also ended with hyphen
                // (Would need to track this in active node)
            }

            int total_demerits = active->total_demerits + demerits;

            // Check if this is a valid path
            if (total_demerits < AWFUL_BAD) {
                // Create new active node
                ActiveNode* new_node = create_active_node(state);
                new_node->position = pos;
                new_node->line_number = line_number;
                new_node->fitness = fitness;
                new_node->total_demerits = total_demerits;
                new_node->total_width = cur_dims.width;
                new_node->total_stretch = cur_dims.stretch;
                new_node->total_shrink = cur_dims.shrink;
                new_node->prev = active;

                // Insert into active list (after current active)
                new_node->link = active->link;
                active->link = new_node;

                // Check if this is the best ending
                if (pos == state->break_count - 1) {  // End of paragraph
                    if (!state->best_node ||
                        total_demerits < state->best_demerits ||
                        (total_demerits == state->best_demerits &&
                         abs(line_number - (int)params.looseness) <
                         abs(state->best_line_count - (int)params.looseness))) {
                        state->best_node = new_node;
                        state->best_demerits = total_demerits;
                        state->best_line_count = line_number;
                    }
                }
            }
        }

        prev_active = active;
        active = active->link;
    }
}

LineBreakResult break_paragraph(
    TexBox* hlist,
    const LineBreakParams& params,
    Arena* arena
) {
    LineBreakResult result = {nullptr, 0, AWFUL_BAD, false};

    if (hlist->content_type != BoxContentType::HList) {
        log_error("tex_paragraph: expected HList for line breaking");
        return result;
    }

    BreakState state = {};
    state.arena = arena;
    state.params = &params;
    state.hlist = hlist;

    // Find potential break points
    state.breaks = find_break_points(hlist, &state.break_count, arena);

    if (state.break_count < 2) {
        // No breaks possible
        result.break_positions = (int*)arena_alloc(arena, sizeof(int));
        result.break_positions[0] = hlist->content.list.count;
        result.line_count = 1;
        result.success = true;
        return result;
    }

    // Compute cumulative dimensions
    state.dims = (CumulativeDims*)arena_alloc(arena,
        state.break_count * sizeof(CumulativeDims));

    // Map break indices to cumulative dims
    float width = 0, stretch = 0, shrink = 0;
    for (int b = 0; b < state.break_count; ++b) {
        int end_idx = (b == 0) ? 0 : state.breaks[b].index;
        int start_idx = (b == 0) ? 0 : state.breaks[b - 1].index + 1;

        for (int i = start_idx; i < end_idx && i < hlist->content.list.count; ++i) {
            TexBox* item = hlist->content.list.children[i];
            width += item->width * item->scale;
            if (item->content_type == BoxContentType::Glue) {
                stretch += item->content.glue.stretch;
                shrink += item->content.glue.shrink;
            }
        }

        state.dims[b].width = width;
        state.dims[b].stretch = stretch;
        state.dims[b].shrink = shrink;
    }

    // Initialize active list with starting node
    state.active_head = create_active_node(&state);
    state.active_head->position = 0;
    state.active_head->line_number = 0;
    state.active_head->fitness = Fitness::Normal;
    state.active_head->total_demerits = 0;
    state.active_head->total_width = 0;
    state.active_head->total_stretch = 0;
    state.active_head->total_shrink = 0;

    state.best_node = nullptr;
    state.best_demerits = AWFUL_BAD;

    // First pass: pretolerance
    if (params.pretolerance >= 0) {
        for (int b = 1; b < state.break_count; ++b) {
            try_break(&state, b, state.breaks[b].penalty);
        }
    }

    // Check if first pass succeeded
    if (!state.best_node && params.pretolerance >= 0) {
        // Second pass with higher tolerance
        log_debug("tex_paragraph: first pass failed, trying second pass");

        // Reset active list
        state.active_head = create_active_node(&state);
        state.active_head->position = 0;
        state.active_head->line_number = 0;
        state.active_head->fitness = Fitness::Normal;
        state.active_head->total_demerits = 0;
        state.active_head->total_width = 0;
        state.active_head->total_stretch = 0;
        state.active_head->total_shrink = 0;

        for (int b = 1; b < state.break_count; ++b) {
            try_break(&state, b, state.breaks[b].penalty);
        }
    }

    // Extract result
    if (state.best_node) {
        // Count lines and allocate
        int line_count = state.best_node->line_number;
        result.break_positions = (int*)arena_alloc(arena, line_count * sizeof(int));
        result.line_count = line_count;
        result.total_demerits = state.best_demerits;
        result.success = true;

        // Walk back through nodes to get break positions
        ActiveNode* node = state.best_node;
        int idx = line_count - 1;

        while (node && idx >= 0) {
            result.break_positions[idx] = state.breaks[node->position].index;
            node = node->prev;
            idx--;
        }

        log_debug("tex_paragraph: found %d lines with demerits %d",
            line_count, result.total_demerits);
    } else {
        log_error("tex_paragraph: no valid line breaks found");

        // Emergency: return single line
        result.break_positions = (int*)arena_alloc(arena, sizeof(int));
        result.break_positions[0] = hlist->content.list.count;
        result.line_count = 1;
        result.success = false;
    }

    return result;
}

// ============================================================================
// Build Lines from Breaks
// ============================================================================

TexBox* extract_line_content(
    TexBox* hlist,
    int start_pos,
    int end_pos,
    BreakType break_type,
    Arena* arena
) {
    TexBox* line = make_hlist_box(arena);

    // Clamp positions
    if (start_pos < 0) start_pos = 0;
    if (end_pos > hlist->content.list.count) end_pos = hlist->content.list.count;

    // Copy items (simplified - actual implementation would handle discretionaries)
    for (int i = start_pos; i < end_pos; ++i) {
        TexBox* item = hlist->content.list.children[i];

        // Skip glue at start of line
        if (line->content.list.count == 0 &&
            item->content_type == BoxContentType::Glue) {
            continue;
        }

        // Skip penalty nodes
        if (item->content_type == BoxContentType::Penalty) {
            continue;
        }

        add_child(line, item, arena);
    }

    compute_hlist_natural_dims(line);
    return line;
}

Line* build_lines(
    TexBox* hlist,
    const LineBreakResult& breaks,
    const LineBreakParams& params,
    Arena* arena
) {
    Line* lines = (Line*)arena_alloc(arena, breaks.line_count * sizeof(Line));

    int prev_pos = 0;
    for (int i = 0; i < breaks.line_count; ++i) {
        int end_pos = breaks.break_positions[i];

        lines[i].content = extract_line_content(
            hlist, prev_pos, end_pos,
            BreakType::Ordinary, arena
        );

        // Set line width
        float target_width = line_width_at(i + 1, params);
        set_hlist_width(lines[i].content, target_width, arena);

        lines[i].width = lines[i].content->width;
        lines[i].baseline_skip = 12.0f;  // Default baseline skip

        prev_pos = end_pos;
    }

    return lines;
}

TexBox* build_paragraph_vlist(
    Line* lines,
    int line_count,
    float baseline_skip,
    Arena* arena
) {
    TexBox* vlist = make_vlist_box(arena);

    for (int i = 0; i < line_count; ++i) {
        add_child(vlist, lines[i].content, arena);

        // Add baseline skip glue between lines
        if (i < line_count - 1) {
            float skip = baseline_skip - lines[i].content->depth -
                        lines[i + 1].content->height;
            if (skip > 0) {
                TexBox* glue = make_glue_box(
                    Glue{skip, skip * 0.1f, GlueOrder::Normal, skip * 0.05f, GlueOrder::Normal},
                    arena
                );
                add_child(vlist, glue, arena);
            }
        }
    }

    compute_vlist_natural_dims(vlist);
    return vlist;
}

// ============================================================================
// Hyphenation (Stub - would need pattern tables)
// ============================================================================

HyphenationResult hyphenate_word(
    const char* word,
    int length,
    const char* language,
    Arena* arena
) {
    // Stub implementation - would use Liang's algorithm with pattern tables
    HyphenationResult result = {nullptr, 0};

    // Minimum word length for hyphenation
    if (length < 5) {
        return result;
    }

    // Simple heuristic: allow breaks between consonant clusters and vowels
    // This is NOT proper hyphenation - just a placeholder

    return result;
}

void insert_hyphenation(
    TexBox* hlist,
    const char* language,
    Arena* arena
) {
    // Would insert discretionary nodes at hyphenation points
    // Stub for now
}

// ============================================================================
// Debugging
// ============================================================================

void dump_break_points(const BreakPoint* breaks, int count) {
    log_debug("Break points (%d):", count);
    for (int i = 0; i < count; ++i) {
        const char* type_names[] = {
            "Ordinary", "Hyphen", "Explicit", "Math", "Discretionary", "Penalty"
        };
        log_debug("  [%d] index=%d type=%s penalty=%d",
            i, breaks[i].index, type_names[(int)breaks[i].type], breaks[i].penalty);
    }
}

void dump_line_breaks(const LineBreakResult& result) {
    log_debug("Line breaks (%d lines, demerits=%d, success=%s):",
        result.line_count, result.total_demerits,
        result.success ? "true" : "false");
    for (int i = 0; i < result.line_count; ++i) {
        log_debug("  Line %d ends at position %d", i + 1, result.break_positions[i]);
    }
}

} // namespace tex
