#include "layout.hpp"
#include "render.hpp"

#include "../lib/tagged.hpp"
#include <cmath>

bool layout_view_is_abs_or_fixed(ViewBlock* block) {
    return block && block->position &&
        (block->positionp()->position == CSS_VALUE_ABSOLUTE ||
         block->positionp()->position == CSS_VALUE_FIXED);
}

bool layout_element_has_in_flow_content(DomElement* element,
                                        LayoutTextContentPredicate text_predicate,
                                        LayoutElementContentPredicate element_predicate) {
    if (!element) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (text_predicate && text_predicate(child)) return true;
            continue;
        }
        if (!child->is_element()) continue;
        if (element_predicate && !element_predicate(child->as_element())) continue;
        ViewBlock* child_block = lam::view_as_block(child->as_element());
        if (!child_block) {
            if (!layout_display_is_none(resolve_display_value(child))) return true;
            continue;
        }
        if (!layout_block_is_display_none(child_block) &&
            !layout_view_is_abs_or_fixed(child_block)) return true;
    }
    return false;
}

bool layout_element_has_direct_text_content(DomElement* element) {
    if (!element) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (layout_text_node_has_content(child)) return true;
    }
    return false;
}

static int layout_collect_flattened_item_children(
        LayoutContext* lycon, ViewBlock* container, DomNode* first_child,
        DomNode** nodes, int capacity, const LayoutFlattenedItemPolicy* policy,
        bool allow_skipped_element) {
    if (!container || !nodes || capacity <= 0) return 0;

    int count = 0;
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (policy && policy->include_text &&
                policy->include_text(child, container) &&
                count < capacity) {
                nodes[count++] = child;
            } else if (policy && policy->include_text) {
                layout_suppress_ignorable_container_text(child);
            }
            continue;
        }
        if (!child->is_element()) continue;

        DomElement* element = child->as_element();
        if (allow_skipped_element && policy && element == policy->skipped_element) continue;
        if (policy && policy->reset_styles_resolved) {
            element->set_styles_resolved(false);
        }

        DisplayValue display = resolve_display_value(child);
        if (layout_display_is_none(display)) {
            element->view_type = RDT_VIEW_NONE;
            continue;
        }
        if (display.outer == CSS_VALUE_CONTENTS) {
            if (policy && policy->initialize_contents) {
                layout_init_display_contents_view(lycon, element);
            }
            int remaining = capacity - count;
            if (remaining > 0) {
                count += layout_collect_flattened_item_children(
                    lycon, container, element->first_child, nodes + count,
                    remaining, policy, false);
            }
            continue;
        }
        if (count < capacity) nodes[count++] = child;
    }
    return count;
}

int layout_collect_flattened_item_nodes(LayoutContext* lycon,
                                        ViewBlock* container,
                                        DomNode* first_child,
                                        DomNode** nodes, int capacity,
                                        const LayoutFlattenedItemPolicy* policy) {
    return layout_collect_flattened_item_children(
        lycon, container, first_child, nodes, capacity, policy, true);
}

static int layout_count_flattened_item_children(DomNode* first_child,
                                                bool include_text) {
    int count = 0;
    for (DomNode* child = first_child; child; child = child->next_sibling) {
        if (child->is_element()) {
            DisplayValue display = resolve_display_value(child);
            if (display.outer == CSS_VALUE_CONTENTS) {
                count += layout_count_flattened_item_children(
                    child->as_element()->first_child, include_text);
            } else {
                count++;
            }
        } else if (include_text) {
            // Flex separator text can become an anonymous item after flattening.
            count++;
        }
    }
    return count;
}

int layout_count_flattened_item_nodes(ViewBlock* container, bool include_text) {
    return layout_count_flattened_item_children(
        container ? container->first_child : nullptr, include_text);
}

