#include "render.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/str.h"

#include <math.h>
#include <string.h>

static Rect marker_image_rect(ViewSpan* marker, const MarkerProp* prop,
                              float origin_x, float origin_y, float font_size, float scale) {
    float width = (prop->content_width > 0.0f ? prop->content_width : prop->width) * scale;
    float height = prop->height * scale;
    float x = origin_x + marker->x * scale;
    if (prop->is_outside && !prop->is_image_marker)
        x += prop->width * scale - font_size - width / 2.0f;
    return {x, origin_y + (marker->y + marker->height / 2.0f) * scale - height / 2.0f,
        width, height};
}

bool render_paint_marker_image(PaintList* paint, UiContext* ui, ViewSpan* marker,
                              float origin_x, float origin_y, FontBox* font) {
    if (!paint || !ui || !marker || !marker->is_element()) return false;
    DomElement* element = lam::dom_require_element(lam::view_dom_node(marker));
    MarkerProp* prop = element->marker_prop();
    if (!prop) return false;
    float font_size = element->font ? font_prop_used_size(element->font)
        : font && font->style ? font_prop_used_size(font->style) : 16.0f;
    Rect rect = marker_image_rect(marker, prop, origin_x, origin_y, font_size, 1.0f);
    if (prop->is_image_marker)
        return render_paint_gradient_image(paint, &prop->image, rect, ui_context_raster_scale(ui), nullptr, true);
    if (!prop->image.url || strcmp(prop->image.url, "none") == 0) return false;
    ImageSurface* image = load_document_image(ui->document, ui, prop->image.url);
    if (!image) return false;
    PaintImageBox box = {lam::up(image), ui->font_ctx, rect, rect, 1.0f, 255, true};
    return render_paint_image_box(paint, &box);
}

