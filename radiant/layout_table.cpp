#include "layout.hpp"
#include "view.hpp"  // For FormDefaults (radio/checkbox margin constants)
#include "render.hpp"
#include "../lib/log.h"
#include "../lib/strview.h"
#include "../lib/arraylist.h"
#include "../lib/arraylist.hpp"
#include "../lib/utf.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_style_node.hpp"


static inline ViewBlock* table_array_view_block(ArrayList* list, int index) {
    View* view = static_cast<View*>(list->data[index]);
    return lam::view_require_block(view);
}

ViewTableRow* ViewTable::first_row() {
    for (View* child = static_cast<View*>(first_child); child; child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            return lam::view_require<RDT_VIEW_TABLE_ROW>(child);
        }
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRow* row = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child)->first_row();
            if (row) return row;
        }
    }
    return nullptr;
}

ViewBlock* ViewTable::first_row_group() {
    if (acts_as_tbody()) return this;
    View* group = layout_first_view_with_type(static_cast<View*>(first_child),
                                          RDT_VIEW_TABLE_ROW_GROUP);
    return group ? lam::view_require_block(group) : nullptr;
}

ViewTableRow* ViewTable::next_row(ViewTableRow* current) {
    if (!current) return nullptr;
    for (View* sibling = static_cast<View*>(current->next_sibling); sibling; sibling = static_cast<View*>(sibling->next_sibling)) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) return lam::view_require<RDT_VIEW_TABLE_ROW>(sibling);
    }
    ViewBlock* parent = lam::view_as_block(static_cast<View*>(current->parent));
    if (parent && parent->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        for (View* next = static_cast<View*>(parent->next_sibling); next; next = static_cast<View*>(next->next_sibling)) {
            if (next->view_type == RDT_VIEW_TABLE_ROW) return lam::view_require<RDT_VIEW_TABLE_ROW>(next);
            if (next->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                ViewTableRow* row = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(next)->first_row();
                if (row) return row;
            }
        }
    }
    return nullptr;
}

TableSectionType ViewTableRowGroup::get_section_type() const {
    NameId tag = tag_id;
    if (tag == MARKUP_NAME_THEAD) return TABLE_SECTION_THEAD;
    if (tag == MARKUP_NAME_TFOOT) return TABLE_SECTION_TFOOT;
    if (tag == MARKUP_NAME_TBODY) return TABLE_SECTION_TBODY;
    DisplayValue resolved = resolve_display_value((void*)this);
    if (resolved.inner == CSS_VALUE_TABLE_HEADER_GROUP) {
        return TABLE_SECTION_THEAD;
    }
    if (resolved.inner == CSS_VALUE_TABLE_FOOTER_GROUP) {
        return TABLE_SECTION_TFOOT;
    }
    return TABLE_SECTION_TBODY;
}

ViewTableRow* ViewTableRowGroup::first_row() {
    View* row = layout_first_view_with_type(static_cast<View*>(first_child),
                                        RDT_VIEW_TABLE_ROW);
    return row ? lam::view_require<RDT_VIEW_TABLE_ROW>(row) : nullptr;
}

ViewTableRow* ViewTableRowGroup::next_row(ViewTableRow* current) {
    if (!current) return nullptr;
    View* row = layout_first_view_with_type(static_cast<View*>(current->next_sibling),
                                        RDT_VIEW_TABLE_ROW);
    return row ? lam::view_require<RDT_VIEW_TABLE_ROW>(row) : nullptr;
}

ViewTableCell* ViewTableRow::first_cell() {
    View* cell = layout_first_view_with_type(static_cast<View*>(first_child),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

ViewTableCell* ViewTableRow::next_cell(ViewTableCell* current) {
    if (!current) return nullptr;
    View* cell = layout_first_view_with_type(static_cast<View*>(current->next_sibling),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

static float table_row_collapsed_vertical_border_contribution(ViewTableRow* row,
                                                              float* max_top_border,
                                                              float* max_bottom_border) {
    if (max_top_border) *max_top_border = 0.0f;
    if (max_bottom_border) *max_bottom_border = 0.0f;
    row->each_cell([&](ViewTableCell* cell) {
        if (cell->td->top_resolved && max_top_border &&
            cell->td->top_resolved->width > *max_top_border) {
            *max_top_border = cell->td->top_resolved->width;
        }
        if (cell->td->bottom_resolved && max_bottom_border &&
            cell->td->bottom_resolved->width > *max_bottom_border) {
            *max_bottom_border = cell->td->bottom_resolved->width;
        }
    });
    float top = max_top_border ? *max_top_border : 0.0f;
    float bottom = max_bottom_border ? *max_bottom_border : 0.0f;
    return top / 2.0f + bottom / 2.0f;
}

ViewBlock* ViewTableRow::parent_row_group() {
    ViewBlock* parent = lam::view_as_block(static_cast<View*>(this->parent));
    if (parent && (parent->view_type == RDT_VIEW_TABLE_ROW_GROUP || parent->view_type == RDT_VIEW_TABLE)) {
        return parent;
    }
    return nullptr;
}

ViewTableCell* ViewTable::first_direct_cell() {
    if (!acts_as_row()) return nullptr;
    View* cell = layout_first_view_with_type(static_cast<View*>(first_child),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

ViewTableCell* ViewTable::next_direct_cell(ViewTableCell* current) {
    if (!current || !acts_as_row()) return nullptr;
    View* cell = layout_first_view_with_type(static_cast<View*>(current->next_sibling),
                                         RDT_VIEW_TABLE_CELL);
    return cell ? lam::view_require<RDT_VIEW_TABLE_CELL>(cell) : nullptr;
}

TableCellInsets table_cell_insets(ViewTableCell* cell) {
    TableCellInsets insets = {};
    if (!cell || !cell->bound) return insets;
    BoundaryProp* boundary = cell->boundary_mut();
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        CssBoxSide box_side = (CssBoxSide)side;
        insets.padding.values[side] = max(
            *radiant_spacing_value(&boundary->padding, box_side), 0.0f);
        if (boundary->border) {
            RadiantBorderSide border = radiant_border_side(boundary->border, box_side);
            insets.border.values[side] = *border.style != CSS_VALUE_NONE
                ? *border.width : 0.0f;
        }
    }
    return insets;
}

static ViewTable* get_parent_table(ViewTableCell* cell) {
    if (!cell) return nullptr;
    DomNode* parent = cell->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_TABLE) {
            return lam::view_require<RDT_VIEW_TABLE>(static_cast<View*>(parent));
        }
        parent = parent->parent;
    }
    return nullptr;
}

static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell, ViewBlock* table = nullptr);
static float table_inter_spacing(ViewTable* table, bool horizontal);
static int table_effective_column_count(ViewTable* table, TableMetadata* meta,
                                        const float* col_widths, int columns);

static void table_apply_positioned_layout(LayoutContext* lycon, ViewBlock* block) {
    if (!block || !block->position) return;
    if (block->positionp()->position == CSS_VALUE_RELATIVE) {
        layout_relative_positioned(lycon, block);
    } else if (block->positionp()->position == CSS_VALUE_STICKY) {
        layout_sticky_positioned(lycon, block);
    }
}

static void table_apply_positioned_row(LayoutContext* lycon, ViewTableRow* row) {
    if (!row) return;
    table_apply_positioned_layout(lycon, static_cast<ViewBlock*>(row));
    row->each_cell( [&](ViewTableCell* tcell) {
        table_apply_positioned_layout(lycon, lam::view_require_block(tcell));
    });
}

static float resolve_table_relative_width(LayoutContext* lycon, const CssValue* value, float table_content_width) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return table_content_width > 0.0f ?
            (float)(value->data.percentage.value / 100.0) * table_content_width : 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
    }
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        BlockContext percentage_base = {};
        percentage_base.content_width = table_content_width;
        BlockContext* saved_parent = lycon->block.parent;
        lycon->block.parent = &percentage_base;
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
        lycon->block.parent = saved_parent;
        return isnan(resolved) ? 0.0f : resolved;
    }
    return 0.0f;
}

static bool table_width_value_is_relative(const CssValue* value) {
    return layout_css_value_any(value, [](const CssValue* item) {
        return item->type == CSS_VALUE_TYPE_PERCENTAGE ||
               item->type == CSS_VALUE_TYPE_CALC;
    });
}

static bool table_width_value_has_nonzero_length_term(const CssValue* value) {
    return layout_css_value_any(value, [](const CssValue* item) {
        return item->type == CSS_VALUE_TYPE_LENGTH &&
               fabs(item->data.length.value) > 0.0001;
    });
}

static bool table_direct_float_overlaps_y(ViewBlock* floating, ViewTable* table, float y) {
    if (!floating || !table) return false;
    float rel_y = floating->parent == table ? 0.0f : floating->y - table->y;
    float margin_bottom = floating->bound ? floating->boundary()->margin.bottom : 0.0f;
    return y >= rel_y && y < rel_y + floating->height + margin_bottom;
}

template <typename Fn>
static void for_each_table_direct_float(ViewTable* table, Fn fn) {
    if (!table) return;
    for (View* child = table->first_child; child; child = child->next_sibling) {
        if (!child->is_block()) continue;
        ViewBlock* floating = lam::view_require_block(child);
        if (layout_position_is_floated(floating->position)) fn(floating);
    }
}

static float table_direct_float_intrusion(ViewTable* table, float y, float table_width,
                                          CssEnum float_side) {
    float intrusion = 0.0f;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        if (floating->positionp()->float_prop != float_side) return;
        if (!table_direct_float_overlaps_y(floating, table, y)) return;
        float rel_x = floating->x - table->x;
        float candidate;
        if (float_side == CSS_VALUE_LEFT) {
            float margin_right = floating->bound ? floating->boundary()->margin.right : 0.0f;
            candidate = rel_x + floating->width + margin_right;
            if (table_width > 0.0f && candidate > table_width) candidate = table_width;
        } else {
            if (rel_x < 0.0f) rel_x = 0.0f;
            if (rel_x > table_width) rel_x = table_width;
            candidate = table_width - rel_x;
        }
        if (candidate > intrusion) intrusion = candidate;
    });
    return intrusion;
}

static float table_direct_float_next_clear_y(ViewTable* table, float y) {
    float next_y = y;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        if (!table_direct_float_overlaps_y(floating, table, y)) return;
        float margin_bottom = floating->bound ? floating->boundary()->margin.bottom : 0.0f;
        float candidate = floating->height + margin_bottom;
        if (candidate > next_y) next_y = candidate;
    });
    return next_y;
}

static bool table_has_direct_float(ViewTable* table) {
    bool found = false;
    for_each_table_direct_float(table, [&](ViewBlock* floating) {
        (void)floating;
        found = true;
    });
    return found;
}

static float table_clear_direct_float_intrusion(ViewTable* table, float y,
                                                float content_width,
                                                bool has_direct_float) {
    if (!has_direct_float || table->width <= 0.0f || content_width <= 0.0f) return y;
    for (int guard = 0; guard < 32; guard++) {
        float left_intrusion = table_direct_float_intrusion(table, y, table->width, CSS_VALUE_LEFT);
        float right_intrusion = table_direct_float_intrusion(table, y, table->width, CSS_VALUE_RIGHT);
        if (left_intrusion + content_width + right_intrusion <= table->width + 0.01f) {
            break;
        }
        float next_y = table_direct_float_next_clear_y(table, y);
        if (next_y <= y + 0.01f) break;
        y = next_y;
    }
    return y;
}

static bool table_cell_calc_width_is_indefinite_constraint(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function) return false;
    CssFunction* func = value->data.function;
    if (!func->name || strcmp(func->name, "calc") != 0) return false;
    // CSS table auto layout treats percentages as column constraints. A calc()
    return table_width_value_is_relative(value) &&
        table_width_value_has_nonzero_length_term(value);
}

static float get_cell_css_width_percent(ViewTableCell* tcell) {
    if (!tcell || tcell->node_type != DOM_NODE_ELEMENT) return 0.0f;
    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem) return 0.0f;
    CssDeclaration* width_decl = dom_elem->specified_style
        ? style_tree_get_declaration(dom_elem->specified_style, CSS_PROPERTY_WIDTH)
        : nullptr;
    if (width_decl && width_decl->value &&
        width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        float percent = (float)width_decl->value->data.percentage.value;
        return percent > 0.0f ? percent : 0.0f;
    }
    if (tcell->blk && !isnan(tcell->block()->given_width_percent)) {
        return tcell->block()->given_width_percent > 0.0f ?
            tcell->block()->given_width_percent : 0.0f;
    }
    return 0.0f;
}

static float get_cell_css_width(LayoutContext* lycon, ViewTableCell* tcell, float table_content_width, bool border_collapse = false, bool* is_table_relative = nullptr) {
    if (tcell->node_type != DOM_NODE_ELEMENT) return 0.0f;
    if (is_table_relative) *is_table_relative = false;
    DomElement* dom_elem = tcell->as_element();
    if (!dom_elem) return 0.0f;
    CssDeclaration* width_decl = dom_elem->specified_style
        ? style_tree_get_declaration(dom_elem->specified_style, CSS_PROPERTY_WIDTH)
        : nullptr;
    float cell_width = 0.0f;
    float css_content_width = 0.0f;
    bool html_width_hint = false;
    if (width_decl && width_decl->value &&
        table_cell_calc_width_is_indefinite_constraint(width_decl->value)) {
        cell_width = 0.0f;
        if (is_table_relative) *is_table_relative = true;
    } else if (width_decl && width_decl->value &&
        (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE ||
         width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
         width_decl->value->type == CSS_VALUE_TYPE_FUNCTION)) {
        css_content_width = resolve_table_relative_width(lycon, width_decl->value, table_content_width);
        cell_width = css_content_width;
        if (is_table_relative) *is_table_relative = table_width_value_is_relative(width_decl->value);
    } else if (tcell->blk && !isnan(tcell->block()->given_width_percent) &&
               table_content_width > 0.0f) {
        css_content_width = table_content_width * tcell->block()->given_width_percent / 100.0f;
        cell_width = css_content_width;
        if (is_table_relative) *is_table_relative = true;
        html_width_hint = true;
    } else if (tcell->blk && tcell->block_mut()->given_width >= 0.0f) {
        css_content_width = tcell->block()->given_width;
        cell_width = css_content_width;
        html_width_hint = true;
    }
    if (cell_width <= 0) return 0.0f;
    bool is_border_box = html_width_hint ||
        layout_uses_border_box(tcell);
    if (!is_border_box) {
        TableCellInsets insets = table_cell_insets(tcell);
        if (tcell->bound && tcell->boundary_mut()->padding.left >= 0 && tcell->boundary_mut()->padding.right >= 0) {
            cell_width += insets.padding.left + insets.padding.right;
        }
        if (!border_collapse) cell_width += insets.border.left + insets.border.right;
    }
    cell_width = layout_clamp_min_max_axis(tcell, cell_width, true);
    return cell_width;
}

static float get_explicit_css_height(LayoutContext* lycon, ViewBlock* element) {
    if (element->node_type != DOM_NODE_ELEMENT) return 0.0f;
    DomElement* dom_elem = element->as_element();
    if (!dom_elem) return 0.0f;
    if (dom_elem->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
        if (height_decl && height_decl->value) {
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            if (resolved > 0) {
                return resolved;
            }
        }
    }
    if (element->blk && element->block_mut()->given_height > 0) {
        return element->block()->given_height;
    }
    return 0.0f;
}

static bool is_cell_empty(ViewTableCell* cell) {
    DomNode* child = lam::dom_require<DOM_NODE_ELEMENT>(cell)->first_child;
    bool ws_preserved = false;
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(cell);
    if (elem->blk && elem->block_mut()->white_space != 0) {
        CssEnum ws = elem->block()->white_space;
        if (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
            ws == CSS_VALUE_PRE_LINE || ws == CSS_VALUE_BREAK_SPACES) {
            ws_preserved = true;
        }
    }
    while (child) {
        if (child->is_element()) {
            return false;
        }
        if (child->is_text()) {
            const char* text = lam::dom_require<DOM_NODE_TEXT>(child)->text;
            if (text) {
                if (ws_preserved && strlen(text) > 0) {
                    return false;
                }
                const unsigned char* p = (const unsigned char*)text;
                const unsigned char* p_end = p + strlen(text);
                while (p < p_end) {
                    uint32_t codepoint;
                    int bytes = str_utf8_decode((const char*)p, (size_t)(p_end - p), &codepoint);
                    if (bytes <= 0) break;  // Invalid UTF-8
                    bool is_ws = (codepoint == 0x0020 || codepoint == 0x0009 || codepoint == 0x000A ||
                                  codepoint == 0x000B || codepoint == 0x000C || codepoint == 0x000D);
                    if (!is_ws) {
                        is_ws = (codepoint == 0x1680 ||
                                 (codepoint >= 0x2000 && codepoint <= 0x200A) ||
                                 codepoint == 0x205F || codepoint == 0x3000);
                    }
                    if (!is_ws) {
                        return false;
                    }
                    p += bytes;
                }
            }
        }
        child = child->next_sibling;
    }
    return true;  // No visible content found
}

static bool is_visibility_collapse(ViewBlock* element) {
    if (!element) return false;
    DomElement* dom_elem = element->as_element();
    if (dom_elem && dom_elem->in_line) {
        return dom_elem->inl()->visibility == VIS_COLLAPSE;
    }
    return false;
}

static float find_descendant_float_max_y(ViewElement* parent, float y_offset) {
    float max_y = 0;
    for (View* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (child->view_type == RDT_VIEW_BLOCK ||
            child->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = lam::view_require_block(child);
            float abs_y = y_offset + child->y;
            if (layout_position_is_floated(block->position)) {
                float bottom = abs_y + child->height;
                if (bottom > max_y) max_y = bottom;
            }
            if (block->is_element()) {
                float nested = find_descendant_float_max_y(lam::view_require_element(block), abs_y);
                if (nested > max_y) max_y = nested;
            }
        }
    }
    return max_y;
}

static bool table_cell_vertical_align_skips_child(View* child);

template <typename Fn>
static void for_each_table_cell_vertical_align_child(ViewElement* cell, Fn fn) {
    if (!cell) return;
    for (View* child = cell->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (table_cell_vertical_align_skips_child(child)) continue;
        fn(child);
    }
}

static bool table_inline_span_has_inline_axis_decoration(ViewSpan* span) {
    if (!span || !span->bound) return false;
    if (span->boundary()->margin.left != 0.0f || span->boundary()->margin.right != 0.0f ||
        span->boundary()->padding.left != 0.0f || span->boundary()->padding.right != 0.0f) {
        return true;
    }
    return span->boundary()->border &&
        (span->boundary()->border->width.left != 0.0f ||
         span->boundary()->border->width.right != 0.0f);
}

static bool table_view_has_cell_line_content(View* view);

static bool table_inline_span_is_phantom_for_cell_height(ViewSpan* span) {
    if (!span) return true;
    if (table_inline_span_has_inline_axis_decoration(span)) return false;
    for (View* child = span->first_child; child; child = child->next_sibling) {
        if (table_view_has_cell_line_content(child)) return false;
    }
    return true;
}

static bool table_view_has_cell_line_content(View* view) {
    if (!view || !view->view_type || table_cell_vertical_align_skips_child(view)) {
        return false;
    }
    if (view->view_type == RDT_VIEW_TEXT) {
        return view->width > 0.0f && view->height > 0.0f;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        return !table_inline_span_is_phantom_for_cell_height(
            lam::view_require<RDT_VIEW_INLINE>(view));
    }
    return true;
}

static void table_note_cell_content_extent(TableCellContentExtent* extent,
                                           float top, float bottom) {
    if (!extent) return;
    if (!extent->has_content || top < extent->min_y) {
        extent->min_y = top;
    }
    if (!extent->has_content || bottom > extent->max_y) {
        extent->max_y = bottom;
    }
    extent->has_content = true;
}

static void table_note_cell_line_position(TableCellContentExtent* extent,
                                          float top, float line_height) {
    if (!extent) return;
    if (!extent->has_line_y) {
        extent->line_min_y = top;
        extent->line_max_y = top;
        extent->last_line_y = top;
        extent->has_line_y = true;
    } else {
        if (top < extent->line_min_y) extent->line_min_y = top;
        if (top > extent->line_max_y) extent->line_max_y = top;
        float gap = top - extent->last_line_y;
        if (gap < 0.0f) gap = -gap;
        if (gap > 0.5f && gap > extent->max_line_gap) {
            extent->max_line_gap = gap;
        }
        if (gap > 0.5f) extent->last_line_y = top;
    }
    if (line_height > extent->max_line_height) {
        extent->max_line_height = line_height;
    }
}

static float table_inline_line_stack_height(const TableCellContentExtent* extent) {
    if (!extent || !extent->has_content) return 0.0f;
    float line_stack_height = extent->max_y - extent->min_y;
    if (extent->has_line_y) {
        float line_unit = extent->max_line_height;
        if (extent->max_line_gap > line_unit) {
            line_unit = extent->max_line_gap;
        }
        float positioned_height = extent->line_max_y - extent->line_min_y + line_unit;
        if (positioned_height > line_stack_height) line_stack_height = positioned_height;
    }
    return line_stack_height;
}

static float table_text_font_normal_line_height(ViewText* text, FontProp* font,
                                                bool* has_cjk_text) {
    if (has_cjk_text) *has_cjk_text = false;
    if (!text || !font) return 0.0f;
    const char* data = reinterpret_cast<const char*>(text->text_data());
    size_t len = text->length;
    if (!data || len == 0) return 0.0f;
    FontHandle* handle = font->font_handle;
    if (!handle) return 0.0f;
    FontStyleDesc style = font_style_desc_from_prop(font);
    float max_normal_line_height = 0.0f;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp = 0;
        int consumed = utf8_decode(data + pos, len - pos, &cp);
        if (consumed <= 0) break;
        if (has_cjk_text && utf_is_cjk(cp)) *has_cjk_text = true;
        LoadedGlyph* glyph = font_load_glyph(handle, &style, cp, false);
        if (glyph && glyph->font_normal_line_height > max_normal_line_height) {
            max_normal_line_height = glyph->font_normal_line_height;
        }
        pos += (size_t)consumed;
    }
    return max_normal_line_height;
}

static float table_resolve_cell_line_height(View* view, float line_height,
                                            bool line_height_is_normal,
                                            float parent_font_size,
                                            FontProp* cell_font) {
    if (!view || !line_height_is_normal || view->view_type != RDT_VIEW_TEXT) {
        return line_height;
    }
    ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
    FontProp* text_font = text->font ? text->font : cell_font;
    bool has_cjk_text = false;
    float normal_height = table_text_font_normal_line_height(
        text, text_font, &has_cjk_text);
    if (normal_height > line_height) return normal_height;
    if (has_cjk_text) {
        float cjk_height = get_cjk_system_line_height(parent_font_size);
        if (cjk_height > line_height) return cjk_height;
    }
    return line_height;
}

static float table_cell_line_box_height_for_view(View* view, float cell_line_height,
                                                 bool line_height_is_normal,
                                                 float parent_font_size,
                                                 FontProp* cell_font) {
    if (!view) return cell_line_height;
    float view_height = view->height;
    cell_line_height = table_resolve_cell_line_height(
        view, cell_line_height, line_height_is_normal, parent_font_size, cell_font);
    return max(cell_line_height > 0.0f ? cell_line_height : 0.0f, view_height);
}

static void table_collect_inline_line_box_extent(View* view, float cell_line_height,
                                                 bool line_height_is_normal,
                                                 float parent_font_size,
                                                 FontProp* cell_font,
                                                 TableCellContentExtent* extent) {
    if (!view || !view->view_type || table_cell_vertical_align_skips_child(view)) return;
    if (view->view_type == RDT_VIEW_TEXT || view->view_type == RDT_VIEW_BR) {
        if (view->view_type == RDT_VIEW_BR && view->height <= 0.0f) {
            return;
        }
        float line_height = cell_line_height;
        line_height = table_resolve_cell_line_height(
            view, line_height, line_height_is_normal, parent_font_size, cell_font);
        if (line_height <= 0.0f) line_height = view->height;
        if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(view);
            bool noted_rect = false;
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                float rect_line_height = max(line_height, rect->height);
                table_note_cell_content_extent(extent, rect->y, rect->y + rect_line_height);
                table_note_cell_line_position(extent, rect->y, rect_line_height);
                noted_rect = true;
            }
            if (noted_rect) return;
        }
        float child_top = view->y;
        table_note_cell_content_extent(extent, child_top, child_top + line_height);
        table_note_cell_line_position(extent, child_top, line_height);
        return;
    }
    if (view->view_type == RDT_VIEW_INLINE) {
        ViewSpan* span = lam::view_require<RDT_VIEW_INLINE>(view);
        if (table_inline_span_is_phantom_for_cell_height(span)) return;
        float descendant_line_height = cell_line_height;
        if (span->content_height > descendant_line_height) {
            // Nested inline struts participate independently of the cell's root strut.
            descendant_line_height = span->content_height;
        }
        for (View* child = span->first_child; child; child = child->next_sibling) {
            table_collect_inline_line_box_extent(child, descendant_line_height,
                                                 line_height_is_normal,
                                                 parent_font_size, cell_font, extent);
        }
        return;
    }
    if (view->view_type == RDT_VIEW_INLINE_BLOCK ||
        view->view_type == RDT_VIEW_BLOCK ||
        view->view_type == RDT_VIEW_LIST_ITEM ||
        view->view_type == RDT_VIEW_TABLE) {
        float child_top = view->y;
        table_note_cell_content_extent(extent, child_top, child_top + view->height);
        if (view->view_type != RDT_VIEW_INLINE_BLOCK && cell_line_height > 0.0f) {
            table_note_cell_line_position(extent, child_top, cell_line_height);
        }
    }
}

static bool table_empty_inline_atomic_line_top(LayoutContext* lycon,
                                               ViewTableCell* tcell,
                                               ViewBlock* block,
                                               float* line_top);

static float measure_cell_content_height(LayoutContext* lycon, ViewTableCell* tcell) {
    bool has_block_content = false;
    float block_content_min_y = 0.0f;   // Track min y of block content (for offset)
    float block_content_max_y = 0.0f;   // Track max bottom of block content
    bool has_inline_content = false;
    bool has_inline_formatting_content = false;
    float inline_content_min_y = 0.0f;  // Track min y of inline/text content
    float inline_content_max_y = 0.0f;  // Track max bottom of inline/text content
    View* last_sizing_child = nullptr;
    for_each_table_cell_vertical_align_child(
        lam::view_require_element(tcell), [&](View* child) { last_sizing_child = child; });
    float ignored_quirky_margin_bottom = 0.0f;
    ViewBlock* last_sizing_block = lam::view_as_block(last_sizing_child);
    if (last_sizing_block && last_sizing_block->bound &&
        layout_quirky_container_ignores_child_margin_bottom(
            lycon, tcell, last_sizing_block)) {
        ignored_quirky_margin_bottom = last_sizing_block->boundary()->margin.bottom;
    }
    LayoutContextScope context_scope(lycon);
    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    float cell_line_height = lycon->block.line_height;
    bool cell_line_height_is_normal = lycon->block.line_height_is_normal;
    float cell_font_size = tcell->font && tcell->fontp()->font_size > 0.0f
        ? tcell->fontp()->font_size
        : lycon->font.current_font_size;
    if (tcell->blk && tcell->block_mut()->line_height && tcell->font) {
        float specified_line_height = layout_resolve_line_height_value(
            lycon, tcell->block()->line_height, tcell, tcell->fontp()->font_size);
        if (specified_line_height > cell_line_height) {
            // unitless inherited line-height must resolve against the cell font;
            cell_line_height = specified_line_height;
        }
    }
    if (layout_quirks_block_ignores_line_height(lycon, tcell)) {
        // The inline-only quirks rule omits the cell root strut during row sizing too.
        cell_line_height = 0.0f;
    }
    lycon->font = context_scope.saved_font;
    for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            has_inline_formatting_content = true;
            float text_top = text->y;
            float text_height = table_cell_line_box_height_for_view(
                child, cell_line_height, cell_line_height_is_normal, cell_font_size,
                tcell->font);
            float text_bottom = text_top + text_height;
            if (!has_inline_content || text_top < inline_content_min_y) {
                inline_content_min_y = text_top;
                has_inline_content = true;
            }
            if (text_bottom > inline_content_max_y) {
                inline_content_max_y = text_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_BR) {
            has_inline_formatting_content = true;
            float br_top = child->y;
            float br_bottom = br_top + child->height;
            if (!has_inline_content || br_top < inline_content_min_y) {
                inline_content_min_y = br_top;
                has_inline_content = true;
            }
            if (br_bottom > inline_content_max_y) {
                inline_content_max_y = br_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_BLOCK ||
                 child->view_type == RDT_VIEW_LIST_ITEM ||
                 child->view_type == RDT_VIEW_INLINE ||
                 child->view_type == RDT_VIEW_INLINE_BLOCK) {
            if (child->view_type == RDT_VIEW_INLINE &&
                table_inline_span_is_phantom_for_cell_height(
                    lam::view_require<RDT_VIEW_INLINE>(child))) {
                return;
            }
            if (child->view_type == RDT_VIEW_INLINE) {
                has_inline_formatting_content = true;
            }
            ViewElement* block = lam::view_require_element(child);
            float child_height = block->height;
            float child_top = child->y;
            float child_bottom = child->y + child_height;
            if (child->view_type == RDT_VIEW_INLINE_BLOCK &&
                table_empty_inline_atomic_line_top(
                    lycon, tcell, lam::view_require_block(child), &child_top)) {
                child_bottom = child_top + cell_line_height;
            }
            if (child->view_type == RDT_VIEW_INLINE) {
                TableCellContentExtent inline_extent = {};
                table_collect_inline_line_box_extent(child, cell_line_height,
                                                     cell_line_height_is_normal,
                                                     cell_font_size, tcell->font,
                                                     &inline_extent);
                if (block->content_height > 0) {
                    child_height = max(child_height, block->content_height);
                } else if (cell_line_height > child_height) {
                    child_height = cell_line_height;
                }
                if (inline_extent.has_content) {
                    float inline_line_box_height = table_inline_line_stack_height(&inline_extent);
                    if (inline_line_box_height > child_height) {
                        child_height = inline_line_box_height;
                    }
                }
                child_bottom = child->y + child_height;
            }
            if (block->bound) {
                bool is_self_collapsing = (child_height == 0);
                if (is_self_collapsing && block->boundary_mut()->border) {
                    float bt = block->boundary()->border->width.top;
                    float bb = block->boundary()->border->width.bottom;
                    if (bt > 0 || bb > 0) is_self_collapsing = false;
                }
                if (is_self_collapsing) {
                    float pt = block->boundary()->padding.top;
                    float pb = block->boundary()->padding.bottom;
                    if (pt > 0 || pb > 0) is_self_collapsing = false;
                }
                if (!is_self_collapsing) {
                    child_top -= block->boundary()->margin.top;
                    float margin_bottom = block->boundary()->margin.bottom;
                    if (child == last_sizing_child && ignored_quirky_margin_bottom > 0.0f) {
                        margin_bottom = 0.0f;
                    }
                    child_bottom += margin_bottom;
                }
            }
            if (!has_block_content || child_top < block_content_min_y) {
                block_content_min_y = child_top;
                has_block_content = true;
            }
            if (child_bottom > block_content_max_y) {
                block_content_max_y = child_bottom;
            }
        }
        else if (child->view_type == RDT_VIEW_TABLE) {
            ViewTable* nested_table = lam::view_require<RDT_VIEW_TABLE>(child);
            float table_height = nested_table->height;
            float table_top = child->y;
            float table_bottom = table_top + table_height;
            ViewBlock* table_block = lam::view_require_block(child);
            if (table_block->bound) {
                table_top -= table_block->boundary()->margin.top;
                table_bottom += table_block->boundary()->margin.bottom;
            }
            if (!has_block_content || table_top < block_content_min_y) {
                block_content_min_y = table_top;
                has_block_content = true;
            }
            if (table_bottom > block_content_max_y) {
                block_content_max_y = table_bottom;
            }
        }
    });
    float overall_min_y = 0, overall_max_y = 0;
    bool has_any = false;
    if (has_block_content) {
        overall_min_y = block_content_min_y;
        overall_max_y = block_content_max_y;
        has_any = true;
    }
    if (has_inline_content) {
        if (!has_any || inline_content_min_y < overall_min_y) overall_min_y = inline_content_min_y;
        if (!has_any || inline_content_max_y > overall_max_y) overall_max_y = inline_content_max_y;
        has_any = true;
    }
    float content_height = has_any ? (overall_max_y - overall_min_y) : 0.0f;
    float flow_content_height = max(
        0.0f, tcell->content_height - ignored_quirky_margin_bottom);
    if (has_inline_formatting_content && flow_content_height > content_height) {
        // table cell row sizing is based on line boxes; inline DOMRects only
        content_height = flow_content_height;
    }
    if (!has_any && tcell->content_height > content_height) {
        // Replaced-only spacer cells measure from their child boxes; the line
        content_height = tcell->content_height;
        has_any = true;
    }


    // CSS 2.1 §10.6.3: BFC height includes float descendants.
    float float_max_y = find_descendant_float_max_y(lam::view_require_element(tcell), 0);
    if (float_max_y > 0) {
        if (!has_any || float_max_y > overall_max_y) {
            overall_max_y = float_max_y;
            has_any = true;
        }
        content_height = has_any ? (overall_max_y - overall_min_y) : 0.0f;
    }
    return content_height;
}

