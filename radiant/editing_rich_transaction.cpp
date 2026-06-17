#include "editing_rich_transaction.hpp"

#include "dom_range.hpp"
#include "state_store.hpp"
#include "text_edit.hpp"
#include "view.hpp"
#include "../lambda/mark_editor.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <stdio.h>
#include <string.h>

DomText* editing_rich_find_text_descendant(DomNode* node, bool last) {
    if (!node) return nullptr;
    if (node->is_text()) return lam::dom_require_text(node);
    if (!node->is_element()) return nullptr;

    DomElement* elem = lam::dom_require_element(node);
    DomText* found = nullptr;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        DomText* text = editing_rich_find_text_descendant(child, last);
        if (!text) continue;
        if (!last) return text;
        found = text;
    }
    return found;
}

static bool rich_selection_is_host_child_range(const EditingSurface* surface,
                                               const DomSelection* selection) {
    if (!surface || !surface->owner || !selection || selection->range_count == 0 ||
        dom_selection_is_collapsed(selection) || !selection->ranges[0]) {
        return false;
    }

    DomRange* range = selection->ranges[0];
    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    if (range->start.node != owner_node || range->end.node != owner_node) {
        return false;
    }
    if (range->start.offset != 0) return false;
    return range->end.offset == dom_node_boundary_length(owner_node);
}

static bool rich_selection_single_text_range(const DomSelection* selection,
                                             DomText** out_text,
                                             uint32_t* out_start,
                                             uint32_t* out_end) {
    if (out_text) *out_text = nullptr;
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (!selection || selection->range_count == 0 || dom_selection_is_collapsed(selection) ||
        !selection->ranges[0]) {
        return false;
    }

    DomRange* range = selection->ranges[0];
    if (!range->start.node || range->start.node != range->end.node ||
        !range->start.node->is_text()) {
        return false;
    }

    DomText* text = lam::dom_require_text(range->start.node);
    uint32_t start = dom_text_utf16_to_utf8(text, range->start.offset);
    uint32_t end = dom_text_utf16_to_utf8(text, range->end.offset);
    if (end < start) {
        uint32_t tmp = start;
        start = end;
        end = tmp;
    }
    if (out_text) *out_text = text;
    if (out_start) *out_start = start;
    if (out_end) *out_end = end;
    return true;
}

