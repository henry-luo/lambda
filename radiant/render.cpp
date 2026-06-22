#include "render.hpp"
#include "render_profiler.hpp"
#include "render_media.hpp"
#include "render_svg_inline.hpp"
#include "render_clip.hpp"
#include "render_overlay.hpp"
#include "scroller.hpp"
#include "layout.hpp"

#include "../lib/tagged.hpp"
#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/str.h"
#include "../lambda/input/css/dom_element.hpp"
#include <string.h>
#include <math.h>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

/**
 * Reset canvas target and draw shapes to buffer.
 * This resets ThorVG's dirty region tracking to prevent black backgrounds
 * when rendering multiple shapes to the same frame buffer.
 *
 * ThorVG's smart rendering tracks "dirty regions" and clears them before
 * each draw. When we render multiple shapes to the same buffer within one
 * frame, this causes previously drawn content to be cleared to black.
 * Resetting the target sets fulldraw=true, which bypasses dirty region clearing.
 */
// CollapsedBorder struct is now defined in view.hpp

static bool inline_span_has_direct_text_fragment(ViewSpan* span) {
    if (!span) return false;
    for (View* child = span->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = lam::view_require<RDT_VIEW_TEXT>(child);
            if (text->rect) return true;
        }
    }
    return false;
}

void render_embed_doc(RenderContext* rdcon, ViewBlock* block) {
    BlockBlot pa_block = rdcon->block;
    if (block->bound) { render_bound(rdcon, block); }

    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;

    // Constrain clip region to iframe content box (before scroller setup)
    // This ensures embedded documents (SVG, PDF, etc.) don't render outside iframe bounds
    float content_left = rdcon->block.x;
    float content_top = rdcon->block.y;
    float content_right = rdcon->block.x + block->width * s;
    float content_bottom = rdcon->block.y + block->height * s;

    // Adjust for borders if present
    if (block->bound && block->bound->border) {
        content_left += block->bound->border->width.left * s;
        content_top += block->bound->border->width.top * s;
        content_right -= block->bound->border->width.right * s;
        content_bottom -= block->bound->border->width.bottom * s;
    }

    // Intersect with parent clip region
    rdcon->block.clip.left = max(rdcon->block.clip.left, content_left);
    rdcon->block.clip.top = max(rdcon->block.clip.top, content_top);
    rdcon->block.clip.right = min(rdcon->block.clip.right, content_right);
    rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, content_bottom);

    log_debug("iframe clip set to: left:%.0f, top:%.0f, right:%.0f, bottom:%.0f (content box)",
              rdcon->block.clip.left, rdcon->block.clip.top,
              rdcon->block.clip.right, rdcon->block.clip.bottom);

    // setup clip box for scrolling
    if (block->scroller) { setup_scroller(rdcon, block); }

    RenderClipScope iframe_clip_scope = render_clip_push_rect_scope(rdcon, &rdcon->block.clip);

    // render the embedded doc
    if (block->embed && block->embed->doc) {
        DomDocument* doc = block->embed->doc;
        // render html doc
        if (doc && doc->view_tree && doc->view_tree->root) {
            View* root_view = doc->view_tree->root;
            if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
                log_debug("render doc root view:");
                // Save parent context and reset for embedded document
                FontBox pa_font = rdcon->font;
                Color pa_color = rdcon->color;
                DomDocument* pa_doc = rdcon->ui_context
                    ? rdcon->ui_context->document : nullptr;
                if (rdcon->ui_context) rdcon->ui_context->document = doc;

                // Reset color to black for embedded document (don't inherit from parent doc)
                // Each document should start with default black text color
                rdcon->color.c = 0xFF000000;  // opaque black (ABGR)

                // load default font
                FontProp* default_font = doc->view_tree->html_version == HTML5 ? &rdcon->ui_context->default_font : &rdcon->ui_context->legacy_default_font;
                log_debug("render_init default font: %s, html version: %d", default_font->family, doc->view_tree->html_version);
                setup_font(rdcon->ui_context, &rdcon->font, default_font);

                ViewBlock* root_block = lam::view_require_block(root_view);

                // Per CSS 2.1 §14.2: the iframe's viewport is its own canvas.
                // Propagate the body background (or html background) to fill the
                // iframe content box, otherwise the body only paints its own
                // intrinsic-sized box (often smaller than the iframe viewport,
                // leaving white gaps below the body content).
                if (root_block->tag_id != HTM_TAG_SVG &&
                    !(root_block->embed && root_block->embed->img)) {
                    Color canvas_bg;
                    canvas_bg.c = 0;
                    bool html_has_bg = root_block->bound && root_block->bound->background &&
                                       root_block->bound->background->color.a > 0;
                    if (html_has_bg) {
                        canvas_bg = root_block->bound->background->color;
                    } else {
                        // walk html children for body bg
                        View* c = root_block->first_child;
                        while (c) {
                            if (c->view_type == RDT_VIEW_BLOCK) {
                                ViewBlock* cb = lam::view_require_block(c);
                                const char* nm = cb->node_name();
                                if (nm && str_ieq_const(nm, strlen(nm), "body")) {
                                    if (cb->bound && cb->bound->background &&
                                        cb->bound->background->color.a > 0) {
                                        canvas_bg = cb->bound->background->color;
                                    }
                                    break;
                                }
                            }
                            c = static_cast<View*>(c->next_sibling);
                        }
                    }
                    if (canvas_bg.a > 0) {
                        // Fill iframe content box (already computed above as content_left/top/right/bottom).
                        rc_fill_rect(rdcon,
                                     content_left, content_top,
                                     content_right - content_left,
                                     content_bottom - content_top,
                                     canvas_bg);
                    }
                }

                // Check if root element is SVG - if so, render directly without background
                if (root_block->tag_id == HTM_TAG_SVG) {
                    log_debug("render embedded SVG document (no background)");
                    render_inline_svg(rdcon, root_block);
                } else if (root_block->embed && root_block->embed->img) {
                    // Image/SVG document root — use render_image_view
                    render_image_view(rdcon, root_block);
                } else {
                    // Regular HTML document - render with background
                    render_block_view(rdcon, root_block);
                }
                if (doc->state) {
                    render_ui_overlays(rdcon, doc->state);
                }

                rdcon->font = pa_font;
                rdcon->color = pa_color;
                if (rdcon->ui_context) rdcon->ui_context->document = pa_doc;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    if (iframe_clip_scope.active) {
        render_clip_pop_scope(rdcon, &iframe_clip_scope);
    }

    // Render scrollbar for the iframe scroll container
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    render_profiler_increment(rdcon->profiler, RENDER_PROFILE_INLINE);
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");

    bool self_hidden = view_span->in_line && view_span->in_line->visibility == VIS_HIDDEN;

    // Render border/outline for inline elements.
    // Background is rendered per-line-fragment in render_text_view so that
    // wrapping inline elements (e.g. <code> spanning two lines) don't fill
    // the entire bounding-box rectangle with background color. Borders are
    // handled by the same text-fragment path so rounded inline decorations
    // break at line boundaries instead of enclosing the aggregate span box.
    if (!self_hidden && view_span->bound) {
        BackgroundProp* saved_bg = view_span->bound->background;
        BorderProp* saved_border = view_span->bound->border;
        bool border_is_fragment_painted = saved_border &&
            inline_span_has_direct_text_fragment(view_span);
        view_span->bound->background = nullptr;
        if (border_is_fragment_painted) {
            view_span->bound->border = nullptr;
        }
        render_bound(rdcon, lam::unsafe_view_block_api_span(view_span));
        view_span->bound->background = saved_bg;
        if (border_is_fragment_painted) {
            view_span->bound->border = saved_border;
        }
    }

    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->has_color) {
            log_debug("[RENDER COLOR INLINE] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      view_span->node_name(),
                      view_span->in_line->color.r, view_span->in_line->color.g, view_span->in_line->color.b,
                      pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line->color.c);
            rdcon->color = view_span->in_line->color;
        } else {
            log_debug("[RENDER COLOR INLINE] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      view_span->node_name(), pa_color.r, pa_color.g, pa_color.b,
                      view_span->in_line, view_span->in_line ? view_span->in_line->color.c : 0);
        }
        render_children(rdcon, view);
    }
    else {
        log_debug("view has no child");
    }
    rdcon->font = pa_font;  rdcon->color = pa_color;
}