static float calculate_cell_height(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                                  float content_height, float explicit_height) {
    bool is_border_box = layout_uses_border_box(tcell);
    TableCellInsets insets = table_cell_insets(tcell);
    float pad_top = insets.padding.top;
    float pad_bottom = insets.padding.bottom;
    if (tcell->bound && tcell->boundary_mut()->padding.top >= 0 && tcell->boundary_mut()->padding.bottom >= 0) {
        pad_top = insets.padding.top;
        pad_bottom = insets.padding.bottom;
    } else {
        pad_top = 0.0f;
        pad_bottom = 0.0f;
    }
    float border_top = 0, border_bottom = 0;
    if (table->tb->border_collapse) {
        border_top = tcell->td->top_resolved ? tcell->td->top_resolved->width : 0.0f;
        border_bottom = tcell->td->bottom_resolved ? tcell->td->bottom_resolved->width : 0.0f;
        float half_borders = (border_top + border_bottom) / 2.0f;
        border_top = half_borders / 2.0f;
        border_bottom = half_borders - border_top;
    } else {
        border_top = insets.border.top;
        border_bottom = insets.border.bottom;
    }
    float content_total = content_height + pad_top + pad_bottom + border_top + border_bottom;
    if (explicit_height > 0) {
        float explicit_total;
        if (is_border_box) {
            explicit_total = explicit_height;
        } else {
            explicit_total = explicit_height + pad_top + pad_bottom + border_top + border_bottom;
        }
        return (content_total > explicit_total) ? content_total : explicit_total;
    }
    return content_total;
}

static float find_table_row_baseline(LayoutContext* lycon, ViewTableRow* trow);
static float table_last_baseline_for_writing(LayoutContext* lycon, ViewTable* table);
static void shift_table_cell_vertical_align_children(ViewTableCell* tcell,
                                                      float y_adjustment);

static float table_row_baseline_callback(LayoutContext* lycon, View* row) {
    return find_table_row_baseline(lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(row));
}

float find_first_baseline_recursive(LayoutContext* lycon, View* parent, float cumulative_y, bool use_normal_lh) {
    return radiant::compute_view_first_text_baseline(
        lycon, parent, cumulative_y, use_normal_lh, true,
        table_row_baseline_callback);
}

static bool table_find_last_row_axis(View* parent, float cumulative_x,
                                     float* row_axis, float* row_extent) {
    if (!parent || !row_axis || !row_extent || !parent->is_element()) return false;
    bool found = false;
    DomNode* last_child = lam::view_require_element(parent)->last_child;
    for (View* child = static_cast<View*>(last_child); child;
         child = static_cast<View*>(child->prev_sibling)) {
        float child_axis = cumulative_x + child->x;
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            *row_axis = child_axis;
            *row_extent = child->width;
            found = true;
        }
        if (child->is_element() &&
            table_find_last_row_axis(child, child_axis, row_axis, row_extent)) {
            found = true;
        }
    }
    return found;
}

static float table_last_baseline_for_vertical_writing(ViewTable* table) {
    if (!table || !layout_block_inline_axis_is_vertical(table)) return -1.0f;
    float row_axis = 0.0f;
    float row_extent = 0.0f;
    if (!table_find_last_row_axis(table, 0.0f, &row_axis, &row_extent)) {
        return -1.0f;
    }
    // CSS Writing Modes uses the central baseline in vertical typographic
    float central_axis = row_axis + row_extent / 2.0f;
    float block_start_border = 0.0f;
    if (table->bound && table->boundary()->border) {
        block_start_border = layout_block_writing_mode(table) == WM_VERTICAL_RL
            ? table->boundary()->border->width.right
            : table->boundary()->border->width.left;
    }
    if (layout_block_writing_mode(table) == WM_VERTICAL_RL) {
        return central_axis;
    }
    return table->width - central_axis - block_start_border;
}

static float table_last_baseline_for_horizontal_writing(
    LayoutContext* lycon, View* parent, float cumulative_y) {
    if (!parent || !parent->is_element()) return -1.0f;
    DomNode* last_child = lam::view_require_element(parent)->last_child;
    for (View* child = static_cast<View*>(last_child); child;
         child = static_cast<View*>(child->prev_sibling)) {
        float child_y = cumulative_y + child->y;
        if (child->view_type == RDT_VIEW_TABLE_ROW) {
            float row_baseline = find_table_row_baseline(
                lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(child));
            if (row_baseline >= 0.0f) return child_y + row_baseline;
        }
        if (child->is_element()) {
            float descendant_baseline = table_last_baseline_for_horizontal_writing(
                lycon, child, child_y);
            if (descendant_baseline >= 0.0f) return descendant_baseline;
        }
    }
    return -1.0f;
}

static float table_last_baseline_for_writing(LayoutContext* lycon, ViewTable* table) {
    if (!table) return -1.0f;
    if (layout_block_inline_axis_is_vertical(table)) {
        return table_last_baseline_for_vertical_writing(table);
    }
    return table_last_baseline_for_horizontal_writing(
        lycon, static_cast<View*>(table), 0.0f);
}

float find_last_baseline_recursive(LayoutContext* lycon, View* parent,
                                   float cumulative_x, bool use_normal_lh) {
    (void)use_normal_lh;
    if (!parent || !parent->is_element()) return -1.0f;
    if (parent->view_type == RDT_VIEW_TABLE) {
        return cumulative_x + table_last_baseline_for_writing(
            lycon, lam::view_require<RDT_VIEW_TABLE>(parent));
    }
    DomNode* last_child = lam::view_require_element(parent)->last_child;
    for (View* child = static_cast<View*>(last_child); child;
         child = static_cast<View*>(child->prev_sibling)) {
        float child_axis = cumulative_x + child->x;
        if (child->view_type == RDT_VIEW_TABLE) {
            float table_baseline = table_last_baseline_for_writing(
                lycon, lam::view_require<RDT_VIEW_TABLE>(child));
            if (table_baseline >= 0.0f) return child_axis + table_baseline;
        }
        if (child->is_element()) {
            float descendant_baseline = find_last_baseline_recursive(
                lycon, child, child_axis, use_normal_lh);
            if (descendant_baseline >= 0.0f) return descendant_baseline;
        }
    }
    (void)lycon;
    return -1.0f;
}

float layout_table_baseline_for_source(LayoutContext* lycon, ViewBlock* table,
                                       bool prefer_last) {
    if (!table) return -1.0f;
    if (table->blk) {
        float cached = radiant::layout_select_cached_baseline(
            table, table->block()->first_line_baseline,
            table->block()->last_line_baseline, false, -1.0f);
        if (cached >= 0.0f) return cached;
    }
    // captured during table layout so baseline-source:last is not recomputed
    return prefer_last
        ? find_last_baseline_recursive(lycon, static_cast<View*>(table), 0.0f, true)
        : find_first_baseline_recursive(lycon, static_cast<View*>(table), 0.0f, true);
}

static float find_cell_baseline(LayoutContext* lycon, ViewTableCell* tcell,
                                bool use_vertical_dominant_baseline = true) {
    float baseline = find_first_baseline_recursive(lycon, static_cast<View*>(tcell), 0);
    if (baseline < 0.0f && tcell->blk &&
        tcell->block()->first_line_baseline > 0.0f) {
        baseline = tcell->block()->first_line_baseline;
    }
    if (use_vertical_dominant_baseline && baseline >= 0.0f && tcell->blk &&
        layout_block_inline_axis_is_vertical(tcell)) {
        float line_extent = tcell->block()->first_line_max_ascender +
            tcell->block()->first_line_max_descender;
        if (line_extent > 0.0f) {
            // CSS Writing Modes uses the central baseline in vertical
            baseline = baseline - tcell->block()->first_line_max_ascender +
                line_extent / 2.0f;
        }
    }
    if (baseline < 0) {
        bool has_line_box = false;
        for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_INLINE) {
                has_line_box = true;
                break;
            }
        }
        if (has_line_box && tcell->font) {
            float fallback_ascent = tcell->fontp()->font_size * 0.8f;
            baseline = radiant::compute_font_baseline_ascender(
                lycon, tcell->font, false, fallback_ascent);
        }
        if (baseline < 0) {
            float content_edge_bottom = tcell->height;
            if (tcell->bound) {
                TableCellInsets insets = table_cell_insets(tcell);
                content_edge_bottom -= insets.border.bottom + insets.padding.bottom;
            }
            baseline = content_edge_bottom;
        }
    }
    return baseline;
}

static float find_table_row_baseline(LayoutContext* lycon, ViewTableRow* trow) {
    if (!trow) return -1.0f;
    float row_baseline = -1.0f;
    trow->each_cell( [&](ViewTableCell* tcell) {
        if (!tcell->td) return;
        // The table-root baseline remains the row's table-coordinate baseline;
        // the vertical row-sharing group, not when exporting that baseline.
        float cell_baseline = tcell->y +
            find_cell_baseline(lycon, tcell, false);
        if (cell_baseline > row_baseline) {
            row_baseline = cell_baseline;
        }
    });
    return row_baseline;
}

static bool table_cell_is_baseline_aligned(ViewTableCell* tcell) {
    return tcell->td && tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE &&
        !tcell->td->is_empty;
}

static float apply_row_baseline_alignment(LayoutContext* lycon, ViewTableRow* trow, float* row_height) {
    bool has_baseline_cells = false;
    trow->each_cell( [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            has_baseline_cells = true;
        }
    });
    if (!has_baseline_cells) return 0;


    float max_baseline = 0;
    int baseline_cell_count = 0;
    trow->each_cell( [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            float baseline = find_cell_baseline(lycon, tcell);
            if (baseline >= 0) {
                if (baseline > max_baseline) max_baseline = baseline;
                baseline_cell_count++;
            }
        }
    });
    if (baseline_cell_count < 2) return 0;
    trow->each_cell( [&](ViewTableCell* tcell) {
        if (table_cell_is_baseline_aligned(tcell)) {
            float cell_baseline = find_cell_baseline(lycon, tcell);
            if (cell_baseline < 0) return;  // Skip cells without real baselines
            float shift = max_baseline - cell_baseline;
            if (shift > 0.5f) {
                shift_table_cell_vertical_align_children(tcell, shift);
                float content_height = measure_cell_content_height(lycon, tcell);
                float needed_height = content_height;
                TableCellInsets insets = table_cell_insets(tcell);
                needed_height += insets.padding.top + insets.padding.bottom +
                    insets.border.top + insets.border.bottom;
                needed_height += shift;
                if (needed_height > tcell->height) {
                    tcell->height = needed_height;
                }
            }
        }
    });
    float new_row_height = *row_height;
    trow->each_cell( [&](ViewTableCell* tcell) {
        if (tcell->td && tcell->td->row_span <= 1 && tcell->height > new_row_height) {
            new_row_height = tcell->height;
        }
    });
    if (new_row_height > *row_height) {
        *row_height = new_row_height;
    }
    return new_row_height - *row_height;
}

static float compute_cell_strut_baseline(LayoutContext* lycon, ViewTableCell* tcell) {
    if (!lycon || !tcell) return 0.0f;
    LayoutContextScope context_scope(lycon);
    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    layout_setup_block_font_metrics(lycon);
    float half_leading = (lycon->block.line_height -
        (lycon->block.init_ascender + lycon->block.init_descender)) / 2.0f;
    float baseline = lycon->block.init_ascender + half_leading;
    return baseline;
}

static float compute_inline_atomic_baseline_for_cell(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return 0.0f;
    float item_height = block->height + (block->bound ?
        block->boundary()->margin.top + block->boundary()->margin.bottom : 0.0f);
    bool is_inline_table = block->view_type == RDT_VIEW_TABLE &&
        (block->display.outer == CSS_VALUE_INLINE ||
         block->display.outer == CSS_VALUE_INLINE_BLOCK);
    if (is_inline_table) {
        float table_baseline = find_first_baseline_recursive(lycon, static_cast<View*>(block), 0.0f, true);
        if (table_baseline >= 0.0f) {
            return (block->bound ? block->boundary()->margin.top : 0.0f) + table_baseline;
        }
    }
    if (block->display.inner == RDT_DISPLAY_REPLACED) {
        return block->height + (block->bound ? block->boundary()->margin.top : 0.0f);
    }
    if (block->blk && block->block_mut()->last_line_max_ascender > 0.0f) {
        bool overflow_visible = !block->scroller ||
            (block->scroll()->overflow_x == CSS_VALUE_VISIBLE &&
             block->scroll()->overflow_y == CSS_VALUE_VISIBLE);
        if (overflow_visible) {
            return (block->bound ? block->boundary()->margin.top : 0.0f) +
                block->block()->last_line_max_ascender;
        }
    }
    return item_height;
}

static bool table_empty_inline_atomic_line_top(LayoutContext* lycon,
                                               ViewTableCell* tcell,
                                               ViewBlock* block,
                                               float* line_top) {
    if (!block || !line_top) return false;
    float item_height = block->height + (block->bound ?
        block->boundary()->margin.top + block->boundary()->margin.bottom : 0.0f);
    if (item_height > 0.5f) return false;
    float strut_baseline = compute_cell_strut_baseline(lycon, tcell);
    if (strut_baseline <= 0.0f) return false;
    float item_baseline = compute_inline_atomic_baseline_for_cell(lycon, block);
    *line_top = block->y + item_baseline - strut_baseline;
    return true;
}

static float find_cell_content_top_for_vertical_align(LayoutContext* lycon, ViewTableCell* tcell,
                                                      float fallback_top) {
    float content_top = fallback_top;
    bool found_line_box_top = false;
    for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
        if (!child->view_type) continue;
        if (child->view_type == RDT_VIEW_INLINE_BLOCK || child->view_type == RDT_VIEW_TABLE) {
            ViewBlock* block = lam::view_require_block(child);
            float line_top = 0.0f;
            if (!table_empty_inline_atomic_line_top(lycon, tcell, block, &line_top)) continue;
            if (!found_line_box_top || line_top < content_top) {
                content_top = line_top;
                found_line_box_top = true;
            }
        }
    }
    return content_top;
}

static void shift_table_cell_vertical_align_children(ViewTableCell* tcell,
                                                      float y_adjustment) {
    if (!tcell || y_adjustment == 0.0f) return;
    auto shift_view = [&](View* child, int) -> bool {
        if (table_cell_vertical_align_skips_child(child) || !child->view_type) return false;
        child->y += y_adjustment;
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            for (TextRect* rect = text->rect; rect; rect = rect->next) rect->y += y_adjustment;
        }
        return child->view_type == RDT_VIEW_INLINE;
    };
    auto no_leave = [](View*, int) {};
    layout_walk_view_descendants(
        static_cast<View*>(lam::view_require_element(tcell)->first_child),
        shift_view, no_leave, false);
}

static TableCellContentExtent table_cell_vertical_bounds(ViewTableCell* tcell,
                                                         bool include_margins = false) {
    TableCellContentExtent bounds = {};
    bounds.min_y = 1e9f;
    if (!tcell) return bounds;
    for_each_table_cell_vertical_align_child(lam::view_require_element(tcell), [&](View* child) {
        if (!child->view_type) return;
        float child_top = child->y;
        if (include_margins && (child->view_type == RDT_VIEW_BLOCK ||
                                child->view_type == RDT_VIEW_LIST_ITEM ||
                                child->view_type == RDT_VIEW_INLINE_BLOCK)) {
            ViewBlock* block = lam::view_require_block(child);
            if (block->bound) child_top -= block->boundary()->margin.top;
        }
        if (child_top < bounds.min_y) bounds.min_y = child_top;
        float child_bottom = child->y + child->height;
        if (child_bottom > bounds.max_y) bounds.max_y = child_bottom;
        bounds.has_content = true;
    });
    return bounds;
}

static float table_cell_vertical_align_target(int valign, float content_area_height,
                                              float content_height, float content_start_y,
                                              bool clamp_to_content = false) {
    if (clamp_to_content && content_area_height <= content_height) {
        return content_start_y;
    }
    if (valign == TableCellProp::CELL_VALIGN_MIDDLE) {
        return content_start_y + (content_area_height - content_height) / 2.0f;
    }
    if (valign == TableCellProp::CELL_VALIGN_BOTTOM) {
        return content_start_y + content_area_height - content_height;
    }
    return content_start_y;
}

static void apply_cell_vertical_align(LayoutContext* lycon, ViewTableCell* tcell, float cell_height, float content_height) {
    if (tcell->td->is_empty && tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE) {
        tcell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
    }
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_TOP) {
        return; // No adjustment needed
    }
    if (tcell->td->vertical_align == TableCellProp::CELL_VALIGN_BASELINE) {
        return;
    }
    TableCellInsets insets = table_cell_insets(tcell);
    float cell_content_area = cell_height - insets.border.top - insets.border.bottom -
                              insets.padding.top - insets.padding.bottom;
    float content_start_y = insets.border.top + insets.padding.top;
    float target_y = table_cell_vertical_align_target(
        tcell->td->vertical_align, cell_content_area, content_height, content_start_y);
    TableCellContentExtent bounds = table_cell_vertical_bounds(tcell, true);
    if (!bounds.has_content) return;
    float current_content_top = bounds.min_y;
    current_content_top = find_cell_content_top_for_vertical_align(lycon, tcell, current_content_top);
    float y_adjustment = target_y - current_content_top;
    shift_table_cell_vertical_align_children(tcell, y_adjustment);
}

static void position_cell_text_children(ViewTableCell* tcell) {
    float content_x = 1; // 1px border
    float content_y = 1;
    TableCellInsets insets = table_cell_insets(tcell);
    content_x += insets.padding.left;
    content_y += insets.padding.top;
    for (View* child = lam::view_require_element(tcell)->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            child->x = content_x;
            child->y = content_y;
        }
    }
}

template <typename Fn>
static int for_each_table_span_column(int start_col, int span, int columns, Fn fn) {
    if (span <= 0 || start_col >= columns) return 0;
    int end_col = start_col + span;
    int count = 0;
    for (int c = start_col; c < end_col && c < columns; c++) {
        fn(c);
        count++;
    }
    return count;
}

static float table_sum_span_columns(float* col_widths, int start_col, int span, int columns) {
    if (!col_widths) return 0.0f;
    float width = 0.0f;
    for_each_table_span_column(start_col, span, columns, [&](int c) {
        width += col_widths[c];
    });
    return width;
}

static void table_assign_span_columns(float* col_widths, int start_col, int span,
                                      int columns, float width, float* assigned_total) {
    if (!col_widths) return;
    for_each_table_span_column(start_col, span, columns, [&](int c) {
        col_widths[c] = width;
        if (assigned_total) *assigned_total += width;
    });
}

static void table_assign_columns(float* col_widths, int columns, float width) {
    table_assign_span_columns(col_widths, 0, columns, columns, width, NULL);
}

static void table_copy_columns(float* dst, float* src, int columns) {
    if (!dst || !src) return;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        dst[c] = src[c];
    });
}

static void table_scale_columns(float* col_widths, int columns, float scale) {
    if (!col_widths) return;
    for_each_table_span_column(0, columns, columns, [&](int c) {
        col_widths[c] *= scale;
    });
}

static void table_position_row_group_box(ViewTable* table, TableMetadata* meta,
                                         ViewBlock* child, float* col_widths,
                                         float* col_x_positions, int columns,
                                         bool has_direct_float, float* current_y) {
    float tbody_content_width = 0.0f;
    if (table->tb->border_collapse) {
        tbody_content_width = col_x_positions[columns] - col_x_positions[0];
    } else {
        tbody_content_width = table_sum_span_columns(col_widths, 0, columns, columns);
        float column_spacing = table_inter_spacing(table, true);
        int effective_columns = table_effective_column_count(
            table, meta, col_widths, columns);
        if (column_spacing > 0.0f && effective_columns > 1) {
            tbody_content_width += (effective_columns - 1) * column_spacing;
        }
    }
    if (tbody_content_width <= 0.0f && table->width > 0.0f && columns == 0) {
        tbody_content_width = table->width;
    }
    *current_y = table_clear_direct_float_intrusion(
        table, *current_y, tbody_content_width, has_direct_float);
    float float_shift_x = 0.0f;
    if (has_direct_float && table->width > 0.0f && tbody_content_width > 0.0f) {
        float left_intrusion = table_direct_float_intrusion(table, *current_y, table->width, CSS_VALUE_LEFT);
        float max_shift = table->width - tbody_content_width;
        if (max_shift < 0.0f) max_shift = 0.0f;
        if (left_intrusion > max_shift) left_intrusion = max_shift;
        float_shift_x = left_intrusion;
    }
    child->x = table->tb->border_collapse
        ? meta->collapsed_border_left / 2.0f + float_shift_x
        : col_x_positions[0] + float_shift_x;
    child->y = *current_y;
    child->width = tbody_content_width;
}

static bool table_columns_within_tolerance(float* col_widths, int columns, float tolerance) {
    if (!col_widths || columns <= 0) return false;
    float first_width = col_widths[0];
    for (int i = 1; i < columns; i++) {
        if (fabsf(col_widths[i] - first_width) > tolerance) return false;
    }
    return true;
}

template <typename Eligible, typename Weight>
static void table_distribute_extra(float* col_widths, int columns, float extra,
                                   Eligible eligible, Weight weight) {
    if (!col_widths || columns <= 0 || extra == 0.0f) return;
    float total_weight = 0.0f;
    int eligible_count = 0;
    for (int i = 0; i < columns; i++) {
        if (!eligible(i)) continue;
        total_weight += max(weight(i), 0.0f);
        eligible_count++;
    }
    if (eligible_count == 0) return;
    for (int i = 0; i < columns; i++) {
        if (!eligible(i)) continue;
        float share = total_weight > 0.0f
            ? extra * max(weight(i), 0.0f) / total_weight
            : extra / eligible_count;
        col_widths[i] += share;
    }
}

static void table_grow_percent_columns(TableMetadata* meta, float* col_widths,
                                       int columns, float extra) {
    if (!meta || !col_widths) return;
    table_distribute_extra(col_widths, columns, extra,
        [&](int c) { return meta->col_percent_widths[c] > 0.0f; },
        [&](int c) { return meta->col_percent_widths[c]; });
}

static bool table_apply_percent_column_distribution(TableMetadata* meta, float* col_widths,
                                                    int columns, float total_percent_col_width,
                                                    float available_content_width,
                                                    float min_table_content_width) {
    if (!meta || !col_widths || total_percent_col_width <= 0.0f ||
        available_content_width <= min_table_content_width) {
        return false;
    }
    float assigned_total = 0.0f;
    for (int i = 0; i < columns; i++) {
        float percent = meta->col_percent_widths[i];
        float min_floor = meta->col_single_min_widths[i] > 0.0f
            ? meta->col_single_min_widths[i] : 0.0f;
        float target = 0.0f;
        if (percent > 0.0f) {
            target = available_content_width * percent / 100.0f;
        } else {
            target = meta->col_max_widths[i] > min_floor
                ? meta->col_max_widths[i] : min_floor;
        }
        if (target < min_floor) target = min_floor;
        col_widths[i] = target;
        assigned_total += target;
    }
    if (assigned_total > available_content_width) {
        float excess = assigned_total - available_content_width;
        while (excess > 0.01f) {
            float shrink_capacity = 0.0f;
            for (int i = 0; i < columns; i++) {
                if (col_widths[i] > meta->col_min_widths[i]) {
                    shrink_capacity += col_widths[i] - meta->col_min_widths[i];
                }
            }
            if (shrink_capacity <= 0.01f) break;
            float shrink_step = excess < shrink_capacity ? excess : shrink_capacity;
            for (int i = 0; i < columns; i++) {
                float capacity = col_widths[i] - meta->col_min_widths[i];
                if (capacity <= 0.0f) continue;
                float amount = shrink_step * capacity / shrink_capacity;
                if (amount > capacity) amount = capacity;
                col_widths[i] -= amount;
            }
            excess -= shrink_step;
        }
    } else if (assigned_total < available_content_width) {
        float extra = available_content_width - assigned_total;
        int auto_grow_count = 0;
        for (int i = 0; i < columns; i++) {
            if (meta->col_percent_widths[i] <= 0.0f) {
                auto_grow_count++;
            }
        }
        if (auto_grow_count > 0) {
            table_distribute_extra(col_widths, columns, extra,
                [&](int i) { return meta->col_percent_widths[i] <= 0.0f; },
                [&](int i) { return col_widths[i]; });
        } else {
            table_grow_percent_columns(meta, col_widths, columns, extra);
        }
    }
    return true;
}

static void table_apply_auto_column_width_distribution(TableMetadata* meta, float* col_widths,
                                                       int columns, float available_content_width,
                                                       float min_table_content_width,
                                                       float pref_table_content_width) {
    if (!meta || !col_widths) return;
    if (fabsf(available_content_width - pref_table_content_width) < 0.01f) {
        table_copy_columns(col_widths, meta->col_max_widths, columns);
        return;
    }
    if (available_content_width <= pref_table_content_width) {
        if (available_content_width < min_table_content_width) {
            table_copy_columns(col_widths, meta->col_min_widths, columns);
            return;
        }
        float factor = pref_table_content_width > min_table_content_width
            ? (available_content_width - min_table_content_width) /
              (pref_table_content_width - min_table_content_width) : 0.0f;
        for (int i = 0; i < columns; i++) {
            float min_width = meta->col_min_widths[i];
            float range = meta->col_max_widths[i] - min_width;
            col_widths[i] = min_width + (range > 0.0f ? range * factor : 0.0f);
        }
        return;
    }

    table_copy_columns(col_widths, meta->col_max_widths, columns);
    float extra_space = available_content_width - pref_table_content_width;
    int auto_col_count = 0;
    for (int i = 0; i < columns; i++) {
        if (!meta->col_has_explicit_width[i]) auto_col_count++;
    }
    if (auto_col_count > 0) {
        table_distribute_extra(col_widths, columns, extra_space,
            [&](int i) { return !meta->col_has_explicit_width[i]; },
            [&](int i) { return col_widths[i]; });
    } else if (pref_table_content_width > 0.0f) {
        table_distribute_extra(col_widths, columns, extra_space,
            [](int) { return true; }, [&](int i) { return col_widths[i]; });
    }
}

static float table_apply_auto_available_width_constraint(
    LayoutContext* lycon, ViewTable* table, TableMetadata* meta,
    float* pref_table_width, float min_table_width) {
    if (!pref_table_width) return 0.0f;
    float container_width = 0.0f;
    bool margins_already_subtracted = false;
    bool table_box_already_subtracted = false;
    // float-avoidance BFCs already hand over a reduced content box.
    if (lycon->block.content_width > 0.0f) {
        container_width = lycon->block.content_width;
        margins_already_subtracted = true;
        // collapsed borders are added back when the final table border box is
        // published, so they are not included in the parent content width.
        table_box_already_subtracted = !table->tb->border_collapse;
    }
    if (container_width <= 0.0f) {
        ViewBlock* parent = lam::view_as_block(static_cast<View*>(table->parent));
        if (parent && parent->width > 0.0f) {
            container_width = parent->width;
            if (parent->bound) {
                container_width -= layout_box_metrics(parent).pad_border_h;
            }
        }
    }
    if (container_width <= 0.0f) {
        container_width = lycon->line.right - lycon->line.left;
    }
    if (container_width <= 0.0f && lycon->available_space.width.is_definite()) {
        container_width = lycon->available_space.width.to_px_or_zero();
    }
    if (!margins_already_subtracted) {
        float margin_left = 0.0f, margin_right = 0.0f;
        if (table->bound) {
            margin_left = table->boundary()->margin.left;
            margin_right = table->boundary()->margin.right;
        }
        container_width -= margin_left + margin_right;
    }
    float table_horizontal_overhead = 0.0f;
    if (!table_box_already_subtracted) {
        if (!table->tb->border_collapse && table->bound) {
            BoxMetrics table_box = layout_box_metrics(table);
            if (table->boundary()->padding.left > 0.0f) {
                table_horizontal_overhead += table->boundary()->padding.left;
            }
            if (table->boundary()->padding.right > 0.0f) {
                table_horizontal_overhead += table->boundary()->padding.right;
            }
            table_horizontal_overhead += table_box.border_h;
        } else if (table->tb->border_collapse) {
            table_horizontal_overhead += meta->collapsed_border_left / 2.0f +
                meta->collapsed_border_right / 2.0f;
        }
    }
    float max_available_width = container_width - table_horizontal_overhead;
    if (max_available_width < 0.0f) max_available_width = 0.0f;


    if (max_available_width > 0.0f && *pref_table_width > max_available_width) {
        *pref_table_width = max_available_width;
    } else if (max_available_width == 0.0f && *pref_table_width > min_table_width) {
        *pref_table_width = min_table_width;
    }
    return max_available_width;
}

static float table_explicit_content_area_for_auto_layout(ViewTable* table,
                                                         TableMetadata* meta,
                                                         float explicit_table_width) {
    float explicit_content_area = explicit_table_width;
    if (table->tb->border_collapse) {
        float half_left = meta->collapsed_border_left / 2.0f;
        float half_right = meta->collapsed_border_right / 2.0f;
        explicit_content_area -= (half_left + half_right);
        return explicit_content_area;
    }
    bool is_border_box = layout_uses_border_box(table);
    if (is_border_box && table->bound && table->boundary_mut()->border) {
        explicit_content_area -= layout_box_metrics(table).border_h;
    }
    if (is_border_box && table->bound &&
        table->boundary_mut()->padding.left >= 0.0f && table->boundary_mut()->padding.right >= 0.0f) {
        explicit_content_area -= layout_box_metrics(table).padding_h;
    }
    return explicit_content_area;
}

static float table_fixed_css_padding_box_width(ViewTable* table, float fixed_table_width) {
    float css_padding_box = fixed_table_width;
    bool fixed_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    if (fixed_is_border_box && table->bound && table->boundary_mut()->border) {
        css_padding_box -= layout_box_metrics(table).border_h;
    } else if (!fixed_is_border_box && table->bound) {
        if (table->boundary_mut()->padding.left >= 0.0f) css_padding_box += table->boundary_mut()->padding.left;
        if (table->boundary_mut()->padding.right >= 0.0f) css_padding_box += table->boundary_mut()->padding.right;
    }
    return css_padding_box;
}

static void table_apply_minmax_width_constraints(ViewTable* table, TableMetadata* meta,
                                                 float* col_widths, int columns,
                                                 float* table_width,
                                                 float table_padding_horizontal) {
    if (!table->blk || !table_width) return;
    float early_border_width = table->tb->border_collapse
        ? meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f
        : ((table->bound && table->boundary_mut()->border) ? layout_box_metrics(table).border_h : 0.0f);
    float old_table_width = *table_width;
    float border_box_width = *table_width + early_border_width;
    bool minmax_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    float minmax_extra = minmax_is_border_box ? 0.0f :
        early_border_width + table_padding_horizontal;
    // max-width cannot compress a table below its minimum content border-box.
    if (table->block()->given_max_width >= 0.0f) {
        float max_w_bb = table->block()->given_max_width + minmax_extra;
        float min_col_total = table_sum_span_columns(meta->col_min_widths, 0, columns, columns);
        float min_spacing = 0.0f;
        float column_spacing = table_inter_spacing(table, true);
        if (column_spacing > 0.0f) {
            min_spacing = (columns + 1) * column_spacing;
        }
        float min_bb = min_col_total + min_spacing + table_padding_horizontal + early_border_width;
        if (max_w_bb < min_bb) max_w_bb = min_bb;
        if (border_box_width > max_w_bb) {
            *table_width = max_w_bb - early_border_width;
            if (*table_width < 0.0f) *table_width = 0.0f;
        }
    }
    if (table->block()->given_min_width >= 0.0f) {
        float min_w_bb = table->block()->given_min_width + minmax_extra;
        if (border_box_width < min_w_bb) {
            *table_width = min_w_bb - early_border_width;
        }
    }
    if (*table_width == old_table_width || columns <= 0) return;
    float overhead = table_padding_horizontal;
    float column_spacing = table_inter_spacing(table, true);
    if (column_spacing > 0.0f) {
        overhead += (columns + 1) * column_spacing;
    }
    float new_col_total = *table_width - overhead;
    if (new_col_total < 0.0f) new_col_total = 0.0f;
    float old_col_total = old_table_width - overhead;
    if (old_col_total > 0.0f) {
        table_scale_columns(col_widths, columns, new_col_total / old_col_total);
    } else if (new_col_total > 0.0f) {
        table_assign_columns(col_widths, columns, new_col_total / columns);
    }
}

static float table_prepare_final_padding_box_width(ViewTable* table, TableMetadata* meta,
                                                   float* col_widths, int columns,
                                                   float* table_padding_horizontal) {
    if (table_padding_horizontal) *table_padding_horizontal = 0.0f;
    for (int i = 0; i < columns; i++) {
        if (!meta->col_collapsed[i]) continue;
        meta->col_original_widths[i] = col_widths[i];
        col_widths[i] = 0.0f;
        meta->col_min_widths[i] = 0.0f;
        meta->col_max_widths[i] = 0.0f;
    }
    float table_width = table_sum_span_columns(col_widths, 0, columns, columns);
    float column_spacing = table_inter_spacing(table, true);
    int effective_columns = table_effective_column_count(table, meta, col_widths, columns);
    if (column_spacing > 0.0f) {
        if (effective_columns > 1) table_width += (effective_columns - 1) * column_spacing;
        if (effective_columns > 0) table_width += 2.0f * column_spacing;
    }
    if (!table->tb->border_collapse && table->bound &&
        table->boundary_mut()->padding.left >= 0.0f && table->boundary_mut()->padding.right >= 0.0f) {
        float padding_h = layout_box_metrics(table).padding_h;
        if (table_padding_horizontal) *table_padding_horizontal = padding_h;
        table_width += padding_h;
    }
    return table_width;
}

