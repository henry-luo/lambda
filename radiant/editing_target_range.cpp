#include "event.hpp"

#include "view.hpp"
#include "../lambda/input/css/dom_element.hpp"

#include <string.h>

static uint32_t text_control_prev_offset(uint32_t offset) {
    return offset > 0 ? offset - 1 : 0;
}

static uint32_t text_control_next_offset(uint32_t offset, uint32_t limit) {
    return offset < limit ? offset + 1 : limit;
}

static uint32_t compute_text_control_target_ranges(DocState* state,
                                                   const EditingSurface* surface,
                                                   const EditingIntent* intent,
                                                   EditingTargetRange* out)
{
    if (!state || !surface || !surface->owner || !intent || !out) return 0;
    DomElement* elem = surface->owner;
    if (!tc_is_text_control(elem)) return 0;
    tc_ensure_init(elem);
    FormControlProp* form = elem->form;
    if (!form) return 0;

    switch (intent->type) {
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
        case INPUT_INTENT_SELECT_ALL:
            return 0;
        default:
            break;
    }

    uint32_t start = 0;
    uint32_t end = 0;
    form_control_get_selection(state, static_cast<View*>(elem), &start, &end, nullptr);
    uint32_t limit = form->current_value_u16_len;
    if (start > limit) start = limit;
    if (end > limit) end = limit;
    if (start > end) {
        uint32_t tmp = start;
        start = end;
        end = tmp;
    }

    if (start == end) {
        switch (intent->type) {
            case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
                start = text_control_prev_offset(start);
                break;
            case INPUT_INTENT_DELETE_CONTENT_FORWARD:
                end = text_control_next_offset(end, limit);
                break;
            case INPUT_INTENT_DELETE_WORD_BACKWARD:
                start = 0;
                break;
            case INPUT_INTENT_DELETE_WORD_FORWARD:
                end = limit;
                break;
            case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
            case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:
                start = 0;
                break;
            case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
            case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:
                end = limit;
                break;
            default:
                break;
        }
    }

    DomNode* node = static_cast<DomNode*>(elem);
    out[0].start = { node, start };
    out[0].end = { node, end };
    return 1;
}

static bool rich_composition_intent_replaces_dom_preedit(
        const EditingIntent* intent) {
    return intent &&
        (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT ||
         intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
         intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT);
}

static uint32_t compute_rich_composition_target_ranges(
        DocState* state,
        const EditingSurface* surface,
        const EditingIntent* intent,
        EditingTargetRange* out) {
    if (!state || !surface || !intent || !out ||
        !rich_composition_intent_replaces_dom_preedit(intent)) {
        return 0;
    }
    if (!state->editing.composition.active ||
        state->editing.composition.dom_preedit_len == 0) {
        if (intent->type != INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
            return 0;
        }
    }
    if (state->editing.composition.surface.kind != EDIT_SURFACE_NONE &&
        state->editing.composition.surface.owner &&
        surface->owner &&
        state->editing.composition.surface.owner != surface->owner) {
        return 0;
    }

    View* anchor_view = state->editing.composition.anchor_view;
    if (!anchor_view) return 0;
    DomNode* anchor_node = static_cast<DomNode*>(anchor_view);
    if (!anchor_node || !anchor_node->is_text()) return 0;
    DomText* text = anchor_node->as_text();
    if (!text) return 0;

    const char* old_text = text->text ? text->text : "";
    uint32_t old_len = text->length > 0
        ? (uint32_t)text->length
        : (uint32_t)strlen(old_text);
    uint32_t start = state->editing.composition.anchor_offset < 0
        ? 0
        : (uint32_t)state->editing.composition.anchor_offset;
    if (start > old_len) start = old_len;
    uint32_t end = start + state->editing.composition.dom_preedit_len;
    if (end > old_len || end < start) end = old_len;

    out[0].start = { anchor_node, dom_text_utf8_to_utf16(text, start) };
    out[0].end = { anchor_node, dom_text_utf8_to_utf16(text, end) };
    return 1;
}

static bool target_range_is_join_block_tag(uintptr_t tag_id) {
    switch (tag_id) {
        case HTM_TAG_DIV:
        case HTM_TAG_LI:
        case HTM_TAG_P:
        case HTM_TAG_PRE:
        case HTM_TAG_TD:
        case HTM_TAG_TH:
            return true;
        default:
            return false;
    }
}