LayoutContainingBlock layout_containing_block_for_view(ViewBlock* block) {
    LayoutContainingBlock cb = {};
    cb.view = lam::up(block);
    if (!block) return cb;

    BoxMetrics box = layout_box_metrics(block);

    cb.border_x = 0.0f;
    cb.border_y = 0.0f;
    cb.border_width = max(block->width, 0.0f);
    cb.border_height = max(block->height, 0.0f);

    cb.padding_x = box.border.left;
    cb.padding_y = box.border.top;
    cb.padding_width = max(cb.border_width - box.border_h, 0.0f);
    cb.padding_height = max(cb.border_height - box.border_v, 0.0f);

    cb.content_x = box.border.left + box.padding.left;
    cb.content_y = box.border.top + box.padding.top;
    cb.content_width = max(cb.border_width - box.pad_border_h, 0.0f);
    cb.content_height = max(cb.border_height - box.pad_border_v, 0.0f);
    if (block->view_type == RDT_VIEW_TABLE_CELL) {
        ViewTableCell* cell = lam::view_require<RDT_VIEW_TABLE_CELL>(block);
        float collapsed_top = cell->td && cell->td->top_resolved
            ? cell->td->top_resolved->width / 2.0f : 0.0f;
        // Positioned descendants of a collapsed-border cell use the cell's
        // padding edge, which begins after the shared top half-border.
        cb.padding_y += collapsed_top;
        cb.padding_height = max(cb.padding_height - collapsed_top, 0.0f);
        cb.content_y += collapsed_top;
        cb.content_height = max(cb.content_height - collapsed_top, 0.0f);
    }

    cb.has_definite_width = block->blk && block->block_mut()->given_width >= 0.0f;
    cb.has_definite_height = block->blk && block->block_mut()->given_height >= 0.0f;
    return cb;
}

LayoutContainingBlock layout_initial_containing_block(LayoutContext* lycon) {
    LayoutContainingBlock cb = {};
    if (!lycon) return cb;

    if (lycon->ui_context) {
        cb.border_width = lycon->ui_context->viewport_width > 0
            ? lycon->ui_context->viewport_width
            : 0.0f;
        cb.border_height = lycon->ui_context->viewport_height > 0
            ? lycon->ui_context->viewport_height
            : 0.0f;
    }
    if (cb.border_width <= 0.0f) cb.border_width = lycon->width;
    if (cb.border_height <= 0.0f) cb.border_height = lycon->height;

    cb.padding_width = cb.border_width;
    cb.padding_height = cb.border_height;
    cb.content_width = cb.border_width;
    cb.content_height = cb.border_height;
    cb.has_definite_width = cb.border_width > 0.0f;
    cb.has_definite_height = cb.border_height > 0.0f;
    return cb;
}

bool layout_is_initial_containing_block(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return false;
    if (lycon && lycon->doc && lycon->doc->view_tree &&
        lycon->doc->view_tree->root == (View*)block) {
        return true;
    }
    return block->parent_view() == nullptr;
}

static void layout_resolve_percent_size_axis(LayoutContext* lycon, ViewBlock* child,
                                             LayoutAxis axis, float base, bool definite) {
    LayoutAxisRefs refs(child->block_mut(), axis);
    float percent = *refs.given_percent;
    if (isnan(percent) || !definite) return;

    float value = base * percent / 100.0f;
    float& child_given = *refs.given;
    LayoutAxisRefs context_refs(&lycon->block, axis);
    if (context_refs.given) *context_refs.given = value;
    child_given = value;
}

static void layout_resolve_percent_offset_axis(PositionProp* position, LayoutAxis axis,
                                               bool start, float base) {
    LayoutAxisRefs refs(position, axis);
    RadiantInsetSide inset = start ? refs.insets.start : refs.insets.end;
    if (!*inset.has || isnan(*inset.percent)) return;

    *inset.value = *inset.percent * base / 100.0f;
}

