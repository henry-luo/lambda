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
#include "../lib/strbuf.h"

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

static bool rich_transaction_boundary_caret_from_state(DocState* state,
                                                       DomBoundary* out) {
    if (out) {
        out->node = nullptr;
        out->offset = 0;
    }
    if (!state || !state->dom_selection ||
        state->dom_selection->range_count == 0 ||
        !state->dom_selection->ranges[0] || !out) {
        return false;
    }

    DomRange* range = state->dom_selection->ranges[0];
    if (!dom_range_collapsed(range) || !range->start.node) return false;
    *out = range->start;
    return true;
}

static bool rich_transaction_is_text_deletion_intent(
        const EditingIntent* intent) {
    if (!intent) return false;
    return intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_CONTENT_FORWARD ||
        intent->type == INPUT_INTENT_DELETE_WORD_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_WORD_FORWARD ||
        intent->type == INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_SOFT_LINE_FORWARD ||
        intent->type == INPUT_INTENT_DELETE_HARD_LINE_BACKWARD ||
        intent->type == INPUT_INTENT_DELETE_HARD_LINE_FORWARD;
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

static bool rich_transaction_can_delete_inline_wrapper(
        DomElement* elem,
        const EditingSurface* surface) {
    if (!elem || !elem->parent || !elem->parent->is_element()) return false;
    if (surface && surface->owner == elem) return false;
    if (dom_element_has_attribute(elem, "contenteditable")) return false;
    return rich_transaction_is_cleanup_inline_tag(elem->tag());
}

static bool rich_transaction_delete_previous_inline_wrapper(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || !text->parent || !text->parent->is_element()) {
        return false;
    }

    DomElement* parent = lam::dom_require_element(text->parent);
    DomNode* text_node = static_cast<DomNode*>(text);
    DomNode* prev_node = text_node->prev_sibling;
    if (!parent || !prev_node || !prev_node->is_element()) return false;

    DomElement* inline_elem = lam::dom_require_element(prev_node);
    if (!rich_transaction_can_delete_inline_wrapper(inline_elem, surface)) {
        return false;
    }
    if (!inline_elem->first_child ||
        inline_elem->first_child != inline_elem->last_child ||
        !inline_elem->first_child->is_text()) {
        return false;
    }

    DomText* prev_text = lam::dom_require_text(inline_elem->first_child);
    if (!prev_text || dom_text_utf16_length(prev_text) != 1) return false;

    uint32_t inline_idx = dom_node_child_index(prev_node);
    if (inline_idx == (uint32_t)-1) return false;
    uint32_t old_len = prev_text->length > 0
        ? (uint32_t)prev_text->length
        : (uint32_t)strlen(prev_text->text ? prev_text->text : "");

    dom_mutation_pre_remove(state, prev_node);
    if (!rich_transaction_remove_child_for_edit(parent, prev_node)) {
        return false;
    }
    if (!rich_transaction_collapse_dom_caret(state,
            static_cast<DomNode*>(parent), inline_idx)) {
        return false;
    }

    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(parent),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent, "delete-inline",
                         old_len, 0, inline_idx, inline_idx, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
        if (log_mutation) {
            log_mutation(state, surface, intent, "delete-inline",
                         old_len, 0, inline_idx, inline_idx, log_user);
        }
    }
    log_debug("rich_transaction_delete_previous_inline_wrapper: removed %s at index %u",
              inline_elem->node_name(), inline_idx);
    return true;
}

static bool rich_transaction_is_atomic_delete_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_BR || tag_id == HTM_TAG_HR || tag_id == HTM_TAG_IMG;
}

static DomNode* rich_transaction_child_at(DomElement* parent, uint32_t index) {
    if (!parent) return nullptr;
    uint32_t cur = 0;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (cur == index) return child;
        cur++;
    }
    return nullptr;
}

static bool rich_transaction_delete_atomic_node(
        DocState* state,
        const EditingSurface* surface,
        DomElement* parent,
        DomNode* node,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !parent || !node ||
        node->parent != static_cast<DomNode*>(parent) ||
        !node->is_element()) {
        return false;
    }

    DomElement* elem = lam::dom_require_element(node);
    if (!elem || !rich_transaction_is_atomic_delete_tag(elem->tag())) {
        return false;
    }

    uint32_t child_idx = dom_node_child_index(node);
    if (child_idx == (uint32_t)-1) return false;
    dom_mutation_pre_remove(state, node);
    if (!rich_transaction_remove_child_for_edit(parent, node)) {
        return false;
    }
    if (!rich_transaction_collapse_dom_caret(state,
            static_cast<DomNode*>(parent), child_idx)) {
        return false;
    }

    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(parent),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent, "delete-atomic",
                         1, 0, child_idx, child_idx, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
        if (log_mutation) {
            log_mutation(state, surface, intent, "delete-atomic",
                         1, 0, child_idx, child_idx, log_user);
        }
    }
    log_debug("rich_transaction_delete_atomic_node: removed %s at index %u",
              elem->node_name(), child_idx);
    return true;
}

static bool rich_transaction_atomic_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static uint32_t rich_transaction_text_byte_len(DomText* text) {
    if (!text) return 0;
    const char* data = text->text ? text->text : "";
    return text->length > 0 ? (uint32_t)text->length : (uint32_t)strlen(data);
}

static uint32_t rich_transaction_atomic_leading_space_len(const char* text,
                                                          uint32_t length) {
    uint32_t count = 0;
    while (count < length &&
           rich_transaction_atomic_space((unsigned char)text[count])) {
        count++;
    }
    return count;
}

static uint32_t rich_transaction_atomic_trim_trailing_space(const char* text,
                                                           uint32_t length) {
    while (length > 0 &&
           rich_transaction_atomic_space((unsigned char)text[length - 1])) {
        length--;
    }
    return length;
}

static bool rich_transaction_set_text_slice(DomText* text,
                                            uint32_t start,
                                            uint32_t end) {
    if (!text || end < start) return false;
    const char* data = text->text ? text->text : "";
    uint32_t len = rich_transaction_text_byte_len(text);
    if (start > len) start = len;
    if (end > len) end = len;
    if (end < start) end = start;
    uint32_t new_len = end - start;
    char* replacement = (char*)mem_alloc((size_t)new_len + 1, MEM_CAT_TEMP);
    if (!replacement) return false;
    if (new_len > 0) memcpy(replacement, data + start, new_len);
    replacement[new_len] = '\0';
    bool ok = dom_text_set_content(text, replacement);
    mem_free(replacement);
    return ok;
}

static bool rich_transaction_delete_previous_atomic_whitespace_before_text(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || !text->parent || !text->parent->is_element()) {
        return false;
    }

    DomNode* text_node = static_cast<DomNode*>(text);
    DomNode* atomic_node = text_node->prev_sibling;
    if (!atomic_node || !atomic_node->is_element()) return false;
    DomElement* atomic = lam::dom_require_element(atomic_node);
    if (!atomic) return false;
    uintptr_t tag = atomic->tag();
    if (tag != HTM_TAG_BR && tag != HTM_TAG_HR) return false;

    DomElement* parent = lam::dom_require_element(text->parent);
    if (!parent) return false;
    uint32_t atomic_idx = dom_node_child_index(atomic_node);
    if (atomic_idx == (uint32_t)-1) return false;

    const char* data = text->text ? text->text : "";
    uint32_t text_len = rich_transaction_text_byte_len(text);
    uint32_t leading_len =
        rich_transaction_atomic_leading_space_len(data, text_len);
    if (leading_len > 0 && caret_offset <= leading_len) {
        if (!rich_transaction_set_text_slice(text, leading_len, text_len)) {
            return false;
        }
        return rich_transaction_delete_atomic_node(state, surface, parent,
            atomic_node, log_mutation, intent, log_user);
    }

    if (tag != HTM_TAG_HR || caret_offset != 0) return false;
    DomNode* before_atomic = atomic_node->prev_sibling;
    if (!before_atomic) return false;
    if (before_atomic->is_text()) {
        DomText* prev_text = lam::dom_require_text(before_atomic);
        const char* prev_data = prev_text->text ? prev_text->text : "";
        uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
        uint32_t trimmed_len =
            rich_transaction_atomic_trim_trailing_space(prev_data, prev_len);
        if (trimmed_len == prev_len) return false;
        if (!rich_transaction_set_text_slice(prev_text, 0, trimmed_len)) {
            return false;
        }
        return rich_transaction_delete_atomic_node(state, surface, parent,
            atomic_node, log_mutation, intent, log_user);
    }
    if (before_atomic->is_element() &&
        before_atomic->as_element()->tag() == HTM_TAG_BR) {
        dom_mutation_pre_remove(state, before_atomic);
        if (!rich_transaction_remove_child_for_edit(parent, before_atomic)) {
            return false;
        }
        if (!rich_transaction_delete_atomic_node(state, surface, parent,
                atomic_node, log_mutation, intent, log_user)) {
            return false;
        }
        log_debug("rich_transaction_delete_previous_atomic_whitespace_before_text: removed br before hr at index %u",
                  atomic_idx - 1);
        return true;
    }
    return false;
}