static bool target_range_is_table_cell_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_TD || tag_id == HTM_TAG_TH;
}

static bool target_range_is_list_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_OL || tag_id == HTM_TAG_UL;
}

static bool target_range_join_block_pair_allowed(DomElement* prev_block,
                                                 DomElement* current_block) {
    if (!prev_block || !current_block) return false;
    bool prev_cell = target_range_is_table_cell_tag(prev_block->tag());
    bool current_cell = target_range_is_table_cell_tag(current_block->tag());
    if (!prev_cell && !current_cell) return true;
    if (!prev_cell || !current_cell) return false;
    if (prev_block->parent != current_block->parent) return false;
    return !dom_element_has_attribute(prev_block, "rowspan") &&
        !dom_element_has_attribute(current_block, "rowspan");
}

static bool target_range_table_cell_without_span(DomElement* cell) {
    return cell && target_range_is_table_cell_tag(cell->tag()) &&
        !dom_element_has_attribute(cell, "rowspan") &&
        !dom_element_has_attribute(cell, "colspan");
}

static DomElement* target_range_text_block_parent(DomText* text) {
    if (!text || !text->parent || !text->parent->is_element()) return nullptr;
    DomElement* elem = text->parent->as_element();
    while (elem && !target_range_is_join_block_tag(elem->tag())) {
        DomNode* parent = elem->parent;
        elem = parent && parent->is_element() ? parent->as_element() : nullptr;
    }
    return elem;
}

static bool target_range_is_simple_inline_tag(uintptr_t tag_id) {
    switch (tag_id) {
        case HTM_TAG_A:
        case HTM_TAG_ABBR:
        case HTM_TAG_B:
        case HTM_TAG_BDI:
        case HTM_TAG_BDO:
        case HTM_TAG_BIG:
        case HTM_TAG_CITE:
        case HTM_TAG_CODE:
        case HTM_TAG_DEL:
        case HTM_TAG_DFN:
        case HTM_TAG_EM:
        case HTM_TAG_FONT:
        case HTM_TAG_I:
        case HTM_TAG_INS:
        case HTM_TAG_KBD:
        case HTM_TAG_MARK:
        case HTM_TAG_Q:
        case HTM_TAG_S:
        case HTM_TAG_SAMP:
        case HTM_TAG_SMALL:
        case HTM_TAG_SPAN:
        case HTM_TAG_STRIKE:
        case HTM_TAG_STRONG:
        case HTM_TAG_SUB:
        case HTM_TAG_SUP:
        case HTM_TAG_TIME:
        case HTM_TAG_TT:
        case HTM_TAG_U:
        case HTM_TAG_VAR:
            return true;
        default:
            return false;
    }
}

struct TargetRangeJoinContent {
    DomText* text;
    bool direct_text;
};

static bool target_range_inline_chain_text_only(DomNode* node, DomText* text) {
    DomNode* cur = node;
    DomNode* text_node = static_cast<DomNode*>(text);
    while (cur && cur != text_node) {
        if (!cur->is_element()) return false;
        DomElement* elem = cur->as_element();
        if (!elem || !target_range_is_simple_inline_tag(elem->tag()) ||
            !elem->first_child || elem->first_child != elem->last_child) {
            return false;
        }
        cur = elem->first_child;
    }
    return cur == text_node;
}

static bool target_range_inline_fragment_node(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) return true;
    if (!node->is_element()) return false;
    DomElement* elem = node->as_element();
    if (!elem || !target_range_is_simple_inline_tag(elem->tag()) ||
        dom_element_has_attribute(elem, "contenteditable")) {
        return false;
    }
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!target_range_inline_fragment_node(child)) return false;
    }
    return true;
}

static bool target_range_block_inline_fragments(DomElement* block) {
    if (!block || !block->first_child) return false;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (!target_range_inline_fragment_node(child)) return false;
    }
    return true;
}

static bool target_range_simple_text_content(DomElement* block,
                                             DomText* text,
                                             TargetRangeJoinContent* out) {
    if (out) {
        out->text = nullptr;
        out->direct_text = false;
    }
    if (!block || !text || !block->first_child ||
        block->first_child != block->last_child) {
        return false;
    }

    DomNode* child = block->first_child;
    if (child == static_cast<DomNode*>(text)) {
        if (out) {
            out->text = text;
            out->direct_text = true;
        }
        return true;
    }
    if (!child->is_element()) return false;

    if (!target_range_inline_chain_text_only(child, text)) {
        return false;
    }
    if (out) {
        out->text = text;
        out->direct_text = false;
    }
    return true;
}

