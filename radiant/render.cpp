#include "render.hpp"
#include "render_img.hpp"
#include "../lib/log.h"
#include "../lib/avl_tree.h"
#include "../lambda/input/css/css_style.hpp"
#include <string.h>
#include <math.h>
#include <chrono>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

#define DEBUG_RENDER 0

// ============================================================================
// CSS white-space Property Helpers for Rendering
// ============================================================================

/**
 * Check if whitespace should be preserved according to white-space property.
 * Returns true for: pre, pre-wrap, break-spaces
 * Returns false for: normal, nowrap, pre-line
 */
static inline bool ws_preserve_spaces(CssEnum ws) {
    return ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP || ws == CSS_VALUE_BREAK_SPACES;
}

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
DomDocument* load_html_doc(Url* base, const char* doc_url, int viewport_width, int viewport_height);

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_children(RenderContext* rdcon, View* view);
void scrollpane_render(Tvg_Canvas* canvas, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip);

// draw a color glyph bitmap (BGRA format, used for color emoji) into the doc surface
void draw_color_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->rows);
    if (left >= right || top >= bottom) return; // glyph outside the surface
    ImageSurface* surface = rdcon->ui_context->surface;
    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i) * surface->pitch;
        uint8_t* src_row = bitmap->buffer + i * bitmap->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;
            // BGRA format: Blue, Green, Red, Alpha (4 bytes per pixel)
            uint8_t* src = src_row + j * 4;
            uint8_t src_b = src[0], src_g = src[1], src_r = src[2], src_a = src[3];
            if (src_a > 0) {
                uint8_t* dst = (uint8_t*)(row_pixels + (x + j) * 4);
                if (src_a == 255) {
                    // fully opaque - just copy
                    dst[0] = src_r;  // our surface is RGBA
                    dst[1] = src_g;
                    dst[2] = src_b;
                    dst[3] = 255;
                } else {
                    // alpha blend
                    uint32_t inv_alpha = 255 - src_a;
                    dst[0] = (dst[0] * inv_alpha + src_r * src_a) / 255;
                    dst[1] = (dst[1] * inv_alpha + src_g * src_a) / 255;
                    dst[2] = (dst[2] * inv_alpha + src_b * src_a) / 255;
                    dst[3] = 255;
                }
            }
        }
    }
}

// draw a glyph bitmap into the doc surface
void draw_glyph(RenderContext* rdcon, FT_Bitmap *bitmap, int x, int y) {
    // handle color emoji bitmaps (BGRA format)
    if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        draw_color_glyph(rdcon, bitmap, x, y);
        return;
    }
    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->rows);
    if (left >= right || top >= bottom) {
        log_debug("glyph clipped: x=%d, y=%d, bitmap=%dx%d, clip=[%.0f,%.0f,%.0f,%.0f]",
            x, y, bitmap->width, bitmap->rows,
            rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
        return; // glyph outside the surface
    }
    ImageSurface* surface = rdcon->ui_context->surface;
    for (int i = top - y; i < bottom - y; i++) {
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i) * surface->pitch;
        for (int j = left - x; j < right - x; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;
            uint32_t intensity = bitmap->buffer[i * bitmap->pitch + j];
            if (intensity > 0) {
                // blend the pixel with the background
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);
                // important to use 32bit int for computation below
                uint32_t v = 255 - intensity;
                // can further optimize if background is a fixed color
                if (rdcon->color.c == 0xFF) { // black text color
                    p[0] = p[0] * v / 255;
                    p[1] = p[1] * v / 255;
                    p[2] = p[2] * v / 255;
                    p[3] = 0xFF;
                }
                else { // non-black text color
                    p[0] = (p[0] * v + rdcon->color.r * intensity) / 255;
                    p[1] = (p[1] * v + rdcon->color.g * intensity) / 255;
                    p[2] = (p[2] * v + rdcon->color.b * intensity) / 255;
                    p[3] = 0xFF;  // alpha channel
                }
            }
        }
    }
}

extern CssEnum get_white_space_value(DomNode* node);

