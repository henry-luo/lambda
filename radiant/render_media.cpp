#include "render_media.hpp"

#include "render.hpp"
#include "render_geometry.hpp"
#include "render_painter.hpp"
#include "render_selection.hpp"
#include "render_state.hpp"
#include "webview.h"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <math.h>
#include <string.h>

static Bound render_media_intersect_clip_rect(const Bound* clip, const Rect* rect) {
    Bound out = clip ? *clip : Bound{rect->x, rect->y, rect->x + rect->width, rect->y + rect->height};
    float rect_right = rect->x + rect->width;
    float rect_bottom = rect->y + rect->height;
    if (out.left < rect->x) out.left = rect->x;
    if (out.top < rect->y) out.top = rect->y;
    if (out.right > rect_right) out.right = rect_right;
    if (out.bottom > rect_bottom) out.bottom = rect_bottom;
    return out;
}

static bool render_media_rect_contains(const Rect* outer, const Rect* inner) {
    if (!outer || !inner) return true;
    const float eps = 0.01f;
    return inner->x >= outer->x - eps &&
           inner->y >= outer->y - eps &&
           inner->x + inner->width <= outer->x + outer->width + eps &&
           inner->y + inner->height <= outer->y + outer->height + eps;
}

static bool render_media_push_content_clip(RenderContext* rdcon, const Rect* content_rect,
                                           const Rect* image_rect) {
    if (!rdcon || !content_rect || render_media_rect_contains(content_rect, image_rect)) {
        return false;
    }
    RdtPath* clip_path = rdt_path_new();
    rdt_path_add_rect(clip_path, content_rect->x, content_rect->y,
                      content_rect->width, content_rect->height, 0, 0);
    rc_push_clip(rdcon, clip_path, render_state_current_transform(rdcon));
    rdt_path_free(clip_path);
    return true;
}

static bool render_media_view_has_dom_element(ViewBlock* view) {
    if (!view) return false;
    DomNode* node = lam::view_dom_node(static_cast<View*>(view));
    return node && node->node_type == DOM_NODE_ELEMENT;
}

static uint8_t render_media_content_opacity(ViewBlock* view) {
    if (!view || !view->in_line) return 255;
    float opacity = view->in_line->opacity;
    if (opacity >= 1.0f) return 255;
    if (opacity <= 0.0f) return 0;
    return (uint8_t)(opacity * 255.0f + 0.5f);
}

static float render_media_object_position_offset(float box_size, float rendered_size,
                                                 float position, bool is_percent,
                                                 float scale) {
    if (is_percent) {
        return (box_size - rendered_size) * position / 100.0f;
    }
    return position * scale;
}

bool render_media_rasterize_svg_picture(ImageSurface* surface, int target_width,
                                        int target_height) {
    if (!surface || !surface->pic || target_width <= 0 || target_height <= 0) {
        return false;
    }
    if (surface->pixels &&
        surface->decoded_width == target_width &&
        surface->decoded_height == target_height) {
        return true;
    }

    uint32_t* pixels = (uint32_t*)mem_alloc(
        (size_t)target_width * (size_t)target_height * sizeof(uint32_t),
        MEM_CAT_RENDER);
    if (!pixels) return false;
    memset(pixels, 0, (size_t)target_width * (size_t)target_height * sizeof(uint32_t));

    RdtVector tmp_vec = {};
    rdt_vector_init(&tmp_vec, pixels, (uint32_t)target_width, (uint32_t)target_height,
                    (uint32_t)target_width); // INT_CAST_OK: target dimensions are validated positive raster pixels.
    int saved_clip_depth = rdt_clip_save_depth();

    // Intentional local/offscreen draw: this rasterizes an SVG resource into
    // its ImageSurface cache, outside the live RenderContext painter pipeline.
    RdtPicture* pic = rdt_picture_dup(surface->pic);
    if (pic) {
        rdt_picture_set_size(pic, (float)target_width, (float)target_height);
        rdt_picture_draw(&tmp_vec, pic, 255, nullptr);
        rdt_picture_free(pic);
    }

    rdt_clip_restore_depth(saved_clip_depth);
    rdt_vector_destroy(&tmp_vec);

    if (surface->pixels) {
        mem_free(surface->pixels);
    }
    surface->pixels = pixels;
    surface->decoded_width = target_width;
    surface->decoded_height = target_height;
    surface->pitch = target_width * 4;
    image_surface_bump_generation(surface);
    return true;
}