static bool target_range_collapsible_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static uint32_t target_range_trim_trailing_space(const char* text,
                                                 uint32_t length) {
    while (length > 0 &&
           target_range_collapsible_space((unsigned char)text[length - 1])) {
        length--;
    }
    return length;
}

static uint32_t target_range_leading_space_len(const char* text,
                                               uint32_t length) {
    uint32_t count = 0;
    while (count < length &&
           target_range_collapsible_space((unsigned char)text[count])) {
        count++;
    }
    return count;
}

static DomText* target_range_find_text_descendant(DomNode* node, bool last) {
    if (!node) return nullptr;
    if (node->is_text()) return node->as_text();
    if (!node->is_element()) return nullptr;

    DomElement* elem = node->as_element();
    DomText* found = nullptr;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomText* text = target_range_find_text_descendant(child, last);
        if (!text) continue;
        if (!last) return text;
        found = text;
    }
    return found;
}

static bool target_range_node_is_inside(DomNode* node, DomElement* owner) {
    DomNode* owner_node = static_cast<DomNode*>(owner);
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == owner_node) return true;
    }
    return false;
}

static bool target_range_can_cleanup_inline(DomElement* elem,
                                            const EditingSurface* surface) {
    if (!elem || !elem->parent || !elem->parent->is_element()) return false;
    if (surface && surface->owner == elem) return false;
    if (dom_element_has_attribute(elem, "contenteditable")) return false;
    if (!target_range_is_simple_inline_tag(elem->tag())) return false;
    return elem->first_child && elem->first_child == elem->last_child &&
        elem->first_child->is_text();
}

static DomText* target_range_previous_cleanup_inline_text(DomText* text,
                                                          const EditingSurface* surface) {
    if (!text || !text->parent || !text->parent->is_element()) return nullptr;
    DomNode* text_node = static_cast<DomNode*>(text);
    DomNode* prev = text_node->prev_sibling;
    if (!prev || !prev->is_element()) return nullptr;
    DomElement* elem = prev->as_element();
    if (!target_range_can_cleanup_inline(elem, surface)) return nullptr;
    return elem->first_child->as_text();
}

static bool target_range_is_atomic_delete_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_BR || tag_id == HTM_TAG_HR || tag_id == HTM_TAG_IMG;
}

static DomNode* target_range_child_at(DomElement* parent, uint32_t index) {
    if (!parent) return nullptr;
    uint32_t cur = 0;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (cur == index) return child;
        cur++;
    }
    return nullptr;
}

static bool target_range_atomic_node(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem && target_range_is_atomic_delete_tag(elem->tag());
}

static uint32_t target_range_text_byte_len(DomText* text) {
    if (!text) return 0;
    const char* data = text->text ? text->text : "";
    return text->length > 0 ? (uint32_t)text->length : (uint32_t)strlen(data);
}

static bool target_range_text_line_boundary(DomBoundary caret,
                                            bool forward,
                                            EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text()) return false;
    DomText* text = caret.node->as_text();
    if (!text) return false;

    const char* data = text->text ? text->text : "";
    uint32_t text_len = target_range_text_byte_len(text);
    uint32_t byte_offset = dom_text_utf16_to_utf8(text, caret.offset);
    uint32_t byte_boundary = forward
        ? te_line_end(data, text_len, byte_offset)
        : te_line_start(data, text_len, byte_offset);
    uint32_t utf16_boundary = dom_text_utf8_to_utf16(text, byte_boundary);
    if (forward) {
        out[0].start = caret;
        out[0].end = { caret.node, utf16_boundary };
    } else {
        out[0].start = { caret.node, utf16_boundary };
        out[0].end = caret;
    }
    return true;
}

static DomText* target_range_last_text_before_child(DomElement* parent,
                                                    DomNode* stop_child) {
    if (!parent || !stop_child) return nullptr;
    DomText* found = nullptr;
    for (DomNode* child = parent->first_child; child && child != stop_child;
         child = child->next_sibling) {
        DomText* text = target_range_find_text_descendant(child, true);
        if (text) found = text;
    }
    return found;
}

