#include "handler.hpp"

#include "../lib/log.h"
Document* show_html_doc(lxb_url_t *base, char* doc_filename);
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

void target_children(EventContext* evcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
            view->type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = (ViewBlock*)view;
            printf("target view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                block->node->name(),
                block->x, block->y, block->width, block->height);                
            target_block_view(evcon, block);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("target view inline:%s\n", span->node->name());                
            target_inline_view(evcon, span);
        }
        else if (view->type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            target_text_view(evcon, text);
        }
        else {
            printf("Invalid target view type: %d\n", view->type);
        }
        view = view->next;
    } while (view && !evcon->target);
}

void target_text_view(EventContext* evcon, ViewText* text) {
    float x = evcon->block.x + text->x, y = evcon->block.y + text->y;
    unsigned char* str = text->node->text_data();  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    printf("target text:%s start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d\n", 
        str, text->start_index, text->length, text->x, text->y, text->width, text->height, evcon->block.x);
    bool has_space = false;   
    for (; p < end; p++) {
        int wd = 0;
        if (is_space(*p)) { 
            if (has_space) continue;  // skip consecutive spaces
            else has_space = true;
            // printf("target_space: %c, x:%f, end:%f\n", *p, x, x + evcon->font.space_width);
            wd = evcon->font.space_width;
        }
        else {
            has_space = false;
            if (FT_Load_Char(evcon->font.face, *p, FT_LOAD_RENDER)) {
                fprintf(stderr, "Could not load character '%c'\n", *p);
                continue;
            }
            // draw the glyph to the image buffer
            // printf("target_glyph: %c, x:%f, end:%f, y:%f\n", *p, x, x + (evcon->font.face->glyph->advance.x >> 6), y);
            wd = evcon->font.face->glyph->advance.x >> 6;  // changed from rdcon to evcon
        }
        float char_right = x + wd;  float char_bottom = y + (evcon->font.face->height >> 6);
        MousePositionEvent* event = &evcon->event.mouse_position;
        if (x <= event->x && event->x < char_right && y <= event->y && event->y < char_bottom) {
            printf("hit on text: %c\n", *p);
            evcon->target = (View*)text;  return;
        }
        // advance to the next position
        x += wd;
    }
}

void target_inline_view(EventContext* evcon, ViewSpan* view_span) {
    FontBox pa_font = evcon->font;
    View* view = view_span->child;
    if (view) {
        if (view_span->font) {
            setup_font(evcon->ui_context, &evcon->font, pa_font.face->family_name, view_span->font);
        }
        target_children(evcon, view);
    }
    else {
        printf("view has no child\n");
    }
    evcon->font = pa_font;
}

void target_block_view(EventContext* evcon, ViewBlock* block) {
    printf("targetting block: %s\n", block->node->name());
    BlockBlot pa_block = evcon->block;  FontBox pa_font = evcon->font;
    evcon->block.x = pa_block.x + block->x;  evcon->block.y = pa_block.y + block->y;
    MousePositionEvent* event = &evcon->event.mouse_position;
    // target the scrollbars first
    View* view = NULL;
    bool hover = false;
    if (block->scroller && block->scroller->pane) {
        hover = scrollpane_target(evcon, block);
        if (hover) {
            printf("hit on block scroll: %s\n", block->node->name());
            evcon->target = (View*)block;
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
            goto RETURN;
        }
        else {
            printf("no hit on block scroll\n");
        }
        // setup scrolling offset
        evcon->block.x -= block->scroller->pane->h_scroll_position;
        evcon->block.y -= block->scroller->pane->v_scroll_position;
    }
    view = block->child;
    if (view) {
        if (block->font) {
            setup_font(evcon->ui_context, &evcon->font, pa_font.face->family_name, block->font); 
        }
        target_children(evcon, view);
        if (!evcon->target) { // check the block itself
            int x = evcon->block.x, y = evcon->block.y;
            if (x <= event->x && event->x < x + block->width &&
                y <= event->y && event->y < y + block->height) {
                printf("hit on block: %s\n", block->node->name());
                evcon->target = (View*)block;
                evcon->offset_x = event->x - evcon->block.x;
                evcon->offset_y = event->y - evcon->block.y;                
            }
        }
    }
    else {
        printf("view has no child\n");
    }
    RETURN:
    evcon->block = pa_block;  evcon->font = pa_font;
}