static int table_positive_span_attr(ViewElement* element) {
    const char* span_str = element ? element->get_attribute("span") : NULL;
    int span = (span_str && *span_str) ? (int)str_to_int64_default(span_str, strlen(span_str), 0) : 1; // INT_CAST_OK: span count from attribute
    return span > 0 ? span : 1;
}

static float table_resolve_fixed_explicit_width(LayoutContext* lycon, ViewTable* table) {
    float fixed_explicit_width = 0.0f;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    float container_width = lycon->available_space.width.is_definite()
                        ? lycon->available_space.width.value
                        : lycon->block.content_width;
                    if (container_width <= 0.0f) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0.0f) {
                        fixed_explicit_width = container_width * percentage / 100.0;
                    }
                } else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    fixed_explicit_width = resolve_length_value(
                        lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                }
            }
        }
    }
    if (fixed_explicit_width == 0.0f && lycon->block.given_width > 0.0f) {
        fixed_explicit_width = lycon->block.given_width;
    }
    return fixed_explicit_width;
}

static float table_fixed_content_width_for_columns(ViewTable* table,
                                                   float fixed_explicit_width,
                                                   int columns) {
    float content_width = fixed_explicit_width;
    bool fixed_width_is_border_box = table->tb->border_collapse ||
        layout_uses_border_box(table);
    if (fixed_width_is_border_box) {
        float table_border_h = 0.0f;
        if (table->bound && table->boundary_mut()->border) {
            table_border_h = layout_box_metrics(table).border_h;
        }
        content_width -= table_border_h;
    }
    // fixed-layout column percentages resolve against content tracks, not table border or spacing overhead.
    float column_spacing = table_inter_spacing(table, true);
    if (column_spacing > 0.0f) {
        content_width -= (columns + 1) * column_spacing;
    }
    return content_width;
}

static float table_resolve_percent_or_length_width(LayoutContext* lycon,
                                                   const CssValue* value,
                                                   float percentage_basis) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return (float)(percentage_basis * value->data.percentage.value / 100.0);
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        return resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
    }
    if (value->type == CSS_VALUE_TYPE_CALC || value->type == CSS_VALUE_TYPE_FUNCTION) {
        // fixed table percentages in calc() use the same content-track basis
        // as a standalone percentage.
        BlockContext percentage_base = {};
        percentage_base.content_width = percentage_basis;
        BlockContext* saved_parent = lycon->block.parent;
        lycon->block.parent = &percentage_base;
        float resolved = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, value);
        lycon->block.parent = saved_parent;
        return isnan(resolved) ? 0.0f : resolved;
    }
    return 0.0f;
}

static float table_resolve_fixed_column_css_width(LayoutContext* lycon, ViewElement* column,
                                                  float content_width) {
    if (!column || column->node_type != DOM_NODE_ELEMENT) return 0.0f;
    DomElement* dom_col = column->as_element();
    if (!dom_col || !dom_col->specified_style) return 0.0f;
    CssDeclaration* w_decl = style_tree_get_declaration(dom_col->specified_style, CSS_PROPERTY_WIDTH);
    if (!w_decl || !w_decl->value) return 0.0f;
    return table_resolve_percent_or_length_width(lycon, w_decl->value, content_width);
}

static int table_apply_fixed_column_css_width(LayoutContext* lycon, ViewElement* column,
                                              float* explicit_col_widths, int col_idx,
                                              int columns, float content_width,
                                              float* total_explicit) {
    int span = table_positive_span_attr(column);
    float col_width = table_resolve_fixed_column_css_width(lycon, column, content_width);
    if (col_width > 0.0f) {
        float per_col = col_width / span;
        table_assign_span_columns(explicit_col_widths, col_idx, span,
                                  columns, per_col, total_explicit);
    }
    return span;
}

static float table_resolve_fixed_first_row_cell_width(LayoutContext* lycon, ViewTable* table,
                                                      ViewTableCell* cell, float content_width,
                                                      int col) {
    float cell_width = 0.0f;
    if (cell->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = cell->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                cell_width = table_resolve_percent_or_length_width(
                    lycon, width_decl->value, content_width);
            }
        }
    }
    if (cell_width <= 0.0f || layout_uses_border_box(cell)) return cell_width;
    TableCellInsets insets = table_cell_insets(cell);
    if (cell->bound && cell->boundary_mut()->padding.left >= 0 && cell->boundary_mut()->padding.right >= 0) {
        cell_width += insets.padding.left + insets.padding.right;
    }
    if (!table->tb->border_collapse) {
        cell_width += insets.border.left + insets.border.right;
    }
    return cell_width;
}

static int table_apply_fixed_first_row_cell_width(LayoutContext* lycon, ViewTable* table,
                                                  ViewTableCell* cell,
                                                  float* explicit_col_widths, int col,
                                                  int columns, float content_width,
                                                  float* total_explicit,
                                                  int* unspecified_cols) {
    int span = cell->td->col_span;
    if (explicit_col_widths[col] > 0.0f) {
        return span;
    }
    float cell_width = table_resolve_fixed_first_row_cell_width(
        lycon, table, cell, content_width, col);
    if (cell_width > 0.0f) {
        float per_col_width = cell_width / span;
        table_assign_span_columns(explicit_col_widths, col, span,
                                  columns, per_col_width, total_explicit);
    } else {
        *unspecified_cols += span;
    }
    return span;
}

static void table_distribute_fixed_column_widths(float* explicit_col_widths, int columns,
                                                 float* content_width,
                                                 float total_explicit,
                                                 int unspecified_cols) {
    if (!explicit_col_widths || columns <= 0 || !content_width) return;
    if (total_explicit > 0.0f) {
        float remaining_width = *content_width - total_explicit;
        if (unspecified_cols > 0 && remaining_width > 0.0f) {
            float width_per_unspecified = remaining_width / unspecified_cols;
            for (int i = 0; i < columns; i++) {
                if (explicit_col_widths[i] == 0.0f) {
                    explicit_col_widths[i] = width_per_unspecified;
                }
            }
        } else if (remaining_width < 0.0f) {
            // CSS 2.1 §17.5.2.1: if explicit columns exceed table width, the table widens.
            *content_width = total_explicit;
        }
    } else {
        float width_per_col = *content_width / columns;
        table_assign_columns(explicit_col_widths, columns, width_per_col);
    }
}

static float table_resolve_fixed_explicit_height(LayoutContext* lycon, ViewTable* table) {
    float explicit_height = 0.0f;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value) {
                explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            }
        }
        if (explicit_height <= 0.0f && table->blk && table->block_mut()->given_height > 0.0f) {
            explicit_height = table->block()->given_height;
        }
    }
    return explicit_height;
}

static void table_apply_fixed_height_distribution(LayoutContext* lycon, ViewTable* table, int rows) {
    float explicit_table_height = table_resolve_fixed_explicit_height(lycon, table);
    if (explicit_table_height <= 0.0f) return;


    float content_height = explicit_table_height;
    bool height_is_border_box = table->tb->border_collapse || layout_uses_border_box(table);
    if (height_is_border_box && table->bound && table->boundary_mut()->border) {
        content_height -= layout_box_metrics(table).border_v;
    }
    if (!table->tb->border_collapse && table->bound) {
        if (table->boundary_mut()->padding.top >= 0.0f) content_height -= table->boundary_mut()->padding.top;
        if (table->boundary_mut()->padding.bottom >= 0.0f) content_height -= table->boundary_mut()->padding.bottom;
    }
    float row_spacing = table_inter_spacing(table, false);
    if (row_spacing > 0.0f && rows > 0) {
        content_height -= (rows + 1) * row_spacing;
    }
    table->tb->fixed_row_height = rows > 0 ? content_height / rows : 0.0f;
}

static void table_raise_column_width_constraints(TableMetadata* meta, float* col_widths,
                                                 int col, float width) {
    if (!meta || !col_widths || col < 0 || col >= meta->column_count || width <= 0.0f) return;
    if (width > meta->col_min_widths[col]) meta->col_min_widths[col] = width;
    if (width > meta->col_max_widths[col]) meta->col_max_widths[col] = width;
    if (width > col_widths[col]) col_widths[col] = width;
}

static void table_clamp_column_max_width(TableMetadata* meta, float* col_widths,
                                         int col, float max_width) {
    if (!meta || !col_widths || col < 0 || col >= meta->column_count || max_width < 0.0f) return;
    if (meta->col_max_widths[col] > max_width) meta->col_max_widths[col] = max_width;
    if (col_widths[col] > max_width) col_widths[col] = max_width;
}

static float table_resolve_column_length_constraint(LayoutContext* lycon, ViewBlock* col_elem,
                                                    CssPropertyCode property, float box_value,
                                                    bool has_box_value, float unset_value) {
    CssDeclaration* decl = col_elem && col_elem->specified_style
        ? style_tree_get_declaration(col_elem->specified_style, property) : nullptr;
    if (decl && decl->value &&
        decl->value->type != CSS_VALUE_TYPE_LENGTH &&
        decl->value->type != CSS_VALUE_TYPE_NUMBER) {
        // percentage and calc() column sizes are constraints on the table
        // algorithm, not definite used widths during auto layout.
        return unset_value;
    }
    if (has_box_value) return box_value;
    if (!col_elem || !col_elem->specified_style) return unset_value;
    if (decl && decl->value &&
        (decl->value->type == CSS_VALUE_TYPE_LENGTH ||
         decl->value->type == CSS_VALUE_TYPE_NUMBER)) {
        return resolve_length_value(lycon, property, decl->value);
    }
    return unset_value;
}

static void table_apply_column_limit(LayoutContext* lycon, TableMetadata* meta,
                                     float* col_widths, int col, ViewBlock* col_elem,
                                     CssPropertyCode property, float box_value,
                                     bool has_box_value, bool maximum, float divisor) {
    float value = table_resolve_column_length_constraint(
        lycon, col_elem, property, box_value, has_box_value, maximum ? -1.0f : 0.0f);
    // CSS Tables treats a zero column max-size as an omitted cap when a
    // definite column width establishes the used size.
    if (maximum ? value <= 0.0f : value <= 0.0f) return;
    float scaled = divisor > 1.0f ? value / divisor : value;
    if (maximum) table_clamp_column_max_width(meta, col_widths, col, scaled);
    else table_raise_column_width_constraints(meta, col_widths, col, scaled);
}

static void table_apply_column_constraints(LayoutContext* lycon, TableMetadata* meta,
                                           float* col_widths, int col,
                                           ViewBlock* col_elem, float width_divisor,
                                           bool has_cell) {
    table_apply_column_limit(lycon, meta, col_widths, col, col_elem,
        CSS_PROPERTY_WIDTH, col_elem->blk ? col_elem->block()->given_width : 0.0f,
        col_elem->blk && col_elem->block_mut()->given_width > 0.0f, false, width_divisor);
    if (has_cell) {
        table_apply_column_limit(lycon, meta, col_widths, col, col_elem,
            CSS_PROPERTY_MIN_WIDTH, col_elem->blk ? col_elem->block()->given_min_width : -1.0f,
            col_elem->blk && col_elem->block_mut()->given_min_width >= 0.0f, false, width_divisor);
    }
    table_apply_column_limit(lycon, meta, col_widths, col, col_elem,
        CSS_PROPERTY_MAX_WIDTH, col_elem->blk ? col_elem->block()->given_max_width : -1.0f,
        col_elem->blk && col_elem->block_mut()->given_max_width >= 0.0f, true, width_divisor);
}

static void table_distribute_span_extra(float* col_widths, int col, int span, int columns,
                                        int actual_span, float current_total, float extra_needed,
                                        TableMetadata* meta = nullptr) {
    if (!col_widths || actual_span <= 0 || extra_needed <= 0.0f) return;
    (void)current_total;
    auto eligible = [&](int index) {
        if (index < col || index >= col + span) return false;
        // preserve single-column contributions when distributing a spanning
        // cell's deficit; only unconstrained columns absorb it first.
        if (meta && (meta->col_single_min_widths[index] > 0.0f ||
                     meta->col_has_explicit_width[index])) {
            for (int c = col; c < col + span && c < columns; c++) {
                if (meta->col_single_min_widths[c] <= 0.0f &&
                    !meta->col_has_explicit_width[c]) return false;
            }
        }
        return true;
    };
    table_distribute_extra(col_widths, columns, extra_needed, eligible,
        [&](int index) { return col_widths[index]; });
}

static float table_cell_internal_border_spacing(ViewTable* table, ViewTableCell* tcell) {
    if (!table || !table->tb || !tcell || !tcell->td) return 0.0f;
    if (table->tb->border_collapse || table->tb->border_spacing_h <= 0.0f) return 0.0f;
    if (tcell->td->col_span <= 1) return 0.0f;
    return table->tb->border_spacing_h * (tcell->td->col_span - 1);
}

static float table_column_visual_x(ViewTable* table, float* col_widths, float* col_x_positions,
                                   int start_col, int span, int columns);

typedef struct ColspanWidthContribution {
    ViewTableCell* cell;
    int col;
    int span;
    int order;
    float min_width;
    float pref_width;
    float cell_width;
} ColspanWidthContribution;

static int compare_colspan_width_contributions(ArrayListValue left, ArrayListValue right) {
    ColspanWidthContribution* a = (ColspanWidthContribution*)left;
    ColspanWidthContribution* b = (ColspanWidthContribution*)right;
    if (!a || !b) return 0;
    if (a->span != b->span) return a->span - b->span;
    return a->order - b->order;
}

static void apply_colspan_width_contribution(ViewTable* table, TableMetadata* meta,
                                             ColspanWidthContribution* contribution) {
    if (!table || !meta || !contribution || !contribution->cell) return;
    int columns = meta->column_count;
    int col = contribution->col;
    int span = contribution->span;
    if (span <= 1) return;
    float current_col_total = 0.0f;
    float current_min_total = 0.0f;
    float current_max_total = 0.0f;
    int actual_span = 0;
    for_each_table_span_column(col, span, columns, [&](int c) {
        current_col_total += meta->col_widths[c];
        current_min_total += meta->col_min_widths[c];
        current_max_total += meta->col_max_widths[c];
        actual_span++;
    });
    if (actual_span <= 0) return;
    float internal_spacing = table_cell_internal_border_spacing(table, contribution->cell);
    if (actual_span < span && internal_spacing > 0.0f) {
        internal_spacing = table->tb->border_spacing_h * (actual_span - 1);
    }
    // CSS 2.1 §17.5.2.2: After single-column cells establish the column
    float current_min_span_width = current_min_total + internal_spacing;
    if (contribution->min_width > current_min_span_width) {
        table_distribute_span_extra(meta->col_min_widths, col, span, columns, actual_span,
                                    current_min_total, contribution->min_width - current_min_span_width,
                                    meta);
    }
    float current_max_span_width = current_max_total + internal_spacing;
    if (contribution->pref_width > current_max_span_width) {
        table_distribute_span_extra(meta->col_max_widths, col, span, columns, actual_span,
                                    current_max_total, contribution->pref_width - current_max_span_width,
                                    meta);
    }
    float current_col_span_width = current_col_total + internal_spacing;
    if (contribution->cell_width > current_col_span_width) {
        table_distribute_span_extra(meta->col_widths, col, span, columns, actual_span,
                                    current_col_total, contribution->cell_width - current_col_span_width,
                                    meta);
    }
}

static float process_table_cell(LayoutContext* lycon, ViewTableCell* tcell, ViewTable* table,
                               float* col_widths, float* col_x_positions, int columns,
                               float* col_edge_max_border = nullptr,
                               bool* col_collapsed = nullptr,
                               float* col_original_widths = nullptr) {
    ViewBlock* cell = lam::view_require_block(tcell);
    // CSS 2.1 §17.5.5: Detect if ALL columns this cell spans are collapsed.
    // then zero the rendered width. Row heights are NOT recalculated.
    bool cell_is_collapsed = false;
    if (col_collapsed) {
        bool all_collapsed = true;
        int end_col = tcell->td->col_index + tcell->td->col_span;
        for (int c = tcell->td->col_index; c < end_col && c < columns; c++) {
            if (!col_collapsed[c]) { all_collapsed = false; break; }
        }
        cell_is_collapsed = all_collapsed;
    }
    {
        bool empty_cells_hide = (table->tb->empty_cells == TableProp::EMPTY_CELLS_HIDE);
        DomElement* cell_dom = lam::dom_require<DOM_NODE_ELEMENT>(tcell);
        if (cell_dom->specified_style) {
            CssDeclaration* ec_decl = style_tree_get_declaration(
                cell_dom->specified_style, CSS_PROPERTY_EMPTY_CELLS);
            if (ec_decl && ec_decl->value && ec_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                empty_cells_hide = (ec_decl->value->data.keyword == CSS_VALUE_HIDE);
            }
        }
        if (tcell->td->is_empty && !table->tb->border_collapse && empty_cells_hide) {
            tcell->td->hide_empty = 1;
        } else {
            tcell->td->hide_empty = 0;
        }
    }
    float cell_abs_x = table_column_visual_x(table, col_widths, col_x_positions,
                                             tcell->td->col_index, tcell->td->col_span, columns);
    cell->x = cell_abs_x - col_x_positions[0];
    cell->y = 0;
    position_cell_text_children(tcell);
    float cell_width = 0.0f;
    if (cell_is_collapsed && col_original_widths) {
        cell_width = table_sum_span_columns(
            col_original_widths, tcell->td->col_index, tcell->td->col_span, columns);
    } else {
        cell_width = table_sum_span_columns(
            col_widths, tcell->td->col_index, tcell->td->col_span, columns);
    }
    if (!table->tb->border_collapse) {
        cell_width += table_cell_internal_border_spacing(table, tcell);
    }
    cell->width = cell_width;
    // percentage padding was intentionally zero during intrinsic sizing; now
    // that the cell's final column span is known, resolve it against that span.
    layout_reresolve_percentage_box(cell, cell_width);
    // percentage cell padding resolves against the finalized table inline size,
    // not the parent constraint still held in table->width during cell layout.
    float table_inline_base = 0.0f;
    for (int c = 0; c < columns; c++) table_inline_base += col_widths[c];
    if (!table->tb->border_collapse && columns > 1) {
        table_inline_base += table->tb->border_spacing_h * (columns - 1);
    }
    if (cell->node_type == DOM_NODE_ELEMENT && cell->as_element()->specified_style) {
        CssPropertyCode padding_properties[4] = {
            CSS_PROPERTY_PADDING_TOP, CSS_PROPERTY_PADDING_RIGHT,
            CSS_PROPERTY_PADDING_BOTTOM, CSS_PROPERTY_PADDING_LEFT
        };
        float* padding_values[4] = {
            &cell->boundary_mut()->padding.top, &cell->boundary_mut()->padding.right,
            &cell->boundary_mut()->padding.bottom, &cell->boundary_mut()->padding.left
        };
        for (int side = 0; side < 4; side++) {
            CssDeclaration* declaration = style_tree_get_declaration(
                cell->as_element()->specified_style, padding_properties[side]);
            if (declaration && declaration->value &&
                layout_css_value_has_percentage(declaration->value)) {
                float resolved = resolve_table_relative_width(
                    lycon, declaration->value, table_inline_base);
                if (resolved >= 0.0f) *padding_values[side] = resolved;
            }
        }
    }
    layout_table_cell_content(lycon, cell, table);
    float explicit_cell_height = get_explicit_css_height(lycon, cell);
    float content_height = measure_cell_content_height(lycon, tcell);
    float cell_height_val = calculate_cell_height(lycon, tcell, table, content_height, explicit_cell_height);
    cell->height = cell_height_val;
    if (cell_is_collapsed) {
        cell->width = 0;
        cell->height = 0;
    }
    apply_cell_vertical_align(lycon, tcell, cell_height_val, content_height);
    float height_for_row = cell_height_val;
    if (tcell->td->row_span > 1) {
        height_for_row = 0.0f;
    }
    return height_for_row;
}

static void update_row_cells_after_height_change(LayoutContext* lycon, ViewTableRow* trow,
                                                 float row_height, bool only_single_rowspan,
                                                 bool grow_only) {
    if (!trow) return;
    trow->each_cell( [&](ViewTableCell* cell) {
        if (only_single_rowspan && (!cell->td || cell->td->row_span != 1)) return;
        if (grow_only && cell->height >= row_height) return;
        cell->height = row_height;
        float content_height = measure_cell_content_height(lycon, cell);
        apply_cell_vertical_align(lycon, cell, cell->height, content_height);
    });
}

static void apply_fixed_row_height(LayoutContext* lycon, ViewTableRow* trow, float fixed_height) {
    trow->height = fixed_height;
    update_row_cells_after_height_change(lycon, trow, fixed_height, false, true);
}

static float table_inter_spacing(ViewTable* table, bool horizontal) {
    if (!table || !table->tb || table->tb->border_collapse) return 0.0f;
    float spacing = horizontal ? table->tb->border_spacing_h : table->tb->border_spacing_v;
    return spacing > 0.0f ? spacing : 0.0f;
}

static float table_column_span_width(ViewTable* table, float* col_widths,
                                     int start_col, int span, int columns) {
    if (!col_widths || columns <= 0 || span <= 0) return 0.0f;
    if (start_col < 0) {
        span += start_col;
        start_col = 0;
    }
    if (start_col >= columns || span <= 0) return 0.0f;
    int end_col = start_col + span;
    if (end_col > columns) end_col = columns;
    int actual_span = end_col - start_col;
    if (actual_span <= 0) return 0.0f;
    float width = table_sum_span_columns(col_widths, start_col, actual_span, columns);
    float spacing = table_inter_spacing(table, true);
    if (spacing > 0.0f && actual_span > 1) {
        width += spacing * (actual_span - 1);
    }
    return width;
}

static float table_column_visual_x(ViewTable* table, float* col_widths, float* col_x_positions,
                                   int start_col, int span, int columns) {
    if (!col_x_positions || columns <= 0) return 0.0f;
    if (start_col < 0) start_col = 0;
    if (start_col >= columns) start_col = columns - 1;
    if (span <= 0) span = 1;
    if (!(table && table->blk && table->block_mut()->direction == CSS_VALUE_RTL)) {
        return col_x_positions[start_col];
    }
    int end_col = start_col + span;
    if (end_col > columns) end_col = columns;
    float grid_left = col_x_positions[0];
    float total_width = table_column_span_width(table, col_widths, 0, columns, columns);
    float leading_source_width = table_column_span_width(table, col_widths, 0, end_col, columns);
    return grid_left + total_width - leading_source_width;
}

static void layout_column_elements(ViewTable* table, float* col_widths, float* col_x_positions,
                                   TableMetadata* meta, int columns, float table_height,
                                   float content_y_offset) {
    if (!table || columns <= 0) return;
    int current_col = 0;
    auto span_has_cell = [&](int start, int span) {
        for (int col = start; col < start + span && col < columns; col++) {
            for (int row = 0; meta && row < meta->row_count; row++) {
                if (meta->grid(row, col)) return true;
            }
        }
        return false;
    };
    auto span_has_authored_width = [&](ViewElement* column, int start, int span) {
        if (!column || !column->specified_style) return false;
        CssDeclaration* width = style_tree_get_declaration(
            column->specified_style, CSS_PROPERTY_WIDTH);
        if (!width || !width->value) return false;
        return width->value->type == CSS_VALUE_TYPE_PERCENTAGE ||
            width->value->type == CSS_VALUE_TYPE_LENGTH ||
            (width->value->type == CSS_VALUE_TYPE_NUMBER &&
             width->value->data.number.value != 0.0);
    };
    table->each_column_source( [&](ViewElement* child) {
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            int first_col = current_col;
            int last_col = current_col;
            int col_count = 0;
            radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                col_count += table_positive_span_attr(col);
            });
            if (col_count == 0) {
                col_count = table_positive_span_attr(child);
            }
            last_col = first_col + col_count - 1;
            if (last_col >= columns) last_col = columns - 1;
            if (first_col < columns) {
                int effective_columns = table_effective_column_count(
                    table, meta, col_widths, columns);
                int effective_span = effective_columns > first_col
                    ? effective_columns - first_col : 0;
                if (effective_span > col_count) effective_span = col_count;
                float x = table_column_visual_x(table, col_widths, col_x_positions,
                                                first_col, col_count, columns);
                float width = table_column_span_width(table, col_widths,
                                                      first_col, effective_span, columns);
                child->x = x;
                child->y = content_y_offset;
                child->width = width;
                child->height = (width > 0) ? table_height : 0;
            }
            float colgroup_x = child->x;  // Colgroup's x relative to table
            int col_idx = first_col;
            radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                if (col_idx < columns) {
                    int col_span = table_positive_span_attr(col);
                    int col_end = col_idx + col_span - 1;
                    if (col_end >= columns) col_end = columns - 1;
                    float col_x_in_table = table_column_visual_x(table, col_widths, col_x_positions,
                                                                 col_idx, col_span, columns);
                    float col_x = col_x_in_table - colgroup_x;
                    float col_width = table_column_span_width(table, col_widths,
                                                              col_idx, col_span, columns);
                    if (col_width <= 0.0f && !span_has_cell(col_idx, col_span) &&
                        !span_has_authored_width(col, col_idx, col_span)) {
                        col_x = 0.0f;
                    }
                    col->x = col_x;
                    col->y = 0;
                    col->width = col_width;
                    col->height = (col_width > 0) ? table_height : 0;
                    col_idx = col_end + 1;
                }
            });
            current_col = last_col + 1;
        }
        else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
            if (current_col < columns) {
                int span = table_positive_span_attr(child);
                int col_end = current_col + span - 1;
                if (col_end >= columns) col_end = columns - 1;
                float col_x = table_column_visual_x(table, col_widths, col_x_positions,
                                                    current_col, span, columns);
                float col_width = table_column_span_width(table, col_widths,
                                                          current_col, span, columns);
                if (col_width <= 0.0f && !span_has_cell(current_col, span) &&
                    !span_has_authored_width(child, current_col, span)) {
                    col_x = 0.0f;
                }
                child->x = col_x;
                child->y = content_y_offset;
                child->width = col_width;
                child->height = (col_width > 0) ? table_height : 0;
                current_col = col_end + 1;
            }
        }
    });
}

static uint8_t get_border_style_priority(CssEnum style) {
    switch (style) {
        case CSS_VALUE_HIDDEN:  return 255; // Always wins
        case CSS_VALUE_NONE:    return 0;   // Always loses
        case CSS_VALUE_DOUBLE:  return 8;
        case CSS_VALUE_SOLID:   return 7;
        case CSS_VALUE_DASHED:  return 6;
        case CSS_VALUE_DOTTED:  return 5;
        case CSS_VALUE_RIDGE:   return 4;
        case CSS_VALUE_OUTSET:  return 3;
        case CSS_VALUE_GROOVE:  return 2;
        case CSS_VALUE_INSET:   return 1;
        default:                return 0;
    }
}

static CollapsedBorder select_winning_border(const CollapsedBorder& a, const CollapsedBorder& b) {
    if (a.style == CSS_VALUE_HIDDEN) {
        CollapsedBorder result = a;
        result.width = 0;
        return result;
    }
    if (b.style == CSS_VALUE_HIDDEN) {
        CollapsedBorder result = b;
        result.width = 0;
        return result;
    }
    if (a.style == CSS_VALUE_NONE && b.style == CSS_VALUE_NONE) return a;
    if (a.style == CSS_VALUE_NONE) return b;
    if (b.style == CSS_VALUE_NONE) return a;
    if (a.width > b.width) return a;
    if (b.width > a.width) return b;
    uint8_t a_pri = get_border_style_priority(a.style);
    uint8_t b_pri = get_border_style_priority(b.style);
    if (a_pri > b_pri) return a;
    if (b_pri > a_pri) return b;
    return a;
}

static CollapsedBorder get_boundary_border(const BoundaryProp* bound, int side) {
    CollapsedBorder border;
    if (!bound || !bound->border) return border;
    const BorderProp* bp = bound->border;
    if (side < 0 || side >= 4) return border;
    float widths[4] = {bp->width.top, bp->width.right, bp->width.bottom, bp->width.left};
    CssEnum styles[4] = {bp->top_style, bp->right_style, bp->bottom_style, bp->left_style};
    Color colors[4] = {bp->top_color, bp->right_color, bp->bottom_color, bp->left_color};
    border.width = widths[side];
    border.style = styles[side];
    border.color = colors[side];
    border.priority = get_border_style_priority(border.style);
    return border;
}

template <typename ViewType>
static CollapsedBorder table_view_border(ViewType* view, int side) {
    return get_boundary_border(view ? view->bound : NULL, side);
}

static void apply_collapsed_border_to_cell(LayoutContext* lycon, ViewTableCell* cell,
                                           const CollapsedBorder& border, int side) {
    if (!cell || !cell->td) return;
    if (side < 0 || side >= 4) return;
    CollapsedBorder** targets[4] = {&cell->td->top_resolved, &cell->td->right_resolved,
                                    &cell->td->bottom_resolved, &cell->td->left_resolved};
    CollapsedBorder** target = targets[side];
    if (!*target) {
        *target = (CollapsedBorder*)alloc_prop(lycon, sizeof(CollapsedBorder));
        if (!*target) {
            log_error("Failed to allocate CollapsedBorder");
            return;
        }
    }
    (*target)->width = border.width;
    (*target)->style = border.style;
    (*target)->color = border.color;
    (*target)->priority = border.priority;
}

static ViewBlock* find_rowgroup_for_row(ViewTable* table, int target_row,
                                        int* out_first_row_in_group, int* out_last_row_in_group) {
    int row_idx = 0;
    for (View* child = static_cast<View*>(table->first_child); child; child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            int first_in_group = row_idx;
            ViewTableRowGroup* rg = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            for (ViewTableRow* row = rg->first_row(); row; row = rg->next_row(row)) {
                row_idx++;
            }
            int last_in_group = row_idx - 1;
            if (target_row >= first_in_group && target_row <= last_in_group) {
                if (out_first_row_in_group) *out_first_row_in_group = first_in_group;
                if (out_last_row_in_group) *out_last_row_in_group = last_in_group;
                return lam::view_require_block(child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            if (row_idx == target_row) {
                if (out_first_row_in_group) *out_first_row_in_group = -1;
                if (out_last_row_in_group) *out_last_row_in_group = -1;
                return nullptr;
            }
            row_idx++;
        }
    }
    return nullptr;
}

typedef lam::ArrayOwnedList<CollapsedBorder, lam::LayoutSessionDomain> CollapsedBorderList;

static void append_collapsed_border_candidate(CollapsedBorderList& candidates,
                                              const CollapsedBorder& value) {
    lam::SessionPtr<CollapsedBorder> border = lam::session_make<CollapsedBorder>(MEM_CAT_LAYOUT);
    if (!border) {
        log_error("append_collapsed_border_candidate_alloc_failed");
        return;
    }
    *border = value;
    if (!candidates.append(static_cast<lam::SessionPtr<CollapsedBorder>&&>(border))) {
        log_error("append_collapsed_border_candidate_append_failed: count=%zu", candidates.size());
    }
}

template <typename ViewType>
static void table_append_border(CollapsedBorderList& candidates, ViewType* view,
                                int side, bool visible_only = false) {
    if (!view) return;
    CollapsedBorder border = table_view_border(view, side);
    if (!visible_only || border.style != CSS_VALUE_NONE) {
        append_collapsed_border_candidate(candidates, border);
    }
}

static ViewBlock* find_table_column_source(ViewTable* table, int target_col, bool want_group) {
    int current_col = 0;
    ViewBlock* found = nullptr;
    table->each_column_source( [&](ViewElement* child) {
        if (found) return;
        if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
            int first_col = current_col;
            int group_count = 0;
            radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                if (!want_group && !found && current_col == target_col) {
                    found = lam::view_require_block(col);
                }
                current_col++;
                group_count++;
            });
            if (want_group) {
                if (group_count == 0) current_col += table_positive_span_attr(child);
                if (target_col >= first_col && target_col < current_col) {
                    found = lam::view_require_block(child);
                }
            } else if (!found && group_count == 0 && current_col <= target_col) {
                current_col += table_positive_span_attr(child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
            if (!want_group && current_col == target_col) found = lam::view_require_block(child);
            current_col++;
        }
    });
    return found;
}

static ViewBlock* find_column_element(ViewTable* table, int target_col) {
    return find_table_column_source(table, target_col, false);
}

static int table_effective_column_count(ViewTable* table, TableMetadata* meta,
                                        const float* col_widths, int columns) {
    int effective = 0;
    for (int col = 0; col < columns; col++) {
        bool active = col_widths && col_widths[col] > 0.0001f;
        for (int row = 0; meta && row < meta->row_count && !active; row++) {
            active = meta->grid(row, col);
        }
        ViewBlock* column = find_column_element(table, col);
        if (column && column->specified_style) {
            CssDeclaration* width = style_tree_get_declaration(
                column->specified_style, CSS_PROPERTY_WIDTH);
            active = active && true;
            if (width && width->value &&
                (width->value->type == CSS_VALUE_TYPE_PERCENTAGE ||
                 width->value->type == CSS_VALUE_TYPE_LENGTH ||
                 (width->value->type == CSS_VALUE_TYPE_NUMBER &&
                  width->value->data.number.value != 0.0))) {
                active = true;
            }
        }
        if (active) effective = col + 1;
    }
    return effective;
}

