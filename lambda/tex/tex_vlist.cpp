// tex_vlist.cpp - VList Builder Implementation
//
// Reference: TeXBook Chapters 12-15

#include "tex_vlist.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// VList Context Initialization
// ============================================================================

void init_vlist_context(VListContext& ctx, float text_width) {
    // Set line breaking width
    ctx.line_params = LineBreakParams::defaults();
    ctx.line_params.hsize = text_width;

    // Set default fonts (using CMR family)
    ctx.body_font = FontSpec("cmr10", 10.0f, nullptr, 0);

    // Headings use larger sizes
    ctx.heading1_font = FontSpec("cmr10", 14.4f, nullptr, 0);  // \Large
    ctx.heading2_font = FontSpec("cmr10", 12.0f, nullptr, 0);  // \large
    ctx.heading3_font = FontSpec("cmr10", 10.0f, nullptr, 0);  // \normalsize bold would be cmbx10
}

// ============================================================================
// VList Building
// ============================================================================

TexNode* begin_vlist(VListContext& ctx) {
    ctx.current_vlist = make_vlist(ctx.arena);
    ctx.last_line = nullptr;
    ctx.prev_depth = 0;
    return ctx.current_vlist;
}

TexNode* add_paragraph(
    VListContext& ctx,
    const char* text,
    int text_len
) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    // Add parskip before non-first paragraphs
    if (ctx.last_line) {
        TexNode* parskip = make_glue(ctx.arena, ctx.params.par_skip, "parskip");
        ctx.current_vlist->append_child(parskip);
    }

    // Build HList from text
    HListContext hctx(ctx.arena, ctx.fonts);
    set_font(hctx, ctx.body_font.name, ctx.body_font.size_pt);

    TexNode* hlist = text_to_hlist(text, text_len, hctx);
    if (!hlist) {
        log_error("tex_vlist: failed to build HList from paragraph text");
        return nullptr;
    }

    // Typeset paragraph (line breaking + vlist)
    TexNode* para_vlist = typeset_paragraph(
        hlist,
        ctx.line_params,
        ctx.params.baseline_skip,
        ctx.arena
    );

    if (!para_vlist) {
        log_error("tex_vlist: failed to typeset paragraph");
        return nullptr;
    }

    // Append all children of para_vlist to our main vlist
    // (We merge rather than nest to keep the vlist flat)
    bool first_line = true;
    for (TexNode* child = para_vlist->first_child; child; ) {
        TexNode* next = child->next_sibling;

        // Remove from para_vlist
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
        child->parent = nullptr;

        // Compute interline glue if needed
        if (child->node_class == NodeClass::HBox) {
            if (ctx.last_line && first_line) {
                // Need to add interline glue before first line of new paragraph
                TexNode* interline = compute_interline_glue(
                    ctx.last_line, child, ctx.params, ctx.arena
                );
                if (interline) {
                    ctx.current_vlist->append_child(interline);
                }
            }
            ctx.last_line = child;
            ctx.prev_depth = child->depth;
            first_line = false;
        }

        ctx.current_vlist->append_child(child);
        child = next;
    }

    return para_vlist;
}

