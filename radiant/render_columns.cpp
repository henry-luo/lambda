#include "render_columns.hpp"
#include "render.hpp"
#include "layout_box.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"

void render_column_rules(RenderContext* rdcon, ViewBlock* block) {
    if (!block->multicol) return;

    MultiColumnProp* mc = block->multicol;

    int rule_column_count = mc->computed_used_column_count > 0
        ? mc->computed_used_column_count
        : mc->computed_column_count;
    if (rule_column_count <= 1 || mc->rule_width <= 0 ||
        mc->rule_style == CSS_VALUE_NONE) {
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;

    float block_x = rdcon->block.x;
    float block_y = rdcon->block.y;
    if (block->bound) {
        block_x += block->bound->padding.left;
        block_y += block->bound->padding.top;
    }

    float rule_height = block->height;
    if (block->bound) {
        rule_height -= layout_box_metrics(block).pad_border_v;
    }

    if (rule_height <= 0) {
        View* child = static_cast<View*>(block->first_child);
        float max_bottom = 0;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_block = lam::view_require_block(child);
                float child_bottom = child_block->y + child_block->height;
                if (child_bottom > max_bottom) max_bottom = child_bottom;
            }
            child = child->next();
        }
        rule_height = max_bottom;
        log_debug("[MULTICOL] Rule height computed from children: %.1f", rule_height);
    }

    log_debug("[MULTICOL] Rendering %d column rules, width=%.1f, style=%d",
              rule_column_count - 1, mc->rule_width, mc->rule_style);

    for (int i = 0; i < rule_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap + gap / 2.0f - mc->rule_width / 2.0f;

        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            float thin_width = mc->rule_width / 3.0f;
            rc_fill_rect(rdcon, rule_x - thin_width, block_y, thin_width, rule_height, mc->rule_color);
            rc_fill_rect(rdcon, rule_x + thin_width, block_y, thin_width, rule_height, mc->rule_color);
        } else {
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rule_x, block_y);
            rdt_path_line_to(p, rule_x, block_y + rule_height);

            float* dash = NULL;
            int dash_count = 0;
            float dash_pattern[2];
            if (mc->rule_style == CSS_VALUE_DOTTED) {
                dash_pattern[0] = mc->rule_width;
                dash_pattern[1] = mc->rule_width * 2;
                dash = dash_pattern;
                dash_count = 2;
            } else if (mc->rule_style == CSS_VALUE_DASHED) {
                dash_pattern[0] = mc->rule_width * 3;
                dash_pattern[1] = mc->rule_width * 2;
                dash = dash_pattern;
                dash_count = 2;
            }

            rc_stroke_path(rdcon, p, mc->rule_color, mc->rule_width,
                           RDT_CAP_BUTT, RDT_JOIN_MITER, dash, dash_count, NULL);
            rdt_path_free(p);
        }

        log_debug("[MULTICOL] Rule %d at x=%.1f, height=%.1f", i, rule_x, rule_height);
    }
}