static bool rich_transaction_delete_previous_atomic_before_text(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!text || !text->parent || !text->parent->is_element()) return false;
    DomNode* previous = static_cast<DomNode*>(text)->prev_sibling;
    if (!previous) return false;
    DomElement* parent = lam::dom_require_element(text->parent);
    return rich_transaction_delete_atomic_node(state, surface, parent, previous,
        log_mutation, intent, log_user);
}

static bool rich_transaction_delete_previous_atomic_at_boundary(
        DocState* state,
        const EditingSurface* surface,
        DomBoundary caret,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!caret.node || !caret.node->is_element() || caret.offset == 0) {
        return false;
    }
    DomElement* parent = lam::dom_require_element(caret.node);
    DomNode* previous = rich_transaction_child_at(parent, caret.offset - 1);
    return rich_transaction_delete_atomic_node(state, surface, parent, previous,
        log_mutation, intent, log_user);
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

static bool rich_transaction_is_table_cell_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_TD || tag_id == HTM_TAG_TH;
}

static bool rich_transaction_is_list_tag(uintptr_t tag_id) {
    return tag_id == HTM_TAG_OL || tag_id == HTM_TAG_UL;
}

static bool rich_transaction_join_block_pair_allowed(DomElement* prev_block,
                                                     DomElement* current_block) {
    if (!prev_block || !current_block) return false;
    bool prev_cell = rich_transaction_is_table_cell_tag(prev_block->tag());
    bool current_cell = rich_transaction_is_table_cell_tag(current_block->tag());
    if (!prev_cell && !current_cell) return true;
    if (!prev_cell || !current_cell) return false;
    if (prev_block->parent != current_block->parent) return false;
    return !dom_element_has_attribute(prev_block, "rowspan") &&
        !dom_element_has_attribute(current_block, "rowspan");
}

static bool rich_transaction_table_cell_without_span(DomElement* cell) {
    return cell && rich_transaction_is_table_cell_tag(cell->tag()) &&
        !dom_element_has_attribute(cell, "rowspan") &&
        !dom_element_has_attribute(cell, "colspan");
}

static uint32_t rich_transaction_table_cell_colspan(DomElement* cell) {
    const char* attr = cell ? dom_element_get_attribute(cell, "colspan") : nullptr;
    if (!attr || !attr[0]) return 1;
    uint32_t span = 0;
    for (const char* p = attr; *p; p++) {
        if (*p < '0' || *p > '9') return 1;
        span = span * 10 + (uint32_t)(*p - '0');
        if (span > 1000) return 1;
    }
    return span > 0 ? span : 1;
}

static bool rich_transaction_normalize_table_cell_colspan_after_join(
        DomElement* prev_cell,
        DomElement* current_cell) {
    if (!prev_cell || !current_cell ||
        !rich_transaction_is_table_cell_tag(prev_cell->tag()) ||
        !rich_transaction_is_table_cell_tag(current_cell->tag()) ||
        prev_cell->parent != current_cell->parent) {
        return true;
    }

    bool had_span = dom_element_has_attribute(prev_cell, "colspan") ||
        dom_element_has_attribute(current_cell, "colspan");
    if (!had_span) return true;

    uint32_t joined_span = rich_transaction_table_cell_colspan(prev_cell) +
        rich_transaction_table_cell_colspan(current_cell);
    if (joined_span <= 1) {
        if (dom_element_has_attribute(prev_cell, "colspan")) {
            return dom_element_remove_attribute(prev_cell, "colspan");
        }
        return true;
    }

    char span_text[16];
    snprintf(span_text, sizeof(span_text), "%u", joined_span);
    return dom_element_set_attribute(prev_cell, "colspan", span_text);
}

static uint32_t rich_transaction_leading_space_len(const char* text,
                                                   uint32_t length);

static bool rich_transaction_whitespace_text_node(DomNode* node) {
    if (!node || !node->is_text()) return false;
    DomText* text = lam::dom_require_text(node);
    const char* data = text && text->text ? text->text : "";
    uint32_t len = rich_transaction_text_byte_len(text);
    return rich_transaction_leading_space_len(data, len) == len;
}

static bool rich_transaction_filler_br_block(DomElement* block) {
    if (!block || !block->first_child) return false;
    uint32_t br_count = 0;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (child->is_element() && child->as_element()->tag() == HTM_TAG_BR) {
            br_count++;
            continue;
        }
        if (child->is_element() &&
            rich_transaction_is_cleanup_inline_tag(child->as_element()->tag()) &&
            !dom_element_has_attribute(child->as_element(), "contenteditable") &&
            rich_transaction_filler_br_block(child->as_element())) {
            br_count++;
            continue;
        }
        if (!rich_transaction_whitespace_text_node(child)) return false;
    }
    return br_count == 1;
}

static bool rich_transaction_only_whitespace_after(DomNode* node) {
    for (DomNode* cur = node ? node->next_sibling : nullptr; cur;
         cur = cur->next_sibling) {
        if (!rich_transaction_whitespace_text_node(cur)) return false;
    }
    return true;
}

static bool rich_transaction_remove_whitespace_after(
        DocState* state,
        DomElement* parent,
        DomNode* node) {
    if (!parent || !node) return false;
    while (node->next_sibling) {
        DomNode* sibling = node->next_sibling;
        if (!rich_transaction_whitespace_text_node(sibling)) return false;
        dom_mutation_pre_remove(state, sibling);
        if (!rich_transaction_remove_child_for_edit(parent, sibling)) {
            return false;
        }
    }
    return true;
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

static DomNode* rich_transaction_live_child_at_index(DomElement* parent,
                                                     int64_t child_idx,
                                                     DomNode* fallback) {
    if (!parent || child_idx < 0) return fallback;
    int64_t idx = 0;
    for (DomNode* child = parent->first_child; child;
         child = child->next_sibling, idx++) {
        if (idx == child_idx) return child;
    }
    return fallback;
}

static DomText* rich_create_detached_text(DomDocument* doc,
                                          const char* data,
                                          uint32_t data_len);
static bool rich_transaction_insert_child_for_edit(DomElement* parent,
                                                   DomNode* child,
                                                   uint32_t child_idx);

static DomText* rich_transaction_split_text_for_edit(DocState* state,
                                                     DomElement* parent,
                                                     DomText* text,
                                                     uint32_t byte_offset,
                                                     uint32_t utf16_offset) {
    if (!parent || !parent->doc || !text || !text->native_string) {
        return nullptr;
    }
    int64_t text_idx = dom_text_get_child_index(text);
    if (text_idx < 0) return nullptr;

    const char* old_text = text->text ? text->text : "";
    uint32_t old_len = text->length > 0
        ? (uint32_t)text->length
        : (uint32_t)strlen(old_text);
    if (byte_offset > old_len) return nullptr;

    DomText* right = rich_create_detached_text(parent->doc,
        old_text + byte_offset, old_len - byte_offset);
    if (!right) return nullptr;

    char* left = (char*)mem_alloc((size_t)byte_offset + 1, MEM_CAT_TEMP);
    if (!left) return nullptr;
    if (byte_offset > 0) memcpy(left, old_text, byte_offset);
    left[byte_offset] = '\0';
    bool updated = dom_text_set_content(text, left);
    mem_free(left);
    if (!updated) return nullptr;

    DomText* live_left = rich_transaction_live_text_at_index(parent, text_idx,
                                                             text);
    if (!rich_transaction_insert_child_for_edit(parent, static_cast<DomNode*>(right),
            (uint32_t)(text_idx + 1))) {
        return nullptr;
    }
    DomNode* live_right_node = rich_transaction_live_child_at_index(parent,
        text_idx + 1, static_cast<DomNode*>(right));
    DomText* live_right = live_right_node && live_right_node->is_text()
        ? lam::dom_require_text(live_right_node)
        : right;
    if (state) dom_mutation_text_split(state, live_left, live_right, utf16_offset);
    return live_right;
}

static uint32_t rich_transaction_child_count(DomElement* parent) {
    uint32_t count = 0;
    if (!parent) return 0;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        count++;
    }
    return count;
}