void render_text_view(RenderContext* rdcon, ViewText* text_view) {
    log_debug("render_text_view clip:[%.0f,%.0f,%.0f,%.0f]",
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    if (!rdcon->font.ft_face) {
        log_debug("font face is null");
        return;
    }
    unsigned char* str = text_view->text_data();
    TextRect* text_rect = text_view->rect;

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = ws_preserve_spaces(white_space);

    // Check if parent inline element has a background color to render
    DomElement* parent_elem = text_view->parent ? text_view->parent->as_element() : nullptr;
    Color* bg_color = nullptr;
    if (parent_elem && parent_elem->bound && parent_elem->bound->background &&
        parent_elem->bound->background->color.a > 0) {
        bg_color = &parent_elem->bound->background->color;
    }

    while (text_rect) {
        float x = rdcon->block.x + text_rect->x, y = rdcon->block.y + text_rect->y;

        // Render background for inline element if present
        if (bg_color) {
            Rect bg_rect = {x, y, text_rect->width, text_rect->height};
            fill_surface_rect(rdcon->ui_context->surface, &bg_rect, bg_color->c, &rdcon->block.clip);
        }

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        log_debug("draw text:'%t', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, at (%f, %f), white_space:%d, preserve:%d, color:0x%08x",
            str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, x, y,
            white_space, preserve_spaces, rdcon->color.c);
        bool has_space = false;  uint32_t codepoint;
        while (p < end) {
            // log_debug("draw character '%c'", *p);
            if (is_space(*p)) {
                if (preserve_spaces || !has_space) {  // preserve all spaces or add single whitespace
                    has_space = true;
                    // log_debug("draw_space: %c, x:%f, end:%f", *p, x, x + rdcon->font.space_width);
                    x += rdcon->font.style->space_width;
                }
                // else  // skip consecutive spaces
                p++;
            }
            else {
                has_space = false;
                int bytes = utf8_to_codepoint(p, &codepoint);
                if (bytes <= 0) { p++;  codepoint = 0; }
                else { p += bytes; }

                FT_GlyphSlot glyph = load_glyph(rdcon->ui_context, rdcon->font.ft_face, rdcon->font.style, codepoint, true);
                if (!glyph) {
                    // draw a square box for missing glyph
                    Rect rect = {x + 1, y, (float)(rdcon->font.style->space_width - 2), (float)(rdcon->font.ft_face->size->metrics.y_ppem / 64.0)};
                    fill_surface_rect(rdcon->ui_context->surface, &rect, 0xFF0000FF, &rdcon->block.clip);
                    x += rdcon->font.style->space_width;
                }
                else {
                    // draw the glyph to the image buffer
                    float ascend = rdcon->font.ft_face->size->metrics.ascender / 64.0; // still use orginal font ascend to align glyphs at same baseline
                    draw_glyph(rdcon, &glyph->bitmap, x + glyph->bitmap_left, y + ascend - glyph->bitmap_top);
                    // advance to the next position
                    x += glyph->advance.x / 64.0;
                }
            }
        }
        // render text deco
        if (rdcon->font.style->text_deco != CSS_VALUE_NONE) {
            float thinkness = max(rdcon->font.ft_face->underline_thickness / 64.0, 1);
            Rect rect;
            // todo: underline probably shoul draw below/before the text, and leaves a gap where text has descender
            if (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE) {
                // underline drawn at baseline, with a gap of thickness
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y +
                    (rdcon->font.ft_face->size->metrics.ascender / 64.0) + thinkness;
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_OVERLINE) {
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y;
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_LINE_THROUGH) {
                rect.x = rdcon->block.x + text_rect->x;  rect.y = rdcon->block.y + text_rect->y + text_rect->height / 2;
            }
            rect.width = text_rect->width;  rect.height = thinkness; // corrected the variable name from h to height
            log_debug("text deco: %d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f", rdcon->font.style->text_deco,
                rect.x, rect.y, rect.width, rect.height); // corrected w to width
            fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
        }
        text_rect = text_rect->next;
    }
}

// Function to convert integer to Roman numeral
/*
static void toRoman(int num, char* result, int uppercase) {
    if (num <= 0 || num >= 4000) {
        strcpy(result, "invalid");
        return;
    }
    const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    const char* symbols_lower[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    const char* symbols_upper[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    const char** symbols = uppercase ? symbols_upper : symbols_lower;
    result[0] = '\0';
    int i = 0;
    while (num > 0) {
        while (num >= values[i]) {
            strcat(result, symbols[i]);
            num -= values[i];
        }
        i++;
    }
}

// list bullet formatting function
void formatListNumber(StrBuf* buf, int num, CssEnum list_style) {
    if (num <= 0) { return; }
    switch (list_style) {
        case CSS_VALUE_LOWER_ROMAN:
            toRoman(num, buf, 0);
            break;
        case CSS_VALUE_UPPER_ROMAN:
            toRoman(num, buf, 1);
            break;
        case CSS_VALUE_UPPER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'A' + (num - 1);
                result[1] = '\0';
            }
            break;
        case CSS_VALUE_LOWER_ALPHA:
            if (num > 26) {
                strcpy(result, "invalid");
            } else {
                result[0] = 'a' + (num - 1);
                result[1] = '\0';
            }
            break;
    }
}
*/

void render_list_bullet(RenderContext* rdcon, ViewBlock* list_item) {
    // bullets are aligned to the top and right side of the list item
    float ratio = rdcon->ui_context->pixel_ratio;
    if (rdcon->list.list_style_type == CSS_VALUE_DISC) {
        Rect rect;
        rect.x = rdcon->block.x + list_item->x - 15 * ratio;
        rect.y = rdcon->block.y + list_item->y + 7 * ratio;
        rect.width = rect.height = 5 * ratio;
        fill_surface_rect(rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip);
    }
    else if (rdcon->list.list_style_type == CSS_VALUE_DECIMAL) {
        log_debug("render list decimal");
        // StrBuf* num = strbuf_new_cap(10);
        // strbuf_append_format(num, "%d.", rdcon->list.item_index);
        // // output the number as VIEW_TEXT
        // lxb_dom_text_t lxb_node;  ViewText text;
        // // Initialize the lexbor text node structure properly
        // memset(&lxb_node, 0, sizeof(lxb_dom_text_t));
        // lxb_node.char_data.node.type = LXB_DOM_NODE_TYPE_TEXT;
        // lxb_node.char_data.data.data = (unsigned char *)num->str;
        // lxb_node.char_data.data.length = num->length;

        // // Initialize the ViewText structure
        // text.type = RDT_VIEW_TEXT;  text.next = NULL;  text.parent = NULL;
        // text.font = rdcon->font.style;
        // TextRect text_rect;
        // text.rect = &text_rect;  text_rect.next = NULL;
        // text_rect.start_index = 0;  text_rect.length = num->length;

        // // Create DomNode wrapper
        // DomNode dom_wrapper;
        // memset(&dom_wrapper, 0, sizeof(DomNode));
        // dom_wrapper.type = LEXBOR_NODE;
        // dom_wrapper.lxb_node = (lxb_dom_node_t*)&lxb_node;
        // text.node = &dom_wrapper;
        // float font_size = rdcon->font.ft_face->size->metrics.y_ppem / 64.0;
        // text.x = list_item->x - 20 * ratio;
        // text.y = list_item->y;  // align at top the list item
        // text.width = text_rect.length * font_size;  text.height = font_size;
        // render_text_view(rdcon, &text);
        // strbuf_free(num);
    }
    else {
        log_debug("unknown list style type");
    }
}

void render_litem_view(RenderContext* rdcon, ViewBlock* list_item) {
    log_debug("view list item:%s", list_item->node_name());
    rdcon->list.item_index++;
    render_block_view(rdcon, list_item);
}

void render_list_view(RenderContext* rdcon, ViewBlock* view) {
    ViewBlock* list = (ViewBlock*)view;
    log_debug("view list:%s", list->node_name());
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;  rdcon->list.list_style_type = list->blk->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}

// Helper function to render linear gradient
static void render_linear_gradient(RenderContext* rdcon, Rect* rect, BackgroundProp* bg, Bound* clip) {
    LinearGradient* lg = bg->linear_gradient;
    if (!lg || lg->stop_count < 2) return;
    
    ImageSurface* surface = rdcon->ui_context->surface;
    
    // Convert angle to radians (CSS: 0deg = to top, 90deg = to right)
    // CSS angle convention: 0 = up, increases clockwise
    float angle_rad = (lg->angle - 90.0f) * 3.14159265f / 180.0f;
    
    // Calculate gradient direction vector
    float dx = cosf(angle_rad);
    float dy = sinf(angle_rad);
    
    // Calculate the gradient line length for this rect
    // The gradient line goes through the center and extends to corners
    float half_w = rect->width / 2.0f;
    float half_h = rect->height / 2.0f;
    float gradient_length = fabsf(dx) * rect->width + fabsf(dy) * rect->height;
    
    // Fill the rectangle with gradient
    int x0 = (int)rect->x;
    int y0 = (int)rect->y;
    int x1 = (int)(rect->x + rect->width);
    int y1 = (int)(rect->y + rect->height);
    
    // Clipping
    if (clip) {
        x0 = max(x0, (int)clip->left);
        y0 = max(y0, (int)clip->top);
        x1 = min(x1, (int)clip->right);
        y1 = min(y1, (int)clip->bottom);
    }
    
    // Center of the gradient
    float cx = rect->x + half_w;
    float cy = rect->y + half_h;
    
    // Draw pixel by pixel
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int pitch = surface->pitch / 4;  // pitch in pixels
    
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            // Calculate position along gradient line (0.0 to 1.0)
            float px = (float)x - cx;
            float py = (float)y - cy;
            float proj = (px * dx + py * dy) / (gradient_length / 2.0f);
            float t = (proj + 1.0f) / 2.0f;  // normalize to 0..1
            t = fmaxf(0.0f, fminf(1.0f, t));
            
            // Find the two color stops to interpolate between
            Color c1 = lg->stops[0].color;
            Color c2 = lg->stops[lg->stop_count - 1].color;
            float t1 = 0.0f, t2 = 1.0f;
            
            for (int i = 0; i < lg->stop_count - 1; i++) {
                if (t >= lg->stops[i].position && t <= lg->stops[i + 1].position) {
                    c1 = lg->stops[i].color;
                    c2 = lg->stops[i + 1].color;
                    t1 = lg->stops[i].position;
                    t2 = lg->stops[i + 1].position;
                    break;
                }
            }
            
            // Interpolate between the two colors
            float local_t = (t2 > t1) ? (t - t1) / (t2 - t1) : 0.0f;
            uint8_t r = (uint8_t)(c1.r + local_t * (c2.r - c1.r));
            uint8_t g = (uint8_t)(c1.g + local_t * (c2.g - c1.g));
            uint8_t b = (uint8_t)(c1.b + local_t * (c2.b - c1.b));
            uint8_t a = (uint8_t)(c1.a + local_t * (c2.a - c1.a));
            
            // Pack color (RGBA format, same as fill_surface_rect)
            uint32_t color = (r << 24) | (g << 16) | (b << 8) | a;
            pixels[y * pitch + x] = color;
        }
    }
    log_debug("Rendered linear gradient: angle=%.1f, %d stops, rect=(%.0f,%.0f,%.0f,%.0f)",
        lg->angle, lg->stop_count, rect->x, rect->y, rect->width, rect->height);
}

