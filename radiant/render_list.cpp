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
        return;
    }

    float x = rdcon->block.x + marker->x;
    float y = rdcon->block.y + marker->y;
    float width = marker_prop->width;
    float content_width = marker_prop->content_width > 0.0f
        ? marker_prop->content_width : width;
    float bullet_size = marker_prop->bullet_size;
    CssEnum marker_type = marker_prop->marker_type;
    Color color = rdcon->color;
    const FontMetrics* marker_metrics = font_box_handle(&rdcon->font)
        ? font_get_metrics(font_box_handle(&rdcon->font)) : NULL;
    float marker_font_size = marker_metrics
        ? font_handle_get_physical_size_px(font_box_handle(&rdcon->font)) : 16.0f;


    if (marker_prop->is_image_marker) {
        Rect image_rect = {
            x, y + (marker->height - marker_prop->height) / 2.0f,
            content_width, marker_prop->height
        };
        switch (marker_prop->image.gradient_type) {
            case GRADIENT_LINEAR:
                render_list_marker_linear_gradient(
                    rdcon, marker_prop->image.linear_gradient, image_rect);
                return;
            case GRADIENT_RADIAL:
                render_list_marker_radial_gradient(
                    rdcon, marker_prop->image.radial_gradient, image_rect);
                return;
            case GRADIENT_CONIC:
                render_list_marker_conic_gradient(
                    rdcon, marker_prop->image.conic_gradient, image_rect);
                return;
            default:
                break;
        }
    }

    if (marker_prop->image.url && strcmp(marker_prop->image.url, "none") != 0) {
        if (!marker_prop->loaded_image) {
            marker_prop->loaded_image = load_image(rdcon->ui_context, marker_prop->image.url);
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
            // Paint the decoded source into the used marker box; raw pixels
            // would leave intrinsic images unzoomed after layout scales them.
            float img_w = content_width > 0.0f ? content_width : (float)img->width;
            float img_h = marker_prop->height > 0.0f ? marker_prop->height : (float)img->height;
            float ix = x;
            if (marker_prop->is_outside) {
                // outside image markers align to the font-relative marker field;
                // placing them at the box start incorrectly anchors post-image space.
                ix = x + width - marker_font_size - img_w / 2.0f;
            }
            float iy = y + marker->height / 2.0f - img_h / 2.0f;
            // display-list image replay expects decoded dimensions and uint32_t row stride.
            int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
            int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
            rc_draw_image(rdcon, (uint32_t*)img->pixels, src_w, src_h,
                          img->pitch / 4, ix, iy, img_w, img_h, 255, nullptr, img);
            return;
        }
    }

    float marker_cx = x + width - marker_font_size;
    float marker_cy = y + marker->height / 2.0f;

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            float radius = bullet_size / 2.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, marker_cx, marker_cy, radius, radius);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            break;
        }

        case CSS_VALUE_CIRCLE: {
            float radius = bullet_size / 2.0f;
            float stroke_width = 1.0f;

            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, marker_cx, marker_cy, radius - stroke_width/2, radius - stroke_width/2);
            rc_stroke_path(rdcon, p, color, stroke_width, RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
            rdt_path_free(p);
            break;
        }

        case CSS_VALUE_SQUARE: {
            float sx = marker_cx - bullet_size / 2.0f;
            float sy = marker_cy - bullet_size / 2.0f;

            rc_fill_rect(rdcon, sx, sy, bullet_size, bullet_size, color);
            break;
        }

        case CSS_VALUE_DISCLOSURE_CLOSED: {
            float tri_size = bullet_size * 1.6f;

            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, marker_cx, marker_cy - tri_size / 2.0f);
            rdt_path_line_to(p, marker_cx + tri_size, marker_cy);
            rdt_path_line_to(p, marker_cx, marker_cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            break;
        }

        case CSS_VALUE_DISCLOSURE_OPEN: {
            float tri_size = bullet_size * 1.6f;

            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, marker_cx - tri_size / 2.0f, marker_cy - tri_size / 2.0f);
            rdt_path_line_to(p, marker_cx + tri_size / 2.0f, marker_cy - tri_size / 2.0f);
            rdt_path_line_to(p, marker_cx, marker_cy + tri_size / 2.0f);
            rdt_path_close(p);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
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
            if (marker_prop->text_content && *marker_prop->text_content && font_box_handle(&rdcon->font)) {
                float s = rdcon->raster_scale;
                const FontMetrics* _mk = font_get_metrics(font_box_handle(&rdcon->font));
                float ascend = _mk ? (_mk->hhea_ascender * s) : 12.0f;

                float total_text_width = 0.0f;
                const char* p = marker_prop->text_content;
                while (*p) {
                    uint32_t cp;
                    int bytes = str_utf8_decode(p, strlen(p), &cp);
                    if (bytes <= 0) { p++; continue; }
                    p += bytes;
                    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);
                    LoadedGlyph* glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd, cp, false);
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
                    LoadedGlyph* glyph = font_load_glyph(font_box_handle(&rdcon->font), &sd, cp, true);
                    if (glyph) {
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(tx + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        tx += glyph->advance_x + rdcon->font.style->letter_spacing * s;
                    } else {
                        tx += rdcon->font.style->space_width * s;
                    }
                }

            }
            break;
        }

        default:
            break;
    }
}

void render_litem_view(RenderContext* rdcon, ViewBlock* list_item) {
    rdcon->list.item_index++;
    render_block_view(rdcon, list_item);
}

void render_list_view(RenderContext* rdcon, ViewBlock* view) {
    ViewBlock* list = lam::view_require_block(view);
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;
    rdcon->list.list_style_type = list->block()->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}
