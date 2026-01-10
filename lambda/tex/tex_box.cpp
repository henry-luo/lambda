// tex_box.cpp - Implementation of TeX box operations
//
// Box setting, natural dimension computation, and inter-atom spacing

#include "tex_box.hpp"
#include "../../lib/log.h"
#include <cmath>
#include <algorithm>

namespace tex {

// ============================================================================
// Inter-Atom Spacing Tables (TeXBook Chapter 18, p.170)
// ============================================================================

// Values: 0=none, 3=thin, 4=medium, 5=thick
// Rows: left atom type, Columns: right atom type
static const int SPACING_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */   {0,  3,   4,   5,   0,   0,    0,    3},
    /* Op  */   {3,  3,   0,   5,   0,   0,    0,    3},
    /* Bin */   {4,  4,   0,   0,   4,   0,    0,    4},
    /* Rel */   {5,  5,   0,   0,   5,   0,    0,    5},
    /* Open*/   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/  {0,  3,   4,   5,   0,   0,    0,    3},
    /* Punct*/  {3,  3,   0,   3,   3,   0,    3,    3},
    /* Inner*/  {3,  3,   4,   5,   3,   0,    3,    3},
};

// Tight spacing for script/scriptscript styles (most spacing removed)
static const int TIGHT_SPACING_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */   {0,  3,   0,   0,   0,   0,    0,    0},
    /* Op  */   {3,  3,   0,   0,   0,   0,    0,    0},
    /* Bin */   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Rel */   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Open*/   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/  {0,  3,   0,   0,   0,   0,    0,    0},
    /* Punct*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Inner*/  {0,  3,   0,   0,   0,   0,    0,    0},
};

int get_inter_atom_spacing(AtomType left, AtomType right, bool tight) {
    int l = (int)left;
    int r = (int)right;

    // Special types don't contribute to spacing
    if (l >= 8 || r >= 8) return 0;

    return tight ? TIGHT_SPACING_TABLE[l][r] : SPACING_TABLE[l][r];
}

// ============================================================================
// HList Natural Dimensions
// ============================================================================

void compute_hlist_natural_dims(TexBox* hlist) {
    if (hlist->content_type != BoxContentType::HList) {
        return;
    }

    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;

    for (int i = 0; i < hlist->content.list.count; ++i) {
        TexBox* child = hlist->content.list.children[i];

        // Apply child's scale
        float child_width = child->width * child->scale;
        float child_height = child->height * child->scale;
        float child_depth = child->depth * child->scale;

        // Set child's x position
        child->x = total_width;
        child->y = 0;  // Baseline aligned

        total_width += child_width;
        max_height = std::max(max_height, child_height - child->y);
        max_depth = std::max(max_depth, child_depth + child->y);
    }

    hlist->width = total_width;
    hlist->height = max_height;
    hlist->depth = max_depth;
}

// ============================================================================
// VList Natural Dimensions
// ============================================================================

void compute_vlist_natural_dims(TexBox* vlist) {
    if (vlist->content_type != BoxContentType::VList) {
        return;
    }

    float total_height = 0;
    float max_width = 0;

    // VList stacks children vertically
    // First child is at top, reference point is at baseline of first child
    for (int i = 0; i < vlist->content.list.count; ++i) {
        TexBox* child = vlist->content.list.children[i];

        float child_width = child->width * child->scale;
        float child_total_height = child->total_height() * child->scale;

        // Set child's position
        child->x = 0;

        if (i == 0) {
            // First child: its baseline is at y=0
            child->y = 0;
            total_height = child->depth;
        } else {
            // Subsequent children: stacked below
            child->y = total_height + child->height;
            total_height = child->y + child->depth;
        }

        max_width = std::max(max_width, child_width);
    }

    vlist->width = max_width;
    // Height is from reference point to top
    if (vlist->content.list.count > 0) {
        TexBox* first = vlist->content.list.children[0];
        vlist->height = first->height;
        vlist->depth = total_height;
    }
}

// ============================================================================
// Set HList Width (Glue Distribution)
// ============================================================================