static bool rich_transaction_delete_dom_range(DocState* state,
                                              DomSelection* selection) {
    if (!state || !selection || selection->range_count == 0 ||
        dom_selection_is_collapsed(selection) || !selection->ranges[0]) {
        return false;
    }
    if (selection != state->dom_selection) return false;
    const char* exc = nullptr;
    if (!state_store_delete_selection_from_document(state, &exc)) {
        log_debug("rich_transaction_delete_dom_range: delete rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    return true;
}

static bool rich_transaction_collapse_text_caret(DocState* state,
                                                 DomText* text,
                                                 uint32_t byte_offset) {
    if (!state || !text) return false;
    uint32_t caret_u16 = dom_text_utf8_to_utf16(text, byte_offset);
    DomBoundary caret = { static_cast<DomNode*>(text), caret_u16 };
    const char* exc = nullptr;
    if (!state_store_set_selection(state, &caret, &caret, &exc)) {
        log_debug("rich_transaction_collapse_text_caret: collapse rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    return true;
}

static bool rich_transaction_collapse_dom_caret(DocState* state,
                                                DomNode* node,
                                                uint32_t offset) {
    if (!state || !node) return false;
    DomBoundary caret = { node, offset };
    const char* exc = nullptr;
    if (!state_store_set_selection(state, &caret, &caret, &exc)) {
        log_debug("rich_transaction_collapse_dom_caret: collapse rejected: %s",
                  exc ? exc : "?");
        return false;
    }
    return true;
}

static bool rich_transaction_caret_from_state(DocState* state,
                                              DomText** out_text,
                                              uint32_t* out_byte_offset) {
    if (out_text) *out_text = nullptr;
    if (out_byte_offset) *out_byte_offset = 0;
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0 ||
        !state->dom_selection->ranges[0]) {
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    if (!dom_range_collapsed(range) || !range->start.node ||
        !range->start.node->is_text()) {
        return false;
    }

    DomText* text = lam::dom_require_text(range->start.node);
    if (!text) return false;
    if (out_text) *out_text = text;
    if (out_byte_offset) {
        *out_byte_offset = dom_text_utf16_to_utf8(text, range->start.offset);
    }
    return true;
}

static bool rich_transaction_is_text_deletion_intent(
        const EditingIntent* intent) {
    if (!intent) return false;
    return intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD ||
        intent->type == INPUT_INTENT_DELETE_WORD_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_WORD_FORWARD;
}

static bool rich_transaction_is_cleanup_inline_tag(uintptr_t tag_id) {
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

static bool rich_transaction_inline_wrapper_is_empty(DomElement* elem) {
    if (!elem) return false;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_text()) return false;
        DomText* text = lam::dom_require_text(child);
        if (text && text->length > 0) return false;
    }
    return true;
}

static bool rich_transaction_can_cleanup_empty_inline(
        DomElement* elem,
        const EditingSurface* surface) {
    if (!elem || !elem->parent || !elem->parent->is_element()) return false;
    if (surface && surface->owner == elem) return false;
    if (dom_element_has_attribute(elem, "contenteditable")) return false;
    if (!rich_transaction_is_cleanup_inline_tag(elem->tag())) return false;
    return rich_transaction_inline_wrapper_is_empty(elem);
}

static bool rich_transaction_remove_child_for_edit(DomElement* parent,
                                                   DomNode* child) {
    if (!parent || !child || child->parent != static_cast<DomNode*>(parent)) {
        return false;
    }

    uint32_t child_idx = dom_node_child_index(child);
    if (child_idx == (uint32_t)-1) return false;

    if (parent->native_element && parent->doc && parent->doc->input) {
        if (child_idx > 0x7fffffffu) return false;
        MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
        Item result = editor.elmt_delete_child(
            {.element = parent->native_element},
            (int)child_idx // INT_CAST_OK: MarkEditor child index API is int.
        );
        if (!result.element) {
            log_debug("rich_transaction_remove_child_for_edit: backing delete rejected");
            return false;
        }
        parent->native_element = result.element;
        if (parent->doc->input->ui_mode) {
            child->parent = nullptr;
            child->prev_sibling = nullptr;
            child->next_sibling = nullptr;
            return true;
        }
    }

    return parent->remove_child(child);
}

static bool rich_transaction_cleanup_empty_inline_after_delete(
        DocState* state,
        DomText* text,
        const EditingSurface* surface,
        DomNode** out_caret_node,
        uint32_t* out_caret_offset) {
    if (out_caret_node) *out_caret_node = nullptr;
    if (out_caret_offset) *out_caret_offset = 0;
    if (!state || !text || !text->parent || !text->parent->is_element()) {
        return false;
    }

    DomElement* inline_elem = lam::dom_require_element(text->parent);
    if (!rich_transaction_can_cleanup_empty_inline(inline_elem, surface)) {
        return false;
    }

    DomNode* inline_node = static_cast<DomNode*>(inline_elem);
    DomElement* parent = lam::dom_require_element(inline_node->parent);
    if (!parent) return false;

    uint32_t inline_idx = dom_node_child_index(inline_node);
    if (inline_idx == (uint32_t)-1) return false;

    dom_mutation_pre_remove(state, inline_node);
    if (!rich_transaction_remove_child_for_edit(parent, inline_node)) {
        return false;
    }

    if (out_caret_node) *out_caret_node = static_cast<DomNode*>(parent);
    if (out_caret_offset) *out_caret_offset = inline_idx;
    log_debug("rich_transaction_cleanup_empty_inline_after_delete: removed empty %s at index %u",
              inline_elem->node_name(), inline_idx);
    return true;
}

static bool rich_transaction_delete_dom_range_and_read_caret(
        DocState* state, DomText** out_text, uint32_t* out_byte_offset) {
    if (!state || !state->dom_selection) return false;
    if (!rich_transaction_delete_dom_range(state, state->dom_selection)) {
        return false;
    }
    return rich_transaction_caret_from_state(state, out_text, out_byte_offset);
}

static bool rich_transaction_is_join_block_tag(uintptr_t tag_id) {
    switch (tag_id) {
        case HTM_TAG_DIV:
        case HTM_TAG_P:
        case HTM_TAG_PRE:
            return true;
        default:
            return false;
    }
}

static DomElement* rich_transaction_text_block_parent(DomText* text) {
    if (!text || !text->parent || !text->parent->is_element()) return nullptr;
    DomElement* elem = lam::dom_require_element(text->parent);
    while (elem && !rich_transaction_is_join_block_tag(elem->tag())) {
        DomNode* parent = elem->parent;
        elem = parent && parent->is_element()
            ? lam::dom_require_element(parent)
            : nullptr;
    }
    return elem;
}

static DomText* rich_transaction_live_text_at_index(DomElement* parent,
                                                    int64_t child_idx,
                                                    DomText* fallback) {
    if (!parent || child_idx < 0) return fallback;
    int64_t idx = 0;
    for (DomNode* child = parent->first_child; child;
         child = child->next_sibling, idx++) {
        if (idx == child_idx) {
            return child->is_text() ? lam::dom_require_text(child) : fallback;
        }
    }
    return fallback;
}

struct RichJoinBlockContent {
    DomNode* child;
    DomText* text;
    bool direct_text;
};

static bool rich_transaction_simple_text_content(DomElement* block,
                                                 DomText* text,
                                                 RichJoinBlockContent* out) {
    if (out) {
        out->child = nullptr;
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
            out->child = child;
            out->text = text;
            out->direct_text = true;
        }
        return true;
    }
    if (!child->is_element()) return false;

    DomElement* inline_elem = lam::dom_require_element(child);
    if (!inline_elem || !rich_transaction_is_cleanup_inline_tag(inline_elem->tag()) ||
        inline_elem->first_child != static_cast<DomNode*>(text) ||
        inline_elem->last_child != static_cast<DomNode*>(text)) {
        return false;
    }
    if (out) {
        out->child = child;
        out->text = text;
        out->direct_text = false;
    }
    return true;
}

static bool rich_transaction_collapsible_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static uint32_t rich_transaction_trim_trailing_space(const char* text,
                                                     uint32_t length) {
    while (length > 0 &&
           rich_transaction_collapsible_space((unsigned char)text[length - 1])) {
        length--;
    }
    return length;
}

static uint32_t rich_transaction_leading_space_len(const char* text,
                                                   uint32_t length) {
    uint32_t count = 0;
    while (count < length &&
           rich_transaction_collapsible_space((unsigned char)text[count])) {
        count++;
    }
    return count;
}

static bool rich_transaction_item_for_child(DomNode* child, Item* out) {
    if (out) *out = ItemNull;
    if (!child || !out) return false;
    if (child->is_text()) {
        DomText* text = lam::dom_require_text(child);
        if (!text || !text->native_string) return false;
        *out = (Item){.item = s2it(text->native_string)};
        return true;
    }
    if (child->is_element()) {
        DomElement* elem = lam::dom_require_element(child);
        if (!elem || !elem->native_element) return false;
        *out = (Item){.element = elem->native_element};
        return true;
    }
    return false;
}

static bool rich_transaction_append_child_for_edit(DomElement* parent,
                                                   DomNode* child) {
    if (!parent || !child || child->parent) return false;
    if (parent->native_element && parent->doc && parent->doc->input) {
        Item child_item;
        if (!rich_transaction_item_for_child(child, &child_item)) {
            return false;
        }
        MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
        Item result = editor.elmt_append_child(
            {.element = parent->native_element},
            child_item
        );
        if (!result.element) {
            log_debug("rich_transaction_append_child_for_edit: backing append rejected");
            return false;
        }
        parent->native_element = result.element;
        if (parent->doc->input->ui_mode) {
            return true;
        }
    }
    return static_cast<DomNode*>(parent)->append_child(child);
}

static bool rich_transaction_join_previous_block(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text) return false;

    DomElement* current_block = rich_transaction_text_block_parent(text);
    RichJoinBlockContent current_content;
    if (!current_block ||
        !rich_transaction_simple_text_content(current_block, text,
            &current_content)) {
        return false;
    }
    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;

    DomElement* prev_block = lam::dom_require_element(prev_node);
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag())) {
        return false;
    }
    DomText* prev_text = editing_rich_find_text_descendant(prev_node, true);
    RichJoinBlockContent prev_content;
    if (!prev_text ||
        !rich_transaction_simple_text_content(prev_block, prev_text,
            &prev_content)) {
        return false;
    }
    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? lam::dom_require_element(current_node->parent)
        : nullptr;
    if (!parent) return false;

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = prev_text->length > 0
        ? (uint32_t)prev_text->length
        : (uint32_t)strlen(prev_data);
    uint32_t current_len = text->length > 0
        ? (uint32_t)text->length
        : (uint32_t)strlen(current_data);
    uint32_t prev_keep_len = prev_len;
    uint32_t current_skip_len = 0;
    bool direct_text_join =
        prev_content.direct_text && current_content.direct_text;
    if (direct_text_join &&
        prev_block->tag() != HTM_TAG_PRE && current_block->tag() != HTM_TAG_PRE) {
        prev_keep_len = rich_transaction_trim_trailing_space(prev_data, prev_len);
        current_skip_len = rich_transaction_leading_space_len(current_data, current_len);
    }
    if (caret_offset > current_skip_len) return false;

    if (!direct_text_join) {
        if (caret_offset != 0) return false;
        DomNode* moving_child = current_content.child;
        if (!moving_child || !current_node->remove_child(moving_child)) {
            return false;
        }
        if (!rich_transaction_append_child_for_edit(prev_block, moving_child)) {
            current_node->append_child(moving_child);
            return false;
        }
        dom_mutation_pre_remove(state, current_node);
        if (!rich_transaction_remove_child_for_edit(parent, current_node)) {
            return false;
        }
        if (!rich_transaction_collapse_text_caret(state, prev_text, prev_len)) {
            return false;
        }
        EditingSurface live_surface;
        if (editing_surface_from_target(static_cast<View*>(prev_text),
                &live_surface) && editing_surface_is_rich(&live_surface)) {
            editing_interaction_set_active_surface(state, &live_surface);
            if (log_mutation) {
                log_mutation(state, &live_surface, intent, "block-join",
                             prev_len, prev_len, prev_len, prev_len, log_user);
            }
        } else if (surface) {
            editing_interaction_set_active_surface(state, surface);
        }
        log_debug("rich_transaction_join_previous_block: moved inline child at offset %u",
                  prev_len);
        return true;
    }

    size_t joined_len =
        (size_t)prev_keep_len + (size_t)(current_len - current_skip_len);
    char* joined = (char*)mem_alloc(joined_len + 1, MEM_CAT_TEMP);
    if (!joined) return false;
    if (prev_keep_len > 0) memcpy(joined, prev_data, prev_keep_len);
    if (current_len > current_skip_len) {
        memcpy(joined + prev_keep_len, current_data + current_skip_len,
               current_len - current_skip_len);
    }
    joined[joined_len] = '\0';

    int64_t prev_idx = dom_text_get_child_index(prev_text);
    bool updated = dom_text_set_content(prev_text, joined);
    mem_free(joined);
    if (!updated) return false;

    DomText* live_prev_text =
        rich_transaction_live_text_at_index(prev_block, prev_idx, prev_text);

    dom_mutation_pre_remove(state, current_node);
    if (!rich_transaction_remove_child_for_edit(parent, current_node)) {
        return false;
    }

    if (!rich_transaction_collapse_text_caret(state, live_prev_text,
            prev_keep_len)) {
        return false;
    }
    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(live_prev_text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent, "block-join",
                         prev_keep_len, prev_len + current_skip_len,
                         prev_keep_len, prev_keep_len, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_previous_block: joined %u/%u + %u/%u bytes",
              prev_keep_len, prev_len, current_len - current_skip_len,
              current_len);
    return true;
}