LayoutContainingBlock layout_absolute_containing_block(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return layout_initial_containing_block(lycon);

    bool is_icb = layout_is_initial_containing_block(lycon, block);
    if (is_icb) {
        return layout_initial_containing_block(lycon);
    }

    LayoutContainingBlock cb = layout_containing_block_for_view(block);
    // CSS Position resolves an abspos percentage inline size against the
    // containing block's used padding box, including an auto-width block.
    cb.has_definite_width = true;
    // css 2.1 §10.5: abspos percentage heights also use an auto-height
    // containing block's used padding box, rather than the replaced ratio.
    cb.has_definite_height = true;
    return cb;
}

void layout_resolve_percent_size_for_child(LayoutContext* lycon, ViewBlock* child,
    LayoutContainingBlock cb, bool use_content_box) {
    if (!lycon || !child || !child->blk) return;

    float width_base = use_content_box ? cb.content_width : cb.padding_width;
    float height_base = use_content_box ? cb.content_height : cb.padding_height;
    LayoutAxisPair<float> bases = layout_axis_pair(width_base, height_base);
    LayoutAxisPair<bool> definite = {cb.has_definite_width, cb.has_definite_height};
    for (LayoutAxis axis : layout_axes()) {
        layout_resolve_percent_size_axis(
            lycon, child, axis, bases[axis], definite[axis]);
    }
}

void layout_resolve_percent_offsets_for_child(ViewBlock* child,
    LayoutContainingBlock cb) {
    if (!child || !child->position) return;

    PositionProp* pos = child->position;

    LayoutAxisPair<float> bases = layout_axis_pair(cb.padding_width, cb.padding_height);
    for (LayoutAxis axis : layout_axes()) {
        layout_resolve_percent_offset_axis(pos, axis, true, bases[axis]);
        layout_resolve_percent_offset_axis(pos, axis, false, bases[axis]);
    }
}

ViewElement* layout_create_anonymous_text_item(LayoutContext* lycon,
        ScratchMark* scope, ViewBlock* container, DomText* text) {
    if (!lycon || !container || !text) return nullptr;
    if (text->view_type != RDT_VIEW_TEXT) set_view(lycon, RDT_VIEW_TEXT, text);
    ViewText* text_view = lam::view_require<RDT_VIEW_TEXT>(text);
    // With a scope the item is that caller's temporary; without one it lives
    // until the enclosing layout pass restores its scratch mark.
    auto scratch_item_calloc = [&](size_t size) -> void* {
        return scope ? scratch_scope_calloc(&lycon->scratch, scope, size)
                     : scratch_calloc(&lycon->scratch, size);
    };
    ViewElement* item = (ViewElement*)scratch_item_calloc(sizeof(ViewElement));
    if (!item) return nullptr;
    // CSS Flexbox §4 / Grid §6 generates an anonymous blockified item for each non-empty
    // direct text run; keeping it pass-local avoids changing the DOM/render tree.
    item->node_type = DOM_NODE_ELEMENT;
    item->view_type = RDT_VIEW_BLOCK;
    item->parent = lam::up((DomNode*)container);
    item->tag_name = lam::up("anonymous-text-item");
    item->display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW, false};
    // The item's props share its scratch lifetime, so they go when the scratch
    // is restored; the view-tree pool would keep them until the tree dies.
    item->mark_scratch_lived();
    BlockProp* blk = (BlockProp*)scratch_item_calloc(sizeof(BlockProp));
    BoundaryProp* bound = (BoundaryProp*)scratch_item_calloc(sizeof(BoundaryProp));
    if (!blk || !bound) return nullptr;
    memcpy(blk, &BLOCK_PROP_DEFAULT, sizeof(BlockProp));
    blk->text_align = lycon->block.text_align;
    blk->direction = lycon->block.direction;
    blk->white_space = container->blk
        ? container->block()->white_space : CSS_VALUE_NORMAL;
    blk->writing_mode = container->blk
        ? container->block()->writing_mode : WM_HORIZONTAL_TB;
    memcpy(bound, &BOUNDARY_PROP_DEFAULT, sizeof(BoundaryProp));
    item->blk = lam::view_prop(blk);
    item->bound = lam::view_prop(bound);
    // Borrows the text's or container's font: a scratch-lived item never
    // reaches view-tree teardown (asserted there), so the slot is never freed.
    item->font = lam::view_prop((FontProp*)(text_view->font ? (FontProp*)text_view->font : (FontProp*)container->font));
    return item;
}