static void render_image_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->img) return;

    log_debug("render image content");
    ImageSurface* img = view->embed->img;
    Rect border_rect = render_geometry_block_border_rect(&rdcon->block, view, rdcon->scale);
    Rect rect = render_geometry_block_content_rect(&rdcon->block, view, rdcon->scale);
    float s = rdcon->scale;

    // Apply object-fit: compute actual image render rect
    CssEnum object_fit = view->embed->object_fit;
    Rect img_rect = rect;  // default: fill (stretch to container)
    // SVG images with a viewBox implicitly use preserveAspectRatio="xMidYMid meet"
    // (equivalent to object-fit: contain) unless object-fit is explicitly set.
    bool svg_default_contain = (img->format == IMAGE_FORMAT_SVG && !object_fit);
    if ((object_fit && object_fit != CSS_VALUE_FILL && img->width > 0 && img->height > 0) ||
        svg_default_contain) {
        float img_w = (float)img->width;
        float img_h = (float)img->height;
        float box_w = rect.width;
        float box_h = rect.height;
        float scale_x = box_w / img_w;
        float scale_y = box_h / img_h;
        float scale;

        if (object_fit == CSS_VALUE_CONTAIN || svg_default_contain) {
            scale = (scale_x < scale_y) ? scale_x : scale_y;
        } else if (object_fit == CSS_VALUE_COVER) {
            scale = (scale_x > scale_y) ? scale_x : scale_y;
        } else if (object_fit == CSS_VALUE_NONE) {
            scale = 1.0f * s;
        } else if (object_fit == CSS_VALUE_SCALE_DOWN) {
            // scale-down: use the smaller of none (1x) or contain
            float contain_scale = (scale_x < scale_y) ? scale_x : scale_y;
            scale = (contain_scale < s) ? contain_scale : s;
        } else {
            scale = scale_x; // fallback: fill-like
        }

        float rendered_w = img_w * scale;
        float rendered_h = img_h * scale;
        float pos_x = 50.0f;
        float pos_y = 50.0f;
        bool pos_x_is_percent = true;
        bool pos_y_is_percent = true;
        if (view->embed->object_position_set) {
            pos_x = view->embed->object_position_x;
            pos_y = view->embed->object_position_y;
            pos_x_is_percent = view->embed->object_position_x_is_percent;
            pos_y_is_percent = view->embed->object_position_y_is_percent;
        }
        img_rect.x = rect.x + render_media_object_position_offset(
            box_w, rendered_w, pos_x, pos_x_is_percent, s);
        img_rect.y = rect.y + render_media_object_position_offset(
            box_h, rendered_h, pos_y, pos_y_is_percent, s);
        img_rect.width = rendered_w;
        img_rect.height = rendered_h;
    }
    log_debug("[IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f), clip=(%.0f,%.0f,%.0f,%.0f)",
              img->url && img->url->href ? img->url->href->chars : "unknown",
              img->format, img->width, img->height,
              rect.width, rect.height, rect.x, rect.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);
    uint8_t content_opacity = render_media_content_opacity(view);
    Bound image_clip = rdcon->has_transform
        ? rdcon->block.clip
        : render_media_intersect_clip_rect(&rdcon->block.clip, &rect);
    bool pushed_content_clip = render_media_push_content_clip(rdcon, &rect, &img_rect);
    if (img->format == IMAGE_FORMAT_SVG) {
        bool drew_svg = false;
        if (img->pic) {
            log_debug("render svg image as display-list picture at x:%f, y:%f, wd:%f, hg:%f",
                      img_rect.x, img_rect.y, img_rect.width, img_rect.height);
            RdtPicture* pic = rdt_picture_dup(img->pic);
            if (pic) {
                render_painter_draw_picture_rect(rdcon, pic, &img_rect, &image_clip,
                                                 content_opacity);
                drew_svg = true;
            }
        }

        int svg_target_w = (int)ceilf(img_rect.width); // INT_CAST_OK: rasterized SVG image target width in pixels.
        int svg_target_h = (int)ceilf(img_rect.height); // INT_CAST_OK: rasterized SVG image target height in pixels.
        if (!drew_svg && svg_target_w < 1) svg_target_w = 1;
        if (!drew_svg && svg_target_h < 1) svg_target_h = 1;
        bool rasterized = !drew_svg && render_media_rasterize_svg_picture(img, svg_target_w, svg_target_h);
        if (!drew_svg && rasterized && img->pixels) {
            log_debug("render svg image as local raster at x:%f, y:%f, wd:%f, hg:%f, src=%dx%d",
                      img_rect.x, img_rect.y, img_rect.width, img_rect.height,
                      svg_target_w, svg_target_h);
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels, svg_target_w, svg_target_h,
                                            svg_target_w, &img_rect, &image_clip,
                                            content_opacity, img);
            drew_svg = true;
        }
        if (!drew_svg) {
            log_debug("failed to render svg image: no vector picture or raster pixels");
        }
    } else {
        // ensure raster image pixels are decoded (lazy loading) at the displayed size
        image_surface_ensure_decoded(img, (int)img_rect.width, (int)img_rect.height); // INT_CAST_OK: image decoder target dimensions are integer pixels
        log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        if (rdcon->has_transform) {
            // scaled image decodes may replace pixels with a smaller buffer;
            // display-list image commands must use decoded dimensions.
            int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
            int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels,
                                            src_w, src_h, img->pitch,
                                            &img_rect, &image_clip,
                                            content_opacity, img);
        } else {
            render_painter_blit_surface_scaled(rdcon, img, NULL, rdcon->ui_context->surface,
                &img_rect, &image_clip, SCALE_MODE_LINEAR,
                rdcon->clip_shapes, rdcon->clip_shape_depth, content_opacity);
        }
    }
    if (pushed_content_clip) {
        rc_pop_clip(rdcon);
    }

    // Render blue selection overlay if image is within a cross-view selection
    DocState* state = rdcon->ui_context && rdcon->ui_context->document
        ? rdcon->ui_context->document->state : NULL;
    if (render_selection_contains_view(state, static_cast<View*>(view))) {
        // Semi-transparent blue overlay (same color as text selection)
        uint32_t sel_bg_color = 0x80FF9933;  // ABGR format: semi-transparent blue
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        log_debug("[IMAGE SELECTION] Rendered blue overlay on image at (%.0f,%.0f) size %.0fx%.0f",
                  border_rect.x, border_rect.y, border_rect.width, border_rect.height);
    }
}

