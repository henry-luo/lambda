#include "handler.hpp"
#include "state_store.hpp"

#include "../lib/log.h"
#include "../lib/utf.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_parser.hpp"
DomDocument* show_html_doc(Url *base, char* doc_filename, int viewport_width, int viewport_height);
View* layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
extern "C" void process_document_font_faces(UiContext* uicon, DomDocument* doc);
void to_repaint();

// Forward declarations for event targeting
void target_html_doc(EventContext* evcon, ViewTree* view_tree);
void target_block_view(EventContext* evcon, ViewBlock* block);
void target_inline_view(EventContext* evcon, ViewSpan* view_span);
void target_text_view(EventContext* evcon, ViewText* text);
void scrollpane_scroll(EventContext* evcon, ScrollPane* sp);
bool scrollpane_target(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_up(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block);
void scrollpane_drag(EventContext* evcon, ViewBlock* block);
void update_scroller(ViewBlock* block, float content_width, float content_height);

void target_children(EventContext* evcon, View* view) {
    do {
        if (view->is_block()) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->position && block->position->position != CSS_VALUE_STATIC) {
                // skip absolute/fixed positioned block
            } else {
                target_block_view(evcon, block);
            }
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            target_inline_view(evcon, span);
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            target_text_view(evcon, text);
        }
        view = view->next();
    } while (view && !evcon->target);
}

void target_text_view(EventContext* evcon, ViewText* text) {
    unsigned char* str = text->text_data();
    TextRect *text_rect = text->rect;
    MousePositionEvent* event = &evcon->event.mouse_position;

    NEXT_RECT:
    float x = evcon->block.x + text_rect->x, y = evcon->block.y + text_rect->y;
    float rect_right = x + text_rect->width;
    float rect_bottom = y + text_rect->height;

    log_debug("target text:'%t' start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d",
        str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, evcon->block.x);

    // First check if mouse is within the text rect bounds (use rect height, not char height)
    if (x <= event->x && event->x < rect_right && y <= event->y && event->y < rect_bottom) {
        // Mouse is in this text rect - set target and return
        log_debug("hit on text rect at (%d, %d)", event->x, event->y);
        evcon->target = text;
        evcon->target_text_rect = text_rect;
        return;
    }

    assert(text_rect->next != text_rect);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
}

void target_inline_view(EventContext* evcon, ViewSpan* view_span) {
    log_enter();
    FontBox pa_font = evcon->font;
    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(evcon->ui_context, &evcon->font, view_span->font);
        }
        target_children(evcon, view);
    }
    evcon->font = pa_font;
    log_leave();
}

void target_block_view(EventContext* evcon, ViewBlock* block) {
    log_enter();
    BlockBlot pa_block = evcon->block;  FontBox pa_font = evcon->font;
    evcon->block.x = pa_block.x + block->x;  evcon->block.y = pa_block.y + block->y;
    MousePositionEvent* event = &evcon->event.mouse_position;
    // target the scrollbars first
    View* view = NULL;
    bool hover = false;
    if (block->scroller && block->scroller->pane) {
        hover = scrollpane_target(evcon, block);
        if (hover) {
            log_debug("hit on block scroll: %s", block->node_name());
            evcon->target = (View*)block;
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
            goto RETURN;
        }
        else {
            log_debug("hit not on block scroll");
        }
        // setup scrolling offset
        evcon->block.x -= block->scroller->pane->h_scroll_position;
        evcon->block.y -= block->scroller->pane->v_scroll_position;
    }

    // Check if this block contains an embedded iframe document
    // If so, target into the iframe's document instead of treating it as a normal block
    if (block->embed && block->embed->doc) {
        DomDocument* iframe_doc = block->embed->doc;
        if (iframe_doc->view_tree && iframe_doc->view_tree->root) {
            log_debug("targeting into iframe embedded document: %s", block->node_name());

            // Save current state
            View* prev_target = evcon->target;

            // Target into the embedded document's view tree
            // The coordinate system is already set up correctly (evcon->block.x/y)
            // since we added block->x and block->y above
            target_html_doc(evcon, iframe_doc->view_tree);

            // If we found a target inside the iframe, we're done
            if (evcon->target && evcon->target != prev_target) {
                log_debug("found target inside iframe: %s",
                    evcon->target->is_element() ? ((ViewElement*)evcon->target)->node_name() : "text");
                goto RETURN;
            }

            log_debug("no target found inside iframe, will target iframe block itself");
        }
    }

    // target absolute/fixed positioned children
    if (block->position && block->position->first_abs_child) {
        ViewBlock* abs_child = block->position->first_abs_child;
        do {
            // todo: should target based on z-index order
            log_debug("targetting positioned child block: %s", abs_child->node_name());
            target_block_view(evcon, abs_child);
            if (evcon->target) { goto RETURN; }
            abs_child = abs_child->position->next_abs_sibling;
        } while (abs_child);
    }

    // target static positioned children
    view = block->first_child;
    if (view) {
        if (block->font) {
            setup_font(evcon->ui_context, &evcon->font, block->font);
        }
        target_children(evcon, view);
    }

    RETURN:
    // Only restore block position if no target was found
    // When a target is found, keep block at the parent's position for coordinate calculations
    if (!evcon->target) {
        evcon->block = pa_block;
    }
    evcon->font = pa_font;

    if (!evcon->target) { // check the block itself
        float x = evcon->block.x, y = evcon->block.y;
        if (x <= event->x && event->x < x + block->width &&
            y <= event->y && event->y < y + block->height) {
            log_debug("hit on block: %s", block->node_name());
            evcon->target = (View*)block;
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
        }
        else {
            log_debug("hit not on block: %s, x: %.1f, y: %.1f, ex: %.1f, ey: %.1f, right: %.1f, bottom: %.1f",
                block->node_name(), x, y, event->x, event->y, x + block->width, y + block->height);
        }
    }
    log_leave();
}

void target_html_doc(EventContext* evcon, ViewTree* view_tree) {
    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("target root view");
        FontBox pa_font = evcon->font;
        FontProp* default_font = view_tree->html_version == HTML5 ? &evcon->ui_context->default_font : &evcon->ui_context->legacy_default_font;
        log_debug("target_html_doc default font: %s, html version: %d", default_font->family, view_tree->html_version);
        setup_font(evcon->ui_context, &evcon->font, default_font);
        target_block_view(evcon, (ViewBlock*)root_view);
        evcon->font = pa_font;
    }
    else {
        log_error("Invalid root view: %d", root_view ? root_view->view_type : -1);
    }
}

