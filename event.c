#include "view.h"

typedef struct EventContext {
    RdtEvent* event;
    View* target;

    BlockBlot block;
    FontProp* font; // current font style
    FT_Face face;   // current font face
    float space_width;
    FT_Library library;
    UiContext* ui_context;
} EventContext;

void target_children(EventContext* evcon, View* view) {
    do {
        if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            printf("fire view block:%s, x:%f, y:%f, wd:%f, hg:%f\n",
                lxb_dom_element_local_name(lxb_dom_interface_element(block->node), NULL),
                block->x, block->y, block->width, block->height);                
            target_block_view(evcon, block);
        }
        else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            printf("fire view inline:%s\n", lxb_dom_element_local_name(lxb_dom_interface_element(span->node), NULL));                
            target_inline_view(evcon, (ViewSpan*)view);
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
        MouseMotionEvent* event = (MouseMotionEvent*)evcon->event;
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
            MouseMotionEvent* event = (MouseMotionEvent*)evcon->event;
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

void event_context_init(EventContext* evcon, UiContext* uicon) {
    memset(evcon, 0, sizeof(EventContext));
    evcon->ui_context = uicon;
    // evcon->view_tree = uicon->document->view_tree;
}

void event_context_cleanup(EventContext* evcon) {
    // free cursor and caret states
}

void target_html_doc(UiContext* uicon, View* root_view) {
    EventContext evcon;
    printf("fire at HTML doc\n");
    event_context_init(&evcon, uicon);

    if (root_view && root_view->type == RDT_VIEW_BLOCK) {
        printf("fire root view:\n");
        target_block_view(&evcon, (ViewBlock*)root_view);
    }
    else {
        fprintf(stderr, "Invalid root view\n");
    }

    event_context_cleanup(&evcon);
}

View* target_view(EventContext* evcon, View* root_view, float mouse_x, float mouse_y) {
    View* view = root_view;
    while (view) {
        if (view->type == RDT_VIEW_BLOCK) {
            ViewBlock* block = (ViewBlock*)view;
            if (mouse_x >= block->x && mouse_x <= block->x + block->width &&
                mouse_y >= block->y && mouse_y <= block->y + block->height) {
                if (block->child) {
                    view = block->child;
                    continue;
                }
                return view;
            }
        } else if (view->type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            if (span->child) {
                view = span->child;
                continue;
            }
            return view;
        } else {
            ViewText* text = (ViewText*)view;
            return view;
        }
        view = view->next;
    }
    return NULL;
}

void build_view_stack(EventContext* evcon, View* view, View** stack, int* stack_size) {
    while (view) {
        stack[(*stack_size)++] = view;
        view = view->parent;
    }
}

void fire_mouse_events(EventContext* evcon, View** stack, int stack_size, MouseEvent* event) {
    for (int i = stack_size - 1; i >= 0; i--) {
        View* view = stack[i];
        // if (view->type == RDT_VIEW_BLOCK) {
        //     fire_block_mouse_event(evcon, (ViewBlock*)view, event);
        // } else if (view->type == RDT_VIEW_INLINE) {
        //     fire_inline_mouse_event(evcon, (ViewSpan*)view, event);
        // } else {
        //     fire_text_mouse_event(evcon, (ViewText*)view, event);
        // }
    }
}

void handle_mouse_event(UiContext* uicon, View* root_view, float mouse_x, float mouse_y, MouseEvent* event) {
    EventContext evcon;
    event_context_init(&evcon, uicon);

    // find target view based on mouse position
    View* target = target_view(&evcon, root_view, mouse_x, mouse_y);
    if (target) {
        View* stack[100];
        int stack_size = 0;
        // build stack of views from root to target view
        build_view_stack(&evcon, target, stack, &stack_size);
        // fire event to views in the stack
        fire_mouse_events(&evcon, stack, stack_size, event);
    } else {
        printf("No target view found at position (%f, %f)\n", mouse_x, mouse_y);
    }

    event_context_cleanup(&evcon);
}