static bool target_range_whitespace_text_node(DomNode* node) {
    if (!node || !node->is_text()) return false;
    DomText* text = node->as_text();
    const char* data = text && text->text ? text->text : "";
    uint32_t len = target_range_text_byte_len(text);
    return target_range_leading_space_len(data, len) == len;
}

static bool target_range_filler_br_block(DomElement* block) {
    if (!block || !block->first_child) return false;
    uint32_t br_count = 0;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (child->is_element() && child->as_element()->tag() == HTM_TAG_BR) {
            br_count++;
            continue;
        }
        if (child->is_element() &&
            target_range_is_simple_inline_tag(child->as_element()->tag()) &&
            !dom_element_has_attribute(child->as_element(), "contenteditable") &&
            target_range_filler_br_block(child->as_element())) {
            br_count++;
            continue;
        }
        if (!target_range_whitespace_text_node(child)) return false;
    }
    return br_count == 1;
}

static bool target_range_only_whitespace_after(DomNode* node) {
    for (DomNode* cur = node ? node->next_sibling : nullptr; cur;
         cur = cur->next_sibling) {
        if (!target_range_whitespace_text_node(cur)) return false;
    }
    return true;
}

static bool target_range_backspace_atomic_whitespace(DomBoundary caret,
                                                     const EditingSurface* surface,
                                                     EditingTargetRange* out) {
    if (!out || !surface || !surface->owner || !caret.node ||
        !caret.node->is_text() || !caret.node->parent ||
        !caret.node->parent->is_element()) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* parent = caret.node->parent->as_element();
    DomNode* atomic_node = caret.node->prev_sibling;
    if (!text || !parent || !atomic_node || !atomic_node->is_element()) {
        return false;
    }
    DomElement* atomic = atomic_node->as_element();
    if (!atomic || !target_range_node_is_inside(atomic_node, surface->owner)) {
        return false;
    }
    uintptr_t tag = atomic->tag();
    if (tag != HTM_TAG_BR && tag != HTM_TAG_HR) return false;

    uint32_t atomic_idx = dom_node_child_index(atomic_node);
    if (atomic_idx == (uint32_t)-1) return false;

    const char* data = text->text ? text->text : "";
    uint32_t text_len = target_range_text_byte_len(text);
    uint32_t leading_len = target_range_leading_space_len(data, text_len);
    if (leading_len > 0 && caret.offset <= leading_len) {
        out[0].start = { static_cast<DomNode*>(parent), atomic_idx };
        out[0].end = { static_cast<DomNode*>(text), leading_len };
        return true;
    }

    if (tag != HTM_TAG_HR || caret.offset != 0) return false;
    DomNode* before_atomic = atomic_node->prev_sibling;
    if (!before_atomic) return false;
    if (before_atomic->is_text()) {
        DomText* prev_text = before_atomic->as_text();
        const char* prev_data = prev_text->text ? prev_text->text : "";
        uint32_t prev_len = target_range_text_byte_len(prev_text);
        uint32_t trimmed_len = target_range_trim_trailing_space(prev_data, prev_len);
        if (trimmed_len == prev_len) return false;
        out[0].start = { static_cast<DomNode*>(prev_text), trimmed_len };
        out[0].end = { static_cast<DomNode*>(parent), atomic_idx + 1 };
        return true;
    }
    if (before_atomic->is_element() &&
        before_atomic->as_element()->tag() == HTM_TAG_BR) {
        out[0].start = { static_cast<DomNode*>(parent), atomic_idx - 1 };
        out[0].end = { static_cast<DomNode*>(parent), atomic_idx + 1 };
        return true;
    }
    return false;
}