ArrayList* build_view_stack(EventContext* evcon, View* view) {
    ArrayList* list = arraylist_new(100);
    while (view) {
        arraylist_prepend(list, view);
        view = (View*)view->parent;
    }
    return list;
}

void fire_text_event(EventContext* evcon, ViewText* text) {
    log_debug("fire text event");
    if (evcon->new_cursor == CSS_VALUE_AUTO) {
        log_debug("set text cursor");
        evcon->new_cursor = CSS_VALUE_TEXT;
    }
    else {
        log_debug("cursor already set as %d", evcon->new_cursor);
    }
}

void fire_inline_event(EventContext* evcon, ViewSpan* span) {
    log_debug("fire inline event");
    if (span->in_line && span->in_line->cursor) {
        evcon->new_cursor = span->in_line->cursor;
    }
    uintptr_t name = span->tag();
    log_debug("fired at view %s", span->node_name());
    if (name == HTM_TAG_A) {
        log_debug("fired at anchor tag");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            log_debug("mouse down at anchor tag");
            const char* href = span->get_attribute("href");
            if (href) {
                log_debug("got anchor href: %s", href);
                evcon->new_url = (char*)href;
                const char* target = span->get_attribute("target");
                if (target) {
                    log_debug("got anchor target: %s", target);
                    evcon->new_target = (char*)target;
                }
                else {
                    log_debug("no anchor target found");
                }
            }
        }
    }
}

void fire_block_event(EventContext* evcon, ViewBlock* block) {
    log_debug("fire block event");
    // fire as inline view first
    fire_inline_event(evcon, (ViewSpan*)block);
    if (block->scroller && block->scroller->pane) {
        if (evcon->event.type == RDT_EVENT_SCROLL) {
            scrollpane_scroll(evcon, block->scroller->pane);
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_DOWN &&
            (block->scroller->pane->is_h_hovered || block->scroller->pane->is_v_hovered)) {
            scrollpane_mouse_down(evcon, block);
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_UP) {
            scrollpane_mouse_up(evcon, block);
        }
        else if (evcon->event.type == RDT_EVENT_MOUSE_DRAG &&
            (block->scroller->pane->h_is_dragging || block->scroller->pane->v_is_dragging)) {
            scrollpane_drag(evcon, block);
        }
    }
}

void fire_events(EventContext* evcon, ArrayList* target_list) {
    int stack_size = target_list->length;
    for (int i = 0; i < stack_size; i++) {
        log_debug("fire event to view no. %d", i);
        View* view = (View*)target_list->data[i];
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_LIST_ITEM) {
            fire_block_event(evcon, (ViewBlock*)view);
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            fire_inline_event(evcon, (ViewSpan*)view);
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            fire_text_event(evcon, (ViewText*)view);
        }
        else {
            log_error("Invalid fire view type: %d", view->view_type);
        }
    }
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    evcon->event = *event;
    // load default font Arial, size 16 px
    setup_font(uicon, &evcon->font, &uicon->default_font);
    evcon->new_cursor = CSS_VALUE_AUTO;
    if (!uicon->document->state) {
        // Create the new RadiantState with in-place mode
        uicon->document->state = radiant_state_create(uicon->document->pool, STATE_MODE_IN_PLACE);
        if (uicon->document->state) {
            log_debug("event_context_init: created RadiantState for document");
        }
    }
}

void event_context_cleanup(EventContext* evcon) {
}

// ============================================================================
// Interaction State Updates
// ============================================================================

/**
 * Helper to update element's pseudo_state bitmask along with state store
 * Also schedules reflow if the pseudo-state change may affect layout
 */
static void sync_pseudo_state(View* view, uint32_t pseudo_flag, bool set) {
    if (!view || !view->is_element()) return;

    DomElement* element = (DomElement*)view;
    uint32_t old_state = element->pseudo_state;

    if (set) {
        dom_element_set_pseudo_state(element, pseudo_flag);
    } else {
        dom_element_clear_pseudo_state(element, pseudo_flag);
    }

    // If state actually changed, schedule potential reflow
    if (element->pseudo_state != old_state && element->doc && element->doc->state) {
        RadiantState* state = (RadiantState*)element->doc->state;

        // Pseudo-states that can affect layout (need reflow, not just repaint)
        bool affects_layout = (pseudo_flag == PSEUDO_STATE_HOVER ||
                               pseudo_flag == PSEUDO_STATE_ACTIVE ||
                               pseudo_flag == PSEUDO_STATE_FOCUS ||
                               pseudo_flag == PSEUDO_STATE_CHECKED ||
                               pseudo_flag == PSEUDO_STATE_DISABLED);

        if (affects_layout) {
            // Schedule subtree reflow (element and descendants may change)
            reflow_schedule(state, view, REFLOW_SUBTREE, CHANGE_PSEUDO_STATE);
        }

        // Always mark for repaint
        dirty_mark_element(state, view);
        state->is_dirty = true;
    }
}

/**
 * Update hover state when mouse moves to a new target
 * Sets :hover on target and all ancestors, clears :hover on previous target
 */
void update_hover_state(EventContext* evcon, View* new_target) {
    RadiantState* state = (RadiantState*)evcon->ui_context->document->state;
    if (!state) return;

    View* prev_hover = (View*)state->hover_target;

    if (prev_hover == new_target) return;  // no change

    // Clear :hover on previous target and its ancestors
    if (prev_hover) {
        View* node = prev_hover;
        while (node) {
            state_set_bool(state, node, STATE_HOVER, false);
            sync_pseudo_state(node, PSEUDO_STATE_HOVER, false);
            node = (View*)node->parent;
        }
        log_debug("update_hover_state: cleared hover on %p", prev_hover);
    }

    // Set :hover on new target and its ancestors
    if (new_target) {
        View* node = new_target;
        while (node) {
            state_set_bool(state, node, STATE_HOVER, true);
            sync_pseudo_state(node, PSEUDO_STATE_HOVER, true);
            node = (View*)node->parent;
        }
        log_debug("update_hover_state: set hover on %p", new_target);
    }

    state->hover_target = new_target;
    state->needs_repaint = true;
}

/**
 * Update active state on mouse down/up
 */