LayoutAnonymousTextRun* layout_append_anonymous_text_run(LayoutContext* lycon,
        ScratchMark* scope, LayoutAnonymousTextRun* head, DomText* text,
        bool leading, bool trailing, bool whitespace) {
    // runs share the enclosing item's scratch owner (D4.5.1v4).
    LayoutAnonymousTextRun* run = (LayoutAnonymousTextRun*)(scope
        ? scratch_scope_calloc(&lycon->scratch, scope, sizeof(LayoutAnonymousTextRun))
        : scratch_calloc(&lycon->scratch, sizeof(LayoutAnonymousTextRun)));
    if (!run) return nullptr;
    run->text = lam::up(text);
    run->preserve_leading_space = leading;
    run->preserve_trailing_space = trailing;
    run->is_whitespace = whitespace;
    if (head) {
        while (head->next) head = head->next;
        head->next = lam::own(run);
    }
    return run;
}

void layout_shift_anonymous_text_runs(LayoutAnonymousTextRun* runs, float dx, float dy) {
    for (LayoutAnonymousTextRun* run = runs; run; run = run->next) {
        ViewText* text = run->text
            ? lam::view_require<RDT_VIEW_TEXT>(run->text) : nullptr;
        if (!text) continue;
        text->x += dx;
        text->y += dy;
        layout_shift_text_rects(text, dx, dy);
    }
}

radiant::SizeF layout_measure_anonymous_text(LayoutContext* lycon,
        ViewElement* item, LayoutAnonymousTextRun* runs, float width, bool intrinsic) {
    // detach retained rectangles before trial formatting: layout_text recycles
    // its input chain, and a size query must leave that chain intact (D4.1.4v5).
    struct TextSnapshot {
        DomText* node;
        DomText value;
        TextSnapshot* next;
    };
    ScratchScope scope(&lycon->scratch);
    TextSnapshot* snapshots = nullptr;
    bool complete = true;
    for (LayoutAnonymousTextRun* run = runs; run; run = run->next) {
        TextSnapshot* snapshot = scope.array_zero<TextSnapshot>(1);
        if (!snapshot) { complete = false; break; }
        snapshot->node = run->text;
        memcpy(&snapshot->value, run->text.get(), sizeof(DomText));
        snapshot->next = snapshots;
        snapshots = snapshot;
        run->text->rect = nullptr;
    }
    radiant::SizeF result = radiant::size_f(0.0f, 0.0f);
    if (complete) {
        LayoutContext saved_context = *lycon;
        float saved_x = item->x, saved_y = item->y, saved_width = item->width;
        float saved_content_width = item->content_width;
        float saved_content_height = item->content_height;
        item->x = item->y = 0.0f;
        item->width = width;
        lycon->run_mode = radiant::RunMode::ComputeSize;
        lycon->available_space.width = intrinsic
            ? (width == 0.0f ? AvailableSize::make_min_content() : AvailableSize::make_max_content())
            : AvailableSize::make_definite(width);
        layout_anonymous_text_runs(item, runs, lycon, nullptr, true, intrinsic);
        result = radiant::size_f(item->content_width, item->content_height);
        item->x = saved_x;
        item->y = saved_y;
        item->width = saved_width;
        item->content_width = saved_content_width;
        item->content_height = saved_content_height;
        // measured baselines belong to this scratch box, like its intrinsic sizes.
        radiant::layout_restore_context(lycon, saved_context);
    }
    for (TextSnapshot* snapshot = snapshots; snapshot; snapshot = snapshot->next) {
        lycon->doc->view_tree->recycle_text_rects(snapshot->node->rect);
        memcpy(snapshot->node, &snapshot->value, sizeof(DomText));
    }
    return result;
}