void render_bound(RenderContext* rdcon, ViewBlock* view) {
    Rect rect;
    rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
    rect.width = view->width;  rect.height = view->height;
    
    // Check for gradient background first
    if (view->bound->background && view->bound->background->gradient_type == GRADIENT_LINEAR &&
        view->bound->background->linear_gradient) {
        render_linear_gradient(rdcon, &rect, view->bound->background, &rdcon->block.clip);
    }
    // fill background, if bg-color is non-transparent
    else if (view->bound->background && (view->bound->background->color.a)) {
        if (view->bound->border && (view->bound->border->radius.top_left > 0 ||
            view->bound->border->radius.top_right > 0 || view->bound->border->radius.bottom_left > 0 ||
            view->bound->border->radius.bottom_right > 0)) {
            // fill the background with rounded corners
            tvg_canvas_remove(rdcon->canvas, NULL);  // clear any existing shapes
            Tvg_Paint* bg_shape = tvg_shape_new();
            tvg_shape_append_rect(bg_shape, rect.x, rect.y, rect.width, rect.height,
                view->bound->border->radius.top_left, view->bound->border->radius.top_right);
            Color bgcolor = view->bound->background->color;
            tvg_shape_set_fill_color(bg_shape, bgcolor.r, bgcolor.g, bgcolor.b, bgcolor.a);
            // clip the svg picture
            Tvg_Paint* clip_rect = tvg_shape_new();  Bound* clip = &rdcon->block.clip;
            tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
            tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill
            tvg_paint_set_mask_method(bg_shape, clip_rect, TVG_MASK_METHOD_ALPHA);
            tvg_canvas_push(rdcon->canvas, bg_shape);
            tvg_canvas_draw(rdcon->canvas, false);
            tvg_canvas_sync(rdcon->canvas);
        } else {
            fill_surface_rect(rdcon->ui_context->surface, &rect, view->bound->background->color.c, &rdcon->block.clip);
        }
    }
    if (view->bound->border) {
        log_debug("render border");
        if (view->bound->border->left_color.a) {
            Rect border_rect = rect;
            border_rect.width = view->bound->border->width.left;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->left_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->right_color.a) {
            Rect border_rect = rect;
            border_rect.x = rect.x + rect.width - view->bound->border->width.right;
            border_rect.width = view->bound->border->width.right;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->right_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->top_color.a) {
            Rect border_rect = rect;
            border_rect.height = view->bound->border->width.top;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->top_color.c, &rdcon->block.clip);
        }
        if (view->bound->border->bottom_color.a) {
            Rect border_rect = rect;
            border_rect.y = rect.y + rect.height - view->bound->border->width.bottom;
            border_rect.height = view->bound->border->width.bottom;
            fill_surface_rect(rdcon->ui_context->surface, &border_rect, view->bound->border->bottom_color.c, &rdcon->block.clip);
        }
    }
}