static bool target_range_backspace_atomic(DomBoundary caret,
                                          const EditingSurface* surface,
                                          EditingTargetRange* out) {
    if (!out || !surface || !surface->owner || !caret.node) return false;

    DomElement* parent = nullptr;
    DomNode* previous = nullptr;
    uint32_t start_idx = 0;
    if (caret.node->is_text()) {
        if (caret.offset != 0 || !caret.node->parent ||
            !caret.node->parent->is_element()) {
            return false;
        }
        parent = caret.node->parent->as_element();
        previous = caret.node->prev_sibling;
        if (!previous) return false;
        start_idx = dom_node_child_index(previous);
    } else if (caret.node->is_element()) {
        parent = caret.node->as_element();
        if (caret.offset == 0) return false;
        previous = target_range_child_at(parent, caret.offset - 1);
        start_idx = caret.offset - 1;
    }

    if (!parent || !previous || start_idx == (uint32_t)-1) return false;
    if (!target_range_atomic_node(previous)) return false;
    if (!target_range_node_is_inside(previous, surface->owner)) return false;

    out[0].start = { static_cast<DomNode*>(parent), start_idx };
    out[0].end = { static_cast<DomNode*>(parent), start_idx + 1 };
    return true;
}

static bool target_range_backspace_empty_inline(DomBoundary caret,
                                                const EditingSurface* surface,
                                                EditingTargetRange* out) {
    if (!out || !surface || !surface->owner || !caret.node) return false;
    DomText* deleted_text = nullptr;
    DomBoundary end = caret;
    bool caret_inside_deleted_text = false;

    if (caret.node->is_text()) {
        DomText* text = caret.node->as_text();
        uint32_t text_len = dom_text_utf16_length(text);
        if (caret.offset == text_len && text_len == 1) {
            deleted_text = text;
            caret_inside_deleted_text = true;
        } else if (caret.offset == 0) {
            DomText* prev_text = target_range_previous_cleanup_inline_text(
                text, surface);
            if (prev_text && dom_text_utf16_length(prev_text) == 1) {
                deleted_text = prev_text;
            }
        }
    }

    if (!deleted_text || !deleted_text->parent ||
        !deleted_text->parent->is_element()) {
        return false;
    }
    if (!target_range_node_is_inside(static_cast<DomNode*>(deleted_text),
            surface->owner)) {
        return false;
    }

    DomElement* inline_elem = deleted_text->parent->as_element();
    if (!target_range_can_cleanup_inline(inline_elem, surface)) {
        return false;
    }

    DomNode* inline_node = static_cast<DomNode*>(inline_elem);
    DomElement* parent = inline_node->parent && inline_node->parent->is_element()
        ? inline_node->parent->as_element()
        : nullptr;
    if (!parent) return false;

    uint32_t inline_idx = dom_node_child_index(inline_node);
    if (inline_idx == (uint32_t)-1) return false;
    out[0].start = { static_cast<DomNode*>(parent), inline_idx };
    if (caret_inside_deleted_text) {
        end = { static_cast<DomNode*>(parent), inline_idx + 1 };
    }
    out[0].end = end;
    return true;
}

static bool target_range_backspace_block_join(DomBoundary caret,
                                              EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text()) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_block = target_range_text_block_parent(text);
    TargetRangeJoinContent current_content;
    if (!current_block ||
        !target_range_simple_text_content(current_block, text,
            &current_content)) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = prev_node->as_element();
    if (!prev_block || !target_range_is_join_block_tag(prev_block->tag()) ||
        !target_range_join_block_pair_allowed(prev_block, current_block)) {
        return false;
    }

    DomText* prev_text = target_range_find_text_descendant(prev_node, true);
    TargetRangeJoinContent prev_content;
    if (!prev_text ||
        !target_range_simple_text_content(prev_block, prev_text,
            &prev_content)) {
        return false;
    }

    uint32_t start_offset = dom_text_utf16_length(prev_text);
    uint32_t end_offset = 0;
    bool direct_text_join =
        prev_content.direct_text && current_content.direct_text;
    if (direct_text_join &&
        prev_block->tag() != HTM_TAG_PRE && current_block->tag() != HTM_TAG_PRE) {
        const char* prev_data = prev_text->text ? prev_text->text : "";
        const char* current_data = text->text ? text->text : "";
        uint32_t prev_len = prev_text->length > 0
            ? (uint32_t)prev_text->length
            : (uint32_t)strlen(prev_data);
        uint32_t current_len = text->length > 0
            ? (uint32_t)text->length
            : (uint32_t)strlen(current_data);
        start_offset = target_range_trim_trailing_space(prev_data, prev_len);
        end_offset = target_range_leading_space_len(current_data, current_len);
        if (caret.offset > end_offset) return false;
    } else if (!direct_text_join) {
        if (caret.offset != 0) return false;
    } else if (caret.offset != 0) {
        return false;
    }

    out[0].start = {
        static_cast<DomNode*>(prev_text),
        start_offset
    };
    out[0].end = { static_cast<DomNode*>(text), end_offset };
    return true;
}