void render_marker_view(RasterRenderContext* rdcon, ViewSpan* marker) {
    if (!marker || !marker->is_element() || view_visibility_is_hidden(marker)) return;

    DomElement* elem = lam::dom_require_element(lam::view_dom_node(marker));
    MarkerProp* marker_prop = elem->marker_prop();
    if (!marker_prop) {
        return;
    }
    FontBox marker_font_box = elem->font
        ? FontBox{lam::up(elem->font), font_prop_used_size(elem->font)} : rdcon->font;
    FontBox* marker_font = &marker_font_box;

    // layout marker geometry is logical; this legacy raster painter consumes physical coordinates.
    float scale = rdcon->raster_scale;
    float x = rdcon->block.x + marker->x * scale;
    float y = rdcon->block.y + marker->y * scale;
    float width = marker_prop->width * scale;
    float marker_height = marker->height * scale;
    float bullet_size = marker_prop->bullet_size * scale;
    CssEnum marker_type = marker_prop->marker_type;
    Color color = marker_prop->has_color ? marker_prop->color : rdcon->color;
    const FontMetrics* marker_metrics = font_box_handle(marker_font)
        ? font_get_metrics(font_box_handle(marker_font)) : NULL;
    float marker_font_size = marker_metrics
        ? font_handle_get_physical_size_px(font_box_handle(marker_font)) : 16.0f * scale;


    if (marker_prop->is_image_marker) {
        Rect image_rect = marker_image_rect(marker, marker_prop,
            rdcon->block.x / scale, rdcon->block.y / scale, marker_font_size / scale, 1.0f);
        RdtMatrix density = rdt_matrix_scale(scale, scale);
        const RdtMatrix* active = render_state_current_transform(rdcon);
        RdtMatrix transform = active ? rdt_matrix_multiply(active, &density) : density;
        RdtPath* clip = render_path_create_clip_path(rdcon);
        rc_push_clip(rdcon, clip, nullptr);
        // all backends record the same gradient image; retained effects own its payloads.
        bool painted = render_paint_gradient_image(rdcon->paint_list, &marker_prop->image,
            image_rect, scale, &transform, true);
        if (painted) {
            paint_ir_lower_raster_fragment(rdcon->paint_list, rdcon->dl);
            paint_list_clear(rdcon->paint_list);
        }
        rc_pop_clip(rdcon);
        rdt_path_free(clip);
        if (painted) return;
    }

    if (marker_prop->image.url && strcmp(marker_prop->image.url, "none") != 0) {
        if (!marker_prop->loaded_image) {
            marker_prop->loaded_image = lam::up(load_document_image(rdcon->ui_context->document, rdcon->ui_context, marker_prop->image.url));
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
        // file-backed images can reach paint with metadata before their first decode.
        if (img && img->width > 0 && img->height > 0)
            image_surface_ensure_decoded(img, img->width, img->height);
        if (img && img->pixels && img->width > 0 && img->height > 0) {
            // Paint the decoded source into the used marker box; raw pixels
            // would leave intrinsic images unzoomed after layout scales them.
            Rect rect = marker_image_rect(marker, marker_prop, rdcon->block.x, rdcon->block.y,
                marker_font_size, scale);
            // list-style images use the originating item's inherited sampling policy.
            ScaleMode mode = render_image_scale_mode(elem->parent_element(), false);
            render_painter_blit_surface_scaled(rdcon, img, nullptr, rdcon->ui_context->surface,
                &rect, &rdcon->block.clip, mode, rdcon->clip_shapes, rdcon->clip_shape_depth);
            return;
        }
    }

    float marker_cx = x + width - marker_font_size;
    float marker_cy = y + marker_height / 2.0f;

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
            float stroke_width = scale;

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
            if (marker_prop->text_content && *marker_prop->text_content && font_box_handle(marker_font)) {
                Color previous_color = rdcon->color;
                rdcon->color = color;
                float s = rdcon->raster_scale;
                float ascend = render_marker_baseline_y(marker_prop, 0.0f) * s;
                FontStyleDesc marker_style = font_style_desc_from_prop(marker_font->style);
                FontHandle* marker_handle = font_box_handle(marker_font);
                const char* marker_end = marker_prop->text_content +
                    strlen(marker_prop->text_content);

                float total_text_width = 0.0f;
                const char* p = marker_prop->text_content;
                while (p < marker_end) {
                    uint32_t cp;
                    if (!layout_utf8_next_codepoint(&p, marker_end, &cp)) continue;
                    LoadedGlyph* glyph = font_load_glyph(marker_handle, &marker_style, cp, false);
                    total_text_width += glyph ? glyph->advance_x + marker_font->style->letter_spacing * s : (marker_font->style->space_width * s);
                }

                float tx = x + width - total_text_width;
                p = marker_prop->text_content;
                while (p < marker_end) {
                    uint32_t cp;
                    if (!layout_utf8_next_codepoint(&p, marker_end, &cp)) continue;

                    if (cp == ' ') {
                        tx += marker_font->style->space_width * s;
                        continue;
                    }

                    LoadedGlyph* glyph = font_load_glyph(marker_handle, &marker_style, cp, true);
                    if (glyph) {
                        draw_glyph(rdcon, &glyph->bitmap, lroundf(tx + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                        tx += glyph->advance_x + marker_font->style->letter_spacing * s;
                    } else {
                        tx += marker_font->style->space_width * s;
                    }
                }
                rdcon->color = previous_color;

            }
            break;
        }

        default:
            break;
    }
}

void render_litem_view(RasterRenderContext* rdcon, ViewBlock* list_item) {
    rdcon->list.item_index++;
    render_block_view(rdcon, list_item);
}

void render_list_view(RasterRenderContext* rdcon, ViewBlock* view) {
    ViewBlock* list = lam::view_require_block(view);
    ListBlot pa_list = rdcon->list;
    rdcon->list.item_index = 0;
    rdcon->list.list_style_type = list->block()->list_style_type;
    render_block_view(rdcon, list);
    rdcon->list = pa_list;
}