struct RichDefaultSelectionTarget {
    DomText* text;
    uint32_t start;
    uint32_t end;
    bool mutation_complete;
};

static void rich_default_selection_target_clear(
        RichDefaultSelectionTarget* target) {
    if (!target) return;
    target->text = nullptr;
    target->start = 0;
    target->end = 0;
    target->mutation_complete = false;
}

static bool rich_transaction_resolve_selection_target(
        DocState* state,
        const EditingSurface* fallback_surface,
        const EditingIntent* intent,
        DomSelection* selection,
        RichDefaultSelectionTarget* out) {
    rich_default_selection_target_clear(out);
    if (!state || !intent || !selection || !out ||
        selection->range_count == 0 || !selection->ranges[0] ||
        dom_selection_is_collapsed(selection)) {
        return false;
    }

    if (rich_selection_single_text_range(selection, &out->text,
            &out->start, &out->end)) {
        return true;
    }

    if (intent->type == INPUT_INTENT_DELETE_BY_CUT ||
        intent->type == INPUT_INTENT_DELETE_BY_DRAG) {
        out->mutation_complete =
            rich_transaction_delete_dom_range(state, selection);
        return out->mutation_complete;
    }

    if (intent->type == INPUT_INTENT_INSERT_FROM_PASTE ||
        intent->type == INPUT_INTENT_INSERT_FROM_DROP) {
        if (!rich_transaction_delete_dom_range_and_read_caret(state,
                &out->text, &out->start)) {
            return false;
        }
        out->end = out->start;
        return true;
    }

    if (!fallback_surface || !editing_surface_is_rich(fallback_surface) ||
        !rich_selection_is_host_child_range(fallback_surface, selection)) {
        return false;
    }

    out->text = editing_rich_find_text_descendant(
        static_cast<DomNode*>(fallback_surface->owner), true);
    if (!out->text) return false;
    const char* selected_text = out->text->text ? out->text->text : "";
    out->start = out->text->length > 0
        ? (uint32_t)out->text->length
        : (uint32_t)strlen(selected_text);
    out->end = out->start;
    return true;
}

bool editing_rich_is_composition_intent(const EditingIntent* intent) {
    return intent &&
        (intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT ||
         intent->type == INPUT_INTENT_INSERT_FROM_COMPOSITION ||
         intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT);
}

static uint32_t rich_utf8_byte_offset_for_codepoints(const char* text,
                                                     uint32_t len,
                                                     uint32_t codepoints) {
    if (!text || codepoints == 0) return 0;
    uint32_t seen = 0;
    uint32_t i = 0;
    while (i < len && seen < codepoints) {
        unsigned char b = (unsigned char)text[i];
        uint32_t step = 1;
        if (b >= 0xF0) step = 4;
        else if (b >= 0xE0) step = 3;
        else if (b >= 0xC0) step = 2;
        if (i + step > len) step = 1;
        i += step;
        seen++;
    }
    return i > len ? len : i;
}

static bool rich_text_default_composition_range(DocState* state,
                                                const EditingIntent* intent,
                                                DomText** out_text,
                                                uint32_t* out_start,
                                                uint32_t* out_end) {
    if (out_text) *out_text = nullptr;
    if (out_start) *out_start = 0;
    if (out_end) *out_end = 0;
    if (!state || !intent || !editing_rich_is_composition_intent(intent)) {
        return false;
    }
    if (!state->editing.composition.active) return false;
    View* anchor_view = state->editing.composition.anchor_view;
    if (!anchor_view || !anchor_view->is_text()) return false;

    DomText* text = lam::dom_require_text(static_cast<DomNode*>(anchor_view));
    if (!text) return false;
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

    if (out_text) *out_text = text;
    if (out_start) *out_start = start;
    if (out_end) *out_end = end;
    return true;
}

static const char* rich_text_default_mutation_operation(
        const EditingIntent* intent) {
    if (!intent) return "replace";
    switch (intent->type) {
        case INPUT_INTENT_INSERT_TEXT:
            return "insert";
        case INPUT_INTENT_INSERT_REPLACEMENT_TEXT:
            return "replace";
        case INPUT_INTENT_INSERT_COMPOSITION_TEXT:
        case INPUT_INTENT_INSERT_FROM_COMPOSITION:
        case INPUT_INTENT_DELETE_COMPOSITION_TEXT:
            return "composition";
        case INPUT_INTENT_INSERT_FROM_PASTE:
            return "paste";
        case INPUT_INTENT_INSERT_FROM_DROP:
            return "drop";
        case INPUT_INTENT_INSERT_PARAGRAPH:
        case INPUT_INTENT_INSERT_LINE_BREAK:
            return "linebreak";
        case INPUT_INTENT_DELETE_BY_DRAG:
            return "drag";
        case INPUT_INTENT_DELETE_BY_CUT:
        case INPUT_INTENT_DELETE_CONTENT_BACKWARD:
        case INPUT_INTENT_DELETE_CONTENT_FORWARD:
        case INPUT_INTENT_DELETE_WORD_BACKWARD:
        case INPUT_INTENT_DELETE_WORD_FORWARD:
            return "delete";
        default:
            return "replace";
    }
}

static const char* rich_format_tag_name(const EditingIntent* intent) {
    if (!intent) return nullptr;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_BOLD:
            return "b";
        case INPUT_INTENT_FORMAT_ITALIC:
            return "i";
        case INPUT_INTENT_FORMAT_UNDERLINE:
            return "u";
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:
            return "s";
        case INPUT_INTENT_FORMAT_SUBSCRIPT:
            return "sub";
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:
            return "sup";
        default:
            return nullptr;
    }
}

static bool rich_format_tag_matches(const EditingIntent* intent,
                                    uintptr_t tag) {
    if (!intent || !tag) return false;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_BOLD:
            return tag == HTM_TAG_B || tag == HTM_TAG_STRONG;
        case INPUT_INTENT_FORMAT_ITALIC:
            return tag == HTM_TAG_I || tag == HTM_TAG_EM;
        case INPUT_INTENT_FORMAT_UNDERLINE:
            return tag == HTM_TAG_U;
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:
            return tag == HTM_TAG_S || tag == HTM_TAG_STRIKE;
        case INPUT_INTENT_FORMAT_SUBSCRIPT:
            return tag == HTM_TAG_SUB;
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:
            return tag == HTM_TAG_SUP;
        default:
            return false;
    }
}

static bool rich_format_range_selects_all_children(DomRange* range,
                                                   DomElement* wrapper) {
    if (!range || !wrapper) return false;
    DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
    return range->start.node == wrapper_node &&
        range->start.offset == 0 &&
        range->end.node == wrapper_node &&
        range->end.offset == dom_node_boundary_length(wrapper_node);
}