TexNode* add_heading(
    VListContext& ctx,
    const char* text,
    int text_len,
    int level
) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    // Select font based on level
    FontSpec font;
    switch (level) {
        case 1: font = ctx.heading1_font; break;
        case 2: font = ctx.heading2_font; break;
        default: font = ctx.heading3_font; break;
    }

    // Add space above heading (unless at start)
    if (ctx.last_line) {
        TexNode* above_skip = make_glue(ctx.arena, ctx.params.above_section_skip, "abovesectionskip");
        ctx.current_vlist->append_child(above_skip);
    }

    // Build heading HList
    HListContext hctx(ctx.arena, ctx.fonts);
    set_font(hctx, font.name, font.size_pt);

    TexNode* hlist = text_to_hlist(text, text_len, hctx);
    if (!hlist) {
        log_error("tex_vlist: failed to build heading HList");
        return nullptr;
    }

    // Measure and create HBox
    HListDimensions dim = measure_hlist(hlist);
    TexNode* line = make_hbox(ctx.arena, ctx.line_params.hsize);
    line->height = dim.height;
    line->depth = dim.depth;
    line->width = dim.width;

    // Copy children from hlist to line
    for (TexNode* child = hlist->first_child; child; ) {
        TexNode* next = child->next_sibling;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
        child->parent = nullptr;
        line->append_child(child);
        child = next;
    }

    // Add interline glue if needed
    if (ctx.last_line) {
        TexNode* interline = compute_interline_glue(
            ctx.last_line, line, ctx.params, ctx.arena
        );
        if (interline) {
            ctx.current_vlist->append_child(interline);
        }
    }

    ctx.current_vlist->append_child(line);
    ctx.last_line = line;
    ctx.prev_depth = line->depth;

    // Add space below heading
    TexNode* below_skip = make_glue(ctx.arena, ctx.params.below_section_skip, "belowsectionskip");
    ctx.current_vlist->append_child(below_skip);

    return line;
}

void add_display_math(VListContext& ctx, TexNode* math_list) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    // Add space above display
    Glue above = ctx.params.above_display_skip;
    // Use short skip if previous line was short
    // (TeXBook: use short skip if previous line's right edge is to the left of display center)
    TexNode* above_glue = make_glue(ctx.arena, above, "abovedisplayskip");
    ctx.current_vlist->append_child(above_glue);

    // Center the math
    float math_width = math_list->width;
    float line_width = ctx.line_params.hsize;

    TexNode* centered = center_line(math_list, line_width, ctx.arena);
    if (centered) {
        ctx.current_vlist->append_child(centered);
        ctx.last_line = centered;
        ctx.prev_depth = centered->depth;
    }

    // Add space below display
    TexNode* below_glue = make_glue(ctx.arena, ctx.params.below_display_skip, "belowdisplayskip");
    ctx.current_vlist->append_child(below_glue);
}

void add_vspace(VListContext& ctx, const Glue& space) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    TexNode* glue = make_glue(ctx.arena, space, "vspace");
    ctx.current_vlist->append_child(glue);
}

void add_hrule(VListContext& ctx, float thickness, float width) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    float actual_width = (width < 0) ? ctx.line_params.hsize : width;

    // Rules in vertical mode have running width (full page width)
    TexNode* rule = make_rule(ctx.arena, actual_width, thickness, 0);
    ctx.current_vlist->append_child(rule);

    ctx.prev_depth = 0;
}

void add_line(VListContext& ctx, TexNode* line) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    // Compute interline glue
    if (ctx.last_line) {
        TexNode* interline = compute_interline_glue(
            ctx.last_line, line, ctx.params, ctx.arena
        );
        if (interline) {
            ctx.current_vlist->append_child(interline);
        }
    }

    ctx.current_vlist->append_child(line);
    ctx.last_line = line;
    ctx.prev_depth = line->depth;
}

void add_raw(VListContext& ctx, TexNode* node) {
    if (!ctx.current_vlist) {
        begin_vlist(ctx);
    }

    ctx.current_vlist->append_child(node);

    // Update tracking
    if (node->node_class == NodeClass::HBox ||
        node->node_class == NodeClass::HList) {
        ctx.last_line = node;
        ctx.prev_depth = node->depth;
    }
}

TexNode* end_vlist(VListContext& ctx) {
    if (!ctx.current_vlist) {
        return nullptr;
    }

    // Compute final dimensions
    VListDimensions dim = measure_vlist(ctx.current_vlist);
    ctx.current_vlist->height = dim.height;
    ctx.current_vlist->depth = dim.depth;

    TexNode* result = ctx.current_vlist;
    ctx.current_vlist = nullptr;
    ctx.last_line = nullptr;
    ctx.prev_depth = 0;

    return result;
}

