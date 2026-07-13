#include "render.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/str.h"

#include <math.h>
#include <string.h>

void render_marker_view(RenderContext* rdcon, ViewSpan* marker) {
    if (!marker || !marker->is_element()) return;

    DomElement* elem = lam::dom_require_element(lam::view_dom_node(marker));
    MarkerProp* marker_prop = (MarkerProp*)elem->blk;
    if (!marker_prop) {
        log_debug("[MARKER RENDER] No marker_prop found");
        return;
    }

    float x = rdcon->block.x + marker->x;
    float y = rdcon->block.y + marker->y;
    float width = marker_prop->width;
    float bullet_size = marker_prop->bullet_size;
    CssEnum marker_type = marker_prop->marker_type;
    Color color = rdcon->color;

    log_debug("[MARKER RENDER] type=%d, x=%.1f, y=%.1f, width=%.1f, bullet_size=%.1f",
              marker_type, x, y, width, bullet_size);

    if (marker_prop->image_url && strcmp(marker_prop->image_url, "none") != 0) {
        if (!marker_prop->loaded_image) {
            marker_prop->loaded_image = load_image(rdcon->ui_context, marker_prop->image_url);
        }
        if (marker_prop->loaded_image && marker_prop->loaded_image->pic) {
            float iw, ih;
            rdt_picture_get_size(marker_prop->loaded_image->pic, &iw, &ih);
            if (iw > 0 && ih > 0) {
                int target_w = (int)(iw + 0.5f); // INT_CAST_OK: SVG marker intrinsic width rounded to pixels.
                int target_h = (int)(ih + 0.5f); // INT_CAST_OK: SVG marker intrinsic height rounded to pixels.
                if (target_w < 1) target_w = 1;
                if (target_h < 1) target_h = 1;
                // list markers are often loaded during layout; rasterize any
                // vector marker that reaches paint without decoded pixels.
                marker_prop->loaded_image->max_render_width = target_w;
                render_media_rasterize_svg_picture(marker_prop->loaded_image,
                                                   target_w, target_h);
            }
        }
        ImageSurface* img = marker_prop->loaded_image;
        if (img && img->pixels && img->width > 0 && img->height > 0) {
            image_surface_ensure_decoded(img, img->width, img->height);
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float img_w = (float)img->width;
            float img_h = (float)img->height;
            float ix = x + width - font_size - img_w / 2.0f;
            float iy = y + marker->height / 2.0f - img_h / 2.0f;
            // display-list image replay expects decoded dimensions and uint32_t row stride.
            int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
            int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
            rc_draw_image(rdcon, (uint32_t*)img->pixels, src_w, src_h,
                          img->pitch / 4, ix, iy, img_w, img_h, 255, nullptr, img);
            log_debug("[MARKER RENDER] Drew list-style-image at (%.1f, %.1f) size %.0fx%.0f",
                      ix, iy, img_w, img_h);
            return;
        }
        log_debug("[MARKER RENDER] list-style-image failed to load, falling back to marker_type");
    }

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float radius = bullet_size / 2.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, cx, cy, radius, radius);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disc at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_CIRCLE: {
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float radius = bullet_size / 2.0f;
            float stroke_width = 1.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, cx, cy, radius - stroke_width/2, radius - stroke_width/2);
            rc_stroke_path(rdcon, p, color, stroke_width, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew circle outline at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_SQUARE: {
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float sx = cx - bullet_size / 2.0f;
            float sy = cy - bullet_size / 2.0f;

            rc_fill_rect(rdcon, sx, sy, bullet_size, bullet_size, color);
            log_debug("[MARKER RENDER] Drew square at (%.1f, %.1f) size=%.1f", sx, sy, bullet_size);
            break;
        }

        case CSS_VALUE_DISCLOSURE_CLOSED: {
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, cx, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx + tri_size, cy);
            rdt_path_line_to(p, cx, cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disclosure-closed triangle at (%.1f, %.1f)", cx, cy);
            break;
        }

        case CSS_VALUE_DISCLOSURE_OPEN: {
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, cx - tri_size / 2.0f, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx + tri_size / 2.0f, cy - tri_size / 2.0f);
            rdt_path_line_to(p, cx, cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disclosure-open triangle at (%.1f, %.1f)", cx, cy);
            break;
        }

        case CSS_VALUE_DECIMAL:
        case CSS_VALUE_DECIMAL_LEADING_ZERO:
        case CSS_VALUE_LOWER_ROMAN:
        case CSS_VALUE_UPPER_ROMAN:
        case CSS_VALUE_LOWER_ALPHA:
        case CSS_VALUE_UPPER_ALPHA:
        case CSS_VALUE_LOWER_LATIN:
        case CSS_VALUE_UPPER_LATIN:
        case CSS_VALUE_LOWER_GREEK:
        case CSS_VALUE_ARMENIAN:
        case CSS_VALUE_GEORGIAN: {
            if (marker_prop->text_content && *marker_prop->text_content && rdcon->font.font_handle) {
                float s = rdcon->scale;
                const FontMetrics* _mk = font_get_metrics(rdcon->font.font_handle);
                float ascend = _mk ? (_mk->hhea_ascender * s) : 12.0f;

                float total_text_width = 0.0f;
                const char* p = marker_prop->text_content;
                while (*p) {
                    uint32_t cp;
                    int bytes = str_utf8_decode(p, strlen(p), &cp);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;
                    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
                    LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, cp, false);
                    total_text_width += glyph ? glyph->advance_x + rdcon->font.style->letter_spacing * s : (rdcon->font.style->space_width * s);
                }

                float tx = x + (width * s) - total_text_width;
                p = marker_prop->text_content;
                while (*p) {
                    uint32_t cp;
                    int bytes = str_utf8_decode(p, strlen(p), &cp);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;

                    if (cp == ' ') {
                        tx += rdcon->font.style->space_width * s;
                        continue;
                    }

                    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
                    LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, cp, true);
                    if (glyph) {
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(tx + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        tx += glyph->advance_x + rdcon->font.style->letter_spacing * s;
                    } else {
                        tx += rdcon->font.style->space_width * s;
                    }
                }

                log_debug("[MARKER RENDER] Text marker: '%s' at x=%.1f y=%.1f w=%.1f",
                          marker_prop->text_content, x, y, width);
            }
            break;
        }

        default:
            log_debug("[MARKER RENDER] Unsupported marker type: %d", marker_type);
            break;
    }
}

void render_list_bullet(RenderContext* rdcon, ViewBlock* list_item) {
    float ratio = rdcon->ui_context->pixel_ratio;
    if (rdcon->list.list_style_type == CSS_VALUE_DISC) {
        Rect rect;
        rect.x = rdcon->block.x + list_item->x - 15 * ratio;
        rect.y = rdcon->block.y + list_item->y + 7 * ratio;
        rect.width = rect.height = 5 * ratio;
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, rdcon->color.c,
            &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
    else if (rdcon->list.list_style_type == CSS_VALUE_DECIMAL) {
        log_debug("render list decimal");
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
    ViewBlock* list = lam::view_require_block(view);
    log_debug("view list:%s", list->node_name());
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;
    rdcon->list.list_style_type = list->blk->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}