void draw_debug_rect(Tvg_Canvas* canvas, Rect rect, Bound* clip) {
    tvg_canvas_remove(canvas, NULL);  // clear any existing shapes
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, rect.x, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y);
    tvg_shape_line_to(shape, rect.x + rect.width, rect.y + rect.height);
    tvg_shape_line_to(shape, rect.x, rect.y + rect.height);
    tvg_shape_close(shape);
    tvg_shape_set_stroke_width(shape, 2); // stroke width of 2 pixels
    tvg_shape_set_stroke_color(shape, 255, 0, 0, 100); // Red stroke color (RGBA)
    // define the dash pattern for a dotted line
    float dash_pattern[2] = {8.0f, 8.0f}; // 8 units on, 8 units off
    tvg_shape_set_stroke_dash(shape, dash_pattern, 2, 0);

    // set clipping
    Tvg_Paint* clip_rect = tvg_shape_new();
    tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill
    tvg_paint_set_mask_method(shape, clip_rect, TVG_MASK_METHOD_ALPHA);

    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);
}

void setup_scroller(RenderContext* rdcon, ViewBlock* block) {
    if (block->scroller->has_clip) {
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            block->scroller->clip.left, block->scroller->clip.top, block->scroller->clip.right, block->scroller->clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + block->scroller->clip.left);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + block->scroller->clip.top);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + block->scroller->clip.right);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + block->scroller->clip.bottom);
    }
    if (block->scroller->pane) {
        rdcon->block.x -= block->scroller->pane->h_scroll_position;
        rdcon->block.y -= block->scroller->pane->v_scroll_position;
    }
}

