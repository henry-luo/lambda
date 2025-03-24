#include "handler.h"

Document* show_html_doc(lxb_url_t *base, char* doc_filename);
void free_document(Document* doc);
void to_repaint();

void target_block_view(EventContext* evcon, ViewBlock* view_block);
void target_inline_view(EventContext* evcon, ViewSpan* view_span);
void target_text_view(EventContext* evcon, ViewText* text);
void scrollpane_scroll(EventContext* evcon, ScrollPane* sp);
bool scrollpane_target(EventContext* evcon, ViewBlock* block);
void scrollpane_mouse_down(EventContext* evcon, ViewBlock* block);

void target_children(EventContext* evcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
            view->type == RDT_VIEW_LIST || view->type == RDT_VIEW_LIST_ITEM || 
            view->type == RDT_VIEW_IMAGE) {
            ViewBlock* block = (ViewBlock*)view;
            printf("target view block:%s, x:%d, y:%d, wd:%d, hg:%d\n",
                lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                block->x, block->y, block->width, block->height);                
            target_block_view(evcon, block);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("target view inline:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));                
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
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    printf("text:%s start:%d, len:%d, x:%d, y:%d, wd:%d, hg:%d, blk_x:%d\n", 
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

void target_block_view(EventContext* evcon, ViewBlock* view_block) {
    BlockBlot pa_block = evcon->block;  FontBox pa_font = evcon->font;
    evcon->block.x = pa_block.x + view_block->x;  evcon->block.y = pa_block.y + view_block->y;
    MousePositionEvent* event = &evcon->event.mouse_position;
    // target the scrollbars first
    if (view_block->scroller && view_block->scroller->pane) {
        bool hover = scrollpane_target(evcon, view_block);
        if (hover) {
            printf("hit on block scroll: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(view_block->node), NULL));
            evcon->target = (View*)view_block;
            evcon->offset_x = event->x - evcon->block.x;
            evcon->offset_y = event->y - evcon->block.y;
            goto RETURN;
        }
    }
    View* view = view_block->child;
    if (view) {
        if (view_block->font) {
            setup_font(evcon->ui_context, &evcon->font, pa_font.face->family_name, view_block->font); 
        }        
        target_children(evcon, view);
        if (!evcon->target) { // check the block itself
            int x = evcon->block.x, y = evcon->block.y;
            if (x <= event->x && event->x < x + view_block->width &&
                y <= event->y && event->y < y + view_block->height) {
                printf("hit on block: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(view_block->node), NULL));
                evcon->target = (View*)view_block;
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
    int name = ((lxb_html_element_t*)span->node)->element.node.local_name;
    printf("fired at view %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));
    if (name == LXB_TAG_A) {
        printf("fired at anchor tag\n");
        if (evcon->event.type == RDT_EVENT_MOUSE_DOWN) {
            printf("mouse down at anchor tag\n");
            lxb_dom_attr_t *href = lxb_dom_element_attr_by_id(lxb_dom_interface_element(span->node), LXB_DOM_ATTR_HREF);
            if (href) {
                printf("got anchor href: %s\n", href->value->data);
                evcon->new_uri = (char*)href->value->data;
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
    }
}

void fire_events(EventContext* evcon, ArrayList* target_list) {
    int stack_size = target_list->length;
    for (int i = 0; i < stack_size; i++) {
        printf("fire event to view no. %d\n", i);
        View* view = (View*)target_list->data[i];
        if (view->type == RDT_VIEW_BLOCK || view->type == RDT_VIEW_INLINE_BLOCK ||
            view->type == RDT_VIEW_LIST || view->type == RDT_VIEW_LIST_ITEM || 
            view->type == RDT_VIEW_IMAGE) {
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
        uicon->document->state = calloc(1, sizeof(StateStore));
    }
}

void event_context_cleanup(EventContext* evcon) {
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
    case RDT_EVENT_MOUSE_MOVE:  
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
    case RDT_EVENT_MOUSE_DOWN:   case RDT_EVENT_MOUSE_UP:
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
        if (evcon.new_uri) {
            printf("Opening URI: %s\n", evcon.new_uri);
            Document* old_doc = evcon.ui_context->document;
            // load the new document
            evcon.ui_context->document = show_html_doc(evcon.ui_context->document->url, evcon.new_uri);
            free_document(old_doc);
            to_repaint();
        }
        break;
    case RDT_EVENT_SCROLL:
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