static uint32_t rich_transaction_child_index_or_end(DomElement* parent,
                                                    DomNode* child) {
    if (!parent || !child) return rich_transaction_child_count(parent);
    uint32_t idx = dom_node_child_index(child);
    return idx == (uint32_t)-1 ? rich_transaction_child_count(parent) : idx;
}

struct RichJoinBlockContent {
    DomNode* child;
    DomText* text;
    bool direct_text;
};

static bool rich_transaction_inline_chain_text_only(DomNode* node,
                                                    DomText* text) {
    DomNode* cur = node;
    DomNode* text_node = static_cast<DomNode*>(text);
    while (cur && cur != text_node) {
        if (!cur->is_element()) return false;
        DomElement* elem = lam::dom_require_element(cur);
        if (!elem || !rich_transaction_is_cleanup_inline_tag(elem->tag()) ||
            !elem->first_child || elem->first_child != elem->last_child) {
            return false;
        }
        cur = elem->first_child;
    }
    return cur == text_node;
}

static bool rich_transaction_inline_fragment_node(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) return true;
    if (!node->is_element()) return false;
    DomElement* elem = lam::dom_require_element(node);
    if (!elem || !rich_transaction_is_cleanup_inline_tag(elem->tag()) ||
        dom_element_has_attribute(elem, "contenteditable")) {
        return false;
    }
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!rich_transaction_inline_fragment_node(child)) return false;
    }
    return true;
}

static bool rich_transaction_block_inline_fragments(DomElement* block) {
    if (!block || !block->first_child) return false;
    for (DomNode* child = block->first_child; child; child = child->next_sibling) {
        if (!rich_transaction_inline_fragment_node(child)) return false;
    }
    return true;
}

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

    if (!rich_transaction_inline_chain_text_only(child, text)) {
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

static bool rich_transaction_insert_child_for_edit(DomElement* parent,
                                                   DomNode* child,
                                                   uint32_t child_idx) {
    if (!parent || !child || child->parent) return false;
    if (parent->native_element && parent->doc && parent->doc->input) {
        if (child_idx > 0x7fffffffu) return false;
        Item child_item;
        if (!rich_transaction_item_for_child(child, &child_item)) {
            return false;
        }
        MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
        Item result = editor.elmt_insert_child(
            {.element = parent->native_element},
            (int)child_idx, // INT_CAST_OK: MarkEditor child index API is int.
            child_item
        );
        if (!result.element) {
            log_debug("rich_transaction_insert_child_for_edit: backing insert rejected");
            return false;
        }
        parent->native_element = result.element;
        if (parent->doc->input->ui_mode) {
            return true;
        }
    }

    DomNode* parent_node = static_cast<DomNode*>(parent);
    DomNode* ref = parent->first_child;
    uint32_t idx = 0;
    while (ref && idx < child_idx) {
        ref = ref->next_sibling;
        idx++;
    }
    if (ref) return parent_node->insert_before(child, ref);
    return parent_node->append_child(child);
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
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag()) ||
        !rich_transaction_join_block_pair_allowed(prev_block, current_block)) {
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
        if (!rich_transaction_normalize_table_cell_colspan_after_join(
                prev_block, current_block)) {
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

    if (!rich_transaction_normalize_table_cell_colspan_after_join(
            prev_block, current_block)) {
        return false;
    }

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

static bool rich_transaction_join_previous_inline_fragment_block(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || caret_offset != 0) return false;

    DomElement* current_block = rich_transaction_text_block_parent(text);
    if (!current_block ||
        !rich_transaction_block_inline_fragments(current_block)) {
        return false;
    }
    DomNode* current_node = static_cast<DomNode*>(current_block);
    if (editing_rich_find_text_descendant(current_node, false) != text) {
        return false;
    }

    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = lam::dom_require_element(prev_node);
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag()) ||
        !rich_transaction_join_block_pair_allowed(prev_block, current_block) ||
        !rich_transaction_block_inline_fragments(prev_block)) {
        return false;
    }

    DomText* prev_text = editing_rich_find_text_descendant(prev_node, true);
    if (!prev_text) return false;
    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? lam::dom_require_element(current_node->parent)
        : nullptr;
    if (!parent) return false;

    uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
    while (current_block->first_child) {
        DomNode* child = current_block->first_child;
        if (!current_node->remove_child(child)) return false;
        if (!rich_transaction_append_child_for_edit(prev_block, child)) {
            current_node->append_child(child);
            return false;
        }
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
            log_mutation(state, &live_surface, intent,
                         "inline-fragment-block-join",
                         prev_len, prev_len, prev_len, prev_len, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_previous_inline_fragment_block: moved inline fragments at offset %u",
              prev_len);
    return true;
}

static bool rich_transaction_join_previous_trailing_br_block(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || caret_offset != 0) return false;

    DomElement* current_block = rich_transaction_text_block_parent(text);
    RichJoinBlockContent current_content;
    if (!current_block ||
        !rich_transaction_simple_text_content(current_block, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;

    DomElement* prev_block = lam::dom_require_element(prev_node);
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag()) ||
        !prev_block->first_child || !prev_block->first_child->is_text()) {
        return false;
    }

    DomText* prev_text = lam::dom_require_text(prev_block->first_child);
    DomNode* last = prev_block->last_child;
    if (!prev_text || !last || !last->is_element() ||
        last->as_element()->tag() != HTM_TAG_BR) {
        return false;
    }
    for (DomNode* child = prev_block->first_child->next_sibling;
         child; child = child->next_sibling) {
        if (!child->is_element() || child->as_element()->tag() != HTM_TAG_BR) {
            return false;
        }
    }

    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? lam::dom_require_element(current_node->parent)
        : nullptr;
    if (!parent) return false;

    uint32_t br_idx = dom_node_child_index(last);
    if (br_idx == (uint32_t)-1) return false;
    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
    uint32_t current_len = rich_transaction_text_byte_len(text);

    dom_mutation_pre_remove(state, last);
    if (!rich_transaction_remove_child_for_edit(prev_block, last)) {
        return false;
    }

    DomText* caret_text = prev_text;
    uint32_t caret_offset_after = prev_len;
    if (br_idx == 1) {
        int64_t prev_idx = dom_text_get_child_index(prev_text);
        size_t joined_len = (size_t)prev_len + (size_t)current_len;
        char* joined = (char*)mem_alloc(joined_len + 1, MEM_CAT_TEMP);
        if (!joined) return false;
        if (prev_len > 0) memcpy(joined, prev_data, prev_len);
        if (current_len > 0) memcpy(joined + prev_len, current_data, current_len);
        joined[joined_len] = '\0';
        bool updated = dom_text_set_content(prev_text, joined);
        mem_free(joined);
        if (!updated) return false;
        caret_text = rich_transaction_live_text_at_index(
            prev_block, prev_idx, prev_text);
    } else {
        DomNode* moving_child = current_content.child;
        if (!moving_child || !current_node->remove_child(moving_child)) {
            return false;
        }
        if (!rich_transaction_append_child_for_edit(prev_block, moving_child)) {
            current_node->append_child(moving_child);
            return false;
        }
        caret_text = text;
        caret_offset_after = 0;
    }

    dom_mutation_pre_remove(state, current_node);
    if (!rich_transaction_remove_child_for_edit(parent, current_node)) {
        return false;
    }
    if (br_idx != 1) {
        int64_t appended_idx = (int64_t)rich_transaction_child_count(prev_block) - 1;
        caret_text = rich_transaction_live_text_at_index(
            prev_block, appended_idx, caret_text);
    }
    if (!rich_transaction_collapse_text_caret(state, caret_text,
            caret_offset_after)) {
        return false;
    }

    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(caret_text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent, "block-join-br",
                         prev_len, prev_len + current_len,
                         caret_offset_after, caret_offset_after, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_previous_trailing_br_block: joined with trailing br at index %u",
              br_idx);
    return true;
}