static ViewBlock* find_colgroup_element(ViewTable* table, int target_col) {
    return find_table_column_source(table, target_col, true);
}

static ViewTableRow* table_row_at_index(ViewTable* table, int target_row) {
    if (!table || target_row < 0) return nullptr;
    int row_index = 0;
    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        if (row_index == target_row) return row;
        row_index++;
    }
    return nullptr;
}

static ViewTableCell* find_cell_at(ViewTable* table, int target_row, int target_col) {
    return table->find_cell( [&](ViewTableRow* row, ViewTableCell* cell) {
        (void)row;
        int row_start = cell->td->row_index;
        int row_end = row_start + cell->td->row_span;
        int col_start = cell->td->col_index;
        int col_end = col_start + cell->td->col_span;
        return target_row >= row_start && target_row < row_end &&
            target_col >= col_start && target_col < col_end;
    });
}

static void apply_collapsed_border_pair(LayoutContext* lycon, ViewTable* table,
                                        TableMetadata* meta, CollapsedBorderList& candidates,
                                        int row, int col, bool horizontal) {
    if (candidates.size() == 0) return;
    CollapsedBorder winner = *candidates[0];
    for (size_t i = 1; i < candidates.size(); i++) {
        winner = select_winning_border(winner, *candidates[i]);
    }
    if (horizontal ? row > 0 : col > 0) {
        ViewTableCell* previous = find_cell_at(
            table, horizontal ? row - 1 : row, horizontal ? col : col - 1);
        if (previous) {
            apply_collapsed_border_to_cell(lycon, previous, winner, horizontal ? 2 : 1);
        }
    }
    if (horizontal ? row < meta->row_count : col < meta->column_count) {
        ViewTableCell* next = find_cell_at(table, row, col);
        if (next) {
            apply_collapsed_border_to_cell(lycon, next, winner, horizontal ? 0 : 3);
        }
    }
}

static CollapsedBorder* table_cell_resolved_border(ViewTableCell* cell, int side) {
    if (!cell || !cell->td) return nullptr;
    switch (side) {
        case 0: return cell->td->top_resolved;
        case 1: return cell->td->right_resolved;
        case 2: return cell->td->bottom_resolved;
        case 3: return cell->td->left_resolved;
        default: return nullptr;
    }
}

static void table_update_collapsed_edge(ViewTable* table, TableMetadata* meta,
                                        bool horizontal, bool start) {
    int count = horizontal ? meta->column_count : meta->row_count;
    int fixed = start ? 0 : (horizontal ? meta->row_count : meta->column_count) - 1;
    int side = horizontal ? (start ? 0 : 2) : (start ? 3 : 1);
    float* max_width = horizontal
        ? (start ? &meta->collapsed_border_top : &meta->collapsed_border_bottom)
        : (start ? &meta->collapsed_border_left : &meta->collapsed_border_right);
    for (int index = 0; index < count; index++) {
        int row = horizontal ? fixed : index;
        int col = horizontal ? index : fixed;
        CollapsedBorder* border = table_cell_resolved_border(find_cell_at(table, row, col), side);
        if (border && border->width > *max_width) *max_width = border->width;
    }
}

static bool is_out_of_flow_table_cell_slot(View* view) {
    ViewElement* elem = lam::view_as_element(view);
    if (!elem || elem->view_type == RDT_VIEW_TABLE_CELL) return false;
    NameId tag = elem->tag();
    if (tag != MARKUP_NAME_TD && tag != MARKUP_NAME_TH) return false;
    return layout_view_is_out_of_flow_positioned(view);
}

template <typename Fn>
static void for_each_table_row_cell_slot(ViewTableRow* row, Fn fn) {
    if (!row) return;
    for (View* child = static_cast<View*>(row->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_CELL || is_out_of_flow_table_cell_slot(child)) {
            fn(child);
        }
    }
}

static void collect_collapsed_border_candidates(ViewTable* table, TableMetadata* meta,
                                                int row, int col, bool horizontal,
                                                CollapsedBorderList& candidates) {
    int edge = horizontal ? row : col;
    int edge_count = horizontal ? meta->row_count : meta->column_count;
    bool at_start = edge == 0;
    bool at_end = edge == edge_count;
    int previous_row = horizontal ? row - 1 : row;
    int previous_col = horizontal ? col : col - 1;
    int next_row = row;
    int next_col = col;
    int previous_side = horizontal ? 2 : 1;
    int next_side = horizontal ? 0 : 3;
    if (at_start) {
        append_collapsed_border_candidate(candidates,
            table_view_border(table, horizontal ? 0 : 3));
    } else {
        table_append_border(candidates, find_cell_at(table, previous_row, previous_col),
                            previous_side);
    }
    if (at_end) {
        append_collapsed_border_candidate(candidates,
            table_view_border(table, horizontal ? 2 : 1));
    } else {
        table_append_border(candidates, find_cell_at(table, next_row, next_col), next_side);
    }
    if (horizontal) {
        if (!at_start) {
            table_append_border(candidates, table_row_at_index(table, row - 1), 2, true);
        }
        if (!at_end) {
            table_append_border(candidates, table_row_at_index(table, row), 0, true);
        }
        if (!at_end) {
            int first = -1, last = -1;
            ViewBlock* group = find_rowgroup_for_row(table, row, &first, &last);
            if (group && row == first) table_append_border(candidates, group, 0, true);
        }
        if (!at_start) {
            int first = -1, last = -1;
            ViewBlock* group = find_rowgroup_for_row(table, row - 1, &first, &last);
            if (group && row - 1 == last) table_append_border(candidates, group, 2, true);
        }
        if (at_start || at_end) {
            int side = at_start ? 0 : 2;
            table_append_border(candidates, find_column_element(table, col), side, true);
            table_append_border(candidates, find_colgroup_element(table, col), side, true);
        }
        return;
    }
    if (at_start || at_end) {
        int side = at_start ? 3 : 1;
        table_append_border(candidates, table_row_at_index(table, row), side, true);
        int first = -1, last = -1;
        ViewBlock* group = find_rowgroup_for_row(table, row, &first, &last);
        table_append_border(candidates, group, side, true);
    }
    if (!at_start) {
        table_append_border(candidates, find_column_element(table, col - 1), 1, true);
    }
    if (!at_end) {
        table_append_border(candidates, find_column_element(table, col), 3, true);
    }
    ViewBlock* left_group = !at_start ? find_colgroup_element(table, col - 1) : nullptr;
    ViewBlock* right_group = !at_end ? find_colgroup_element(table, col) : nullptr;
    if (left_group && left_group != right_group) {
        table_append_border(candidates, left_group, 1, true);
    }
    if (right_group && right_group != left_group) {
        table_append_border(candidates, right_group, 3, true);
    }
    if (at_start) table_append_border(candidates, right_group, 3, true);
    if (at_end) table_append_border(candidates, left_group, 1, true);
}

static void resolve_collapsed_borders(LayoutContext* lycon, ViewTable* table, TableMetadata* meta) {
    if (!table || !meta || !table->tb->border_collapse) return;
    auto resolve_axis = [&](bool horizontal) {
        int edge_count = horizontal ? meta->row_count : meta->column_count;
        int slot_count = horizontal ? meta->column_count : meta->row_count;
        for (int edge = 0; edge <= edge_count; edge++) {
            for (int slot = 0; slot < slot_count; slot++) {
                int row = horizontal ? edge : slot;
                int col = horizontal ? slot : edge;
                CollapsedBorderList candidates(MEM_CAT_LAYOUT, 4);
                collect_collapsed_border_candidates(
                    table, meta, row, col, horizontal, candidates);
                apply_collapsed_border_pair(
                    lycon, table, meta, candidates, row, col, horizontal);
            }
        }
    };
    resolve_axis(true);
    resolve_axis(false);
    table_update_collapsed_edge(table, meta, true, true);
    table_update_collapsed_edge(table, meta, true, false);
    table_update_collapsed_edge(table, meta, false, true);
    table_update_collapsed_edge(table, meta, false, false);

}

static float table_sum_rows(const TableMetadata* meta, int start_row, int end_row,
                            float row_spacing) {
    if (!meta || !meta->row_heights) return 0.0f;
    float total = 0.0f;
    for (int row = start_row; row < end_row; row++) {
        total += meta->row_heights[row];
        if (row_spacing > 0.0f && row < end_row - 1) total += row_spacing;
    }
    return total;
}

static void distribute_rowspan_heights(ViewTable* table, TableMetadata* meta) {
    int rows = meta->row_count;
    float row_spacing = table_inter_spacing(table, false);
    struct RowspanCell {
        ViewTableCell* cell;
        int start_row;
        int end_row;
        float required_height;
    };
    ArrayList* rowspan_cells = arraylist_new(8);
    table->each_row( [&](ViewTableRow* row) {
        row->each_cell( [&](ViewTableCell* tcell) {
            if (tcell->td->row_span > 1) {
                int start_row = tcell->td->row_index;
                int end_row = start_row + tcell->td->row_span;
                if (start_row < 0) start_row = 0;     // never index before row_heights[0]
                if (end_row > rows) end_row = rows;    // never index past row_heights[rows-1]
                float current_total = table_sum_rows(meta, start_row, end_row, row_spacing);
                float required_height = tcell->height;
                if (meta->row_collapsed) {
                    bool spans_collapsed_row = false;
                    for (int row_idx = start_row; row_idx < end_row; row_idx++) {
                        if (meta->row_collapsed[row_idx]) {
                            spans_collapsed_row = true;
                            break;
                        }
                    }
                    if (spans_collapsed_row) {
                        // CSS Tables keeps the authored rowspan allocation before
                        // removing collapsed rows; distribute the natural cell
                        // height across the original span rather than moving it all
                        // into the surviving row.
                        required_height /= (float)tcell->td->row_span;
                    }
                }
                if (required_height > current_total) {
                    RowspanCell* rsc = (RowspanCell*)mem_alloc(sizeof(RowspanCell), MEM_CAT_LAYOUT);
                    rsc->cell = tcell;
                    rsc->start_row = start_row;
                    rsc->end_row = end_row;
                    rsc->required_height = required_height;
                    arraylist_append(rowspan_cells, rsc);
                }
            }
        });
    });
    for (int i = 0; i < rowspan_cells->length; i++) {
        RowspanCell* rsc = (RowspanCell*)rowspan_cells->data[i];
        // expanded overlapping rows. otherwise sibling rowspans over the same
        float current_total = table_sum_rows(
            meta, rsc->start_row, rsc->end_row, row_spacing);
        float excess = rsc->required_height - current_total;
        if (excess <= 0.0f) {
            continue;
        }
        float total_content = table_sum_rows(meta, rsc->start_row, rsc->end_row, 0.0f);
        if (total_content > 0) {
            for (int r = rsc->start_row; r < rsc->end_row; r++) {
                float proportion = meta->row_heights[r] / total_content;
                float amount = excess * proportion;
                meta->row_heights[r] += amount;
            }
        } else {
            // height into the later grid row. this preserves empty placeholder
            // rows used only to terminate rowspans.
            int target_row = rsc->end_row - 1;
            if (meta->row_collapsed) {
                while (target_row > rsc->start_row && meta->row_collapsed[target_row]) {
                    target_row--;
                }
            }
            meta->row_heights[target_row] += excess;
        }
    }
    for (int i = 0; i < rowspan_cells->length; i++) {
        mem_free(rowspan_cells->data[i]);
    }
    arraylist_free(rowspan_cells);
}

static bool table_resolve_border_collapse_value(CssValue* value, bool* border_collapse, bool* keep_inheriting) {
    if (keep_inheriting) *keep_inheriting = false;
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum kw = value->data.keyword;
    if (kw == CSS_VALUE_INHERIT || kw == CSS_VALUE_UNSET) {
        if (keep_inheriting) *keep_inheriting = true;
        return false;
    }
    if (kw == CSS_VALUE_COLLAPSE || kw == CSS_VALUE_COLLAPSE_TABLE) {
        *border_collapse = true;
        return true;
    }
    if (kw == CSS_VALUE_SEPARATE || kw == CSS_VALUE_INITIAL) {
        *border_collapse = false;
        return true;
    }
    return false;
}

static bool table_resolve_caption_side_value(CssValue* value, bool* is_bottom) {
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD || !is_bottom) return false;
    *is_bottom = value->data.keyword == CSS_VALUE_BOTTOM;
    return true;
}

template <typename Resolve, typename Assign>
static bool table_inherit_property(LayoutContext* lycon, DomNode* element,
                                   CssPropertyCode property, Resolve resolve, Assign assign) {
    DomNode* ancestor = element ? element->parent : nullptr;
    while (ancestor) {
        if (ancestor->is_element()) {
            DomElement* anc_elem = ancestor->as_element();
            if (anc_elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(anc_elem->specified_style, property);
                if (decl && decl->value) {
                    bool keep_inheriting = false;
                    if (resolve((CssValue*)decl->value, &keep_inheriting)) {
                        return true;
                    }
                    if (!keep_inheriting) return false;
                }
            }
            if (anc_elem->table_prop()) {
                assign(anc_elem);
                return true;
            }
        }
        ancestor = ancestor->parent;
    }
    return false;
}

static bool table_inherit_border_collapse(LayoutContext* lycon, DomNode* element, bool* border_collapse) {
    return table_inherit_property(lycon, element, CSS_PROPERTY_BORDER_COLLAPSE,
        [&](CssValue* value, bool* keep) {
            return table_resolve_border_collapse_value(value, border_collapse, keep);
        },
        [&](DomElement* ancestor) { *border_collapse = ancestor->tb->border_collapse; });
}

static bool table_inherit_border_spacing(LayoutContext* lycon, DomNode* element,
        float* spacing_h, float* spacing_v) {
    return table_inherit_property(lycon, element, CSS_PROPERTY_BORDER_SPACING,
        [&](CssValue* value, bool* keep) {
            LayoutBorderSpacingValue resolved = layout_resolve_border_spacing_value(lycon, value);
            *keep = resolved.keep_inheriting;
            if (!resolved.resolved) return false;
            *spacing_h = resolved.horizontal;
            *spacing_v = resolved.vertical;
            return true;
        },
        [&](DomElement* ancestor) {
            *spacing_h = ancestor->tb->border_spacing_h;
            *spacing_v = ancestor->tb->border_spacing_v;
        });
}

static void resolve_table_properties(LayoutContext* lycon, DomNode* element, ViewTable* table) {
    if (element->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = element->as_element();
        if (dom_elem->tag() == MARKUP_NAME_TABLE) {
            table->tb->border_spacing_h = 2.0f;
            table->tb->border_spacing_v = 2.0f;
            const char* cellspacing_attr = dom_elem->get_attribute("cellspacing");
            if (cellspacing_attr) {
                float spacing = (float)str_to_double_default(cellspacing_attr, strlen(cellspacing_attr), 0.0);
                table->tb->border_spacing_h = spacing;
                table->tb->border_spacing_v = spacing;
            }
            const char* rules_attr = dom_elem->get_attribute("rules");
            if (rules_attr) {
                size_t rules_len = strlen(rules_attr);
                if (!str_ieq_const(rules_attr, rules_len, "none")) {
                    table->tb->border_collapse = true;
                }
            }
        }
    }
    if (element->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = element->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* collapse_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_BORDER_COLLAPSE);
            if (collapse_decl && collapse_decl->value) {
                CssValue* val = (CssValue*)collapse_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    bool keep_inheriting = false;
                    if (!table_resolve_border_collapse_value(
                            val, &table->tb->border_collapse, &keep_inheriting) && keep_inheriting) {
                        if (!table_inherit_border_collapse(
                                lycon, element, &table->tb->border_collapse)) {
                            table->tb->border_collapse = false;
                        }
                    }
                }
            }
            CssDeclaration* spacing_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_BORDER_SPACING);
            if (spacing_decl && spacing_decl->value) {
                CssValue* val = (CssValue*)spacing_decl->value;
                LayoutBorderSpacingValue resolved =
                    layout_resolve_border_spacing_value(lycon, val);
                if (resolved.resolved) {
                    table->tb->border_spacing_h = resolved.horizontal;
                    table->tb->border_spacing_v = resolved.vertical;
                } else if (resolved.keep_inheriting) {
                    if (!table_inherit_border_spacing(lycon, element, &table->tb->border_spacing_h,
                            &table->tb->border_spacing_v)) {
                        table->tb->border_spacing_h = 0.0f;
                        table->tb->border_spacing_v = 0.0f;
                    }
                }
            } else {
                bool is_html_table = (dom_elem->tag() == MARKUP_NAME_TABLE);
                if (!is_html_table) {
                    table_inherit_border_spacing(lycon, element,
                        &table->tb->border_spacing_h, &table->tb->border_spacing_v);
                }
            }
            CssDeclaration* caption_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_CAPTION_SIDE);
            if (caption_decl && caption_decl->value) {
                bool caption_bottom = false;
                if (table_resolve_caption_side_value(caption_decl->value, &caption_bottom)) {
                    table->tb->caption_side = caption_bottom
                        ? TableProp::CAPTION_SIDE_BOTTOM : TableProp::CAPTION_SIDE_TOP;
                }
            }
            CssDeclaration* empty_cells_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_EMPTY_CELLS);
            if (empty_cells_decl && empty_cells_decl->value) {
                CssValue* val = (CssValue*)empty_cells_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_HIDE) {
                        table->tb->empty_cells = TableProp::EMPTY_CELLS_HIDE;
                    } else {
                        table->tb->empty_cells = TableProp::EMPTY_CELLS_SHOW;
                    }
                }
            }
            CssDeclaration* layout_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_TABLE_LAYOUT);
            if (layout_decl && layout_decl->value) {
                CssValue* val = (CssValue*)layout_decl->value;
                if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                    if (val->data.keyword == CSS_VALUE_FIXED) {
                        table->tb->table_layout = TableProp::TABLE_LAYOUT_FIXED;
                    } else if (val->data.keyword == CSS_VALUE_AUTO) {
                        table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
                    }
                }
            }
        } else {
            bool inherited_collapse = false;
            if (table_inherit_border_collapse(lycon, element, &inherited_collapse)) {
                table->tb->border_collapse = inherited_collapse;
            }
            bool is_html_table = (dom_elem->tag() == MARKUP_NAME_TABLE);
            if (!is_html_table) {
                table_inherit_border_spacing(lycon, element,
                    &table->tb->border_spacing_h, &table->tb->border_spacing_v);
            }
        }
    }
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        return;
    }
    table->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
}

static bool table_cell_apply_vertical_align_keyword(ViewTableCell* cell,
                                                    CssEnum keyword) {
    if (!cell || !cell->td) return false;
    switch (keyword) {
    case CSS_VALUE_TOP:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_TOP;
        break;
    case CSS_VALUE_MIDDLE:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_MIDDLE;
        break;
    case CSS_VALUE_BOTTOM:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_BOTTOM;
        break;
    case CSS_VALUE_BASELINE:
    case CSS_VALUE_SUB:
    case CSS_VALUE_SUPER:
    case CSS_VALUE_TEXT_TOP:
    case CSS_VALUE_TEXT_BOTTOM:
        cell->td->vertical_align = TableCellProp::CELL_VALIGN_BASELINE;
        break;
    default:
        return false;
    }
    return true;
}

static void parse_cell_attributes(LayoutContext* lycon, DomNode* cellNode, ViewTableCell* cell) {
    assert(cell->td);
    cell->td->col_span = 1;
    cell->td->row_span = 1;
    cell->td->col_index = -1;
    cell->td->row_index = -1;
    cell->td->is_empty = is_cell_empty(cell) ? 1 : 0;
    NameId tag = cellNode->tag();
    cell->td->vertical_align = (tag == MARKUP_NAME_TD || tag == MARKUP_NAME_TH)
        ? TableCellProp::CELL_VALIGN_MIDDLE
        : TableCellProp::CELL_VALIGN_BASELINE;
    if (!cellNode->is_element()) return;
    if (cellNode->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = cellNode->as_element();
        const char* colspan_str = dom_elem->get_attribute("colspan");
        if (colspan_str && colspan_str[0] != '\0') {
            int span = (int)str_to_int64_default(colspan_str, strlen(colspan_str), 0); // INT_CAST_OK: string length
            if (span > 0 && span <= 1000) {
                cell->td->col_span = span;
            }
        }
        const char* rowspan_str = dom_elem->get_attribute("rowspan");
        if (rowspan_str && rowspan_str[0] != '\0') {
            int span = (int)str_to_int64_default(rowspan_str, strlen(rowspan_str), 0); // INT_CAST_OK: string length
            if (span == 0) {
                cell->td->row_span = 0;
            } else if (span > 0 && span <= 65534) {
                cell->td->row_span = span;
            }
        }
        if (cell->in_line && cell->inl()->vertical_align) {
            table_cell_apply_vertical_align_keyword(cell, cell->inl()->vertical_align);
        }
        if (dom_elem->specified_style) {
            CssDeclaration* valign_decl = style_tree_get_declaration(
                dom_elem->specified_style,
                CSS_PROPERTY_VERTICAL_ALIGN);
            if (valign_decl && valign_decl->value && valign_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                table_cell_apply_vertical_align_keyword(
                    cell, valign_decl->value->data.keyword);
            } else if (!valign_decl) {
                for (DomNode* ancestor = cellNode->parent; ancestor;
                     ancestor = ancestor->parent) {
                    if (!ancestor->is_element()) continue;
                    DomElement* ancestor_element = ancestor->as_element();
                    CssDeclaration* inherited_decl = ancestor_element->specified_style
                        ? style_tree_get_declaration(
                            ancestor_element->specified_style,
                            CSS_PROPERTY_VERTICAL_ALIGN)
                        : nullptr;
                    if (!inherited_decl || !inherited_decl->value ||
                        inherited_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
                        continue;
                    }
                    if (table_cell_apply_vertical_align_keyword(
                            cell, inherited_decl->value->data.keyword)) {
                        break;
                    }
                }
            }
        }
    }
}

static bool table_view_is_caption(ViewBlock* child) {
    if (!child) return false;
    DisplayValue child_display = resolve_display_value((void*)child);
    return child->tag() == MARKUP_NAME_CAPTION ||
        child_display.inner == CSS_VALUE_TABLE_CAPTION;
}

static void inherit_anonymous_table_block_props(LayoutContext* lycon, DomElement* anon, DomElement* parent) {
    if (!lycon || !anon || !parent) return;
    anon->ensure_block(lycon);
    if (!anon->blk) return;
    if (parent->blk) {
        anon->blk->text_align = parent->blk->text_align;
        anon->blk->text_align_last = parent->blk->text_align_last;
        anon->blk->direction = parent->blk->direction;
        anon->blk->text_transform = parent->blk->text_transform;
        anon->blk->line_height = parent->blk->line_height;
        anon->blk->text_indent = parent->blk->text_indent;
        anon->blk->text_indent_percent = parent->blk->text_indent_percent;
        anon->blk->text_indent_calc = parent->blk->text_indent_calc;
        anon->blk->white_space = parent->blk->white_space;
        anon->blk->word_break = parent->blk->word_break;
        anon->blk->overflow_wrap = parent->blk->overflow_wrap;
        anon->blk->line_break = parent->blk->line_break;
        anon->blk->text_spacing_trim = parent->blk->text_spacing_trim;
        anon->blk->tab_size = parent->blk->tab_size;
        anon->blk->text_box_trim = parent->blk->text_box_trim;
        anon->blk->text_box_over_edge = parent->blk->text_box_over_edge;
        anon->blk->text_box_under_edge = parent->blk->text_box_under_edge;
        anon->blk->text_overflow = parent->blk->text_overflow;
        anon->blk->line_clamp = parent->blk->line_clamp;
    }
    CssEnum specified_white_space = layout_specified_keyword(parent, CSS_PROPERTY_WHITE_SPACE);
    if (specified_white_space != 0) {
        anon->blk->white_space = specified_white_space;
    }
    CssEnum specified_direction = layout_specified_keyword(parent, CSS_PROPERTY_DIRECTION);
    if (specified_direction != 0) {
        anon->blk->direction = specified_direction;
    }
    CssEnum specified_text_transform = layout_specified_keyword(parent, CSS_PROPERTY_TEXT_TRANSFORM);
    if (specified_text_transform != 0) {
        anon->blk->text_transform = specified_text_transform;
    }
    CssEnum specified_word_break = layout_specified_keyword(parent, CSS_PROPERTY_WORD_BREAK);
    if (specified_word_break != 0) {
        anon->blk->word_break = specified_word_break;
    }
    CssEnum specified_overflow_wrap = layout_specified_keyword(parent, CSS_PROPERTY_OVERFLOW_WRAP);
    if (specified_overflow_wrap != 0) {
        anon->blk->overflow_wrap = specified_overflow_wrap;
    }
    CssEnum specified_line_break = layout_specified_keyword(parent, CSS_PROPERTY_LINE_BREAK);
    if (specified_line_break != 0) {
        anon->blk->line_break = specified_line_break;
    }
}

static void inherit_anonymous_table_font(LayoutContext* lycon, DomElement* anon,
                                         const FontProp* parent) {
    if (!lycon || !anon || !parent) return;
    FontProp* font = anon->ensure_font(lycon);
    if (!font) return;
    // handles remain local to the box and cannot alias the parent's cache.
    radiant_copy_font_values(font, parent);
}

static void inherit_anonymous_table_inline(LayoutContext* lycon, DomElement* anon,
                                           const InlineProp* parent) {
    if (!lycon || !anon || !parent) return;
    InlineProp* in_line = anon->ensure_inline(lycon);
    if (!in_line) return;
    in_line->color = parent->color;
    in_line->has_color = parent->has_color;
    in_line->visibility = parent->visibility;
    in_line->opacity = 1.0f;
}

static DomElement* create_anonymous_table_element(LayoutContext* lycon, DomElement* parent,
                                                   CssEnum display_type, const char* tag_name) {
    if (!lycon || !parent) return nullptr;
    Pool* pool = lycon->doc->view_tree->prop_pool;
    if (!pool) return nullptr;
    DomElement* anon = lam::pool_alloc_dom_element(pool);
    if (!anon) return nullptr;
    dom_element_retain_tag_name(anon, lam::borrow_const(lam::promote_to_pool(pool, tag_name)));
    anon->doc = parent->doc;
    anon->parent = parent;
    switch (display_type) {
        case CSS_VALUE_TABLE_ROW_GROUP:
        case CSS_VALUE_TABLE_HEADER_GROUP:
        case CSS_VALUE_TABLE_FOOTER_GROUP:
            anon->display.outer = CSS_VALUE_TABLE_ROW_GROUP;
            anon->display.inner = CSS_VALUE_TABLE_ROW_GROUP;
            break;
        case CSS_VALUE_TABLE_ROW:
            anon->display.outer = CSS_VALUE_TABLE_ROW;
            anon->display.inner = CSS_VALUE_TABLE_ROW;
            break;
        case CSS_VALUE_TABLE_CELL:
            anon->display.outer = CSS_VALUE_TABLE_CELL;
            anon->display.inner = CSS_VALUE_TABLE_CELL;
            break;
        default:
            anon->display.outer = display_type;
            anon->display.inner = display_type;
            break;
    }
    inherit_anonymous_table_font(lycon, anon, parent->font);
    inherit_anonymous_table_block_props(lycon, anon, parent);
    inherit_anonymous_table_inline(lycon, anon, parent->in_line);
    anon->bound = nullptr;
    anon->set_styles_resolved(true);
    return anon;
}

static void append_detached_table_node(DomElement* parent, DomNode* child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = nullptr;
    child->prev_sibling = parent->last_child;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static void append_child_to_element(DomElement* parent, DomElement* child) {
    append_detached_table_node(parent, static_cast<DomNode*>(child));
}

static void reparent_node(DomNode* node, DomElement* new_parent) {
    if (!node || !new_parent) return;
    DomElement* old_parent = lam::dom_as<DOM_NODE_ELEMENT>(node->parent);
    if (old_parent) {
        if (node->prev_sibling) {
            node->prev_sibling->next_sibling = node->next_sibling;
        } else {
            old_parent->first_child = node->next_sibling;
        }
        if (node->next_sibling) {
            node->next_sibling->prev_sibling = node->prev_sibling;
        } else {
            old_parent->last_child = node->prev_sibling;
        }
    }
    append_detached_table_node(new_parent, node);
}

static void reparent_run(ArrayList* run, DomElement* new_parent) {
    if (!run || !new_parent) return;
    for (int i = 0; i < run->length; i++) {
        reparent_node(static_cast<DomNode*>(run->data[i]), new_parent);
    }
}

static void insert_node_before(DomElement* parent, DomNode* new_node, DomNode* ref_node) {
    if (!parent || !new_node) return;
    if (!ref_node) {
        append_detached_table_node(parent, new_node);
        return;
    }
    new_node->parent = parent;
    new_node->next_sibling = ref_node;
    new_node->prev_sibling = ref_node->prev_sibling;
    if (ref_node->prev_sibling) {
        ref_node->prev_sibling->next_sibling = new_node;
    } else {
        parent->first_child = new_node;
    }
    ref_node->prev_sibling = new_node;
}

static bool table_text_node_has_preserved_whitespace_content(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = lam::dom_require<DOM_NODE_TEXT>(node)->text;
    if (!text || !*text) return false;
    if (layout_dom_text_has_non_whitespace(
            lam::dom_require<DOM_NODE_TEXT>(node))) return false;
    return white_space_preserves_space_advance(get_white_space_value(node));
}

static bool table_anonymous_run_allows_preserved_whitespace(ArrayList* run) {
    if (!run) return false;
    for (int i = run->length - 1; i >= 0; i--) {
        DomNode* node = static_cast<DomNode*>(run->data[i]);
        if (!node) continue;
        if (node->is_text()) {
            return layout_dom_text_has_non_whitespace(
                       lam::dom_require<DOM_NODE_TEXT>(node)) ||
                   table_text_node_has_preserved_whitespace_content(node);
        }
        if (node->is_element()) {
            DisplayValue display = resolve_display_value(node);
            if (layout_display_is_none(display)) {
                continue;
            }
            return false;
        }
    }
    return false;
}

static bool table_text_node_generates_anonymous_content(DomNode* node,
        bool run_allows_preserved_whitespace) {
    if (layout_dom_text_has_non_whitespace(
            lam::dom_require<DOM_NODE_TEXT>(node))) return true;
    if (!table_text_node_has_preserved_whitespace_content(node)) return false;
    return run_allows_preserved_whitespace;
}

static void wrap_run_in_cells(LayoutContext* lycon, ArrayList* run, DomElement* parent_row) {
    DomElement* current_anon_td = nullptr;
    for (int i = 0; i < run->length; i++) {
        DomNode* node = static_cast<DomNode*>(run->data[i]);
        bool is_cell = false;
        if (node->is_element()) {
            DisplayValue disp = resolve_display_value(node);
            is_cell = disp.inner == CSS_VALUE_TABLE_CELL;
        }
        if (is_cell) {
            current_anon_td = nullptr;
            reparent_node(node, parent_row);
        } else {
            if (!current_anon_td) {
                current_anon_td = create_anonymous_table_element(lycon, parent_row,
                    CSS_VALUE_TABLE_CELL, "::anon-td");
                append_child_to_element(parent_row, current_anon_td);
            }
            reparent_node(node, current_anon_td);
        }
    }
}

static void place_anonymous_table_child(DomElement* parent, DomElement* child,
                                        DomNode* before) {
    if (before) {
        insert_node_before(parent, static_cast<DomNode*>(child), before);
    } else {
        append_child_to_element(parent, child);
    }
}

static ArrayList* table_snapshot_children(DomElement* parent) {
    ArrayList* children = arraylist_new(8);
    if (!children || !parent) return children;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        arraylist_append(children, child);
    }
    return children;
}