// ============================================================================
// Batch Document Building
// ============================================================================

TexNode* build_document(
    ContentItem* items,
    int item_count,
    VListContext& ctx
) {
    begin_vlist(ctx);

    for (int i = 0; i < item_count; ++i) {
        const ContentItem& item = items[i];

        switch (item.type) {
            case ContentType::Paragraph:
                add_paragraph(ctx, item.data.paragraph.text, item.data.paragraph.text_len);
                break;

            case ContentType::Heading1:
            case ContentType::Heading2:
            case ContentType::Heading3:
                add_heading(ctx, item.data.heading.text, item.data.heading.text_len,
                           item.data.heading.level);
                break;

            case ContentType::DisplayMath:
                add_display_math(ctx, item.data.display.math_list);
                break;

            case ContentType::Rule:
                add_hrule(ctx, item.data.rule.thickness, item.data.rule.width);
                break;

            case ContentType::VSpace:
                add_vspace(ctx, item.data.vspace.space);
                break;

            case ContentType::Raw:
                add_raw(ctx, item.data.raw.node);
                break;
        }
    }

    return end_vlist(ctx);
}

// ============================================================================
// VList Measurements
// ============================================================================

VListDimensions measure_vlist(TexNode* vlist) {
    VListDimensions dim = {};

    if (!vlist || !vlist->first_child) {
        return dim;
    }

    float total = 0;
    TexNode* last = nullptr;

    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        last = c;

        if (c->node_class == NodeClass::Glue) {
            const Glue& g = c->content.glue.spec;
            total += g.space;

            switch (g.stretch_order) {
                case GlueOrder::Normal: dim.stretch += g.stretch; break;
                case GlueOrder::Fil: dim.stretch_fil += (int)g.stretch; break;
                case GlueOrder::Fill: dim.stretch_fill += (int)g.stretch; break;
                case GlueOrder::Filll: dim.stretch_filll += (int)g.stretch; break;
            }
            dim.shrink += g.shrink;
        } else if (c->node_class == NodeClass::Kern) {
            total += c->content.kern.amount;
        } else {
            // Box or line
            total += c->height + c->depth;
        }
    }

    // The vlist's height is total minus last item's depth
    // The depth is the last item's depth
    if (last) {
        dim.depth = last->depth;
        dim.height = total - dim.depth;
    } else {
        dim.height = total;
    }

    dim.natural_height = dim.height;

    return dim;
}

// ============================================================================
// VList Glue Setting
// ============================================================================

void set_vlist_glue(TexNode* vlist, float target_height) {
    VListDimensions dim = measure_vlist(vlist);

    float excess = target_height - dim.natural_height;

    // Determine glue order and ratio
    GlueOrder order = GlueOrder::Normal;
    float total_inf = 0;

    if (excess >= 0) {
        // Stretching
        if (dim.stretch_filll > 0) {
            order = GlueOrder::Filll;
            total_inf = (float)dim.stretch_filll;
        } else if (dim.stretch_fill > 0) {
            order = GlueOrder::Fill;
            total_inf = (float)dim.stretch_fill;
        } else if (dim.stretch_fil > 0) {
            order = GlueOrder::Fil;
            total_inf = (float)dim.stretch_fil;
        } else {
            order = GlueOrder::Normal;
            total_inf = dim.stretch;
        }
    } else {
        // Shrinking (always finite order)
        order = GlueOrder::Normal;
        total_inf = dim.shrink;
    }

    float ratio = (total_inf > 0) ? excess / total_inf : 0;

    // Store glue set info
    vlist->content.list.glue_set.order = order;
    vlist->content.list.glue_set.ratio = ratio;
    vlist->content.list.glue_set.is_stretch = (excess >= 0);

    // Update vlist dimensions
    vlist->height = target_height;
}