static bool rich_format_range_selects_only_text_child(DomRange* range,
                                                      DomElement* wrapper,
                                                      DomText** out_text) {
    if (out_text) *out_text = nullptr;
    if (!range || !wrapper || !wrapper->first_child ||
        wrapper->first_child->next_sibling ||
        !wrapper->first_child->is_text()) {
        return false;
    }
    DomText* text = lam::dom_require_text(wrapper->first_child);
    if (!text || range->start.node != static_cast<DomNode*>(text) ||
        range->end.node != static_cast<DomNode*>(text) ||
        range->start.offset != 0 ||
        range->end.offset != dom_text_utf16_length(text)) {
        return false;
    }
    if (out_text) *out_text = text;
    return true;
}

static DomElement* rich_format_toggle_wrapper_for_range(
        DomRange* range,
        const EditingIntent* intent) {
    if (!range || !intent || !range->start.node || !range->end.node) {
        return nullptr;
    }
    if (range->start.node == range->end.node &&
        range->start.node->is_element()) {
        DomElement* elem = lam::dom_require_element(range->start.node);
        if (elem && rich_format_tag_matches(intent, elem->tag()) &&
            rich_format_range_selects_all_children(range, elem)) {
            return elem;
        }
    }

    if (!range->start.node->is_text() ||
        range->start.node != range->end.node ||
        !range->start.node->parent ||
        !range->start.node->parent->is_element()) {
        return nullptr;
    }
    DomElement* parent = lam::dom_require_element(range->start.node->parent);
    if (!parent || !rich_format_tag_matches(intent, parent->tag())) {
        return nullptr;
    }
    DomText* text = nullptr;
    return rich_format_range_selects_only_text_child(range, parent, &text)
        ? parent
        : nullptr;
}

static bool rich_format_unwrap_element(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       DomElement* wrapper,
                                       EditingRichMutationLogFn log_mutation,
                                       void* log_user) {
    if (!state || !surface || !intent || !wrapper ||
        !wrapper->parent || !wrapper->parent->is_element()) {
        return false;
    }

    DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
    DomNode* parent_node = wrapper_node->parent;
    uint32_t wrapper_idx = dom_node_child_index(wrapper_node);
    if (wrapper_idx == (uint32_t)-1) return false;

    DomNode* first_moved = nullptr;
    DomNode* last_moved = nullptr;
    uint32_t moved_count = 0;
    DomNode* child = wrapper->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_mutation_pre_remove(state, child);
        if (!wrapper_node->remove_child(child)) return false;
        if (!parent_node->insert_before(child, wrapper_node)) return false;
        dom_mutation_post_insert(state, parent_node, child);
        if (!first_moved) first_moved = child;
        last_moved = child;
        moved_count++;
        child = next;
    }

    dom_mutation_pre_remove(state, wrapper_node);
    if (!parent_node->remove_child(wrapper_node)) return false;

    const char* exc = nullptr;
    DomBoundary start = { parent_node, wrapper_idx };
    DomBoundary end = { parent_node, wrapper_idx + moved_count };
    if (first_moved && first_moved == last_moved && first_moved->is_text()) {
        DomText* text = lam::dom_require_text(first_moved);
        start = { first_moved, 0 };
        end = { first_moved, dom_text_utf16_length(text) };
    }
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_format: unwrap selection restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "format-toggle",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_format: unwrapped <%s>", wrapper->node_name());
    return true;
}

bool editing_rich_default_format(DocState* state,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 EditingRichMutationLogFn log_mutation,
                                 void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0]) {
        return false;
    }
    if (!editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc) {
        return false;
    }

    const char* tag_name = rich_format_tag_name(intent);
    if (!tag_name) return false;

    DomRange* range = state->dom_selection->ranges[0];
    if (dom_range_collapsed(range)) {
        return false;
    }

    DomElement* toggle_wrapper =
        rich_format_toggle_wrapper_for_range(range, intent);
    if (toggle_wrapper) {
        return rich_format_unwrap_element(state, surface, intent,
                                          toggle_wrapper, log_mutation,
                                          log_user);
    }

    DomElement* wrapper = dom_element_create(surface->owner->doc,
                                             tag_name, nullptr);
    if (!wrapper) {
        log_debug("editing_rich_default_format: failed to create <%s>", tag_name);
        return false;
    }

    const char* exc = nullptr;
    if (!dom_range_surround_contents(range, static_cast<DomNode*>(wrapper), &exc)) {
        log_debug("editing_rich_default_format: surround <%s> rejected: %s",
                  tag_name, exc ? exc : "?");
        return false;
    }

    DomBoundary start = { static_cast<DomNode*>(wrapper), 0 };
    DomBoundary end = {
        static_cast<DomNode*>(wrapper),
        dom_node_boundary_length(static_cast<DomNode*>(wrapper))
    };
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_format: selection restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "format",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_format: wrapped selection in <%s>", tag_name);
    return true;
}

static const char* rich_style_property_name(const EditingIntent* intent) {
    if (!intent) return nullptr;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_FORE_COLOR:
            return "color";
        default:
            return nullptr;
    }
}

static DomElement* rich_create_native_element(DomDocument* doc,
                                              const char* tag_name);

bool editing_rich_default_style(DocState* state,
                                const EditingSurface* surface,
                                const EditingIntent* intent,
                                EditingRichMutationLogFn log_mutation,
                                void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0]) {
        return false;
    }
    if (!editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc || !surface->owner->doc->arena ||
        !intent->data || !intent->data[0]) {
        return false;
    }

    const char* prop_name = rich_style_property_name(intent);
    if (!prop_name) return false;

    DomRange* range = state->dom_selection->ranges[0];
    if (dom_range_collapsed(range)) {
        return false;
    }

    DomElement* wrapper =
        rich_create_native_element(surface->owner->doc, "span");
    if (!wrapper) return false;

    size_t style_len = strlen(prop_name) + strlen(intent->data) + 3;
    char* style_text =
        (char*)arena_alloc(surface->owner->doc->arena, style_len + 1);
    if (!style_text) return false;
    snprintf(style_text, style_len + 1, "%s: %s", prop_name, intent->data);

    if (!dom_element_set_attribute(wrapper, "style", style_text)) {
        log_debug("editing_rich_default_style: failed to set %s style",
                  prop_name);
        return false;
    }

    const char* exc = nullptr;
    if (!dom_range_surround_contents(range, static_cast<DomNode*>(wrapper), &exc)) {
        log_debug("editing_rich_default_style: surround <span> rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    DomBoundary start = { static_cast<DomNode*>(wrapper), 0 };
    DomBoundary end = {
        static_cast<DomNode*>(wrapper),
        dom_node_boundary_length(static_cast<DomNode*>(wrapper))
    };
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_style: selection restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "style",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_style: wrapped selection with %s=%s",
              prop_name, intent->data);
    return true;
}

static DomElement* rich_create_native_element(DomDocument* doc,
                                              const char* tag_name) {
    if (!doc || !doc->input || !tag_name) return nullptr;
    MarkBuilder builder(doc->input);
    Item item = builder.createElement(tag_name);
    if (get_type_id(item) != LMD_TYPE_ELEMENT || !item.element) {
        log_debug("rich_create_native_element: failed to create <%s>", tag_name);
        return nullptr;
    }
    return dom_element_create(doc, tag_name, item.element);
}