void set_hlist_width(TexBox* hlist, float target_width, Arena* arena) {
    if (hlist->content_type != BoxContentType::HList) {
        return;
    }

    // First compute natural dimensions
    compute_hlist_natural_dims(hlist);

    float natural_width = hlist->width;
    float excess = target_width - natural_width;

    if (std::abs(excess) < 0.01f) {
        // Already at target width
        hlist->width = target_width;
        return;
    }

    // Collect total stretch/shrink at each order
    float total_stretch[4] = {0, 0, 0, 0};
    float total_shrink[4] = {0, 0, 0, 0};

    for (int i = 0; i < hlist->content.list.count; ++i) {
        TexBox* child = hlist->content.list.children[i];
        if (child->content_type == BoxContentType::Glue) {
            const Glue& g = child->content.glue;
            total_stretch[(int)g.stretch_order] += g.stretch;
            total_shrink[(int)g.shrink_order] += g.shrink;
        }
    }

    // Determine which order of infinity to use
    GlueOrder stretch_order = GlueOrder::Normal;
    GlueOrder shrink_order = GlueOrder::Normal;

    for (int i = 3; i >= 0; --i) {
        if (total_stretch[i] > 0) {
            stretch_order = (GlueOrder)i;
            break;
        }
    }
    for (int i = 3; i >= 0; --i) {
        if (total_shrink[i] > 0) {
            shrink_order = (GlueOrder)i;
            break;
        }
    }

    // Compute glue ratio
    GlueSetInfo glue_set = {};

    if (excess >= 0) {
        // Stretching
        glue_set.is_stretching = true;
        glue_set.order = stretch_order;
        float total = total_stretch[(int)stretch_order];
        glue_set.ratio = (total > 0) ? excess / total : 0;
    } else {
        // Shrinking
        glue_set.is_stretching = false;
        glue_set.order = shrink_order;
        float total = total_shrink[(int)shrink_order];
        glue_set.ratio = (total > 0) ? -excess / total : 0;

        // Cap shrink ratio at 1.0 to prevent negative glue
        if (glue_set.ratio > 1.0f) {
            glue_set.ratio = 1.0f;
            log_debug("tex_box: overfull hbox, shrink ratio capped at 1.0");
        }
    }

    // Apply glue setting to all children
    float current_x = 0;

    for (int i = 0; i < hlist->content.list.count; ++i) {
        TexBox* child = hlist->content.list.children[i];

        child->x = current_x;
        child->y = 0;

        float child_width = child->width * child->scale;

        if (child->content_type == BoxContentType::Glue) {
            // Compute actual glue width
            const Glue& g = child->content.glue;
            child_width = glue_set.compute_size(g) * child->scale;
            child->width = child_width / child->scale;  // Update child's width
        }

        current_x += child_width;
    }

    hlist->width = target_width;
    hlist->content.list.glue_set = glue_set;
}

// ============================================================================
// Set VList Height (Glue Distribution)
// ============================================================================

void set_vlist_height(TexBox* vlist, float target_height, Arena* arena) {
    if (vlist->content_type != BoxContentType::VList) {
        return;
    }

    // First compute natural dimensions
    compute_vlist_natural_dims(vlist);

    float natural_height = vlist->height + vlist->depth;
    float excess = target_height - natural_height;

    if (std::abs(excess) < 0.01f) {
        return;
    }

    // Collect total stretch/shrink at each order
    float total_stretch[4] = {0, 0, 0, 0};
    float total_shrink[4] = {0, 0, 0, 0};

    for (int i = 0; i < vlist->content.list.count; ++i) {
        TexBox* child = vlist->content.list.children[i];
        if (child->content_type == BoxContentType::Glue) {
            const Glue& g = child->content.glue;
            total_stretch[(int)g.stretch_order] += g.stretch;
            total_shrink[(int)g.shrink_order] += g.shrink;
        }
    }

    // Determine which order of infinity to use
    GlueOrder stretch_order = GlueOrder::Normal;
    GlueOrder shrink_order = GlueOrder::Normal;

    for (int i = 3; i >= 0; --i) {
        if (total_stretch[i] > 0) {
            stretch_order = (GlueOrder)i;
            break;
        }
    }
    for (int i = 3; i >= 0; --i) {
        if (total_shrink[i] > 0) {
            shrink_order = (GlueOrder)i;
            break;
        }
    }

    // Compute glue ratio
    GlueSetInfo glue_set = {};

    if (excess >= 0) {
        glue_set.is_stretching = true;
        glue_set.order = stretch_order;
        float total = total_stretch[(int)stretch_order];
        glue_set.ratio = (total > 0) ? excess / total : 0;
    } else {
        glue_set.is_stretching = false;
        glue_set.order = shrink_order;
        float total = total_shrink[(int)shrink_order];
        glue_set.ratio = (total > 0) ? -excess / total : 0;
        if (glue_set.ratio > 1.0f) {
            glue_set.ratio = 1.0f;
        }
    }

    // Apply glue setting to all children
    float current_y = 0;

    for (int i = 0; i < vlist->content.list.count; ++i) {
        TexBox* child = vlist->content.list.children[i];

        float child_height = child->height * child->scale;
        float child_depth = child->depth * child->scale;

        if (i == 0) {
            child->y = 0;  // First child baseline at reference point
            current_y = child_depth;
        } else {
            // Check if previous item was glue
            TexBox* prev = vlist->content.list.children[i - 1];
            if (prev->content_type == BoxContentType::Glue) {
                // Apply glue setting
                float glue_size = glue_set.compute_size(prev->content.glue);
                current_y += glue_size;
            }

            child->y = current_y + child_height;
            current_y = child->y + child_depth;
        }

        child->x = 0;
    }

    // Update vlist dimensions
    if (vlist->content.list.count > 0) {
        TexBox* first = vlist->content.list.children[0];
        vlist->height = first->height;
        vlist->depth = current_y;
    }

    vlist->content.list.glue_set = glue_set;
}

} // namespace tex