static bool rich_transaction_join_previous_empty_br_block(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || caret_offset != 0) return false;

    DomElement* current_block = rich_transaction_text_block_parent(text);
    RichJoinBlockContent current_content;
    if (!current_block ||
        !rich_transaction_simple_text_content(current_block, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;

    DomElement* prev_block = lam::dom_require_element(prev_node);
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag()) ||
        !rich_transaction_join_block_pair_allowed(prev_block, current_block) ||
        !rich_transaction_filler_br_block(prev_block)) {
        return false;
    }

    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? lam::dom_require_element(current_node->parent)
        : nullptr;
    if (!parent) return false;

    while (prev_block->first_child) {
        DomNode* filler = prev_block->first_child;
        dom_mutation_pre_remove(state, filler);
        if (!rich_transaction_remove_child_for_edit(prev_block, filler)) {
            return false;
        }
    }

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
    int64_t appended_idx = (int64_t)rich_transaction_child_count(prev_block) - 1;
    DomText* live_text = rich_transaction_live_text_at_index(
        prev_block, appended_idx, text);
    if (!rich_transaction_collapse_text_caret(state, live_text, 0)) {
        return false;
    }

    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(live_text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent,
                         "empty-br-block-join", 0, 0, 0, 0, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_previous_empty_br_block: moved text into filler block");
    return true;
}

static bool rich_transaction_join_child_block_with_parent_text(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || !text->parent || !text->parent->is_element()) {
        return false;
    }

    DomElement* parent = lam::dom_require_element(text->parent);
    if (!parent || !rich_transaction_is_join_block_tag(parent->tag())) {
        return false;
    }

    DomNode* text_node = static_cast<DomNode*>(text);
    DomNode* prev_node = text_node->prev_sibling;
    if (!prev_node || !prev_node->is_element()) return false;
    DomElement* prev_block = lam::dom_require_element(prev_node);
    if (!prev_block || !rich_transaction_is_join_block_tag(prev_block->tag())) {
        return false;
    }

    DomText* prev_text = editing_rich_find_text_descendant(prev_node, true);
    RichJoinBlockContent prev_content;
    if (!prev_text ||
        !rich_transaction_simple_text_content(prev_block, prev_text,
            &prev_content) ||
        !prev_content.direct_text) {
        return false;
    }

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
    uint32_t current_len = rich_transaction_text_byte_len(text);
    uint32_t prev_keep_len = prev_len;
    uint32_t current_skip_len = 0;
    if (prev_block->tag() != HTM_TAG_PRE && parent->tag() != HTM_TAG_PRE) {
        prev_keep_len = rich_transaction_trim_trailing_space(prev_data, prev_len);
        current_skip_len = rich_transaction_leading_space_len(current_data, current_len);
    }
    if (caret_offset > current_skip_len) return false;

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

    dom_mutation_pre_remove(state, text_node);
    if (!rich_transaction_remove_child_for_edit(parent, text_node)) {
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
            log_mutation(state, &live_surface, intent, "nested-block-join",
                         prev_keep_len, prev_len + current_skip_len,
                         prev_keep_len, prev_keep_len, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_child_block_with_parent_text: joined nested block at offset %u",
              prev_keep_len);
    return true;
}

static bool rich_transaction_join_parent_text_with_child_block(
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
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_node = static_cast<DomNode*>(current_block);
    DomNode* prev_node = current_node->prev_sibling;
    if (!prev_node || !prev_node->is_text()) return false;
    DomText* prev_text = lam::dom_require_text(prev_node);

    DomElement* parent = current_node->parent && current_node->parent->is_element()
        ? lam::dom_require_element(current_node->parent)
        : nullptr;
    if (!parent || !rich_transaction_is_join_block_tag(parent->tag())) {
        return false;
    }

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
    uint32_t current_len = rich_transaction_text_byte_len(text);
    uint32_t prev_keep_len = prev_len;
    uint32_t current_skip_len = 0;
    if (parent->tag() != HTM_TAG_PRE && current_block->tag() != HTM_TAG_PRE) {
        prev_keep_len = rich_transaction_trim_trailing_space(prev_data, prev_len);
        current_skip_len = rich_transaction_leading_space_len(current_data, current_len);
    }
    if (caret_offset > current_skip_len) return false;

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

    dom_mutation_pre_remove(state, current_node);
    if (!rich_transaction_remove_child_for_edit(parent, current_node)) {
        return false;
    }
    DomText* live_prev_text =
        rich_transaction_live_text_at_index(parent, prev_idx, prev_text);
    if (!rich_transaction_collapse_text_caret(state, live_prev_text,
            prev_keep_len)) {
        return false;
    }

    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(live_prev_text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent,
                         "nested-parent-text-join",
                         prev_keep_len, prev_len + current_skip_len,
                         prev_keep_len, prev_keep_len, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_parent_text_with_child_block: joined nested block at offset %u",
              prev_keep_len);
    return true;
}