static bool target_range_backspace_inline_fragment_block_join(
        DomBoundary caret,
        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text() || caret.offset != 0) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_block = target_range_text_block_parent(text);
    if (!current_block || !target_range_block_inline_fragments(current_block)) {
        return false;
    }
    DomNode* current_node = static_cast<DomNode*>(current_block);
    if (target_range_find_text_descendant(current_node, false) != text) {
        return false;
    }

    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = prev_node->as_element();
    if (!prev_block || !target_range_is_join_block_tag(prev_block->tag()) ||
        !target_range_join_block_pair_allowed(prev_block, current_block) ||
        !target_range_block_inline_fragments(prev_block)) {
        return false;
    }

    DomText* prev_text = target_range_find_text_descendant(prev_node, true);
    if (!prev_text) return false;
    out[0].start = {
        static_cast<DomNode*>(prev_text),
        dom_text_utf16_length(prev_text)
    };
    out[0].end = { static_cast<DomNode*>(text), 0 };
    return true;
}

static bool target_range_backspace_trailing_br_block_join(
        DomBoundary caret,
        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text() || caret.offset != 0) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_block = target_range_text_block_parent(text);
    TargetRangeJoinContent current_content;
    if (!current_block ||
        !target_range_simple_text_content(current_block, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = prev_node->as_element();
    if (!prev_block || !target_range_is_join_block_tag(prev_block->tag()) ||
        !prev_block->first_child || !prev_block->first_child->is_text()) {
        return false;
    }

    DomNode* last = prev_block->last_child;
    if (!last || !last->is_element() ||
        last->as_element()->tag() != HTM_TAG_BR) {
        return false;
    }
    for (DomNode* child = prev_block->first_child->next_sibling;
         child; child = child->next_sibling) {
        if (!child->is_element() || child->as_element()->tag() != HTM_TAG_BR) {
            return false;
        }
    }

    uint32_t br_idx = dom_node_child_index(last);
    if (br_idx == (uint32_t)-1) return false;
    out[0].start = { static_cast<DomNode*>(prev_block), br_idx };
    out[0].end = { static_cast<DomNode*>(text), 0 };
    return true;
}

static bool target_range_backspace_empty_br_block_join(
        DomBoundary caret,
        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text() || caret.offset != 0) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_block = target_range_text_block_parent(text);
    TargetRangeJoinContent current_content;
    if (!current_block ||
        !target_range_simple_text_content(current_block, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = prev_node->as_element();
    if (!prev_block || !target_range_is_join_block_tag(prev_block->tag()) ||
        !target_range_join_block_pair_allowed(prev_block, current_block) ||
        !target_range_filler_br_block(prev_block)) {
        return false;
    }

    out[0].start = { static_cast<DomNode*>(prev_block), 0 };
    out[0].end = { static_cast<DomNode*>(text), 0 };
    return true;
}

static bool target_range_backspace_child_block_parent_join(
        DomBoundary caret,
        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text()) {
        return false;
    }

    DomText* text = caret.node->as_text();
    if (!text || !text->parent || !text->parent->is_element()) return false;
    DomElement* parent = text->parent->as_element();
    if (!parent || !target_range_is_join_block_tag(parent->tag())) return false;

    DomNode* text_node = static_cast<DomNode*>(text);
    DomNode* prev_node = text_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = prev_node->as_element();
    if (!prev_block || !target_range_is_join_block_tag(prev_block->tag())) {
        return false;
    }

    DomText* prev_text = target_range_find_text_descendant(prev_node, true);
    TargetRangeJoinContent prev_content;
    if (!prev_text ||
        !target_range_simple_text_content(prev_block, prev_text,
            &prev_content) ||
        !prev_content.direct_text) {
        return false;
    }

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = target_range_text_byte_len(prev_text);
    uint32_t current_len = target_range_text_byte_len(text);
    uint32_t start_offset = prev_len;
    uint32_t end_offset = 0;
    if (prev_block->tag() != HTM_TAG_PRE && parent->tag() != HTM_TAG_PRE) {
        start_offset = target_range_trim_trailing_space(prev_data, prev_len);
        end_offset = target_range_leading_space_len(current_data, current_len);
    }
    if (caret.offset > end_offset) return false;

    out[0].start = { static_cast<DomNode*>(prev_text), start_offset };
    out[0].end = { static_cast<DomNode*>(text), end_offset };
    return true;
}

static bool target_range_backspace_parent_text_child_block_join(
        DomBoundary caret,
        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text()) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_block = target_range_text_block_parent(text);
    TargetRangeJoinContent current_content;
    if (!current_block ||
        !target_range_simple_text_content(current_block, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_text()) return false;
    DomText* prev_text = prev_node->as_text();

    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? current_node->parent->as_element()
        : nullptr;
    if (!parent || !target_range_is_join_block_tag(parent->tag())) return false;

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = target_range_text_byte_len(prev_text);
    uint32_t current_len = target_range_text_byte_len(text);
    uint32_t start_offset = prev_len;
    uint32_t end_offset = 0;
    if (parent->tag() != HTM_TAG_PRE && current_block->tag() != HTM_TAG_PRE) {
        start_offset = target_range_trim_trailing_space(prev_data, prev_len);
        end_offset = target_range_leading_space_len(current_data, current_len);
    }
    if (caret.offset > end_offset) return false;

    out[0].start = { static_cast<DomNode*>(prev_text), start_offset };
    out[0].end = { static_cast<DomNode*>(text), end_offset };
    return true;
}

static bool target_range_backspace_nested_list_unwrap(DomBoundary caret,
                                                      EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text() || caret.offset != 0) {
        return false;
    }

    DomText* text = caret.node->as_text();
    DomElement* current_li = target_range_text_block_parent(text);
    if (!current_li || current_li->tag() != HTM_TAG_LI) return false;
    DomNode* current_li_node = static_cast<DomNode*>(current_li);
    if (target_range_find_text_descendant(current_li_node, false) != text) {
        return false;
    }

    DomElement* nested_list = current_li_node->parent &&
            current_li_node->parent->is_element()
        ? current_li_node->parent->as_element()
        : nullptr;
    if (!nested_list || !target_range_is_list_tag(nested_list->tag()) ||
        nested_list->first_child != current_li_node) {
        return false;
    }

    DomNode* nested_list_node = static_cast<DomNode*>(nested_list);
    DomElement* parent_li = nested_list_node->parent &&
            nested_list_node->parent->is_element()
        ? nested_list_node->parent->as_element()
        : nullptr;
    if (!parent_li || parent_li->tag() != HTM_TAG_LI ||
        !target_range_only_whitespace_after(nested_list_node) ||
        parent_li->first_child == nested_list_node) {
        return false;
    }

    DomNode* parent_li_node = static_cast<DomNode*>(parent_li);
    DomElement* outer_list = parent_li_node->parent &&
            parent_li_node->parent->is_element()
        ? parent_li_node->parent->as_element()
        : nullptr;
    if (!outer_list || !target_range_is_list_tag(outer_list->tag())) {
        return false;
    }

    DomText* prev_text = target_range_last_text_before_child(parent_li,
        nested_list_node);
    if (!prev_text) return false;

    out[0].start = {
        static_cast<DomNode*>(prev_text),
        dom_text_utf16_length(prev_text)
    };
    out[0].end = { static_cast<DomNode*>(text), 0 };
    return true;
}