static DomElement* rich_nearest_anchor(const EditingSurface* surface,
                                       DomRange* range) {
    if (!surface || !surface->owner || !range) return nullptr;
    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    DomBoundary boundary = range->end.node ? range->end : range->start;
    DomNode* node = boundary.node;
    if (!node) return nullptr;

    for (DomNode* cur = node; cur && cur != owner_node; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = lam::dom_require_element(cur);
        if (elem && elem->tag() == HTM_TAG_A) return elem;
    }
    return nullptr;
}

static bool rich_unwrap_anchor(DocState* state,
                               const EditingSurface* surface,
                               const EditingIntent* intent,
                               DomElement* anchor,
                               DomRange* range,
                               EditingRichMutationLogFn log_mutation,
                               void* log_user) {
    if (!state || !surface || !intent || !anchor || !range ||
        !anchor->parent || !anchor->parent->is_element()) {
        return false;
    }

    DomNode* anchor_node = static_cast<DomNode*>(anchor);
    DomNode* parent_node = anchor_node->parent;
    uint32_t anchor_idx = dom_node_child_index(anchor_node);
    if (anchor_idx == (uint32_t)-1) return false;

    DomBoundary restore_start = range->start;
    DomBoundary restore_end = range->end;
    DomNode* child = anchor->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_mutation_pre_remove(state, child);
        if (!anchor_node->remove_child(child)) return false;
        if (!parent_node->insert_before(child, anchor_node)) return false;
        dom_mutation_post_insert(state, parent_node, child);
        child = next;
    }

    dom_mutation_pre_remove(state, anchor_node);
    if (!parent_node->remove_child(anchor_node)) return false;

    if (restore_start.node == anchor_node) {
        restore_start.node = parent_node;
        restore_start.offset = anchor_idx;
    }
    if (restore_end.node == anchor_node) {
        restore_end.node = parent_node;
        restore_end.offset = anchor_idx;
    }

    const char* exc = nullptr;
    if (!state_store_set_selection(state, &restore_start, &restore_end, &exc)) {
        DomBoundary start = { parent_node, anchor_idx };
        DomBoundary end = { parent_node, anchor_idx };
        if (!state_store_set_selection(state, &start, &end, &exc)) {
            log_debug("editing_rich_default_link: unlink selection restore rejected: %s",
                      exc ? exc : "?");
            return false;
        }
        restore_start = start;
        restore_end = end;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "unlink",
                     0, 0, restore_start.offset, restore_end.offset, log_user);
    }
    log_debug("editing_rich_default_link: unwrapped <a>");
    return true;
}

bool editing_rich_default_link(DocState* state,
                               const EditingSurface* surface,
                               const EditingIntent* intent,
                               EditingRichMutationLogFn log_mutation,
                               void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc) {
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    if (intent->type == INPUT_INTENT_FORMAT_UNLINK) {
        DomElement* anchor = rich_nearest_anchor(surface, range);
        if (!anchor) {
            log_debug("editing_rich_default_link: no anchor to unlink");
            return false;
        }
        return rich_unwrap_anchor(state, surface, intent, anchor, range,
                                  log_mutation, log_user);
    }

    if (intent->type != INPUT_INTENT_INSERT_LINK ||
        !intent->data || !intent->data[0] || dom_range_collapsed(range)) {
        return false;
    }

    DomElement* anchor =
        rich_create_native_element(surface->owner->doc, "a");
    if (!anchor) return false;
    if (!dom_element_set_attribute(anchor, "href", intent->data)) {
        log_debug("editing_rich_default_link: failed to set href");
        return false;
    }

    const char* exc = nullptr;
    if (!dom_range_surround_contents(range, static_cast<DomNode*>(anchor), &exc)) {
        log_debug("editing_rich_default_link: surround <a> rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    DomBoundary start = { static_cast<DomNode*>(anchor), 0 };
    DomBoundary end = {
        static_cast<DomNode*>(anchor),
        dom_node_boundary_length(static_cast<DomNode*>(anchor))
    };
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_link: selection restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "create-link",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_link: wrapped selection in <a>");
    return true;
}

bool editing_rich_default_object(DocState* state,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 EditingRichMutationLogFn log_mutation,
                                 void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc) {
        return false;
    }
    bool is_horizontal_rule =
        intent->type == INPUT_INTENT_INSERT_HORIZONTAL_RULE;
    bool is_image = intent->type == INPUT_INTENT_INSERT_IMAGE;
    if (!is_horizontal_rule && !is_image) {
        return false;
    }
    if (is_image && (!intent->data || !intent->data[0])) {
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    const char* exc = nullptr;
    if (!dom_range_collapsed(range) &&
        !dom_range_delete_contents(range, &exc)) {
        log_debug("editing_rich_default_object: delete selection rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    const char* tag_name = is_image ? "img" : "hr";
    DomElement* object = rich_create_native_element(surface->owner->doc, tag_name);
    if (!object) return false;
    if (is_image && !dom_element_set_attribute(object, "src", intent->data)) {
        log_debug("editing_rich_default_object: failed to set image src");
        return false;
    }

    DomNode* object_node = static_cast<DomNode*>(object);
    if (!dom_range_insert_node(range, object_node, &exc)) {
        log_debug("editing_rich_default_object: insert <%s> rejected: %s",
                  tag_name,
                  exc ? exc : "?");
        return false;
    }

    DomNode* parent = object_node->parent;
    uint32_t index = dom_node_child_index(object_node);
    if (!parent || index == (uint32_t)-1) {
        return false;
    }

    DomBoundary caret = { parent, index + 1 };
    if (!state_store_set_selection(state, &caret, &caret, &exc)) {
        log_debug("editing_rich_default_object: caret restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent,
                     is_image ? "insert-image" : "insert-horizontal-rule",
                     0, 0, caret.offset, caret.offset, log_user);
    }
    log_debug("editing_rich_default_object: inserted <%s>", tag_name);
    return true;
}

static bool rich_format_block_supported_tag(const char* tag_name) {
    if (!tag_name || !tag_name[0]) return false;
    return strcasecmp(tag_name, "p") == 0 ||
        strcasecmp(tag_name, "div") == 0 ||
        strcasecmp(tag_name, "h1") == 0 ||
        strcasecmp(tag_name, "h2") == 0 ||
        strcasecmp(tag_name, "h3") == 0 ||
        strcasecmp(tag_name, "h4") == 0 ||
        strcasecmp(tag_name, "h5") == 0 ||
        strcasecmp(tag_name, "h6") == 0 ||
        strcasecmp(tag_name, "blockquote") == 0 ||
        strcasecmp(tag_name, "pre") == 0;
}

static const char* rich_format_block_normalize_tag(DomDocument* doc,
                                                   const char* value) {
    if (!doc || !value) return nullptr;
    while (*value == ' ' || *value == '\t' ||
           *value == '\n' || *value == '\r') {
        value++;
    }
    if (*value == '<') value++;

    char tag_buf[16];
    uint32_t len = 0;
    while (value[len] && value[len] != '>' &&
           value[len] != ' ' && value[len] != '\t' &&
           value[len] != '\n' && value[len] != '\r' &&
           len + 1 < sizeof(tag_buf)) {
        char c = value[len];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        tag_buf[len] = c;
        len++;
    }
    tag_buf[len] = '\0';
    if (!rich_format_block_supported_tag(tag_buf)) return nullptr;

    lam::PoolPtr<char> tag_copy = lam::promote_to_arena(doc->arena, tag_buf);
    return tag_copy ? tag_copy.get() : nullptr;
}

static bool rich_format_block_tag(uintptr_t tag) {
    return tag == HTM_TAG_P ||
        tag == HTM_TAG_DIV ||
        tag == HTM_TAG_H1 ||
        tag == HTM_TAG_H2 ||
        tag == HTM_TAG_H3 ||
        tag == HTM_TAG_H4 ||
        tag == HTM_TAG_H5 ||
        tag == HTM_TAG_H6 ||
        tag == HTM_TAG_BLOCKQUOTE ||
        tag == HTM_TAG_PRE;
}

static bool rich_node_contains(DomNode* ancestor, DomNode* node) {
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur == ancestor) return true;
    }
    return false;
}

static DomElement* rich_format_block_target(const EditingSurface* surface,
                                            DomRange* range) {
    if (!surface || !surface->owner || !range) return nullptr;
    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    DomBoundary boundary = range->end.node ? range->end : range->start;
    DomNode* node = boundary.node;
    if (!node) return nullptr;

    for (DomNode* cur = node; cur && cur != owner_node; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = lam::dom_require_element(cur);
        if (elem && rich_format_block_tag(elem->tag())) {
            return elem;
        }
    }
    return nullptr;
}

bool editing_rich_default_format_block(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingRichMutationLogFn log_mutation,
                                       void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc) {
        return false;
    }

    const char* tag_name =
        rich_format_block_normalize_tag(surface->owner->doc, intent->data);
    if (!tag_name) {
        log_debug("editing_rich_default_format_block: unsupported tag value %s",
                  intent->data ? intent->data : "?");
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    DomElement* old_block = rich_format_block_target(surface, range);
    if (!old_block || !old_block->parent || !old_block->parent->is_element()) {
        log_debug("editing_rich_default_format_block: no single block target");
        return false;
    }
    if (strcasecmp(old_block->node_name(), tag_name) == 0) {
        return true;
    }

    DomElement* new_block = dom_element_create(surface->owner->doc,
                                               tag_name, nullptr);
    if (!new_block) {
        log_debug("editing_rich_default_format_block: failed to create <%s>",
                  tag_name);
        return false;
    }

    DomNode* old_node = static_cast<DomNode*>(old_block);
    DomNode* new_node = static_cast<DomNode*>(new_block);
    DomNode* parent_node = old_node->parent;
    DomBoundary restore_start = range->start;
    DomBoundary restore_end = range->end;
    bool start_in_old = rich_node_contains(old_node, restore_start.node);
    bool end_in_old = rich_node_contains(old_node, restore_end.node);

    if (!parent_node->insert_before(new_node, old_node)) return false;
    dom_mutation_post_insert(state, parent_node, new_node);

    DomNode* child = old_block->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_mutation_pre_remove(state, child);
        if (!old_node->remove_child(child)) return false;
        if (!new_node->append_child(child)) return false;
        dom_mutation_post_insert(state, new_node, child);
        child = next;
    }

    dom_mutation_pre_remove(state, old_node);
    if (!parent_node->remove_child(old_node)) return false;

    if (restore_start.node == old_node) restore_start.node = new_node;
    if (restore_end.node == old_node) restore_end.node = new_node;
    if (!start_in_old) {
        restore_start.node = new_node;
        restore_start.offset = 0;
    }
    if (!end_in_old) {
        restore_end.node = new_node;
        restore_end.offset = dom_node_boundary_length(new_node);
    }

    const char* exc = nullptr;
    if (!state_store_set_selection(state, &restore_start, &restore_end, &exc)) {
        DomBoundary start = { new_node, 0 };
        DomBoundary end = { new_node, dom_node_boundary_length(new_node) };
        if (!state_store_set_selection(state, &start, &end, &exc)) {
            log_debug("editing_rich_default_format_block: selection restore rejected: %s",
                      exc ? exc : "?");
            return false;
        }
        restore_start = start;
        restore_end = end;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "format-block",
                     0, 0, restore_start.offset, restore_end.offset, log_user);
    }
    log_debug("editing_rich_default_format_block: changed <%s> to <%s>",
              old_block->node_name(), tag_name);
    return true;
}

