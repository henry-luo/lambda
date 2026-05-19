#include "render_media.hpp"

#include "display_list.h"
#include "render.hpp"
#include "render_raster.hpp"
#include "render_selection.hpp"
#include "webview.h"

#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <math.h>
#include <string.h>

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);

void render_svg(ImageSurface* surface) {
    if (!surface->pic) {
        log_debug("no picture to render");  return;
    }
    // Rasterize the SVG picture into a pixel buffer using a temporary vector context
    if (surface->width <= 0 || surface->height <= 0) {
        log_debug("invalid svg image size: %dx%d", surface->width, surface->height);
        return;
    }
    int target_width = surface->max_render_width > 0 ? surface->max_render_width : surface->width;
    if (target_width <= 0) {
        log_debug("invalid svg render width: %d", target_width);
        return;
    }
    uint32_t width = (uint32_t)target_width; // INT_CAST_OK: ThorVG raster target dimensions are integer pixels
    uint32_t height = (uint32_t)ceilf((float)target_width * (float)surface->height / (float)surface->width); // INT_CAST_OK: raster target height rounded to pixels
    if (height == 0) {
        log_debug("invalid svg render height for width: %d", target_width);
        return;
    }
    surface->pixels = (uint32_t*)mem_alloc(width * height * sizeof(uint32_t), MEM_CAT_RENDER);
    if (!surface->pixels) return;
    image_surface_bump_generation(surface);

    // CRITICAL: Clear the buffer to transparent before rendering SVG
    memset(surface->pixels, 0, width * height * sizeof(uint32_t));

    // Create a temporary vector context targeting the pixel buffer
    RdtVector tmp_vec = {};
    rdt_vector_init(&tmp_vec, (uint32_t*)surface->pixels, width, height, width);

    // Save and clear the global clip stack so parent clips don't leak into this
    // isolated off-screen SVG rasterization
    int saved_clip_depth = rdt_clip_save_depth();

    // Draw SVG picture at target size (takes ownership from surface)
    RdtPicture* pic = surface->pic;
    surface->pic = NULL;  // ownership transferred
    if (pic) {
        rdt_picture_set_size(pic, (float)width, (float)height);
        rdt_picture_draw(&tmp_vec, pic, 255, nullptr);
        rdt_picture_free(pic);
    }

    rdt_clip_restore_depth(saved_clip_depth);
    rdt_vector_destroy(&tmp_vec);
    surface->width = width;  surface->height = height;  surface->pitch = width * sizeof(uint32_t);
}

void render_image_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->img) return;

    log_debug("render image content");
    float s = rdcon->scale;
    ImageSurface* img = view->embed->img;
    Rect rect;
    rect.x = rdcon->block.x + view->x * s;  rect.y = rdcon->block.y + view->y * s;
    rect.width = view->width * s;  rect.height = view->height * s;

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
        // center in the content box (default object-position: 50% 50%)
        img_rect.x = rect.x + (box_w - rendered_w) * 0.5f;
        img_rect.y = rect.y + (box_h - rendered_h) * 0.5f;
        img_rect.width = rendered_w;
        img_rect.height = rendered_h;
    }
    log_debug("[IMAGE RENDER] url=%s, format=%d, img_size=%dx%d, view_size=%.0fx%.0f, pos=(%.0f,%.0f), clip=(%.0f,%.0f,%.0f,%.0f)",
              img->url && img->url->href ? img->url->href->chars : "unknown",
              img->format, img->width, img->height,
              rect.width, rect.height, rect.x, rect.y,
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);
    if (img->format == IMAGE_FORMAT_SVG) {
        log_debug("render svg image vector-first at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        if (img->pic) {
            RdtPicture* pic = rdt_picture_dup(img->pic);
            if (pic) {
                render_painter_draw_picture_rect(rdcon, pic, &img_rect, &rdcon->block.clip, 255);
                if (!rdcon->dl) {
                    rdt_picture_free(pic);
                }
            }
        } else if (img->pixels) {
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels, img->width, img->height,
                                            img->width, &img_rect, &rdcon->block.clip, 255, img);
        } else {
            log_debug("failed to render svg image: no vector picture or raster pixels");
        }
    } else {
        // ensure raster image pixels are decoded (lazy loading) at the displayed size
        image_surface_ensure_decoded(img, (int)img_rect.width, (int)img_rect.height); // INT_CAST_OK: image decoder target dimensions are integer pixels
        log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        render_painter_blit_surface_scaled(rdcon, img, NULL, rdcon->ui_context->surface,
            &img_rect, &rdcon->block.clip, SCALE_MODE_LINEAR,
            rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    // Render blue selection overlay if image is within a cross-view selection
    DocState* state = rdcon->ui_context && rdcon->ui_context->document
        ? rdcon->ui_context->document->state : NULL;
    if (render_selection_contains_view(state, static_cast<View*>(view))) {
        // Semi-transparent blue overlay (same color as text selection)
        uint32_t sel_bg_color = 0x80FF9933;  // ABGR format: semi-transparent blue
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
        log_debug("[IMAGE SELECTION] Rendered blue overlay on image at (%.0f,%.0f) size %.0fx%.0f",
                  rect.x, rect.y, rect.width, rect.height);
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

    // render border and background, etc. under the outer image marker so the
    // marker bounds include the replaced content recorded below.
    rdcon->element_marker_suppression_depth++;
    render_block_view(rdcon, view);
    rdcon->element_marker_suppression_depth--;

    // render the image content (using parent coordinates restored by render_block_view)
    render_image_content(rdcon, view);
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

    if (rdcon->dl) {
        dl_webview_layer_placeholder(rdcon->dl, wv->surface,
                                     dst_x, dst_y, dst_w, dst_h,
                                     &rdcon->block.clip,
                                     wv->surface ? wv->surface->generation : 0);
    } else {
        // fallback: direct blit (single-threaded path)
        Rect rect = { dst_x, dst_y, dst_w, dst_h };
        RasterPaintContext raster = {
            rdcon->ui_context->surface,
            &rdcon->block.clip,
            rdcon->clip_shapes,
            rdcon->clip_shape_depth
        };
        raster_blit_surface_scaled(&raster, wv->surface, NULL, &rect, SCALE_MODE_LINEAR);
    }
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

    if (rdcon->dl) {
        dl_video_placeholder(rdcon->dl, view->embed->video,
                             dst_x, dst_y, dst_w, dst_h,
                             object_fit_flags, &rdcon->block.clip,
                             rdcon->ui_context && rdcon->ui_context->document &&
                             rdcon->ui_context->document->state ?
                                 rdcon->ui_context->document->state->video_frame_generation : 0);
    }
}
