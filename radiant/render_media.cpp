#include "render.hpp"
#include "radiant.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <math.h>
#include <string.h>

static bool render_media_push_content_clip(RasterRenderContext* rdcon, const Rect* content_rect,
                                           const Rect* image_rect) {
    if (!rdcon || !content_rect || !image_rect ||
        view_geometry_rect_contains_rect(*content_rect, *image_rect, 0.01f)) {
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
    float opacity = view->inl()->opacity;
    if (opacity >= 1.0f) return 255;
    if (opacity <= 0.0f) return 0;
    return clamp_byte_round(opacity * 255.0f);
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
    // its ImageSurface cache, outside the live RasterRenderContext painter pipeline.
    RdtPicture* pic = rdt_picture_dup(surface->pic);
    if (pic) {
        rdt_picture_set_size(pic, (float)target_width, (float)target_height);
        rdt_picture_draw(&tmp_vec, pic, 255, nullptr);
        rdt_picture_free(pic);
    }

    rdt_clip_restore_depth(saved_clip_depth);
    rdt_vector_destroy(&tmp_vec);

    image_surface_adopt_pixels(surface, pixels);
    surface->decoded_width = target_width;
    surface->decoded_height = target_height;
    surface->pitch = target_width * 4;
    image_surface_bump_generation(surface);
    return true;
}

Rect render_media_object_rect(const EmbedProp* embed, ImageSurface* img, Rect rect, float s) {
    if (!embed || !img || img->width <= 0 || img->height <= 0 ||
        rect.width <= 0.0f || rect.height <= 0.0f || s <= 0.0f) return rect;
    // Apply object-fit: compute actual image render rect
    CssEnum object_fit = embed->object_fit;
    Rect img_rect = rect;  // default: fill (stretch to container)
    if (object_fit && object_fit != CSS_VALUE_FILL) {
        float density = embed->content_image_resolution > 0.0f ? embed->content_image_resolution : 1.0f;
        ReplacedIntrinsicFacts facts = {}; layout_replaced_image_facts(&facts, img, true, density);
        float img_w, img_h;
        layout_replaced_default_object_size(&facts, rect.width / s, rect.height / s, &img_w, &img_h);
        if (img_w <= 0.0f || img_h <= 0.0f) return rect;
        float box_w = rect.width;
        float box_h = rect.height;
        float rendered_w = img_w * s, rendered_h = img_h * s;
        // SVG preserveAspectRatio is applied inside its viewport; object-fit uses only natural sizing facts.
        if (object_fit == CSS_VALUE_CONTAIN || object_fit == CSS_VALUE_COVER || object_fit == CSS_VALUE_SCALE_DOWN) {
            float fitted_w = box_w, fitted_h = box_h;
            if (facts.has_natural_aspect_ratio) {
                float ratio = facts.natural_aspect_ratio;
                float fit = object_fit == CSS_VALUE_COVER ? fmaxf(box_w / ratio, box_h) : fminf(box_w / ratio, box_h);
                fitted_w = fit * ratio; fitted_h = fit;
            }
            if (object_fit != CSS_VALUE_SCALE_DOWN || rendered_w > fitted_w || rendered_h > fitted_h) {
                rendered_w = fitted_w; rendered_h = fitted_h;
            }
        } else if (object_fit != CSS_VALUE_NONE) {
            return rect;
        }
        float pos_x = 50.0f;
        float pos_y = 50.0f;
        bool pos_x_is_percent = true;
        bool pos_y_is_percent = true;
        if (embed->object_position_set) {
            pos_x = embed->object_position_x;
            pos_y = embed->object_position_y;
            pos_x_is_percent = embed->object_position_x_is_percent;
            pos_y_is_percent = embed->object_position_y_is_percent;
        }
        img_rect.x = rect.x + render_media_object_position_offset(
            box_w, rendered_w, pos_x, pos_x_is_percent, s);
        img_rect.y = rect.y + render_media_object_position_offset(
            box_h, rendered_h, pos_y, pos_y_is_percent, s);
        img_rect.width = rendered_w;
        img_rect.height = rendered_h;
    }
    return img_rect;
}

Rect render_media_image_rect(ViewBlock* view, ImageSurface* image, Rect rect, float scale) {
    return render_media_object_rect(view && view->embed ? view->embedp() : nullptr, image, rect, scale);
}

bool render_media_paint_svg_image(PaintList* paint, ImageSurface* image, Rect rect,
        const Rect* content_rect, float raster_scale, FontContext* fonts, UiContext* ui, uint8_t opacity) {
    if (!paint || !content_rect) return false;
    Element* root = image && image->pic ? rdt_picture_get_svg_root(image->pic) : nullptr;
    if (!root) return false;
    if (rect.width <= 0 || rect.height <= 0) return true;
    RdtMatrix placement = rdt_matrix_translate(rect.x, rect.y);
    PaintSvgSubscene scene = {};
    render_svg_build_subscene(&scene, root, rect.width, rect.height,
        rdt_picture_get_pool(image->pic), raster_scale,
        fonts, &placement, nullptr, nullptr, nullptr,
        rdt_picture_get_source_path(image->pic), 1.0f, false, nullptr, true, -1.0f, ui);
    scene.opacity = (float)opacity / 255.0f;
    scene.image_document = true;
    scene.clip_viewport = true;
    scene.animation_time = rdt_picture_animation_time(image->pic);
    // replaced images clip cover/none overflow to their CSS content box in every backend.
    RdtPath* clip = rdt_path_new();
    if (!clip) return true;
    rdt_path_add_rect(clip, content_rect->x, content_rect->y,
        content_rect->width, content_rect->height, 0, 0);
    paint_push_clip(paint, clip, nullptr);
    paint_svg_subscene(paint, &scene);
    paint_pop_clip(paint);
    rdt_path_free(clip);
    return true;
}

bool render_media_paint_svg_picture(PaintList* paint, UiContext* ui, ViewBlock* view,
        const Rect* content_rect) {
    if (!view || !view->embed || !content_rect) return false;
    ImageSurface* image = view->embedp()->img;
    return render_media_paint_svg_image(paint, image,
        render_media_image_rect(view, image, *content_rect, 1.0f), content_rect,
        ui_context_raster_scale(ui), ui ? ui->font_ctx.get() : nullptr, ui);
}

bool render_paint_image_box(PaintList* paint, const PaintImageBox* box) {
    if (!paint || !box || !box->image) return false;
    if (box->content_rect.width <= 0.0f || box->content_rect.height <= 0.0f) return true;
    if (box->image->format == IMAGE_FORMAT_SVG) {
        return render_media_paint_svg_image(paint, box->image, box->image_rect,
            &box->content_rect, box->raster_scale, box->fonts, nullptr, box->opacity);
    }
    RdtPath* clip = rdt_path_new();
    if (!clip) return false;
    rdt_path_add_rect(clip, box->content_rect.x, box->content_rect.y,
        box->content_rect.width, box->content_rect.height, 0.0f, 0.0f);
    paint_push_clip(paint, clip, nullptr); rdt_path_free(clip);
    paint_draw_image_resource(paint, box->image, box->image_rect.x, box->image_rect.y,
        box->image_rect.width, box->image_rect.height, box->opacity, nullptr);
    paint_pop_clip(paint);
    return true;
}

static void render_image_content(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embedp()->img) return;

    ImageSurface* img = view->embedp()->img;
    Rect border_rect = render_geometry_block_border_rect(&rdcon->block, view, rdcon->raster_scale);
    Rect rect = render_geometry_block_content_rect(&rdcon->block, view, rdcon->raster_scale);
    float s = rdcon->raster_scale;

    Rect img_rect = render_media_image_rect(view, img, rect, s);
    uint8_t content_opacity = render_media_content_opacity(view);
    Bound image_clip = rdcon->has_transform
        ? rdcon->block.clip
        : view_geometry_intersect_bound_rect(rdcon->block.clip, rect);
    bool pushed_content_clip = render_media_push_content_clip(rdcon, &rect, &img_rect);
    if (img->format == IMAGE_FORMAT_SVG) {
        bool drew_svg = false;
        if (img->pic) {
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
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels, svg_target_w, svg_target_h,
                                            svg_target_w, &img_rect, &image_clip,
                                            content_opacity, img);
            drew_svg = true;
        }
        if (!drew_svg) {
        }
    } else {
        ScaleMode image_scale_mode = render_image_scale_mode(view, false);
        // ensure raster image pixels are decoded (lazy loading) at the displayed size
        if (image_scale_mode == SCALE_MODE_NEAREST ||
            image_scale_mode == SCALE_MODE_PIXELATED) {
            // Pixel-preserving sampling needs source pixels even when shrinking.
            image_surface_ensure_decoded(img, img->width, img->height);
        } else {
            image_surface_ensure_decoded(img, (int)img_rect.width, (int)img_rect.height); // INT_CAST_OK: image decoder target dimensions are integer pixels
        }
        if (rdcon->has_transform) {
            // scaled image decodes may replace pixels with a smaller buffer;
            // display-list image commands store decoded dimensions and uint32_t row stride.
            int src_w = img->decoded_width > 0 ? img->decoded_width : img->width;
            int src_h = img->decoded_height > 0 ? img->decoded_height : img->height;
            render_painter_draw_pixels_rect(rdcon, (uint32_t*)img->pixels,
                                            src_w, src_h, img->pitch / 4,
                                            &img_rect, &image_clip,
                                            content_opacity, img);
        } else {
            render_painter_blit_surface_scaled(rdcon, img, NULL, rdcon->ui_context->surface,
                &img_rect, &image_clip, image_scale_mode,
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
    }
}

void render_image_view(RasterRenderContext* rdcon, ViewBlock* view) {
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
    log_leave();
}

bool render_media_is_webview_layer(ViewBlock* view) {
    return view && view->embed && view->embedp()->webview &&
           view->embedp()->webview->mode == WEBVIEW_MODE_LAYER;
}

void render_webview_layer_content(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embedp()->webview) return;
    WebViewProp* wv = view->embedp()->webview;
    if (wv->mode != WEBVIEW_MODE_LAYER || !wv->surface || !wv->surface->pixels) return;
    if (!wv->visible) return;

    float s = rdcon->raster_scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;


    rc_webview_layer_placeholder(rdcon, wv->surface,
                                 dst_x, dst_y, dst_w, dst_h,
                                 &rdcon->block.clip,
                                 wv->surface ? wv->surface->generation : 0);
}

void render_video_content(RasterRenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embedp()->video) return;

    float s = rdcon->raster_scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;
    int object_fit_flags = (int)view->embedp()->object_fit; // INT_CAST_OK: enum packing for display-list placeholder flags
    // pack has_controls into bit 8
    if (view->embedp()->has_controls) object_fit_flags |= 0x100;


    rc_video_placeholder(rdcon, view->embedp()->video,
                         dst_x, dst_y, dst_w, dst_h,
                         object_fit_flags, &rdcon->block.clip,
                         rdcon->ui_context && rdcon->ui_context->document &&
                         rdcon->ui_context->document->state ?
                             rdcon->ui_context->document->state->video_frame_generation : 0);
}