void update_active_state(EventContext* evcon, View* target, bool is_active) {
    RadiantState* state = (RadiantState*)evcon->ui_context->document->state;
    if (!state) return;

    if (is_active) {
        // Set :active on target and ancestors
        View* node = target;
        while (node) {
            state_set_bool(state, node, STATE_ACTIVE, true);
            sync_pseudo_state(node, PSEUDO_STATE_ACTIVE, true);
            node = (View*)node->parent;
        }
        state->active_target = target;
        log_debug("update_active_state: set active on %p", target);
    } else {
        // Clear :active on previous active target
        View* prev_active = (View*)state->active_target;
        if (prev_active) {
            View* node = prev_active;
            while (node) {
                state_set_bool(state, node, STATE_ACTIVE, false);
                sync_pseudo_state(node, PSEUDO_STATE_ACTIVE, false);
                node = (View*)node->parent;
            }
        }
        state->active_target = NULL;
        log_debug("update_active_state: cleared active");
    }

    state->needs_repaint = true;
}

/**
 * Check if an element is focusable
 */
bool is_view_focusable(View* view) {
    if (!view) return false;

    // Elements that are focusable by default:
    // - <a> with href
    // - <button>
    // - <input> (except hidden)
    // - <select>
    // - <textarea>
    // - elements with tabindex >= 0

    if (view->is_element()) {
        ViewElement* elem = (ViewElement*)view;
        uint32_t tag = elem->tag();

        switch (tag) {
        case HTM_TAG_A:
            // <a> is focusable if it has href
            return elem->get_attribute("href") != NULL;
        case HTM_TAG_BUTTON:
        case HTM_TAG_SELECT:
        case HTM_TAG_TEXTAREA:
            return true;
        case HTM_TAG_INPUT: {
            // Input is focusable unless type="hidden"
            const char* type = elem->get_attribute("type");
            return !type || strcmp(type, "hidden") != 0;
        }
        default:
            // Check for tabindex attribute
            const char* tabindex = elem->get_attribute("tabindex");
            if (tabindex) {
                int ti = atoi(tabindex);
                return ti >= 0;
            }
            break;
        }
    }

    return false;
}

/**
 * Propagate :focus-within pseudo-state up the ancestor chain
 */
static void propagate_focus_within(View* view, bool set) {
    View* ancestor = view ? view->parent : nullptr;
    while (ancestor) {
        sync_pseudo_state(ancestor, PSEUDO_STATE_FOCUS_WITHIN, set);
        ancestor = ancestor->parent;
    }
}

/**
 * Update focus state when an element gains/loses focus
 * @param from_keyboard true if focus change was triggered by keyboard (Tab key, etc.)
 */
void update_focus_state(EventContext* evcon, View* new_focus, bool from_keyboard) {
    RadiantState* state = (RadiantState*)evcon->ui_context->document->state;
    if (!state) return;

    View* prev_focus = focus_get(state);

    if (prev_focus == new_focus) return;  // no change

    // Use the focus API to handle all state updates
    if (new_focus) {
        focus_set(state, new_focus, from_keyboard);

        // Sync DOM pseudo-states for previous focus
        if (prev_focus) {
            sync_pseudo_state(prev_focus, PSEUDO_STATE_FOCUS, false);
            sync_pseudo_state(prev_focus, PSEUDO_STATE_FOCUS_VISIBLE, false);
            // Clear :focus-within from previous ancestor chain
            propagate_focus_within(prev_focus, false);
        }

        // Set :focus on new element
        sync_pseudo_state(new_focus, PSEUDO_STATE_FOCUS, true);

        // Set :focus-visible only for keyboard navigation
        if (from_keyboard) {
            sync_pseudo_state(new_focus, PSEUDO_STATE_FOCUS_VISIBLE, true);
        }

        // Propagate :focus-within up the ancestor chain
        propagate_focus_within(new_focus, true);

        log_debug("update_focus_state: set focus on %p (keyboard=%d, focus-visible=%d)",
                  new_focus, from_keyboard, from_keyboard);
    } else {
        focus_clear(state);

        if (prev_focus) {
            sync_pseudo_state(prev_focus, PSEUDO_STATE_FOCUS, false);
            sync_pseudo_state(prev_focus, PSEUDO_STATE_FOCUS_VISIBLE, false);
            // Clear :focus-within from ancestor chain
            propagate_focus_within(prev_focus, false);
        }

        log_debug("update_focus_state: cleared focus");
    }
}

/**
 * Update drag state
 */
void update_drag_state(EventContext* evcon, View* target, bool is_dragging) {
    RadiantState* state = (RadiantState*)evcon->ui_context->document->state;
    if (!state) return;

    state->drag_target = is_dragging ? target : NULL;
    state->is_dirty = true;

    log_debug("update_drag_state: dragging=%d, target=%p", is_dragging, target);
}

// find iframe by name and set new src using selector
DomNode* set_iframe_src_by_name(DomElement *document, const char *target_name, const char *new_src) {
    if (!document || !target_name || !new_src) {
        log_error("Invalid parameters to set_iframe_src_by_name");
        return NULL;
    }
    // get memory pool from document
    Pool* pool = document->doc ? document->doc->pool : nullptr;
    if (!pool) {
        log_error("Document has no memory pool");
        return NULL;
    }

    // construct selector string: iframe[name="target_name"]
    char selector_str[256];
    int len = snprintf(selector_str, sizeof(selector_str), "iframe[name=\"%s\"]", target_name);
    if (len < 0 || len >= (int)sizeof(selector_str)) {
        log_error("Selector string too long");
        return NULL;
    }

    log_debug("parsing iframe selector: %s", selector_str);
    // tokenize the selector
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(selector_str, (size_t)len, pool, &token_count);
    if (!tokens || token_count == 0) {
        log_error("Failed to tokenize selector");
        return NULL;
    }
    // parse the selector
    int pos = 0;
    CssSelector* selector = css_parse_selector_with_combinators(tokens, &pos, (int)token_count, pool);
    if (!selector) {
        log_error("Failed to parse selector");
        return NULL;
    }
    // create selector matcher
    SelectorMatcher* matcher = selector_matcher_create(pool);
    if (!matcher) {
        log_error("Failed to create selector matcher");
        return NULL;
    }

    // find the iframe element matching the selector
    DomElement* iframe_element = selector_matcher_find_first(matcher, selector, document);
    if (iframe_element) {
        log_debug("Found iframe with name='%s', setting src to: %s", target_name, new_src);
        // set the src attribute
        if (!dom_element_set_attribute(iframe_element, "src", new_src)) {
            log_error("Failed to set src attribute");
            selector_matcher_destroy(matcher);
            return NULL;
        }
        log_debug("iframe src attribute set successfully");
        selector_matcher_destroy(matcher);
        return iframe_element;  // Return DomElement* (which is a DomNodeBase*)
    }

    log_debug("No iframe found with name='%s'", target_name);
    selector_matcher_destroy(matcher);
    return NULL;
}