static void flush_anonymous_cell_run(LayoutContext* lycon, DomElement* parent,
                                     ArrayList* run, DomNode* before,
                                     bool create_row_group) {
    if (!run || run->length == 0) return;
    DomElement* row_parent = parent;
    DomElement* row_group = nullptr;
    if (create_row_group) {
        row_group = create_anonymous_table_element(
            lycon, parent, CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
        row_parent = row_group;
    }
    DomElement* row = create_anonymous_table_element(
        lycon, row_parent, CSS_VALUE_TABLE_ROW, "::anon-tr");
    if (row_group) append_child_to_element(row_group, row);
    wrap_run_in_cells(lycon, run, row);
    place_anonymous_table_child(parent, row_group ? row_group : row, before);
    arraylist_clear(run);
}

static void flush_anonymous_row_run(LayoutContext* lycon, DomElement* table,
                                    ArrayList* run, DomNode* before) {
    if (!run || run->length == 0) return;
    DomElement* row_group = create_anonymous_table_element(
        lycon, table, CSS_VALUE_TABLE_ROW_GROUP, "::anon-tbody");
    reparent_run(run, row_group);
    place_anonymous_table_child(table, row_group, before);
    arraylist_clear(run);
}

static void flush_anonymous_noncell_run(LayoutContext* lycon, DomElement* row,
                                        ArrayList* run, DomNode* before) {
    if (!run || run->length == 0) return;
    DomElement* cell = create_anonymous_table_element(
        lycon, row, CSS_VALUE_TABLE_CELL, "::anon-td");
    if (!cell) return;
    reparent_run(run, cell);
    place_anonymous_table_child(row, cell, before);
    arraylist_clear(run);
}

static void repair_anonymous_table_children(LayoutContext* lycon,
                                            DomElement* parent,
                                            CssEnum proper_child_display,
                                            bool wrap_run_in_row) {
    if (!lycon || !parent) return;
    ArrayList* children = table_snapshot_children(parent);
    ArrayList* run = arraylist_new(8);
    if (!children || !run) {
        if (children) arraylist_free(children);
        if (run) arraylist_free(run);
        return;
    }
    for (int i = 0; i < children->length; i++) {
        DomNode* child = static_cast<DomNode*>(children->data[i]);
        if (!child) continue;
        if (!child->is_element()) {
            if (child->is_text() && table_text_node_generates_anonymous_content(
                    child, table_anonymous_run_allows_preserved_whitespace(run))) {
                arraylist_append(run, child);
            }
            continue;
        }
        DisplayValue display = resolve_display_value(child);
        if (display.inner == proper_child_display) {
            if (run->length > 0) {
                if (wrap_run_in_row) {
                    flush_anonymous_cell_run(lycon, parent, run, child, false);
                } else {
                    flush_anonymous_noncell_run(lycon, parent, run, child);
                }
            }
        } else {
            arraylist_append(run, child);
        }
    }
    if (run->length > 0) {
        if (wrap_run_in_row) {
            flush_anonymous_cell_run(lycon, parent, run, nullptr, false);
        } else {
            flush_anonymous_noncell_run(lycon, parent, run, nullptr);
        }
    }
    arraylist_free(children);
    arraylist_free(run);
}

static void generate_anonymous_table_boxes(LayoutContext* lycon, DomElement* table) {
    if (!lycon || !table) return;
    Pool* pool = lycon->doc->view_tree->prop_pool;
    if (!pool) return;
    ArrayList* children_to_process = table_snapshot_children(table);
    ArrayList* current_cell_run = arraylist_new(8);
    ArrayList* current_row_run = arraylist_new(8);
    for (int i = 0; i < children_to_process->length; ) {
        DomNode* child = static_cast<DomNode*>(children_to_process->data[i]);
        if (child->is_text()) {
            if (table_text_node_generates_anonymous_content(child,
                    table_anonymous_run_allows_preserved_whitespace(current_cell_run))) {
                arraylist_append(current_cell_run, child);
            }
            i++;
            continue;
        }
        if (!child->is_element()) {
            i++;
            continue;
        }
        DisplayValue display = resolve_display_value(child);
        NameId tag = child->tag();
        bool is_row_group = layout_display_is_table_row_group(display.inner);
        bool is_row = display.inner == CSS_VALUE_TABLE_ROW;
        bool is_cell = display.inner == CSS_VALUE_TABLE_CELL;
        bool is_column = (display.inner == CSS_VALUE_TABLE_COLUMN ||
                          display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) ||
                        tag == MARKUP_NAME_COL || tag == MARKUP_NAME_COLGROUP;
        bool is_caption = display.inner == CSS_VALUE_TABLE_CAPTION ||
                          tag == MARKUP_NAME_CAPTION;
        if (is_row_group || is_column || is_caption) {
            if (current_cell_run->length > 0) {
                flush_anonymous_cell_run(
                    lycon, table, current_cell_run, child, true);
            }
            if (current_row_run->length > 0) {
                flush_anonymous_row_run(lycon, table, current_row_run, child);
            }
            i++;
            continue;
        }
        if (is_row) {
            if (current_cell_run->length > 0) {
                flush_anonymous_cell_run(
                    lycon, table, current_cell_run, child, true);
            }
            arraylist_append(current_row_run, child);
            i++;
            continue;
        }
        if (is_cell) {
            if (current_row_run->length > 0) {
                flush_anonymous_row_run(lycon, table, current_row_run, child);
            }
            arraylist_append(current_cell_run, child);
            i++;
            continue;
        }
        arraylist_append(current_cell_run, child);
        i++;
    }
    if (current_cell_run->length > 0) {
        flush_anonymous_cell_run(
            lycon, table, current_cell_run, nullptr, true);
    }
    if (current_row_run->length > 0) {
        flush_anonymous_row_run(lycon, table, current_row_run, nullptr);
    }
    arraylist_free(children_to_process);
    arraylist_free(current_cell_run);
    arraylist_free(current_row_run);
    for (DomNode* child = table->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* row_group = child->as_element();
        DisplayValue display = resolve_display_value(child);
        if (!layout_display_is_table_row_group(display.inner)) {
            continue;
        }
        repair_anonymous_table_children(
            lycon, row_group, CSS_VALUE_TABLE_ROW, true);
    }
    for (DomNode* group_node = table->first_child; group_node; group_node = group_node->next_sibling) {
        if (!group_node->is_element()) continue;
        DomElement* row_group = group_node->as_element();
        DisplayValue group_display = resolve_display_value(group_node);
        if (!layout_display_is_table_row_group(group_display.inner)) {
            continue;
        }
        for (DomNode* row_node = row_group->first_child; row_node; row_node = row_node->next_sibling) {
            if (!row_node->is_element()) continue;
            DomElement* row = row_node->as_element();
            DisplayValue row_display = resolve_display_value(row_node);
            if (row_display.inner != CSS_VALUE_TABLE_ROW) {
                continue;
            }
            repair_anonymous_table_children(
                lycon, row, CSS_VALUE_TABLE_CELL, false);
        }
    }

}

static void detect_anonymous_boxes(ViewTable* table) {
    if (!table || !table->tb) return;
    table->tb->is_annoy_tbody = 0;
    table->tb->is_annoy_tr = 0;
    table->tb->is_annoy_td = 0;
    table->tb->is_annoy_colgroup = 0;
    bool has_row_group = false;
    bool has_direct_row = false;
    bool has_direct_cell = false;
    for (View* child = static_cast<View*>(table->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            has_row_group = true;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            has_direct_row = true;
        } else if (child->view_type == RDT_VIEW_TABLE_CELL) {
            has_direct_cell = true;
        }
    }
    if (has_direct_row && !has_row_group) {
        table->tb->is_annoy_tbody = 1;
    }
    if (has_direct_cell) {
        table->tb->is_annoy_tbody = 1;
        table->tb->is_annoy_tr = 1;
    }
    for (View* child = static_cast<View*>(table->first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            bool group_has_direct_cell = false;
            for (View* gchild = static_cast<View*>(lam::dom_require<DOM_NODE_ELEMENT>(child)->first_child); gchild;
                 gchild = static_cast<View*>(gchild->next_sibling)) {
                if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                    group_has_direct_cell = true;
                    break;
                }
            }
            if (group_has_direct_cell) {
                for (View* gchild = static_cast<View*>(lam::dom_require<DOM_NODE_ELEMENT>(child)->first_child); gchild;
                     gchild = static_cast<View*>(gchild->next_sibling)) {
                    if (gchild->view_type == RDT_VIEW_TABLE_CELL) {
                        ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(gchild);
                        if (cell->td) {
                            cell->td->is_annoy_tr = 1;
                        }
                    }
                }
            }
        }
    }
}

static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewElement* parent);

static ViewElement* mark_table_box(LayoutContext* lycon, DomNode* node,
                                   ViewType view_type, DisplayValue display) {
    if (!lycon || !node) return nullptr;
    View* view = set_view(lycon, view_type, node);
    ViewElement* element = lam::view_require_element(view);
    if (!element) return nullptr;
    element->display = display;
    lycon->view = view;
    dom_node_resolve_style(node, lycon);
    if (element->font) setup_font(lycon->ui_context, &lycon->font, element->font);
    return element;
}

static void mark_table_children(LayoutContext* lycon, DomNode* node,
                                ViewElement* parent) {
    if (!lycon || !node || !parent || !node->is_element()) return;
    for (DomNode* child = node->as_element()->first_child; child;
         child = child->next_sibling) {
        if (child->is_element()) mark_table_node(lycon, child, parent);
    }
}

static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewElement* parent) {
    if (!node || !node->is_element()) return;
    DisplayValue display = resolve_display_value(node);
    NameId tag = node->tag();
    // CSS 2.1 §9.7: Elements with float become block-level elements
    // Check if this element has float set - if so, it's not a table internal element
    DomElement* elem = node->as_element();
    CssEnum float_value = CSS_VALUE_NONE;
    if (elem->position) {
        float_value = elem->positionp()->float_prop;
    } else {
        float_value = layout_specified_keyword(
            elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
    }
    if (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT) {
        DisplayValue float_display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
        elem->set_float_prelaid(true);
        {
            LayoutViewScope view_scope(lycon);
            layout_block(lycon, node, float_display);
        }
        return;
    }
    if (layout_element_is_abs_or_fixed(elem)) {
        {
            LayoutViewScope view_scope(lycon);
            layout_flow_node(lycon, node);
        }
        return;
    }
    LayoutViewScope view_scope(lycon);
    LayoutFontScope font_scope(lycon);
    lycon->elmt = node;
    if (tag == MARKUP_NAME_CAPTION || display.inner == CSS_VALUE_TABLE_CAPTION) {
        ViewBlock* caption = lam::view_require_block(set_view(lycon, RDT_VIEW_BLOCK, node));
        if (caption) {
            caption->display.inner = CSS_VALUE_TABLE_CAPTION;
            lycon->view = static_cast<View*>(caption);
            dom_node_resolve_style(node, lycon);
            DomElement* dom_elem = lam::dom_require_element(node);
            if (dom_elem->specified_style && parent && parent->view_type == RDT_VIEW_TABLE) {
                ViewTable* table = lam::view_require<RDT_VIEW_TABLE>(parent);
                if (table->tb) {
                    CssDeclaration* caption_decl = style_tree_get_declaration(
                        dom_elem->specified_style, CSS_PROPERTY_CAPTION_SIDE);
                    bool caption_bottom = false;
                    if (table_resolve_caption_side_value(
                            caption_decl ? caption_decl->value : nullptr, &caption_bottom) &&
                        caption_bottom) {
                        table->tb->caption_side = TableProp::CAPTION_SIDE_BOTTOM;
                    }
                }
            }
            LayoutContextScope lscope(lycon);
            float caption_width = lycon->line.right - lycon->line.left;
            if (caption_width <= 0) caption_width = 600;
            float content_width = caption_width;
            if (caption->bound) {
                content_width -= layout_box_metrics(caption).pad_border_h;
            }
            content_width = max(content_width, 0.0f);
            lycon->block.content_width = content_width;
            lycon->block.content_height = 10000;  // Large enough for content
            lycon->block.advance_y = 0;
            float inner_left = 0;
            if (caption->bound) {
                if (caption->boundary()->border) {
                    inner_left += caption->boundary()->border->width.left;
                    lycon->block.advance_y += caption->boundary()->border->width.top;
                }
                inner_left += caption->boundary()->padding.left;
                lycon->block.advance_y += caption->boundary()->padding.top;
            }
            lycon->line.left = inner_left;
            lycon->line.right = inner_left + content_width;
            if (caption->font) {
                setup_font(lycon->ui_context, &lycon->font, caption->font);
            }
            setup_line_height(lycon, caption);
            layout_setup_block_font_metrics(lycon);
            if (caption->blk) {
                if (!isnan(caption->block()->text_indent_percent)) {
                    lycon->block.text_indent = content_width * caption->block()->text_indent_percent / 100.0f;
                } else {
                    lycon->block.text_indent = caption->block()->text_indent;
                }
                if (lycon->block.text_indent != 0.0f) {
                    lycon->block.is_first_line = true;
                }
            }
            line_reset(lycon);  // reset start_view, advance_x, etc. for fresh line
            if (caption->blk && caption->block_mut()->text_align) {
                lycon->block.text_align = caption->block()->text_align;
            }
            if (caption->blk && caption->block_mut()->direction) {
                lycon->block.direction = caption->block()->direction;
            }
            DomNode* child = lam::dom_require_element(node)->first_child;
            for (; child; child = child->next_sibling) {
                layout_flow_node(lycon, child);
            }
            if (!lycon->line.is_line_start) { line_break(lycon); }
            float caption_content_height = lycon->block.advance_y;
            float caption_given_height = (caption->blk && caption->block_mut()->given_height >= 0) ? caption->block_mut()->given_height : -1;
            if (caption_given_height >= 0) {
                caption->height = caption_given_height;
                if (caption->bound) {
                    caption->height += layout_box_metrics(caption).pad_border_v;
                }
            } else {
                caption->height = caption_content_height;
                if (caption->bound) {
                    caption->height += caption->boundary()->padding.bottom;
                    if (caption->boundary()->border) {
                        caption->height += caption->boundary()->border->width.bottom;
                    }
                }
            }
            if (caption->blk) {
                caption->height = layout_apply_min_max_axis(
                    caption, caption->height, false, true);
            }
            caption->width = (float)caption_width;  // Preliminary width; final width set during positioning
        }
    }
    else if (layout_display_is_table_row_group(display.inner)) {
        ViewTableRowGroup* group = static_cast<ViewTableRowGroup*>(mark_table_box(
            lycon, node, RDT_VIEW_TABLE_ROW_GROUP, display));
        if (group) {
            mark_table_children(lycon, node, group);
        }
    }
    else if (display.inner == CSS_VALUE_TABLE_ROW) {
        ViewTableRow* row = static_cast<ViewTableRow*>(mark_table_box(
            lycon, node, RDT_VIEW_TABLE_ROW, display));
        if (row) {
            if (node->is_element()) {
                row->pseudo = alloc_pseudo_content_prop(lycon, lam::view_require_block(row));
                if (row->pseudo) {
                    DomElement* row_elem = node->as_element();
                    auto insert_pseudo_for_row = [&](DomElement* pseudo, bool is_before) {
                        if (!pseudo) return;
                        DisplayValue pseudo_display = resolve_display_value(pseudo);
                        bool is_cell = pseudo_display.inner == CSS_VALUE_TABLE_CELL;
                        if (is_cell) {
                            insert_pseudo_into_dom(row_elem, pseudo, is_before);
                        } else {
                            DomElement* anon_td = create_anonymous_table_element(lycon, row_elem,
                                CSS_VALUE_TABLE_CELL, "::anon-td");
                            if (anon_td) {
                                pseudo->parent = anon_td;
                                anon_td->first_child = pseudo;
                                anon_td->last_child = pseudo;
                                insert_pseudo_into_dom(row_elem, anon_td, is_before);
                            }
                        }
                    };
                    insert_pseudo_for_row(row->pseudo->before, true);
                    insert_pseudo_for_row(row->pseudo->after, false);
                }
            }
            mark_table_children(lycon, node, row);
        }
    }
    else if (display.inner == CSS_VALUE_TABLE_CELL) {
        ViewTableCell* cell = static_cast<ViewTableCell*>(mark_table_box(
            lycon, node, RDT_VIEW_TABLE_CELL, display));
        if (cell) {
            parse_cell_attributes(lycon, node, cell);
        }
    }
    else if (tag == MARKUP_NAME_COLGROUP || display.inner == CSS_VALUE_TABLE_COLUMN_GROUP) {
        ViewBlock* colgroup = static_cast<ViewBlock*>(mark_table_box(
            lycon, node, RDT_VIEW_TABLE_COLUMN_GROUP, display));
        if (colgroup) {
            mark_table_children(lycon, node, colgroup);
        }
    }
    else if (tag == MARKUP_NAME_COL || display.inner == CSS_VALUE_TABLE_COLUMN) {
        mark_table_box(lycon, node, RDT_VIEW_TABLE_COLUMN, display);
    }
}

ViewTable* build_table_tree(LayoutContext* lycon, DomNode* tableNode) {
    ViewTable* table = lam::view_require<RDT_VIEW_TABLE>(tableNode);
    dom_node_resolve_style(tableNode, lycon);
    resolve_table_properties(lycon, tableNode, table);
    if (tableNode->is_element()) {
        generate_anonymous_table_boxes(lycon, tableNode->as_element());
    }
    mark_table_children(lycon, tableNode, table);
    return table;
}

static bool table_cell_vertical_align_skips_child(View* child) {
    ViewElement* element = lam::view_as_element(child);
    return element && layout_position_is_abs_fixed(element->position);
}

static void reapply_rowspan_vertical_alignment(ViewTableCell* tcell) {
    if (!tcell || !tcell->td) return;
    if (tcell->td->row_span <= 1) return;  // Only for rowspan cells
    int valign = tcell->td->vertical_align;
    if (valign == TableCellProp::CELL_VALIGN_TOP) return;  // No adjustment needed for top
    TableCellInsets insets = table_cell_insets(tcell);
    float border_top = tcell->bound ? insets.border.top : 1.0f;
    float border_bottom = tcell->bound ? insets.border.bottom : 1.0f;
    float padding_top = insets.padding.top;
    float padding_bottom = insets.padding.bottom;
    float content_area_height = tcell->height - border_top - border_bottom - padding_top - padding_bottom;
    float content_start_y = border_top + padding_top;
    TableCellContentExtent bounds = table_cell_vertical_bounds(tcell);
    if (!bounds.has_content) return;
    float content_actual_height = bounds.max_y - bounds.min_y;
    float new_offset = table_cell_vertical_align_target(
        valign, content_area_height, content_actual_height, content_start_y);
    float adjustment = new_offset - bounds.min_y;
    shift_table_cell_vertical_align_children(tcell, adjustment);
}

static float table_rowspan_spanned_height(ViewTable* table, TableMetadata* meta,
                                          int start_row, int row_span) {
    if (!table || !table->tb || !meta || row_span <= 1) return 0.0f;
    int end_row = start_row + row_span;
    if (start_row < 0) start_row = 0;
    if (start_row >= meta->row_count) return 0.0f;
    if (end_row > meta->row_count) end_row = meta->row_count;
    if (end_row <= start_row) return 0.0f;
    float spanned_height = 0.0f;
    float row_spacing = table_inter_spacing(table, false);
    for (int r = start_row; r < end_row; r++) {
        spanned_height += meta->row_heights[r];
        bool next_row_collapsed = meta->row_collapsed &&
            r + 1 < meta->row_count && meta->row_collapsed[r + 1];
        if (row_spacing > 0.0f && r < end_row - 1 &&
            !meta->row_collapsed[r] && !next_row_collapsed) {
            spanned_height += row_spacing;
        }
    }
    return spanned_height;
}

struct TableOrderedRowElements {
    ViewBlock* header_group;
    ViewBlock* footer_group;
    ArrayList* body_groups;
    ArrayList* ordered_elements;
};

static TableOrderedRowElements table_collect_ordered_row_elements(ViewTable* table) {
    TableOrderedRowElements result = {nullptr, nullptr, arraylist_new(8), nullptr};
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            int section = group->get_section_type();
            if (section == TABLE_SECTION_THEAD && !result.header_group) {
                result.header_group = child;
            } else if (section == TABLE_SECTION_TFOOT && !result.footer_group) {
                result.footer_group = child;
            } else {
                arraylist_append(result.body_groups, child);
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            arraylist_append(result.body_groups, child);
        }
    });
    result.ordered_elements = arraylist_new(result.body_groups->length + 2);
    if (result.header_group) arraylist_append(result.ordered_elements, result.header_group);
    for (int i = 0; i < result.body_groups->length; i++) {
        arraylist_append(result.ordered_elements, result.body_groups->data[i]);
    }
    if (result.footer_group) arraylist_append(result.ordered_elements, result.footer_group);
    return result;
}

static void update_rowspan_cell_heights(ViewTable* table, TableMetadata* meta) {
    if (!table || !meta) return;
    table->each_cell( [&](ViewTableRow* row, ViewTableCell* tcell) {
            (void)row;
            if (!tcell->td || tcell->td->row_span <= 1) return;
            int start_row = tcell->td->row_index;
            int end_row = start_row + tcell->td->row_span;
            if (start_row < 0) start_row = 0;
            if (end_row > meta->row_count) end_row = meta->row_count;
            float spanned_height = table_rowspan_spanned_height(
                table, meta, tcell->td->row_index, tcell->td->row_span);
            if (spanned_height <= 0.0f) return;
            tcell->height = spanned_height;
            reapply_rowspan_vertical_alignment(tcell);
    });
}

static float table_row_float_content_bottom(ViewBlock* row) {
    float max_float_bottom = 0.0f;
    if (!row) return max_float_bottom;
    for (View* child = row->first_child; child; child = child->next_sibling) {
        if (!child->view_type || !child->is_block()) continue;
        ViewBlock* block = lam::view_require_block(child);
        if (layout_position_is_floated(block->position)) {
            float bottom = block->y + block->height;
            if (bottom > max_float_bottom) max_float_bottom = bottom;
        }
    }
    return max_float_bottom;
}

static void table_size_float_containing_row(ViewTable* table, ViewBlock* row,
                                            float* group_height) {
    float max_float_bottom = table_row_float_content_bottom(row);
    if (max_float_bottom > 0.0f) {
        row->width = table->width;
        row->height = max_float_bottom;
    }
    if (group_height && row->height > *group_height) {
        *group_height = row->height;
    }
}

static int table_row_metadata_index_from_row(ViewTableRow* trow, int fallback_index) {
    if (!trow) return fallback_index;
    ViewTableCell* first_cell = trow->first_cell();
    if (first_cell && first_cell->td && first_cell->td->row_index >= 0) {
        return first_cell->td->row_index;
    }
    return fallback_index;
}

static int table_row_metadata_index(ViewBlock* row, int fallback_index) {
    if (!row || row->view_type != RDT_VIEW_TABLE_ROW) return fallback_index;
    return table_row_metadata_index_from_row(
        lam::view_require<RDT_VIEW_TABLE_ROW>(row), fallback_index);
}

struct TableHeightSectionSummary {
    float non_body_grid_height;
    float body_natural_height;
    int body_row_count;
    int section_count;
};

static TableHeightSectionSummary table_collect_height_section_summary(ViewTable* table,
                                                                      TableMetadata* meta) {
    TableHeightSectionSummary summary = {};
    if (!table || !meta) return summary;
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            TableSectionType section_type = group->get_section_type();
            bool is_body_group = (section_type == TABLE_SECTION_TBODY);


            float group_height = 0.0f;
            int row_count_in_group = 0;
            group->each_row_with_block( [&](ViewTableRow* row, ViewBlock* row_block) {
                (void)row_block;
                int row_idx = table_row_metadata_index_from_row(row, -1);
                if (row_idx < 0 || row_idx >= meta->row_count) return;
                float row_height = meta->row_heights[row_idx];
                group_height += row_height;
                row_count_in_group++;
            });
            if (is_body_group) {
                summary.body_natural_height += group_height;
                summary.body_row_count += row_count_in_group;
                summary.section_count++;
            } else {
                summary.non_body_grid_height += group_height;
                summary.section_count++;
            }
        }
    });
    return summary;
}

static float table_caption_positive_margin(ViewBlock* caption, bool horizontal, bool start) {
    if (!caption || !caption->bound) return 0.0f;
    const Margin& margin = caption->boundary()->margin;
    float value;
    CssEnum type;
    if (horizontal) {
        value = start ? margin.left : margin.right;
        type = start ? margin.left_type : margin.right_type;
    } else {
        value = start ? margin.top : margin.bottom;
        type = start ? margin.top_type : margin.bottom_type;
    }
    return (horizontal && type == CSS_VALUE_AUTO) || value <= 0.0f ? 0.0f : value;
}

static float table_caption_height_with_margins(ViewBlock* caption) {
    if (!caption) return 0.0f;
    float margin_v = table_caption_positive_margin(caption, false, true) +
        table_caption_positive_margin(caption, false, false);
    return caption->height + margin_v;
}

static bool table_caption_is_bottom(ViewBlock* caption) {
    if (!caption) return false;
    for (ViewElement* current = caption; current; current = current->parent_view()) {
        if (!current->is_element()) continue;
        DomElement* element = current->as_element();
        CssDeclaration* declaration = element && element->specified_style
            ? style_tree_get_declaration(element->specified_style, CSS_PROPERTY_CAPTION_SIDE)
            : nullptr;
        if (!declaration || !declaration->value ||
            declaration->value->type != CSS_VALUE_TYPE_KEYWORD) continue;
        CssEnum keyword = declaration->value->data.keyword;
        if (keyword == CSS_VALUE_BOTTOM) return true;
        if (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_INITIAL) return false;
        if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET ||
            keyword == CSS_VALUE_REVERT) continue;
        return false;
    }
    return false;
}

template <typename Fn>
static void for_each_table_caption(ArrayList* captions, Fn fn) {
    if (!captions) return;
    for (int ci = 0; ci < captions->length; ci++) {
        fn(table_array_view_block(captions, ci), ci);
    }
}

struct TableCaptionCollection {
    ArrayList* captions;
    ArrayList* top_captions;
    ArrayList* bottom_captions;
    ViewBlock* first_caption;
    float top_height;
    float bottom_height;
    float total_height;
};

static TableCaptionCollection table_collect_captions(ViewTable* table) {
    TableCaptionCollection result = {
        arraylist_new(4), arraylist_new(4), arraylist_new(4), nullptr,
        0.0f, 0.0f, 0.0f
    };
    table->each_direct_block( [&](ViewBlock* child) {
        if (!table_view_is_caption(child)) return;
        arraylist_append(result.captions, child);
        if (!result.first_caption) result.first_caption = child;
        bool is_bottom = table_caption_is_bottom(child);
        arraylist_append(is_bottom ? result.bottom_captions : result.top_captions, child);
        if (child->height > 0.0f) {
            float caption_height = table_caption_height_with_margins(child);
            result.total_height += caption_height;
            if (is_bottom) result.bottom_height += caption_height;
            else result.top_height += caption_height;
        }
    });
    return result;
}

static void table_position_caption_with_margins(ViewBlock* caption, float base_y) {
    if (!caption) return;
    caption->x = table_caption_positive_margin(caption, true, true);
    caption->y = base_y + table_caption_positive_margin(caption, false, true);
}

enum TableCaptionWidthChangeReference {
    TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP,
    TABLE_CAPTION_WIDTH_REFERENCE_WRAPPER
};

static float table_adjust_caption_width_and_height(LayoutContext* lycon,
                                                   ViewTable* table,
                                                   ViewBlock* caption,
                                                   float table_width,
                                                   float wrapper_content_width,
                                                   TableCaptionWidthChangeReference reference) {
    float old_width = caption->width;
    adjust_table_caption_width(caption, wrapper_content_width);
    float comparison_width = reference == TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP
        ? caption->width : wrapper_content_width;
    if (fabs(comparison_width - old_width) <= 0.5f) {
        return table_caption_height_with_margins(caption);
    }
    return relayout_table_caption(lycon, caption, table_width);
}

static float table_position_caption_stack(LayoutContext* lycon,
                                          ViewTable* table,
                                          ArrayList* captions,
                                          float base_y,
                                          float table_width,
                                          float wrapper_content_width,
                                          TableCaptionWidthChangeReference reference) {
    float cap_y = base_y;
    float total_height = 0.0f;
    for_each_table_caption(captions, [&](ViewBlock* cap, int) {
        table_position_caption_with_margins(cap, cap_y);
        float this_cap_height = table_adjust_caption_width_and_height(
            lycon, table, cap, table_width, wrapper_content_width, reference);
        total_height += this_cap_height;
        cap_y += this_cap_height;
    });
    return total_height;
}

static float table_caption_stack_block_extent(ArrayList* captions) {
    float extent = 0.0f;
    for_each_table_caption(captions, [&](ViewBlock* caption, int) {
        extent += caption ? caption->width : 0.0f;
    });
    return extent;
}

static void table_swap_vertical_descendants(View* view) {
    if (!view) return;
    DomNode* first_child = view->is_element()
        ? view->as_element()->first_child : nullptr;
    for (View* child = static_cast<View*>(first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        float logical_x = child->x;
        float logical_y = child->y;
        float logical_width = child->width;
        float logical_height = child->height;
        child->x = logical_y;
        child->y = logical_x;
        child->width = logical_height;
        child->height = logical_width;
        table_swap_vertical_descendants(child);
    }
}

static void table_mirror_vertical_descendants(View* view,
                                              float mirror_origin,
                                              float mirror_extent) {
    if (!view) return;
    view->x = mirror_origin + mirror_extent -
        (view->x - mirror_origin) - view->width;
    DomNode* first_child = view->is_element()
        ? view->as_element()->first_child : nullptr;
    for (View* child = static_cast<View*>(first_child); child;
         child = static_cast<View*>(child->next_sibling)) {
        if (child->view_type) {
            table_mirror_vertical_descendants(child, 0.0f, view->width);
        }
    }
}

static bool table_has_explicit_physical_block_size(ViewTable* table) {
    if (!table || !table->is_element()) return false;
    DomElement* element = table->as_element();
    CssDeclaration* declaration = layout_specified_physical_size_declaration(
        element, true);
    return declaration && declaration->property_code == CSS_PROPERTY_BLOCK_SIZE;
}

static void table_publish_vertical_geometry(ViewTable* table) {
    if (!table || !layout_block_inline_axis_is_vertical(table)) return;
    TableCaptionCollection captions = table_collect_captions(table);
    float logical_width = table->width;
    float logical_height = table->height;
    float top_caption_extent = table_caption_stack_block_extent(captions.top_captions);
    float bottom_caption_extent = table_caption_stack_block_extent(captions.bottom_captions);
    float physical_width = logical_height + top_caption_extent + bottom_caption_extent;
    float physical_height = logical_width;
    bool vertical_rl = layout_block_writing_mode(table) == WM_VERTICAL_RL;
    float grid_origin = 0.0f;
    float grid_end = 0.0f;
    bool have_grid_bounds = false;
    table->each_direct_block( [&](ViewBlock* child) {
        if (table_view_is_caption(child)) {
            float caption_width = child->width;
            float margin_top = table_caption_positive_margin(child, false, true);
            float margin_bottom = table_caption_positive_margin(child, false, false);
            child->height = max(logical_width - margin_top - margin_bottom, 0.0f);
            child->y = margin_top;
            bool is_bottom = table_caption_is_bottom(child);
            if ((is_bottom && !vertical_rl) || (!is_bottom && vertical_rl)) {
                child->x = physical_width - caption_width -
                    table_caption_positive_margin(child, true, false);
            } else {
                child->x = table_caption_positive_margin(child, true, true);
            }
            table_swap_vertical_descendants(child);
            return;
        }
        float logical_x = child->x;
        float logical_y = child->y;
        float logical_child_width = child->width;
        float logical_child_height = child->height;
        child->x = logical_y;
        child->y = logical_x;
        child->width = logical_child_height;
        child->height = logical_child_width;
        table_swap_vertical_descendants(child);
        child->x += top_caption_extent;
        if (!have_grid_bounds || child->x < grid_origin) grid_origin = child->x;
        if (!have_grid_bounds || child->x + child->width > grid_end) {
            grid_end = child->x + child->width;
        }
        have_grid_bounds = true;
    });
    if (vertical_rl && have_grid_bounds) {
        // CSS Writing Modes reverses block progression in vertical-rl.
        table->each_direct_block( [&](ViewBlock* child) {
            if (!table_view_is_caption(child)) {
                table_mirror_vertical_descendants(child, grid_origin,
                    grid_end - grid_origin);
            }
        });
    }
    table->width = max(physical_width, 0.0f);
    table->height = max(physical_height, 0.0f);
    table->content_width = table->width;
    table->content_height = table->height;
    layout_normalize_vertical_breaks(table);
}

static float table_measure_caption_width_contribution(LayoutContext* lycon,
                                                      ViewTable* table,
                                                      ViewBlock* caption) {
    if (!caption) return 0.0f;
    float contribution = 0.0f;
    if (caption->blk && caption->block_mut()->given_width > 0.0f) {
        contribution = layout_apply_min_max_axis(caption, caption->block()->given_width, true, false);
    } else if (DomElement* caption_elem = caption->as_element()) {
        IntrinsicSizes caption_sizes = layout_measure_intrinsic_widths(
            lycon, caption_elem);
        contribution = layout_apply_min_max_axis(caption, ceilf(caption_sizes.min_content), true, false);
    }
    if (caption->blk && caption->block_mut()->given_width > 0.0f &&
        !layout_uses_border_box(caption) && caption->bound) {
        BoxMetrics caption_box = layout_box_metrics(caption);
        contribution += caption_box.border_h +
            (caption_box.padding.left > 0.0f ? caption_box.padding.left : 0.0f) +
            (caption_box.padding.right > 0.0f ? caption_box.padding.right : 0.0f);
    }
    if (table->bound) {
        BoxMetrics table_box = layout_box_metrics(table);
        contribution -= table_box.border_h +
            (table_box.padding.left > 0.0f ? table_box.padding.left : 0.0f) +
            (table_box.padding.right > 0.0f ? table_box.padding.right : 0.0f);
    }
    if (caption->bound) {
        float ml = table_caption_positive_margin(caption, true, true);
        float mr = table_caption_positive_margin(caption, true, false);
        if (ml + mr > 0.0f) {
            contribution += ml + mr;
        }
    }
    return contribution;
}

static void table_recalculate_row_y_positions(ViewTable* table, TableMetadata* meta,
                                              float y_accum) {
    if (!table || !meta) return;
    float row_spacing = table_inter_spacing(table, false);
    for (int r = 0; r < meta->row_count; r++) {
        meta->row_y_positions[r] = y_accum;
        y_accum += meta->row_heights[r];
        if (row_spacing > 0.0f) {
            y_accum += row_spacing;
        }
    }
}

static void table_apply_explicit_height_row_extra(TableMetadata* meta, int row_idx,
                                                  float extra_height, int eligible_row_count,
                                                  float eligible_height_total) {
    if (!meta || row_idx < 0 || row_idx >= meta->row_count ||
        eligible_row_count <= 0) return;
    float natural_height = meta->row_heights[row_idx];
    float row_extra = eligible_height_total > 0.0f
        ? extra_height * natural_height / eligible_height_total
        : extra_height / eligible_row_count;
    meta->row_heights[row_idx] += row_extra;
}

static float table_resolve_row_explicit_height(LayoutContext* lycon, TableMetadata* meta,
                                               ViewBlock* row, int row_idx) {
    if (!row || !row->is_element()) return 0.0f;
    DomElement* row_elem = row->as_element();
    if (!row_elem->specified_style) return 0.0f;
    CssDeclaration* height_decl = style_tree_get_declaration(
        row_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0.0f;
    // CSS 2.1 §17.5.3: percentage row heights compute to auto but still
    if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (row_idx >= 0 && row_idx < meta->row_count) {
            meta->row_has_percent_height[row_idx] = true;
        }
        return 0.0f;
    }
    float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    if (resolved_height > 0.0f) {
        return resolved_height;
    }
    return 0.0f;
}

static float table_resolve_row_group_explicit_height(LayoutContext* lycon,
                                                     ViewBlock* group_block,
                                                     bool* has_percent_height) {
    if (has_percent_height) *has_percent_height = false;
    if (!group_block || !group_block->is_element()) return 0.0f;
    DomElement* group_elem = group_block->as_element();
    if (!group_elem->specified_style) return 0.0f;
    CssDeclaration* height_decl = style_tree_get_declaration(
        group_elem->specified_style, CSS_PROPERTY_HEIGHT);
    if (!height_decl || !height_decl->value) return 0.0f;
    // CSS 2.1 row-group percentage heights compute to auto, but the rows
    // must be marked so later table-height distribution preserves that policy.
    if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (has_percent_height) *has_percent_height = true;
        return 0.0f;
    }
    float resolved = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
    if (resolved > 0.0f) {
        return resolved;
    }
    return 0.0f;
}