static bool target_range_backspace_table_cross_row_join(DomBoundary caret,
                                                        EditingTargetRange* out) {
    if (!out || !caret.node || !caret.node->is_text()) return false;

    DomText* text = caret.node->as_text();
    DomElement* current_cell = target_range_text_block_parent(text);
    TargetRangeJoinContent current_content;
    if (!current_cell || !target_range_table_cell_without_span(current_cell) ||
        !target_range_simple_text_content(current_cell, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_cell_node = static_cast<DomNode*>(current_cell);
    DomElement* current_row = current_cell_node->parent &&
            current_cell_node->parent->is_element()
        ? current_cell_node->parent->as_element()
        : nullptr;
    if (!current_row || current_row->tag() != HTM_TAG_TR ||
        current_row->first_child != current_cell_node) {
        return false;
    }

    DomNode* prev_row_node = static_cast<DomNode*>(current_row)->prev_sibling;
    if (!prev_row_node || !prev_row_node->is_element()) return false;
    DomElement* prev_row = prev_row_node->as_element();
    if (!prev_row || prev_row->tag() != HTM_TAG_TR || !prev_row->last_child ||
        !prev_row->last_child->is_element()) {
        return false;
    }

    DomElement* prev_cell = prev_row->last_child->as_element();
    if (!target_range_table_cell_without_span(prev_cell)) return false;
    DomText* prev_text = target_range_find_text_descendant(
        static_cast<DomNode*>(prev_cell), true);
    TargetRangeJoinContent prev_content;
    if (!prev_text || !target_range_simple_text_content(prev_cell, prev_text,
            &prev_content) || !prev_content.direct_text) {
        return false;
    }

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = target_range_text_byte_len(prev_text);
    uint32_t current_len = target_range_text_byte_len(text);
    uint32_t start_offset = target_range_trim_trailing_space(prev_data, prev_len);
    uint32_t end_offset = target_range_leading_space_len(current_data, current_len);
    if (caret.offset > end_offset) return false;

    out[0].start = { static_cast<DomNode*>(prev_text), start_offset };
    out[0].end = { static_cast<DomNode*>(text), end_offset };
    return true;
}

uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap)
{
    if (!state || !surface || !intent || !out || cap < 1) return 0;
    if (editing_surface_is_text_control(surface)) {
        return compute_text_control_target_ranges(state, surface, intent, out);
    }
    if (!editing_surface_is_rich(surface)) return 0;
    uint32_t composition_count =
        compute_rich_composition_target_ranges(state, surface, intent, out);
    if (composition_count > 0) return composition_count;
    if (!state->dom_selection) return 0;

    switch (intent->type) {
        case INPUT_INTENT_HISTORY_UNDO:
        case INPUT_INTENT_HISTORY_REDO:
        case INPUT_INTENT_SELECT_ALL:
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:
            return 0;
        default:
            break;
    }

    DomSelection* sel = state->dom_selection;
    DomNode* anchor = dom_selection_anchor_node(sel);
    uint32_t anchor_off = dom_selection_anchor_offset(sel);
    DomNode* focus = dom_selection_focus_node(sel);
    uint32_t focus_off = dom_selection_focus_offset(sel);
    if (!anchor || !focus) return 0;

    DomBoundary start = { anchor, anchor_off };
    DomBoundary end = { focus, focus_off };
    DomBoundaryOrder ord = dom_boundary_compare(&start, &end);
    if (ord == DOM_BOUNDARY_AFTER) {
        DomBoundary tmp = start;
        start = end;
        end = tmp;
    }

    bool collapsed = dom_selection_is_collapsed(sel);
    if (!collapsed) {
        out[0].start = start;
        out[0].end = end;
        return 1;
    }

    switch (intent->type) {
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD: {
            if (target_range_backspace_block_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_inline_fragment_block_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_empty_br_block_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_trailing_br_block_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_child_block_parent_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_parent_text_child_block_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_nested_list_unwrap(start, out)) {
                return 1;
            }
            if (target_range_backspace_table_cross_row_join(start, out)) {
                return 1;
            }
            if (target_range_backspace_empty_inline(start, surface, out)) {
                return 1;
            }
            if (target_range_backspace_atomic_whitespace(start, surface, out)) {
                return 1;
            }
            if (target_range_backspace_atomic(start, surface, out)) {
                return 1;
            }
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_CHARACTER, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_CONTENT_FORWARD: {
            DomBoundary next = dom_boundary_move(end, DOM_MOD_CHARACTER, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        case INPUT_INTENT_DELETE_WORD_BACKWARD: {
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_WORD, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_WORD_FORWARD: {
            DomBoundary next = dom_boundary_move(end, DOM_MOD_WORD, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD: {
            if (target_range_text_line_boundary(start, false, out)) {
                return 1;
            }
            DomBoundary prev = dom_boundary_move(start, DOM_MOD_DOCUMENT, -1);
            out[0].start = prev;
            out[0].end = end;
            return 1;
        }
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD: {
            if (target_range_text_line_boundary(end, true, out)) {
                return 1;
            }
            DomBoundary next = dom_boundary_move(end, DOM_MOD_DOCUMENT, +1);
            out[0].start = start;
            out[0].end = next;
            return 1;
        }
        default:
            out[0].start = start;
            out[0].end = end;
            return 1;
    }
}