// find the sub-view that matches the given node
View* find_view(View* view, DomNode* node) {
    // Compare if the view's node matches the target node directly
    if (view == node) { return view; }

    if (view->is_group()) {
        ViewElement* group = (ViewElement*)view;
        View* child = group->first_child;
        while (child) {
            View* found = find_view(child, node);
            if (found) { return found; }
            child = child->next_sibling;
        }
    }
    return NULL;
}

/**
 * Calculate absolute window position from view-relative coordinates.
 * Walks up the parent chain accumulating block positions.
 * @param view The view whose coordinate system the position is relative to
 * @param rel_x X coordinate relative to view's parent block
 * @param rel_y Y coordinate relative to view's parent block
 * @param iframe_offset_x Additional X offset for iframe content
 * @param iframe_offset_y Additional Y offset for iframe content
 * @param out_abs_x Output: absolute X in window coordinates
 * @param out_abs_y Output: absolute Y in window coordinates
 */
void view_to_absolute_position(View* view, float rel_x, float rel_y,
    float iframe_offset_x, float iframe_offset_y,
    float* out_abs_x, float* out_abs_y) {

    float abs_x = rel_x;
    float abs_y = rel_y;

    // Walk up from view's parent to accumulate block positions
    View* parent = view->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            abs_x += ((ViewBlock*)parent)->x;
            abs_y += ((ViewBlock*)parent)->y;
        }
        parent = parent->parent;
    }

    // Add iframe offset
    abs_x += iframe_offset_x;
    abs_y += iframe_offset_y;

    *out_abs_x = abs_x;
    *out_abs_y = abs_y;
}

/**
 * Calculate character offset from mouse click position within a text rect
 * Returns the byte offset closest to the click position, aligned to UTF-8 character boundaries
 */
int calculate_char_offset_from_position(EventContext* evcon, ViewText* text,
    TextRect* rect, int mouse_x, int mouse_y) {
    unsigned char* str = text->text_data();
    float x = evcon->block.x + rect->x;
    float y = evcon->block.y + rect->y;

    unsigned char* p = str + rect->start_index;
    unsigned char* end = p + max(rect->length, 0);
    int byte_offset = rect->start_index;  // track byte offset for return value

    float pixel_ratio = (evcon->ui_context && evcon->ui_context->pixel_ratio > 0)
        ? evcon->ui_context->pixel_ratio : 1.0f;

    // Get letter-spacing from font style (same as used in layout)
    float letter_spacing = evcon->font.style ? evcon->font.style->letter_spacing : 0.0f;

    bool has_space = false;
    float prev_x = x;

    log_debug("calculate_char_offset: mouse_x=%d, start x=%.1f, rect.width=%.1f, rect.length=%d, block.x=%.1f, rect.x=%.1f",
              mouse_x, x, rect->width, rect->length, evcon->block.x, rect->x);

    while (p < end) {
        float wd = 0;
        int bytes = 1;  // number of bytes for current character

        // Skip newlines and carriage returns - they don't have visual width
        if (*p == '\n' || *p == '\r') {
            // At end of visual content - treat rest as trailing whitespace
            break;
        }

        if (is_space(*p)) {
            if (has_space) {
                // Consecutive spaces are collapsed - skip without adding width
                p++;
                byte_offset++;
                continue;
            }
            has_space = true;
            wd = evcon->font.style->space_width;
            bytes = 1;  // spaces are always single byte
        } else {
            has_space = false;
            // Decode UTF-8 codepoint to handle multi-byte characters
            uint32_t codepoint;
            bytes = utf8_to_codepoint(p, &codepoint);
            if (bytes <= 0) {
                // Invalid UTF-8 sequence, skip single byte
                bytes = 1;
                codepoint = *p;
            }
            // Use load_glyph to match layout calculation
            FT_GlyphSlot glyph = load_glyph(evcon->ui_context, evcon->font.ft_face, evcon->font.style, codepoint, false);
            if (!glyph) {
                log_error("Could not load codepoint U+%04X", codepoint);
                p += bytes;
                byte_offset += bytes;
                continue;
            }
            wd = glyph->advance.x / 64.0 / pixel_ratio;
        }

        // Add letter-spacing (applied after each character except the last)
        unsigned char* next_p = p + bytes;
        if (next_p < end && *next_p != '\n' && *next_p != '\r') {
            wd += letter_spacing;
        }

        float char_mid = x + wd / 2.0f;

        // If mouse is before the midpoint of this character, return current byte offset
        // (caret should be placed before this character)
        if (mouse_x < char_mid) {
            log_debug("calculate_char_offset: matched at byte_offset %d", byte_offset);
            return byte_offset;
        }

        prev_x = x;
        x += wd;
        p += bytes;
        byte_offset += bytes;
    }

    log_debug("calculate_char_offset: end of text, returning byte_offset=%d", byte_offset);
    // Mouse is after all characters - return end offset
    return byte_offset;
}

/**
 * Calculate visual position (x, y, height) from byte offset within a text rect
 * The target_offset is a byte offset aligned to UTF-8 character boundaries
 * Returns the x position relative to the text rect's origin
 */