static void table_apply_row_group_min_height(LayoutContext* lycon, ViewTable* table,
                                             TableMetadata* meta, ViewTableRowGroup* group,
                                             ViewBlock* group_block,
                                             float explicit_group_height,
                                             float* current_y) {
    float content_group_height = group_block->height;
    if (explicit_group_height > group_block->height) {
        group_block->height = explicit_group_height;
    }
    if (group_block->height <= content_group_height) return;
    float extra = group_block->height - content_group_height;
    *current_y += extra;
    int eligible_rows = 0;
    group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
        (void)trow;
        if (row->height > 0.0f) eligible_rows++;
    });
    if (eligible_rows <= 0) return;
    float extra_per_row = extra / eligible_rows;
        float row_spacing = table_inter_spacing(table, false);
    float y_accum = 0.0f;
    group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
        if (row->height <= 0.0f) return;
        row->y = y_accum;
        row->height += extra_per_row;
        int row_idx = table_row_metadata_index_from_row(trow, -1);
        if (row_idx >= 0 && row_idx < meta->row_count) {
            meta->row_heights[row_idx] = row->height;
        }
        update_row_cells_after_height_change(lycon, trow, row->height, false, true);
        y_accum += row->height;
        if (row_spacing > 0.0f) {
            y_accum += row_spacing;
        }
    });
}

static void table_track_row_metrics(TableMetadata* meta, int row_idx, float row_y,
                                    float row_height) {
    if (row_idx < 0 || row_idx >= meta->row_count) return;
    meta->row_y_positions[row_idx] = row_y;
    meta->row_heights[row_idx] = row_height;
}

static void table_place_collapsed_row(ViewTable* table, TableMetadata* meta,
                                      ViewTableRow* trow, float row_y,
                                      float row_width, float metadata_y,
                                      float* col_widths, float* col_x_positions,
                                      int columns, int row_idx) {
    trow->x = 0.0f;
    trow->y = row_y;
    trow->width = row_width;
    trow->height = 0.0f;
    trow->each_cell( [&](ViewTableCell* tcell) {
        ViewBlock* cell = lam::view_require_block(tcell);
        float cell_abs_x = table_column_visual_x(table, col_widths, col_x_positions,
                                                 tcell->td->col_index,
                                                 tcell->td->col_span, columns);
        cell->x = cell_abs_x - col_x_positions[0];
        cell->y = 0.0f;
        cell->width = table_sum_span_columns(
            col_widths, tcell->td->col_index, tcell->td->col_span, columns);
        cell->height = 0.0f;
    });
    if (row_idx >= 0 && row_idx < meta->row_count) {
        meta->row_y_positions[row_idx] = metadata_y;
        meta->row_heights[row_idx] = 0.0f;
    }
}

static float table_measure_row_cells(LayoutContext* lycon, ViewTable* table,
                                     TableMetadata* meta, ViewTableRow* trow,
                                     float* col_widths, float* col_x_positions,
                                     int columns) {
    float row_height = 0.0f;
    trow->each_cell( [&](ViewTableCell* tcell) {
        float height_for_row = process_table_cell(
            lycon, tcell, table, col_widths, col_x_positions, columns,
            meta->col_edge_max_border, meta->col_collapsed, meta->col_original_widths);
        if (height_for_row > row_height) {
            row_height = height_for_row;
        }
    });
    return row_height;
}

static float table_measure_row_height(LayoutContext* lycon, ViewTable* table,
                                      TableMetadata* meta, ViewTableRow* trow,
                                      ViewBlock* row, float* col_widths,
                                      float* col_x_positions, int columns,
                                      int row_idx, bool contain_floats) {
    float row_height = table_measure_row_cells(
        lycon, table, meta, trow, col_widths, col_x_positions, columns);
    if (contain_floats && row) {
        for (DomNode* child = row->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            ViewBlock* float_view = lam::view_as_block(static_cast<View*>(child));
            if (float_view && layout_position_is_floated(float_view->position)) {
                row_height = max(row_height, float_view->height);
            }
        }
    }
    apply_row_baseline_alignment(lycon, trow, &row_height);
    float explicit_height = table_resolve_row_explicit_height(
        lycon, meta, row, row_idx);
    return max(row_height, explicit_height);
}

static float table_finalize_row_height(LayoutContext* lycon, ViewTable* table,
                                       ViewTableRow* trow, ViewBlock* row,
                                       float row_height, bool apply_fixed_height,
                                       bool include_collapsed_border) {
    if (include_collapsed_border && table->tb->border_collapse) {
        float max_top_border = 0.0f, max_bottom_border = 0.0f;
        row_height += table_row_collapsed_vertical_border_contribution(
            trow, &max_top_border, &max_bottom_border);
    }
    if (apply_fixed_height && table->tb->fixed_row_height > 0.0f) {
        apply_fixed_row_height(lycon, trow, table->tb->fixed_row_height);
    } else {
        row->height = row_height;
        update_row_cells_after_height_change(lycon, trow, row->height, false, true);
    }
    return row->height;
}

static bool table_layout_flow_row(LayoutContext* lycon, ViewTable* table,
                                  TableMetadata* meta, ViewTableRow* trow,
                                  ViewBlock* row, bool in_group, float group_start_y,
                                  float row_width, bool has_direct_float,
                                  float* col_widths, float* col_x_positions, int columns,
                                  int* global_row_index, float* current_y,
                                  float row_spacing, float* group_content_end_y) {
    if (!table || !meta || !trow || !row || !global_row_index || !current_y) return false;
    int row_idx = *global_row_index;
    bool is_collapsed = row_idx < meta->row_count && meta->row_collapsed[row_idx];
    if (is_collapsed) {
        table_place_collapsed_row(
            table, meta, trow, in_group ? *current_y - group_start_y : *current_y,
            row_width, *current_y, col_widths, col_x_positions, columns, row_idx);
        (*global_row_index)++;
        return false;
    }
    if (!in_group) {
        *current_y = table_clear_direct_float_intrusion(
            table, *current_y, row_width, has_direct_float);
    }
    row->x = 0.0f;
    row->y = in_group ? *current_y - group_start_y : *current_y;
    row->width = row_width;
    float row_height = table_measure_row_height(
        lycon, table, meta, trow, row, col_widths, col_x_positions,
        columns, row_idx, in_group);
    row_height = table_finalize_row_height(
        lycon, table, trow, row, row_height, in_group, !in_group);
    table_track_row_metrics(meta, row_idx, *current_y, row->height);
    (*global_row_index)++;
    *current_y += row->height;
    if (group_content_end_y) *group_content_end_y = *current_y;
    if (row_spacing > 0.0f && (!in_group || *global_row_index < meta->row_count)) {
        *current_y += row_spacing;
    }
    return true;
}

static void table_update_row_views_from_metadata(LayoutContext* lycon, ViewTable* table,
                                                 TableMetadata* meta) {
    if (!table || !meta) return;
    int visual_row_index = 0;
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            float group_max_y = 0.0f;
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
                int row_idx = table_row_metadata_index_from_row(trow, visual_row_index);
                visual_row_index++;
                if (row_idx < 0 || row_idx >= meta->row_count) return;
                row->height = meta->row_heights[row_idx];
                row->y = meta->row_y_positions[row_idx] - child->y;
                float row_bottom = row->y + row->height;
                if (row_bottom > group_max_y) group_max_y = row_bottom;
                update_row_cells_after_height_change(lycon, trow, row->height, true, false);
            });
            if (group_max_y > 0.0f) {
                child->height = group_max_y;
            }
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);
            int row_idx = table_row_metadata_index_from_row(trow, visual_row_index);
            visual_row_index++;
            if (row_idx < 0 || row_idx >= meta->row_count) return;
            child->height = meta->row_heights[row_idx];
            child->y = meta->row_y_positions[row_idx];
            update_row_cells_after_height_change(lycon, trow, child->height, true, false);
        }
    });
}

static void table_reposition_row_groups_from_metadata(ViewTable* table, TableMetadata* meta) {
    if (!table || !meta || !table->tb) return;
    float group_y_accum = -1.0f;
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type != RDT_VIEW_TABLE_ROW_GROUP) return;
        if (group_y_accum >= 0.0f) {
            child->y = group_y_accum;
        } else {
            group_y_accum = child->y;
        }
        float group_max_y = 0.0f;
        ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
        group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
            int row_idx = table_row_metadata_index_from_row(trow, -1);
            if (row_idx < 0 || row_idx >= meta->row_count) return;
            row->y = meta->row_y_positions[row_idx] - child->y;
            float row_bottom = row->y + row->height;
            if (row_bottom > group_max_y) group_max_y = row_bottom;
        });
        if (group_max_y > 0.0f) child->height = group_max_y;
        group_y_accum = child->y + child->height;
        float row_spacing = table_inter_spacing(table, false);
        if (row_spacing > 0.0f) {
            group_y_accum += row_spacing;
        }
    });
}

static float reflow_table_rows_from_metadata(LayoutContext* lycon, ViewTable* table,
                                             TableMetadata* meta, ArrayList* ordered_elements,
                                             float content_area_top_y) {
    if (!table || !meta || !ordered_elements) return content_area_top_y;
    float cursor_y = content_area_top_y;
    float row_spacing = table_inter_spacing(table, false);
    int visual_row_index = 0;
    for (int i = 0; i < ordered_elements->length; i++) {
        ViewBlock* child = table_array_view_block(ordered_elements, i);
        if (!child) continue;
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            float group_start_y = cursor_y;
            float group_max_y = 0.0f;
            child->y = group_start_y;
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
                int row_idx = table_row_metadata_index(row, visual_row_index);
                if (row_idx < 0 || row_idx >= meta->row_count) return;
                float row_height = meta->row_heights[row_idx];
                bool is_collapsed = meta->row_collapsed && meta->row_collapsed[row_idx];
                meta->row_y_positions[row_idx] = cursor_y;
                row->y = cursor_y - group_start_y;
                row->height = row_height;
                update_row_cells_after_height_change(lycon, trow, row_height, true, false);
                float row_bottom = row->y + row->height;
                if (row_bottom > group_max_y) group_max_y = row_bottom;
                cursor_y += row_height;
                visual_row_index++;
                bool next_row_collapsed = meta->row_collapsed &&
                    visual_row_index < meta->row_count &&
                    meta->row_collapsed[visual_row_index];
                if (!is_collapsed && !next_row_collapsed && row_spacing > 0.0f &&
                    visual_row_index < meta->row_count) {
                    cursor_y += row_spacing;
                }
            });
            child->height = group_max_y;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            int row_idx = table_row_metadata_index(child, visual_row_index);
            if (row_idx < 0 || row_idx >= meta->row_count) continue;
            float row_height = meta->row_heights[row_idx];
            bool is_collapsed = meta->row_collapsed && meta->row_collapsed[row_idx];
            meta->row_y_positions[row_idx] = cursor_y;
            child->y = cursor_y;
            child->height = row_height;
            update_row_cells_after_height_change(
                lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(child), row_height, true, false);
            cursor_y += row_height;
            visual_row_index++;
            bool next_row_collapsed = meta->row_collapsed &&
                visual_row_index < meta->row_count &&
                meta->row_collapsed[visual_row_index];
            if (!is_collapsed && !next_row_collapsed && row_spacing > 0.0f &&
                visual_row_index < meta->row_count) {
                cursor_y += row_spacing;
            }
        }
    }
    return cursor_y;
}

static void table_apply_rowspan_distributed_height(LayoutContext* lycon,
                                                   ViewTable* table,
                                                   TableMetadata* meta,
                                                   ViewTableRow* trow,
                                                   ViewBlock* row,
                                                   const char* row_label) {
    int row_idx = table_row_metadata_index_from_row(trow, -1);
    if (row_idx < 0 || row_idx >= meta->row_count) return;
    float old_height = row->height;
    row->height = meta->row_heights[row_idx];
    if (row->height == old_height) return;
    update_row_cells_after_height_change(lycon, trow, row->height, true, true);
}

static void align_table_cell_block_child(ViewTableCell* cell, ViewBlock* child,
                                         float content_start_x, float content_width) {
    if (!cell || !child || !cell->blk || content_width <= 0.0f) return;
    CssEnum align = cell->block()->legacy_block_align;
    bool vertical_middle = layout_block_inline_axis_is_vertical(cell) && cell->td &&
        cell->td->vertical_align == TableCellProp::CELL_VALIGN_MIDDLE;
    if (align != CSS_VALUE_CENTER && align != CSS_VALUE_RIGHT && !vertical_middle) return;
    if (layout_block_is_out_of_flow_positioned(child)) {
        return;
    }
    if (element_has_float(child)) return;
    if (child->width >= content_width) return;
    float target_x = content_start_x;
    if (align == CSS_VALUE_RIGHT) {
        target_x = content_start_x + content_width - child->width;
    } else {
        target_x = content_start_x + (content_width - child->width) / 2.0f;
    }
    float delta_x = target_x - child->x;
    if (fabsf(delta_x) <= 0.01f) return;
    child->x += delta_x;
}

static void layout_table_cell_content(LayoutContext* lycon, ViewBlock* cell, ViewBlock* table) {
    ViewTableCell* tcell = lam::view_require<RDT_VIEW_TABLE_CELL>(cell);
    if (!tcell) return;
    LayoutContextScope context_scope(lycon);
    LayoutViewScope view_scope(lycon);
    lycon->block.first_line_ascender = 0.0f;
    lycon->block.last_line_ascender = 0.0f;
    lycon->block.first_line_max_ascender = 0.0f;
    lycon->block.first_line_max_descender = 0.0f;
    lycon->block.last_line_max_ascender = 0.0f;
    lycon->block.last_line_max_descender = 0.0f;
    // CRITICAL: Set up the cell's font before laying out content
    // This ensures text uses the cell's font-size (e.g., 14px) instead of parent's (e.g., 16px)
    if (tcell->font) {
        setup_font(lycon->ui_context, &lycon->font, tcell->font);
    }
    setup_line_height(lycon, tcell);
    layout_setup_block_font_metrics(lycon);
    ViewTable* parent_table = get_parent_table(tcell);
    bool border_collapse = parent_table && parent_table->tb && parent_table->tb->border_collapse;
    TableCellInsets insets = table_cell_insets(tcell);
    float border_left = insets.border.left;
    float border_top = insets.border.top;
    float border_right = insets.border.right;
    float border_bottom = insets.border.bottom;
    float padding_left = insets.padding.left;
    float padding_right = insets.padding.right;
    float padding_top = insets.padding.top;
    float padding_bottom = insets.padding.bottom;
    float content_start_x, content_start_y;
    float content_width, content_height;
    if (border_collapse) {
        // IMPORTANT: Use the same float half-border values used during column width measurement
        // to avoid rounding-up errors that would reduce content_width below the measured minimum.
        // only reserved 3 for borders, causing text to wrap unexpectedly.
        float half_left_f  = tcell->td->left_resolved   ? tcell->td->left_resolved->width   / 2.0f : 0.0f;
        float half_top_f   = tcell->td->top_resolved    ? tcell->td->top_resolved->width    / 2.0f : 0.0f;
        float half_right_f = tcell->td->right_resolved  ? tcell->td->right_resolved->width  / 2.0f : 0.0f;
        float half_bot_f   = tcell->td->bottom_resolved ? tcell->td->bottom_resolved->width / 2.0f : 0.0f;
        float half_left   = half_left_f;
        float half_top    = half_top_f;
        content_start_x = half_left + padding_left;
        content_start_y = half_top + padding_top;
        // Compute line.right from cell->width minus right-side deductions (avoid double rounding):
        float line_right_x = cell->width - half_right_f - padding_right;
        float line_right_y = cell->height - half_bot_f - padding_bottom;
        content_width  = line_right_x - content_start_x;
        content_height = line_right_y - content_start_y;
    } else {
        content_start_x = border_left + padding_left;
        content_start_y = border_top + padding_top;
        content_width = cell->width - border_left - border_right - padding_left - padding_right;
        content_height = cell->height - border_top - border_bottom - padding_top - padding_bottom;
    }
    if (content_width < 0) content_width = 0;
    if (content_height < 0) content_height = 0;
    // CRITICAL FIX: Set line.left and advance_x to content_start_x to apply padding offset
    lycon->block.content_width = content_width;
    lycon->block.content_height = content_height;
    cell->content_width = content_width;
    cell->content_height = content_height;
    lycon->block.parent = &context_scope.saved_block;
    lycon->block.establishing_element = cell;
    lycon->block.is_bfc_root = true;
    lycon->block.origin_x = cell->x + content_start_x;
    lycon->block.origin_y = cell->y + content_start_y;
    lycon->block.bfc_offset_x = 0.0f;
    lycon->block.bfc_offset_y = 0.0f;
    lycon->block.float_left_edge = 0.0f;
    lycon->block.float_right_edge = content_width;
    lycon->block.saved_clear_y = -1.0f;
    block_context_reset_floats(&lycon->block);
    // CSS 2.2 §10.5: If the cell has an explicit CSS height, set given_height in the
    // This only sets given_height (for % resolution via resolve_length_value's
    // given_height fallback), NOT content_height (which affects available_space).
    lycon->block.given_height = -1;
    if (tcell->is_element()) {
        DomElement* cell_elem = tcell->as_element();
        if (cell_elem->specified_style) {
            CssDeclaration* h_decl = style_tree_get_declaration(
                cell_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (h_decl && h_decl->value && h_decl->value->type != CSS_VALUE_TYPE_PERCENTAGE) {
                float explicit_h = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, h_decl->value);
                if (explicit_h > 0) {
                    lycon->block.given_height = explicit_h;
                }
            }
        }
    }
    if (lycon->block.given_height < 0 && table && table->blk && table->block_mut()->given_height > 0) {
        float table_h = table->block()->given_height;
        if (layout_uses_border_box(table) && table->bound && table->boundary_mut()->border) {
            table_h -= layout_box_metrics(table).border_v;
        }
        float row_spacing = table_inter_spacing(parent_table, false);
        if (row_spacing > 0.0f) {
            table_h -= row_spacing * 2;
        }
        float cell_content_h = table_h - border_top - border_bottom - padding_top - padding_bottom;
        if (cell_content_h > 0) {
            lycon->block.given_height = cell_content_h;
        }
    }
    lycon->block.advance_y = content_start_y;  // Start Y position after border+padding
    lycon->line.left = content_start_x;        // Text starts after padding!
    lycon->line.right = content_start_x + content_width;  // Text ends before right padding
    lycon->line.advance_x = content_start_x;   // Start advancing from padding offset
    lycon->line.is_line_start = true;
    lycon->line.start_view = NULL;  // Reset start_view so new text nodes become start of line
    lycon->elmt = tcell;
    if (tcell->blk && tcell->block_mut()->text_align) {
        lycon->block.text_align = tcell->block()->text_align;
    }
    if (tcell->blk && tcell->block_mut()->direction) {
        lycon->block.direction = tcell->block()->direction;
    }
    if (tcell->blk) {
        if (!isnan(tcell->block()->text_indent_percent)) {
            lycon->block.text_indent = content_width * tcell->block()->text_indent_percent / 100.0f;
        } else {
            lycon->block.text_indent = tcell->block()->text_indent;
        }
        if (lycon->block.text_indent != 0.0f) {
            lycon->line.advance_x += lycon->block.text_indent;
            lycon->line.effective_left = lycon->line.left + lycon->block.text_indent;
            lycon->block.is_first_line = false;  // consumed for this line
        }
    }
    if (tcell->is_element()) {
        layout_materialize_pseudo_content(lycon, tcell);
    }
    if (tcell->is_element()) {
        DomElement* cell_elem = lam::dom_require_element(tcell);
        if (cell_elem) wrap_orphaned_table_children(lycon, cell_elem);
        DomNode* cc = lam::dom_require_element(tcell)->first_child;
        for (; cc; cc = cc->next_sibling) {
            layout_flow_node(lycon, cc);
            if (cc->is_element()) {
                ViewBlock* child_block = lam::view_as_block(static_cast<View*>(cc->as_element()));
                if (child_block && (child_block->display.outer == CSS_VALUE_BLOCK ||
                                    child_block->display.inner == CSS_VALUE_TABLE)) {
                    align_table_cell_block_child(tcell, child_block, content_start_x, content_width);
                }
            }
        }
    }
    // CSS 2.1 §10.8.1: Final line break after all cell content.
    if (!lycon->line.is_line_start) {
        lycon->line.is_last_line = true;
        line_break(lycon);
    } else {
        line_align(lycon);
    }
    if (tcell->blk) {
        tcell->block_mut()->first_line_baseline = lycon->block.first_line_ascender;
        tcell->block_mut()->last_line_baseline = lycon->block.last_line_ascender;
        tcell->block_mut()->first_line_max_ascender =
            lycon->block.first_line_max_ascender;
        tcell->block_mut()->first_line_max_descender =
            lycon->block.first_line_max_descender;
        tcell->block_mut()->last_line_max_ascender =
            lycon->block.last_line_max_ascender;
        tcell->block_mut()->last_line_max_descender =
            lycon->block.last_line_max_descender;
    }
    cell->content_height = lycon->block.advance_y - content_start_y;
    if (cell->content_height < 0.0f) cell->content_height = 0.0f;

}

static bool should_collapse_whitespace(ViewTableCell* cell) {
    if (!cell) return true;
    DomElement* elem = cell->as_element();
    if (elem && elem->blk && elem->block_mut()->white_space != 0) {
        CssEnum ws = elem->block()->white_space;
        if (ws == CSS_VALUE_PRE ||
            ws == CSS_VALUE_PRE_WRAP ||
            ws == CSS_VALUE_PRE_LINE ||
            ws == CSS_VALUE_BREAK_SPACES) {
            return false;
        }
        if (ws == CSS_VALUE_NORMAL || ws == CSS_VALUE_NOWRAP) {
            return true;
        }
    }
    CssEnum ws_value = get_white_space_value(static_cast<DomNode*>(cell));
    if (ws_value == CSS_VALUE_PRE ||
        ws_value == CSS_VALUE_PRE_WRAP ||
        ws_value == CSS_VALUE_PRE_LINE ||
        ws_value == CSS_VALUE_BREAK_SPACES) {
        return false;
    }
    return true;
}

static bool should_prevent_wrapping(ViewTableCell* cell) {
    if (!cell) return false;
    DomElement* elem = cell->as_element();
    if (elem && elem->blk && elem->block_mut()->white_space != 0) {
        CssEnum ws = elem->block()->white_space;
        return (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE);
    }
    CssEnum ws_value = get_white_space_value(static_cast<DomNode*>(cell));
    return (ws_value == CSS_VALUE_NOWRAP || ws_value == CSS_VALUE_PRE);
}

static bool is_all_whitespace(const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') {
            return false;
        }
    }
    return true;
}

static float table_cell_width_constraint_border_box(ViewTableCell* cell, float css_width,
                                                    bool border_collapse) {
    if (!cell || !cell->blk || css_width < 0.0f) return css_width;
    if (layout_uses_border_box(cell)) return css_width;
    BoxMetrics box = layout_box_metrics(cell);
    float border_horizontal = border_collapse ? 0.0f : box.border_h;
    return css_width + box.padding_h + border_horizontal;
}

static void apply_table_cell_width_constraints(ViewTableCell* cell, bool border_collapse,
                                               CellIntrinsicWidths* widths) {
    if (!cell || !cell->blk || !widths) return;
    // measure_cell_widths() returns border-box contributions. Convert CSS
    // max-width as CSS 2.1 used-width rules require.
    if (cell->block()->given_max_width >= 0.0f) {
        float max_border_box = table_cell_width_constraint_border_box(
            cell, cell->block()->given_max_width, border_collapse);
        if (widths->min_width > max_border_box) widths->min_width = max_border_box;
        if (widths->max_width > max_border_box) widths->max_width = max_border_box;
    }
    if (cell->block()->given_min_width >= 0.0f) {
        float min_border_box = table_cell_width_constraint_border_box(
            cell, cell->block()->given_min_width, border_collapse);
        if (widths->min_width < min_border_box) widths->min_width = min_border_box;
        if (widths->max_width < min_border_box) widths->max_width = min_border_box;
    }
}

// CSS 2.1 whitespace at an inline boundary comes from its edge text descendant.
static bool element_text_edge_is_whitespace(DomNode* element, bool first) {
    if (!element || !element->is_element()) return false;
    DomNode* node = first ? element->as_element()->first_child
                          : element->as_element()->last_child;
    while (node) {
        if (node->is_text()) {
            const char* text = (const char*)node->text_data();
            if (!text || !*text) return false;
            size_t length = strlen(text);
            unsigned char edge = (unsigned char)(first ? text[0] : text[length - 1]);
            return edge == ' ' || edge == '\t' || edge == '\n' ||
                   edge == '\r' || edge == '\f';
        }
        if (node->is_element()) {
            DomElement* child = node->as_element();
            node = first ? child->first_child : child->last_child;
            if (node) continue;
        }
        return false;
    }
    return false;
}

static float table_intrinsic_child_horizontal_margin(LayoutContext* lycon,
                                                    DomElement* child_elem,
                                                    bool include_shorthand) {
    if (!child_elem) return 0.0f;
    float margin_h = 0.0f;
    if (child_elem->bound) {
        if (child_elem->boundary()->margin.left_type != CSS_VALUE_AUTO) {
            margin_h += child_elem->boundary()->margin.left;
        }
        if (child_elem->boundary()->margin.right_type != CSS_VALUE_AUTO) {
            margin_h += child_elem->boundary()->margin.right;
        }
        if (margin_h != 0.0f || !child_elem->specified_style) return margin_h;
        // intrinsic measurement can see a bound view before its CSS margins are
        // resolved; fall through so specified margins still affect max-content.
    }
    if (!child_elem->specified_style) return 0.0f;
    float margin_left = 0.0f;
    float margin_right = 0.0f;
    // intrinsic table measurement and used-value resolution share the same
    // fixed-term handling for calc() margins; only the shorthand policy differs.
    layout_resolve_intrinsic_horizontal_margins(
        lycon, child_elem, false, &margin_left, &margin_right, include_shorthand);
    return margin_left + margin_right;
}

static void table_intrinsic_flush_inline_run(float* inline_run_max, float* float_run_max,
                                             float* float_run_min, float* max_width,
                                             float* min_width, bool* has_inline_content,
                                             bool* prev_ended_with_space) {
    if (!inline_run_max || !float_run_max || !float_run_min || !max_width || !min_width) return;
    float line_max = *inline_run_max + *float_run_max;
    if (line_max > *max_width) *max_width = line_max;
    if (*float_run_min > *min_width) *min_width = *float_run_min;
    *inline_run_max = 0.0f;
    *float_run_max = 0.0f;
    *float_run_min = 0.0f;
    if (has_inline_content) *has_inline_content = false;
    if (prev_ended_with_space) *prev_ended_with_space = false;
}

