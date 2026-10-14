#include "render.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"

bool render_column_rules_begin(ViewBlock* block, RenderColumnRuleIterator* iterator) {
    if (!block || !iterator || !block->multicol_prop()) return false;
    MultiColumnProp* mc = block->multicol_prop();
    int columns = mc->computed_used_column_count > 0
        ? mc->computed_used_column_count : mc->computed_column_count;
    if (columns <= 1 || mc->rule_width <= 0.0f || mc->rule_style == CSS_VALUE_NONE) return false;
    *iterator = {};
    iterator->block = block;
    iterator->fragment = block->layout_fragment_list();
    iterator->remaining_columns = columns;
    iterator->fragment_columns = iterator->fragment
        ? min(columns, max(mc->computed_column_count, 1)) : columns;
    iterator->column_width = mc->computed_column_width;
    iterator->gap = multicol_column_gap(block);
    return true;
}

bool render_column_rule_next(RenderColumnRuleIterator* iterator, float* x, float* y, float* height) {
    if (!iterator || !iterator->block || !x || !y || !height) return false;
    ViewBlock* block = iterator->block;
    while (iterator->remaining_columns > 0) {
        if (iterator->rule_index + 1 < iterator->fragment_columns) {
            BoxMetrics box = layout_box_metrics(block);
            float origin_x = box.border.left + box.padding.left;
            float origin_y = box.border.top + box.padding.top;
            float rule_height = layout_content_size_from_border_box(block, block->height, false);
            if (iterator->fragment) {
                // CSS Multicol §4: rules join columns within each fragment;
                // a continuation's outer gutter is a different context.
                origin_x += iterator->fragment->x;
                origin_y = iterator->fragment->y;
                rule_height = iterator->fragment->height;
                if (iterator->fragment == block->layout_fragment_list()) {
                    origin_y += box.border.top + box.padding.top;
                    rule_height -= box.border.top + box.padding.top;
                }
                if (!iterator->fragment->next) rule_height -= box.border.bottom + box.padding.bottom;
            }
            if (rule_height <= 0.0f) rule_height = layout_view_children_bottom(block, false);
            int index = iterator->rule_index++;
            *x = origin_x + (index + 1) * iterator->column_width + index * iterator->gap + iterator->gap * 0.5f;
            *y = origin_y;
            *height = rule_height;
            if (rule_height > 0.0f) return true;
            continue;
        }
        iterator->remaining_columns -= iterator->fragment_columns;
        if (!iterator->fragment || !iterator->fragment->next) break;
        iterator->fragment = iterator->fragment->next;
        iterator->fragment_columns = min(iterator->remaining_columns,
            max(block->multicol_prop()->computed_column_count, 1));
        iterator->rule_index = 0;
    }
    iterator->remaining_columns = 0;
    return false;
}

void render_column_rules(RasterRenderContext* rdcon, ViewBlock* block) {
    RenderColumnRuleIterator iterator;
    if (!render_column_rules_begin(block, &iterator)) return;
    MultiColumnProp* mc = block->multicol_prop();
    float rule_x = 0.0f, block_y = 0.0f, rule_height = 0.0f;
    while (render_column_rule_next(&iterator, &rule_x, &block_y, &rule_height)) {
        rule_x += rdcon->block.x;
        block_y += rdcon->block.y;

        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            float thin_width = mc->rule_width / 3.0f;
            rc_fill_rect(rdcon, rule_x - mc->rule_width * 0.5f, block_y, thin_width, rule_height, mc->rule_color);
            rc_fill_rect(rdcon, rule_x + mc->rule_width * 0.5f - thin_width, block_y, thin_width, rule_height, mc->rule_color);
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

        log_debug("[MULTICOL] Rule at x=%.1f, height=%.1f", rule_x, rule_height);
    }
}