void calculate_position_from_char_offset(EventContext* evcon, ViewText* text,
    TextRect* rect, int target_offset, float* out_x, float* out_y, float* out_height) {

    unsigned char* str = text->text_data();
    float x = rect->x;  // relative to block
    float y = rect->y;

    unsigned char* p = str + rect->start_index;
    unsigned char* end = p + max(rect->length, 0);
    int byte_offset = rect->start_index;  // track byte offset
    float pixel_ratio = (evcon->ui_context && evcon->ui_context->pixel_ratio > 0)
        ? evcon->ui_context->pixel_ratio : 1.0f;
    bool has_space = false;

    // Debug: log initial state
    log_debug("[CALC-POS] target_offset=%d, rect->x=%.1f, rect->start_index=%d, pixel_ratio=%.1f, y_ppem=%d",
        target_offset, rect->x, rect->start_index, pixel_ratio,
        evcon->font.ft_face ? evcon->font.ft_face->size->metrics.y_ppem : -1);
    
    while (p < end && byte_offset < target_offset) {
        float wd = 0;
        int bytes = 1;  // number of bytes for current character
        
        if (is_space(*p)) {
            if (has_space) {
                p++;
                byte_offset++;
                continue;
            }
            has_space = true;
            wd = evcon->font.style->space_width;
            bytes = 1;
        } else {
            has_space = false;
            // Decode UTF-8 codepoint to handle multi-byte characters
            uint32_t codepoint;
            bytes = utf8_to_codepoint(p, &codepoint);
            if (bytes <= 0) {
                // Invalid UTF-8 sequence, skip single byte
                bytes = 1;
                codepoint = *p;
            }
            FT_Int32 load_flags = (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
            if (FT_Load_Char(evcon->font.ft_face, codepoint, load_flags)) {
                p += bytes;
                byte_offset += bytes;
                continue;
            }
            wd = evcon->font.ft_face->glyph->advance.x / 64.0f / pixel_ratio;
            
            // Debug: log per-character advance for first 15 chars
            if (byte_offset - rect->start_index < 30) {
                log_debug("[CALC-POS] byte_offset=%d codepoint=U+%04X x=%.1f wd=%.1f (raw advance=%.1f)",
                    byte_offset, codepoint,
                    x, wd, evcon->font.ft_face->glyph->advance.x / 64.0f);
            }
        }
        x += wd;
        p += bytes;
        byte_offset += bytes;
    }
    log_debug("[CALC-POS] final x=%.1f for target_offset=%d", x, target_offset);

    *out_x = x;
    *out_y = y;
    *out_height = rect->height;  // use rect height as caret height
}

// ============================================================================
// Main Event Handler
// ============================================================================

void handle_event(UiContext* uicon, DomDocument* doc, RdtEvent* event) {
    EventContext evcon;
    log_info("HANDLE_EVENT: type=%d", event->type);
    log_debug("Handling event %d", event->type);
    // PDF documents don't have html_root - they only have view_tree
    // For PDFs, we can still handle basic events using the view_tree
    if (!doc) {
        log_error("No document to handle event");
        return;
    }
    if (!doc->html_root && !doc->view_tree) {
        log_error("No document content to handle event");
        return;
    }
    // For PDF documents (no html_root), skip complex event handling for now
    // PDF is a static document format, so we only need basic scrolling/navigation
    if (!doc->html_root) {
        log_debug("PDF document - skipping DOM event handling");
        return;
    }
    event_context_init(&evcon, uicon, event);

    // find target view based on mouse position
    int mouse_x, mouse_y;
    switch (event->type) {
    case RDT_EVENT_MOUSE_MOVE: {
        MousePositionEvent* motion = &event->mouse_position;
        log_debug("Mouse event at (%d, %d)", motion->x, motion->y);
        mouse_x = motion->x;  mouse_y = motion->y;
        target_html_doc(&evcon, doc->view_tree);

        // Update hover state based on new target
        update_hover_state(&evcon, evcon.target);

        if (evcon.target) {
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        RadiantState* state = (RadiantState*)evcon.ui_context->document->state;

        // Handle text selection drag (supports cross-view selection)
        if (state && state->selection && state->selection->is_selecting) {
            View* anchor_view = state->selection->anchor_view;
            View* current_target = evcon.target;
            
            log_debug("[SELECTION DRAG] is_selecting=true, anchor_view=%p, current_target=%p (type=%d)", 
                anchor_view, current_target, current_target ? current_target->view_type : -1);
            
            // Check if we're dragging over a text view (could be the same or different)
            View* drag_target_view = nullptr;
            if (current_target && current_target->view_type == RDT_VIEW_TEXT) {
                drag_target_view = current_target;
            } else if (anchor_view && anchor_view->view_type == RDT_VIEW_TEXT) {
                // Mouse is not over a text view, stay with the anchor view
                drag_target_view = anchor_view;
            }
            
            if (drag_target_view && drag_target_view->view_type == RDT_VIEW_TEXT) {
                ViewText* text = (ViewText*)drag_target_view;
                TextRect* rect = text->rect;

                // Setup font from text view (critical for correct glyph advance calculation)
                FontBox saved_font = evcon.font;
                if (text->font) {
                    setup_font(evcon.ui_context, &evcon.font, text->font);
                }
                
                // Calculate the correct block position for the drag target view
                // by walking up ITS parent chain
                float sel_block_x = 0, sel_block_y = 0;
                View* parent = text->parent;
                while (parent) {
                    if (parent->view_type == RDT_VIEW_BLOCK ||
                        parent->view_type == RDT_VIEW_INLINE_BLOCK ||
                        parent->view_type == RDT_VIEW_LIST_ITEM) {
                        sel_block_x += ((ViewBlock*)parent)->x;
                        sel_block_y += ((ViewBlock*)parent)->y;
                    }
                    parent = parent->parent;
                }

                // Add the iframe offset that was stored when selection started
                sel_block_x += state->selection->iframe_offset_x;
                sel_block_y += state->selection->iframe_offset_y;

                // Save evcon.block and temporarily set it to the selection view's block position
                BlockBlot saved_block = evcon.block;
                evcon.block.x = sel_block_x;
                evcon.block.y = sel_block_y;

                // Calculate character offset from mouse position using target text rect
                int char_offset = calculate_char_offset_from_position(
                    &evcon, text, rect,
                    motion->x, motion->y);

                log_debug("[SELECTION DRAG] target_view=%p (same as anchor: %d), char_offset=%d, anchor=%d",
                    drag_target_view, drag_target_view == anchor_view, char_offset, state->selection->anchor_offset);

                // Check if we're extending to a different view
                if (drag_target_view != anchor_view) {
                    // Cross-view selection: update focus_view
                    selection_extend_to_view(state, drag_target_view, char_offset);
                    log_debug("[CROSS-VIEW SEL] Extending from anchor_view=%p to focus_view=%p", 
                        anchor_view, drag_target_view);
                } else {
                    // Same view selection
                    selection_extend(state, char_offset);
                }
                caret_set(state, drag_target_view, char_offset);

                // Calculate and set visual position for the caret
                float caret_x, caret_y, caret_height;
                calculate_position_from_char_offset(&evcon, text, rect,
                    char_offset, &caret_x, &caret_y, &caret_height);

                log_debug("[CARET DRAG] char_offset=%d, calc pos: (%.1f, %.1f) height=%.1f, sel_block: (%.1f, %.1f)",
                    char_offset, caret_x, caret_y, caret_height, sel_block_x, sel_block_y);
                
                // Restore evcon.block and evcon.font
                evcon.block = saved_block;
                evcon.font = saved_font;

                if (state->caret) {
                    state->caret->x = caret_x;
                    state->caret->y = caret_y;
                    state->caret->height = caret_height;

                    // Use the same iframe offset as the selection
                    state->caret->iframe_offset_x = state->selection->iframe_offset_x;
                    state->caret->iframe_offset_y = state->selection->iframe_offset_y;
                }

                // Update selection end visual coordinates for rendering
                if (state->selection) {
                    state->selection->end_x = caret_x;
                    state->selection->end_y = caret_y + caret_height;
                    log_debug("[SEL-END] Setting selection end: (%.1f, %.1f), caret at (%.1f, %.1f)",
                        state->selection->end_x, state->selection->end_y,
                        state->caret ? state->caret->x : -1, state->caret ? state->caret->y : -1);
                }

                log_debug("Dragging selection to offset %d, collapsed=%d", char_offset, state->selection->is_collapsed);
                evcon.need_repaint = true;
            }
        }

        if (state && state->drag_target) {
            log_debug("Dragging in progress");
            ArrayList* target_list = build_view_stack(&evcon, (View*)state->drag_target);
            evcon.event.type = RDT_EVENT_MOUSE_DRAG;  // deliver as drag event
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }

        if (uicon->mouse_state.cursor != evcon.new_cursor) {
            log_debug("Change cursor to %d", evcon.new_cursor);
            uicon->mouse_state.cursor = evcon.new_cursor; // update the mouse state
            int cursor_type;
            switch (evcon.new_cursor) {
            case CSS_VALUE_TEXT: cursor_type = GLFW_IBEAM_CURSOR; break;
            case CSS_VALUE_POINTER: cursor_type = GLFW_HAND_CURSOR; break;
            default: cursor_type = GLFW_ARROW_CURSOR; break;
            }
            GLFWcursor* cursor = glfwCreateStandardCursor(cursor_type);
            if (cursor) {
                if (uicon->mouse_state.sys_cursor) {
                    glfwDestroyCursor(uicon->mouse_state.sys_cursor);
                }
                uicon->mouse_state.sys_cursor = cursor;
                glfwSetCursor(uicon->window, cursor);
            }
        }
        break;
    }
    case RDT_EVENT_MOUSE_DOWN:   case RDT_EVENT_MOUSE_UP: {
        MouseButtonEvent* btn_event = &event->mouse_button;
        log_debug("Mouse button event (%d, %d)", btn_event->x, btn_event->y);
        mouse_x = btn_event->x;  mouse_y = btn_event->y; // changed to use btn_event's y
        target_html_doc(&evcon, doc->view_tree);

        RadiantState* state = (RadiantState*)evcon.ui_context->document->state;

        // Update active and focus states
        if (event->type == RDT_EVENT_MOUSE_DOWN && evcon.target) {
            log_info("MOUSE_DOWN: target=%p view_type=%d", evcon.target, evcon.target->view_type);
            if (evcon.target->view_type == RDT_VIEW_TEXT) {
                log_info("Target is ViewText, target_text_rect=%p", evcon.target_text_rect);
            }

            // Set :active state
            update_active_state(&evcon, evcon.target, true);

            // Update focus if target is focusable (mouse-triggered focus)
            if (is_view_focusable(evcon.target)) {
                update_focus_state(&evcon, evcon.target, false);  // from_keyboard=false
            }

            // Handle click in text - position caret or start selection
            if (evcon.target->view_type == RDT_VIEW_TEXT && evcon.target_text_rect) {
                ViewText* text = (ViewText*)evcon.target;
                TextRect* rect = evcon.target_text_rect;
                // Setup font from text view (critical for correct glyph advance calculation)
                FontBox saved_font = evcon.font;
                if (text->font) {
                    setup_font(evcon.ui_context, &evcon.font, text->font);
                }

                // Calculate character offset from click position
                int char_offset = calculate_char_offset_from_position(
                    &evcon, text, rect, btn_event->x, btn_event->y);

                log_info("CLICK IN TEXT at offset %d (target=%p)", char_offset, evcon.target);

                // Set caret at clicked position
                caret_set(state, evcon.target, char_offset);

                // Calculate visual position for the caret
                float caret_x = 0, caret_y = 0, caret_height = 16;
                calculate_position_from_char_offset(&evcon, text, rect, char_offset,
                    &caret_x, &caret_y, &caret_height);

                // Set caret visual position
                if (state->caret) {
                    state->caret->x = caret_x;
                    state->caret->y = caret_y;
                    state->caret->height = caret_height;

                    // Calculate iframe offset: evcon.block has the absolute position of
                    // the text's parent block (including any iframe offset). We need to
                    // find the offset that isn't accounted for when walking up the parent
                    // chain within the iframe document.
                    float chain_x = 0, chain_y = 0;
                    View* parent = text->parent;
                    while (parent) {
                        if (parent->view_type == RDT_VIEW_BLOCK ||
                            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
                            parent->view_type == RDT_VIEW_LIST_ITEM) {
                            chain_x += ((ViewBlock*)parent)->x;
                            chain_y += ((ViewBlock*)parent)->y;
                        }
                        parent = parent->parent;
                    }
                    // iframe offset is the difference between evcon.block and chain position
                    state->caret->iframe_offset_x = evcon.block.x - chain_x;
                    state->caret->iframe_offset_y = evcon.block.y - chain_y;

                    log_info("CARET VISUAL: x=%.1f y=%.1f height=%.1f iframe_offset=(%.1f,%.1f)",
                        caret_x, caret_y, caret_height,
                        state->caret->iframe_offset_x, state->caret->iframe_offset_y);
                }

                // Start new selection if shift not pressed, otherwise extend
                if (!(event->mouse_button.mods & RDT_MOD_SHIFT)) {
                    selection_start(state, evcon.target, char_offset);
                    state->selection->is_selecting = true;  // enter selection mode

                    // Set visual coordinates for selection (same point for start)
                    if (state->selection) {
                        state->selection->start_x = caret_x;
                        state->selection->start_y = caret_y;
                        state->selection->end_x = caret_x;
                        state->selection->end_y = caret_y + caret_height;
                        // Copy iframe offset from caret
                        state->selection->iframe_offset_x = state->caret->iframe_offset_x;
                        state->selection->iframe_offset_y = state->caret->iframe_offset_y;
                    }
                } else if (state->selection && !state->selection->is_collapsed) {
                    // Shift-click extends selection
                    selection_extend(state, char_offset);

                    // Update end visual coordinates
                    if (state->selection) {
                        state->selection->end_x = caret_x;
                        state->selection->end_y = caret_y + caret_height;
                    }
                }

                // Restore font
                evcon.font = saved_font;
                evcon.need_repaint = true;
            }
        } else if (event->type == RDT_EVENT_MOUSE_UP) {
            // Clear :active state
            update_active_state(&evcon, NULL, false);

            // End selection mode
            if (state && state->selection) {
                state->selection->is_selecting = false;
            }
        }

        if (evcon.target) {
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        if (evcon.event.type == RDT_EVENT_MOUSE_UP && state && state->drag_target) {
            log_debug("mouse up in dragging");
            ArrayList* target_list = build_view_stack(&evcon, (View*)state->drag_target);
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
            update_drag_state(&evcon, NULL, false);
        }

        if (evcon.new_url) {
            log_info("opening_url:%s", evcon.new_url);
            if (evcon.new_target) {
                log_debug("setting new src to target: %s", evcon.new_target);
                // find iframe with the target name
                DomNode* elmt = set_iframe_src_by_name(doc->root, evcon.new_target, evcon.new_url);
                View* iframe = find_view(doc->view_tree->root, elmt);
                if (iframe) {
                    log_debug("found iframe view");
                    if ((iframe->view_type == RDT_VIEW_BLOCK || iframe->view_type == RDT_VIEW_INLINE_BLOCK) && ((ViewBlock*)iframe)->embed) {
                        log_debug("updating doc of iframe view");
                        ViewBlock* block = (ViewBlock*)iframe;
                        // reset scroll position
                        if (block->scroller && block->scroller->pane) {
                            block->scroller->pane->reset();
                            block->content_width = 0;  block->content_height = 0;
                        }
                        // load the new document
                        // Use iframe dimensions as viewport (already in CSS logical pixels)
                        int css_vw = (int)block->width;
                        int css_vh = (int)block->height;
                        DomDocument* old_doc = block->embed->doc;
                        DomDocument* new_doc = block->embed->doc =
                            load_html_doc(evcon.ui_context->document->url, evcon.new_url, css_vw, css_vh,
                                          1.0f);  // Layout uses CSS pixels, pixel_ratio not needed
                        if (new_doc) {
                            // Set scale for nested document
                            // Iframe content uses default scale (1.0), combined with display pixel_ratio
                            new_doc->given_scale = 1.0f;
                            new_doc->scale = new_doc->given_scale * evcon.ui_context->pixel_ratio;

                            if (new_doc->html_root) {
                                // HTML/Markdown/XML documents: need CSS layout
                                // Save parent document and window dimensions
                                DomDocument* parent_doc = evcon.ui_context->document;
                                float saved_window_width = evcon.ui_context->window_width;
                                float saved_window_height = evcon.ui_context->window_height;
                                // Set document context to iframe doc for proper URL resolution (e.g., images)
                                evcon.ui_context->document = new_doc;
                                // iframe dimensions are now in CSS pixels
                                evcon.ui_context->window_width = (float)css_vw;
                                evcon.ui_context->window_height = (float)css_vh;
                                // Process @font-face rules before layout (critical for custom fonts)
                                process_document_font_faces(evcon.ui_context, new_doc);
                                layout_html_doc(evcon.ui_context, new_doc, false);
                                // Restore parent document and window dimensions
                                evcon.ui_context->document = parent_doc;
                                evcon.ui_context->window_width = saved_window_width;
                                evcon.ui_context->window_height = saved_window_height;
                            }
                            // PDF scaling now happens inside pdf_page_to_view_tree via load_html_doc
                            // For PDF and other pre-laid-out documents, view_tree is already set
                            if (new_doc->view_tree && new_doc->view_tree->root) {
                                ViewBlock* root = (ViewBlock*)new_doc->view_tree->root;
                                // Use width/height for PDF (content_width/height may be 0)
                                block->content_width = root->content_width > 0 ? root->content_width : root->width;
                                block->content_height = root->content_height > 0 ? root->content_height : root->height;
                                update_scroller(block, block->content_width, block->content_height);
                            }
                        }
                        free_document(old_doc);
                        uicon->document->state->is_dirty = true;
                    }
                    else {
                        log_debug("iframe view has no embed");
                    }
                } else {
                    log_debug("failed to find iframe view");
                }
            }
            else {
                DomDocument* old_doc = evcon.ui_context->document;
                // load the new document
                // Use viewport dimensions (already in CSS logical pixels)
                int css_vw = evcon.ui_context->viewport_width;
                int css_vh = evcon.ui_context->viewport_height;
                evcon.ui_context->document = show_html_doc(evcon.ui_context->document->url, evcon.new_url,
                    css_vw, css_vh);
                free_document(old_doc);
            }
            to_repaint();
        }
        break;
    }
    case RDT_EVENT_SCROLL: {
        ScrollEvent* scroll = &event->scroll;
        log_debug("Mouse scroll event");
        mouse_x = scroll->x;  mouse_y = scroll->y; // updated to use scroll's x and y
        target_html_doc(&evcon, doc->view_tree);
        if (evcon.target) {
            log_debug("Target view found at position (%d, %d)", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            log_debug("No target view found at position (%d, %d)", mouse_x, mouse_y);
        }
        break;
    }
    case RDT_EVENT_KEY_DOWN: {
        KeyEvent* key_event = &event->key;
        RadiantState* state = (RadiantState*)evcon.ui_context->document->state;
        if (!state) break;

        View* focused = focus_get(state);
        log_debug("Key down: key=%d, mods=0x%x, focused=%p", key_event->key, key_event->mods, focused);

        // Tab navigation
        if (key_event->key == RDT_KEY_TAB) {
            bool forward = !(key_event->mods & RDT_MOD_SHIFT);
            if (doc->view_tree && doc->view_tree->root) {
                focus_move(state, doc->view_tree->root, forward);
            }
            evcon.need_repaint = true;
            break;
        }

        // Handle caret/selection navigation for focused editable elements
        if (focused && state->caret) {
            bool shift = (key_event->mods & RDT_MOD_SHIFT) != 0;
            bool ctrl = (key_event->mods & RDT_MOD_CTRL) != 0;
            bool cmd = (key_event->mods & RDT_MOD_SUPER) != 0;
            
            // Get text data for UTF-8 aware navigation
            unsigned char* text_data = nullptr;
            if (focused->is_text()) {
                text_data = ((ViewText*)focused)->text_data();
            }

            switch (key_event->key) {
                case RDT_KEY_LEFT:
                    if (shift) {
                        // Extend selection left (UTF-8 aware)
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        int new_offset = text_data 
                            ? utf8_offset_by_chars(text_data, state->caret->char_offset, -1)
                            : state->caret->char_offset - 1;
                        selection_extend(state, new_offset);
                    } else {
                        selection_clear(state);
                        caret_move(state, ctrl ? -10 : -1);  // word jump with ctrl
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_RIGHT:
                    if (shift) {
                        // Extend selection right (UTF-8 aware)
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        int new_offset = text_data
                            ? utf8_offset_by_chars(text_data, state->caret->char_offset, 1)
                            : state->caret->char_offset + 1;
                        selection_extend(state, new_offset);
                    } else {
                        selection_clear(state);
                        caret_move(state, ctrl ? 10 : 1);
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_UP:
                    if (shift) {
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        // Calculate line start/end for extending selection
                        caret_move_line(state, -1);
                        selection_extend(state, state->caret->char_offset);
                    } else {
                        selection_clear(state);
                        caret_move_line(state, -1);
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_DOWN:
                    if (shift) {
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        caret_move_line(state, 1);
                        selection_extend(state, state->caret->char_offset);
                    } else {
                        selection_clear(state);
                        caret_move_line(state, 1);
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_HOME:
                    if (shift) {
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        caret_move_to(state, cmd ? 2 : 0);  // doc start or line start
                        selection_extend(state, state->caret->char_offset);
                    } else {
                        selection_clear(state);
                        caret_move_to(state, cmd ? 2 : 0);
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_END:
                    if (shift) {
                        if (!state->selection || state->selection->is_collapsed) {
                            selection_start(state, focused, state->caret->char_offset);
                        }
                        caret_move_to(state, cmd ? 3 : 1);  // doc end or line end
                        selection_extend(state, state->caret->char_offset);
                    } else {
                        selection_clear(state);
                        caret_move_to(state, cmd ? 3 : 1);
                    }
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_A:
                    // Select all (Ctrl+A / Cmd+A)
                    if (ctrl || cmd) {
                        selection_select_all(state);
                        evcon.need_repaint = true;
                    }
                    break;

                case RDT_KEY_C:
                    // Copy selection (Ctrl+C / Cmd+C)
                    if (ctrl || cmd) {
                        if (selection_has(state)) {
                            Pool* temp_pool = pool_create();
                            Arena* temp_arena = arena_create_default(temp_pool);
                            char* text = extract_selected_text(state, temp_arena);
                            if (text) {
                                clipboard_copy_text(text);
                                log_debug("Copied text to clipboard: %zu chars", strlen(text));
                            }
                            arena_destroy(temp_arena);
                            pool_destroy(temp_pool);
                        }
                    }
                    break;

                case RDT_KEY_X:
                    // Cut selection (Ctrl+X / Cmd+X)
                    if (ctrl || cmd) {
                        if (selection_has(state)) {
                            // Copy to clipboard
                            Pool* temp_pool = pool_create();
                            Arena* temp_arena = arena_create_default(temp_pool);
                            char* text = extract_selected_text(state, temp_arena);
                            if (text) {
                                clipboard_copy_text(text);
                                log_debug("Cut text to clipboard: %zu chars", strlen(text));
                            }
                            arena_destroy(temp_arena);
                            pool_destroy(temp_pool);

                            // TODO: delete selected text
                            selection_clear(state);
                            evcon.need_repaint = true;
                        }
                    }
                    break;

                case RDT_KEY_BACKSPACE:
                    // TODO: delete selection or character before caret
                    evcon.need_repaint = true;
                    break;

                case RDT_KEY_DELETE:
                    // TODO: delete selection or character after caret
                    evcon.need_repaint = true;
                    break;

                default:
                    break;
            }
        }
        break;
    }
    case RDT_EVENT_KEY_UP: {
        // Key release - typically not needed for text editing
        log_debug("Key up: key=%d", event->key.key);
        break;
    }
    case RDT_EVENT_TEXT_INPUT: {
        TextInputEvent* text_event = &event->text_input;
        RadiantState* state = (RadiantState*)evcon.ui_context->document->state;
        if (!state) break;

        View* focused = focus_get(state);
        log_debug("Text input: codepoint=U+%04X, focused=%p", text_event->codepoint, focused);

        if (focused && state->caret) {
            // Delete any existing selection first
            if (selection_has(state)) {
                // TODO: delete selected text
                selection_clear(state);
            }

            // TODO: insert character at caret position
            // This requires access to the text content of the focused element

            // Move caret forward
            caret_move(state, 1);
            evcon.need_repaint = true;
        }
        break;
    }
    default:
        log_debug("Unhandled event type: %d", event->type);
        break;
    }

    // Process pending reflows if any state changes require relayout
    RadiantState* state = (RadiantState*)uicon->document->state;
    if (state && state->needs_reflow) {
        log_debug("Processing pending reflows before repaint");
        reflow_process_pending(state);

        // If reflow is still needed after processing, trigger actual relayout
        if (state->needs_reflow) {
            // Trigger relayout by marking the event context
            evcon.need_repaint = true;  // repaint includes relayout
            log_debug("Reflow required, will trigger relayout");
        }
    }

    if (evcon.need_repaint) {
        uicon->document->state->is_dirty = true;
        to_repaint();
    }
    log_debug("end of event %d", event->type);

    event_context_cleanup(&evcon);
}
