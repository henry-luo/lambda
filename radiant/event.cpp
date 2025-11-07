#include "handler.hpp"

#include "../lib/log.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_parser.hpp"
Document* show_html_doc(Url *base, char* doc_filename);
View* layout_html_doc(UiContext* uicon, Document* doc, bool is_reflow);
void to_repaint();

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
            log_debug("target view block:%s, x:%f, y:%f, wd:%f, hg:%f",
                block->node->name(), block->x, block->y, block->width, block->height);
            if (block->position && block->position->position != LXB_CSS_VALUE_STATIC) {
                log_debug("skip absolute/fixed positioned block");
            } else {
                target_block_view(evcon, block);
            }
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            log_debug("target view inline:%s", span->node->name());
            target_inline_view(evcon, span);
        }
        else if (view->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            target_text_view(evcon, text);
        }
        else {
            log_debug("Invalid target view type: %d", view->type);
        }
        view = view->next;
    } while (view && !evcon->target);
}

void target_text_view(EventContext* evcon, ViewText* text) {
    unsigned char* str = text->node->text_data();
    TextRect *text_rect = text->rect;
    NEXT_RECT:
    float x = evcon->block.x + text_rect->x, y = evcon->block.y + text_rect->y;
    unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + max(text_rect->length, 0);
    log_debug("target text:'%t' start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d",
        str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, evcon->block.x);
    bool has_space = false;
    for (; p < end; p++) {
        int wd = 0;
        if (is_space(*p)) {
            if (has_space) continue;  // skip consecutive spaces
            else has_space = true;
            // printf("target_space: %c, x:%f, end:%f\n", *p, x, x + evcon->font.space_width);
            wd = evcon->font.style->space_width;
        }
        else {
            has_space = false;
            FT_Int32 load_flags = (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
            if (FT_Load_Char(evcon->font.ft_face, *p, load_flags)) {
                log_error("Could not load character '%c'", *p);
                continue;
            }
            // draw the glyph to the image buffer
            // printf("target_glyph: %c, x:%f, end:%f, y:%f\n", *p, x, x + (evcon->font.face->glyph->advance.x / 64.0), y);
            wd = evcon->font.ft_face->glyph->advance.x / 64.0;  // changed from rdcon to evcon
        }
        float char_right = x + wd;  float char_bottom = y + (evcon->font.ft_face->height / 64.0);
        MousePositionEvent* event = &evcon->event.mouse_position;
        if (x <= event->x && event->x < char_right && y <= event->y && event->y < char_bottom) {
            log_debug("hit on text: %c", *p);
            evcon->target = text;  evcon->target_text_rect = text_rect;  return;
        }
        // advance to the next position
        x += wd;
    }
    assert(text_rect->next != text_rect);
    text_rect = text_rect->next;
    if (text_rect) { goto NEXT_RECT; }
    log_debug("hit not on text");
}

void target_inline_view(EventContext* evcon, ViewSpan* view_span) {
    log_debug("targetting inline: %s", view_span->node->name());
    log_enter();
    FontBox pa_font = evcon->font;
    View* view = view_span->child;
    if (view) {
        if (view_span->font) {
            setup_font(evcon->ui_context, &evcon->font, view_span->font);
        }
        target_children(evcon, view);
    }
    else {
        log_debug("view has no child");
    }
    evcon->font = pa_font;
    log_leave();
}

void target_block_view(EventContext* evcon, ViewBlock* block) {
    log_debug("targetting block: %s", block->node->name());
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
            log_debug("hit on block scroll: %s", block->node->name());
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

    // target absolute/fixed positioned children
    if (block->position && block->position->first_abs_child) {
        ViewBlock* abs_child = block->position->first_abs_child;
        do {
            // todo: should target based on z-index order
            log_debug("targetting positioned child block: %s", abs_child->node->name());
            target_block_view(evcon, abs_child);
            if (evcon->target) { goto RETURN; }
            abs_child = abs_child->position->next_abs_sibling;
        } while (abs_child);
    }

    // target static positioned children
    view = block->child;
    if (view) {
        if (block->font) {
            setup_font(evcon->ui_context, &evcon->font, block->font);
        }
        target_children(evcon, view);
    }

    RETURN:
    evcon->block = pa_block;  evcon->font = pa_font;
    if (!evcon->target) { // check the block itself
        float x = evcon->block.x, y = evcon->block.y;
        if (x <= event->x && event->x < x + block->width &&
            y <= event->y && event->y < y + block->height) {
            log_debug("hit on block: %s", block->node->name());
            evcon->target = (View*)block;
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
        }
        else {
            log_debug("hit not on block: %s, x: %.1f, y: %.1f, ex: %.1f, ey: %.1f, right: %.1f, bottom: %.1f",
                block->node->name(), x, y, event->x, event->y, x + block->width, y + block->height);
        }
    }
    log_leave();
}

void target_html_doc(EventContext* evcon, ViewTree* view_tree) {
    View* root_view = view_tree->root;
    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        log_debug("target root view");
        FontBox pa_font = evcon->font;
        FontProp* default_font = view_tree->html_version == HTML5 ? &evcon->ui_context->default_font : &evcon->ui_context->legacy_default_font;
        log_debug("target_html_doc default font: %s, html version: %d", default_font->family, view_tree->html_version);
        setup_font(evcon->ui_context, &evcon->font, default_font);
        target_block_view(evcon, (ViewBlock*)root_view);
        evcon->font = pa_font;
    }
    else {
        log_error("Invalid root view: %d", root_view ? root_view->type : -1);
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
    if (evcon->new_cursor == LXB_CSS_VALUE_AUTO) {
        log_debug("set text cursor");
        evcon->new_cursor = LXB_CSS_VALUE_TEXT;
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
    uintptr_t name = span->node->tag();
    log_debug("fired at view %s", span->node->name());
    if (name == LXB_TAG_A) {
        log_debug("fired at anchor tag");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            log_debug("mouse down at anchor tag");
            const char* href = span->node->get_attribute("href");
            if (href) {
                log_debug("got anchor href: %s", href);
                evcon->new_url = (char*)href;
                const char* target = span->node->get_attribute("target");
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
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
            view->type == RDT_VIEW_LIST_ITEM) {
            fire_block_event(evcon, (ViewBlock*)view);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            fire_inline_event(evcon, (ViewSpan*)view);
        }
        else if (view->type == RDT_VIEW_TEXT) {
            fire_text_event(evcon, (ViewText*)view);
        }
        else {
            log_error("Invalid fire view type: %d", view->type);
        }
    }
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    evcon->event = *event;
    // load default font Arial, size 16 px
    setup_font(uicon, &evcon->font, &uicon->default_font);
    evcon->new_cursor = LXB_CSS_VALUE_AUTO;
    if (!uicon->document->state) {
        uicon->document->state = (StateStore*)calloc(1, sizeof(StateStore));
    }
}

void event_context_cleanup(EventContext* evcon) {
}

lxb_status_t set_iframe_src_callback(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec, void *ctx) {
    lxb_dom_element_t *element = lxb_dom_interface_element(node);
    *(lxb_dom_element_t **)ctx = element;
    return LXB_STATUS_OK;
}

// find iframe by name and set new src using selector
DomNodeBase* set_iframe_src_by_name(DomElement *document, const char *target_name, const char *new_src) {
    if (!document || !target_name || !new_src) {
        log_error("Invalid parameters to set_iframe_src_by_name");
        return NULL;
    }
    // get memory pool from document
    Pool* pool = document->pool;
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
View* find_view(View* view, DomNodeBase* node) {
    // Compare if the view's node matches the target node directly
    if (view->node && node && view->node == node) {
        return view;
    }

    if (view->is_group()) {
        ViewGroup* group = (ViewGroup*)view;
        View* child = group->child;
        while (child) {
            View* found = find_view(child, node);
            if (found) { return found; }
            child = child->next;
        }
    }
    return NULL;
}

void handle_event(UiContext* uicon, Document* doc, RdtEvent* event) {
    EventContext evcon;
    log_debug("Handling event %d", event->type);
    if (!doc || !doc->lambda_html_root) {
        log_error("No document to handle event");
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
        if (evcon.ui_context->document->state->is_dragging) {
            log_debug("Dragging in progress");
            ArrayList* target_list = build_view_stack(&evcon, evcon.ui_context->document->state->drag_target);
            evcon.event.type = RDT_EVENT_MOUSE_DRAG;  // deliver as drag event
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }

        if (uicon->mouse_state.cursor != evcon.new_cursor) {
            log_debug("Change cursor to %d", evcon.new_cursor);
            uicon->mouse_state.cursor = evcon.new_cursor; // update the mouse state
            int cursor_type;
            switch (evcon.new_cursor) {
            case LXB_CSS_VALUE_TEXT: cursor_type = GLFW_IBEAM_CURSOR; break;
            case LXB_CSS_VALUE_POINTER: cursor_type = GLFW_HAND_CURSOR; break;
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
        if (evcon.event.type == RDT_EVENT_MOUSE_UP && evcon.ui_context->document->state->is_dragging) {
            log_debug("mouse up in dragging");
            ArrayList* target_list = build_view_stack(&evcon, evcon.ui_context->document->state->drag_target);
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }

        if (evcon.new_url) {
            log_info("opening_url:%s", evcon.new_url);
            if (evcon.new_target) {
                log_debug("setting new src to target: %s", evcon.new_target);
                // find iframe with the target name
                DomNodeBase* elmt = set_iframe_src_by_name(doc->lambda_dom_root, evcon.new_target, evcon.new_url);
                View* iframe = find_view(doc->view_tree->root, elmt);
                if (iframe) {
                    log_debug("found iframe view");
                    if ((iframe->type == RDT_VIEW_BLOCK || iframe->type == RDT_VIEW_INLINE_BLOCK) && ((ViewBlock*)iframe)->embed) {
                        log_debug("updating doc of iframe view");
                        ViewBlock* block = (ViewBlock*)iframe;
                        // reset scroll position
                        if (block->scroller && block->scroller->pane) {
                            block->scroller->pane->reset();
                            block->content_width = 0;  block->content_height = 0;
                        }
                        // load the new document
                        Document* old_doc = block->embed->doc;
                        Document* new_doc = block->embed->doc =
                            load_html_doc(evcon.ui_context->document->url, evcon.new_url);
                        if (new_doc && new_doc->lambda_html_root) {
                            layout_html_doc(evcon.ui_context, new_doc, false);
                            if (new_doc->view_tree && new_doc->view_tree->root) {
                                ViewBlock* root = (ViewBlock*)new_doc->view_tree->root;
                                block->content_width = root->content_width;
                                block->content_height = root->content_height;
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
                Document* old_doc = evcon.ui_context->document;
                // load the new document
                evcon.ui_context->document = show_html_doc(evcon.ui_context->document->url, evcon.new_url);
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
    default:
        log_debug("Unhandled event type: %d", event->type);
        break;
    }
    if (evcon.need_repaint) {
        uicon->document->state->is_dirty = true;
        to_repaint();
    }
    log_debug("end of event %d", event->type);

    event_context_cleanup(&evcon);
}