static const char* rich_justify_align_value(const EditingIntent* intent) {
    if (!intent) return nullptr;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_JUSTIFY_LEFT:
            return "left";
        case INPUT_INTENT_FORMAT_JUSTIFY_CENTER:
            return "center";
        case INPUT_INTENT_FORMAT_JUSTIFY_RIGHT:
            return "right";
        case INPUT_INTENT_FORMAT_JUSTIFY_FULL:
            return "justify";
        default:
            return nullptr;
    }
}

bool editing_rich_default_justify(DocState* state,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  EditingRichMutationLogFn log_mutation,
                                  void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner) {
        return false;
    }

    const char* align_value = rich_justify_align_value(intent);
    if (!align_value) return false;

    DomRange* range = state->dom_selection->ranges[0];
    DomElement* block = rich_format_block_target(surface, range);
    if (!block) {
        log_debug("editing_rich_default_justify: no single block target");
        return false;
    }
    if (!dom_element_set_attribute(block, "align", align_value)) {
        log_debug("editing_rich_default_justify: failed to set align=%s on <%s>",
                  align_value, block->node_name());
        return false;
    }

    const char* exc = nullptr;
    DomBoundary start = range->start;
    DomBoundary end = range->end;
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_justify: selection restore rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "justify",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_justify: set <%s> align=%s",
              block->node_name(), align_value);
    return true;
}

static const char* rich_list_tag_name(const EditingIntent* intent) {
    if (!intent) return nullptr;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_ORDERED_LIST:
            return "ol";
        case INPUT_INTENT_FORMAT_UNORDERED_LIST:
            return "ul";
        default:
            return nullptr;
    }
}

bool editing_rich_default_list(DocState* state,
                               const EditingSurface* surface,
                               const EditingIntent* intent,
                               EditingRichMutationLogFn log_mutation,
                               void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc) {
        return false;
    }

    const char* list_tag = rich_list_tag_name(intent);
    if (!list_tag) return false;

    DomRange* range = state->dom_selection->ranges[0];
    DomElement* old_block = rich_format_block_target(surface, range);
    if (!old_block || !old_block->parent || !old_block->parent->is_element()) {
        log_debug("editing_rich_default_list: no single block target");
        return false;
    }

    DomElement* list = dom_element_create(surface->owner->doc, list_tag, nullptr);
    DomElement* item = dom_element_create(surface->owner->doc, "li", nullptr);
    if (!list || !item) {
        log_debug("editing_rich_default_list: failed to create <%s><li>",
                  list_tag);
        return false;
    }

    DomNode* old_node = static_cast<DomNode*>(old_block);
    DomNode* list_node = static_cast<DomNode*>(list);
    DomNode* item_node = static_cast<DomNode*>(item);
    DomNode* parent_node = old_node->parent;
    DomBoundary restore_start = range->start;
    DomBoundary restore_end = range->end;
    bool start_in_old = rich_node_contains(old_node, restore_start.node);
    bool end_in_old = rich_node_contains(old_node, restore_end.node);

    if (!list_node->append_child(item_node)) return false;
    if (!parent_node->insert_before(list_node, old_node)) return false;
    dom_mutation_post_insert(state, parent_node, list_node);

    DomNode* child = old_block->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_mutation_pre_remove(state, child);
        if (!old_node->remove_child(child)) return false;
        if (!item_node->append_child(child)) return false;
        dom_mutation_post_insert(state, item_node, child);
        child = next;
    }

    dom_mutation_pre_remove(state, old_node);
    if (!parent_node->remove_child(old_node)) return false;

    if (restore_start.node == old_node) restore_start.node = item_node;
    if (restore_end.node == old_node) restore_end.node = item_node;
    if (!start_in_old) {
        restore_start.node = item_node;
        restore_start.offset = 0;
    }
    if (!end_in_old) {
        restore_end.node = item_node;
        restore_end.offset = dom_node_boundary_length(item_node);
    }

    const char* exc = nullptr;
    if (!state_store_set_selection(state, &restore_start, &restore_end, &exc)) {
        DomBoundary start = { item_node, 0 };
        DomBoundary end = { item_node, dom_node_boundary_length(item_node) };
        if (!state_store_set_selection(state, &start, &end, &exc)) {
            log_debug("editing_rich_default_list: selection restore rejected: %s",
                      exc ? exc : "?");
            return false;
        }
        restore_start = start;
        restore_end = end;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "list",
                     0, 0, restore_start.offset, restore_end.offset, log_user);
    }
    log_debug("editing_rich_default_list: changed <%s> to <%s><li>",
              old_block->node_name(), list_tag);
    return true;
}