void target_html_doc(EventContext* evcon, View* root_view) {
    printf("target root view\n");
    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("fire root view:\n");
        target_block_view(evcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
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
    printf("fire text event\n");
    if (evcon->new_cursor == LXB_CSS_VALUE_AUTO) {
        printf("set text cursor\n");
        evcon->new_cursor = LXB_CSS_VALUE_TEXT;
    }
    else {
        printf("cursor already set as %d\n", evcon->new_cursor);
    }
}

void fire_inline_event(EventContext* evcon, ViewSpan* span) {
    printf("fire inline event\n");
    if (span->in_line && span->in_line->cursor) {
        evcon->new_cursor = span->in_line->cursor;
    }
    uintptr_t name = span->node->tag();
    printf("fired at view %s\n", span->node->name());
    if (name == LXB_TAG_A) {
        printf("fired at anchor tag\n");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            log_debug("mouse down at anchor tag");
            const lxb_char_t *href = span->node->get_attribute("href");
            if (href) {
                printf("got anchor href: %s\n", href);
                evcon->new_url = (char*)href;
                const lxb_char_t *target = span->node->get_attribute("target");
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
    printf("fire block event\n");
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
        printf("fire event to view no. %d\n", i);
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
            printf("Invalid fire view type: %d\n", view->type);
        }
    }
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    evcon->event = *event;
    // load default font Arial, size 16 px
    setup_font(uicon, &evcon->font, uicon->default_font.family, &evcon->ui_context->default_font);
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
lxb_dom_element_t *set_iframe_src_by_name(lxb_html_document_t *document, 
    const char *target_name, const char *new_src) {
    lxb_status_t status;
    lxb_css_parser_t *parser = lxb_css_parser_create();
    status = lxb_css_parser_init(parser, NULL);
    if (status != LXB_STATUS_OK) { return NULL; }

    // create selector
    lxb_selectors_t *selectors = lxb_selectors_create();
    status = lxb_selectors_init(selectors);
    if (status != LXB_STATUS_OK) {
        lxb_selectors_destroy(selectors, true);
        return NULL;
    }
    
    // construct selector string: iframe[name="target_name"]
    char selector_str[128];
    snprintf(selector_str, sizeof(selector_str), "iframe[name=\"%s\"]", target_name);
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(parser, (const lxb_char_t *)selector_str, strlen(selector_str));
    if (parser->status != LXB_STATUS_OK) {
        return NULL;
    }

    lxb_dom_element_t *element = NULL;
    status = lxb_selectors_find(selectors, lxb_dom_interface_node(document), list, 
        set_iframe_src_callback, (void*)&element);
    if (element) {
        log_debug("set iframe src: %s", new_src);
        lxb_dom_element_set_attribute(element, (const lxb_char_t *)"src", 3, 
            (const lxb_char_t *)new_src, (size_t)strlen((char*)new_src));
    }
    // cleanup
    lxb_selectors_destroy(selectors, true);
    lxb_css_selector_list_destroy_memory(list);
    lxb_css_parser_destroy(parser, true);
    return element;
}

// find the sub-view that matches the given node
View* find_view(View* view, lxb_dom_node_t *node) {
    if (view->node && view->node->as_node() == node) { return view; }
    if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
        view->type == RDT_VIEW_LIST_ITEM || view->type == RDT_VIEW_INLINE) {
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

void update_scroller(ViewBlock* block, int content_width, int content_height) {
    if (!block->scroller) { return; }
    // handle horizontal overflow
    if (content_width > block->width) { // hz overflow
        block->scroller->has_hz_overflow = true;
        if (block->scroller->overflow_x == LXB_CSS_VALUE_VISIBLE) {}
        else if (block->scroller->overflow_x == LXB_CSS_VALUE_SCROLL || 
            block->scroller->overflow_x == LXB_CSS_VALUE_AUTO) {
            block->scroller->has_hz_scroll = true;
        }
        if (block->scroller->has_hz_scroll || 
            block->scroller->overflow_x == LXB_CSS_VALUE_CLIP || 
            block->scroller->overflow_x == LXB_CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;                
        }
    }
    else {
        block->scroller->has_hz_overflow = false;
        block->scroller->has_clip = false;
    }
    // handle vertical overflow and determine block->height
    if (content_height > block->height) { // vt overflow
        block->scroller->has_vt_overflow = true;
        if (block->scroller->overflow_y == LXB_CSS_VALUE_VISIBLE) { }
        else if (block->scroller->overflow_y == LXB_CSS_VALUE_SCROLL || block->scroller->overflow_y == LXB_CSS_VALUE_AUTO) {
            block->scroller->has_vt_scroll = true;
        }
        if (block->scroller->has_hz_scroll || 
            block->scroller->overflow_y == LXB_CSS_VALUE_CLIP || 
            block->scroller->overflow_y == LXB_CSS_VALUE_HIDDEN) {
            block->scroller->has_clip = true;
            block->scroller->clip.left = 0;  block->scroller->clip.top = 0;
            block->scroller->clip.right = block->width;  block->scroller->clip.bottom = block->height;          
        }                
    }
    else {
        block->scroller->has_vt_overflow = false;
        block->scroller->has_clip = false;
    }
}

void handle_event(UiContext* uicon, Document* doc, RdtEvent* event) {
    EventContext evcon;
    printf("Handling event %d\n", event->type);
    if (!doc || !doc->dom_tree) {
        printf("No document to handle event\n");
        return;
    }
    event_context_init(&evcon, uicon, event);

    // find target view based on mouse position
    int mouse_x, mouse_y;
    switch (event->type) {
    case RDT_EVENT_MOUSE_MOVE: {
        MousePositionEvent* motion = &event->mouse_position;
        printf("Mouse event at (%d, %d)\n", motion->x, motion->y);
        mouse_x = motion->x;  mouse_y = motion->y;
        target_html_doc(&evcon, doc->view_tree->root);
        if (evcon.target) {
            printf("Target view found at position (%d, %d)\n", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            printf("No target view found at position (%d, %d)\n", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        if (evcon.ui_context->document->state->is_dragging) {
            printf("Dragging in progress\n");
            ArrayList* target_list = build_view_stack(&evcon, evcon.ui_context->document->state->drag_target);
            evcon.event.type = RDT_EVENT_MOUSE_DRAG;  // deliver as drag event
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }
        
        if (uicon->mouse_state.cursor != evcon.new_cursor) {
            printf("Change cursor to %d\n", evcon.new_cursor);
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
        printf("Mouse button event (%d, %d)\n", btn_event->x, btn_event->y);
        mouse_x = btn_event->x;  mouse_y = btn_event->y; // changed to use btn_event's y
        target_html_doc(&evcon, doc->view_tree->root);
        if (evcon.target) {
            printf("Target view found at position (%d, %d)\n", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            printf("No target view found at position (%d, %d)\n", mouse_x, mouse_y);
        }

        // fire drag event if dragging in progress
        if (evcon.event.type == RDT_EVENT_MOUSE_UP && evcon.ui_context->document->state->is_dragging) {
            printf("mouse up in dragging\n");
            ArrayList* target_list = build_view_stack(&evcon, evcon.ui_context->document->state->drag_target);
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        }

        if (evcon.new_url) {
            log_info("opening_url:%s", evcon.new_url);
            if (evcon.new_target) {
                log_debug("setting new src to target: %s", evcon.new_target);
                // find iframe with the target name
                lxb_dom_element_t *elmt = set_iframe_src_by_name(doc->dom_tree, evcon.new_target, evcon.new_url);
                View* iframe = find_view(doc->view_tree->root, (lxb_dom_node_t*)elmt);
                if (iframe) {
                    if ((iframe->type == RDT_VIEW_BLOCK || iframe->type == RDT_VIEW_INLINE_BLOCK) && 
                        ((ViewBlock*)iframe)->embed) {
                        log_debug("updating doc of iframe view");
                        ViewBlock* block = (ViewBlock*)iframe;
                        if (block->scroller && block->scroller->pane) {
                            block->scroller->pane->h_scroll_position = 0;
                            block->scroller->pane->v_scroll_position = 0;
                        }
                        // load the new document
                        Document* old_doc = block->embed->doc;
                        Document* new_doc = block->embed->doc = 
                            load_html_doc(evcon.ui_context->document->url, evcon.new_url);
                        if (new_doc && new_doc->dom_tree) {
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
        printf("Mouse scroll event\n");
        mouse_x = scroll->x;  mouse_y = scroll->y; // updated to use scroll's x and y
        target_html_doc(&evcon, doc->view_tree->root);
        if (evcon.target) {
            printf("Target view found at position (%d, %d)\n", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
            arraylist_free(target_list);
        } else {
            printf("No target view found at position (%d, %d)\n", mouse_x, mouse_y);
        }
        break;
    }
    default:
        printf("Unhandled event type: %d\n", event->type);
        break;
    }
    if (evcon.need_repaint) {
        uicon->document->state->is_dirty = true;
        to_repaint();
    }    
    printf("end of event %d\n", event->type);

    event_context_cleanup(&evcon);
}