void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    rdcon->block.x = pa_block->x + block->x;  rdcon->block.y = pa_block->y + block->y;
    if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width, block->height};
        if (block->bound && block->bound->border) {
            rect.x += block->bound->border->width.left;
            rect.y += block->bound->border->width.top;
            rect.width -= block->bound->border->width.left + block->bound->border->width.right;
            rect.height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
        if (block->scroller->pane) {
            scrollpane_render(rdcon->canvas, block->scroller->pane, &rect,
                block->content_width, block->content_height, &rdcon->block.clip);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    if (block->font) {
        setup_font(rdcon->ui_context, &rdcon->font, block->font);
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    if (block->view_type == RDT_VIEW_LIST_ITEM) {
        render_list_bullet(rdcon, block);
    }
    if (block->bound) {
        // CSS 2.1 Section 17.6.1: empty-cells: hide suppresses borders/backgrounds
        bool skip_bound = false;
        if (block->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = (ViewTableCell*)block;
            if (cell->td && cell->td->hide_empty) {
                skip_bound = true;
                log_debug("Skipping bound for empty cell (empty-cells: hide)");
            }
        }
        if (!skip_bound) {
            render_bound(rdcon, block);
        }
    }

    rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;
    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top : 0);
        rc.width = block->width + (block->bound ? block->bound->margin.left + block->bound->margin.right : 0);
        rc.height = block->height + (block->bound ? block->bound->margin.top + block->bound->margin.bottom : 0);
        draw_debug_rect(rdcon->canvas, rc, &rdcon->block.clip);
    }

    View* view = block->first_child;
    if (view) {
        if (block->in_line && block->in_line->color.c) {
            rdcon->color = block->in_line->color;
        }
        // setup clip box
        if (block->scroller) {
            setup_scroller(rdcon, block);
        }
        // render negative z-index children
        render_children(rdcon, view);
        // render positive z-index children
        if (block->position) {
            log_debug("render absolute/fixed positioned children");
            ViewBlock* child_block = block->position->first_abs_child;
            while (child_block) {
                render_block_view(rdcon, child_block);
                child_block = child_block->position->next_abs_sibling;
            }
        }
    }
    else {
        log_debug("view has no child");
    }

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
    log_leave();
}

