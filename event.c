#include "view.h"

typedef struct EventContext {
    RdtEvent event;
    View* target;

    BlockBlot block;
    FontProp* font; // current font style
    FT_Face face;   // current font face
    float space_width;

    PropValue new_cursor;
    
    UiContext* ui_context;
} EventContext;

void target_block_view(EventContext* evcon, ViewBlock* view_block);
void target_inline_view(EventContext* evcon, ViewSpan* view_span);
void target_text_view(EventContext* evcon, ViewText* text);

void target_children(EventContext* evcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            printf("target view block:%s, x:%f, y:%f, wd:%f, hg:%f\n",
                lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                block->x, block->y, block->width, block->height);                
            target_block_view(evcon, block);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("target view inline:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));                
            target_inline_view(evcon, span);
        }
        else {
            ViewText* text = (ViewText*)view;
            target_text_view(evcon, text);
        }
        view = view->next;
    } while (view && !evcon->target);
}

void target_text_view(EventContext* evcon, ViewText* text) {
    float x = evcon->block.x + text->x, y = evcon->block.y + text->y;
    unsigned char* str = lxb_dom_interface_text(text->node)->char_data.data.data;  
    unsigned char* p = str + text->start_index;  unsigned char* end = p + text->length;
    printf("text:%s start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, blk_x:%f\n", 
        str, text->start_index, text->length, text->x, text->y, text->width, text->height, evcon->block.x);
    bool has_space = false;   
    for (; p < end; p++) {
        int wd = 0;
        if (is_space(*p)) { 
            if (has_space) continue;  // skip consecutive spaces
            else has_space = true;
            printf("target_space: %c, x:%f, end:%f\n", *p, x, x + evcon->space_width);
            wd = evcon->space_width;
        }
        else {
            has_space = false;
            if (FT_Load_Char(evcon->face, *p, FT_LOAD_RENDER)) {
                fprintf(stderr, "Could not load character '%c'\n", *p);
                continue;
            }
            // draw the glyph to the image buffer
            printf("target_glyph: %c, x:%f, end:%f, y:%f\n", *p, x, x + (evcon->face->glyph->advance.x >> 6), y);
            wd = evcon->face->glyph->advance.x >> 6;  // changed from rdcon to evcon
        }
        float char_right = x + wd;  float char_bottom = y + (evcon->face->height >> 6);
        SDL_MouseMotionEvent* event = &evcon->event.mouse_motion;
        if (x <= event->x && event->x < char_right && y <= event->y && event->y < char_bottom) {
            printf("@@ hit on text: %c\n", *p);
            evcon->target = (View*)text;  return;
        }
        // advance to the next position
        x += wd;
    }
}

void target_inline_view(EventContext* evcon, ViewSpan* view_span) {
    FT_Face pa_face = evcon->face;  FontProp* pa_font = evcon->font;  float pa_space_width = evcon->space_width;
    evcon->font = &view_span->font;
    View* view = view_span->child;
    if (view) {
        evcon->face = load_styled_font(evcon->ui_context, evcon->face, &view_span->font);
        if (FT_Load_Char(evcon->face, ' ', FT_LOAD_RENDER)) {
            fprintf(stderr, "could not load space character\n");
            evcon->space_width = evcon->face->size->metrics.height >> 6;
        } else {
            evcon->space_width = evcon->face->glyph->advance.x >> 6;
        }        
        target_children(evcon, view);
    }
    else {
        printf("view has no child\n");
    }
    evcon->face = pa_face;  evcon->font = pa_font;  evcon->space_width = pa_space_width;
}

void target_block_view(EventContext* evcon, ViewBlock* view_block) {
    BlockBlot pa_block = evcon->block;
    View* view = view_block->child;
    if (view) {
        evcon->block.x = pa_block.x + view_block->x;  evcon->block.y = pa_block.y + view_block->y;
        target_children(evcon, view);
        if (!evcon->target) { // check the block itself
            float x = evcon->block.x, y = evcon->block.y;
            SDL_MouseMotionEvent* event = &evcon->event.mouse_motion;
            if (x <= event->x && event->x < x + view_block->width &&
                y <= event->y && event->y < y + view_block->height) {
                printf("@@ hit on block: %s\n", lxb_dom_element_local_name(lxb_dom_interface_element(view_block->node), NULL));
                evcon->target = (View*)view_block;
            }
        }
    }
    else {
        printf("view has no child\n");
    }
    evcon->block = pa_block;
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
}