bool layout_text_has_collapsible_edge(DomText* text, bool trailing) {
    if (!text || white_space_preserves_space_advance(get_white_space_value(
            static_cast<DomNode*>(text)))) {
        return false;
    }
    const char* data = (const char*)text->text_data();
    return data && text->length > 0 &&
        is_space(data[trailing ? text->length - 1 : 0]);
}

static void preserve_anonymous_text_spacing(LayoutContext* lycon,
                                                  ViewText* text,
                                                  bool preserve_leading_space,
                                                  bool preserve_trailing_space) {
    if (!lycon || !text || !text->rect ||
        (!preserve_leading_space && !preserve_trailing_space)) return;
    float space_width = layout_measure_space_advance(
        lycon, font_box_handle(&lycon->font), lycon->font.style);
    if (preserve_leading_space) text->rect->width += space_width;
    if (preserve_trailing_space) {
        TextRect* last = text->rect;
        while (last->next) last = last->next;
        last->width += space_width;
    }
    adjust_text_bounds(text);
}

void layout_anonymous_text_runs(ViewElement* item, LayoutAnonymousTextRun* runs,
                                 LayoutContext* lycon, ViewBlock* prelaid_container,
                                 bool finalize, bool intrinsic) {
    if (!item || !lycon || !runs) return;
    LayoutContext saved_context = *lycon;
    lycon->view = lam::up(static_cast<View*>(item));
    lycon->block.content_width = item->width;
    lycon->block.content_height = item->height;
    lycon->block.given_width = item->width;
    lycon->block.given_height = item->height;
    lycon->block.advance_y = item->y;
    lycon->block.max_width = item->x + item->width;
    lycon->block.text_align = finalize && !intrinsic && item->blk
        ? item->blk->text_align : CSS_VALUE_LEFT;
    if (finalize) {
        // each anonymous item owns an independent inline formatting context.
        lycon->block.line_number = 0;
        lycon->block.first_line_ascender = 0.0f;
        lycon->block.last_line_ascender = 0.0f;
        lycon->block.max_width = item->x;
        lycon->block.given_height = -1.0f;
        lycon->block.establishing_element = lam::up(lam::view_as_block(item));
        lycon->block.direction = item->blk->direction;
        ViewBlock* container = layout_nearest_block_ancestor(item->parent_view());
        layout_setup_text_indent(lycon, container, intrinsic ? 0.0f : item->width);
    }
    if (item->font) setup_font(lycon->ui_context, &lycon->font, item->font);
    setup_line_height(lycon, lam::view_as_block(item));
    layout_setup_block_font_metrics(lycon);
    line_init(lycon, item->x, item->x + item->width);

    for (LayoutAnonymousTextRun* run = runs; run; run = run->next) {
        DomText* text_node = run->text;
        ViewText* text = text_node
            ? lam::view_require<RDT_VIEW_TEXT>(text_node) : nullptr;
        if (!text) continue;
        if (prelaid_container && text_node->parent == static_cast<DomNode*>(prelaid_container) &&
            !run->is_whitespace) {
            // Direct text was laid out by layout_final_flex_content; advance
            // past its existing geometry before laying out flattened runs.
            preserve_anonymous_text_spacing(
                lycon, text, run->preserve_leading_space,
                run->preserve_trailing_space);
            float text_end = text->x + text->width;
            for (TextRect* rect = text->rect; rect; rect = rect->next) {
                text_end = max(text_end, rect->x + rect->width);
            }
            lycon->line.advance_x = max(lycon->line.advance_x, text_end);
            lycon->line.is_line_start = false;
            continue;
        }
        if (run->is_whitespace && !finalize) {
            TextRect* rect = lycon->doc && lycon->doc->view_tree
                ? lycon->doc->view_tree->alloc_text_rect() : nullptr;
            if (!rect) continue;
            float space_width = layout_measure_space_advance(
                lycon, font_box_handle(&lycon->font), lycon->font.style);
            rect->x = lycon->line.advance_x;
            rect->y = item->y;
            rect->width = space_width;
            rect->height = item->height;
            rect->length = (int)text_node->length; // INT_CAST_OK: text rectangle source length
            rect->line_number = lycon->block.line_number;
            text->rect = lam::view_prop(rect);
            layout_set_view_geometry(text, rect->x, rect->y,
                                     rect->width, rect->height);
            lycon->line.advance_x += space_width;
            lycon->line.is_line_start = false;
            lycon->line.has_space = true;
            continue;
        }

        layout_text(lycon, static_cast<DomNode*>(text_node));
        if (!finalize) preserve_anonymous_text_spacing(
            lycon, text, run->preserve_leading_space,
            run->preserve_trailing_space &&
                layout_text_has_collapsible_edge(text_node, true));
    }
    if (finalize) {
        if (!lycon->line.is_line_start) {
            lycon->line.is_last_line = true;
            line_break(lycon, true);
        }
        item->content_width = max(0.0f, lycon->block.max_width - item->x);
        item->content_height = max(0.0f, lycon->block.advance_y - item->y);
        // source rectangles use container coordinates; item baselines are local.
        item->blk->first_line_baseline = lycon->block.first_line_ascender - item->y;
        item->blk->last_line_baseline = lycon->block.last_line_ascender - item->y;
    }
    radiant::layout_restore_context(lycon, saved_context);
}