// This performs accurate measurement using font metrics for CSS 2.1 compliance
// border_collapse: if true, don't add cell border to width (CSS 2.1 border-collapse model)
static CellIntrinsicWidths measure_cell_widths(LayoutContext* lycon, ViewTableCell* cell, bool border_collapse = false) {
    CellIntrinsicWidths result = {0.0f, 0.0f};
    if (!cell || !cell->is_element()) return result;
    DomElement* cell_elem = cell->as_element();
    // CSS 2.1 §16.5: Resolve inherited text-transform for cell text measurement
    CssEnum cell_text_transform = get_element_text_transform(cell_elem);
    // CSS 2.1 §15.8: Resolve inherited font-variant for cell text measurement
    CssEnum cell_font_variant = get_element_font_variant(cell_elem);
    // Note: at measurement time, pseudo elements haven't been generated yet,
    // so we check the CSS styles directly via dom_element_has_before/after_content()
    bool has_pseudo_before = dom_element_has_before_content(cell_elem);
    bool has_pseudo_after = dom_element_has_after_content(cell_elem);
    bool has_pseudo_content = has_pseudo_before || has_pseudo_after;
    // CSS 2.1 §17.5.2.2: For truly empty cells (no DOM children and no pseudo content),
    // intrinsic widths are determined by padding and border only (content width = 0).
    if (!cell_elem->first_child && !has_pseudo_content) {
        BoxMetrics box = layout_box_metrics(cell);
        float padding_horizontal = box.padding_h;
        float border_horizontal = border_collapse ? 0.0f : box.border_h;
        result.min_width = padding_horizontal + border_horizontal;
        result.max_width = padding_horizontal + border_horizontal;
        apply_table_cell_width_constraints(cell, border_collapse, &result);
        return result;
    }
    // Set up CSS 2.1 measurement context with infinite width
    radiant::LayoutMeasureScope measure_scope(lycon, cell);
    // Apply the cell's CSS font properties for accurate measurement
    if (cell->font) {
        setup_font(lycon->ui_context, &lycon->font, cell->font);
    }
    // CSS 2.1: Infinite width for preferred content width (no line wrapping)
    lycon->block.content_width = 10000.0f;
    lycon->block.content_height = 10000.0f;
    lycon->block.advance_y = 0;
    lycon->line.left = 0;
    lycon->line.right = 10000.0f;
    lycon->line.advance_x = 0;
    lycon->line.is_line_start = true;
    float max_width = 0.0f;
    float min_width = 0.0f;
    // Check if we should collapse whitespace based on CSS white-space property
    bool collapse_ws = should_collapse_whitespace(cell);
    // CSS Text 3 §5.2: word-break: break-word behaves as overflow-wrap: anywhere
    CssEnum cell_overflow_wrap = CSS_VALUE_NORMAL;
    {
        DomNode* n = static_cast<DomNode*>(cell);
        while (n) {
            if (n->is_element()) {
                DomElement* el = lam::dom_require<DOM_NODE_ELEMENT>(n);
                if (el->blk) {
                    if (el->block()->overflow_wrap != 0) {
                        cell_overflow_wrap = el->block()->overflow_wrap;
                        break;
                    }
                    if (el->block()->word_break == CSS_VALUE_BREAK_WORD) {
                        cell_overflow_wrap = CSS_VALUE_ANYWHERE;
                        break;
                    }
                }
            }
            n = n->parent;
        }
    }
    // CSS 2.1: For inline content, consecutive text nodes flow on the same line.
    float inline_run_max = 0.0f;  // Running sum for current inline sequence
    float float_run_max = 0.0f;   // Sum of side-by-side floats in the current run
    float float_run_min = 0.0f;   // Widest float in the current run
    bool has_inline_content = false;  // Track if we have any inline content
    bool prev_ended_with_space = false;  // Track whitespace between text nodes
    // CSS 2.1 §16.1: text-indent applies to the first formatted line of a block
    // Percentage text-indent cannot be resolved during intrinsic measurement
    // (circular dependency with table width), so only fixed lengths are used.
    float cell_text_indent = 0.0f;
    if (cell->blk && cell->block_mut()->text_indent != 0.0f && isnan(cell->block_mut()->text_indent_percent)) {
        cell_text_indent = cell->block()->text_indent;
    }
    for (DomNode* child = cell_elem->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            const unsigned char* text = child->text_data();
            if (text && *text) {
                size_t text_len = strlen((const char*)text);
                const char* measure_text = (const char*)text;
                size_t measure_len = text_len;
                static char normalized_buffer[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.
                bool original_has_leading_ws = (text_len > 0 && is_all_whitespace((const char*)text, 1));
                bool original_has_trailing_ws = false;
                if (text_len > 0) {
                    const char* end = (const char*)text + text_len - 1;
                    while (end >= (const char*)text && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                        original_has_trailing_ws = true;
                        end--;
                    }
                }
                if (collapse_ws) {
                    if (is_all_whitespace((const char*)text, text_len)) {
                        // Whitespace-only text contributes a space between adjacent text nodes
                        prev_ended_with_space = true;
                        continue; // Skip whitespace-only text nodes
                    }
                    size_t normalized_len = layout_normalize_collapsible_whitespace(
                        (const char*)text, text_len, normalized_buffer, sizeof(normalized_buffer));
                    if (normalized_len == 0) continue; // Skip if normalized to nothing
                    measure_text = normalized_buffer;
                    measure_len = normalized_len;
                }
                TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                    lycon, measure_text, measure_len, cell_text_transform, cell_font_variant,
                    CSS_VALUE_NORMAL, cell_overflow_wrap, CSS_VALUE_NORMAL);
                float text_max = (float)widths.max_content;  // PCW (max-content)
                float text_min = (float)widths.min_content;  // MCW (min-content)
                if (collapse_ws && has_inline_content && (prev_ended_with_space || original_has_leading_ws) && lycon->font.style) {
                    inline_run_max += lycon->font.style->space_width;
                }
                inline_run_max += text_max;
                has_inline_content = true;
                if (text_min > min_width) min_width = text_min;
                prev_ended_with_space = original_has_trailing_ws;
            }
        }
        else if (child->is_element()) {
            // For nested block/inline elements, check for explicit CSS width first
            DomElement* child_elem = child->as_element();
            // This properly handles explicit CSS widths (with border/padding),
            DisplayValue child_display = resolve_display_value(child);
            bool child_is_replaced = child_display.inner == RDT_DISPLAY_REPLACED;
            if (!child_elem->styles_resolved() &&
                child_is_replaced &&
                child_elem->specified_style &&
                child_elem->specified_style->tree &&
                child_elem->specified_style->tree->node_count > 0) {
                // Table intrinsic sizing must resolve styled cell children before
                // used margins and aspect-ratio transfers are measured.
                radiant::LayoutRunModeScope run_mode_scope(
                    lycon, radiant::RunMode::ComputeSize);
                View* saved_view = lycon->view;
                lycon->view = static_cast<View*>(child_elem);
                dom_node_resolve_style(child_elem, lycon);
                lycon->view = saved_view;
            }
            IntrinsicSizes child_sizes = layout_measure_intrinsic_widths(lycon, child_elem);
            float child_max = child_sizes.max_content;
            float child_min = child_sizes.min_content;
            float child_unresolved_box_extra = layout_unresolved_html_cell_horizontal_box_extra(child_elem);
            if (child_unresolved_box_extra > 0.0f) {
                child_max += child_unresolved_box_extra;
                child_min += child_unresolved_box_extra;
            }
            bool is_inline = (child_display.outer == CSS_VALUE_INLINE ||
                              child_display.outer == CSS_VALUE_INLINE_BLOCK);
            bool child_is_float = layout_element_is_floated(child_elem);
            // Special handling for <br> - it breaks the inline run even though it's inline
            NameId child_tag = child->tag();
            bool is_line_break = (child_tag == MARKUP_NAME_BR);
            if (is_line_break) {
                table_intrinsic_flush_inline_run(
                    &inline_run_max, &float_run_max, &float_run_min, &max_width, &min_width,
                    &has_inline_content, &prev_ended_with_space);
            } else if (is_inline) {
                // CSS 2.1: Account for whitespace between inline elements.
                bool starts_with_ws = element_text_edge_is_whitespace(child, true);
                if (collapse_ws && has_inline_content && (prev_ended_with_space || starts_with_ws) && lycon->font.style) {
                    inline_run_max += lycon->font.style->space_width;
                }
                // CSS 2.1: inline element horizontal margins contribute to line box width
                float inline_margin_h = table_intrinsic_child_horizontal_margin(
                    lycon, child_elem, false);
                if (!child_elem->bound && !child_elem->specified_style) {
                    NameId ctag = child_elem->tag();
                    if (ctag == MARKUP_NAME_INPUT) {
                        const char* inp_type = child_elem->get_attribute("type");
                        if (inp_type && strcmp(inp_type, "radio") == 0) {
                            inline_margin_h = FormDefaults::RADIO_MARGIN_LEFT + FormDefaults::RADIO_MARGIN_RIGHT;
                        } else if (inp_type && strcmp(inp_type, "checkbox") == 0) {
                            inline_margin_h = FormDefaults::CHECKBOX_MARGIN_LEFT + FormDefaults::CHECKBOX_MARGIN_RIGHT;
                        }
                    }
                }
                inline_run_max += child_max + inline_margin_h;
                child_min += inline_margin_h;
                if (child_min < 0) child_min = 0;
                has_inline_content = true;
                prev_ended_with_space = element_text_edge_is_whitespace(child, false);
            } else {
                if (!child_is_float) {
                    table_intrinsic_flush_inline_run(
                        &inline_run_max, &float_run_max, &float_run_min, &max_width, &min_width,
                        &has_inline_content, &prev_ended_with_space);
                }
                float margin_h = table_intrinsic_child_horizontal_margin(
                    lycon, child_elem, true);
                float block_outer_max = child_max + margin_h;
                if (block_outer_max < 0) block_outer_max = 0;
                float block_outer_min = child_min + margin_h;
                if (block_outer_min < 0) block_outer_min = 0;
                if (child_is_float) {
                    // CSS Sizing 3 §5: at max-content, floats are placed as high
                    float rounded_child = ceilf(block_outer_max * 2.0f) / 2.0f;
                    float_run_max += rounded_child;
                    if (block_outer_min > float_run_min) float_run_min = block_outer_min;
                    child_min = block_outer_min;
                } else {
                    if (block_outer_max > max_width) max_width = block_outer_max;
                    child_min = block_outer_min;
                }
            }
            if (child_min > min_width) min_width = child_min;
        }
    }
    // CSS 2.1 §12.2: Account for ::before/::after pseudo-element generated content
    // get content directly from CSS styles via dom_element_get_pseudo_element_content()
    if (has_pseudo_content) {
        for (int p = 0; p < 2; p++) {
            bool is_before = (p == 0);
            if ((is_before && !has_pseudo_before) || (!is_before && !has_pseudo_after)) continue;
            const char* content = nullptr;
            if (lycon->counter_context) {
                content = dom_element_get_pseudo_element_content_with_counters(
                    cell_elem, is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER,
                    lycon->counter_context, lycon->scratch.arena);
            }
            if (!content) {
                content = dom_element_get_pseudo_element_content(
                    cell_elem, is_before ? PSEUDO_ELEMENT_BEFORE : PSEUDO_ELEMENT_AFTER);
            }
            if (!content || !*content) continue;
            size_t content_len = strlen(content);
            TextIntrinsicWidths widths = layout_measure_text_intrinsic_widths(
                lycon, content, content_len, cell_text_transform, cell_font_variant,
                CSS_VALUE_NORMAL, cell_overflow_wrap, CSS_VALUE_NORMAL);
            float text_max = (float)widths.max_content;
            float text_min = (float)widths.min_content;
            inline_run_max += text_max;
            has_inline_content = true;
            if (text_min > min_width) min_width = text_min;
        }
    }
    // CSS 2.1 §16.1: text-indent adds to the first line of inline content.
    if (cell_text_indent > 0 && has_inline_content) {
        inline_run_max += cell_text_indent;
        min_width += cell_text_indent;
    }
    {
        float line_max = inline_run_max + float_run_max;
        if (line_max > max_width) max_width = line_max;
        if (float_run_min > min_width) min_width = float_run_min;
    }
    // CSS Text 3 §5.2: white-space: nowrap/pre prevents soft wrap opportunities,
    // so min-content width equals max-content width (content cannot break into lines)
    if (should_prevent_wrapping(cell) && max_width > min_width) {
        min_width = max_width;
    }


    BoxMetrics box = layout_box_metrics(cell);
    // table auto-layout measures percentage cell padding against an indefinite
    // inline size first; using the previously resolved box value inflated the
    // column contribution before the final table width was known.
    LayoutContainingBlockScope intrinsic_inline_scope(lycon, -1.0f, -1.0f, false);
    float padding_horizontal = layout_intrinsic_padding_border_axis(
        lycon, cell->as_element(), true, -1.0f) - box.border_h;
    if (padding_horizontal < 0.0f) padding_horizontal = 0.0f;
    // CSS 2.1 §17.6.2: In border-collapse mode, cell borders don't contribute to column widths.
    // The column widths are content+padding only. The half-borders are added only at
    float border_horizontal = border_collapse ? 0.0f : box.border_h;
    max_width += border_horizontal + padding_horizontal;
    min_width += border_horizontal + padding_horizontal;
    // CSS 2.1: Ensure max_width is at least 1px for cells that have actual content
    // Note: min_width is NOT clamped - cells with no visible content can be 0-width
    if (max_width < 1.0f && max_width > 0.0f) max_width = 1.0f;


    result.max_width = max_width;
    result.min_width = min_width;
    apply_table_cell_width_constraints(cell, border_collapse, &result);
    return result;
}

static int table_row_remaining_in_group(ViewTableRow* row) {
    if (!row) return 0;
    int remaining = 0;
    for (View* sibling = static_cast<View*>(row); sibling; sibling = static_cast<View*>(sibling->next_sibling)) {
        if (sibling->view_type == RDT_VIEW_TABLE_ROW) remaining++;
    }
    return remaining > 0 ? remaining : 1;
}

static bool normalize_rowspans_to_row_groups(ViewTable* table) {
    bool changed = false;
    if (!table) return changed;
    table->each_row( [&](ViewTableRow* row) {
        int remaining_in_group = table_row_remaining_in_group(row);
        row->each_cell( [&](ViewTableCell* cell) {
            int original_span = cell->td->row_span;
            int used_span = original_span;
            if (used_span == 0) {
                used_span = remaining_in_group;
            } else if (used_span > remaining_in_group) {
                // CSS 2.1 §17.5: a cell box cannot extend beyond the last row
                used_span = remaining_in_group;
            }
            if (used_span < 1) used_span = 1;
            if (used_span != original_span) {
                cell->td->row_span = used_span;
                changed = true;
            }
        });
    });
    return changed;
}

static int table_place_span(bool* occupied, TableMetadata* meta, int rows, int columns,
                            int row, int col, int row_span, int col_span,
                            int* start_col = nullptr, int* max_col_used = nullptr) {
    while (col < columns && (meta ? meta->grid(row, col) : occupied[row * columns + col])) col++;
    if (start_col) *start_col = col;
    for (int r = row; r < row + row_span && r < rows; r++) {
        for (int c = col; c < col + col_span && c < columns; c++) {
            if (meta) meta->grid(r, c) = true;
            else occupied[r * columns + c] = true;
        }
    }
    int right = col + col_span;
    if (max_col_used && right > *max_col_used) *max_col_used = right;
    return right;
}

static TableMetadata* analyze_table_structure(LayoutContext* lycon, ViewTable* table) {
    int columns = 0;
    int rows = 0;
    // CSS 2.1 §17.5.5: Collapsed rows still contribute to column width calculation
    table->each_row( [&](ViewTableRow* row) {
        rows++;
        int row_cells = 0;
        for_each_table_row_cell_slot(row, [&](View* child) {
            if (child->view_type == RDT_VIEW_TABLE_CELL) {
                ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
                row_cells += cell->td->col_span;
            } else {
                row_cells++;
            }
        });
        if (row_cells > columns) columns = row_cells;
    });
    // CSS 2.1 §17.2.1: Column count is max(cells_per_row, col_element_count)
    {
        int col_count = 0;
        table->each_column_source( [&](ViewElement* child) {
            if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                bool has_col = false;
                radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                    col_count += table_positive_span_attr(col);
                    has_col = true;
                });
                if (!has_col) {
                    col_count += table_positive_span_attr(child);
                }
            } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                col_count += table_positive_span_attr(child);
            }
        });
        if (col_count > columns) columns = col_count;
    }
    if (columns <= 0) return nullptr;
    // CSS 2.1 §17.5: a cell cannot extend beyond the last row box of its row group.
    // HTML rowspan=0 also spans only the remaining rows in the row group.
    {
        normalize_rowspans_to_row_groups(table);
        // CSS Tables slot placement must account for every rowspan, not only
        // rowspans normalized from zero; otherwise later-row cells reuse an
        // occupied column instead of creating the required missing-cell slot.
        int est_cols = columns * 2 + 4;  // generous estimate
        bool* occupied = (bool*)mem_calloc(rows * est_cols, sizeof(bool), MEM_CAT_LAYOUT);
        int max_col_used = 0;
        int cur_row = 0;
        table->each_row( [&](ViewTableRow* row) {
            int col = 0;
            for_each_table_row_cell_slot(row, [&](View* child) {
                if (is_out_of_flow_table_cell_slot(child)) {
                    col = table_place_span(occupied, nullptr, rows, est_cols,
                                           cur_row, col, 1, 1, nullptr, &max_col_used);
                    return;
                }
                ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
                col = table_place_span(occupied, nullptr, rows, est_cols,
                                       cur_row, col, cell->td->row_span,
                                       cell->td->col_span, nullptr, &max_col_used);
            });
            cur_row++;
        });
        mem_free(occupied);
        if (max_col_used > columns) {
            columns = max_col_used;
        }
    }
    TableMetadata* meta = table_metadata_create(&lycon->scratch, columns, rows);
    int current_row = 0;
    table->each_row( [&](ViewTableRow* row) {
        // CSS 2.1 §17.5.5: Rows with visibility: collapse don't contribute to height
        if (is_visibility_collapse(lam::view_require_block(row))) {
            meta->row_collapsed[current_row] = true;
        }
        int col = 0;
        for_each_table_row_cell_slot(row, [&](View* child) {
            if (is_out_of_flow_table_cell_slot(child)) {
                col = table_place_span(nullptr, meta, rows, columns,
                                       current_row, col, 1, 1);
                return;
            }
            ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(child);
            int start_col = 0;
            col = table_place_span(nullptr, meta, rows, columns, current_row, col,
                                   cell->td->row_span, cell->td->col_span, &start_col);
            cell->td->col_index = (start_col < columns) ? start_col : (columns > 0 ? columns - 1 : 0);
            cell->td->row_index = current_row;
        });
        current_row++;
    });
    // CSS 2.1 §17.5.5: Track visibility: collapse for columns
    {
        int col_idx = 0;
        table->each_column_source( [&](ViewElement* child) {
            if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                bool colgroup_collapsed = is_visibility_collapse(lam::view_require_block(child));
                bool has_col_children = false;
                radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                    has_col_children = true;
                    if (col_idx < columns) {
                        if (colgroup_collapsed || is_visibility_collapse(lam::view_require_block(col))) {
                            meta->col_collapsed[col_idx] = true;
                        }
                        col_idx++;
                    }
                });
                if (!has_col_children) {
                    int span = table_positive_span_attr(child);
                    for (int s = 0; s < span && col_idx < columns; s++) {
                        if (colgroup_collapsed) {
                            meta->col_collapsed[col_idx] = true;
                        }
                        col_idx++;
                    }
                }
            } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                if (col_idx < columns && is_visibility_collapse(lam::view_require_block(child))) {
                    meta->col_collapsed[col_idx] = true;
                }
                col_idx++;
            }
        });
    }
    return meta;
}

