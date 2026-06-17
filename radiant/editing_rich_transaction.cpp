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