static bool rich_transaction_join_nested_list_item_with_parent(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text || caret_offset != 0) return false;

    DomElement* current_li = rich_transaction_text_block_parent(text);
    if (!current_li || current_li->tag() != HTM_TAG_LI) return false;
    DomNode* current_li_node = static_cast<DomNode*>(current_li);
    if (editing_rich_find_text_descendant(current_li_node, false) != text) {
        return false;
    }

    DomElement* nested_list = current_li_node->parent &&
            current_li_node->parent->is_element()
        ? lam::dom_require_element(current_li_node->parent)
        : nullptr;
    if (!nested_list || !rich_transaction_is_list_tag(nested_list->tag()) ||
        nested_list->first_child != current_li_node) {
        return false;
    }

    DomNode* nested_list_node = static_cast<DomNode*>(nested_list);
    DomElement* parent_li = nested_list_node->parent &&
            nested_list_node->parent->is_element()
        ? lam::dom_require_element(nested_list_node->parent)
        : nullptr;
    if (!parent_li || parent_li->tag() != HTM_TAG_LI ||
        !rich_transaction_only_whitespace_after(nested_list_node) ||
        parent_li->first_child == nested_list_node) {
        return false;
    }

    DomNode* parent_li_node = static_cast<DomNode*>(parent_li);
    DomElement* outer_list = parent_li_node->parent &&
            parent_li_node->parent->is_element()
        ? lam::dom_require_element(parent_li_node->parent)
        : nullptr;
    if (!outer_list || !rich_transaction_is_list_tag(outer_list->tag())) {
        return false;
    }

    uint32_t parent_idx = dom_node_child_index(parent_li_node);
    if (parent_idx == (uint32_t)-1) return false;

    bool nested_list_will_empty = nested_list->last_child == current_li_node;
    dom_mutation_pre_remove(state, current_li_node);
    if (!rich_transaction_remove_child_for_edit(nested_list, current_li_node)) {
        return false;
    }
    if (!rich_transaction_insert_child_for_edit(outer_list, current_li_node,
            parent_idx + 1)) {
        rich_transaction_insert_child_for_edit(nested_list, current_li_node, 0);
        return false;
    }

    if (nested_list_will_empty) {
        if (!rich_transaction_remove_whitespace_after(state, parent_li,
                nested_list_node)) {
            return false;
        }
        dom_mutation_pre_remove(state, nested_list_node);
        if (!rich_transaction_remove_child_for_edit(parent_li, nested_list_node)) {
            return false;
        }
    }

    if (!rich_transaction_collapse_text_caret(state, text, 0)) {
        return false;
    }
    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
        if (log_mutation) {
            log_mutation(state, &live_surface, intent,
                         "nested-list-unwrap", 0, 0, 0, 0, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_nested_list_item_with_parent: lifted nested li after parent");
    return true;
}

static bool rich_transaction_join_previous_row_cell(
        DocState* state,
        const EditingSurface* surface,
        DomText* text,
        uint32_t caret_offset,
        EditingRichMutationLogFn log_mutation,
        const EditingIntent* intent,
        void* log_user) {
    if (!state || !text) return false;

    DomElement* current_cell = rich_transaction_text_block_parent(text);
    RichJoinBlockContent current_content;
    if (!current_cell || !rich_transaction_table_cell_without_span(current_cell) ||
        !rich_transaction_simple_text_content(current_cell, text,
            &current_content) ||
        !current_content.direct_text) {
        return false;
    }

    DomNode* current_cell_node = static_cast<DomNode*>(current_cell);
    DomElement* current_row = current_cell_node->parent &&
            current_cell_node->parent->is_element()
        ? lam::dom_require_element(current_cell_node->parent)
        : nullptr;
    if (!current_row || current_row->tag() != HTM_TAG_TR ||
        current_row->first_child != current_cell_node) {
        return false;
    }

    DomNode* current_row_node = static_cast<DomNode*>(current_row);
    DomNode* prev_row_node = current_row_node->prev_sibling;
    if (!prev_row_node || !prev_row_node->is_element()) return false;
    DomElement* prev_row = lam::dom_require_element(prev_row_node);
    if (!prev_row || prev_row->tag() != HTM_TAG_TR || !prev_row->last_child ||
        !prev_row->last_child->is_element()) {
        return false;
    }

    DomElement* prev_cell = lam::dom_require_element(prev_row->last_child);
    if (!rich_transaction_table_cell_without_span(prev_cell)) return false;
    DomText* prev_text = editing_rich_find_text_descendant(
        static_cast<DomNode*>(prev_cell), true);
    RichJoinBlockContent prev_content;
    if (!prev_text || !rich_transaction_simple_text_content(prev_cell,
            prev_text, &prev_content) || !prev_content.direct_text) {
        return false;
    }

    const char* prev_data = prev_text->text ? prev_text->text : "";
    const char* current_data = text->text ? text->text : "";
    uint32_t prev_len = rich_transaction_text_byte_len(prev_text);
    uint32_t current_len = rich_transaction_text_byte_len(text);
    uint32_t prev_keep_len = rich_transaction_trim_trailing_space(prev_data,
        prev_len);
    uint32_t current_skip_len = rich_transaction_leading_space_len(current_data,
        current_len);
    if (caret_offset > current_skip_len) return false;

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
        rich_transaction_live_text_at_index(prev_cell, prev_idx, prev_text);

    dom_mutation_pre_remove(state, current_cell_node);
    if (!rich_transaction_remove_child_for_edit(current_row, current_cell_node)) {
        return false;
    }

    if (!current_row->first_child) {
        DomElement* row_parent = current_row_node->parent &&
                current_row_node->parent->is_element()
            ? lam::dom_require_element(current_row_node->parent)
            : nullptr;
        if (!row_parent) return false;
        dom_mutation_pre_remove(state, current_row_node);
        if (!rich_transaction_remove_child_for_edit(row_parent, current_row_node)) {
            return false;
        }
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
            log_mutation(state, &live_surface, intent, "table-cross-row-join",
                         prev_keep_len, prev_len + current_skip_len,
                         prev_keep_len, prev_keep_len, log_user);
        }
    } else if (surface) {
        editing_interaction_set_active_surface(state, surface);
    }
    log_debug("rich_transaction_join_previous_row_cell: joined first cell with previous row");
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
        case INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_SOFT_LINE_FORWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_BACKWARD:
        case INPUT_INTENT_DELETE_HARD_LINE_FORWARD:
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

enum RichInlineFormatBit {
    RICH_INLINE_FORMAT_BOLD = 1u << 0,
    RICH_INLINE_FORMAT_ITALIC = 1u << 1,
    RICH_INLINE_FORMAT_UNDERLINE = 1u << 2,
    RICH_INLINE_FORMAT_STRIKETHROUGH = 1u << 3,
    RICH_INLINE_FORMAT_SUBSCRIPT = 1u << 4,
    RICH_INLINE_FORMAT_SUPERSCRIPT = 1u << 5
};

static uint32_t rich_format_state_bit_for_intent(const EditingIntent* intent) {
    if (!intent) return 0;
    switch (intent->type) {
        case INPUT_INTENT_FORMAT_BOLD:
            return RICH_INLINE_FORMAT_BOLD;
        case INPUT_INTENT_FORMAT_ITALIC:
            return RICH_INLINE_FORMAT_ITALIC;
        case INPUT_INTENT_FORMAT_UNDERLINE:
            return RICH_INLINE_FORMAT_UNDERLINE;
        case INPUT_INTENT_FORMAT_STRIKETHROUGH:
            return RICH_INLINE_FORMAT_STRIKETHROUGH;
        case INPUT_INTENT_FORMAT_SUBSCRIPT:
            return RICH_INLINE_FORMAT_SUBSCRIPT;
        case INPUT_INTENT_FORMAT_SUPERSCRIPT:
            return RICH_INLINE_FORMAT_SUPERSCRIPT;
        default:
            return 0;
    }
}

static const char* rich_format_tag_name_for_bit(uint32_t bit) {
    switch (bit) {
        case RICH_INLINE_FORMAT_BOLD:
            return "b";
        case RICH_INLINE_FORMAT_ITALIC:
            return "i";
        case RICH_INLINE_FORMAT_UNDERLINE:
            return "u";
        case RICH_INLINE_FORMAT_STRIKETHROUGH:
            return "s";
        case RICH_INLINE_FORMAT_SUBSCRIPT:
            return "sub";
        case RICH_INLINE_FORMAT_SUPERSCRIPT:
            return "sup";
        default:
            return nullptr;
    }
}

static uint32_t rich_format_bit_for_tag(uintptr_t tag) {
    switch (tag) {
        case HTM_TAG_B:
        case HTM_TAG_STRONG:
            return RICH_INLINE_FORMAT_BOLD;
        case HTM_TAG_I:
        case HTM_TAG_EM:
            return RICH_INLINE_FORMAT_ITALIC;
        case HTM_TAG_U:
            return RICH_INLINE_FORMAT_UNDERLINE;
        case HTM_TAG_S:
        case HTM_TAG_STRIKE:
            return RICH_INLINE_FORMAT_STRIKETHROUGH;
        case HTM_TAG_SUB:
            return RICH_INLINE_FORMAT_SUBSCRIPT;
        case HTM_TAG_SUP:
            return RICH_INLINE_FORMAT_SUPERSCRIPT;
        default:
            return 0;
    }
}

static bool rich_format_plain_wrapper_for_merge(DomElement* elem) {
    if (!elem || !rich_format_bit_for_tag(elem->tag())) return false;
    if (elem->id || elem->class_count > 0) return false;
    return true;
}

static bool rich_format_can_merge_pair(DomElement* left, DomElement* right) {
    if (!left || !right) return false;
    if (left->tag() != right->tag()) return false;
    return rich_format_plain_wrapper_for_merge(left) &&
        rich_format_plain_wrapper_for_merge(right);
}

static bool rich_format_move_children_for_merge(DocState* state,
                                                DomElement* dst,
                                                DomElement* src) {
    if (!state || !dst || !src) return false;
    while (src->first_child) {
        DomNode* child = src->first_child;
        dom_mutation_pre_remove(state, child);
        if (!rich_transaction_remove_child_for_edit(src, child)) {
            return false;
        }
        if (!rich_transaction_append_child_for_edit(dst, child)) {
            return false;
        }
        dom_mutation_post_insert(state, static_cast<DomNode*>(dst), child);
    }
    return !src->first_child;
}

static DomElement* rich_format_merge_pair(DocState* state,
                                          DomElement* left,
                                          DomElement* right) {
    if (!state || !rich_format_can_merge_pair(left, right) ||
        !right->parent || right->parent != static_cast<DomNode*>(left)->parent ||
        !right->parent->is_element()) {
        return nullptr;
    }

    DomElement* parent = lam::dom_require_element(right->parent);
    DomNode* right_node = static_cast<DomNode*>(right);
    if (!parent || !rich_format_move_children_for_merge(state, left, right)) {
        return nullptr;
    }
    dom_mutation_pre_remove(state, right_node);
    if (!rich_transaction_remove_child_for_edit(parent, right_node)) {
        return nullptr;
    }
    left->native_element = nullptr;
    right->native_element = nullptr;
    log_debug("editing_rich_default_format: merged adjacent <%s> wrappers",
              left->node_name());
    return left;
}

static DomElement* rich_format_normalize_adjacent(DocState* state,
                                                  DomElement* wrapper) {
    if (!state || !wrapper || !wrapper->parent) return wrapper;

    bool merged = true;
    while (merged && wrapper && wrapper->parent) {
        merged = false;
        DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
        DomNode* prev = wrapper_node->prev_sibling;
        if (prev && prev->is_element()) {
            DomElement* prev_elem = lam::dom_require_element(prev);
            DomElement* normalized = rich_format_merge_pair(state, prev_elem,
                                                            wrapper);
            if (normalized) {
                wrapper = normalized;
                merged = true;
                continue;
            }
        }

        DomNode* next = wrapper_node->next_sibling;
        if (next && next->is_element()) {
            DomElement* next_elem = lam::dom_require_element(next);
            DomElement* normalized = rich_format_merge_pair(state, wrapper,
                                                            next_elem);
            if (normalized) {
                wrapper = normalized;
                merged = true;
            }
        }
    }
    return wrapper;
}

static uint32_t rich_format_ancestor_state_bits(const EditingSurface* surface,
                                                DomNode* node) {
    if (!surface || !surface->owner || !node) return 0;
    uint32_t bits = 0;
    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    for (DomNode* cur = node; cur; cur = cur->parent) {
        if (cur->is_element()) {
            DomElement* elem = lam::dom_require_element(cur);
            if (elem) bits |= rich_format_bit_for_tag(elem->tag());
        }
        if (cur == owner_node) break;
    }
    return bits;
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
        uint32_t bit = rich_format_state_bit_for_intent(intent);
        if (!bit || !range->start.node) return false;

        uint32_t inherited = rich_format_ancestor_state_bits(surface,
            range->start.node);
        bool current = (state->editing.inline_format_state_mask & bit)
            ? ((state->editing.inline_format_state & bit) != 0)
            : ((inherited & bit) != 0);
        state->editing.inline_format_state_mask |= bit;
        if (current) {
            state->editing.inline_format_state &= ~bit;
        } else {
            state->editing.inline_format_state |= bit;
        }

        editing_interaction_set_active_surface(state, surface);
        if (log_mutation) {
            log_mutation(state, surface, intent, "format-typing-state",
                         0, 0, range->start.offset, range->end.offset,
                         log_user);
        }
        log_debug("editing_rich_default_format: toggled typing state bit=%u active=%d",
                  bit, (state->editing.inline_format_state & bit) ? 1 : 0);
        return true;
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
        case INPUT_INTENT_FORMAT_BACK_COLOR:
        case INPUT_INTENT_FORMAT_HILITE_COLOR:
            return "background-color";
        case INPUT_INTENT_FORMAT_FONT_NAME:
            return "font-family";
        case INPUT_INTENT_FORMAT_FONT_SIZE:
            return "font-size";
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

static DomElement* rich_remove_format_wrapper_for_range(
        DomRange* range,
        const EditingSurface* surface) {
    if (!range || !range->start.node || !range->end.node) return nullptr;

    if (range->start.node == range->end.node &&
        range->start.node->is_element()) {
        DomElement* elem = lam::dom_require_element(range->start.node);
        if (elem && elem != surface->owner &&
            rich_transaction_is_cleanup_inline_tag(elem->tag()) &&
            !dom_element_has_attribute(elem, "contenteditable") &&
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
    if (!parent || parent == surface->owner ||
        !rich_transaction_is_cleanup_inline_tag(parent->tag()) ||
        dom_element_has_attribute(parent, "contenteditable")) {
        return nullptr;
    }

    DomText* text = nullptr;
    return rich_format_range_selects_only_text_child(range, parent, &text)
        ? parent
        : nullptr;
}

bool editing_rich_default_remove_format(DocState* state,
                                        const EditingSurface* surface,
                                        const EditingIntent* intent,
                                        EditingRichMutationLogFn log_mutation,
                                        void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        state->dom_selection->range_count == 0 || !state->dom_selection->ranges[0]) {
        return false;
    }
    if (!editing_surface_is_rich(surface) || !surface->owner ||
        !surface->owner->doc || intent->type != INPUT_INTENT_FORMAT_REMOVE) {
        return false;
    }
    if (dom_range_collapsed(state->dom_selection->ranges[0])) {
        return false;
    }

    bool unwrapped = false;
    for (uint32_t pass = 0; pass < 64; pass++) {
        DomRange* range = state->dom_selection->ranges[0];
        DomElement* wrapper =
            rich_remove_format_wrapper_for_range(range, surface);
        if (!wrapper) break;
        if (!rich_format_unwrap_element(state, surface, intent, wrapper,
                                        log_mutation, log_user)) {
            return false;
        }
        unwrapped = true;
    }

    if (unwrapped) {
        log_debug("editing_rich_default_remove_format: unwrapped selected inline formatting");
    }
    return unwrapped;
}

bool editing_rich_default_select_all(DocState* state,
                                     const EditingSurface* surface,
                                     const EditingIntent* intent,
                                     EditingRichMutationLogFn log_mutation,
                                     void* log_user) {
    if (!state || !surface || !intent || !state->dom_selection ||
        intent->type != INPUT_INTENT_SELECT_ALL) {
        return false;
    }
    if (!editing_surface_is_rich(surface) || !surface->owner) {
        return false;
    }

    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    DomBoundary start = { owner_node, 0 };
    DomBoundary end = { owner_node, dom_node_boundary_length(owner_node) };
    const char* exc = nullptr;
    if (!state_store_set_selection(state, &start, &end, &exc)) {
        log_debug("editing_rich_default_select_all: selection rejected: %s",
                  exc ? exc : "?");
        return false;
    }

    editing_interaction_set_active_surface(state, surface);
    if (log_mutation) {
        log_mutation(state, surface, intent, "selectAll",
                     0, 0, start.offset, end.offset, log_user);
    }
    log_debug("editing_rich_default_select_all: selected native boundaries");
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

static const char* rich_skip_ascii_space(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                      *p == '\r' || *p == '\f')) {
        p++;
    }
    return p;
}

static const char* rich_trim_ascii_space_end(const char* start,
                                             const char* end) {
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\n' || end[-1] == '\r' ||
                          end[-1] == '\f')) {
        end--;
    }
    return end;
}