static DomElement* rich_nearest_blockquote(const EditingSurface* surface,
                                           DomElement* block) {
    if (!surface || !surface->owner || !block) return nullptr;
    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    DomNode* node = static_cast<DomNode*>(block);
    for (DomNode* cur = node; cur && cur != owner_node; cur = cur->parent) {
        if (!cur->is_element()) continue;
        DomElement* elem = lam::dom_require_element(cur);
        if (elem && elem->tag() == HTM_TAG_BLOCKQUOTE) return elem;
    }
    return nullptr;
}

static bool rich_indent_block(DocState* state,
                              const EditingSurface* surface,
                              const EditingIntent* intent,
                              DomElement* block,
                              DomRange* range,
                              EditingRichMutationLogFn log_mutation,
                              void* log_user) {
    if (!state || !surface || !surface->owner || !surface->owner->doc ||
        !intent || !block || !range || !block->parent ||
        !block->parent->is_element()) {
        return false;
    }

    DomElement* quote =
        dom_element_create(surface->owner->doc, "blockquote", nullptr);
    if (!quote) {
        log_debug("editing_rich_default_indent: failed to create <blockquote>");
        return false;
    }

    DomNode* block_node = static_cast<DomNode*>(block);
    DomNode* quote_node = static_cast<DomNode*>(quote);
    DomNode* parent_node = block_node->parent;
    DomBoundary restore_start = range->start;
    DomBoundary restore_end = range->end;

    if (!parent_node->insert_before(quote_node, block_node)) return false;
    dom_mutation_post_insert(state, parent_node, quote_node);

    dom_mutation_pre_remove(state, block_node);
    if (!parent_node->remove_child(block_node)) return false;
    if (!quote_node->append_child(block_node)) return false;
    dom_mutation_post_insert(state, quote_node, block_node);

    const char* exc = nullptr;
    if (!state_store_set_selection(state, &restore_start, &restore_end, &exc)) {
        DomBoundary start = { block_node, 0 };
        DomBoundary end = { block_node, dom_node_boundary_length(block_node) };
        if (!state_store_set_selection(state, &start, &end, &exc)) {
            log_debug("editing_rich_default_indent: selection restore rejected: %s",
                      exc ? exc : "?");
            return false;
        }
        restore_start = start;
        restore_end = end;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "indent",
                     0, 0, restore_start.offset, restore_end.offset, log_user);
    }
    log_debug("editing_rich_default_indent: wrapped <%s> in <blockquote>",
              block->node_name());
    return true;
}

static bool rich_outdent_blockquote(DocState* state,
                                    const EditingSurface* surface,
                                    const EditingIntent* intent,
                                    DomElement* quote,
                                    DomRange* range,
                                    EditingRichMutationLogFn log_mutation,
                                    void* log_user) {
    if (!state || !surface || !intent || !quote || !range ||
        !quote->parent || !quote->parent->is_element()) {
        return false;
    }

    DomNode* quote_node = static_cast<DomNode*>(quote);
    DomNode* parent_node = quote_node->parent;
    uint32_t quote_idx = dom_node_child_index(quote_node);
    if (quote_idx == (uint32_t)-1) return false;

    DomBoundary restore_start = range->start;
    DomBoundary restore_end = range->end;
    DomNode* child = quote->first_child;
    while (child) {
        DomNode* next = child->next_sibling;
        dom_mutation_pre_remove(state, child);
        if (!quote_node->remove_child(child)) return false;
        if (!parent_node->insert_before(child, quote_node)) return false;
        dom_mutation_post_insert(state, parent_node, child);
        child = next;
    }

    dom_mutation_pre_remove(state, quote_node);
    if (!parent_node->remove_child(quote_node)) return false;

    if (restore_start.node == quote_node) {
        restore_start.node = parent_node;
        restore_start.offset = quote_idx;
    }
    if (restore_end.node == quote_node) {
        restore_end.node = parent_node;
        restore_end.offset = quote_idx;
    }

    const char* exc = nullptr;
    if (!state_store_set_selection(state, &restore_start, &restore_end, &exc)) {
        DomBoundary start = { parent_node, quote_idx };
        DomBoundary end = { parent_node, quote_idx };
        if (!state_store_set_selection(state, &start, &end, &exc)) {
            log_debug("editing_rich_default_indent: outdent selection restore rejected: %s",
                      exc ? exc : "?");
            return false;
        }
        restore_start = start;
        restore_end = end;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "outdent",
                     0, 0, restore_start.offset, restore_end.offset, log_user);
    }
    log_debug("editing_rich_default_indent: unwrapped <blockquote>");
    return true;
}

bool editing_rich_default_indent(DocState* state,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 EditingRichMutationLogFn log_mutation,
                                 void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0] ||
        !editing_surface_is_rich(surface) || !surface->owner) {
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    DomElement* block = rich_format_block_target(surface, range);
    if (!block) {
        log_debug("editing_rich_default_indent: no single block target");
        return false;
    }

    if (intent->type == INPUT_INTENT_FORMAT_INDENT) {
        return rich_indent_block(state, surface, intent, block, range,
                                 log_mutation, log_user);
    }
    if (intent->type == INPUT_INTENT_FORMAT_OUTDENT) {
        DomElement* quote = rich_nearest_blockquote(surface, block);
        if (!quote) {
            log_debug("editing_rich_default_indent: no blockquote to outdent");
            return false;
        }
        return rich_outdent_blockquote(state, surface, intent, quote, range,
                                       log_mutation, log_user);
    }
    return false;
}