void render_svg(ImageSurface* surface) {
    if (!surface->pic) {
        log_debug("no picture to render");  return;
    }
    // Step 1: Create an offscreen canvas to render the original Picture
    Tvg_Canvas* canvas = tvg_swcanvas_create();
    if (!canvas) return;

    uint32_t width = surface->max_render_width;
    uint32_t height = surface->max_render_width * surface->height / surface->width;
    surface->pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!surface->pixels) {
        tvg_canvas_destroy(canvas);
        return;
    }

    // Set the canvas target to the buffer
    if (tvg_swcanvas_set_target(canvas, (uint32_t*)surface->pixels, width, width, height,
        TVG_COLORSPACE_ABGR8888) != TVG_RESULT_SUCCESS) {
        log_debug("Failed to set canvas target");
        free(surface->pixels);  surface->pixels = NULL;
        tvg_canvas_destroy(canvas);
        return;
    }

    tvg_picture_set_size(surface->pic, width, height);
    tvg_canvas_push(canvas, surface->pic);
    tvg_canvas_update(canvas);
    tvg_canvas_draw(canvas, true);
    tvg_canvas_sync(canvas);

    // Step 4: Clean up canvas
    tvg_canvas_destroy(canvas); // this also frees pic
    surface->pic = NULL;
    surface->width = width;  surface->height = height;  surface->pitch = width * sizeof(uint32_t);
}

// load surface pixels to a picture
Tvg_Paint* load_picture(ImageSurface* surface) {
    Tvg_Paint* pic = tvg_picture_new();
    if (!pic) { return NULL; }

    // Load the raw pixel data into the new Picture
    if (tvg_picture_load_raw(pic, (uint32_t*)surface->pixels, surface->width, surface->height,
        TVG_COLORSPACE_ABGR8888, false) != TVG_RESULT_SUCCESS) {
        log_debug("Failed to load raw pixel data");
        tvg_paint_del(pic);
        return NULL;
    }
    return pic;
}