static bool rich_style_decl_is_text_align(const char* start,
                                          const char* end) {
    static const char prop[] = "text-align";
    const char* p = rich_skip_ascii_space(start, end);
    const char* colon = p;
    while (colon < end && *colon != ':') colon++;
    if (colon >= end) return false;
    const char* name_end = rich_trim_ascii_space_end(p, colon);
    size_t prop_len = sizeof(prop) - 1;
    if ((size_t)(name_end - p) != prop_len) return false;
    return strncasecmp(p, prop, prop_len) == 0;
}

static bool rich_set_text_align_style(DomElement* elem,
                                      const char* align_value) {
    if (!elem || !align_value || !align_value[0]) return false;
    const char* old_style = dom_element_get_attribute(elem, "style");
    size_t old_len = old_style ? strlen(old_style) : 0;
    StrBuf* sb = strbuf_new_cap(old_len + strlen(align_value) + 32);
    if (!sb) return false;

    const char* p = old_style ? old_style : "";
    const char* end = p + old_len;
    while (p < end) {
        const char* semi = p;
        while (semi < end && *semi != ';') semi++;
        const char* decl_start = rich_skip_ascii_space(p, semi);
        const char* decl_end = rich_trim_ascii_space_end(decl_start, semi);
        if (decl_start < decl_end &&
            !rich_style_decl_is_text_align(decl_start, decl_end)) {
            if (sb->length > 0) strbuf_append_char(sb, ' ');
            strbuf_append_str_n(sb, decl_start,
                (int)(decl_end - decl_start)); // INT_CAST_OK: inline style declaration slice.
            strbuf_append_char(sb, ';');
        }
        p = semi < end ? semi + 1 : end;
    }

    if (sb->length > 0) strbuf_append_char(sb, ' ');
    strbuf_append_str(sb, "text-align: ");
    strbuf_append_str(sb, align_value);
    strbuf_append_char(sb, ';');
    bool ok = dom_element_set_attribute(elem, "style", sb->str ? sb->str : "");
    strbuf_free(sb);
    return ok;
}