void render_image_view(RenderContext* rdcon, ViewBlock* view) {
    log_debug("render image view");
    log_enter();
    if (render_block_dirty_misses(rdcon, view)) {
        log_leave();
        return;
    }
    if (render_block_try_retained_fragment(rdcon, view)) {
        log_leave();
        return;
    }

    RenderElementMarkerScope marker_scope = render_element_marker_begin(rdcon, view);
    BlockBlot parent_block = rdcon->block;

    if (render_media_view_has_dom_element(view)) {
        // render border and background, etc. under the outer image marker so the
        // marker bounds include the replaced content recorded below.
        rdcon->element_marker_suppression_depth++;
        render_block_view(rdcon, view);
        rdcon->element_marker_suppression_depth--;
    }

    // render the image content after any DOM-backed block decorations.
    RenderTransformScope content_transform_scope = render_state_push_transform(rdcon, view, &parent_block);
    render_image_content(rdcon, view);
    render_state_pop_transform(&content_transform_scope);
    render_element_marker_end(rdcon, &marker_scope);
    log_debug("end of image render");
    log_leave();
}

bool render_media_is_webview_layer(ViewBlock* view) {
    return view && view->embed && view->embed->webview &&
           view->embed->webview->mode == WEBVIEW_MODE_LAYER;
}

void render_webview_layer_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->webview) return;
    WebViewProp* wv = view->embed->webview;
    if (wv->mode != WEBVIEW_MODE_LAYER || !wv->surface || !wv->surface->pixels) return;
    if (!wv->visible) return;

    float s = rdcon->scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;

    log_debug("[WEBVIEW LAYER RENDER] pos=(%.0f,%.0f) size=%.0fx%.0f surface=%dx%d",
              dst_x, dst_y, dst_w, dst_h,
              wv->surface->width, wv->surface->height);

    rc_webview_layer_placeholder(rdcon, wv->surface,
                                 dst_x, dst_y, dst_w, dst_h,
                                 &rdcon->block.clip,
                                 wv->surface ? wv->surface->generation : 0);
}

void render_video_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->video) return;

    float s = rdcon->scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;
    int object_fit_flags = (int)view->embed->object_fit; // INT_CAST_OK: enum packing for display-list placeholder flags
    // pack has_controls into bit 8
    if (view->embed->has_controls) object_fit_flags |= 0x100;

    log_debug("[VIDEO RENDER] placeholder at (%.0f,%.0f) size %.0fx%.0f controls=%d",
              dst_x, dst_y, dst_w, dst_h, view->embed->has_controls);

    rc_video_placeholder(rdcon, view->embed->video,
                         dst_x, dst_y, dst_w, dst_h,
                         object_fit_flags, &rdcon->block.clip,
                         rdcon->ui_context && rdcon->ui_context->document &&
                         rdcon->ui_context->document->state ?
                             rdcon->ui_context->document->state->video_frame_generation : 0);
}