void render_image_view(RenderContext* rdcon, ViewBlock* view) {
    log_debug("render image view");
    log_enter();
    // render border and background, etc.
    render_block_view(rdcon, (ViewBlock*)view);
    // render the image
    if (view->embed && view->embed->img) {
        log_debug("image view has embed image");
        ImageSurface* img = view->embed->img;
        Rect rect;
        rect.x = rdcon->block.x + view->x;  rect.y = rdcon->block.y + view->y;
        rect.width = view->width;  rect.height = view->height;
        if (img->format == IMAGE_FORMAT_SVG) {
            // render the SVG image
            log_debug("render svg image at x:%f, y:%f, wd:%f, hg:%f", rect.x, rect.y, rect.width, rect.height);
            if (!img->pixels) {
                render_svg(img);
            }
            Tvg_Paint* pic = load_picture(img);
            if (pic) {
                tvg_canvas_remove(rdcon->canvas, NULL);  // clear any existing shapes
                tvg_picture_set_size(pic, rect.width, rect.height);
                tvg_paint_translate(pic, rect.x, rect.y);
                // clip the svg picture
                Tvg_Paint* clip_rect = tvg_shape_new();  Bound* clip = &rdcon->block.clip;
                tvg_shape_append_rect(clip_rect, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
                tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255); // solid fill
                tvg_paint_set_mask_method(pic, clip_rect, TVG_MASK_METHOD_ALPHA);
                tvg_canvas_push(rdcon->canvas, pic);
                tvg_canvas_draw(rdcon->canvas, false);
                tvg_canvas_sync(rdcon->canvas);
            } else {
                log_debug("failed to load svg picture");
            }
        } else {
            log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", rect.x, rect.y, rect.width, rect.height);
            blit_surface_scaled(img, NULL, rdcon->ui_context->surface, &rect, &rdcon->block.clip, SCALE_MODE_LINEAR);
        }
    }
    else {
        log_debug("image view has no embed image");
    }
    log_debug("end of image render");
    log_leave();
}

void render_embed_doc(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;
    if (block->bound) { render_bound(rdcon, block); }

    rdcon->block.x = pa_block.x + block->x;  rdcon->block.y = pa_block.y + block->y;
    // setup clip box
    if (block->scroller) { setup_scroller(rdcon, block); }
    // render the embedded doc
    if (block->embed && block->embed->doc) {
        DomDocument* doc = block->embed->doc;
        // render html doc
        if (doc && doc->view_tree && doc->view_tree->root) {
            View* root_view = doc->view_tree->root;
            if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
                log_debug("render doc root view:");
                // load default font
                FontBox pa_font = rdcon->font;
                FontProp* default_font = doc->view_tree->html_version == HTML5 ? &rdcon->ui_context->default_font : &rdcon->ui_context->legacy_default_font;
                log_debug("render_init default font: %s, html version: %d", default_font->family, doc->view_tree->html_version);
                setup_font(rdcon->ui_context, &rdcon->font, default_font);

                render_block_view(rdcon, (ViewBlock*)root_view);

                rdcon->font = pa_font;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");
    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->color.c) {
            rdcon->color = view_span->in_line->color;
        }
        render_children(rdcon, view);
    }
    else {
        log_debug("view has no child");
    }
    rdcon->font = pa_font;  rdcon->color = pa_color;
}