void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    if (!table) return;
    float row_spacing = table_inter_spacing(table, false);
    float column_spacing = table_inter_spacing(table, true);
    float table_font_size = (table->tb && table->tb->computed_font_size > 0)
                           ? table->tb->computed_font_size : 16.0f;
    table->tb->fixed_row_height = 0;  // 0 = auto height (calculate from content)
    bool has_direct_float = table_has_direct_float(table);
    TableCaptionCollection caption_collection = table_collect_captions(table);
    ArrayList* captions = caption_collection.captions;
    ArrayList* top_captions = caption_collection.top_captions;
    ArrayList* bottom_captions = caption_collection.bottom_captions;
    ViewBlock* caption = caption_collection.first_caption;  // first caption (for backward-compat checks)
    float top_caption_height = caption_collection.top_height;
    float caption_height = caption_collection.total_height;
    TableMetadata* meta = analyze_table_structure(lycon, table);
    if (!meta) {
        // CSS 2.1 §17.4: A table with only a caption is valid; the caption
        if (caption) {
            float table_width = caption->width;
            if (caption->blk && caption->block_mut()->given_width > 0) {
                table_width = caption->block()->given_width;
                if (caption->bound) {
                    table_width += layout_box_metrics(caption).pad_border_h;
                }
            } else if (DomElement* caption_elem = caption->as_element()) {
                IntrinsicSizes caption_sizes = layout_measure_intrinsic_widths(
                    lycon, caption_elem);
                float available_width = lycon->block.content_width;
                if (available_width <= 0.0f) {
                    available_width = lycon->line.right - lycon->line.left;
                }
                AvailableSize available = (available_width > 0.0f)
                    ? AvailableSize::make_definite(available_width)
                    : AvailableSize::make_indefinite();
                table_width = ceilf(compute_shrink_to_fit_width(
                    caption_sizes.min_content, caption_sizes.max_content, available));
                table_width = layout_apply_min_max_axis(caption, table_width, true, false);
            }
            float caption_box_width = table_width;  // Caption's own box width (without margins)
            table_width += table_caption_positive_margin(caption, true, true) +
                table_caption_positive_margin(caption, true, false);
            float cap_y = 0;
            for_each_table_caption(top_captions, [&](ViewBlock* cap, int ci) {
                table_position_caption_with_margins(cap, cap_y);
                cap->width = caption_box_width;
                cap_y += table_caption_height_with_margins(cap);
            });
            float grid_height = 0;
            if (table->blk && table->block_mut()->given_height >= 0) {
                grid_height = table->block()->given_height;
                if (table->bound) {
                    BoxMetrics table_box = layout_box_metrics(table);
                    grid_height += table_box.border_v;
                    if (table_box.padding.top > 0) grid_height += table_box.padding.top;
                    if (table_box.padding.bottom > 0) grid_height += table_box.padding.bottom;
                }
            }
            cap_y = grid_height;
            for_each_table_caption(bottom_captions, [&](ViewBlock* cap, int ci) {
                table_position_caption_with_margins(cap, cap_y);
                cap->width = caption_box_width;
                cap_y += table_caption_height_with_margins(cap);
            });
            float total_height = caption_height + grid_height;
            table->width = table_width;
            table->height = total_height;
            table->content_width = table_width;
            table->content_height = total_height;
            lam::view_require_block(table)->height = total_height;
        } else {
            // if specified, otherwise from the element's own padding and border.
            float bp_top = 0, bp_bottom = 0, bp_left = 0, bp_right = 0;
            if (table->bound) {
                if (table->boundary_mut()->padding.top > 0) bp_top += table->boundary_mut()->padding.top;
                if (table->boundary_mut()->padding.bottom > 0) bp_bottom += table->boundary_mut()->padding.bottom;
                if (table->boundary_mut()->padding.left > 0) bp_left += table->boundary_mut()->padding.left;
                if (table->boundary_mut()->padding.right > 0) bp_right += table->boundary_mut()->padding.right;
                if (table->boundary()->border) {
                    bp_top += table->boundary()->border->width.top;
                    bp_bottom += table->boundary()->border->width.bottom;
                    bp_left += table->boundary()->border->width.left;
                    bp_right += table->boundary()->border->width.right;
                }
            }
            if (table->blk && table->block_mut()->given_width > 0) {
                if (layout_uses_border_box(table)) {
                    table->width = table->block()->given_width;
                } else {
                    table->width = table->block()->given_width + bp_left + bp_right;
                }
            } else {
                float empty_table_auto_width = bp_left + bp_right;
                float column_spacing = table_inter_spacing(table, true);
                if (column_spacing > 0.0f) {
                    empty_table_auto_width += 2.0f * column_spacing;
                }
                bool has_float_children = false;
                for (View* ch = table->first_child; ch; ch = ch->next_sibling) {
                    if (ch->node_type != DOM_NODE_ELEMENT) continue;
                    ViewBlock* vb = lam::view_require_block(ch);
                    if (layout_position_is_floated(vb->position)) {
                        has_float_children = true;
                        break;
                    }
                }
                bool is_floated = layout_position_is_floated(table->position);
                bool is_inline_table = (table->display.outer == CSS_VALUE_INLINE);
                bool is_abspos = layout_position_is_abs_fixed(table->position);
                if (has_float_children && !is_floated && !is_inline_table && !is_abspos) {
                    float container_width = lycon->block.content_width;
                    if (container_width <= 0) {
                        ViewBlock* parent = lam::view_as_block(static_cast<View*>(table->parent));
                        if (parent && parent->width > 0) {
                            container_width = parent->width;
                            if (parent->bound) {
                                container_width -= layout_box_metrics(parent).pad_border_h;
                            }
                        }
                    }
                    if (container_width > 0) {
                        table->width = container_width;
                    } else {
                        table->width = empty_table_auto_width;
                    }
                } else {
                    table->width = empty_table_auto_width;
                }
            }
            table->width = layout_apply_min_max_axis(table, table->width, true, true);
            table->content_width = layout_content_size_from_border_box(
                table, table->width, true);
            table->height = bp_top + bp_bottom;
            if (table->blk && table->block_mut()->given_height > 0) {
                if (layout_uses_border_box(table)) {
                    table->height = table->block()->given_height;
                } else {
                    table->height = table->block()->given_height + bp_top + bp_bottom;
                }
            }
            lam::view_require_block(table)->height = table->height;
            for (View* tch = table->first_child; tch; tch = tch->next_sibling) {
                if (!tch->view_type) continue;  // skip nil-views (text nodes)
                ViewBlock* tblk = lam::view_require_block(tch);
                bool is_row_group = (tblk->view_type == RDT_VIEW_TABLE_ROW_GROUP);
                bool is_row = (tblk->view_type == RDT_VIEW_TABLE_ROW);
                if (!is_row_group && !is_row) continue;
                float group_height = 0;
                if (is_row) {
                    table_size_float_containing_row(table, tblk, nullptr);
                } else {
                    for (View* rch = tblk->first_child; rch; rch = rch->next_sibling) {
                        if (!rch->view_type) continue;
                        ViewBlock* row = lam::view_require_block(rch);
                        if (row->view_type != RDT_VIEW_TABLE_ROW) continue;
                        table_size_float_containing_row(table, row, &group_height);
                    }
                    if (group_height > 0) {
                        tblk->width = table->width;
                        tblk->height = group_height;
                    }
                }
            }
        }
        return;
    }
    int columns = meta->column_count;
    int rows = meta->row_count;
    if (table->tb->border_collapse) {
        resolve_collapsed_borders(lycon, table, meta);
        table->each_cell( [&](ViewTableRow* row, ViewTableCell* tcell) {
                (void)row;
                int col = tcell->td->col_index;
                int right_edge = col + tcell->td->col_span;
                if (tcell->td->left_resolved && col >= 0 && col <= columns) {
                    float w = tcell->td->left_resolved->width;
                    if (w > meta->col_edge_max_border[col]) {
                        meta->col_edge_max_border[col] = w;
                    }
                }
                if (tcell->td->right_resolved && right_edge >= 0 && right_edge <= columns) {
                    float w = tcell->td->right_resolved->width;
                    if (w > meta->col_edge_max_border[right_edge]) {
                        meta->col_edge_max_border[right_edge] = w;
                    }
                }
        });
    }
    float explicit_table_width = 0;
    bool has_explicit_table_width = false;
    float table_content_width = 0;
    if (table->blk && table->block_mut()->given_width >= 0) {
        explicit_table_width = table->block()->given_width;
        has_explicit_table_width = true;
    }
    if (!has_explicit_table_width && table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* width_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_WIDTH);
            if (width_decl && width_decl->value) {
                if (width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    double percentage = width_decl->value->data.percentage.value;
                    float container_width_f = lycon->available_space.width.is_definite()
                        ? lycon->available_space.width.value
                        : lycon->block.content_width;
                    float container_width = container_width_f;
                    if (container_width <= 0) {
                        container_width = lycon->line.right - lycon->line.left;
                    }
                    if (container_width > 0) {
                        explicit_table_width = container_width * percentage / 100.0;
                        has_explicit_table_width = true;
                    }
                }
                else if (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
                         (width_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
                          width_decl->value->data.number.value == 0)) {
                    float resolved_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
                    explicit_table_width = resolved_width;
                    has_explicit_table_width = true;
                }
            }
        }
    }
    if (explicit_table_width > 0) {
        table_content_width = explicit_table_width;
        bool table_width_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        if (table_width_is_border_box && table->bound && table->boundary_mut()->border) {
            BoxMetrics table_box = layout_box_metrics(table);
            table_content_width -= table_box.border_h;
        }
        if (table_width_is_border_box && !table->tb->border_collapse &&
            table->bound && table->boundary_mut()->padding.left >= 0 && table->boundary_mut()->padding.right >= 0) {
            table_content_width -= layout_box_metrics(table).padding_h;
        }
        if (column_spacing > 0.0f) {
            table_content_width -= (columns + 1) * column_spacing;
        }
    }
    float* col_widths = meta->col_widths;
    ArrayList* colspan_widths = arraylist_new(8);
    table->each_cell( [&](ViewTableRow* row, ViewTableCell* tcell) {
            (void)row;
            int col = tcell->td->col_index;
            LayoutFontScope font_scope(lycon);
            if (tcell->font) {
                setup_font(lycon->ui_context, &lycon->font, tcell->font);
            }
            bool cell_width_is_table_relative = false;
            float cell_width = get_cell_css_width(lycon, tcell, table_content_width,
                table->tb->border_collapse, &cell_width_is_table_relative);
            float cell_width_percent = get_cell_css_width_percent(tcell);
            if (cell_width_percent > 0.0f && col >= 0 && col < meta->column_count) {
                int span = tcell->td->col_span > 0 ? tcell->td->col_span : 1;
                float percent_per_col = cell_width_percent / span;
                for_each_table_span_column(col, span, meta->column_count, [&](int c) {
                    if (percent_per_col > meta->col_percent_widths[c]) {
                        meta->col_percent_widths[c] = percent_per_col;
                    }
                });
            }
            if (cell_width > 0 && tcell->td->col_span == 1 && col >= 0 && col < meta->column_count) {
                meta->col_has_explicit_width[col] = true;
            }
            float min_width = 0.0f;
            float pref_width = 0.0f;
            if (cell_width == 0.0f) {
                CellIntrinsicWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                pref_width = widths.max_width;
                min_width = widths.min_width;
                cell_width = pref_width;
            } else if (table->tb->border_collapse) {
                pref_width = cell_width;
                CellIntrinsicWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                min_width = widths.min_width;
            } else {
                pref_width = cell_width;
                CellIntrinsicWidths widths = measure_cell_widths(lycon, tcell, table->tb->border_collapse);
                min_width = cell_width_is_table_relative ?
                    widths.min_width : (widths.min_width > cell_width ? widths.min_width : cell_width);
            }
            if (should_prevent_wrapping(tcell) && pref_width < min_width) {
                pref_width = min_width;
                cell_width = min_width;
            }
            tcell->td->intrinsic_width = pref_width;
            if (table->tb->border_collapse && tcell->td->col_span == 1) {
                float half_left = tcell->td->left_resolved ? tcell->td->left_resolved->width / 2.0f : 0.0f;
                float half_right = tcell->td->right_resolved ? tcell->td->right_resolved->width / 2.0f : 0.0f;
                pref_width += half_left + half_right;
                min_width += half_left + half_right;
                cell_width += half_left + half_right;
            }
            if (tcell->td->col_span == 1) {
                if (col >= 0 && col < meta->column_count) {
                    if (min_width > meta->col_min_widths[col]) {
                        meta->col_min_widths[col] = min_width;
                    }
                    if (min_width > meta->col_single_min_widths[col]) {
                        meta->col_single_min_widths[col] = min_width;
                    }
                    if (pref_width > meta->col_max_widths[col]) {
                        meta->col_max_widths[col] = pref_width;
                    }
                    if (cell_width > col_widths[col]) {
                        col_widths[col] = cell_width;
                    }
                }
            } else {
                ColspanWidthContribution* contribution =
                    (ColspanWidthContribution*)mem_calloc(1, sizeof(ColspanWidthContribution), MEM_CAT_LAYOUT);
                contribution->cell = tcell;
                contribution->col = col;
                contribution->span = tcell->td->col_span;
                contribution->order = colspan_widths->length;
                contribution->min_width = min_width;
                contribution->pref_width = pref_width;
                contribution->cell_width = cell_width;
                arraylist_append(colspan_widths, contribution);
            }
    });
    arraylist_sort(colspan_widths, compare_colspan_width_contributions);
    for (int i = 0; i < colspan_widths->length; i++) {
        ColspanWidthContribution* contribution =
            (ColspanWidthContribution*)colspan_widths->data[i];
        apply_colspan_width_contribution(table, meta, contribution);
        mem_free(contribution);
    }
    arraylist_free(colspan_widths);
    for (int c = 0; c < columns; c++) {
        ViewBlock* col_elem = find_column_element(table, c);
        float width_divisor = 1.0f;
        if (!col_elem) {
            col_elem = find_colgroup_element(table, c);
            if (!col_elem) continue;
            int span = table_positive_span_attr(col_elem);
            width_divisor = (float)span;
        }
        bool has_cell = false;
        for (int r = 0; r < meta->row_count && !has_cell; r++) {
            has_cell = meta->grid(r, c);
        }
        table_apply_column_constraints(lycon, meta, col_widths, c, col_elem,
                                       width_divisor, has_cell);
    }
    float fixed_table_width = 0;
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED) {
        float fixed_explicit_width = table_resolve_fixed_explicit_width(lycon, table);
        fixed_table_width = fixed_explicit_width;
      if (fixed_explicit_width > 0) {
        float content_width = table_fixed_content_width_for_columns(
            table, fixed_explicit_width, columns);
        float* explicit_col_widths = (float*)scratch_calloc(&lycon->scratch, columns * sizeof(float));
        float total_explicit = 0.0f;  int unspecified_cols = 0;
        {
            int col_idx = 0;
            table->each_column_source( [&](ViewElement* child) {
                if (col_idx >= columns) return;
                if (child->view_type == RDT_VIEW_TABLE_COLUMN_GROUP) {
                    radiant_each_table_colgroup_column(child, [&](ViewElement* col) {
                        if (col_idx >= columns) return;
                        col_idx += table_apply_fixed_column_css_width(
                            lycon, col, explicit_col_widths, col_idx, columns,
                            content_width, &total_explicit);
                    });
                } else if (child->view_type == RDT_VIEW_TABLE_COLUMN) {
                    col_idx += table_apply_fixed_column_css_width(
                        lycon, child, explicit_col_widths, col_idx, columns,
                        content_width, &total_explicit);
                }
            });
        }
        ViewTableRow* first_row = table->first_row();
        if (first_row) {
            int col = 0;
            first_row->each_cell( [&](ViewTableCell* cell) {
                if (col >= columns) return;
                col += table_apply_fixed_first_row_cell_width(
                    lycon, table, cell, explicit_col_widths, col, columns,
                    content_width, &total_explicit, &unspecified_cols);
            });
        }
        table_distribute_fixed_column_widths(
            explicit_col_widths, columns, &content_width, total_explicit, unspecified_cols);
        memcpy(col_widths, explicit_col_widths, columns * sizeof(float));
        scratch_free(&lycon->scratch, explicit_col_widths);
        table_apply_fixed_height_distribution(lycon, table, rows);
      } // end if (fixed_explicit_width > 0)
    }
    if (table->tb->table_layout != TableProp::TABLE_LAYOUT_FIXED || fixed_table_width == 0) {
    float min_table_content_width = 0;  // MCW sum for table content
    float pref_table_content_width = 0; // PCW sum for table content
    for (int i = 0; i < columns; i++) {
        min_table_content_width += meta->col_min_widths[i];
        pref_table_content_width += meta->col_max_widths[i];
    }
    float total_percent_col_width =
        table_sum_span_columns(meta->col_percent_widths, 0, columns, columns);
    float border_spacing_total = 0;
    if (column_spacing > 0.0f) {
        int effective_columns = table_effective_column_count(table, meta, col_widths, columns);
        border_spacing_total = effective_columns > 0
            ? (effective_columns + 1) * column_spacing : 0.0f;
    }
    float min_table_width = min_table_content_width + border_spacing_total;
    float pref_table_width = pref_table_content_width + border_spacing_total;
    for_each_table_caption(captions, [&](ViewBlock* table_caption, int ci) {
        float caption_width_contribution =
            table_measure_caption_width_contribution(lycon, table, table_caption);
        if (caption_width_contribution > pref_table_width) {
            pref_table_width = caption_width_contribution;
        }
        if (caption_width_contribution > min_table_width) {
            min_table_width = caption_width_contribution;
        }
    });


    float max_available_width = 0.0f;
    if (!has_explicit_table_width) {
        max_available_width = table_apply_auto_available_width_constraint(
            lycon, table, meta, &pref_table_width, min_table_width);
    }
    float used_table_width;
    bool direct_float_expanded_auto_width = false;
    if (has_explicit_table_width) {
        float explicit_content_area = table_explicit_content_area_for_auto_layout(
            table, meta, explicit_table_width);
        used_table_width = explicit_content_area > min_table_width ? explicit_content_area : min_table_width;
    } else {
        used_table_width = max(pref_table_width, min_table_width);
        if (has_direct_float && table->display.outer == CSS_VALUE_BLOCK &&
            max_available_width > used_table_width) {
            used_table_width = max_available_width;
            direct_float_expanded_auto_width = true;
        }
        if (total_percent_col_width >= 99.0f &&
            max_available_width > used_table_width) {
            used_table_width = max_available_width;
        }
    }
    float available_content_width = used_table_width - border_spacing_total;
    if (direct_float_expanded_auto_width) {
        available_content_width = pref_table_content_width;
    }
    bool used_percent_distribution = table_apply_percent_column_distribution(
        meta, col_widths, columns, total_percent_col_width,
        available_content_width, min_table_content_width);
    bool use_equal_distribution = table_columns_within_tolerance(meta->col_max_widths, columns, 3.0f);
    if (!used_percent_distribution &&
        use_equal_distribution && columns > 1 && !has_explicit_table_width) {
        float avg_width = used_table_width / columns;
        table_assign_columns(col_widths, columns, avg_width);
    }
    if (!used_percent_distribution) {
        table_apply_auto_column_width_distribution(
            meta, col_widths, columns, available_content_width,
            min_table_content_width, pref_table_content_width);
    }
    } // End of auto layout algorithm guard
    float table_padding_horizontal = 0.0f;
    float table_width = table_prepare_final_padding_box_width(
        table, meta, col_widths, columns, &table_padding_horizontal);
    if (table->tb->table_layout == TableProp::TABLE_LAYOUT_FIXED && fixed_table_width > 0) {
        float css_padding_box = table_fixed_css_padding_box_width(table, fixed_table_width);
        if (css_padding_box > table_width) {
            table_width = css_padding_box;
        }
    }
    else if (explicit_table_width > 0 && table->tb->border_collapse) {
        float bc_outer_half = meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f;
        float bc_content_area = explicit_table_width - bc_outer_half;
        if (bc_content_area > table_width) {
            table_width = bc_content_area;
        }
    }
    table_apply_minmax_width_constraints(
        table, meta, col_widths, columns, &table_width, table_padding_horizontal);
    float* col_x_positions = (float*)scratch_calloc(&lycon->scratch, (columns + 1) * sizeof(float));
    float table_padding_left = 0;
    if (!table->tb->border_collapse && table->bound && table->boundary_mut()->padding.left >= 0) {
        table_padding_left = table->boundary()->padding.left;
    }
    float table_border_left = 0;
    if (table->bound && table->boundary_mut()->border && table->boundary_mut()->border->width.left > 0) {
        if (table->tb->border_collapse) {
            table_border_left = table->boundary()->border->width.left / 2.0f;
        } else {
            table_border_left = table->boundary()->border->width.left;
        }
    }
    col_x_positions[0] = table_border_left + table_padding_left;
    if (column_spacing > 0.0f) {
        col_x_positions[0] += column_spacing;
    }
    // CSS 2.1 Column Position Calculation (Section 17.5)
    int effective_columns = table_effective_column_count(table, meta, col_widths, columns);
    if (table->tb->border_collapse) {
        for (int i = 1; i <= columns; i++) {
            col_x_positions[i] = i <= effective_columns
                ? col_x_positions[i-1] + col_widths[i-1] : col_x_positions[0];
        }
    } else {
        for (int i = 1; i <= columns; i++) {
            col_x_positions[i] = i <= effective_columns
                ? col_x_positions[i-1] + col_widths[i-1] : col_x_positions[0];
            if (column_spacing > 0.0f && i <= effective_columns) {
                // CSS 2.1: Separate borders - add border-spacing between columns
                col_x_positions[i] += column_spacing;
            }
        }
    }
    float current_y = top_caption_height;
    float table_border_top = 0;
    if (table->tb->border_collapse) {
        float collapsed_top = meta->collapsed_border_top;
        if (collapsed_top <= 0 && table->bound && table->boundary_mut()->border) {
            collapsed_top = table->boundary()->border->width.top;
        }
        if (collapsed_top > 0) {
            table_border_top = collapsed_top / 2.0f;
            current_y += table_border_top;
        }
    } else if (table->bound && table->boundary_mut()->border && table->boundary_mut()->border->width.top > 0) {
        table_border_top = table->boundary()->border->width.top;
        current_y += table_border_top;
    }
    float table_padding_top = 0;
    if (!table->tb->border_collapse && table->bound && table->boundary_mut()->padding.top >= 0) {
        table_padding_top = table->boundary()->padding.top;
        current_y += table_padding_top;
    }
    if (row_spacing > 0.0f) {
        current_y += row_spacing;
    }
    float content_area_top_y = current_y;
    // CSS 2.1 §17.4: captions use the wrapper's content width as containing block.
    float table_border_h = 0;
    if (table->tb->border_collapse) {
        table_border_h = meta->collapsed_border_left / 2.0f + meta->collapsed_border_right / 2.0f;
    } else if (table->bound && table->boundary_mut()->border) {
        table_border_h = layout_box_metrics(table).border_h;
    }
    float wrapper_content_width = table_width + table_border_h;
    // CSS Tables: position only captions whose own caption-side is top.
    if (top_captions->length > 0) {
        top_caption_height = table_position_caption_stack(
            lycon, table, top_captions, 0.0f, table_width, wrapper_content_width,
            TABLE_CAPTION_WIDTH_REFERENCE_ADJUSTED_CAP);
        caption_height = top_caption_height;
        current_y = caption_height + table_border_top + table_padding_top;
        if (row_spacing > 0.0f) {
            current_y += row_spacing;
        }
        content_area_top_y = current_y;
    }
    int global_row_index = 0;
    // CSS 2.1 Section 17.2: Visual ordering of row groups
    // Only the FIRST table-header-group acts as header (rendered at top).
    // Only the FIRST table-footer-group acts as footer (rendered at bottom).
    TableOrderedRowElements ordered_rows = table_collect_ordered_row_elements(table);
    ArrayList* body_groups = ordered_rows.body_groups;
    ArrayList* ordered_elements = ordered_rows.ordered_elements;
    log_info("%s Row group ordering: header=%s, %d body groups, footer=%s (total %d)", table->source_loc(),
              ordered_rows.header_group ? "yes" : "no", body_groups->length,
              ordered_rows.footer_group ? "yes" : "no", ordered_elements->length);
    for (int _i = 0; _i < ordered_elements->length; _i++) {
        ViewBlock* child = table_array_view_block(ordered_elements, _i);
        log_info("%s Processing ordered element %d: view_type=%d", table->source_loc(), _i, child->view_type);
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            table_position_row_group_box(
                table, meta, child, col_widths, col_x_positions, columns,
                has_direct_float, &current_y);
            float group_start_y = current_y;
            float group_content_end_y = group_start_y;
            // CSS 2.1 §17.5.3: Check row group height properties
            // Note: CSS 2.1 says min-height/max-height on row groups is undefined.
            bool group_has_percent_height = false;
            float explicit_group_height = table_resolve_row_group_explicit_height(
                lycon, child, &group_has_percent_height);
            group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
                // CSS 2.1 §17.5.3: If row group has percentage height, mark all its rows
                if (group_has_percent_height && global_row_index < meta->row_count) {
                    meta->row_has_percent_height[global_row_index] = true;
                }
                table_layout_flow_row(
                    lycon, table, meta, trow, row, true, group_start_y, child->width,
                    has_direct_float, col_widths, col_x_positions, columns,
                    &global_row_index, &current_y, row_spacing, &group_content_end_y);
            });
            // Set row group dimensions (relative to table) - preserve our calculated positioning
            child->height = group_content_end_y - group_start_y;
            // CSS 2.1 §17.5.3: height on row groups specifies minimum height
            table_apply_row_group_min_height(
                lycon, table, meta, group, child, explicit_group_height, &current_y);
        }
        else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);
            if (!table_layout_flow_row(
                    lycon, table, meta, trow, child, false, 0.0f, table_width,
                    has_direct_float, col_widths, col_x_positions, columns,
                    &global_row_index, &current_y, row_spacing, nullptr)) {
                continue;
            }
        }
    }  // End of ordered elements loop
    // NOTE: direct_rows are now processed in the main loop above as part of ordered_elements
    // Must happen after single-row cells establish baseline heights
    distribute_rowspan_heights(table, meta);
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            group->each_row_with_block( [&](ViewTableRow* trow, ViewBlock* row) {
                table_apply_rowspan_distributed_height(
                    lycon, table, meta, trow, row, "row");
            });
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            ViewTableRow* trow = lam::view_require<RDT_VIEW_TABLE_ROW>(child);
            table_apply_rowspan_distributed_height(
                lycon, table, meta, trow, child, "direct row");
        }
    });
    current_y = reflow_table_rows_from_metadata(
        lycon, table, meta, ordered_elements, content_area_top_y);
    update_rowspan_cell_heights(table, meta);
    float final_table_height = current_y;
    // CSS 2.1 Section 17.5.3: Handle explicit table height
    float explicit_css_height = 0;
    if (table->node_type == DOM_NODE_ELEMENT) {
        DomElement* dom_elem = table->as_element();
        if (dom_elem->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value) {
                // CRITICAL: Use the TABLE's font-size for resolving em units in height,
                // not the cell's font-size which may have polluted lycon->font.current_font_size.
                // CSS 2.1: "height: 4em" on the table uses the table's computed font-size.
                float saved_font_size = lycon->font.current_font_size;
                lycon->font.current_font_size = table_font_size;
                float resolved_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                lycon->font.current_font_size = saved_font_size;  // restore
                if (resolved_height > 0) {
                    explicit_css_height = resolved_height;
                }
            }
        }
    }
    if (explicit_css_height <= 0 && table->blk && table->block_mut()->given_height > 0) {
        explicit_css_height = table->block()->given_height;
    }
    float constrained_css_height = layout_clamp_min_max_axis(table, explicit_css_height, false);
    if (constrained_css_height != explicit_css_height) {
        explicit_css_height = constrained_css_height;
    }
    float min_content_height = current_y;
    BoxMetrics table_box = layout_box_metrics(table);
    float table_padding_vert = (table_box.padding.top >= 0.0f ? table_box.padding.top : 0.0f) +
        (table_box.padding.bottom >= 0.0f ? table_box.padding.bottom : 0.0f);
    float table_border_vert = 0;
    float table_spacing_vert = 0;
    if (table->bound && table->boundary_mut()->border) {
        // collapsed borders are shared with cells; only half of each outer edge counts.
        if (table->tb->border_collapse) {
            float collapsed_top = meta->collapsed_border_top > 0.0f
                ? meta->collapsed_border_top : table_box.border.top;
            float collapsed_bottom = meta->collapsed_border_bottom > 0.0f
                ? meta->collapsed_border_bottom : table_box.border.bottom;
            table_border_vert = (collapsed_top + collapsed_bottom) / 2.0f;
        } else {
            table_border_vert = table_box.border_v;
        }
    }
    if (row_spacing > 0.0f) {
        table_spacing_vert = 2 * row_spacing;  // Top and bottom edge spacing
    }
    float content_only_height = min_content_height - table_padding_vert;  // current_y includes top padding
    // CSS 2.1 §17.5.3: Extra height is distributed to body rows only, not header/footer
    if (explicit_css_height > 0 && meta->row_count > 0) {
        // CSS 2.1 §17.6.2: In border-collapse, CSS height is border-box — subtract border.
        // CSS 2.1 §10.2: In separate borders, CSS height is content-box — border is additional.
        // Exception: box-sizing:border-box makes height border-box.
        bool auto_height_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        float available_for_content = auto_height_is_border_box
            ? explicit_css_height - table_border_vert
            : explicit_css_height;
        // content-box height excludes table padding; border-box height includes it.
        float height_in_grid = content_only_height +
            (auto_height_is_border_box ? table_padding_vert : 0.0f);
        float extra_height = available_for_content - (height_in_grid + table_spacing_vert);


        if (extra_height > 0) {
            // CSS 2.1 table captions are outside the table grid box. The table
            // before or after it, so they must not consume row height.
            TableHeightSectionSummary section_summary =
                table_collect_height_section_summary(table, meta);
            float non_body_grid_height = section_summary.non_body_grid_height;
            float body_natural_height = section_summary.body_natural_height;
            int body_row_count = section_summary.body_row_count;
            int section_count = section_summary.section_count;  // Count row-group sections for grid spacing
            float distributed_height_delta = 0.0f;
            int within_group_boundaries = meta->row_count - section_count;
            if (within_group_boundaries < 0) within_group_boundaries = 0;
            int edge_spacing_count = section_count > 0 ? section_count + 1 : 0;
            float total_spacing = (edge_spacing_count + within_group_boundaries) * row_spacing;
                float padding_in_height = auto_height_is_border_box ? table_padding_vert : 0.0f;
                float extra_for_body = available_for_content - padding_in_height - total_spacing -
                                non_body_grid_height - body_natural_height;


            if (extra_for_body > 0 && body_row_count > 0) {
                // CSS 2.1 §17.5.3: Rows with percentage heights compute to auto and should
                // not receive extra height. Only distribute to rows without percentage heights.
                int eligible_row_count = 0;
                float eligible_height_total = 0.0f;
                table->each_body_row( [&](ViewTableRowGroup* group, ViewTableRow* trow) {
                    (void)group;
                    int row_idx = table_row_metadata_index_from_row(trow, -1);
                    if (row_idx >= 0 && row_idx < meta->row_count) {
                        // Only count rows that don't have percentage height
                        if (!meta->row_has_percent_height[row_idx]) {
                            eligible_row_count++;
                            if (meta->row_heights[row_idx] > 0.0f) {
                                eligible_height_total += meta->row_heights[row_idx];
                            }
                        }
                    }
                });
                if (eligible_row_count > 0) {
                    distributed_height_delta = extra_for_body;
                    table->each_body_row( [&](ViewTableRowGroup* group, ViewTableRow* trow) {
                        (void)group;
                        int row_idx = table_row_metadata_index_from_row(trow, -1);
                        if (row_idx < 0 || row_idx >= meta->row_count) return;
                        if (meta->row_has_percent_height[row_idx]) {
                            return;
                        }
                        table_apply_explicit_height_row_extra(
                            meta, row_idx, extra_for_body, eligible_row_count,
                            eligible_height_total);
                    });
                }
                table_recalculate_row_y_positions(
                    table, meta, table_border_top + table_padding_top + top_caption_height + row_spacing);
            } else if (extra_for_body > 0 && body_row_count == 0 && meta->row_count > 0) {
                // CSS Tables 3: no tbody rows exist — distribute extra height to all
                // Use direct index-based iteration over meta->row_heights to avoid
                // Also: do NOT exclude percent-height rows here — when there are no
                // body rows, ALL rows should participate in filling the table height.
                int eligible_row_count = meta->row_count;  // all rows are eligible
                if (eligible_row_count > 0) {
                    distributed_height_delta = extra_for_body;
                    float eligible_height_total = 0.0f;
                    for (int r = 0; r < meta->row_count; r++) {
                        if (meta->row_heights[r] > 0.0f) {
                            eligible_height_total += meta->row_heights[r];
                        }
                    }
                    for (int r = 0; r < meta->row_count; r++) {
                        table_apply_explicit_height_row_extra(
                            meta, r, extra_for_body, eligible_row_count,
                            eligible_height_total);
                    }
                    table_recalculate_row_y_positions(
                        table, meta, table_border_top + table_padding_top + top_caption_height + row_spacing);
                }
            }
            table_update_row_views_from_metadata(lycon, table, meta);
            current_y += distributed_height_delta;
            final_table_height = current_y;
            // so we must reposition groups sequentially and recalculate both row-relative
            table_reposition_row_groups_from_metadata(table, meta);
            // pass, so rowspanning cell boxes must be refreshed from the final rows.
            update_rowspan_cell_heights(table, meta);
        }
    }
    // CSS 2.1 §17.2.1: Column/column-group elements span only the table row area,
    // not including captions, padding, or border-spacing at the table edges.
    float row_area_height = final_table_height - content_area_top_y;
    // CSS 2.1 §17.6.2: Padding on table elements is ignored in border-collapse mode
    float table_padding_bottom = 0;
    if (!table->tb->border_collapse && table->bound && table->boundary_mut()->padding.bottom >= 0) {
        table_padding_bottom = table->boundary()->padding.bottom;
        final_table_height += table_padding_bottom;
    }
    if (row_spacing > 0.0f) {
        final_table_height += row_spacing;
    }
    // Position captions at bottom if caption-side is bottom (CSS 2.1 Section 17.4.1)
    if (bottom_captions->length > 0) {
        float total_bottom_caption_height = table_position_caption_stack(
            lycon, table, bottom_captions, final_table_height, table_width, wrapper_content_width,
            TABLE_CAPTION_WIDTH_REFERENCE_WRAPPER);
        final_table_height += total_bottom_caption_height;
    }
    // CSS 2.1 Section 17.5.3: If the table has an explicit height, use it
    // Note: The height distribution to rows was already done above (around line 4731)
    // Here we just ensure final_table_height respects the explicit height constraint.
    // final_table_height includes border_top (via current_y) but NOT border_bottom
    // For border-box: explicit_css_height is border-box — subtract border_bottom for comparison.
    // For content-box: explicit_css_height is content — add border_top + padding for comparison.
    if (explicit_css_height > 0) {
        float css_height_comparable = explicit_css_height;
        bool height_css_is_border_box = table->tb->border_collapse ||
            layout_uses_border_box(table);
        if (height_css_is_border_box) {
            if (table->tb->border_collapse) {
                // final geometry adds the collapsed bottom half-border after
                // row sizing, so exclude it from the border-box comparison.
                css_height_comparable -= meta->collapsed_border_bottom / 2.0f;
            } else if (table->bound && table->boundary_mut()->border) {
                css_height_comparable -= table->boundary()->border->width.bottom;
            }
        } else {
            if (table->bound) {
                if (table->boundary()->border)
                    css_height_comparable += table->boundary()->border->width.top;
                if (table->boundary()->padding.top >= 0)
                    css_height_comparable += table->boundary()->padding.top;
                if (table->boundary()->padding.bottom >= 0)
                    css_height_comparable += table->boundary()->padding.bottom;
            }
        }
        if (css_height_comparable > final_table_height) {
            final_table_height = css_height_comparable;
        }
    }
    // This handles CSS tables where display:table-header-group contains non-row content.
    if (meta->row_count == 0 && explicit_css_height > 0) {
        float bottom_overhead = table_padding_bottom;
        if (row_spacing > 0.0f) {
            bottom_overhead += row_spacing;
        }
        float available_for_groups = final_table_height - content_area_top_y - bottom_overhead;
        if (available_for_groups > 0) {
            int row_group_count = 0;
            table->each_row_group( [&](ViewTableRowGroup* group, ViewBlock* child) {
                (void)group; (void)child;
                row_group_count++;
            });
            if (row_group_count > 0) {
                float height_per_group = available_for_groups / row_group_count;
                table->each_row_group( [&](ViewTableRowGroup* group, ViewBlock* child) {
                    (void)group;
                    if (height_per_group > child->height) {
                        child->height = height_per_group;
                    }
                });
            }
        }
    }
    // CRITICAL FIX: Handle table border dimensions correctly for each border model
    float table_border_width = 0;
    float table_border_height = 0;
    if (table->tb->border_collapse) {
        // Border-collapse: CSS 2.1 Section 17.6.2
        float collapsed_left = meta->collapsed_border_left;
        float collapsed_right = meta->collapsed_border_right;
        float collapsed_bottom = meta->collapsed_border_bottom;
        // Width: table_border_left (half) is NOT in table_width, so add both halves
        table_border_width = collapsed_left / 2.0f + collapsed_right / 2.0f;
        // so only add half_bottom to avoid double-counting half_top.
        // and only border_bottom is added at the end.
        table_border_height = collapsed_bottom / 2.0f;
    } else if (table->bound && table->boundary_mut()->border) {
        // so only add border_bottom to avoid double-counting border_top.
        table_border_width = layout_box_metrics(table).border_h;
        table_border_height = table->boundary()->border->width.bottom;
    }
    table->width = table_width + table_border_width;
    table->height = final_table_height + table_border_height;
    table->content_width = table_width;  // Content area excludes border
    table->content_height = final_table_height - table_border_top;
    // CSS Tables 3: Table dimensions = max(styled_size, content_size).
    // max-width/max-height only affect the styled height used for row distribution
    // (already applied above at the explicit_css_height level). They must NOT shrink
    // below the natural content. Only min-width/min-height act as a floor here.
    bool table_is_content_box = !layout_uses_border_box(table);
    if (table_is_content_box && table->bound && !table->tb->border_collapse) {
        BoxMetrics table_box = layout_box_metrics(table);
        float pb_w = table_box.pad_border_h;
        float pb_h = table_box.pad_border_v;
        float content_w = max(table->width - pb_w, 0.0f);
        float content_h = max(table->height - pb_h, 0.0f);
        // Only apply min as a floor (not max)
        content_w = layout_floor_min_axis(table, content_w, true);
        content_h = layout_floor_min_axis(table, content_h, false);
        table->width = content_w + pb_w;
        table->height = content_h + pb_h;
    } else {
        // Border-box: only apply min as a floor
        if (table->blk) {
            table->width = layout_floor_min_axis(table, table->width, true);
            table->height = layout_floor_min_axis(table, table->height, false);
            // CSS Box Model: In border-box, the box width cannot be smaller than padding+border
            if (layout_uses_border_box(table) && table->bound) {
                BoxMetrics table_box = layout_box_metrics(table);
                float pad_border_w = table_box.pad_border_h;
                float pad_border_h = table_box.pad_border_v;
                if (table->width < pad_border_w) table->width = pad_border_w;
                if (table->height < pad_border_h) table->height = pad_border_h;
            }
        }
    }
    // CRITICAL: Also set ViewBlock height for block layout system integration
    lam::view_require_block(table)->height = table->height;
    // CSS 2.1 §17.5.1: Set dimensions for column and column group elements
    // Column elements span the table row area only (not including captions)
    layout_column_elements(table, col_widths, col_x_positions, meta, columns,
                           (float)row_area_height, (float)content_area_top_y);
    arraylist_free(body_groups);
    arraylist_free(ordered_elements);
    table_metadata_destroy(meta);
    scratch_free(&lycon->scratch, col_x_positions);
    #undef GRID
}

bool is_table_internal_display(CssEnum display) {
    return display == CSS_VALUE_TABLE_CELL ||
           display == CSS_VALUE_TABLE_ROW ||
           display == CSS_VALUE_TABLE_ROW_GROUP ||
           display == CSS_VALUE_TABLE_HEADER_GROUP ||
           display == CSS_VALUE_TABLE_FOOTER_GROUP ||
           display == CSS_VALUE_TABLE_COLUMN ||
           display == CSS_VALUE_TABLE_COLUMN_GROUP ||
           display == CSS_VALUE_TABLE_CAPTION;
}

bool wrap_orphaned_table_children(LayoutContext* lycon, DomElement* parent) {
    if (!lycon || !parent || !parent->first_child) return false;
    bool has_table_internal = false;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DisplayValue child_display = resolve_display_value((void*)child);
        if (is_table_internal_display(child_display.inner)) {
            has_table_internal = true;
            break;
        }
    }
    if (!has_table_internal) {
        return false;
    }
    DomNode* child = parent->first_child;
    bool wrapped_any = false;
    while (child) {
        if (!child->is_element()) {
            child = child->next_sibling;
            continue;
        }
        DisplayValue child_display = resolve_display_value((void*)child);
        if (!is_table_internal_display(child_display.inner)) {
            child = child->next_sibling;
            continue;
        }
        DomNode* run_start = child;
        DomNode* run_end = child;
        while (run_end->next_sibling) {
            DomNode* next = run_end->next_sibling;
            if (next->is_element()) {
                DisplayValue next_display = resolve_display_value((void*)next);
                if (is_table_internal_display(next_display.inner)) {
                    run_end = next;
                } else {
                    break;
                }
            } else if (next->is_text()) {
                // CSS 2.1 §17.2.1: whitespace adjacent to table-internal boxes is
                // orphaned table-internal run even when another table-cell follows.
                if (!layout_dom_text_has_non_whitespace(next->as_text())) {
                    run_end = next;  // absorb trailing whitespace
                    DomNode* after_text = next->next_sibling;
                    while (after_text && after_text->is_text() &&
                           !layout_dom_text_has_non_whitespace(after_text->as_text())) {
                        after_text = after_text->next_sibling;
                    }
                    if (after_text && after_text->is_element()) {
                        DisplayValue after_display = resolve_display_value((void*)after_text);
                        if (is_table_internal_display(after_display.inner)) {
                            continue;
                        }
                    }
                }
                break;  // stop regardless (whitespace included or not)
            } else {
                break;
            }
        }
        // CSS 2.1 §17.2.1: All orphaned table-internal elements need an anonymous table.
        // When the run contains ONLY cells (no rows/row-groups), also create an
        // anonymous row to wrap them directly (avoiding an unnecessary extra anon-tbody
        bool has_cells = false;
        bool has_rows_or_groups = false;
        bool needs_table = false;
        for (DomNode* n = run_start; n; n = n->next_sibling) {
            if (n->is_element()) {
                DisplayValue n_display = resolve_display_value((void*)n);
                CssEnum disp = n_display.inner;
                if (disp == CSS_VALUE_TABLE_CELL) {
                    has_cells = true;
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_CAPTION) {
                    // anonymous table wrapper but must not be wrapped in a row.
                    needs_table = true;
                } else if (disp == CSS_VALUE_TABLE_ROW ||
                           disp == CSS_VALUE_TABLE_ROW_GROUP ||
                           disp == CSS_VALUE_TABLE_HEADER_GROUP ||
                           disp == CSS_VALUE_TABLE_FOOTER_GROUP) {
                    has_rows_or_groups = true;
                    needs_table = true;
                }
            }
            if (n == run_end) break;
        }
        DomElement* table_wrapper = nullptr;
        DomElement* row_wrapper = nullptr;
        if (needs_table) {
            // and table-child repair share inheritance and pool ownership.
            DisplayValue parent_display = resolve_display_value((void*)parent);
            bool parent_is_inline = (parent_display.outer == CSS_VALUE_INLINE);
            table_wrapper = create_anonymous_table_element(
                lycon, parent, CSS_VALUE_TABLE, "::anon-table");
            if (table_wrapper) {
                table_wrapper->display.outer = parent_is_inline ? CSS_VALUE_INLINE : CSS_VALUE_BLOCK;
                if (!parent->font && lycon->font.style) {
                    inherit_anonymous_table_font(lycon, table_wrapper, lycon->font.style);
                }
            }
        }
        // - cells-only: create anon-tr as sole child of anon-table
        if (has_cells && table_wrapper) {
            row_wrapper = create_anonymous_table_element(
                lycon, table_wrapper, CSS_VALUE_TABLE_ROW, "::anon-tr");
            if (row_wrapper) {
                row_wrapper->display.outer = CSS_VALUE_BLOCK;
            }
        }
        if (table_wrapper) {
            // Detach the source range once; appending each node then cannot
            DomNode* prev = run_start->prev_sibling;
            DomNode* next_after_run = run_end->next_sibling;
            if (prev) prev->next_sibling = next_after_run;
            else parent->first_child = next_after_run;
            if (next_after_run) next_after_run->prev_sibling = prev;
            else parent->last_child = prev;
            run_start->prev_sibling = nullptr;
            run_end->next_sibling = nullptr;
            // - When no mix (cells-only), all go into row_wrapper
            bool has_mix = has_cells && has_rows_or_groups;
            bool row_added_to_table = false;
            DomNode* move_node = run_start;
            while (move_node) {
                DomNode* next_to_move = move_node->next_sibling;
                bool is_last = (move_node == run_end);
                DomElement* target = table_wrapper;  // default: direct child of table
                if (row_wrapper) {
                    if (has_mix && move_node->is_element()) {
                        DisplayValue n_display = resolve_display_value((void*)move_node);
                        CssEnum disp = n_display.inner;
                        if (disp == CSS_VALUE_TABLE_ROW ||
                            disp == CSS_VALUE_TABLE_ROW_GROUP ||
                            disp == CSS_VALUE_TABLE_HEADER_GROUP ||
                            disp == CSS_VALUE_TABLE_FOOTER_GROUP) {
                            if (!row_added_to_table && row_wrapper->first_child) {
                                append_child_to_element(table_wrapper, row_wrapper);
                                row_added_to_table = true;
                            }
                            target = table_wrapper;
                        } else {
                            target = row_wrapper;
                        }
                    } else {
                        // Cells-only: all go into row_wrapper
                        target = row_wrapper;
                    }
                }
                                append_detached_table_node(target, move_node);
                if (is_last) break;
                move_node = next_to_move;
            }
            if (row_wrapper && row_wrapper->first_child && !row_added_to_table) {
                // For cells-only case or when row comes at the end
                append_child_to_element(table_wrapper, row_wrapper);
            }
            place_anonymous_table_child(parent, table_wrapper, next_after_run);
            wrapped_any = true;
            child = table_wrapper->next_sibling;
        } else {
            child = run_end->next_sibling;
        }
    }
    return wrapped_any;
}


void layout_table_content(LayoutContext* lycon, DomNode* tableNode, DisplayValue display) {
    if (!tableNode) {
        return;
    }
    // CRITICAL: Save the table's font-size BEFORE building table tree.
    // but the table's CSS properties (like height: 4em) should use the table's font-size.
    float table_font_size = 16.0f;  // default
    if (lycon->font.current_font_size > 0) {
        table_font_size = lycon->font.current_font_size;
    } else if (lycon->font.style && lycon->font.style->font_size > 0) {
        table_font_size = lycon->font.style->font_size;
    }
    // CRITICAL: Update font context before building table tree
    // Use tableNode->font directly (safe) instead of casting lycon->view to ViewTable*
    // (which may not be a ViewTable yet — it's the parent's view at this point).
    DomElement* table_element = tableNode->is_element() ? lam::dom_require<DOM_NODE_ELEMENT>(tableNode) : nullptr;
    if (table_element && table_element->font) {
        setup_font(lycon->ui_context, &lycon->font, table_element->font);
    }
    // We must allocate tb before build_table_tree accesses it.
    if (tableNode->is_element()) {
        ViewTable* vtable = lam::unsafe_view_table_storage(tableNode);
        if (vtable->role_kind() != DomElement::ROLE_TABLE) {
            vtable->ensure_table(lycon->doc->view_tree);
            vtable->tb->table_layout = TableProp::TABLE_LAYOUT_AUTO;
            vtable->tb->border_spacing_h = 0.0f;
            vtable->tb->border_spacing_v = 0.0f;
            vtable->tb->border_collapse = false;
            vtable->tb->is_annoy_tbody = 0;
            vtable->tb->is_annoy_tr = 0;
            vtable->tb->is_annoy_td = 0;
            vtable->tb->is_annoy_colgroup = 0;
            vtable->view_type = RDT_VIEW_TABLE;
        }
        lycon->view = static_cast<View*>(vtable);
    }
    ViewTable* table = build_table_tree(lycon, tableNode);
    if (!table) {
        log_error("%s Failed to build table structure", tableNode->source_loc());
        return;
    }
    if (table->tb) {
        table->tb->computed_font_size = table_font_size;
    }
    detect_anonymous_boxes(table);
    table_auto_layout(lycon, table);
    // logical-property resolution; only auto tables need axis publication.
    bool has_explicit_physical_width = table->blk &&
        table->block_mut()->given_width >= 0.0f;
    bool has_logical_block_size = table_has_explicit_physical_block_size(table);
    if (!has_explicit_physical_width && !has_logical_block_size) {
        table_publish_vertical_geometry(table);
    }
    // CRITICAL: Set advance_y to table height so finalize_block_flow works correctly
    lycon->block.advance_y = table->height;
    // CRITICAL FIX: Update max_width for inline-table elements
    // So max_width must be table->width (border-box) MINUS padding.right and border.right,
    // because finalize_block_flow will add them back.
    {
        float sub_right = 0;
        if (table->bound) {
            sub_right += table->boundary()->padding.right;
            if (table->boundary()->border)
                sub_right += table->boundary()->border->width.right;
        }
        lycon->block.max_width = table->width - sub_right;
    }
    // CRITICAL FIX: Ensure proper line state management for tables
    // Tables are block-level elements and should not participate in line layout
    lycon->line.is_line_start = true;
    // CSS Position 3 §3.4: Apply relative/sticky positioning to table sub-elements
    // Table-internal elements can be relatively positioned per CSS 2.1 §17.5.1.
    table->each_direct_block( [&](ViewBlock* child) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            table_apply_positioned_layout(lycon, child);
            ViewTableRowGroup* group = lam::view_require<RDT_VIEW_TABLE_ROW_GROUP>(child);
            group->each_row_with_block( [&](ViewTableRow* row, ViewBlock* row_block) {
                (void)row_block;
                table_apply_positioned_row(lycon, row);
            });
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            table_apply_positioned_row(lycon, lam::view_require<RDT_VIEW_TABLE_ROW>(child));
        } else {
            table_apply_positioned_layout(lycon, child);
        }
    });
    // CSS Inline 3 baseline-source:first: block parents need the table's first
    // row baseline; otherwise an inline-block containing a table falls back to
    if (table->blk) {
        float first_baseline = find_first_baseline_recursive(
            lycon, static_cast<View*>(table), 0.0f, true);
        if (first_baseline >= 0.0f) {
            table->block_mut()->first_line_baseline = first_baseline;
        }
        float last_baseline = find_last_baseline_recursive(
            lycon, static_cast<View*>(table), 0.0f, true);
        if (last_baseline >= 0.0f) {
            table->block_mut()->last_line_baseline = last_baseline;
        }
    }

}