static bool layout_find_contents_text_edge(DomElement* contents, bool from_start,
                                          DomNode** out_text) {
    if (out_text) *out_text = nullptr;
    if (!contents) return true;

    DomNode* child = from_start ? contents->first_child : contents->last_child;
    while (child) {
        if (child->is_text()) {
            if (layout_text_node_has_content(child)) {
                if (out_text) *out_text = child;
                return true;
            }
        } else if (child->is_element()) {
            DisplayValue display = resolve_display_value(child);
            if (display.outer != CSS_VALUE_CONTENTS) return false;
            DomNode* edge_text = nullptr;
            if (!layout_find_contents_text_edge(
                    child->as_element(), from_start, &edge_text)) {
                return false;
            }
            if (edge_text) {
                if (out_text) *out_text = edge_text;
                return true;
            }
        }
        child = from_start ? child->next_sibling : child->prev_sibling;
    }
    return true;
}

DomNode* layout_adjacent_flattened_text(DomNode* node,
                                              ViewBlock* container,
                                              bool following) {
    if (!node || !container) return nullptr;

    DomNode* current = node;
    DomNode* container_node = static_cast<DomNode*>(container);
    while (current && current != container_node) {
        DomNode* sibling = following ? current->next_sibling : current->prev_sibling;
        while (sibling) {
            if (sibling->is_text()) {
                if (layout_text_node_has_content(sibling)) return sibling;
            } else if (sibling->is_element()) {
                DisplayValue display = resolve_display_value(sibling);
                if (display.outer != CSS_VALUE_CONTENTS) return nullptr;
                DomNode* edge_text = nullptr;
                if (!layout_find_contents_text_edge(
                        sibling->as_element(), !following, &edge_text)) {
                    return nullptr;
                }
                if (edge_text) return edge_text;
            }
            sibling = following ? sibling->next_sibling : sibling->prev_sibling;
        }

        DomNode* parent = current->parent;
        if (!parent || parent == container_node || !parent->is_element()) return nullptr;
        DisplayValue parent_display = resolve_display_value(parent);
        if (parent_display.outer != CSS_VALUE_CONTENTS) return nullptr;
        current = parent;
    }
    return nullptr;
}

bool layout_contents_whitespace_is_text_separator(DomNode* text,
                                                        ViewBlock* container) {
    if (!text || !container || !text->is_text() ||
        layout_text_node_has_content(text)) return false;
    return layout_adjacent_flattened_text(text, container, false) != nullptr &&
           layout_adjacent_flattened_text(text, container, true) != nullptr;
}

bool layout_collect_flattened_text_item(DomNode* text,
                                             ViewBlock* container) {
    return layout_text_node_has_content(text) ||
        layout_contents_whitespace_is_text_separator(text, container);
}