// ============================================================================
// Inter-line Spacing
// ============================================================================

TexNode* compute_interline_glue(
    TexNode* prev_line,
    TexNode* curr_line,
    const VListParams& params,
    Arena* arena
) {
    if (!prev_line || !curr_line) {
        return nullptr;
    }

    // TeX baseline skip algorithm:
    // desired = baselineskip - prev_depth - curr_height
    // if desired >= lineskiplimit: use desired
    // else: use lineskip

    float prev_depth = prev_line->depth;
    float curr_height = curr_line->height;

    float desired = params.baseline_skip - prev_depth - curr_height;

    if (desired >= params.line_skip_limit) {
        // Use flexible glue based on baseline skip
        Glue g = Glue::flexible(desired, desired * 0.1f, desired * 0.05f);
        return make_glue(arena, g, "baselineskip");
    } else {
        // Lines too close, use fixed lineskip
        return make_kern(arena, params.line_skip);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

TexNode* center_line(TexNode* content, float line_width, Arena* arena) {
    TexNode* line = make_hbox(arena, line_width);

    // Add hfil before content
    line->append_child(make_glue(arena, hfil_glue(), "hfil"));

    // Add content
    line->append_child(content);

    // Add hfil after content
    line->append_child(make_glue(arena, hfil_glue(), "hfil"));

    // Set dimensions
    line->width = line_width;
    line->height = content->height;
    line->depth = content->depth;

    return line;
}

TexNode* right_align_line(TexNode* content, float line_width, Arena* arena) {
    TexNode* line = make_hbox(arena, line_width);

    // Add hfill before content
    line->append_child(make_glue(arena, hfill_glue(), "hfill"));

    // Add content
    line->append_child(content);

    // Set dimensions
    line->width = line_width;
    line->height = content->height;
    line->depth = content->depth;

    return line;
}

TexNode* split_line(
    TexNode* left_content,
    TexNode* right_content,
    float line_width,
    Arena* arena
) {
    TexNode* line = make_hbox(arena, line_width);

    // Left content
    line->append_child(left_content);

    // Flexible space in middle
    line->append_child(make_glue(arena, hfill_glue(), "hfill"));

    // Right content
    line->append_child(right_content);

    // Set dimensions
    line->width = line_width;
    float max_height = (left_content->height > right_content->height) ?
                        left_content->height : right_content->height;
    float max_depth = (left_content->depth > right_content->depth) ?
                       left_content->depth : right_content->depth;
    line->height = max_height;
    line->depth = max_depth;

    return line;
}

// ============================================================================
// Debugging
// ============================================================================

void dump_vlist(TexNode* vlist) {
    if (!vlist) {
        log_debug("VList: (null)");
        return;
    }

    VListDimensions dim = measure_vlist(vlist);

    log_debug("VList: height=%.2f depth=%.2f", dim.height, dim.depth);

    int index = 0;
    for (TexNode* c = vlist->first_child; c; c = c->next_sibling) {
        const char* type_name = node_class_name(c->node_class);

        if (c->node_class == NodeClass::Glue) {
            const Glue& g = c->content.glue.spec;
            const char* name = c->content.glue.name ? c->content.glue.name : "";
            log_debug("  [%d] %s: %.2f+%.2f-%.2f (%s)",
                index, type_name, g.space, g.stretch, g.shrink, name);
        } else if (c->node_class == NodeClass::Kern) {
            log_debug("  [%d] %s: %.2f", index, type_name, c->content.kern.amount);
        } else if (c->node_class == NodeClass::HBox || c->node_class == NodeClass::HList) {
            log_debug("  [%d] %s: w=%.2f h=%.2f d=%.2f",
                index, type_name, c->width, c->height, c->depth);
        } else {
            log_debug("  [%d] %s: w=%.2f h=%.2f d=%.2f",
                index, type_name, c->width, c->height, c->depth);
        }
        index++;
    }
}

} // namespace tex