void render_children(RenderContext* rdcon, View* view) {
    do {
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_TABLE || view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW || view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewBlock* block = (ViewBlock*)view;
            if (block->embed) {
                if (block->embed->img) {
                    render_image_view(rdcon, block);
                }
                else if (block->embed->doc) {
                    render_embed_doc(rdcon, block);
                }
                else if (block->embed->flex) {
                    render_block_view(rdcon, block);
                }
            }
            else if (block->blk && block->blk->list_style_type) {
                render_list_view(rdcon, block);
            }
            else {
                // Skip only absolute/fixed positioned elements - they are rendered separately
                // Floats (which also have position struct) should be rendered in normal flow
                if (block->position &&
                    (block->position->position == CSS_VALUE_ABSOLUTE ||
                     block->position->position == CSS_VALUE_FIXED)) {
                    log_debug("absolute/fixed positioned block, skip in normal rendering");
                } else {
                    render_block_view(rdcon, block);
                }
            }
        }
        else if (view->view_type == RDT_VIEW_LIST_ITEM) {
            render_litem_view(rdcon, (ViewBlock*)view);
        }
        else if (view->view_type == RDT_VIEW_INLINE) {
            ViewSpan* span = (ViewSpan*)view;
            render_inline_view(rdcon, span);
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            render_text_view(rdcon, text);
        }
        else {
            log_debug("unknown view in rendering: %d", view->view_type);
        }
        view = view->next();
    } while (view);
}

void render_init(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;
    rdcon->canvas = tvg_swcanvas_create();
    tvg_swcanvas_set_target(rdcon->canvas, (uint32_t*)uicon->surface->pixels, uicon->surface->width,
        uicon->surface->width, uicon->surface->height, TVG_COLORSPACE_ABGR8888);

    // load default font
    FontProp* default_font = view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    log_debug("render_init default font: %s, html version: %d", default_font->family, view_tree->html_version);
    setup_font(uicon, &rdcon->font, default_font);
    rdcon->block.clip = (Bound){0, 0, (float)uicon->surface->width, (float)uicon->surface->height};
    log_debug("render_init clip: [%.0f, %.0f, %.0f, %.0f]", rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
}

void render_clean_up(RenderContext* rdcon) {
    tvg_canvas_destroy(rdcon->canvas);
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    RenderContext rdcon;
    log_debug("Render HTML doc");
    render_init(&rdcon, uicon, view_tree);

    // fill the surface with a white background
    fill_surface_rect(rdcon.ui_context->surface, NULL, 0xFFFFFFFF, &rdcon.block.clip);

    auto t_init = high_resolution_clock::now();

    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("Render root view");
        render_block_view(&rdcon, (ViewBlock*)root_view);
        // render positioned children
        if (((ViewBlock*)root_view)->position) {
            log_debug("render absolute/fixed positioned children of root view");
            ViewBlock* child_block = ((ViewBlock*)root_view)->position->first_abs_child;
            while (child_block) {
                render_block_view(&rdcon, child_block);
                child_block = child_block->position->next_abs_sibling;
            }
        }
    }
    else {
        log_error("Invalid root view");
    }

    auto t_render = high_resolution_clock::now();
    log_info("[TIMING] render_block_view: %.1fms", duration<double, std::milli>(t_render - t_init).count());

    // all shapes should already have been drawn to the canvas
    // tvg_canvas_draw(rdcon.canvas, false); // no clearing of the buffer
    tvg_canvas_sync(rdcon.canvas);  // wait for async draw operation to complete

    auto t_sync = high_resolution_clock::now();
    log_info("[TIMING] tvg_canvas_sync: %.1fms", duration<double, std::milli>(t_sync - t_render).count());

    // save the rendered surface to image file (PNG or JPEG based on extension)
    if (output_file) {
        const char* ext = strrchr(output_file, '.');
        if (ext && (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
            save_surface_to_jpeg(rdcon.ui_context->surface, output_file, 85); // Default quality 85
        } else {
            save_surface_to_png(rdcon.ui_context->surface, output_file);
        }
    }

    auto t_save = high_resolution_clock::now();
    if (output_file) {
        log_info("[TIMING] save_to_file: %.1fms", duration<double, std::milli>(t_save - t_sync).count());
    }

    render_clean_up(&rdcon);
    if (uicon->document->state) {
        uicon->document->state->is_dirty = false;
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc total: %.1fms", duration<double, std::milli>(t_end - t_start).count());
}