void fire_inline_event(EventContext* evcon, ViewSpan* span) {
    printf("fire inline event\n");
    if (span->in_line && span->in_line->cursor) {
        printf("changing to new cursor\n");
        evcon->new_cursor = span->in_line->cursor;
    }
}

void fire_block_event(EventContext* evcon, ViewBlock* block) {
    printf("fire block event\n");
    // fire as inline view first
    fire_inline_event(evcon, (ViewSpan*)block);
}

void fire_events(EventContext* evcon, ArrayList* target_list) {
    int stack_size = target_list->length;
    for (int i = 0; i < stack_size; i++) {
        View* view = (View*)target_list->data[i];
        if (view->type == RDT_VIEW_BLOCK) {
            fire_block_event(evcon, (ViewBlock*)view);
        } else if (view->type == RDT_VIEW_INLINE) {
            fire_inline_event(evcon, (ViewSpan*)view);
        } else if (view->type == RDT_VIEW_TEXT) {
            fire_text_event(evcon, (ViewText*)view);
        }
    }
}

void event_context_init(EventContext* evcon, UiContext* uicon, RdtEvent* event) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    evcon->event = *event;
    if (event->type == SDL_MOUSEMOTION) {
        evcon->event.mouse_motion.x *= uicon->pixel_ratio;
        evcon->event.mouse_motion.y *= uicon->pixel_ratio;
    }
    // load default font Arial, size 16 px
    evcon->face = load_font_face(uicon, "Arial", 16);
    if (FT_Load_Char(evcon->face, ' ', FT_LOAD_RENDER)) {
        fprintf(stderr, "could not load space character\n");
        evcon->space_width = evcon->face->size->metrics.height >> 6;
    } else {
        evcon->space_width = evcon->face->glyph->advance.x >> 6;
    }
    evcon->new_cursor = LXB_CSS_VALUE_AUTO;
}

void event_context_cleanup(EventContext* evcon) {
}

void handle_event(UiContext* uicon, Document* doc, RdtEvent* event) {
    EventContext evcon;
    printf("Handling event %d\n", event->type);
    event_context_init(&evcon, uicon, event);

    // find target view based on mouse position
    switch (event->type) {
    case SDL_MOUSEMOTION:  
        SDL_MouseMotionEvent* motion = &event->mouse_motion;
        printf("Mouse event at (%d, %d)\n", motion->x, motion->y);
        float mouse_x = motion->x, mouse_y = motion->y;
        target_html_doc(&evcon, doc->view_tree->root);
        if (evcon.target) {
            printf("Target view found at position (%f, %f)\n", mouse_x, mouse_y);
            // build stack of views from root to target view
            ArrayList* target_list = build_view_stack(&evcon, evcon.target);

            // fire event to views in the stack
            fire_events(&evcon, target_list);
        } else {
            printf("No target view found at position (%f, %f)\n", mouse_x, mouse_y);
        }
        if (uicon->mouse_state.cursor != evcon.new_cursor) {
            printf("Change cursor to %d\n", evcon.new_cursor);
            uicon->mouse_state.cursor = evcon.new_cursor; // update the mouse state
            SDL_SystemCursor cursor;
            switch (evcon.new_cursor) {
            case LXB_CSS_VALUE_TEXT: cursor = SDL_SYSTEM_CURSOR_IBEAM; break;
            case LXB_CSS_VALUE_POINTER: cursor = SDL_SYSTEM_CURSOR_HAND; break;
            default: cursor = SDL_SYSTEM_CURSOR_ARROW; break;
            }
            SDL_Cursor* sdl_cursor = SDL_CreateSystemCursor(cursor);
            if (sdl_cursor) {
                if (uicon->mouse_state.sdl_cursor) {
                    SDL_FreeCursor(uicon->mouse_state.sdl_cursor);
                }
                uicon->mouse_state.sdl_cursor = sdl_cursor;
                SDL_SetCursor(sdl_cursor);
            }
        }        
        break;
    case SDL_MOUSEBUTTONDOWN:   case SDL_MOUSEBUTTONUP:
        printf("Mouse button event\n");
        break;
    case SDL_MOUSEWHEEL:
        printf("Mouse wheel event\n");
        break;
    default:
        break;
    }
    printf("end of event %d\n", event->type);

    event_context_cleanup(&evcon);
}

