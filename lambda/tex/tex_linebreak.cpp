// tex_linebreak.cpp - Knuth-Plass Line Breaking Implementation
//
// Implements the optimal line breaking algorithm from TeXBook Appendix H
// using the new TexNode system.

#include "tex_linebreak.hpp"
#include "tex_hlist.hpp"
#include "../../lib/log.h"
#include <cmath>
#include <cstring>

namespace tex {

// ============================================================================
// Constants
// ============================================================================

static constexpr float TIGHT_BOUND = -0.5f;
static constexpr float NORMAL_BOUND = 0.5f;
static constexpr float LOOSE_BOUND = 1.0f;

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

int compute_badness(float excess, float stretch, float shrink) {
    if (excess >= 0) {
        // Need to stretch
        if (stretch <= 0) {
            return (excess > 0.1f) ? INF_BAD + 1 : 0;  // Overfull if excess
        }
        float ratio = excess / stretch;
        if (ratio > 1.0f) {
            // Underfull - badness depends on how much
            float r3 = ratio * ratio * ratio;
            int bad = (int)(100.0f * r3 + 0.5f);
            return (bad > INF_BAD) ? INF_BAD + 1 : bad;
        }
        // Normal stretch
        float r3 = ratio * ratio * ratio;
        return (int)(100.0f * r3 + 0.5f);
    } else {
        // Need to shrink
        float shrink_needed = -excess;
        if (shrink <= 0) {
            return INF_BAD + 1;  // Overfull
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
    Fitness prev_fitness,
    int adj_demerits
) {
    int d;

    // Basic demerits formula
    if (penalty >= 0) {
        d = (line_penalty + badness) * (line_penalty + badness) + penalty * penalty;
    } else if (penalty > EJECT_PENALTY) {
        d = (line_penalty + badness) * (line_penalty + badness) - penalty * penalty;
    } else {
        d = (line_penalty + badness) * (line_penalty + badness);
    }

    // Adjacent fitness penalty
    int fitness_diff = abs((int)fitness - (int)prev_fitness);
    if (fitness_diff > 1) {
        d += adj_demerits;
    }

    return d;
}

// ============================================================================
// Line Width/Indent Helpers
// ============================================================================

float get_line_width(int line_number, const LineBreakParams& params) {
    // Check parshape first
    if (params.parshape_widths && line_number <= params.parshape_count) {
        return params.parshape_widths[line_number - 1];
    }

    // Check hanging indent
    if (params.hang_indent != 0) {
        bool in_hang;
        if (params.hang_after >= 0) {
            in_hang = (line_number > params.hang_after);
        } else {
            in_hang = true;  // Simplified
        }
        if (in_hang) {
            return params.hsize - fabsf(params.hang_indent);
        }
    }

    return params.hsize;
}

float get_line_indent(int line_number, const LineBreakParams& params) {
    // Check parshape first
    if (params.parshape_indents && line_number <= params.parshape_count) {
        return params.parshape_indents[line_number - 1];
    }

    // First line indent
    if (line_number == 1) {
        return params.par_indent;
    }

    // Hanging indent
    if (params.hang_indent > 0) {
        bool in_hang;
        if (params.hang_after >= 0) {
            in_hang = (line_number > params.hang_after);
        } else {
            in_hang = true;
        }
        if (in_hang) {
            return params.hang_indent;
        }
    }

    return 0;
}

// ============================================================================
// Break State - Internal algorithm state
// ============================================================================

struct BreakState {
    Arena* arena;
    const LineBreakParams* params;
    TexNode* hlist;

    // Item traversal
    TexNode** items;        // Array of child nodes
    int item_count;         // Number of items

    // Cumulative dimensions at each item
    CumulativeDims* cum_dims;

    // Break point tracking
    TexNode** break_nodes;  // Nodes where breaks can occur
    int* break_indices;     // Indices in items array
    int break_count;
    int break_capacity;

    // Active and passive lists
    ActiveNode* active_head;
    PassiveNode* passive_head;
    int passive_count;

    // Best solution found
    ActiveNode* best_active;
    int best_line_count;

    // Current threshold (tolerance)
    float threshold;
    bool second_pass;
};

// ============================================================================
// Node Allocation
// ============================================================================

static ActiveNode* alloc_active(BreakState* state) {
    ActiveNode* node = (ActiveNode*)arena_alloc(state->arena, sizeof(ActiveNode));
    memset(node, 0, sizeof(ActiveNode));
    return node;
}

static PassiveNode* alloc_passive(BreakState* state) {
    PassiveNode* node = (PassiveNode*)arena_alloc(state->arena, sizeof(PassiveNode));
    memset(node, 0, sizeof(PassiveNode));
    node->serial = state->passive_count++;
    node->link = state->passive_head;
    state->passive_head = node;
    return node;
}

// ============================================================================
// Find Break Points
// ============================================================================

static void find_break_points(BreakState* state) {
    state->break_capacity = 64;
    state->break_nodes = (TexNode**)arena_alloc(state->arena,
        state->break_capacity * sizeof(TexNode*));
    state->break_indices = (int*)arena_alloc(state->arena,
        state->break_capacity * sizeof(int));
    state->break_count = 0;

    auto add_break = [state](TexNode* node, int index) {
        if (state->break_count >= state->break_capacity) {
            int new_cap = state->break_capacity * 2;
            TexNode** new_nodes = (TexNode**)arena_alloc(state->arena,
                new_cap * sizeof(TexNode*));
            int* new_indices = (int*)arena_alloc(state->arena,
                new_cap * sizeof(int));
            memcpy(new_nodes, state->break_nodes, state->break_count * sizeof(TexNode*));
            memcpy(new_indices, state->break_indices, state->break_count * sizeof(int));
            state->break_nodes = new_nodes;
            state->break_indices = new_indices;
            state->break_capacity = new_cap;
        }
        state->break_nodes[state->break_count] = node;
        state->break_indices[state->break_count] = index;
        state->break_count++;
    };

    // Add starting break point (index -1, node null)
    add_break(nullptr, -1);

    TexNode* prev = nullptr;
    for (int i = 0; i < state->item_count; ++i) {
        TexNode* node = state->items[i];
        bool can_break = false;

        switch (node->node_class) {
            case NodeClass::Glue:
                // Can break before glue if preceded by non-glue
                if (prev && prev->node_class != NodeClass::Glue) {
                    can_break = true;
                }
                break;

            case NodeClass::Penalty:
                // Can break at penalty if < INF_PENALTY
                if (node->content.penalty.value < INF_PENALTY) {
                    can_break = true;
                }
                break;

            case NodeClass::Kern:
                // Can break after kern before glue (simplified)
                if (i + 1 < state->item_count &&
                    state->items[i + 1]->node_class == NodeClass::Glue) {
                    can_break = true;
                }
                break;

            case NodeClass::Disc:
                can_break = true;
                break;

            default:
                break;
        }

        if (can_break) {
            add_break(node, i);
        }

        prev = node;
    }

    // Add ending break point (force break at end)
    add_break(nullptr, state->item_count);

    log_debug("tex_linebreak: found %d break points", state->break_count);
}

// ============================================================================
// Compute Cumulative Dimensions
// ============================================================================

static void compute_cumulative_dims(BreakState* state) {
    state->cum_dims = (CumulativeDims*)arena_alloc(state->arena,
        (state->item_count + 1) * sizeof(CumulativeDims));

    CumulativeDims cum;
    state->cum_dims[0] = cum;  // Before any items

    for (int i = 0; i < state->item_count; ++i) {
        TexNode* node = state->items[i];

        switch (node->node_class) {
            case NodeClass::Char:
            case NodeClass::Ligature:
            case NodeClass::HBox:
            case NodeClass::VBox:
            case NodeClass::Rule:
                cum.add_width(node->width);
                break;

            case NodeClass::Glue:
                cum.add(node->content.glue.spec);
                break;

            case NodeClass::Kern:
                cum.add_width(node->content.kern.amount);
                break;

            case NodeClass::MathChar:
            case NodeClass::Fraction:
            case NodeClass::Radical:
            case NodeClass::Scripts:
                cum.add_width(node->width);
                break;

            default:
                // Penalties and other nodes don't contribute to width
                break;
        }

        state->cum_dims[i + 1] = cum;
    }
}

// ============================================================================
// Try Break - Core of Knuth-Plass
// ============================================================================

static void try_break(BreakState* state, int break_idx) {
    const LineBreakParams& params = *state->params;
    TexNode* break_node = state->break_nodes[break_idx];
    int item_idx = state->break_indices[break_idx];

    // Get penalty at this break
    int penalty = 0;
    if (break_node) {
        if (break_node->node_class == NodeClass::Penalty) {
            penalty = break_node->content.penalty.value;
        }
    } else if (break_idx == state->break_count - 1) {
        // End of paragraph - force break
        penalty = EJECT_PENALTY;
    }

    // Skip if infinite penalty
    if (penalty >= INF_PENALTY) return;

    // Get cumulative dimensions at this break
    int cur_cum_idx = (item_idx < 0) ? 0 : item_idx;
    if (cur_cum_idx > state->item_count) cur_cum_idx = state->item_count;
    const CumulativeDims& cur = state->cum_dims[cur_cum_idx];

    // Try breaking from each active node
    ActiveNode* prev_active = nullptr;
    ActiveNode* active = state->active_head;
    ActiveNode* best_for_fitness[4] = {nullptr, nullptr, nullptr, nullptr};
    int best_demerits[4] = {AWFUL_BAD, AWFUL_BAD, AWFUL_BAD, AWFUL_BAD};

    while (active) {
        // Get dimensions of line from active to current break
        int line_number = active->line_number + 1;
        float target_width = get_line_width(line_number, params);
        float indent = get_line_indent(line_number, params);

        // Compute line dimensions
        // Get start position: for initial node (null break_passive), start at 0
        // Otherwise, start after the previous break
        int start_position = 0;
        if (active->break_passive) {
            int prev_item_idx = state->break_indices[active->break_passive->position];
            start_position = (prev_item_idx < 0) ? 0 : prev_item_idx;
        }
        const CumulativeDims& start = state->cum_dims[start_position];

        float line_width = cur.width - start.width + indent;
        float line_stretch = cur.stretch - start.stretch;
        float line_shrink = cur.shrink - start.shrink;

        // Handle infinite glue
        int stretch_order = 0;
        float total_inf_stretch = 0;
        if (cur.stretch_filll - start.stretch_filll > 0) {
            stretch_order = 3;
            total_inf_stretch = (float)(cur.stretch_filll - start.stretch_filll);
        } else if (cur.stretch_fill - start.stretch_fill > 0) {
            stretch_order = 2;
            total_inf_stretch = (float)(cur.stretch_fill - start.stretch_fill);
        } else if (cur.stretch_fil - start.stretch_fil > 0) {
            stretch_order = 1;
            total_inf_stretch = (float)(cur.stretch_fil - start.stretch_fil);
        }

        // Add right/left skip
        line_width += params.left_skip.space + params.right_skip.space;
        if (params.left_skip.stretch_order == GlueOrder::Normal) {
            line_stretch += params.left_skip.stretch;
        }
        if (params.right_skip.stretch_order == GlueOrder::Normal) {
            line_stretch += params.right_skip.stretch;
        }
        line_shrink += params.left_skip.shrink + params.right_skip.shrink;

        // For last line, add parfillskip (infinite stretch)
        // This allows the last line to be short
        bool is_last_line = (break_idx == state->break_count - 1);
        if (is_last_line) {
            // Add fil stretch - treated as infinite
            if (stretch_order == 0) {
                stretch_order = 1;  // Promote to fil
                total_inf_stretch = 1.0f;
            }
        }

        float excess = target_width - line_width;

        // Compute badness
        int badness;
        float ratio = 0;
        if (stretch_order > 0 && excess >= 0) {
            // Infinite glue and line is short - badness is 0
            badness = 0;
            ratio = excess / (total_inf_stretch * 100.0f);  // Scaled
        } else {
            badness = compute_badness(excess, line_stretch, line_shrink);
            if (excess >= 0 && line_stretch > 0) {
                ratio = excess / line_stretch;
            } else if (excess < 0 && line_shrink > 0) {
                ratio = excess / line_shrink;
            }
        }

        // Check if line is overfull - deactivate if we can't possibly find a good break
        // Only deactivate if line is WIDER than target (overfull), not shorter (underfull)
        // Also, never deactivate the last active node - keep trying
        if (badness > INF_BAD && line_width > target_width && active->link != nullptr) {
            // Line is overfull - deactivate since adding more will only make it worse
            if (prev_active) {
                prev_active->link = active->link;
            } else {
                state->active_head = active->link;
            }
            active = active->link;
            continue;
        }

        // Check if this break is feasible
        bool feasible = (badness <= state->threshold) || (penalty <= EJECT_PENALTY);


        if (feasible) {
            // Compute fitness
            Fitness fitness = compute_fitness(ratio);

            // Compute demerits
            Fitness prev_fitness = active->fitness;
            int demerits = compute_demerits(
                badness, penalty, params.line_penalty,
                fitness, prev_fitness, params.adj_demerits
            );

            // Add hyphen penalties
            if (break_node && break_node->node_class == NodeClass::Disc) {
                if (active->break_type == BreakType::Hyphen ||
                    active->break_type == BreakType::Explicit) {
                    demerits += params.double_hyphen_demerits;
                }
            }

            int total_dem = active->total_demerits + demerits;

            // Track best for each fitness class
            int f = (int)fitness;
            if (total_dem < best_demerits[f]) {
                best_demerits[f] = total_dem;
                best_for_fitness[f] = active;
            }
        }

        prev_active = active;
        active = active->link;
    }

    // Create new active nodes for best candidates
    for (int f = 0; f < 4; ++f) {
        if (best_demerits[f] < AWFUL_BAD) {
            ActiveNode* from = best_for_fitness[f];

            // Create passive node for this break
            PassiveNode* passive = alloc_passive(state);
            passive->break_node = break_node;
            passive->position = break_idx;
            if (from->break_passive) {
                passive->prev_break = from->break_passive;
            }

            // Create active node
            ActiveNode* new_active = alloc_active(state);
            new_active->break_passive = passive;
            new_active->line_number = from->line_number + 1;
            new_active->fitness = (Fitness)f;
            new_active->total_demerits = best_demerits[f];

            // Set break type
            if (break_node) {
                if (break_node->node_class == NodeClass::Disc) {
                    new_active->break_type = BreakType::Hyphen;
                } else if (break_node->node_class == NodeClass::Penalty) {
                    new_active->break_type = BreakType::Penalty;
                } else {
                    new_active->break_type = BreakType::Ordinary;
                }
            } else {
                new_active->break_type = BreakType::Ordinary;
            }

            // Update cumulative dimensions
            new_active->total_width = cur.width;
            new_active->total_stretch = cur.stretch;
            new_active->total_shrink = cur.shrink;
            new_active->total_stretch_fil = cur.stretch_fil;
            new_active->total_stretch_fill = cur.stretch_fill;
            new_active->total_stretch_filll = cur.stretch_filll;

            // Insert at head of active list
            new_active->link = state->active_head;
            state->active_head = new_active;

            // Check if this is end of paragraph
            if (break_idx == state->break_count - 1) {
                if (!state->best_active ||
                    best_demerits[f] < state->best_active->total_demerits) {
                    state->best_active = new_active;
                    state->best_line_count = new_active->line_number;
                }
            }
        }
    }
}

// ============================================================================
// Main Algorithm
// ============================================================================

LineBreakResult break_paragraph(
    TexNode* hlist,
    const LineBreakParams& params,
    Arena* arena
) {
    LineBreakResult result;

    if (hlist->node_class != NodeClass::HList) {
        log_error("tex_linebreak: expected HList node");
        return result;
    }

    // Initialize state
    BreakState state = {};
    state.arena = arena;
    state.params = &params;
    state.hlist = hlist;

    // Build items array from hlist children
    int count = 0;
    for (TexNode* c = hlist->first_child; c; c = c->next_sibling) count++;

    if (count == 0) {
        // Empty paragraph
        result.breaks = nullptr;
        result.line_count = 0;
        result.success = true;
        return result;
    }

    state.items = (TexNode**)arena_alloc(arena, count * sizeof(TexNode*));
    state.item_count = count;

    int i = 0;
    for (TexNode* c = hlist->first_child; c; c = c->next_sibling) {
        state.items[i++] = c;
    }

    // Find break points
    find_break_points(&state);

    // Compute cumulative dimensions
    compute_cumulative_dims(&state);

    log_debug("tex_linebreak: %d items, %d breaks, total width=%.1f, stretch=%.1f, target=%.1f",
        state.item_count, state.break_count,
        state.cum_dims[state.item_count].width,
        state.cum_dims[state.item_count].stretch,
        params.hsize);

    // Initialize active list with starting node
    state.active_head = alloc_active(&state);
    state.active_head->line_number = 0;
    state.active_head->fitness = Fitness::Normal;
    state.active_head->total_demerits = 0;

    state.best_active = nullptr;
    state.threshold = params.pretolerance;
    state.second_pass = false;

    // First pass
    if (params.pretolerance >= 0) {
        log_debug("tex_linebreak: first pass with tolerance %.1f", params.pretolerance);
        for (int b = 1; b < state.break_count; ++b) {
            try_break(&state, b);
        }
    }

    // Second pass if needed
    if (!state.best_active) {
        log_debug("tex_linebreak: second pass with tolerance %.1f", params.tolerance);
        state.second_pass = true;
        state.threshold = params.tolerance;

        // Reset active list
        state.active_head = alloc_active(&state);
        state.active_head->line_number = 0;
        state.active_head->fitness = Fitness::Normal;
        state.active_head->total_demerits = 0;

        for (int b = 1; b < state.break_count; ++b) {
            try_break(&state, b);
        }
    }

    // Emergency pass if still no solution
    if (!state.best_active && params.emergency_stretch > 0) {
        log_debug("tex_linebreak: emergency pass with stretch %.1f", params.emergency_stretch);
        state.threshold = 10000;  // Accept anything

        state.active_head = alloc_active(&state);
        state.active_head->line_number = 0;
        state.active_head->fitness = Fitness::Normal;
        state.active_head->total_demerits = 0;

        for (int b = 1; b < state.break_count; ++b) {
            try_break(&state, b);
        }
    }

    // Extract result
    if (state.best_active) {
        int line_count = state.best_active->line_number;
        result.breaks = (TexNode**)arena_alloc(arena, line_count * sizeof(TexNode*));
        result.line_count = line_count;
        result.total_demerits = state.best_active->total_demerits;
        result.success = true;

        // Walk back through passive nodes
        PassiveNode* p = state.best_active->break_passive;
        int idx = line_count - 1;
        while (p && idx >= 0) {
            result.breaks[idx] = p->break_node;
            p = p->prev_break;
            idx--;
        }

        log_debug("tex_linebreak: found solution with %d lines, demerits=%d",
            line_count, result.total_demerits);
    } else {
        log_error("tex_linebreak: no valid solution found");

        // Emergency: single line
        result.breaks = (TexNode**)arena_alloc(arena, sizeof(TexNode*));
        result.breaks[0] = nullptr;
        result.line_count = 1;
        result.success = false;
    }

    return result;
}

// ============================================================================
// Build Lines from Breaks
// ============================================================================

TexNode** build_lines_from_breaks(
    TexNode* hlist,
    const LineBreakResult& result,
    const LineBreakParams& params,
    Arena* arena
) {
    if (result.line_count == 0) return nullptr;

    TexNode** lines = (TexNode**)arena_alloc(arena, result.line_count * sizeof(TexNode*));

    // Build items array
    int count = 0;
    for (TexNode* c = hlist->first_child; c; c = c->next_sibling) count++;

    TexNode** items = (TexNode**)arena_alloc(arena, count * sizeof(TexNode*));
    int i = 0;
    for (TexNode* c = hlist->first_child; c; c = c->next_sibling) {
        items[i++] = c;
    }

    // Find break positions in items array
    int* break_indices = (int*)arena_alloc(arena, result.line_count * sizeof(int));
    for (int l = 0; l < result.line_count; ++l) {
        if (result.breaks[l] == nullptr) {
            break_indices[l] = count;  // End of paragraph
        } else {
            // Find index of break node
            for (int j = 0; j < count; ++j) {
                if (items[j] == result.breaks[l]) {
                    break_indices[l] = j;
                    break;
                }
            }
        }
    }

    // Build each line
    int start_idx = 0;
    for (int l = 0; l < result.line_count; ++l) {
        int end_idx = break_indices[l];
        int line_number = l + 1;

        float target_width = get_line_width(line_number, params);
        float indent = get_line_indent(line_number, params);

        // Create HBox for line
        TexNode* line = make_hbox(arena, target_width);

        // Add indent if needed
        if (indent > 0) {
            TexNode* indent_kern = make_kern(arena, indent);
            line->append_child(indent_kern);
        }

        // Copy items, skipping glue at start and penalties
        bool skip_leading_glue = true;
        for (int j = start_idx; j < end_idx && j < count; ++j) {
            TexNode* item = items[j];

            // Skip penalties
            if (item->node_class == NodeClass::Penalty) continue;

            // Skip leading glue
            if (skip_leading_glue && item->node_class == NodeClass::Glue) continue;
            skip_leading_glue = false;

            // Clone the node (or just reference for now)
            // For a real implementation, we'd clone nodes
            line->append_child(item);
        }

        // Set line dimensions
        HListDimensions dim = measure_hlist(line);
        line->height = dim.height;
        line->depth = dim.depth;
        line->width = dim.width;
        set_hlist_glue(line, target_width);

        lines[l] = line;
        start_idx = end_idx;

        // Skip the break item itself and any following glue
        while (start_idx < count &&
               (items[start_idx]->node_class == NodeClass::Glue ||
                items[start_idx]->node_class == NodeClass::Penalty)) {
            start_idx++;
        }
    }

    return lines;
}

// ============================================================================
// Build Paragraph VList
// ============================================================================

TexNode* build_paragraph_vlist(
    TexNode** lines,
    int line_count,
    float baseline_skip,
    Arena* arena
) {
    TexNode* vlist = make_vlist(arena);

    for (int i = 0; i < line_count; ++i) {
        vlist->append_child(lines[i]);

        // Add baseline skip between lines
        if (i < line_count - 1) {
            float skip = baseline_skip - lines[i]->depth - lines[i + 1]->height;
            if (skip > 0) {
                TexNode* glue = make_glue(arena, Glue::flexible(skip, skip * 0.1f, skip * 0.05f), "baselineskip");
                vlist->append_child(glue);
            }
        }
    }

    // Compute vlist dimensions
    float total_height = 0;
    float max_depth = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        total_height += c->height + c->depth;
        if (c == vlist->last_child) {
            max_depth = c->depth;
            total_height -= c->depth;
        }
    }
    vlist->height = total_height;
    vlist->depth = max_depth;

    return vlist;
}

// ============================================================================
// Convenience Function
// ============================================================================

TexNode* typeset_paragraph(
    TexNode* hlist,
    const LineBreakParams& params,
    float baseline_skip,
    Arena* arena
) {
    LineBreakResult result = break_paragraph(hlist, params, arena);
    if (!result.success && result.line_count == 0) {
        return nullptr;
    }

    TexNode** lines = build_lines_from_breaks(hlist, result, params, arena);
    return build_paragraph_vlist(lines, result.line_count, baseline_skip, arena);
}

// ============================================================================
// Debugging
// ============================================================================

void dump_active_list(const ActiveNode* head) {
    log_debug("Active list:");
    int n = 0;
    for (const ActiveNode* a = head; a; a = a->link) {
        log_debug("  [%d] line=%d fitness=%d demerits=%d",
            n++, a->line_number, (int)a->fitness, a->total_demerits);
    }
}

void dump_line_break_result(const LineBreakResult& result) {
    log_debug("Line break result: %d lines, demerits=%d, success=%s",
        result.line_count, result.total_demerits,
        result.success ? "true" : "false");
    for (int i = 0; i < result.line_count; ++i) {
        log_debug("  Line %d: break at %p", i + 1, (void*)result.breaks[i]);
    }
}

} // namespace tex