static DomElement* rich_create_host_boundary_style_block(
        DocState* state,
        const EditingSurface* surface,
        DomRange* range,
        const char* align_value) {
    if (!state || !surface || !surface->owner || !surface->owner->doc ||
        !range || !align_value ||
        range->start.node != range->end.node ||
        range->start.offset != range->end.offset) {
        return nullptr;
    }

    DomNode* owner_node = static_cast<DomNode*>(surface->owner);
    if (range->start.node != owner_node) return nullptr;

    MarkBuilder builder(surface->owner->doc->input);
    Item block_item = builder.element("div").final();
    Item br_item = builder.element("br").final();
    if (get_type_id(block_item) != LMD_TYPE_ELEMENT ||
        get_type_id(br_item) != LMD_TYPE_ELEMENT) {
        return nullptr;
    }

    DomElement* block = dom_element_create(surface->owner->doc, "div",
                                           block_item.element);
    DomElement* br = dom_element_create(surface->owner->doc, "br",
                                        br_item.element);
    if (!block || !br) return nullptr;
    if (!rich_set_text_align_style(block, align_value)) return nullptr;
    if (!rich_transaction_append_child_for_edit(block, static_cast<DomNode*>(br))) {
        return nullptr;
    }

    uint32_t child_idx = range->start.offset;
    if (child_idx > dom_node_boundary_length(owner_node)) {
        child_idx = dom_node_boundary_length(owner_node);
    }
    if (!rich_transaction_insert_child_for_edit(surface->owner,
            static_cast<DomNode*>(block), child_idx)) {
        return nullptr;
    }
    dom_mutation_post_insert(state, owner_node, static_cast<DomNode*>(block));
    return block;
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
        block = rich_create_host_boundary_style_block(state, surface, range,
                                                      align_value);
        if (!block) {
            log_debug("editing_rich_default_justify: no single block target");
            return false;
        }
    }
    if (!rich_set_text_align_style(block, align_value)) {
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

static DomText* rich_create_detached_text(DomDocument* doc,
                                          const char* data,
                                          uint32_t data_len) {
    if (!doc || !doc->input || !data) return nullptr;
    MarkBuilder builder(doc->input);
    String* s = builder.createDomTextString(data, data_len);
    if (!s) return nullptr;
    DomText* text = string_to_dom_text(s);
    if (!text) return nullptr;
    text->node_type = DOM_NODE_TEXT;
    text->parent = nullptr;
    text->next_sibling = nullptr;
    text->prev_sibling = nullptr;
    text->native_string = s;
    text->text = s->chars;
    text->length = s->len;
    text->content_type = DOM_TEXT_STRING;
    return text;
}

static DomNode* rich_create_formatted_insert_node(
        DomDocument* doc,
        const char* data,
        uint32_t data_len,
        uint32_t active_bits,
        DomText** out_text) {
    if (out_text) *out_text = nullptr;
    if (!doc || !data || data_len == 0) return nullptr;

    DomText* text = rich_create_detached_text(doc, data, data_len);
    if (!text) return nullptr;
    if (out_text) *out_text = text;

    static const uint32_t ordered_bits[] = {
        RICH_INLINE_FORMAT_BOLD,
        RICH_INLINE_FORMAT_ITALIC,
        RICH_INLINE_FORMAT_UNDERLINE,
        RICH_INLINE_FORMAT_STRIKETHROUGH,
        RICH_INLINE_FORMAT_SUBSCRIPT,
        RICH_INLINE_FORMAT_SUPERSCRIPT
    };

    DomNode* child = static_cast<DomNode*>(text);
    for (int32_t i = (int32_t)(sizeof(ordered_bits) / sizeof(ordered_bits[0])) - 1;
         i >= 0; i--) {
        uint32_t bit = ordered_bits[i];
        if ((active_bits & bit) == 0) continue;
        const char* tag_name = rich_format_tag_name_for_bit(bit);
        DomElement* wrapper = rich_create_native_element(doc, tag_name);
        if (!wrapper) return nullptr;
        if (!rich_transaction_append_child_for_edit(wrapper, child)) {
            return nullptr;
        }
        child = static_cast<DomNode*>(wrapper);
    }
    return child;
}

static bool rich_split_format_wrapper_for_plain_insert(
        DocState* state,
        DomElement* wrapper,
        DomNode* reference,
        DomElement** io_parent,
        uint32_t* io_index) {
    if (!state || !wrapper || !wrapper->parent || !wrapper->parent->is_element() ||
        !io_parent || !io_index) {
        return false;
    }

    DomNode* wrapper_node = static_cast<DomNode*>(wrapper);
    DomElement* parent = lam::dom_require_element(wrapper_node->parent);
    if (!parent) return false;
    uint32_t wrapper_idx = dom_node_child_index(wrapper_node);
    if (wrapper_idx == (uint32_t)-1) return false;

    if (reference && reference->parent == wrapper_node) {
        DomElement* right_wrapper =
            rich_create_native_element(wrapper->doc, wrapper->tag_name);
        if (!right_wrapper) return false;

        DomNode* child = reference;
        while (child) {
            DomNode* next = child->next_sibling;
            dom_mutation_pre_remove(state, child);
            if (!wrapper_node->remove_child(child)) return false;
            if (!rich_transaction_append_child_for_edit(right_wrapper, child)) {
                return false;
            }
            dom_mutation_post_insert(state, static_cast<DomNode*>(right_wrapper), child);
            child = next;
        }

        uint32_t insert_right_at = wrapper_idx + 1;
        if (!rich_transaction_insert_child_for_edit(parent,
                static_cast<DomNode*>(right_wrapper), insert_right_at)) {
            return false;
        }
        dom_mutation_post_insert(state, static_cast<DomNode*>(parent),
                                 static_cast<DomNode*>(right_wrapper));
    }

    bool wrapper_empty = !wrapper->first_child;
    if (wrapper_empty) {
        dom_mutation_pre_remove(state, wrapper_node);
        if (!rich_transaction_remove_child_for_edit(parent, wrapper_node)) {
            return false;
        }
        *io_index = wrapper_idx;
    } else {
        *io_index = wrapper_idx + 1;
    }
    *io_parent = parent;
    return true;
}

static bool rich_insert_text_with_typing_state(
        DocState* state,
        const EditingSurface* surface,
        const EditingIntent* intent,
        DomText* text,
        uint32_t start,
        uint32_t data_len,
        EditingRichMutationLogFn log_mutation,
        void* log_user) {
    if (!state || !surface || !intent || !text || !text->parent ||
        !text->parent->is_element() || !intent->data || data_len == 0 ||
        state->editing.inline_format_state_mask == 0) {
        return false;
    }

    DomElement* parent = lam::dom_require_element(text->parent);
    if (!parent || !parent->doc) return false;

    uint32_t inherited = rich_format_ancestor_state_bits(surface,
        static_cast<DomNode*>(text));
    uint32_t desired = inherited;
    desired &= ~state->editing.inline_format_state_mask;
    desired |= state->editing.inline_format_state &
        state->editing.inline_format_state_mask;

    const char* old_text = text->text ? text->text : "";
    uint32_t old_len = text->length > 0
        ? (uint32_t)text->length
        : (uint32_t)strlen(old_text);
    if (start > old_len) start = old_len;

    uint32_t split_u16 = dom_text_utf8_to_utf16(text, start);
    DomNode* reference = nullptr;
    if (start == 0) {
        reference = static_cast<DomNode*>(text);
    } else if (start >= old_len) {
        reference = static_cast<DomNode*>(text)->next_sibling;
    } else {
        DomText* right = rich_transaction_split_text_for_edit(state, parent,
            text, start, split_u16);
        if (!right) return false;
        reference = static_cast<DomNode*>(right);
    }

    DomElement* insert_parent = parent;
    uint32_t insert_idx = rich_transaction_child_index_or_end(parent, reference);
    uint32_t disabled = inherited & ~desired;
    if (disabled != 0) {
        DomNode* owner_node = static_cast<DomNode*>(surface->owner);
        DomNode* cur = static_cast<DomNode*>(parent);
        DomNode* active_reference = reference;
        while (cur && cur != owner_node) {
            if (!cur->is_element()) break;
            DomElement* elem = lam::dom_require_element(cur);
            uint32_t bit = elem ? rich_format_bit_for_tag(elem->tag()) : 0;
            DomNode* next_ancestor = cur->parent;
            if (bit && (disabled & bit)) {
                if (!rich_split_format_wrapper_for_plain_insert(
                        state, elem, active_reference, &insert_parent,
                        &insert_idx)) {
                    return false;
                }
                active_reference = static_cast<DomNode*>(elem)->next_sibling;
            }
            cur = next_ancestor;
        }
    }

    uint32_t container_bits = rich_format_ancestor_state_bits(surface,
        static_cast<DomNode*>(insert_parent));
    uint32_t wrapper_bits = desired & ~container_bits;
    DomText* inserted_text = nullptr;
    DomNode* inserted = rich_create_formatted_insert_node(parent->doc,
        intent->data, data_len, wrapper_bits, &inserted_text);
    if (!inserted || !inserted_text) return false;

    if (!rich_transaction_insert_child_for_edit(insert_parent, inserted,
            insert_idx)) {
        return false;
    }
    dom_mutation_post_insert(state, static_cast<DomNode*>(insert_parent),
                             inserted);
    DomElement* normalized_inserted = nullptr;
    if (inserted->is_element()) {
        normalized_inserted = rich_format_normalize_adjacent(state,
            lam::dom_require_element(inserted));
    }

    DomText* live_inserted_text = inserted_text;
    if (!live_inserted_text->parent) {
        DomNode* live_inserted =
            normalized_inserted && normalized_inserted !=
                lam::dom_require_element(inserted)
            ? static_cast<DomNode*>(normalized_inserted)
            : rich_transaction_live_child_at_index(insert_parent, insert_idx,
                  normalized_inserted ? static_cast<DomNode*>(normalized_inserted)
                                      : inserted);
        if (live_inserted) {
            if (live_inserted->is_text()) {
                live_inserted_text = lam::dom_require_text(live_inserted);
            } else {
                DomText* descendant = editing_rich_find_text_descendant(
                    live_inserted, true);
                if (descendant) live_inserted_text = descendant;
            }
        }
    }

    if (!rich_transaction_collapse_text_caret(state, live_inserted_text, data_len)) {
        return false;
    }
    EditingSurface live_surface;
    if (editing_surface_from_target(static_cast<View*>(live_inserted_text),
            &live_surface) && editing_surface_is_rich(&live_surface)) {
        editing_interaction_set_active_surface(state, &live_surface);
    } else {
        editing_interaction_set_active_surface(state, surface);
    }
    if (log_mutation) {
        log_mutation(state, surface, intent, "insert-typing-state",
                     0, data_len, data_len, data_len, log_user);
    }
    log_debug("rich_insert_text_with_typing_state: inserted %u bytes with bits=%u",
              data_len, desired);
    return true;
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
        intent->type != INPUT_INTENT_DELETE_WORD_FORWARD &&
        intent->type != INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD &&
        intent->type != INPUT_INTENT_DELETE_SOFT_LINE_FORWARD &&
        intent->type != INPUT_INTENT_DELETE_HARD_LINE_BACKWARD &&
        intent->type != INPUT_INTENT_DELETE_HARD_LINE_FORWARD) {
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
            if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD) {
                DomBoundary caret;
                if (rich_transaction_boundary_caret_from_state(state, &caret) &&
                    rich_transaction_delete_previous_atomic_at_boundary(
                        state, fallback_surface_ptr, caret,
                        log_mutation, intent, log_user)) {
                    return true;
                }
            }
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
               intent->type == INPUT_INTENT_DELETE_WORD_FORWARD ||
               intent->type == INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD ||
               intent->type == INPUT_INTENT_DELETE_SOFT_LINE_FORWARD ||
               intent->type == INPUT_INTENT_DELETE_HARD_LINE_BACKWARD ||
               intent->type == INPUT_INTENT_DELETE_HARD_LINE_FORWARD) {
        data = "";
        if (start == end) {
            if (intent->type == INPUT_INTENT_DELETE_CONTENT_BACKWARD) {
                if (rich_transaction_join_previous_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_previous_inline_fragment_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_previous_empty_br_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_previous_trailing_br_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_child_block_with_parent_text(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_parent_text_with_child_block(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_nested_list_item_with_parent(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_join_previous_row_cell(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (rich_transaction_delete_previous_atomic_whitespace_before_text(
                        state, fallback_surface_ptr, text, start,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (start == 0 && rich_transaction_delete_previous_inline_wrapper(
                        state, fallback_surface_ptr, text,
                        log_mutation, intent, log_user)) {
                    return true;
                }
                if (start == 0 && rich_transaction_delete_previous_atomic_before_text(
                        state, fallback_surface_ptr, text,
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
            } else if (intent->type == INPUT_INTENT_DELETE_SOFT_LINE_BACKWARD ||
                       intent->type == INPUT_INTENT_DELETE_HARD_LINE_BACKWARD) {
                uint32_t line_start = te_line_start(old_text, old_len, start);
                if (line_start >= start) return false;
                start = line_start;
            } else if (intent->type == INPUT_INTENT_DELETE_SOFT_LINE_FORWARD ||
                       intent->type == INPUT_INTENT_DELETE_HARD_LINE_FORWARD) {
                uint32_t line_end = te_line_end(old_text, old_len, end);
                if (line_end <= end) return false;
                end = line_end;
            }
        }
    }

    uint32_t data_len = (uint32_t)strlen(data);
    EditingSurface insert_surface;
    const EditingSurface* insert_surface_ptr = fallback_surface_ptr;
    if (!insert_surface_ptr &&
        editing_surface_from_target(static_cast<View*>(text), &insert_surface) &&
        editing_surface_is_rich(&insert_surface)) {
        insert_surface_ptr = &insert_surface;
    }
    if (intent->type == INPUT_INTENT_INSERT_TEXT && start == end &&
        data_len > 0 && state->editing.inline_format_state_mask != 0 &&
        rich_insert_text_with_typing_state(state, insert_surface_ptr, intent,
            text, start, data_len, log_mutation, log_user)) {
        return true;
    }

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