bool editing_rich_default_replace(DocState* state,
                                  const EditingIntent* intent,
                                  View* fallback_view,
                                  int fallback_offset,
                                  EditingRichMutationLogFn log_mutation,
                                  void* log_user) {
    if (!state || !intent) return false;
    if (intent->type != INPUT_INTENT_INSERT_TEXT &&
        intent->type != INPUT_INTENT_INSERT_REPLACEMENT_TEXT &&
        intent->type != INPUT_INTENT_INSERT_COMPOSITION_TEXT &&
        intent->type != INPUT_INTENT_INSERT_FROM_COMPOSITION &&
        intent->type != INPUT_INTENT_DELETE_COMPOSITION_TEXT &&
        intent->type != INPUT_INTENT_INSERT_FROM_PASTE &&
        intent->type != INPUT_INTENT_INSERT_FROM_DROP &&
        intent->type != INPUT_INTENT_INSERT_PARAGRAPH &&
        intent->type != INPUT_INTENT_INSERT_LINE_BREAK &&
        intent->type != INPUT_INTENT_DELETE_BY_CUT &&
        intent->type != INPUT_INTENT_DELETE_BY_DRAG &&
        intent->type != INPUT_INTENT_DELETE_CONTENT_BACKWARD &&
        intent->type != INPUT_INTENT_DELETE_CONTENT_FORWARD &&
        intent->type != INPUT_INTENT_DELETE_WORD_BACKWARD &&
        intent->type != INPUT_INTENT_DELETE_WORD_FORWARD) {
        return false;
    }

    DomText* text = nullptr;
    uint32_t start = 0;
    uint32_t end = 0;
    bool composition_range = rich_text_default_composition_range(state, intent,
        &text, &start, &end);
    DomSelection* dom_selection = state->dom_selection;
    EditingSurface fallback_surface;
    EditingSurface* fallback_surface_ptr = nullptr;
    if (fallback_view && editing_surface_from_target(fallback_view,
            &fallback_surface) && editing_surface_is_rich(&fallback_surface)) {
        fallback_surface_ptr = &fallback_surface;
    }
    if (composition_range) {
        // replace the current rich DOM preedit range below
    } else if (dom_selection && dom_selection->range_count > 0 &&
        dom_selection->ranges[0] && !dom_selection_is_collapsed(dom_selection)) {
        RichDefaultSelectionTarget target;
        if (!rich_transaction_resolve_selection_target(state,
                fallback_surface_ptr, intent, dom_selection, &target)) {
            return false;
        }
        if (target.mutation_complete) return true;
        text = target.text;
        start = target.start;
        end = target.end;
    } else {
        if (fallback_view && fallback_view->is_text()) {
            EditingSurface rich_surface;
            if (editing_surface_from_target(fallback_view, &rich_surface) &&
                editing_surface_is_rich(&rich_surface)) {
                text = lam::dom_require_text(static_cast<DomNode*>(fallback_view));
                start = fallback_offset < 0 ? 0 : (uint32_t)fallback_offset;
            }
        }
        if (!text && !rich_transaction_caret_from_state(state, &text, &start)) {
            return false;
        }
        end = start;
    }

    if (!text) return false;
    if (end < start) {
        uint32_t tmp = start;
        start = end;
        end = tmp;
    }
    const char* old_text = text->text ? text->text : "";
    uint32_t old_len = text->length > 0 ? (uint32_t)text->length : (uint32_t)strlen(old_text);
    if (start > old_len) start = old_len;
    if (end > old_len) end = old_len;

    const char* data = intent->data ? intent->data : "";
    if (intent->type == INPUT_INTENT_INSERT_PARAGRAPH ||
        intent->type == INPUT_INTENT_INSERT_LINE_BREAK) {
        data = "\n";
    } else if (intent->type == INPUT_INTENT_DELETE_BY_CUT ||
               intent->type == INPUT_INTENT_DELETE_BY_DRAG ||
               intent->type == INPUT_INTENT_DELETE_COMPOSITION_TEXT) {
        data = "";
    } else if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD ||
               intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD ||
               intent->type == INPUT_INTENT_DELETE_WORD_BACKWARD ||
               intent->type == INPUT_INTENT_DELETE_WORD_FORWARD) {
        data = "";
        if (start == end) {
            if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD) {
                if (rich_transaction_join_previous_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (start == 0) return false;
                uint32_t prev = start - 1;
                while (prev > 0 &&
                       (((unsigned char)old_text[prev] & 0xC0) == 0x80)) {
                    prev--;
                }
                start = prev;
            } else if (intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD) {
                if (end >= old_len) return false;
                uint32_t next = end + 1;
                while (next < old_len &&
                       (((unsigned char)old_text[next] & 0xC0) == 0x80)) {
                    next++;
                }
                end = next;
            } else if (intent->type == INPUT_INTENT_DELETE_WORD_BACKWARD) {
                uint32_t prev = te_prev_word_byte(old_text, old_len, start);
                if (prev >= start) return false;
                start = prev;
            } else if (intent->type == INPUT_INTENT_DELETE_WORD_FORWARD) {
                uint32_t next = te_next_word_byte(old_text, old_len, end);
                if (next <= end) return false;
                end = next;
            }
        }
    }

    uint32_t data_len = (uint32_t)strlen(data);
    size_t new_len = (size_t)old_len - (size_t)(end - start) + (size_t)data_len;
    char* replacement = (char*)mem_alloc(new_len + 1, MEM_CAT_TEMP);
    if (!replacement) return false;

    if (start > 0) memcpy(replacement, old_text, start);
    if (data_len > 0) memcpy(replacement + start, data, data_len);
    if (end < old_len) {
        memcpy(replacement + start + data_len, old_text + end, old_len - end);
    }
    replacement[new_len] = '\0';

    DomElement* text_parent = text->parent && text->parent->is_element()
        ? lam::dom_require_element(text->parent)
        : nullptr;
    int64_t text_child_idx = dom_text_get_child_index(text);

    doc_state_set_hover_target(state, NULL);
    bool updated = dom_text_set_content(text, replacement);
    mem_free(replacement);
    if (!updated) return false;

    DomText* live_text = text;
    if (text_parent && text_child_idx >= 0) {
        int64_t idx = 0;
        for (DomNode* child = text_parent->first_child; child;
             child = child->next_sibling, idx++) {
            if (idx == text_child_idx) {
                if (child->is_text()) live_text = lam::dom_require_text(child);
                break;
            }
        }
    }

    uint32_t caret_offset = start + data_len;
    if (composition_range &&
        intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT) {
        uint32_t caret_in_preedit = rich_utf8_byte_offset_for_codepoints(
            data, data_len, intent->composition_caret);
        caret_offset = start + caret_in_preedit;
    }

    DomNode* caret_node = static_cast<DomNode*>(live_text);
    uint32_t caret_boundary_offset = caret_offset;
    bool caret_is_text = true;
    if (rich_transaction_is_text_deletion_intent(intent) &&
        data_len == 0 && new_len == 0) {
        DomNode* cleanup_caret_node = nullptr;
        uint32_t cleanup_caret_offset = 0;
        if (rich_transaction_cleanup_empty_inline_after_delete(state, live_text,
                fallback_surface_ptr, &cleanup_caret_node, &cleanup_caret_offset)) {
            live_text = nullptr;
            caret_node = cleanup_caret_node;
            caret_boundary_offset = cleanup_caret_offset;
            caret_is_text = false;
        }
    }

    if (caret_is_text) {
        if (!rich_transaction_collapse_text_caret(state, live_text, caret_offset)) {
            return false;
        }
    } else if (!rich_transaction_collapse_dom_caret(state, caret_node,
            caret_boundary_offset)) {
        return false;
    }
    if (composition_range) {
        state->editing.composition.anchor_view = static_cast<View*>(live_text);
        state->editing.composition.anchor_offset =
            (int)start; // INT_CAST_OK: StateStore composition anchor stores byte offsets as int.
        state->editing.composition.dom_preedit_len =
            intent->type == INPUT_INTENT_INSERT_COMPOSITION_TEXT
                ? data_len
                : 0;
    }
    EditingSurface live_surface;
    View* live_surface_target = live_text
        ? static_cast<View*>(live_text)
        : static_cast<View*>(caret_node);
    if (live_surface_target &&
        editing_surface_from_target(live_surface_target, &live_surface) &&
        editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        uint32_t new_text_len = live_text
            ? (live_text->length > 0
                ? (uint32_t)live_text->length
                : (uint32_t)strlen(live_text->text ? live_text->text : ""))
            : 0;
        uint32_t log_selection_offset = caret_is_text
            ? caret_offset
            : caret_boundary_offset;
        if (log_mutation) {
            log_mutation(state, &live_surface, intent,
                         rich_text_default_mutation_operation(intent),
                         old_len, new_text_len,
                         log_selection_offset, log_selection_offset,
                         log_user);
        }
    }
    log_debug("editing_rich_default_replace: inserted %u bytes at [%u,%u]",
              data_len, start, end);
    return true;
}
