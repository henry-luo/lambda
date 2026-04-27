#include "render.hpp"
#include "render_img.hpp"
#include "render_border.hpp"
#include "render_background.hpp"
#include "render_filter.hpp"
#include "render_svg_inline.hpp"
#include "transform.hpp"
#include "layout.hpp"
#include "form_control.hpp"
#include "state_store.hpp"
#include "dom_range.hpp"
#include "dom_range_resolver.hpp"
#include "tile_pool.h"
#include "webview.h"

#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/avl_tree.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lambda/input/css/css_style.hpp"
#include "../lambda/input/css/css_value.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include <string.h>
#include <math.h>
#include <chrono>
#include <png.h>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "lib/stb_image_write.h"

// Forward declaration for inline SVG rendering (defined in render_svg_inline.cpp)
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);
// Forward declaration for SVG rasterization (defined later in this file)
void render_svg(ImageSurface* surface);

#define DEBUG_RENDER 0

// Rendering performance counters
static int64_t g_render_glyph_count = 0;
static int64_t g_render_draw_count = 0;
static double g_render_load_glyph_time = 0;
static double g_render_draw_glyph_time = 0;
static int64_t g_render_setup_font_count = 0;
static double g_render_setup_font_time = 0;

// Extended profiling counters
static double g_render_bound_time = 0;       // backgrounds + borders + shadows + outline
static int64_t g_render_bound_count = 0;
static double g_render_text_total_time = 0;  // full render_text_view time
static int64_t g_render_text_count = 0;
static double g_render_image_time = 0;       // render_image_view time
static int64_t g_render_image_count = 0;
static double g_render_svg_time = 0;         // render_inline_svg time
static int64_t g_render_svg_count = 0;
static double g_render_filter_time = 0;      // CSS filter time
static int64_t g_render_filter_count = 0;
static double g_render_clip_time = 0;        // clip mask save/apply time
static int64_t g_render_clip_count = 0;
static double g_render_opacity_time = 0;     // opacity blending time
static int64_t g_render_opacity_count = 0;
static double g_render_blend_time = 0;       // mix-blend-mode time
static int64_t g_render_blend_count = 0;
static int64_t g_render_block_count = 0;     // total render_block_view calls
static double g_render_inline_time = 0;      // render_inline_view time
static int64_t g_render_inline_count = 0;
static int64_t g_render_dispatch_count = 0;  // render_children iterations
static double g_render_block_self_time = 0;  // render_block_view self-time (excl. children)
static double g_render_children_time = 0;    // total time in render_children function
static double g_render_overflow_clip_time = 0; // overflow clip mask save+apply time
static int64_t g_render_overflow_clip_count = 0;
static double g_render_font_metrics_time = 0;  // font_get_rendering_ascender time
static int64_t g_render_font_metrics_count = 0;

// Phase 2: Static render pool (persists across frames; lazily initialised)
static RenderPool* g_render_pool = nullptr;
static pthread_once_t g_render_pool_once = PTHREAD_ONCE_INIT;
static int g_render_pool_threads = 0;  // set before pthread_once call

static void init_render_pool_once() {
    g_render_pool = (RenderPool*)mem_calloc(1, sizeof(RenderPool), MEM_CAT_RENDER);
    render_pool_init(g_render_pool, g_render_pool_threads);
}

// Shut down the render pool before ThorVG engine is terminated.
// Must be called before rdt_engine_term() so worker ThorVG canvases
// are destroyed while the engine is still alive.
void render_pool_shutdown() {
    if (g_render_pool) {
        render_pool_destroy(g_render_pool);
        mem_free(g_render_pool);
        g_render_pool = nullptr;
    }
}

// Get desired render thread count: RADIANT_RENDER_THREADS env var, or 0 = auto.
// Set to 1 to disable tiling and use single-threaded dl_replay.
static int get_render_thread_count() {
    static int cached = -1;
    if (cached >= 0) return cached;
    const char* env = getenv("RADIANT_RENDER_THREADS");
    if (env) {
        cached = atoi(env);
        if (cached < 0) cached = 0;
    } else {
        cached = 0;  // auto
    }
    return cached;
}

void reset_render_stats() {
    g_render_glyph_count = 0;
    g_render_draw_count = 0;
    g_render_load_glyph_time = 0;
    g_render_draw_glyph_time = 0;
    g_render_setup_font_count = 0;
    g_render_setup_font_time = 0;
    g_render_bound_time = 0;
    g_render_bound_count = 0;
    g_render_text_total_time = 0;
    g_render_text_count = 0;
    g_render_image_time = 0;
    g_render_image_count = 0;
    g_render_svg_time = 0;
    g_render_svg_count = 0;
    g_render_filter_time = 0;
    g_render_filter_count = 0;
    g_render_clip_time = 0;
    g_render_clip_count = 0;
    g_render_opacity_time = 0;
    g_render_opacity_count = 0;
    g_render_blend_time = 0;
    g_render_blend_count = 0;
    g_render_block_count = 0;
    g_render_inline_time = 0;
    g_render_inline_count = 0;
    g_render_dispatch_count = 0;
    g_render_block_self_time = 0;
    g_render_children_time = 0;
    g_render_overflow_clip_time = 0;
    g_render_overflow_clip_count = 0;
    g_render_font_metrics_time = 0;
    g_render_font_metrics_count = 0;
}

void log_render_stats() {
    log_info("[TIMING] render stats: load_glyph calls=%lld (%.1fms), draw_glyph calls=%lld (%.1fms), setup_font calls=%lld (%.1fms)",
        g_render_glyph_count, g_render_load_glyph_time,
        g_render_draw_count, g_render_draw_glyph_time,
        g_render_setup_font_count, g_render_setup_font_time);
}

// Stderr-based timing output that works with --no-log
void stderr_render_stats() {
    fprintf(stderr, "[RENDER_PROF] font: load_glyph=%lld(%.1fms) draw_glyph=%lld(%.1fms) setup_font=%lld(%.1fms)\n",
        g_render_glyph_count, g_render_load_glyph_time,
        g_render_draw_count, g_render_draw_glyph_time,
        g_render_setup_font_count, g_render_setup_font_time);
    fprintf(stderr, "[RENDER_PROF] bound=%lld(%.1fms) text=%lld(%.1fms) image=%lld(%.1fms) svg=%lld(%.1fms)\n",
        g_render_bound_count, g_render_bound_time,
        g_render_text_count, g_render_text_total_time,
        g_render_image_count, g_render_image_time,
        g_render_svg_count, g_render_svg_time);
    fprintf(stderr, "[RENDER_PROF] filter=%lld(%.1fms) clip=%lld(%.1fms) opacity=%lld(%.1fms) blend=%lld(%.1fms)\n",
        g_render_filter_count, g_render_filter_time,
        g_render_clip_count, g_render_clip_time,
        g_render_opacity_count, g_render_opacity_time,
        g_render_blend_count, g_render_blend_time);
    fprintf(stderr, "[RENDER_PROF] blocks=%lld(self=%.1fms) inlines=%lld(%.1fms) dispatches=%lld children=%.1fms\n",
        g_render_block_count, g_render_block_self_time,
        g_render_inline_count, g_render_inline_time, g_render_dispatch_count, g_render_children_time);
    fprintf(stderr, "[RENDER_PROF] overflow_clip=%lld(%.1fms) font_metrics=%lld(%.1fms)\n",
        g_render_overflow_clip_count, g_render_overflow_clip_time,
        g_render_font_metrics_count, g_render_font_metrics_time);
}

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

// Check if a codepoint has Emoji_Presentation=Yes (Unicode 15.0, UTS #51).
// Mirrors is_emoji_presentation_default in layout_text.cpp.
static inline bool is_emoji_presentation_default(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
    if (cp >= 0xE0020 && cp <= 0xE007F) return true;
    if (cp < 0x231A || cp > 0x3299) return false;
    if (cp <= 0x231B) return true;
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;
    if (cp == 0x25AA || cp == 0x25AB) return true;
    if (cp == 0x25B6 || cp == 0x25C0) return true;
    if (cp >= 0x25FB && cp <= 0x25FE) return true;
    if (cp == 0x2614 || cp == 0x2615) return true;
    if (cp >= 0x2648 && cp <= 0x2653) return true;
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return true;
    if (cp == 0x26AA || cp == 0x26AB) return true;
    if (cp == 0x26BD || cp == 0x26BE) return true;
    if (cp == 0x26C4 || cp == 0x26C5) return true;
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return true;
    if (cp == 0x26F2 || cp == 0x26F3 || cp == 0x26F5) return true;
    if (cp == 0x26FA || cp == 0x26FD) return true;
    if (cp == 0x2702 || cp == 0x2705) return true;
    if (cp >= 0x2708 && cp <= 0x270D) return true;
    if (cp == 0x270F || cp == 0x2712) return true;
    if (cp == 0x2714 || cp == 0x2716) return true;
    if (cp == 0x271D || cp == 0x2721 || cp == 0x2728) return true;
    if (cp == 0x2733 || cp == 0x2734) return true;
    if (cp == 0x2744 || cp == 0x2747) return true;
    if (cp == 0x274C || cp == 0x274E) return true;
    if (cp >= 0x2753 && cp <= 0x2755) return true;
    if (cp == 0x2757) return true;
    if (cp == 0x2763 || cp == 0x2764) return true;
    if (cp >= 0x2795 && cp <= 0x2797) return true;
    if (cp == 0x27A1 || cp == 0x27B0 || cp == 0x27BF) return true;
    if (cp == 0x2934 || cp == 0x2935) return true;
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;
    if (cp == 0x2B1B || cp == 0x2B1C) return true;
    if (cp == 0x2B50 || cp == 0x2B55) return true;
    if (cp == 0x3030 || cp == 0x303D) return true;
    if (cp == 0x3297 || cp == 0x3299) return true;
    return false;
}

// Forward declarations for functions from other modules
int ui_context_init(UiContext* uicon, bool headless);
void ui_context_cleanup(UiContext* uicon);
void ui_context_create_surface(UiContext* uicon, int pixel_width, int pixel_height);
void layout_html_doc(UiContext* uicon, DomDocument* doc, bool is_reflow);
// load_html_doc is declared in view.hpp (via layout.hpp)

void render_block_view(RenderContext* rdcon, ViewBlock* view_block);
void render_inline_view(RenderContext* rdcon, ViewSpan* view_span);
void render_children(RenderContext* rdcon, View* view);
void render_image_content(RenderContext* rdcon, ViewBlock* view);
void render_video_content(RenderContext* rdcon, ViewBlock* view);
void render_webview_layer_content(RenderContext* rdcon, ViewBlock* view);
void scrollpane_render(RdtVector* vec, ScrollPane* sp, Rect* block_bound,
    float content_width, float content_height, Bound* clip, float scale,
    bool show_hz_scroll = true, bool show_vt_scroll = true);
void render_form_control(RenderContext* rdcon, ViewBlock* block);  // form controls
void render_select_dropdown(RenderContext* rdcon, ViewBlock* select, RadiantState* state);  // select dropdown popup
void render_column_rules(RenderContext* rdcon, ViewBlock* block);  // multi-column rules
// post-composite video frame blit (defined in render_video.cpp)
void render_video_frames(DisplayList* dl, ImageSurface* surface, RadiantState* rstate, UiContext* uicon);
void render_video_frames_cached(RadiantState* rstate, ImageSurface* surface, UiContext* uicon);

// ============================================================================
// Per-corner rounded rect path helper
// ============================================================================

/**
 * Build a path for a rectangle with per-corner border radii.
 * Uses cubic Bézier arcs (kappa ≈ 0.5522847) to approximate quarter-circle arcs.
 */
static RdtPath* create_per_corner_rounded_rect_path(
    float x, float y, float w, float h,
    float r_tl, float r_tr, float r_br, float r_bl) {

    RdtPath* p = rdt_path_new();
    // clamp radii to half-dimensions
    float max_rx = w * 0.5f, max_ry = h * 0.5f;
    if (r_tl > max_rx) r_tl = max_rx; if (r_tl > max_ry) r_tl = max_ry;
    if (r_tr > max_rx) r_tr = max_rx; if (r_tr > max_ry) r_tr = max_ry;
    if (r_br > max_rx) r_br = max_rx; if (r_br > max_ry) r_br = max_ry;
    if (r_bl > max_rx) r_bl = max_rx; if (r_bl > max_ry) r_bl = max_ry;

    const float k = 0.5522847f; // cubic Bézier kappa for 90° arc

    // start at top-left corner, after the TL arc
    rdt_path_move_to(p, x + r_tl, y);
    // top edge → top-right corner
    rdt_path_line_to(p, x + w - r_tr, y);
    if (r_tr > 0) {
        rdt_path_cubic_to(p, x + w - r_tr + r_tr * k, y,
                             x + w, y + r_tr - r_tr * k,
                             x + w, y + r_tr);
    }
    // right edge → bottom-right corner
    rdt_path_line_to(p, x + w, y + h - r_br);
    if (r_br > 0) {
        rdt_path_cubic_to(p, x + w, y + h - r_br + r_br * k,
                             x + w - r_br + r_br * k, y + h,
                             x + w - r_br, y + h);
    }
    // bottom edge → bottom-left corner
    rdt_path_line_to(p, x + r_bl, y + h);
    if (r_bl > 0) {
        rdt_path_cubic_to(p, x + r_bl - r_bl * k, y + h,
                             x, y + h - r_bl + r_bl * k,
                             x, y + h - r_bl);
    }
    // left edge → top-left corner
    rdt_path_line_to(p, x, y + r_tl);
    if (r_tl > 0) {
        rdt_path_cubic_to(p, x, y + r_tl - r_tl * k,
                             x + r_tl - r_tl * k, y,
                             x + r_tl, y);
    }
    rdt_path_close(p);
    return p;
}

// Create a ThorVG RdtPath from a ClipShape for use with rdt_push_clip
static RdtPath* create_clip_shape_path(ClipShape* cs) {
    if (!cs) return nullptr;
    RdtPath* p = rdt_path_new();
    switch (cs->type) {
        case CLIP_SHAPE_ROUNDED_RECT: {
            auto& rr = cs->rounded_rect;
            rdt_path_free(p);
            return create_per_corner_rounded_rect_path(rr.x, rr.y, rr.w, rr.h,
                rr.r_tl, rr.r_tr, rr.r_br, rr.r_bl);
        }
        case CLIP_SHAPE_CIRCLE:
            rdt_path_add_circle(p, cs->circle.cx, cs->circle.cy, cs->circle.r, cs->circle.r);
            break;
        case CLIP_SHAPE_ELLIPSE:
            rdt_path_add_circle(p, cs->ellipse.cx, cs->ellipse.cy, cs->ellipse.rx, cs->ellipse.ry);
            break;
        case CLIP_SHAPE_INSET:
            rdt_path_add_rect(p, cs->inset.x, cs->inset.y, cs->inset.w, cs->inset.h, 0, 0);
            break;
        case CLIP_SHAPE_POLYGON:
            if (cs->polygon.count >= 3) {
                rdt_path_move_to(p, cs->polygon.vx[0], cs->polygon.vy[0]);
                for (int i = 1; i < cs->polygon.count; i++) {
                    rdt_path_line_to(p, cs->polygon.vx[i], cs->polygon.vy[i]);
                }
                rdt_path_close(p);
            }
            break;
        default: break;
    }
    return p;
}

// Free a scratch-allocated ClipShape
static void free_clip_shape(ScratchArena* sa, ClipShape* shape) {
    if (shape) {
        if (shape->type == CLIP_SHAPE_POLYGON) {
            scratch_free(sa, shape->polygon.vy);
            scratch_free(sa, shape->polygon.vx);
        }
        scratch_free(sa, shape);
    }
}

// Parse CSS clip-path value and return a ClipShape
static ClipShape* parse_css_clip_shape(ScratchArena* sa, const char* value, float elem_w, float elem_h,
                                        float abs_x, float abs_y) {
    if (!value || strncmp(value, "none", 4) == 0) return nullptr;

    auto parse_len = [](const char*& s, float ref) -> float {
        while (*s == ' ' || *s == ',') s++;
        float val = strtof(s, (char**)&s);
        while (*s == ' ') s++;
        if (*s == '%') { s++; return val / 100.0f * ref; }
        if (s[0] == 'p' && s[1] == 'x') s += 2;
        return val;
    };

    if (strncmp(value, "inset(", 6) == 0) {
        const char* s = value + 6;
        // Parse 1-4 inset values (CSS shorthand: top [right [bottom [left]]])
        float vals[4] = {0};
        int val_count = 0;
        while (*s && *s != ')' && val_count < 4) {
            while (*s == ' ') s++;
            if (*s == ')' || strncmp(s, "round", 5) == 0) break;
            float ref = (val_count == 0 || val_count == 2) ? elem_h : elem_w;
            vals[val_count++] = parse_len(s, ref);
        }
        float top, right_v, bottom, left_v;
        if (val_count == 1)      { top = right_v = bottom = left_v = vals[0]; }
        else if (val_count == 2) { top = bottom = vals[0]; right_v = left_v = vals[1]; }
        else if (val_count == 3) { top = vals[0]; right_v = left_v = vals[1]; bottom = vals[2]; }
        else                     { top = vals[0]; right_v = vals[1]; bottom = vals[2]; left_v = vals[3]; }
        float rx = 0, ry = 0;
        while (*s == ' ') s++;
        if (strncmp(s, "round", 5) == 0) {
            s += 5;
            rx = parse_len(s, elem_w);
            ry = rx;
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(sa, sizeof(ClipShape));
        if (rx > 0 || ry > 0) {
            cs->type = CLIP_SHAPE_ROUNDED_RECT;
            cs->rounded_rect = {abs_x + left_v, abs_y + top,
                                elem_w - left_v - right_v, elem_h - top - bottom,
                                rx, rx, rx, rx};
        } else {
            cs->type = CLIP_SHAPE_INSET;
            cs->inset = {abs_x + left_v, abs_y + top,
                         elem_w - left_v - right_v, elem_h - top - bottom, 0, 0};
        }
        return cs;
    }

    if (strncmp(value, "circle(", 7) == 0) {
        const char* s = value + 7;
        float ref = fmin(elem_w, elem_h);
        float r = parse_len(s, ref);
        float cx = abs_x + elem_w * 0.5f, cy = abs_y + elem_h * 0.5f;
        while (*s == ' ') s++;
        if (strncmp(s, "at", 2) == 0) {
            s += 2;
            cx = abs_x + parse_len(s, elem_w);
            cy = abs_y + parse_len(s, elem_h);
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(sa, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_CIRCLE;
        cs->circle = {cx, cy, r};
        return cs;
    }

    if (strncmp(value, "ellipse(", 8) == 0) {
        const char* s = value + 8;
        float rx = parse_len(s, elem_w);
        float ry = parse_len(s, elem_h);
        float cx = abs_x + elem_w * 0.5f, cy = abs_y + elem_h * 0.5f;
        while (*s == ' ') s++;
        if (strncmp(s, "at", 2) == 0) {
            s += 2;
            cx = abs_x + parse_len(s, elem_w);
            cy = abs_y + parse_len(s, elem_h);
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(sa, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_ELLIPSE;
        cs->ellipse = {cx, cy, rx, ry};
        return cs;
    }

    if (strncmp(value, "polygon(", 8) == 0) {
        const char* s = value + 8;
        // count points first
        const char* scan = s;
        int count = 0;
        while (*scan && *scan != ')') {
            while (*scan == ' ' || *scan == ',') scan++;
            if (*scan == ')') break;
            strtof(scan, (char**)&scan);
            while (*scan == ' ') scan++;
            if (*scan == '%') scan++;
            if (scan[0] == 'p' && scan[1] == 'x') scan += 2;
            strtof(scan, (char**)&scan);
            while (*scan == ' ') scan++;
            if (*scan == '%') scan++;
            if (scan[0] == 'p' && scan[1] == 'x') scan += 2;
            count++;
            while (*scan == ' ' || *scan == ',') scan++;
        }
        if (count < 3) return nullptr;

        float* vx = (float*)scratch_alloc(sa, count * sizeof(float));
        float* vy = (float*)scratch_alloc(sa, count * sizeof(float));
        for (int i = 0; i < count; i++) {
            vx[i] = parse_len(s, elem_w) + abs_x;
            vy[i] = parse_len(s, elem_h) + abs_y;
        }
        ClipShape* cs = (ClipShape*)scratch_calloc(sa, sizeof(ClipShape));
        cs->type = CLIP_SHAPE_POLYGON;
        cs->polygon = {vx, vy, count};
        return cs;
    }

    return nullptr;
}

/**
 * Helper function to apply transform and push paint to canvas
 * (Legacy — retained for infrastructure until full canvas removal)
 */

// draw a color glyph bitmap (BGRA format, used for color emoji) into the doc surface
// supports bitmap_scale for fixed-size bitmap fonts (e.g. emoji at 109ppem → 16px)
void draw_color_glyph(RenderContext* rdcon, GlyphBitmap *bitmap, int x, int y) {
    float bscale = bitmap->bitmap_scale;
    if (bscale <= 0.0f) bscale = 1.0f;

    // target dimensions after scaling
    int target_w = (int)(bitmap->width  * bscale + 0.5f);
    int target_h = (int)(bitmap->height * bscale + 0.5f);
    if (target_w <= 0 || target_h <= 0) return;

    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + target_w);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + target_h);
    if (left >= right || top >= bottom) return; // glyph outside the surface

    // Check if per-row clip shape clipping is needed
    bool need_shape_clip = (rdcon->clip_shape_depth > 0) &&
        !clip_shapes_rect_inside(rdcon->clip_shapes, rdcon->clip_shape_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    ImageSurface* surface = rdcon->ui_context->surface;
    float inv_scale = 1.0f / bscale; // map target pixels back to source pixels

    for (int dy = top - y; dy < bottom - y; dy++) {
        int row_left = left, row_right = right;
        if (need_shape_clip) {
            float py = (float)(y + dy) + 0.5f;
            clip_shapes_scanline_bounds(rdcon->clip_shapes, rdcon->clip_shape_depth,
                py, left, right, &row_left, &row_right);
            if (row_left >= row_right) continue;
        }
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + dy - surface->tile_offset_y) * surface->pitch;
        // map target row to source row with bilinear interpolation
        float src_y = dy * inv_scale;
        int sy0 = (int)src_y;
        int sy1 = sy0 + 1;
        float fy = src_y - sy0;
        if (sy0 >= (int)bitmap->height) sy0 = bitmap->height - 1;
        if (sy1 >= (int)bitmap->height) sy1 = bitmap->height - 1;

        for (int dx = row_left - x; dx < row_right - x; dx++) {
            if (x + dx < 0 || x + dx >= surface->width) continue;

            float src_x = dx * inv_scale;
            int sx0 = (int)src_x;
            int sx1 = sx0 + 1;
            float fx = src_x - sx0;
            if (sx0 >= (int)bitmap->width) sx0 = bitmap->width - 1;
            if (sx1 >= (int)bitmap->width) sx1 = bitmap->width - 1;

            // bilinear sample from BGRA source
            uint8_t* s00 = bitmap->buffer + sy0 * bitmap->pitch + sx0 * 4;
            uint8_t* s10 = bitmap->buffer + sy0 * bitmap->pitch + sx1 * 4;
            uint8_t* s01 = bitmap->buffer + sy1 * bitmap->pitch + sx0 * 4;
            uint8_t* s11 = bitmap->buffer + sy1 * bitmap->pitch + sx1 * 4;

            float w00 = (1 - fx) * (1 - fy);
            float w10 = fx * (1 - fy);
            float w01 = (1 - fx) * fy;
            float w11 = fx * fy;

            uint8_t src_b = (uint8_t)(s00[0]*w00 + s10[0]*w10 + s01[0]*w01 + s11[0]*w11 + 0.5f);
            uint8_t src_g = (uint8_t)(s00[1]*w00 + s10[1]*w10 + s01[1]*w01 + s11[1]*w11 + 0.5f);
            uint8_t src_r = (uint8_t)(s00[2]*w00 + s10[2]*w10 + s01[2]*w01 + s11[2]*w11 + 0.5f);
            uint8_t src_a = (uint8_t)(s00[3]*w00 + s10[3]*w10 + s01[3]*w01 + s11[3]*w11 + 0.5f);

            if (src_a > 0) {
                uint8_t* dst = (uint8_t*)(row_pixels + (x + dx) * 4);
                if (src_a == 255) {
                    dst[0] = src_r;  // our surface is RGBA
                    dst[1] = src_g;
                    dst[2] = src_b;
                    dst[3] = 255;
                } else {
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
void draw_glyph(RenderContext* rdcon, GlyphBitmap *bitmap, int x, int y) {
    if (rdcon->dl) {
        bool is_color = (bitmap->pixel_mode == GLYPH_PIXEL_BGRA);
        dl_draw_glyph(rdcon->dl, bitmap, x, y, rdcon->color, is_color, &rdcon->block.clip);
        return;
    }
    // handle color emoji bitmaps (BGRA format)
    if (bitmap->pixel_mode == GLYPH_PIXEL_BGRA) {
        draw_color_glyph(rdcon, bitmap, x, y);
        return;
    }
    int left = max(rdcon->block.clip.left, x);
    int right = min(rdcon->block.clip.right, x + (int)bitmap->width);
    int top = max(rdcon->block.clip.top, y);
    int bottom = min(rdcon->block.clip.bottom, y + (int)bitmap->height);
    if (left >= right || top >= bottom) {
        log_debug("glyph clipped: x=%d, y=%d, bitmap=%dx%d, clip=[%.0f,%.0f,%.0f,%.0f]",
            x, y, bitmap->width, bitmap->height,
            rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
        return; // glyph outside the surface
    }
    log_debug("[GLYPH RENDER] drawing glyph at x=%d y=%d size=%dx%d color=#%02x%02x%02x (c=0x%08x) pixel_mode=%d",
        x, y, bitmap->width, bitmap->height, rdcon->color.r, rdcon->color.g, rdcon->color.b, rdcon->color.c, bitmap->pixel_mode);
    ImageSurface* surface = rdcon->ui_context->surface;

    // Check if per-row clip shape clipping is needed
    bool need_shape_clip = (rdcon->clip_shape_depth > 0) &&
        !clip_shapes_rect_inside(rdcon->clip_shapes, rdcon->clip_shape_depth,
            (float)left + 0.5f, (float)top + 0.5f,
            (float)(right - left - 1), (float)(bottom - top - 1));

    // handle monochrome bitmaps (1 bit per pixel) - common for some system fonts like Monaco
    bool is_mono = (bitmap->pixel_mode == GLYPH_PIXEL_MONO);

    for (int i = top - y; i < bottom - y; i++) {
        int j_start = left - x;
        int j_end = right - x;
        if (need_shape_clip) {
            float py = (float)(y + i) + 0.5f;
            int rl = left, rr = right;
            clip_shapes_scanline_bounds(rdcon->clip_shapes, rdcon->clip_shape_depth,
                py, left, right, &rl, &rr);
            if (rl >= rr) continue;
            j_start = rl - x;
            j_end = rr - x;
        }
        uint8_t* row_pixels = (uint8_t*)surface->pixels + (y + i - surface->tile_offset_y) * surface->pitch;
        for (int j = j_start; j < j_end; j++) {
            if (x + j < 0 || x + j >= surface->width) continue;

            uint32_t intensity;
            if (is_mono) {
                // for monochrome: each byte contains 8 pixels, MSB first
                int byte_index = j / 8;
                int bit_index = 7 - (j % 8);  // MSB is leftmost pixel
                uint8_t byte_val = bitmap->buffer[i * bitmap->pitch + byte_index];
                intensity = (byte_val & (1 << bit_index)) ? 255 : 0;
            } else {
                // grayscale: 1 byte per pixel
                intensity = bitmap->buffer[i * bitmap->pitch + j];
            }

            if (intensity > 0) {
                // blend the pixel with the background
                uint8_t* p = (uint8_t*)(row_pixels + (x + j) * 4);

                // important to use 32bit int for computation below
                uint32_t v = 255 - intensity;
                // can further optimize if background is a fixed color
                if (rdcon->color.c == 0xFF000000) { // black text color (ABGR: alpha=FF, b=00, g=00, r=00)
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

/**
 * Compare two views in document order (pre-order tree traversal order).
 * Returns:
 *   -1 if view_a comes before view_b
 *    0 if view_a equals view_b
 *    1 if view_a comes after view_b
 *
 * Algorithm: Walk up to find common ancestor, then compare sibling order.
 */
static int compare_view_order(View* view_a, View* view_b) {
    if (view_a == view_b) return 0;
    if (!view_a) return -1;
    if (!view_b) return 1;

    // Build ancestor chains for both views
    View* chain_a[64];  // max depth 64
    View* chain_b[64];
    int depth_a = 0, depth_b = 0;

    for (View* v = view_a; v && depth_a < 64; v = v->parent) {
        chain_a[depth_a++] = v;
    }
    for (View* v = view_b; v && depth_b < 64; v = v->parent) {
        chain_b[depth_b++] = v;
    }

    // Find common ancestor by walking up from root
    int i = depth_a - 1, j = depth_b - 1;
    while (i >= 0 && j >= 0 && chain_a[i] == chain_b[j]) {
        i--; j--;
    }

    // Now chain_a[i+1] == chain_b[j+1] is the common ancestor
    // Compare chain_a[i] and chain_b[j] (children of common ancestor)
    if (i < 0) {
        // view_a is ancestor of view_b, so view_a comes first
        return -1;
    }
    if (j < 0) {
        // view_b is ancestor of view_a, so view_b comes first
        return 1;
    }

    // Compare sibling order: which comes first among children of common ancestor?
    View* child_a = chain_a[i];
    View* child_b = chain_b[j];

    // Walk through siblings to find which comes first
    for (View* sib = child_a; sib; sib = (View*)sib->next_sibling) {
        if (sib == child_b) {
            return -1;  // child_a comes before child_b
        }
    }
    return 1;  // child_b comes before child_a
}

/**
 * Check if a text view is within a cross-view selection and determine
 * which portion of the text should be selected.
 *
 * Returns:
 *   0: not in selection
 *   1: fully selected (all text)
 *   2: partially selected (check *start_offset and *end_offset)
 */
static int get_selection_range_for_view(SelectionState* sel, View* text_view,
    int text_length, int* start_offset, int* end_offset) {

    if (!sel || sel->is_collapsed) return 0;

    View* anchor_view = sel->anchor_view;
    View* focus_view = sel->focus_view;

    // Single-view selection (legacy)
    if (!anchor_view || !focus_view) {
        anchor_view = sel->view;
        focus_view = sel->view;
    }

    // Same view for anchor and focus
    if (anchor_view == focus_view) {
        if (text_view != anchor_view) return 0;
        // Normalize selection range
        *start_offset = sel->anchor_offset < sel->focus_offset ? sel->anchor_offset : sel->focus_offset;
        *end_offset = sel->anchor_offset > sel->focus_offset ? sel->anchor_offset : sel->focus_offset;
        return 2;  // partially selected
    }

    // Cross-view selection: determine order
    int anchor_vs_focus = compare_view_order(anchor_view, focus_view);
    View* first_view = (anchor_vs_focus <= 0) ? anchor_view : focus_view;
    View* last_view = (anchor_vs_focus <= 0) ? focus_view : anchor_view;
    int first_offset = (anchor_vs_focus <= 0) ? sel->anchor_offset : sel->focus_offset;
    int last_offset = (anchor_vs_focus <= 0) ? sel->focus_offset : sel->anchor_offset;

    // Check if text_view is the first view
    if (text_view == first_view) {
        *start_offset = first_offset;
        *end_offset = text_length;  // select to end
        return 2;
    }

    // Check if text_view is the last view
    if (text_view == last_view) {
        *start_offset = 0;
        *end_offset = last_offset;
        return 2;
    }

    // Check if text_view is between first and last
    int view_vs_first = compare_view_order(text_view, first_view);
    int view_vs_last = compare_view_order(text_view, last_view);

    if (view_vs_first > 0 && view_vs_last < 0) {
        // text_view is between first and last - fully selected
        *start_offset = 0;
        *end_offset = text_length;
        return 1;
    }

    return 0;  // not in selection
}

/**
 * Check if a view (any type, including images) is within a cross-view selection.
 * Returns true if the view is between anchor and focus views.
 */
static bool is_view_in_selection(SelectionState* sel, View* view) {
    if (!sel || sel->is_collapsed || !view) return false;

    View* anchor_view = sel->anchor_view;
    View* focus_view = sel->focus_view;

    // Single-view selection (legacy) - images won't be text views
    if (!anchor_view || !focus_view) {
        return false;
    }

    // Same view for anchor and focus - no cross-view selection
    if (anchor_view == focus_view) {
        return false;
    }

    // Cross-view selection: determine order
    int anchor_vs_focus = compare_view_order(anchor_view, focus_view);
    View* first_view = (anchor_vs_focus <= 0) ? anchor_view : focus_view;
    View* last_view = (anchor_vs_focus <= 0) ? focus_view : anchor_view;

    // Check if view is between first and last (inclusive of first and last)
    int view_vs_first = compare_view_order(view, first_view);
    int view_vs_last = compare_view_order(view, last_view);

    // View is selected if it's >= first and <= last in document order
    return (view_vs_first >= 0 && view_vs_last <= 0);
}

// ---------------------------------------------------------------------------
// text-decoration-skip-ink: auto — compute gaps where glyph ink crosses the
// decoration line, so the underline/line-through skips around descenders etc.
// ---------------------------------------------------------------------------
struct SkipInkGap { float x0, x1; };  // horizontal gap range (physical px)

static int collect_skip_ink_gaps(RenderContext* rdcon, unsigned char* str,
                                  TextRect* text_rect, float deco_y_top, float deco_y_bot,
                                  SkipInkGap* gaps, int max_gaps) {
    float s = rdcon->scale;
    float x = rdcon->block.x + text_rect->x * s;
    float y_base = rdcon->block.y + text_rect->y * s;
    float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * s;
    int gap_count = 0;
    float pad = fmaxf(2.0f, (deco_y_bot - deco_y_top));  // padding around glyph ink scales with thickness

    unsigned char* p = str + text_rect->start_index;
    unsigned char* end = p + text_rect->length;
    FontStyleDesc sd = font_style_desc_from_prop(rdcon->font.style);

    while (p < end && gap_count < max_gaps) {
        if (is_space(*p)) { x += rdcon->font.style->space_width * s; p++; continue; }
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
        if (bytes <= 0) { p++; continue; }
        p += bytes;
        if (codepoint == 0x00AD) continue;  // skip soft hyphen

        // Apply text-transform (simplified: just first codepoint)
        uint32_t tt_out[3];
        CssEnum text_transform = CSS_VALUE_NONE;
        // We don't need exact text-transform here — the glyph advance will be
        // close enough for gap positioning even without transform.
        LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &sd, codepoint, true);
        if (!glyph) { x += rdcon->font.style->space_width * s; continue; }

        // Glyph ink vertical extent in physical coords
        float glyph_top = y_base + ascend - glyph->bitmap.bearing_y;
        float glyph_bot = glyph_top + glyph->bitmap.height;
        float glyph_left = x + glyph->bitmap.bearing_x;
        float glyph_right = glyph_left + glyph->bitmap.width;

        // Check if glyph ink overlaps the decoration rect vertically.
        // Require significant overlap (half the decoration height) to only catch
        // genuine descenders, not anti-aliasing fuzz at glyph edges.
        float overlap = fminf(glyph_bot, deco_y_bot) - fmaxf(glyph_top, deco_y_top);
        float deco_height = deco_y_bot - deco_y_top;
        float min_overlap = fmaxf(deco_height * 0.5f, 2.0f);
        if (overlap >= min_overlap && glyph->bitmap.width > 0) {
            // Scan bitmap columns to find actual ink extent within the decoration
            // y-range, rather than using full glyph width. This gives tighter gaps
            // matching browser behavior (e.g. 'p' descender is narrower than glyph).
            int bm_row_start = (int)fmaxf(deco_y_top - glyph_top, 0.0f);
            int bm_row_end = (int)fminf(deco_y_bot - glyph_top, (float)glyph->bitmap.height);
            int ink_col_min = glyph->bitmap.width;
            int ink_col_max = -1;
            uint8_t* buf = glyph->bitmap.buffer;
            int pitch = glyph->bitmap.pitch;
            // ink threshold: ignore faint anti-aliasing pixels (< 25% opacity)
            const uint8_t ink_thresh = 64;
            if (buf && glyph->bitmap.pixel_mode == GLYPH_PIXEL_GRAY) {
                for (int row = bm_row_start; row < bm_row_end; row++) {
                    uint8_t* rowp = buf + row * pitch;
                    for (int col = 0; col < glyph->bitmap.width; col++) {
                        if (rowp[col] >= ink_thresh) {
                            if (col < ink_col_min) ink_col_min = col;
                            if (col > ink_col_max) ink_col_max = col;
                        }
                    }
                }
            }
            if (ink_col_max >= ink_col_min) {
                // use actual ink extent within decoration band
                gaps[gap_count].x0 = glyph_left + ink_col_min - pad;
                gaps[gap_count].x1 = glyph_left + ink_col_max + 1.0f + pad;
            } else {
                // fallback to full glyph width if scan found nothing
                gaps[gap_count].x0 = glyph_left - pad;
                gaps[gap_count].x1 = glyph_right + pad;
            }
            gap_count++;
        }

        x += glyph->advance_x + rdcon->font.style->letter_spacing * s;
    }
    return gap_count;
}

// Draw a decoration line with skip-ink gaps.  Calls fill_fn for each visible segment.
static void draw_deco_with_gaps(RenderContext* rdcon, Rect rect, uint32_t color,
                                 SkipInkGap* gaps, int gap_count) {
    float x_start = rect.x;
    float x_end = rect.x + rect.width;
    for (int i = 0; i < gap_count; i++) {
        if (gaps[i].x0 > x_start) {
            float seg_end = fminf(gaps[i].x0, x_end);
            if (seg_end > x_start) {
                Rect seg = {x_start, rect.y, seg_end - x_start, rect.height};
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, color,
                                     &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        }
        x_start = fmaxf(x_start, gaps[i].x1);
        if (x_start >= x_end) break;
    }
    if (x_start < x_end) {
        Rect seg = {x_start, rect.y, x_end - x_start, rect.height};
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, color,
                             &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
}

void render_text_view(RenderContext* rdcon, ViewText* text_view) {
    log_debug("render_text_view clip:[%.0f,%.0f,%.0f,%.0f]",
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);

    // CSS 2.1 §11.2: text inherits visibility from parent element
    if (text_view->parent && text_view->parent->is_element()) {
        DomElement* parent_elem = (DomElement*)text_view->parent;
        if (parent_elem->in_line && parent_elem->in_line->visibility == VIS_HIDDEN) {
            log_debug("text hidden by parent visibility:hidden");
            return;
        }
    }

    if (!rdcon->font.font_handle) {
        log_debug("font face is null");
        return;
    }

    float s = rdcon->scale;  // scale factor for CSS -> physical pixels
    unsigned char* str = text_view->text_data();
    TextRect* text_rect = text_view->rect;

    if (!text_rect) {
        log_debug("no text rect for text view");
        return;
    }

    // Check if this text view has a selection (supports cross-view selection)
    // Phase A: inline glyph-by-glyph painter disabled. The multi-rect overlay
    // in render_selection() (driven by DomSelection / dom_range_for_each_rect)
    // now paints text-selection backgrounds during render_ui_overlays.
    SelectionState* sel = rdcon->selection;
    int sel_start = 0, sel_end = 0;
    (void)sel; (void)sel_start; (void)sel_end;

    // Calculate total text length for cross-view selection check
    int total_text_length = 0;
    if (str) {
        total_text_length = strlen((const char*)str);
    }
    (void)total_text_length;

    // Inline selection painter disabled — see comment above.
    bool has_selection = false;

    // Apply text color from text_view if set (PDF text uses this for fill color)
    Color saved_color = rdcon->color;
    if (text_view->color.c != 0) {
        rdcon->color = text_view->color;
    }

    // Setup font from text_view if set (PDF text has font property directly on ViewText)
    FontBox saved_font = rdcon->font;
    if (text_view->font) {
        setup_font(rdcon->ui_context, &rdcon->font, text_view->font);
    }

    // Skip rendering if font size is 0 - text should be invisible (e.g., font-size: 0)
    if (rdcon->font.style && rdcon->font.style->font_size <= 0.0f) {
        log_debug("skipping zero font-size text render");
        return;
    }

    // Get the white-space property for this text node
    CssEnum white_space = get_white_space_value(text_view);
    bool preserve_spaces = ws_preserve_spaces(white_space);

    // Get text-transform from parent elements
    CssEnum text_transform = CSS_VALUE_NONE;
    CssEnum text_align = CSS_VALUE_LEFT;  // default to left alignment
    DomNode* parent = text_view->parent;
    while (parent) {
        if (parent->is_element()) {
            DomElement* elem = (DomElement*)parent;
            CssEnum transform = get_text_transform_from_block(elem->blk);
            if (transform != CSS_VALUE_NONE) {
                text_transform = transform;
            }
            // Get text-align property from block properties
            if (elem->blk) {
                BlockProp* blk_prop = (BlockProp*)elem->blk;
                text_align = blk_prop->text_align;
            }
            if (transform != CSS_VALUE_NONE) {
                break;
            }
        }
        parent = parent->parent;
    }

    // Check if parent inline element has a background color to render.
    // Only paint per-fragment backgrounds for true inline parents (e.g., <span>, <em>).
    // Block-level parents (flex items, blocks, table cells) already have their
    // background painted by render_bound — re-painting here with padding offsets
    // would cover sibling elements like inline SVGs.
    DomElement* parent_elem = text_view->parent ? text_view->parent->as_element() : nullptr;
    Color* bg_color = nullptr;
    float bg_pad_top = 0, bg_pad_right = 0, bg_pad_bottom = 0, bg_pad_left = 0;
    float bg_radius = 0;
    if (parent_elem && parent_elem->view_type == RDT_VIEW_INLINE &&
        parent_elem->bound && parent_elem->bound->background &&
        parent_elem->bound->background->color.a > 0) {
        bg_color = &parent_elem->bound->background->color;
        // include parent inline padding in per-fragment background
        bg_pad_top    = parent_elem->bound->padding.top * s;
        bg_pad_right  = parent_elem->bound->padding.right * s;
        bg_pad_bottom = parent_elem->bound->padding.bottom * s;
        bg_pad_left   = parent_elem->bound->padding.left * s;
        // use border-radius for rounded fragment backgrounds
        if (parent_elem->bound->border)
            bg_radius = parent_elem->bound->border->radius.top_left * s;
    }

    // Get text-shadow from parent element's font property
    TextShadow* text_shadow = nullptr;
    if (parent_elem && parent_elem->font && parent_elem->font->text_shadow) {
        text_shadow = parent_elem->font->text_shadow;
    }

    while (text_rect) {
        // Apply scale to convert CSS pixel positions to physical surface pixels
        float x = rdcon->block.x + text_rect->x * s, y = rdcon->block.y + text_rect->y * s;

        // Render background for inline element if present (per-fragment for correct wrapping)
        if (bg_color) {
            bool is_first = (text_rect == text_view->rect);
            bool is_last  = (text_rect->next == nullptr);
            float pl = is_first ? bg_pad_left  : 0;
            float pr = is_last  ? bg_pad_right : 0;
            Rect bg_rect = {
                x - pl,
                y - bg_pad_top,
                text_rect->width * s + pl + pr,
                text_rect->height * s + bg_pad_top + bg_pad_bottom
            };
            if (bg_radius > 0) {
                // first fragment: left radii; last fragment: right radii
                float r_left  = is_first ? bg_radius : 0;
                float r_right = is_last  ? bg_radius : 0;
                // build per-corner rounded rect path
                RdtPath* p = rdt_path_new();
                float fx = bg_rect.x, fy = bg_rect.y, fw = bg_rect.width, fh = bg_rect.height;
                rdt_path_move_to(p, fx + r_left, fy);
                rdt_path_line_to(p, fx + fw - r_right, fy);
                if (r_right > 0) rdt_path_cubic_to(p, fx+fw-r_right*0.45f, fy, fx+fw, fy+r_right*0.45f, fx+fw, fy+r_right);
                else             rdt_path_line_to(p, fx+fw, fy);
                rdt_path_line_to(p, fx + fw, fy + fh - r_right);
                if (r_right > 0) rdt_path_cubic_to(p, fx+fw, fy+fh-r_right*0.45f, fx+fw-r_right*0.45f, fy+fh, fx+fw-r_right, fy+fh);
                else             rdt_path_line_to(p, fx+fw, fy+fh);
                rdt_path_line_to(p, fx + r_left, fy + fh);
                if (r_left > 0) rdt_path_cubic_to(p, fx+r_left*0.45f, fy+fh, fx, fy+fh-r_left*0.45f, fx, fy+fh-r_left);
                else            rdt_path_line_to(p, fx, fy+fh);
                rdt_path_line_to(p, fx, fy + r_left);
                if (r_left > 0) rdt_path_cubic_to(p, fx, fy+r_left*0.45f, fx+r_left*0.45f, fy, fx+r_left, fy);
                rdt_path_close(p);
                rc_fill_path(rdcon, p, *bg_color, RDT_FILL_WINDING, NULL);
                rdt_path_free(p);
            } else {
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &bg_rect, bg_color->c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        }

        unsigned char* p = str + text_rect->start_index;  unsigned char* end = p + text_rect->length;
        log_debug("draw text:'%t', start:%d, len:%d, x:%f, y:%f, wd:%f, hg:%f, at (%f, %f), white_space:%d, preserve:%d, color:0x%08x",
            str, text_rect->start_index, text_rect->length, text_rect->x, text_rect->y, text_rect->width, text_rect->height, x, y,
            white_space, preserve_spaces, rdcon->color.c);

        // Calculate natural text width and space count for justify rendering
        // Note: space_width is in CSS pixels (scaled down for layout), need to scale up for render
        float scaled_space_width = rdcon->font.style->space_width * s;
        float natural_width = 0.0f;
        int space_count = 0;
        unsigned char* scan = p;
        bool scan_has_space = false;

        // Scan all content including trailing spaces for width calculation
        // Trailing whitespace is intentionally included because layout has already
        // determined the correct width and positioning - we should render exactly
        // what was laid out, including spaces between inline elements
        unsigned char* content_end = end;

        while (scan < content_end) {
            if (is_space(*scan)) {
                if (preserve_spaces || !scan_has_space) {
                    scan_has_space = true;
                    natural_width += scaled_space_width;
                    space_count++;
                }
                scan++;
            }
            else {
                scan_has_space = false;
                uint32_t scan_codepoint;
                int bytes = str_utf8_decode((const char*)scan, (size_t)(content_end - scan), &scan_codepoint);
                if (bytes <= 0) { scan++; }
                else { scan += bytes; }

                auto t1 = std::chrono::high_resolution_clock::now();
                FontStyleDesc _sd = font_style_desc_from_prop(rdcon->font.style);
                LoadedGlyph* glyph = font_load_glyph(rdcon->font.font_handle, &_sd, scan_codepoint, false);
                auto t2 = std::chrono::high_resolution_clock::now();
                g_render_load_glyph_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
                g_render_glyph_count++;
                if (glyph) {
                    natural_width += glyph->advance_x + rdcon->font.style->letter_spacing * s;  // already in physical pixels
                } else {
                    natural_width += scaled_space_width;  // fallback width in physical pixels
                }
            }
        }

        // Calculate adjusted space width for justified text (in physical pixels)
        float space_width = scaled_space_width;
        if (text_align == CSS_VALUE_JUSTIFY && space_count > 0 && natural_width > 0 && text_rect->width * s > natural_width) {
            // This text is explicitly justified - distribute extra space across spaces
            float extra_space = (text_rect->width * s) - natural_width;
            space_width += (extra_space / space_count);
            log_debug("apply justification: text_align=JUSTIFY, natural_width=%f, text_rect->width=%f, space_count=%d, space_width=%f -> %f",
                natural_width, text_rect->width * s, space_count, scaled_space_width, space_width);
        }

        // Render the text with adjusted spacing
        bool has_space = false;  uint32_t codepoint;
        bool is_word_start = true;  // Track word boundaries for capitalize
        int char_index = text_rect->start_index;  // Track character offset for selection

        // Selection background color - standard blue for text selection
        uint32_t sel_bg_color = 0x80FF9933;  // ABGR format: semi-transparent blue (like browser selection)

        // Debug: log inline selection position info
        if (has_selection) {
            log_debug("[SEL-INLINE] text_rect: x=%.1f y=%.1f, rdcon->block: x=%.1f y=%.1f, final pos: x=%.1f y=%.1f, font_size=%.1f, y_ppem=%d",
                text_rect->x, text_rect->y, rdcon->block.x, rdcon->block.y, x, y,
                rdcon->font.style->font_size, (int)font_handle_get_physical_size_px(rdcon->font.font_handle));
        }

        // Track cumulative position for debugging
        float debug_start_x = x;

        // Check if any text-shadow needs blur
        bool shadow_needs_blur = false;
        float max_shadow_blur = 0;
        if (text_shadow) {
            TextShadow* ts = text_shadow;
            while (ts) {
                if (ts->blur_radius > 0) {
                    shadow_needs_blur = true;
                    if (ts->blur_radius > max_shadow_blur) max_shadow_blur = ts->blur_radius;
                }
                ts = ts->next;
            }
        }

        // If text-shadow has blur, do a pre-pass rendering only shadow glyphs,
        // then apply box blur, then the main loop skips shadow rendering.
        if (shadow_needs_blur) {
            unsigned char* sp = str + text_rect->start_index;
            unsigned char* s_end = end;
            float sx_pos = x;
            bool s_has_space = false;
            FontStyleDesc _sd_s = font_style_desc_from_prop(rdcon->font.style);
            bool s_word_start = true;

            // Shadow-only glyph pass
            while (sp < s_end) {
                if (is_space(*sp)) {
                    if (preserve_spaces || !s_has_space) {
                        s_has_space = true;
                        sx_pos += space_width;
                    }
                    s_word_start = true;
                    sp++;
                } else {
                    s_has_space = false;
                    uint32_t s_cp;
                    int s_bytes = str_utf8_decode((const char*)sp, (size_t)(s_end - sp), &s_cp);
                    if (s_bytes <= 0) { sp++; continue; }
                    sp += s_bytes;

                    // skip soft hyphen (U+00AD) — invisible unless line breaks there
                    if (s_cp == 0x00AD) continue;

                    uint32_t s_tt_out[3];
                    int s_tt_count = apply_text_transform_full(s_cp, text_transform, s_word_start, s_tt_out);
                    s_word_start = false;

                    for (int sti = 0; sti < s_tt_count; sti++) {
                        uint32_t s_tt_cp = s_tt_out[sti];
                        if (s_tt_cp == 0) continue;

                    LoadedGlyph* s_glyph = font_load_glyph(rdcon->font.font_handle, &_sd_s, s_tt_cp, true);
                    if (!s_glyph) {
                        sx_pos += scaled_space_width;
                        continue;
                    }

                    float s_ascend;
                    {
                        auto tfm1 = std::chrono::high_resolution_clock::now();
                        s_ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                        auto tfm2 = std::chrono::high_resolution_clock::now();
                        g_render_font_metrics_time += std::chrono::duration<double, std::milli>(tfm2 - tfm1).count();
                        g_render_font_metrics_count++;
                    }
                    Color saved_color = rdcon->color;
                    TextShadow* ts = text_shadow;
                    while (ts) {
                        rdcon->color = ts->color;
                        float gsx = sx_pos + s_glyph->bitmap.bearing_x + ts->offset_x * s;
                        float gsy = y + s_ascend - s_glyph->bitmap.bearing_y + ts->offset_y * s;
                        draw_glyph(rdcon, &s_glyph->bitmap, lroundf(gsx), lroundf(gsy));
                        ts = ts->next;
                    }
                    rdcon->color = saved_color;
                    sx_pos += s_glyph->advance_x + rdcon->font.style->letter_spacing * s;
                    } // end for s_tt_count
                }
            }

            // Apply box blur to the shadow region
            if (rdcon->ui_context->surface) {
                float blur_extend = max_shadow_blur * 2;
                // Compute shadow bounding box (text rect expanded by max shadow offsets + blur)
                float shadow_max_ox = 0, shadow_max_oy = 0;
                TextShadow* ts = text_shadow;
                while (ts) {
                    if (fabsf(ts->offset_x) > shadow_max_ox) shadow_max_ox = fabsf(ts->offset_x);
                    if (fabsf(ts->offset_y) > shadow_max_oy) shadow_max_oy = fabsf(ts->offset_y);
                    ts = ts->next;
                }
                int bx = (int)floorf(x - blur_extend - shadow_max_ox * s);
                int by = (int)floorf(y - blur_extend - shadow_max_oy * s);
                int bw = (int)ceilf(text_rect->width * s + blur_extend * 2 + shadow_max_ox * s * 2);
                int bh = (int)ceilf(text_rect->height * s + blur_extend * 2 + shadow_max_oy * s * 2);
                if (rdcon->dl) {
                    dl_box_blur_region(rdcon->dl, bx, by, bw, bh, max_shadow_blur, 0, nullptr);
                } else if (rdcon->ui_context->surface) {
                    box_blur_region(&rdcon->scratch, rdcon->ui_context->surface, bx, by, bw, bh, max_shadow_blur);
                }
                log_debug("[TEXT-SHADOW] Applied blur radius=%.1f to region (%d,%d,%d,%d) dl=%d", max_shadow_blur, bx, by, bw, bh, rdcon->dl != nullptr);
            }
        }

        while (p < end) {
            // Check if current character is in selection range
            bool is_selected = has_selection && char_index >= sel_start && char_index < sel_end;

            // Debug first selected character
            if (is_selected && char_index == sel_start) {
                log_debug("[SEL-INLINE] First selected char at index=%d, x=%.1f y=%.1f, advance_so_far=%.1f (expected overlay start_x=%.1f * scale=%.1f = %.1f)",
                    char_index, x, y, x - debug_start_x, rdcon->selection->start_x, s, rdcon->selection->start_x * s);
            }

            // log_debug("draw character '%c'", *p);
            if (is_space(*p)) {
                if (preserve_spaces || !has_space) {  // preserve all spaces or add single whitespace
                    has_space = true;

                    // Draw selection background for selected space
                    if (is_selected) {
                        Rect sel_rect = {x, y, space_width, text_rect->height * s};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &sel_rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    }

                    // Render space by advancing x position
                    // All spaces are rendered (not just non-trailing) because layout has
                    // already determined correct positioning including inter-element whitespace
                    x += space_width;  // Use adjusted space width for justified text
                }
                // else  // skip consecutive spaces
                is_word_start = true;  // Next non-space is word start
                p++;
                char_index++;
            }
            else {
                has_space = false;
                int bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &codepoint);
                if (bytes <= 0) { p++;  codepoint = 0;  char_index++; }
                else { p += bytes;  char_index++; }

                // skip soft hyphen (U+00AD) — invisible unless line breaks there
                if (codepoint == 0x00AD) continue;

                // Apply text-transform before loading glyph
                uint32_t tt_out[3];
                int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
                codepoint = tt_out[0];
                is_word_start = false;

                for (int tti = 0; tti < tt_count; tti++) {
                uint32_t render_cp = tt_out[tti];
                if (render_cp == 0) continue;

                // Debug: Log the font face being used for this glyph
                static int glyph_debug_count = 0;
                if (glyph_debug_count < 500) {
                    log_debug("[GLYPH DEBUG] loading glyph U+%04X from font '%s' (family=%s) y_ppem=%d css_size=%.2f",
                              codepoint,
                              rdcon->font.font_handle ? font_handle_get_family_name(rdcon->font.font_handle) : "NULL",
                              rdcon->font.style ? rdcon->font.style->family : "NULL",
                              rdcon->font.font_handle ? (int)font_handle_get_physical_size_px(rdcon->font.font_handle) : -1,
                              rdcon->font.style ? rdcon->font.style->font_size : -1.0f);
                    glyph_debug_count++;
                }

                auto t1 = std::chrono::high_resolution_clock::now();
                FontStyleDesc _sd2 = font_style_desc_from_prop(rdcon->font.style);
                // Peek ahead for VS16 (U+FE0F) — forces emoji/color presentation
                bool emoji_presentation = false;
                if (p < end) {
                    uint32_t peek_cp;
                    int peek_bytes = str_utf8_decode((const char*)p, (size_t)(end - p), &peek_cp);
                    if (peek_bytes > 0 && peek_cp == 0xFE0F) {
                        emoji_presentation = true;
                        log_debug("render emoji: VS16 peek hit for U+%04X", render_cp);
                    }
                }
                // Force emoji presentation for codepoints that browsers render as
                // color emoji by default, even without explicit VS16 selector
                if (!emoji_presentation && is_emoji_presentation_default(render_cp)) {
                    emoji_presentation = true;
                }
                LoadedGlyph* glyph = emoji_presentation
                    ? font_load_glyph_emoji(rdcon->font.font_handle, &_sd2, render_cp, true)
                    : font_load_glyph(rdcon->font.font_handle, &_sd2, render_cp, true);
                auto t2 = std::chrono::high_resolution_clock::now();
                g_render_load_glyph_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
                g_render_glyph_count++;
                if (!glyph) {
                    // draw a square box for missing glyph (scaled_space_width is in physical pixels)
                    float phys_size = font_handle_get_physical_size_px(rdcon->font.font_handle);
                    const FontMetrics* _m = font_get_metrics(rdcon->font.font_handle);
                    float box_height = (phys_size > 0) ? phys_size : (_m ? (_m->hhea_line_height * rdcon->scale / 1.2f) : 16.0f);
                    Rect rect = {x + 1, y, (float)(scaled_space_width - 2), box_height};
                    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, 0xFF0000FF, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    x += scaled_space_width;
                }
                else {
                    // draw the glyph to the image buffer — use rendering ascender for glyph bitmap placement.
                    // font_get_rendering_ascender() returns the raw platform ascent (e.g. CoreText ascent on
                    // macOS) WITHOUT half-leading.  The glyph bitmap's bearing_y is measured from this
                    // platform baseline, so we must use the same value here.  Layout uses
                    // fprop->ascender (= init_ascender, which INCLUDES half-leading) for CSS vertical-
                    // align math, but text_rect.y already incorporates lead_y so the absolute baseline
                    // y = text_rect.y + rendering_ascender == init_ascender + lead_y  is correct.
                    float ascend;
                    {
                        auto tfm1 = std::chrono::high_resolution_clock::now();
                        ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                        auto tfm2 = std::chrono::high_resolution_clock::now();
                        g_render_font_metrics_time += std::chrono::duration<double, std::milli>(tfm2 - tfm1).count();
                        g_render_font_metrics_count++;
                    }
                    if (has_selection && char_index <= 15) {
                        log_debug("[SEL-ADVANCE] char_index=%d codepoint=U+%04X '%c' x=%.1f advance=%.1f",
                            char_index, codepoint, (codepoint >= 32 && codepoint < 127) ? (char)codepoint : '?',
                            x, glyph->advance_x);
                    }

                    // Draw selection background BEFORE glyph (so text appears on top)
                    if (is_selected) {
                        float glyph_width = glyph->advance_x;
                        Rect sel_rect = {x, y, glyph_width, text_rect->height * s};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &sel_rect, sel_bg_color, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    }

                    // Debug: Check bitmap data for Monaco (capped to avoid log spam)
                    static int bitmap_debug_count = 0;
                    const char* _dbg_fname = rdcon->font.font_handle ? font_handle_get_family_name(rdcon->font.font_handle) : NULL;
                    if (bitmap_debug_count < 50 && _dbg_fname &&
                        strcmp(_dbg_fname, "Monaco") == 0) {
                        log_debug("[BITMAP DEBUG] Monaco glyph U+%04X: bitmap=%dx%d pitch=%d left=%d top=%d advance=%.1f pixel_mode=%d",
                                  codepoint, glyph->bitmap.width, glyph->bitmap.height,
                                  glyph->bitmap.pitch, glyph->bitmap.bearing_x, glyph->bitmap.bearing_y,
                                  glyph->advance_x, glyph->bitmap.pixel_mode);
                        bitmap_debug_count++;
                    }

                    auto t3 = std::chrono::high_resolution_clock::now();

                    // Render text-shadow glyphs BEFORE the main glyph
                    // Skip if blur pre-pass already rendered shadows
                    if (text_shadow && !shadow_needs_blur) {
                        Color saved_shadow_color = rdcon->color;
                        TextShadow* ts = text_shadow;
                        while (ts) {
                            rdcon->color = ts->color;
                            float sx = x + glyph->bitmap.bearing_x + ts->offset_x * s;
                            float sy = y + ascend - glyph->bitmap.bearing_y + ts->offset_y * s;
                            draw_glyph(rdcon, &glyph->bitmap, lroundf(sx), lroundf(sy));
                            ts = ts->next;
                        }
                        rdcon->color = saved_shadow_color;
                    }

                    draw_glyph(rdcon, &glyph->bitmap, lroundf(x + glyph->bitmap.bearing_x), lroundf(y + ascend - glyph->bitmap.bearing_y));
                    auto t4 = std::chrono::high_resolution_clock::now();
                    g_render_draw_glyph_time += std::chrono::duration<double, std::milli>(t4 - t3).count();
                    g_render_draw_count++;
                    // advance to the next position (include letter-spacing)
                    x += glyph->advance_x + rdcon->font.style->letter_spacing * s;
                }
                } // end for tti (full case mapping expansion)
            }
        }
        // render trailing hyphen for soft-hyphen line break
        if (text_rect->has_trailing_hyphen) {
            FontStyleDesc _sd_h = font_style_desc_from_prop(rdcon->font.style);
            LoadedGlyph* h_glyph = font_load_glyph(rdcon->font.font_handle, &_sd_h, '-', true);
            if (h_glyph) {
                float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                draw_glyph(rdcon, &h_glyph->bitmap, lroundf(x + h_glyph->bitmap.bearing_x), lroundf(y + ascend - h_glyph->bitmap.bearing_y));
                x += h_glyph->advance_x;
            }
        }
        // -webkit-line-clamp: render ellipsis (U+2026) after clamped line
        if (text_rect->has_trailing_ellipsis) {
            FontStyleDesc _sd_e = font_style_desc_from_prop(rdcon->font.style);
            LoadedGlyph* e_glyph = font_load_glyph(rdcon->font.font_handle, &_sd_e, 0x2026, true);
            if (e_glyph) {
                float ascend = font_get_rendering_ascender(rdcon->font.font_handle) * rdcon->scale;
                draw_glyph(rdcon, &e_glyph->bitmap, lroundf(x + e_glyph->bitmap.bearing_x), lroundf(y + ascend - e_glyph->bitmap.bearing_y));
            }
        }
        // render text deco (positions in physical pixels)
        // skip _UNDEF (0) which means no decoration was explicitly set
        if (rdcon->font.style->text_deco != CSS_VALUE_NONE && rdcon->font.style->text_deco != CSS_VALUE__UNDEF) {
            const FontMetrics* _deco_m = font_get_metrics(rdcon->font.font_handle);
            // Use text-decoration-thickness if explicitly set, otherwise font metric or 1px
            float thickness = rdcon->font.style->text_deco_thickness > 0
                ? rdcon->font.style->text_deco_thickness
                : fmaxf(_deco_m ? _deco_m->underline_thickness : 1.0f, 1.0f);
            // Round thickness to nearest pixel (minimum 1px) for crisp rendering
            thickness = fmaxf(roundf(thickness), 1.0f);
            // Use text-decoration-color if set, otherwise currentColor
            Color deco_color = rdcon->font.style->text_deco_color.a > 0
                ? rdcon->font.style->text_deco_color
                : rdcon->color;
            CssEnum deco_style = rdcon->font.style->text_deco_style;
            // Default to solid if not explicitly set
            if (deco_style == CSS_VALUE__UNDEF || deco_style == 0) deco_style = CSS_VALUE_SOLID;

            Rect rect = {0, 0, 0, 0};
            bool draw_deco = true;
            // Use the same ascender as glyph rendering for consistent baseline
            float deco_ascend = font_get_rendering_ascender(rdcon->font.font_handle) * s;
            if (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE) {
                // Use font underline_position metric for correct placement below baseline
                float underline_pos = _deco_m ? _deco_m->underline_position : 0;
                // Apply text-underline-offset if set
                float offset = rdcon->font.style->text_underline_offset;
                rect.x = rdcon->block.x + text_rect->x * s;
                // baseline = text_rect_y + ascender; underline_pos is negative (below baseline)
                rect.y = roundf(rdcon->block.y + text_rect->y * s + deco_ascend - underline_pos * s + offset);
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_OVERLINE) {
                rect.x = rdcon->block.x + text_rect->x * s;
                // overline sits at the ascender line (baseline - ascender = top of text)
                rect.y = floorf(rdcon->block.y + text_rect->y * s);
            }
            else if (rdcon->font.style->text_deco == CSS_VALUE_LINE_THROUGH) {
                rect.x = rdcon->block.x + text_rect->x * s;
                // Line-through at the midpoint of lowercase text (x-height / 2 above baseline),
                // matching browser behavior. Falls back to strikeout metric or 30% of ascender.
                float strike_y;
                if (_deco_m && _deco_m->x_height > 0) {
                    strike_y = _deco_m->x_height * 0.5f;
                } else if (_deco_m && _deco_m->strikeout_position > 0) {
                    strike_y = _deco_m->strikeout_position;
                } else {
                    strike_y = deco_ascend * 0.3f / s;  // convert back from physical for consistency
                }
                rect.y = roundf(rdcon->block.y + text_rect->y * s + deco_ascend - strike_y * s);
                if (_deco_m && _deco_m->strikeout_size > 0)
                    thickness = fmaxf(ceilf(_deco_m->strikeout_size), 1.0f);
            }
            else {
                draw_deco = false;  // unknown decoration type, skip rendering
            }
            if (draw_deco) {
                rect.width = text_rect->width * s;  rect.height = thickness;
                log_debug("text deco: %d style=%d, x:%.1f, y:%.1f, wd:%.1f, hg:%.1f",
                    rdcon->font.style->text_deco, deco_style, rect.x, rect.y, rect.width, rect.height);

                if (deco_style == CSS_VALUE_DASHED) {
                    // Dashed line: dash=3*thickness, gap=3*thickness
                    float dash_len = thickness * 3.0f;
                    float gap_len = thickness * 3.0f;
                    float dx = rect.x;
                    while (dx < rect.x + rect.width) {
                        float seg_w = fminf(dash_len, rect.x + rect.width - dx);
                        Rect seg = {dx, rect.y, seg_w, thickness};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, deco_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                        dx += dash_len + gap_len;
                    }
                } else if (deco_style == CSS_VALUE_DOTTED) {
                    // Dotted line: dot=thickness, gap=thickness
                    float dx = rect.x;
                    while (dx < rect.x + rect.width) {
                        float seg_w = fminf(thickness, rect.x + rect.width - dx);
                        Rect seg = {dx, rect.y, seg_w, thickness};
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &seg, deco_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                        dx += thickness * 2.0f;
                    }
                } else if (deco_style == CSS_VALUE_DOUBLE) {
                    // Double line: two lines separated by a gap
                    // Each line has the same thickness; gap = max(1, thickness - 1)
                    float line_t = thickness;
                    float gap = fmaxf(1.0f, thickness - 1.0f);
                    Rect top_line = {rect.x, rect.y, rect.width, line_t};
                    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &top_line, deco_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    Rect bot_line = {rect.x, rect.y + line_t + gap, rect.width, line_t};
                    rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &bot_line, deco_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                } else if (deco_style == CSS_VALUE_WAVY) {
                    // Wavy line using RdtVector path
                    // Wave centered below the underline position (browser behavior)
                    float wave_amp = thickness * 1.5f;
                    float wave_len = thickness * 4.0f;
                    float wave_center = rect.y + wave_amp;  // shift down so top of wave is at underline pos
                    RdtPath* p = rdt_path_new();
                    float wx = rect.x;
                    rdt_path_move_to(p, wx, wave_center);
                    while (wx < rect.x + rect.width) {
                        float half = fminf(wave_len / 2.0f, rect.x + rect.width - wx);
                        rdt_path_cubic_to(p, wx + half * 0.33f, wave_center - wave_amp,
                                           wx + half * 0.67f, wave_center - wave_amp,
                                           wx + half, wave_center);
                        wx += half;
                        if (wx >= rect.x + rect.width) break;
                        half = fminf(wave_len / 2.0f, rect.x + rect.width - wx);
                        rdt_path_cubic_to(p, wx + half * 0.33f, wave_center + wave_amp,
                                           wx + half * 0.67f, wave_center + wave_amp,
                                           wx + half, wave_center);
                        wx += half;
                    }
                    rc_stroke_path(rdcon, p, deco_color, fmaxf(1.0f, thickness * 0.5f),
                                    RDT_CAP_BUTT, RDT_JOIN_MITER, NULL, 0, NULL);
                    rdt_path_free(p);
                } else {
                    // Solid (default) — apply skip-ink for underline only
                    // (browsers default text-decoration-skip-ink: auto for underline,
                    //  but NOT for line-through or overline)
                    bool apply_skip_ink = (rdcon->font.style->text_deco == CSS_VALUE_UNDERLINE);
                    if (apply_skip_ink) {
                        SkipInkGap gaps[64];
                        int gap_count = collect_skip_ink_gaps(rdcon, str, text_rect,
                                                              rect.y, rect.y + thickness, gaps, 64);
                        draw_deco_with_gaps(rdcon, rect, deco_color.c, gaps, gap_count);
                    } else {
                        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, deco_color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
                    }
                }
            }
        }
        text_rect = text_rect->next;
    }

    // Restore color and font (in case they were changed for PDF text)
    rdcon->font = saved_font;
    rdcon->color = saved_color;
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

/**
 * Render a ViewMarker (list bullet or number) using vector graphics.
 * Bullets (disc, circle, square) are drawn as shapes with fixed width.
 * Text markers (decimal, roman, alpha) render text right-aligned within fixed width.
 */
void render_marker_view(RenderContext* rdcon, ViewSpan* marker) {
    if (!marker || !marker->is_element()) return;

    DomElement* elem = (DomElement*)marker;
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

    // Get current color (inherit from parent text color)
    Color color = rdcon->color;

    log_debug("[MARKER RENDER] type=%d, x=%.1f, y=%.1f, width=%.1f, bullet_size=%.1f",
             marker_type, x, y, width, bullet_size);

    // CSS 2.1 §12.5: list-style-image overrides list-style-type
    if (marker_prop->image_url && strcmp(marker_prop->image_url, "none") != 0) {
        // Lazy-load the image on first render
        if (!marker_prop->loaded_image) {
            marker_prop->loaded_image = load_image(rdcon->ui_context, marker_prop->image_url);
            if (marker_prop->loaded_image && marker_prop->loaded_image->pic) {
                // SVG: set rasterization size based on intrinsic dimensions
                float iw, ih;
                rdt_picture_get_size(marker_prop->loaded_image->pic, &iw, &ih);
                if (iw > 0 && ih > 0) {
                    marker_prop->loaded_image->max_render_width = (int)(iw + 0.5f); // INT_CAST_OK: pixel rounding
                }
                render_svg(marker_prop->loaded_image);
            }
        }
        ImageSurface* img = marker_prop->loaded_image;
        if (img && img->pixels && img->width > 0 && img->height > 0) {
            image_surface_ensure_decoded(img);
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float img_w = (float)img->width;
            float img_h = (float)img->height;
            // Position image: centered at ~1em from content edge, vertically centered in line
            float ix = x + width - font_size - img_w / 2.0f;
            float iy = y + marker->height / 2.0f - img_h / 2.0f;
            rc_draw_image(rdcon, (uint32_t*)img->pixels, img->width, img->height,
                          img->width, ix, iy, img_w, img_h, 255, nullptr);
            log_debug("[MARKER RENDER] Drew list-style-image at (%.1f, %.1f) size %.0fx%.0f",
                     ix, iy, img_w, img_h);
            return;  // image replaces the marker type
        }
        // Fallback: if image failed to load, fall through to marker_type rendering
        log_debug("[MARKER RENDER] list-style-image failed to load, falling back to marker_type");
    }

    switch (marker_type) {
        case CSS_VALUE_DISC: {
            // Filled circle - centered at ~1em from content edge, vertically centered in line
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;
            float radius = bullet_size / 2.0f;

            // Draw filled circle using RdtVector
            RdtPath* p = rdt_path_new();
            rdt_path_add_circle(p, cx, cy, radius, radius);
            rc_fill_path(rdcon, p, color, RDT_FILL_WINDING, NULL);
            rdt_path_free(p);
            log_debug("[MARKER RENDER] Drew disc at (%.1f, %.1f) r=%.1f", cx, cy, radius);
            break;
        }

        case CSS_VALUE_CIRCLE: {
            // Stroked circle (outline only)
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
            // Filled square - centered at ~1em from content edge, vertically centered in line
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
            // Right-pointing triangle ▸ for <summary> elements
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            // right-pointing triangle: left edge at cx, top at cy - tri_size/2, bottom at cy + tri_size/2
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
            // Down-pointing triangle ▾ for open <details> elements
            const FontMetrics* _mk = rdcon->font.font_handle ? font_get_metrics(rdcon->font.font_handle) : NULL;
            float font_size = _mk ? font_handle_get_physical_size_px(rdcon->font.font_handle) : 16.0f;
            float tri_size = bullet_size * 1.6f;
            float cx = x + width - font_size;
            float cy = y + marker->height / 2.0f;

            RdtPath* p = rdt_path_new();
            // down-pointing triangle
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
            // Text markers - render the text_content right-aligned within marker width
            if (marker_prop->text_content && *marker_prop->text_content && rdcon->font.font_handle) {
                float s = rdcon->scale;
                const FontMetrics* _mk = font_get_metrics(rdcon->font.font_handle);
                float ascend = _mk ? (_mk->hhea_ascender * s) : 12.0f;

                // First pass: measure total text width
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

                // Right-align text within marker box: start at (x + width - total_text_width)
                float tx = x + (width * s) - total_text_width;

                // Second pass: render glyphs
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

/**
 * Render vector path (for PDF curves and complex paths)
 * Uses RdtVector to render Bezier curves and path segments
 */
void render_vector_path(RenderContext* rdcon, ViewBlock* block) {
    VectorPathProp* vpath = block->vpath;
    if (!vpath || !vpath->segments) return;

    log_info("[VPATH] Rendering vector path for block at (%.1f, %.1f)", block->x, block->y);

    RdtPath* p = rdt_path_new();
    if (!p) {
        log_error("[VPATH] Failed to create RdtPath");
        return;
    }

    // Build the path from segments
    float offset_x = rdcon->block.x + block->x;
    float offset_y = rdcon->block.y + block->y;

    for (VectorPathSegment* seg = vpath->segments; seg; seg = seg->next) {
        float sx = offset_x + seg->x;
        float sy = offset_y + seg->y;

        switch (seg->type) {
            case VectorPathSegment::VPATH_MOVETO:
                rdt_path_move_to(p, sx, sy);
                log_debug("[VPATH] moveto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_LINETO:
                rdt_path_line_to(p, sx, sy);
                log_debug("[VPATH] lineto (%.1f, %.1f)", sx, sy);
                break;
            case VectorPathSegment::VPATH_CURVETO: {
                float cx1 = offset_x + seg->x1;
                float cy1 = offset_y + seg->y1;
                float cx2 = offset_x + seg->x2;
                float cy2 = offset_y + seg->y2;
                rdt_path_cubic_to(p, cx1, cy1, cx2, cy2, sx, sy);
                log_debug("[VPATH] curveto (%.1f,%.1f)-(%.1f,%.1f)->(%.1f,%.1f)", cx1, cy1, cx2, cy2, sx, sy);
                break;
            }
            case VectorPathSegment::VPATH_CLOSE:
                rdt_path_close(p);
                log_debug("[VPATH] close");
                break;
        }
    }

    // Apply stroke if present
    if (vpath->has_stroke) {
        rc_stroke_path(rdcon, p, vpath->stroke_color, vpath->stroke_width,
                        RDT_CAP_BUTT, RDT_JOIN_MITER,
                        vpath->dash_pattern, vpath->dash_pattern_length > 0 ? vpath->dash_pattern_length : 0,
                        NULL);

        if (vpath->dash_pattern && vpath->dash_pattern_length > 0) {
            log_debug("[VPATH] Setting dash pattern: count=%d, values=[%.1f, %.1f]",
                     vpath->dash_pattern_length,
                     vpath->dash_pattern[0],
                     vpath->dash_pattern_length > 1 ? vpath->dash_pattern[1] : 0.0f);
        }

        log_debug("[VPATH] Stroke: RGB(%d,%d,%d) width=%.1f",
                 vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b, vpath->stroke_width);
    }

    // Apply fill if present
    if (vpath->has_fill) {
        rc_fill_path(rdcon, p, vpath->fill_color, RDT_FILL_WINDING, NULL);
    }

    rdt_path_free(p);
    log_info("[VPATH] Rendered vector path successfully");
}

void render_list_bullet(RenderContext* rdcon, ViewBlock* list_item) {
    // bullets are aligned to the top and right side of the list item
    float ratio = rdcon->ui_context->pixel_ratio;
    if (rdcon->list.list_style_type == CSS_VALUE_DISC) {
        Rect rect;
        rect.x = rdcon->block.x + list_item->x - 15 * ratio;
        rect.y = rdcon->block.y + list_item->y + 7 * ratio;
        rect.width = rect.height = 5 * ratio;
        rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &rect, rdcon->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
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
        // float font_size = rdcon->font.current_font_size;
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

/**
 * Render column rules for multi-column containers
 * Column rules are drawn as vertical lines between columns
 */
void render_column_rules(RenderContext* rdcon, ViewBlock* block) {
    if (!block->multicol) return;

    MultiColumnProp* mc = block->multicol;

    // Only render if we have rules and multiple columns
    if (mc->computed_column_count <= 1 || mc->rule_width <= 0 ||
        mc->rule_style == CSS_VALUE_NONE) {
        return;
    }

    float column_width = mc->computed_column_width;
    float gap = mc->column_gap_is_normal ? 16.0f : mc->column_gap;

    // Calculate block position (rdcon->block already includes block->x/y)
    float block_x = rdcon->block.x;
    float block_y = rdcon->block.y;

    // Adjust for padding
    if (block->bound) {
        block_x += block->bound->padding.left;
        block_y += block->bound->padding.top;
    }

    // Rule height is the content area height (block height minus padding/border)
    // For multi-column containers, get the actual content height from block->height
    float rule_height = block->height;
    if (block->bound) {
        // Subtract padding from top (already offset block_y, so don't need to subtract again)
        // but we need to subtract total padding for height calculation
        rule_height -= block->bound->padding.top + block->bound->padding.bottom;
        if (block->bound->border) {
            rule_height -= block->bound->border->width.top + block->bound->border->width.bottom;
        }
    }

    // Ensure minimum rule height
    if (rule_height <= 0) {
        // Fall back to using the block's content height by iterating children
        View* child = (View*)block->first_child;
        float max_bottom = 0;
        while (child) {
            if (child->is_element()) {
                ViewBlock* child_block = (ViewBlock*)child;
                float child_bottom = child_block->y + child_block->height;
                if (child_bottom > max_bottom) max_bottom = child_bottom;
            }
            child = child->next();
        }
        rule_height = max_bottom;
        log_debug("[MULTICOL] Rule height computed from children: %.1f", rule_height);
    }

    log_debug("[MULTICOL] Rendering %d column rules, width=%.1f, style=%d",
              mc->computed_column_count - 1, mc->rule_width, mc->rule_style);

    // Draw rule between each pair of columns
    for (int i = 0; i < mc->computed_column_count - 1; i++) {
        float rule_x = block_x + (i + 1) * column_width + i * gap + gap / 2.0f - mc->rule_width / 2.0f;

        if (mc->rule_style == CSS_VALUE_DOUBLE) {
            // Double: two thin filled rectangles
            float thin_width = mc->rule_width / 3.0f;
            rc_fill_rect(rdcon, rule_x - thin_width, block_y, thin_width, rule_height, mc->rule_color);
            rc_fill_rect(rdcon, rule_x + thin_width, block_y, thin_width, rule_height, mc->rule_color);
        } else {
            // Solid, dotted, dashed: draw as vertical line
            RdtPath* p = rdt_path_new();
            rdt_path_move_to(p, rule_x, block_y);
            rdt_path_line_to(p, rule_x, block_y + rule_height);

            float* dash = NULL;
            int dash_count = 0;
            float dash_pattern[2];
            if (mc->rule_style == CSS_VALUE_DOTTED) {
                dash_pattern[0] = mc->rule_width;
                dash_pattern[1] = mc->rule_width * 2;
                dash = dash_pattern;
                dash_count = 2;
            } else if (mc->rule_style == CSS_VALUE_DASHED) {
                dash_pattern[0] = mc->rule_width * 3;
                dash_pattern[1] = mc->rule_width * 2;
                dash = dash_pattern;
                dash_count = 2;
            }

            rc_stroke_path(rdcon, p, mc->rule_color, mc->rule_width,
                            RDT_CAP_BUTT, RDT_JOIN_MITER, dash, dash_count, NULL);
            rdt_path_free(p);
        }

        log_debug("[MULTICOL] Rule %d at x=%.1f, height=%.1f", i, rule_x, rule_height);
    }
}

// Helper function to render linear gradient
void render_bound(RenderContext* rdcon, ViewBlock* view) {
    float s = rdcon->scale;
    Rect rect;
    rect.x = rdcon->block.x + view->x * s;  rect.y = rdcon->block.y + view->y * s;
    rect.width = view->width * s;  rect.height = view->height * s;

    // Resolve percentage border-radius values against element's own dimensions (in CSS px)
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }

    // Render box-shadow BEFORE background (shadows go underneath the element)
    if (view->bound->box_shadow) {
        render_box_shadow(rdcon, view, rect);
    }

    // Render background (gradient, solid color, and background-image) using new rendering system
    if (view->bound->background) {
        render_background(rdcon, view, rect);
    }

    // Render inset box-shadow AFTER background (inside the element)
    if (view->bound->box_shadow) {
        render_box_shadow_inset(rdcon, view, rect);
    }

    // Render borders using new rendering system
    if (view->bound->border) {
        log_debug("render border");

        // CSS 2.1 §17.6.2: Use resolved borders for border-collapse cells
        bool use_resolved = false;
        CollapsedBorder* resolved_top = nullptr;
        CollapsedBorder* resolved_right = nullptr;
        CollapsedBorder* resolved_bottom = nullptr;
        CollapsedBorder* resolved_left = nullptr;

        if (view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewTableCell* cell = (ViewTableCell*)view;
            if (cell->td && cell->td->top_resolved) {
                use_resolved = true;
                resolved_top = cell->td->top_resolved;
                resolved_right = cell->td->right_resolved;
                resolved_bottom = cell->td->bottom_resolved;
                resolved_left = cell->td->left_resolved;
            }
        }

        if (use_resolved) {
            // Render collapsed borders using resolved border data (table cells)
            // CSS 2.1 §17.6.2: Collapsed borders are centered on the cell edges.
            // Since cells are positioned half-border inward, we must shift borders
            // outward by half their width to center them on the cell edge.
            if (resolved_left && resolved_left->style != CSS_VALUE_NONE && resolved_left->color.a) {
                float bw = resolved_left->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_left->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_right && resolved_right->style != CSS_VALUE_NONE && resolved_right->color.a) {
                float bw = resolved_right->width * s;
                Rect border_rect = rect;
                border_rect.x = rect.x + rect.width - bw / 2.0f;
                border_rect.width = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_right->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_top && resolved_top->style != CSS_VALUE_NONE && resolved_top->color.a) {
                float bw = resolved_top->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_top->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
            if (resolved_bottom && resolved_bottom->style != CSS_VALUE_NONE && resolved_bottom->color.a) {
                float bw = resolved_bottom->width * s;
                Rect border_rect = rect;
                border_rect.y = rect.y + rect.height - bw / 2.0f;
                border_rect.height = bw;
                rc_fill_surface_rect(rdcon, rdcon->ui_context->surface, &border_rect, resolved_bottom->color.c, &rdcon->block.clip, rdcon->clip_shapes, rdcon->clip_shape_depth);
            }
        } else {
            // Use new comprehensive border rendering
            render_border(rdcon, view, rect);
        }
    }

    // Note: outline is NOT rendered here. Per CSS spec (Appendix E),
    // outlines paint after all backgrounds/borders/shadows in the stacking context.
    // Outlines are rendered in a second pass by the parent's render loop.
}

/**
 * Render outline for a view block (called in a deferred pass after all siblings).
 */
void render_outline_deferred(RenderContext* rdcon, ViewBlock* view) {
    if (!view->bound || !view->bound->outline) return;
    float s = rdcon->scale;
    BlockBlot saved = rdcon->block;
    // Compute absolute position for this view
    rdcon->block.x = saved.x + view->x * s;
    rdcon->block.y = saved.y + view->y * s;
    Rect rect;
    rect.x = rdcon->block.x;  rect.y = rdcon->block.y;
    rect.width = view->width * s;  rect.height = view->height * s;
    if (view->bound->border) {
        resolve_border_radius_percentages(&view->bound->border->radius, view->width, view->height);
    }
    render_outline(rdcon, view, rect);
    rdcon->block = saved;
}

void draw_debug_rect(RenderContext* rdcon, Rect rect, Bound* clip) {
    RdtPath* p = rdt_path_new();
    rdt_path_move_to(p, rect.x, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y);
    rdt_path_line_to(p, rect.x + rect.width, rect.y + rect.height);
    rdt_path_line_to(p, rect.x, rect.y + rect.height);
    rdt_path_close(p);

    // dash pattern for dotted line
    float dash_pattern[2] = {8.0f, 8.0f};
    Color debug_color;
    debug_color.r = 255; debug_color.g = 0; debug_color.b = 0; debug_color.a = 100;

    // clip region
    RdtPath* clip_p = rdt_path_new();
    rdt_path_add_rect(clip_p, clip->left, clip->top,
                      clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    rc_push_clip(rdcon, clip_p, NULL);

    rc_stroke_path(rdcon, p, debug_color, 2.0f, RDT_CAP_BUTT, RDT_JOIN_MITER, dash_pattern, 2, NULL);

    rc_pop_clip(rdcon);
    rdt_path_free(clip_p);
    rdt_path_free(p);
}

void setup_scroller(RenderContext* rdcon, ViewBlock* block) {
    float s = rdcon->scale;
    if (block->scroller->has_clip) {
        // Inset clip by border widths for padding-box clipping (CSS spec: overflow clips to padding edge)
        float bl = 0, bt = 0, br = 0, bb = 0;
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            bl = border->width.left;
            bt = border->width.top;
            br = border->width.right;
            bb = border->width.bottom;
        }
        log_debug("setup scroller clip: left:%f, top:%f, right:%f, bottom:%f",
            block->scroller->clip.left, block->scroller->clip.top, block->scroller->clip.right, block->scroller->clip.bottom);
        rdcon->block.clip.left = max(rdcon->block.clip.left, rdcon->block.x + (block->scroller->clip.left + bl) * s);
        rdcon->block.clip.top = max(rdcon->block.clip.top, rdcon->block.y + (block->scroller->clip.top + bt) * s);
        rdcon->block.clip.right = min(rdcon->block.clip.right, rdcon->block.x + (block->scroller->clip.right - br) * s);
        rdcon->block.clip.bottom = min(rdcon->block.clip.bottom, rdcon->block.y + (block->scroller->clip.bottom - bb) * s);

        // Copy border-radius for rounded corner clipping when overflow:hidden (scale radius)
        if (block->bound && block->bound->border) {
            BorderProp* border = block->bound->border;
            // resolve percentage border-radius if not yet resolved
            resolve_border_radius_percentages(&border->radius, block->width, block->height);
            if (corner_has_radius(&border->radius)) {
                rdcon->block.has_clip_radius = true;
                // Use inner radius (outer minus border width) for padding-box clipping
                rdcon->block.clip_radius.top_left = fmaxf(0, border->radius.top_left - bl) * s;
                rdcon->block.clip_radius.top_right = fmaxf(0, border->radius.top_right - br) * s;
                rdcon->block.clip_radius.bottom_left = fmaxf(0, border->radius.bottom_left - bl) * s;
                rdcon->block.clip_radius.bottom_right = fmaxf(0, border->radius.bottom_right - br) * s;
                rdcon->block.clip_radius.top_left_y = fmaxf(0, border->radius.top_left_y - bt) * s;
                rdcon->block.clip_radius.top_right_y = fmaxf(0, border->radius.top_right_y - bt) * s;
                rdcon->block.clip_radius.bottom_left_y = fmaxf(0, border->radius.bottom_left_y - bb) * s;
                rdcon->block.clip_radius.bottom_right_y = fmaxf(0, border->radius.bottom_right_y - bb) * s;
                constrain_corner_radii(&rdcon->block.clip_radius,
                    rdcon->block.clip.right - rdcon->block.clip.left,
                    rdcon->block.clip.bottom - rdcon->block.clip.top);
                log_debug("setup rounded clip: tl=%f, tr=%f, bl=%f, br=%f",
                    rdcon->block.clip_radius.top_left, rdcon->block.clip_radius.top_right,
                    rdcon->block.clip_radius.bottom_left, rdcon->block.clip_radius.bottom_right);
            }
        }
    }
    if (block->scroller->pane) {
        rdcon->block.x -= block->scroller->pane->h_scroll_position * s;
        rdcon->block.y -= block->scroller->pane->v_scroll_position * s;
    }
}

void render_scroller(RenderContext* rdcon, ViewBlock* block, BlockBlot* pa_block) {
    log_debug("render scrollbars");
    // need to reset block.x and y, which was changed by the scroller
    float s = rdcon->scale;
    rdcon->block.x = pa_block->x + block->x * s;  rdcon->block.y = pa_block->y + block->y * s;
    if (block->scroller->has_hz_scroll || block->scroller->has_vt_scroll) {
        Rect rect = {rdcon->block.x, rdcon->block.y, block->width * s, block->height * s};
        if (block->bound && block->bound->border) {
            rect.x += block->bound->border->width.left * s;
            rect.y += block->bound->border->width.top * s;
            rect.width -= (block->bound->border->width.left + block->bound->border->width.right) * s;
            rect.height -= (block->bound->border->width.top + block->bound->border->width.bottom) * s;
        }
        if (block->scroller->pane) {
            scrollpane_render(&rdcon->vec, block->scroller->pane, &rect,
                block->content_width * s, block->content_height * s, &rdcon->block.clip, s,
                block->scroller->has_hz_scroll, block->scroller->has_vt_scroll);
        } else {
            log_error("scroller has no scroll pane");
        }
    }
}

void render_block_view(RenderContext* rdcon, ViewBlock* block) {
    auto rbv_start = std::chrono::high_resolution_clock::now();
    double children_time = 0; // will accumulate time in child render calls
    g_render_block_count++;
    // Phase 18: Early exit if block is entirely outside the dirty union bbox.
    // Must use the union bbox (not individual rects) because the display list
    // replay clips to the union — parent backgrounds paint the whole union,
    // so all blocks within it must be re-rendered.
    if (rdcon->has_dirty_union) {
        float s = rdcon->scale > 0 ? rdcon->scale : 1.0f;
        float abs_x = rdcon->block.x / s + block->x;
        float abs_y = rdcon->block.y / s + block->y;
        float bw = block->width;
        float bh = block->height;
        // add margin to avoid clipping positioned descendants that overflow
        float margin = 50.0f;
        abs_x -= margin; abs_y -= margin; bw += margin * 2; bh += margin * 2;
        Bound* du = &rdcon->dirty_union;
        bool intersects = (abs_x < du->right && abs_x + bw > du->left &&
                           abs_y < du->bottom && abs_y + bh > du->top);
        if (!intersects) return;
    }

    log_debug("render block view:%s, clip:[%.0f,%.0f,%.0f,%.0f]", block->node_name(),
        rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
    log_enter();
    BlockBlot pa_block = rdcon->block;  FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;

    // CSS 2.1 §11.2: visibility:hidden — suppress own rendering but still render children
    // (children with visibility:visible should still appear)
    bool self_hidden = block->in_line && block->in_line->visibility == VIS_HIDDEN;

    // Save transform state and apply element's transform
    RdtMatrix pa_transform = rdcon->transform;
    bool pa_has_transform = rdcon->has_transform;

    if (block->transform && block->transform->functions) {
        // Calculate transform origin
        float origin_x = block->transform->origin_x_percent
            ? (block->transform->origin_x / 100.0f) * block->width
            : block->transform->origin_x;
        float origin_y = block->transform->origin_y_percent
            ? (block->transform->origin_y / 100.0f) * block->height
            : block->transform->origin_y;

        // Origin is relative to element's position in parent
        float elem_x = pa_block.x + block->x;
        float elem_y = pa_block.y + block->y;
        origin_x += elem_x;
        origin_y += elem_y;

        // Compute new transform matrix
        RdtMatrix new_transform = radiant::compute_transform_matrix(
            block->transform->functions, block->width, block->height, origin_x, origin_y);

        // If parent has transform, concatenate
        if (rdcon->has_transform) {
            // Matrix multiply: new = parent * element
            RdtMatrix combined = {
                pa_transform.e11 * new_transform.e11 + pa_transform.e12 * new_transform.e21 + pa_transform.e13 * new_transform.e31,
                pa_transform.e11 * new_transform.e12 + pa_transform.e12 * new_transform.e22 + pa_transform.e13 * new_transform.e32,
                pa_transform.e11 * new_transform.e13 + pa_transform.e12 * new_transform.e23 + pa_transform.e13 * new_transform.e33,
                pa_transform.e21 * new_transform.e11 + pa_transform.e22 * new_transform.e21 + pa_transform.e23 * new_transform.e31,
                pa_transform.e21 * new_transform.e12 + pa_transform.e22 * new_transform.e22 + pa_transform.e23 * new_transform.e32,
                pa_transform.e21 * new_transform.e13 + pa_transform.e22 * new_transform.e23 + pa_transform.e23 * new_transform.e33,
                pa_transform.e31 * new_transform.e11 + pa_transform.e32 * new_transform.e21 + pa_transform.e33 * new_transform.e31,
                pa_transform.e31 * new_transform.e12 + pa_transform.e32 * new_transform.e22 + pa_transform.e33 * new_transform.e32,
                pa_transform.e31 * new_transform.e13 + pa_transform.e32 * new_transform.e23 + pa_transform.e33 * new_transform.e33
            };
            rdcon->transform = combined;
        } else {
            rdcon->transform = new_transform;
        }
        rdcon->has_transform = true;

        log_debug("[TRANSFORM] Element %s: transform active, origin=(%.1f,%.1f)",
            block->node_name(), origin_x, origin_y);
    }

    if (block->font) {
        auto t1 = std::chrono::high_resolution_clock::now();
        setup_font(rdcon->ui_context, &rdcon->font, block->font);
        auto t2 = std::chrono::high_resolution_clock::now();
        g_render_setup_font_time += std::chrono::duration<double, std::milli>(t2 - t1).count();
        g_render_setup_font_count++;
    }
    // render bullet after setting the font, as bullet is rendered using the same font as the list item
    // Skip legacy render_list_bullet when a ::marker pseudo-element exists,
    // since render_marker_view will handle it during child traversal.
    if (!self_hidden && block->view_type == RDT_VIEW_LIST_ITEM) {
        DomElement* li_elem = (DomElement*)block;
        if (!li_elem->pseudo || !li_elem->pseudo->marker) {
            render_list_bullet(rdcon, block);
        }
    }
    // Push CSS clip-path if specified on this element (clips backgrounds, borders, content, children)
    ClipShape* css_clip_shape = nullptr;
    bool has_css_clip = false;
    {
        CssDeclaration* clip_decl = dom_element_get_specified_value((DomElement*)block, CSS_PROPERTY_CLIP_PATH);
        if (clip_decl && clip_decl->value_text && clip_decl->value_text_len > 0) {
            const char* clip_str = clip_decl->value_text;
            if (clip_str && strncmp(clip_str, "none", 4) != 0) {
                float s = rdcon->scale;
                float elem_w = block->width * s;
                float elem_h = block->height * s;
                float abs_x = pa_block.x + block->x * s;
                float abs_y = pa_block.y + block->y * s;
                css_clip_shape = parse_css_clip_shape(&rdcon->scratch, clip_str, elem_w, elem_h, abs_x, abs_y);
                if (css_clip_shape) {
                    // Push ThorVG clip path for vector operations
                    RdtPath* clip_path = create_clip_shape_path(css_clip_shape);
                    if (clip_path) {
                        rc_push_clip(rdcon, clip_path, nullptr);
                        rdt_path_free(clip_path);
                    }
                    // Push clip shape for direct-pixel operations
                    if (rdcon->clip_shape_depth < RDT_MAX_CLIP_SHAPES) {
                        rdcon->clip_shapes[rdcon->clip_shape_depth++] = css_clip_shape;
                    }
                    has_css_clip = true;
                    log_debug("[CLIP] CSS clip-path: %s on element %s", clip_str, block->node_name());
                }
            }
        }
    }

    // Save backdrop for mix-blend-mode before rendering this element
    CssEnum mix_blend = (block->in_line && block->in_line->mix_blend_mode &&
                         block->in_line->mix_blend_mode != CSS_VALUE_NORMAL)
                        ? block->in_line->mix_blend_mode : (CssEnum)0;
    uint32_t* mix_blend_backdrop = nullptr;
    int mbx0 = 0, mby0 = 0, mbw = 0, mbh = 0;
    if (mix_blend) {
        float ms = rdcon->scale;
        ImageSurface* surface = rdcon->ui_context->surface;
        if (surface && surface->pixels) {
            mbx0 = (int)(pa_block.x + block->x * ms);
            mby0 = (int)(pa_block.y + block->y * ms);
            int mx1 = mbx0 + (int)(block->width * ms);
            int my1 = mby0 + (int)(block->height * ms);
            if (mbx0 < 0) mbx0 = 0;
            if (mby0 < 0) mby0 = 0;
            if (mx1 > surface->width) mx1 = surface->width;
            if (my1 > surface->height) my1 = surface->height;
            mbw = mx1 - mbx0;
            mbh = my1 - mby0;
            if (mbw > 0 && mbh > 0) {
                if (rdcon->dl) {
                    // record save/apply pair — replay handles pixel ops
                    dl_save_backdrop(rdcon->dl, mbx0, mby0, mbw, mbh);
                } else {
                    mix_blend_backdrop = (uint32_t*)scratch_alloc(&rdcon->scratch, (size_t)mbw * mbh * sizeof(uint32_t));
                    if (mix_blend_backdrop) {
                        uint32_t* px = (uint32_t*)surface->pixels;
                        int pitch = surface->pitch / 4;
                        for (int row = 0; row < mbh; row++) {
                            memcpy(mix_blend_backdrop + row * mbw,
                                   px + (mby0 + row) * pitch + mbx0,
                                   mbw * sizeof(uint32_t));
                        }
                        // Clear region to transparent so element renders on clean background
                        for (int row = 0; row < mbh; row++) {
                            memset(px + (mby0 + row) * pitch + mbx0, 0, mbw * sizeof(uint32_t));
                        }
                        log_debug("[MIX-BLEND] Saved backdrop %dx%d for <%s>", mbw, mbh, block->node_name());
                    }
                }
            }
        }
    }

    // Save backdrop for opacity stacking context (must nest inside mix-blend save)
    bool has_opacity_group = (block->in_line && block->in_line->opacity < 1.0f && block->in_line->opacity >= 0.0f);
    uint32_t* opacity_backdrop = nullptr;
    int opx0 = 0, opy0 = 0, opw = 0, oph = 0;
    if (has_opacity_group) {
        float os = rdcon->scale;
        ImageSurface* surface = rdcon->ui_context->surface;
        if (surface && surface->pixels) {
            opx0 = (int)(pa_block.x + block->x * os);
            opy0 = (int)(pa_block.y + block->y * os);
            int ox1 = opx0 + (int)(block->width * os);
            int oy1 = opy0 + (int)(block->height * os);
            if (opx0 < 0) opx0 = 0;
            if (opy0 < 0) opy0 = 0;
            if (ox1 > surface->width) ox1 = surface->width;
            if (oy1 > surface->height) oy1 = surface->height;
            opw = ox1 - opx0;
            oph = oy1 - opy0;
            if (opw > 0 && oph > 0) {
                if (rdcon->dl) {
                    dl_save_backdrop(rdcon->dl, opx0, opy0, opw, oph);
                } else {
                    opacity_backdrop = (uint32_t*)scratch_alloc(&rdcon->scratch, (size_t)opw * oph * sizeof(uint32_t));
                    if (opacity_backdrop) {
                        uint32_t* px = (uint32_t*)surface->pixels;
                        int pitch = surface->pitch / 4;
                        for (int row = 0; row < oph; row++) {
                            memcpy(opacity_backdrop + row * opw,
                                   px + (opy0 + row) * pitch + opx0,
                                   opw * sizeof(uint32_t));
                        }
                        // clear region so element renders on transparent background
                        for (int row = 0; row < oph; row++) {
                            memset(px + (opy0 + row) * pitch + opx0, 0, opw * sizeof(uint32_t));
                        }
                        log_debug("[OPACITY] Saved backdrop %dx%d for <%s>", opw, oph, block->node_name());
                    }
                }
            }
        }
    }

    // Save backdrop for filter: drop-shadow() (element must render on transparent so we get true alpha)
    bool has_drop_shadow = false;
    bool has_filter_backdrop = false;
    uint32_t* ds_backdrop = nullptr;
    int dsx0 = 0, dsy0 = 0, dsw = 0, dsh = 0;
    float ds_offset_x = 0, ds_offset_y = 0, ds_blur = 0;
    float filter_blur_max = 0;
    if (block->filter && block->filter->functions) {
        FilterFunction* ff = block->filter->functions;
        while (ff) {
            if (ff->type == FILTER_DROP_SHADOW) {
                has_drop_shadow = true;
                ds_offset_x = ff->params.drop_shadow.offset_x;
                ds_offset_y = ff->params.drop_shadow.offset_y;
                ds_blur = ff->params.drop_shadow.blur_radius;
            } else if (ff->type == FILTER_BLUR) {
                if (ff->params.blur_radius > filter_blur_max)
                    filter_blur_max = ff->params.blur_radius;
            }
            ff = ff->next;
        }
        // Any filter chain creates a stacking context: render element to a
        // transparent backdrop so filters (esp. opacity()) composite correctly
        // onto the underlying surface instead of punching alpha holes.
        has_filter_backdrop = true;
    }
    if (has_filter_backdrop) {
        float ds = rdcon->scale;
        ImageSurface* surface = rdcon->ui_context->surface;
        if (surface && surface->pixels) {
            // Expand region to cover element + drop-shadow offset/blur + filter blur
            float ds_expand = has_drop_shadow ?
                (ceilf(fabs(ds_offset_x > 0 ? ds_offset_x : -ds_offset_x)) +
                 ceilf(fabs(ds_offset_y > 0 ? ds_offset_y : -ds_offset_y)) +
                 ceilf(ds_blur) + 2) : 0;
            float blur_expand = ceilf(filter_blur_max);
            float expand = ds_expand > blur_expand ? ds_expand : blur_expand;
            dsx0 = (int)(pa_block.x + block->x * ds - expand);
            dsy0 = (int)(pa_block.y + block->y * ds - expand);
            int dx1 = (int)(pa_block.x + block->x * ds + block->width * ds + expand);
            int dy1 = (int)(pa_block.y + block->y * ds + block->height * ds + expand);
            if (dsx0 < 0) dsx0 = 0;
            if (dsy0 < 0) dsy0 = 0;
            if (dx1 > surface->width) dx1 = surface->width;
            if (dy1 > surface->height) dy1 = surface->height;
            dsw = dx1 - dsx0;
            dsh = dy1 - dsy0;
            if (dsw > 0 && dsh > 0) {
                if (rdcon->dl) {
                    dl_save_backdrop(rdcon->dl, dsx0, dsy0, dsw, dsh);
                } else {
                    ds_backdrop = (uint32_t*)scratch_alloc(&rdcon->scratch, (size_t)dsw * dsh * sizeof(uint32_t));
                    if (ds_backdrop) {
                        uint32_t* px = (uint32_t*)surface->pixels;
                        int pitch = surface->pitch / 4;
                        for (int row = 0; row < dsh; row++) {
                            memcpy(ds_backdrop + row * dsw,
                                   px + (dsy0 + row) * pitch + dsx0,
                                   dsw * sizeof(uint32_t));
                        }
                        for (int row = 0; row < dsh; row++) {
                            memset(px + (dsy0 + row) * pitch + dsx0, 0, dsw * sizeof(uint32_t));
                        }
                        log_debug("[DROP-SHADOW] Saved backdrop %dx%d for <%s>", dsw, dsh, block->node_name());
                    }
                }
            }
        }
    }

    if (!self_hidden && block->bound) {
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
            auto tb1 = std::chrono::high_resolution_clock::now();
            render_bound(rdcon, block);
            auto tb2 = std::chrono::high_resolution_clock::now();
            g_render_bound_time += std::chrono::duration<double, std::milli>(tb2 - tb1).count();
            g_render_bound_count++;
        }
    }

    // Render vector path if present (for PDF curves and complex paths)
    if (block->vpath && block->vpath->segments) {
        render_vector_path(rdcon, block);
    }

    // Propagate position with scale applied (CSS logical pixels -> physical surface pixels)
    float s = rdcon->scale;
    rdcon->block.x = pa_block.x + block->x * s;  rdcon->block.y = pa_block.y + block->y * s;
    if (DEBUG_RENDER) {  // debugging outline around the block margin border
        Rect rc;
        rc.x = rdcon->block.x - (block->bound ? block->bound->margin.left * s : 0);
        rc.y = rdcon->block.y - (block->bound ? block->bound->margin.top * s : 0);
        rc.width = block->width * s + (block->bound ? (block->bound->margin.left + block->bound->margin.right) * s : 0);
        rc.height = block->height * s + (block->bound ? (block->bound->margin.top + block->bound->margin.bottom) * s : 0);
        draw_debug_rect(rdcon, rc, &rdcon->block.clip);
    }

    View* view = block->first_child;
    auto rc_start = std::chrono::high_resolution_clock::now();
    if (view) {
        if (block->in_line && block->in_line->color.c) {
            log_debug("[RENDER COLOR] element=%s setting color: #%02x%02x%02x (was #%02x%02x%02x) color.c=0x%08x",
                      block->node_name(),
                      block->in_line->color.r, block->in_line->color.g, block->in_line->color.b,
                      rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line->color.c);
            rdcon->color = block->in_line->color;
        } else {
            log_debug("[RENDER COLOR] element=%s inheriting color #%02x%02x%02x (in_line=%p, color.c=%u)",
                      block->node_name(), rdcon->color.r, rdcon->color.g, rdcon->color.b,
                      block->in_line, block->in_line ? block->in_line->color.c : 0);
        }
        // setup clip box
        if (block->scroller) {
            setup_scroller(rdcon, block);
        }

        // Vector clip for overflow with border-radius
        ClipShape overflow_cs = {};
        bool has_overflow_clip = false;
        if (rdcon->block.has_clip_radius) {
            Bound* clip = &rdcon->block.clip;
            Corner* cr = &rdcon->block.clip_radius;
            float cw = clip->right - clip->left;
            float ch = clip->bottom - clip->top;
            if (cw > 0 && ch > 0) {
                // Push ThorVG clip path for vector operations
                RdtPath* clip_path = create_per_corner_rounded_rect_path(
                    clip->left, clip->top, cw, ch,
                    cr->top_left, cr->top_right, cr->bottom_right, cr->bottom_left);
                rc_push_clip(rdcon, clip_path, nullptr);
                rdt_path_free(clip_path);

                // Push clip shape for direct-pixel operations
                overflow_cs.type = CLIP_SHAPE_ROUNDED_RECT;
                overflow_cs.rounded_rect = {clip->left, clip->top, cw, ch,
                    cr->top_left, cr->top_right, cr->bottom_right, cr->bottom_left};
                if (rdcon->clip_shape_depth < RDT_MAX_CLIP_SHAPES) {
                    rdcon->clip_shapes[rdcon->clip_shape_depth++] = &overflow_cs;
                }
                has_overflow_clip = true;
                // Clear the flag so child elements don't redundantly push the same clip.
                // The clip is already active in ThorVG and clip_shapes stacks.
                rdcon->block.has_clip_radius = false;
                log_debug("[CLIP] pushed overflow vector clip: (%.0f,%.0f) %.0fx%.0f r=[%.0f,%.0f,%.0f,%.0f]",
                    clip->left, clip->top, cw, ch,
                    cr->top_left, cr->top_right, cr->bottom_right, cr->bottom_left);
            }
        }

        // render negative z-index children
        render_children(rdcon, view);
        // render positive z-index children (sorted by z-index)
        if (block->position) {
            log_debug("render absolute/fixed positioned children");
            // collect positioned children into array for z-index sorting
            ViewBlock* abs_children[256];
            int abs_count = 0;
            ViewBlock* child_block = block->position->first_abs_child;
            while (child_block && abs_count < 256) {
                abs_children[abs_count++] = child_block;
                child_block = child_block->position->next_abs_sibling;
            }
            // sort by z-index (stable: preserve document order for equal z-index)
            for (int i = 1; i < abs_count; i++) {
                ViewBlock* key = abs_children[i];
                int key_z = key->position ? key->position->z_index : 0;
                int j = i - 1;
                while (j >= 0) {
                    int j_z = abs_children[j]->position ? abs_children[j]->position->z_index : 0;
                    if (j_z > key_z) {
                        abs_children[j + 1] = abs_children[j];
                        j--;
                    } else {
                        break;
                    }
                }
                abs_children[j + 1] = key;
            }
            for (int i = 0; i < abs_count; i++) {
                render_block_view(rdcon, abs_children[i]);
            }
        }

        // Pop overflow vector clip
        if (has_overflow_clip) {
            auto toc1 = std::chrono::high_resolution_clock::now();
            rc_pop_clip(rdcon);
            if (rdcon->clip_shape_depth > 0) rdcon->clip_shape_depth--;
            auto toc2 = std::chrono::high_resolution_clock::now();
            g_render_overflow_clip_time += std::chrono::duration<double, std::milli>(toc2 - toc1).count();
            g_render_overflow_clip_count++;
        }

        // Deferred outline pass: CSS spec says outlines paint after all
        // backgrounds/borders/shadows in the stacking context. Walk direct
        // children and render their outlines on top of everything.
        View* outline_view = block->first_child;
        while (outline_view) {
            if ((outline_view->view_type == RDT_VIEW_BLOCK ||
                 outline_view->view_type == RDT_VIEW_INLINE_BLOCK) &&
                ((ViewBlock*)outline_view)->bound &&
                ((ViewBlock*)outline_view)->bound->outline) {
                render_outline_deferred(rdcon, (ViewBlock*)outline_view);
            }
            outline_view = (View*)outline_view->next_sibling;
        }
    }
    else {
        log_debug("view has no child");
    }
    auto rc_end = std::chrono::high_resolution_clock::now();
    children_time = std::chrono::duration<double, std::milli>(rc_end - rc_start).count();

    // render scrollbars
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }

    // Render multi-column rules between columns
    if (block->multicol && block->multicol->computed_column_count > 1) {
        render_column_rules(rdcon, block);
    }

    // Apply CSS filters after all content is rendered
    // Filters are applied to the rendered pixel data in the element's region
    if (block->filter && block->filter->functions) {
        auto tf1 = std::chrono::high_resolution_clock::now();

        // Calculate the element's bounding rect, expanded for filter effects
        float filter_expand = 0;
        FilterFunction* ff = block->filter->functions;
        while (ff) {
            if (ff->type == FILTER_BLUR && ff->params.blur_radius > filter_expand)
                filter_expand = ff->params.blur_radius;
            if (ff->type == FILTER_DROP_SHADOW) {
                float ds_exp = fabsf(ff->params.drop_shadow.offset_x)
                             + fabsf(ff->params.drop_shadow.offset_y)
                             + ff->params.drop_shadow.blur_radius + 2;
                if (ds_exp > filter_expand) filter_expand = ds_exp;
            }
            ff = ff->next;
        }
        Rect filter_rect;
        filter_rect.x = pa_block.x + block->x - filter_expand;
        filter_rect.y = pa_block.y + block->y - filter_expand;
        filter_rect.width = block->width + filter_expand * 2;
        filter_rect.height = block->height + filter_expand * 2;

        log_debug("[FILTER] Applying filters to element %s at (%.0f,%.0f) size %.0fx%.0f",
                  block->node_name(), filter_rect.x, filter_rect.y, filter_rect.width, filter_rect.height);

        // Apply the filter chain to the rendered pixels
        if (rdcon->dl) {
            dl_apply_filter(rdcon->dl, filter_rect.x, filter_rect.y, filter_rect.width, filter_rect.height,
                            block->filter, &rdcon->block.clip);
        } else {
            apply_css_filters(&rdcon->scratch, rdcon->ui_context->surface, block->filter, &filter_rect, &rdcon->block.clip);
        }

        auto tf2 = std::chrono::high_resolution_clock::now();
        g_render_filter_time += std::chrono::duration<double, std::milli>(tf2 - tf1).count();
        g_render_filter_count++;
    }

    // Composite filter result: element rendered on transparent, now composite over saved backdrop.
    // apply_css_filters() premultiplies its output, so we use the premul source-over formula
    // for both drop-shadow and other filters.
    if (has_filter_backdrop && dsw > 0 && dsh > 0) {
        if (rdcon->dl) {
            // DL path: composite element+filter group over saved backdrop at full opacity
            dl_composite_opacity(rdcon->dl, dsx0, dsy0, dsw, dsh, 1.0f);
        } else if (ds_backdrop) {
            ImageSurface* surface = rdcon->ui_context->surface;
            uint32_t* px = (uint32_t*)surface->pixels;
            int pitch = surface->pitch / 4;
            for (int row = 0; row < dsh; row++) {
                for (int col = 0; col < dsw; col++) {
                    uint32_t src = px[(dsy0 + row) * pitch + (dsx0 + col)];
                    uint32_t dst = ds_backdrop[row * dsw + col];
                    if (src == 0) {
                        px[(dsy0 + row) * pitch + (dsx0 + col)] = dst;
                        continue;
                    }
                    uint32_t sa = (src >> 24) & 0xFF;
                    if (sa == 255) continue;  // fully opaque element pixel, keep as is
                    // Porter-Duff source-over (premul): result = src + dst * (1 - src_a)
                    uint32_t da = (dst >> 24) & 0xFF;
                    uint32_t inv_sa = 255 - sa;
                    uint32_t ra = sa + (da * inv_sa + 127) / 255;
                    uint32_t rr = (src & 0xFF) + (((dst & 0xFF) * inv_sa + 127) / 255);
                    uint32_t rg = (((src >> 8) & 0xFF) + ((((dst >> 8) & 0xFF) * inv_sa + 127) / 255));
                    uint32_t rb = (((src >> 16) & 0xFF) + ((((dst >> 16) & 0xFF) * inv_sa + 127) / 255));
                    px[(dsy0 + row) * pitch + (dsx0 + col)] =
                        (std::min(ra, 255u) << 24) | (std::min(rb, 255u) << 16) |
                        (std::min(rg, 255u) << 8) | std::min(rr, 255u);
                }
            }
            log_debug("[FILTER] Composited filtered element over backdrop %dx%d", dsw, dsh);
        }
    }

    // Apply CSS opacity: composite element group over saved backdrop at opacity
    if (has_opacity_group && opw > 0 && oph > 0) {
        auto to1 = std::chrono::high_resolution_clock::now();

        float opacity = block->in_line->opacity;
        if (rdcon->dl) {
            dl_composite_opacity(rdcon->dl, opx0, opy0, opw, oph, opacity);
        } else if (opacity_backdrop) {
            ImageSurface* surface = rdcon->ui_context->surface;
            uint32_t* px = (uint32_t*)surface->pixels;
            int pitch = surface->pitch / 4;
            int opacity_i = (int)(opacity * 256 + 0.5f);
            for (int row = 0; row < oph; row++) {
                for (int col = 0; col < opw; col++) {
                    uint32_t src = px[(opy0 + row) * pitch + (opx0 + col)];
                    uint32_t dst = opacity_backdrop[row * opw + col];
                    if (src == 0) {
                        px[(opy0 + row) * pitch + (opx0 + col)] = dst;
                        continue;
                    }
                    // Scale source by opacity (premultiplied alpha compositing)
                    uint32_t sa = ((src >> 24) & 0xFF) * opacity_i + 128;  sa >>= 8;
                    uint32_t sr = (src & 0xFF) * opacity_i + 128;          sr >>= 8;
                    uint32_t sg = ((src >> 8) & 0xFF) * opacity_i + 128;   sg >>= 8;
                    uint32_t sb = ((src >> 16) & 0xFF) * opacity_i + 128;  sb >>= 8;
                    // Porter-Duff source over: result = src' + dst * (1 - src'_alpha)
                    uint32_t inv_sa = 255 - sa;
                    uint32_t da = (dst >> 24) & 0xFF;
                    uint32_t dr = dst & 0xFF;
                    uint32_t dg = (dst >> 8) & 0xFF;
                    uint32_t db = (dst >> 16) & 0xFF;
                    uint32_t ra = sa + (da * inv_sa + 128) / 255;
                    uint32_t rr = sr + (dr * inv_sa + 128) / 255;
                    uint32_t rg = sg + (dg * inv_sa + 128) / 255;
                    uint32_t rb = sb + (db * inv_sa + 128) / 255;
                    if (ra > 255) ra = 255;
                    if (rr > 255) rr = 255;
                    if (rg > 255) rg = 255;
                    if (rb > 255) rb = 255;
                    px[(opy0 + row) * pitch + (opx0 + col)] =
                        (ra << 24) | (rb << 16) | (rg << 8) | rr;
                }
            }
            scratch_free(&rdcon->scratch, opacity_backdrop);
            log_debug("[OPACITY] Composited opacity=%.2f on <%s> region (%d,%d) %dx%d",
                      opacity, block->node_name(), opx0, opy0, opw, oph);
        }

        auto to2 = std::chrono::high_resolution_clock::now();
        g_render_opacity_time += std::chrono::duration<double, std::milli>(to2 - to1).count();
        g_render_opacity_count++;
    }

    // Apply mix-blend-mode: composite rendered element onto saved backdrop
    if (mix_blend && mbw > 0 && mbh > 0) {
        auto tm1 = std::chrono::high_resolution_clock::now();

        if (rdcon->dl) {
            dl_apply_blend_mode(rdcon->dl, mbx0, mby0, mbw, mbh, (int)mix_blend);
        } else if (mix_blend_backdrop) {
            ImageSurface* surface = rdcon->ui_context->surface;
            uint32_t* px = (uint32_t*)surface->pixels;
            int pitch = surface->pitch / 4;
            for (int row = 0; row < mbh; row++) {
                for (int col = 0; col < mbw; col++) {
                    uint32_t backdrop = mix_blend_backdrop[row * mbw + col];
                    uint32_t source = px[(mby0 + row) * pitch + (mbx0 + col)];
                    px[(mby0 + row) * pitch + (mbx0 + col)] =
                        composite_blend_pixel(backdrop, source, mix_blend);
                }
            }
            scratch_free(&rdcon->scratch, mix_blend_backdrop);
            log_debug("[MIX-BLEND] Applied mix-blend-mode on <%s> %dx%d", block->node_name(), mbw, mbh);
        }

        auto tm2 = std::chrono::high_resolution_clock::now();
        g_render_blend_time += std::chrono::duration<double, std::milli>(tm2 - tm1).count();
        g_render_blend_count++;
    }

    // Pop CSS clip-path
    if (has_css_clip) {
        auto tc1 = std::chrono::high_resolution_clock::now();
        rc_pop_clip(rdcon);
        if (rdcon->clip_shape_depth > 0) rdcon->clip_shape_depth--;
        free_clip_shape(&rdcon->scratch, css_clip_shape);
        auto tc2 = std::chrono::high_resolution_clock::now();
        g_render_clip_time += std::chrono::duration<double, std::milli>(tc2 - tc1).count();
        g_render_clip_count++;
    }

    // Restore transform state
    rdcon->transform = pa_transform;
    rdcon->has_transform = pa_has_transform;

    rdcon->block = pa_block;  rdcon->font = pa_font;  rdcon->color = pa_color;
    log_leave();

    auto rbv_end = std::chrono::high_resolution_clock::now();
    double this_total = std::chrono::duration<double, std::milli>(rbv_end - rbv_start).count();
    // Self-time = total minus children traversal time
    g_render_block_self_time += (this_total - children_time);
}

void render_svg(ImageSurface* surface) {
    if (!surface->pic) {
        log_debug("no picture to render");  return;
    }
    // Rasterize the SVG picture into a pixel buffer using a temporary vector context
    uint32_t width = surface->max_render_width;
    uint32_t height = surface->max_render_width * surface->height / surface->width;
    surface->pixels = (uint32_t*)mem_alloc(width * height * sizeof(uint32_t), MEM_CAT_RENDER);
    if (!surface->pixels) return;

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

// Helper function to render just the image content (without block layout)
// Used by both render_image_view and render_block_view for embedded images
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
        // render the SVG image
        log_debug("render svg image at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        if (!img->pixels) {
            render_svg(img);
        }
        if (img->pixels) {
            // clip to block bounds
            Bound* clip = &rdcon->block.clip;
            RdtPath* clip_path = rdt_path_new();
            rdt_path_add_rect(clip_path, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top, 0, 0);
            rc_push_clip(rdcon, clip_path, nullptr);
            rdt_path_free(clip_path);

            rc_draw_image(rdcon, (uint32_t*)img->pixels, img->width, img->height,
                           img->width, img_rect.x, img_rect.y, img_rect.width, img_rect.height, 255, nullptr);

            rc_pop_clip(rdcon);
        } else {
            log_debug("failed to render svg image");
        }
    } else {
        // ensure raster image pixels are decoded (lazy loading)
        image_surface_ensure_decoded(img);
        log_debug("blit image at x:%f, y:%f, wd:%f, hg:%f", img_rect.x, img_rect.y, img_rect.width, img_rect.height);
        rc_blit_surface_scaled(rdcon, img, NULL, rdcon->ui_context->surface, &img_rect, &rdcon->block.clip, SCALE_MODE_LINEAR,
            rdcon->clip_shapes, rdcon->clip_shape_depth);
    }

    // Render blue selection overlay if image is within a cross-view selection
    if (rdcon->selection && is_view_in_selection(rdcon->selection, (View*)view)) {
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
    // render border and background, etc.
    render_block_view(rdcon, (ViewBlock*)view);
    // render the image content (using parent coordinates restored by render_block_view)
    render_image_content(rdcon, view);
    log_debug("end of image render");
    log_leave();
}

// render webview in layer mode: record a DL placeholder for post-composite blit
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
                                     &rdcon->block.clip);
    } else {
        // fallback: direct blit (single-threaded path)
        Rect rect = { dst_x, dst_y, dst_w, dst_h };
        blit_surface_scaled(wv->surface, NULL, rdcon->ui_context->surface, &rect,
                            &rdcon->block.clip, SCALE_MODE_LINEAR,
                            rdcon->clip_shapes, rdcon->clip_shape_depth);
    }
}

// render video element: record a DL_VIDEO_PLACEHOLDER for post-composite blit
void render_video_content(RenderContext* rdcon, ViewBlock* view) {
    if (!view->embed || !view->embed->video) return;

    float s = rdcon->scale;
    float dst_x = rdcon->block.x + view->x * s;
    float dst_y = rdcon->block.y + view->y * s;
    float dst_w = view->width * s;
    float dst_h = view->height * s;
    int object_fit_flags = (int)view->embed->object_fit;
    // pack has_controls into bit 8
    if (view->embed->has_controls) object_fit_flags |= 0x100;

    log_debug("[VIDEO RENDER] placeholder at (%.0f,%.0f) size %.0fx%.0f controls=%d",
              dst_x, dst_y, dst_w, dst_h, view->embed->has_controls);

    if (rdcon->dl) {
        dl_video_placeholder(rdcon->dl, view->embed->video,
                             dst_x, dst_y, dst_w, dst_h,
                             object_fit_flags, &rdcon->block.clip);
    }
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

                // Reset color to black for embedded document (don't inherit from parent doc)
                // Each document should start with default black text color
                rdcon->color.c = 0xFF000000;  // opaque black (ABGR)

                // load default font
                FontProp* default_font = doc->view_tree->html_version == HTML5 ? &rdcon->ui_context->default_font : &rdcon->ui_context->legacy_default_font;
                log_debug("render_init default font: %s, html version: %d", default_font->family, doc->view_tree->html_version);
                setup_font(rdcon->ui_context, &rdcon->font, default_font);

                ViewBlock* root_block = (ViewBlock*)root_view;

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

                rdcon->font = pa_font;
                rdcon->color = pa_color;
            }
            else {
                log_debug("Invalid root view");
            }
        }
    }

    // Render scrollbar for the iframe scroll container
    if (block->scroller) {
        render_scroller(rdcon, block, &pa_block);
    }
    rdcon->block = pa_block;
}

void render_inline_view(RenderContext* rdcon, ViewSpan* view_span) {
    g_render_inline_count++;
    FontBox pa_font = rdcon->font;  Color pa_color = rdcon->color;
    log_debug("render inline view");

    bool self_hidden = view_span->in_line && view_span->in_line->visibility == VIS_HIDDEN;

    // Render border/outline for inline elements.
    // Background is rendered per-line-fragment in render_text_view so that
    // wrapping inline elements (e.g. <code> spanning two lines) don't fill
    // the entire bounding-box rectangle with background color.
    if (!self_hidden && view_span->bound) {
        BackgroundProp* saved_bg = view_span->bound->background;
        view_span->bound->background = nullptr;
        render_bound(rdcon, (ViewBlock*)view_span);
        view_span->bound->background = saved_bg;
    }

    View* view = view_span->first_child;
    if (view) {
        if (view_span->font) {
            setup_font(rdcon->ui_context, &rdcon->font, view_span->font);
        }
        if (view_span->in_line && view_span->in_line->color.c) {
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

void render_children(RenderContext* rdcon, View* view) {
    auto trc_start = std::chrono::high_resolution_clock::now();
    do {
        g_render_dispatch_count++;
        if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
            view->view_type == RDT_VIEW_TABLE || view->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
            view->view_type == RDT_VIEW_TABLE_ROW || view->view_type == RDT_VIEW_TABLE_CELL) {
            ViewBlock* block = (ViewBlock*)view;
            log_debug("[RENDER DISPATCH] view_type=%d, embed=%p, img=%p, width=%.0f, height=%.0f",
                      view->view_type, block->embed,
                      block->embed ? block->embed->img : NULL, block->width, block->height);
            if (block->item_prop_type == DomElement::ITEM_PROP_FORM && block->form) {
                // Form control rendering (input, select, textarea, button)
                // For <button> elements with children, render default button background BEFORE
                // children so the gray fill doesn't cover the text content.
                if (block->form->control_type == FORM_CONTROL_BUTTON && block->first_child) {
                    render_form_control(rdcon, block);  // draw button chrome first
                    render_block_view(rdcon, block);    // then children (text) on top
                } else {
                    // Other form controls: render block first, then form decorations on top
                    log_debug("[RENDER DISPATCH] calling render_block_view for form control");
                    render_block_view(rdcon, block);
                    log_debug("[RENDER DISPATCH] calling render_form_control");
                    render_form_control(rdcon, block);
                }
            }
            else if (block->tag_id == HTM_TAG_SVG) {
                // Inline SVG element - render via ThorVG
                log_debug("[RENDER DISPATCH] calling render_inline_svg for inline SVG");
                auto ts1 = std::chrono::high_resolution_clock::now();
                render_inline_svg(rdcon, block);
                auto ts2 = std::chrono::high_resolution_clock::now();
                g_render_svg_time += std::chrono::duration<double, std::milli>(ts2 - ts1).count();
                g_render_svg_count++;
            }
            else if (block->embed && block->embed->img) {
                log_debug("[RENDER DISPATCH] calling render_image_view");
                auto ti1 = std::chrono::high_resolution_clock::now();
                render_image_view(rdcon, block);
                auto ti2 = std::chrono::high_resolution_clock::now();
                g_render_image_time += std::chrono::duration<double, std::milli>(ti2 - ti1).count();
                g_render_image_count++;
            }
            else if (block->embed && block->embed->video) {
                log_debug("[RENDER DISPATCH] calling render_video_content for <video>");
                render_block_view(rdcon, block);
                render_video_content(rdcon, block);
            }
            else if (block->embed && block->embed->webview &&
                     block->embed->webview->mode == WEBVIEW_MODE_LAYER) {
                log_debug("[RENDER DISPATCH] calling render_webview_layer_content");
                render_block_view(rdcon, block);
                render_webview_layer_content(rdcon, block);
            }
            else if (block->embed && block->embed->doc) {
                render_embed_doc(rdcon, block);
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
            auto tiv1 = std::chrono::high_resolution_clock::now();
            render_inline_view(rdcon, span);
            auto tiv2 = std::chrono::high_resolution_clock::now();
            g_render_inline_time += std::chrono::duration<double, std::milli>(tiv2 - tiv1).count();
        }
        else if (view->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)view;
            auto tt1 = std::chrono::high_resolution_clock::now();
            render_text_view(rdcon, text);
            auto tt2 = std::chrono::high_resolution_clock::now();
            g_render_text_total_time += std::chrono::duration<double, std::milli>(tt2 - tt1).count();
            g_render_text_count++;
        }
        else if (view->view_type == RDT_VIEW_MARKER) {
            // List marker (bullet/number) with fixed width and vector graphics
            ViewSpan* marker = (ViewSpan*)view;
            render_marker_view(rdcon, marker);
        }
        else {
            log_debug("unknown view in rendering: %d", view->view_type);
        }
        view = view->next();
    } while (view);
    auto trc_end = std::chrono::high_resolution_clock::now();
    g_render_children_time += std::chrono::duration<double, std::milli>(trc_end - trc_start).count();
}

// ============================================================================
// Focus, Caret, and Selection Rendering
// ============================================================================

/**
 * Render focus outline around the currently focused element
 * Draws a 2px dotted outline outside the element's border box
 */
void render_focus_outline(RenderContext* rdcon, RadiantState* state) {
    if (!state || !state->focus || !state->focus->current) return;

    // Only render focus-visible (keyboard navigation)
    if (!state->focus->focus_visible) return;

    View* focused = state->focus->current;
    if (focused->view_type != RDT_VIEW_BLOCK) return;

    ViewBlock* block = (ViewBlock*)focused;
    float s = rdcon->scale;

    // Calculate absolute position of the focused element (in CSS pixels)
    float x = block->x;
    float y = block->y;
    float width = block->width;
    float height = block->height;

    // Walk up the tree to get absolute coordinates (CSS pixels)
    View* parent = block->parent;
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK) {
            x += ((ViewBlock*)parent)->x;
            y += ((ViewBlock*)parent)->y;
        }
        parent = parent->parent;
    }

    // Scale to physical pixels
    x *= s;  y *= s;
    width *= s;  height *= s;

    // Outline offset (outside border box) - in physical pixels
    float outline_offset = 2.0f * s;
    float outline_width = 2.0f * s;

    // Draw dotted rectangle outline
    float ox = x - outline_offset;
    float oy = y - outline_offset;
    float ow = width + outline_offset * 2;
    float oh = height + outline_offset * 2;

    // Focus ring color: #005FCC (blue), dotted pattern
    RdtPath* path = rdt_path_new();
    rdt_path_add_rect(path, ox, oy, ow, oh, 0, 0);
    float dash_pattern[] = {4.0f * s, 2.0f * s};
    Color focus_color = {0}; focus_color.r = 0x00; focus_color.g = 0x5F; focus_color.b = 0xCC; focus_color.a = 0xFF;
    rc_stroke_path(rdcon, path, focus_color, outline_width, RDT_CAP_BUTT, RDT_JOIN_MITER, dash_pattern, 2, nullptr);
    rdt_path_free(path);
    log_debug("[FOCUS] Rendered focus outline at (%.0f,%.0f) size %.0fx%.0f", ox, oy, ow, oh);
}

/**
 * Render the text caret (blinking cursor) in an editable element
 */
void render_caret(RenderContext* rdcon, RadiantState* state) {
    if (!state || !state->caret) {
        return;
    }
    // Force visible for debugging
    state->caret->visible = true;

    if (!state->caret->view) return;

    CaretState* caret = state->caret;
    View* view = caret->view;

    // Form controls draw their own caret in render_text_input() / render_textarea()
    if (view->is_element()) {
        DomElement* elem = (DomElement*)view;
        if (elem->item_prop_type == DomElement::ITEM_PROP_FORM &&
            elem->form &&
            (elem->form->control_type == FORM_CONTROL_TEXT ||
             elem->form->control_type == FORM_CONTROL_TEXTAREA)) {
            return;
        }
    }

    float s = rdcon->scale;

    // caret->x and caret->y are relative to the parent block (from TextRect coordinates)
    // So we start with those coordinates and walk up from the parent block
    float x = caret->x;
    float y = caret->y;

    // Walk up from the text view's parent to get absolute coordinates
    // The caret x/y is already relative to the text's parent block
    View* parent = view->parent;  // Start from parent, not from view itself
    while (parent) {
        if (parent->view_type == RDT_VIEW_BLOCK ||
            parent->view_type == RDT_VIEW_INLINE_BLOCK ||
            parent->view_type == RDT_VIEW_LIST_ITEM) {
            ViewBlock* block = (ViewBlock*)parent;
            x += block->x;
            y += block->y;
            // Account for scroll offset (same as render traversal does)
            if (block->scroller && block->scroller->pane) {
                x -= block->scroller->pane->h_scroll_position;
                y -= block->scroller->pane->v_scroll_position;
            }
        }
        parent = parent->parent;
    }

    // Add iframe offset (if the caret is inside an iframe, parent chain stops at iframe doc root)
    x += caret->iframe_offset_x;
    y += caret->iframe_offset_y;

    // Store CSS pixel coordinates for logging
    float css_x = x;
    float css_y = y;

    // Phase 19: save absolute CSS position for dirty-rect caret repaint
    caret->prev_abs_x = css_x;
    caret->prev_abs_y = css_y;
    caret->prev_abs_height = caret->height;

    // Scale to physical pixels
    x *= s;  y *= s;
    float height = caret->height * s;
    float caret_width = 3.0f * s;  // 3 CSS pixels wide

    log_debug("[CARET] Before render: CSS pos (%.1f,%.1f), physical pos (%.1f,%.1f) height=%.1f",
        css_x, css_y, x, y, height);

    // Draw caret rectangle
    Color caret_color = {0}; caret_color.r = 0x66; caret_color.g = 0x66; caret_color.b = 0x66; caret_color.a = 0xCC;
    rc_fill_rect(rdcon, x, y, caret_width, height, caret_color);
    log_debug("[CARET] Drew caret at (%.0f,%.0f) size %.0fx%.0f", x, y, caret_width, height);

    log_debug("[CARET] Rendered caret at (%.0f,%.0f) height=%.0f", x, y, height);
}

/**
 * Render text selection highlight.
 *
 * Phase 6 (single source of truth): canonical multi-line / cross-node
 * selection painter. Reads `state->dom_selection` (the spec-level
 * Selection model), resolves layout via `dom_range_for_each_rect()`,
 * and emits one semi-transparent rectangle per visual fragment.
 *
 * The renderer's inline glyph-by-glyph selection background painter in
 * `render_text_view` is still active for backward compatibility — it
 * paints under each selected glyph using the legacy SelectionState.
 * `render_selection` is therefore registered as an overlay only when
 * the legacy path didn't / can't paint (e.g. element-level selection
 * spanning across non-text nodes set by JS via `setBaseAndExtent`).
 *
 * `dom_range_for_each_rect()` returns rectangles in absolute CSS
 * coordinates; we just scale to physical pixels and fill.
 */
struct SelectionPaintCtx {
    RenderContext* rdcon;
    Color          color;
    float          scale;
    float          iframe_offset_x;
    float          iframe_offset_y;
};

static void selection_paint_rect_cb(float x, float y, float w, float h, void* ud) {
    SelectionPaintCtx* ctx = (SelectionPaintCtx*)ud;
    if (w <= 0 || h <= 0) return;
    float s = ctx->scale;
    float px = (x + ctx->iframe_offset_x) * s;
    float py = (y + ctx->iframe_offset_y) * s;
    float pw = w * s;
    float ph = h * s;
    rc_fill_rect(ctx->rdcon, px, py, pw, ph, ctx->color);
}

void render_selection(RenderContext* rdcon, RadiantState* state) {
    if (!state) return;

    // Prefer DomSelection (canonical). Fall back to legacy SelectionState
    // only when DomSelection is not yet populated (early boot / non-DOM
    // paths). Both should be in sync via legacy_sync_from_dom_selection.
    DomSelection* ds = state->dom_selection;
    bool use_dom = ds && ds->range_count > 0 && !ds->is_collapsed;

    if (!use_dom) {
        // Nothing selected via DOM; legacy painter (inline in
        // render_text_view) covers the legacy SelectionState case.
        return;
    }

    DomRange* r = ds->ranges[0];
    if (!r) return;

    // Resolve layout (idempotent when already valid).
    if (!dom_range_resolve_layout(r)) {
        log_debug("[SELECTION] dom_range_resolve_layout failed");
        return;
    }

    SelectionPaintCtx ctx;
    ctx.rdcon = rdcon;
    ctx.scale = rdcon->scale;
    // Iframe offset cached on the legacy selection (resolver itself doesn't
    // know about iframe nesting). 0 when not in an iframe.
    ctx.iframe_offset_x = state->selection ? state->selection->iframe_offset_x : 0;
    ctx.iframe_offset_y = state->selection ? state->selection->iframe_offset_y : 0;
    // Standard text-selection blue; alpha 0x80 = 50% (matches inline painter).
    ctx.color.r = 0x00; ctx.color.g = 0x78; ctx.color.b = 0xD7; ctx.color.a = 0x80;

    dom_range_for_each_rect(r, selection_paint_rect_cb, &ctx);
    log_debug("[SELECTION] Rendered DomSelection range via dom_range_for_each_rect");
}

/**
 * Render all interactive state overlays (caret, focus)
 * Called after main content rendering, before canvas sync
 * Note: Selection is now rendered inline during text rendering
 */
void render_ui_overlays(RenderContext* rdcon, RadiantState* state) {
    if (!state) {
        log_debug("[UI_OVERLAY] No state");
        return;
    }

    log_debug("[UI_OVERLAY] Rendering overlays: caret=%p", (void*)state->caret);

    // Phase A: render selection via DomSelection-driven multi-rect overlay.
    // Inline glyph painter in render_text_view is now disabled.
    render_selection(rdcon, state);

    // Render open dropdown popup (above content)
    if (state->open_dropdown) {
        ViewBlock* select = (ViewBlock*)state->open_dropdown;
        render_select_dropdown(rdcon, select, state);
    }

    // Render drag-and-drop overlay (drop target highlight + drag indicator)
    if (state->drag_drop && state->drag_drop->active) {
        DragDropState* dd = state->drag_drop;
        float s = rdcon->scale;

        // Highlight the current drop target with a blue border
        if (dd->drop_target && dd->drop_target->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* dt = (ViewBlock*)dd->drop_target;
            float dx = dt->x, dy = dt->y;
            View* par = dt->parent;
            while (par) {
                if (par->view_type == RDT_VIEW_BLOCK) {
                    dx += ((ViewBlock*)par)->x;
                    dy += ((ViewBlock*)par)->y;
                }
                par = par->parent;
            }
            dx *= s;  dy *= s;
            float dw = dt->width * s;
            float dh = dt->height * s;

            // draw highlight border around drop target
            Color drop_stroke_color = {0}; drop_stroke_color.r = 0x33; drop_stroke_color.g = 0x99; drop_stroke_color.b = 0xFF; drop_stroke_color.a = 0xC0;
            RdtPath* drop_path = rdt_path_new();
            rdt_path_add_rect(drop_path, dx - 2*s, dy - 2*s, dw + 4*s, dh + 4*s, 0, 0);
            rc_stroke_path(rdcon, drop_path, drop_stroke_color, 2.0f * s, RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, nullptr);
            rdt_path_free(drop_path);

            // draw semi-transparent blue fill
            Color drop_fill_color = {0}; drop_fill_color.r = 0x33; drop_fill_color.g = 0x99; drop_fill_color.b = 0xFF; drop_fill_color.a = 0x20;
            rc_fill_rect(rdcon, dx, dy, dw, dh, drop_fill_color);
            log_debug("[DRAG] Drop target highlight at (%.0f,%.0f) size %.0fx%.0f", dx, dy, dw, dh);
        }

        // Draw a small drag indicator at cursor position
        float cx = dd->current_x;
        float cy = dd->current_y;
        Color ind_fill = {0}; ind_fill.r = 0x33; ind_fill.g = 0x99; ind_fill.b = 0xFF; ind_fill.a = 0x80;
        rc_fill_rounded_rect(rdcon, cx - 4*s, cy - 4*s, 8*s, 8*s, 2*s, 2*s, ind_fill);
        Color ind_stroke = {0}; ind_stroke.r = 0x33; ind_stroke.g = 0x99; ind_stroke.b = 0xFF; ind_stroke.a = 0xFF;
        RdtPath* ind_path = rdt_path_new();
        rdt_path_add_rect(ind_path, cx - 4*s, cy - 4*s, 8*s, 8*s, 2*s, 2*s);
        rc_stroke_path(rdcon, ind_path, ind_stroke, 1.0f * s, RDT_CAP_BUTT, RDT_JOIN_MITER, nullptr, 0, nullptr);
        rdt_path_free(ind_path);
    }

    // Caret rendered on top of content
    render_caret(rdcon, state);

    // Focus outline rendered last (outside content)
    render_focus_outline(rdcon, state);
}

void render_init(RenderContext* rdcon, UiContext* uicon, ViewTree* view_tree) {
    memset(rdcon, 0, sizeof(RenderContext));
    rdcon->ui_context = uicon;

    // Initialize scratch allocator for scoped temporary buffers
    scratch_init(&rdcon->scratch, view_tree->arena);

    // initialize vector renderer (owns the ThorVG canvas internally)
    rdt_vector_init(&rdcon->vec, (uint32_t*)uicon->surface->pixels,
        uicon->surface->width, uicon->surface->height, uicon->surface->width);

    // Initialize transform state (identity matrix, not active)
    rdcon->transform = rdt_matrix_identity();
    rdcon->has_transform = false;

    // Initialize HiDPI scale factor for converting CSS logical pixels to physical surface pixels
    rdcon->scale = uicon->pixel_ratio > 0 ? uicon->pixel_ratio : 1.0f;
    log_debug("render_init: scale factor = %.2f (pixel_ratio)", rdcon->scale);

    // Initialize selection state from document state
    if (uicon->document && uicon->document->state && uicon->document->state->selection) {
        rdcon->selection = uicon->document->state->selection;
    } else {
        rdcon->selection = nullptr;
    }

    // load default font
    FontProp* default_font = view_tree->html_version == HTML5 ? &uicon->default_font : &uicon->legacy_default_font;
    log_debug("render_init default font: %s, html version: %d", default_font->family, view_tree->html_version);
    setup_font(uicon, &rdcon->font, default_font);
    rdcon->block.clip = (Bound){0, 0, (float)uicon->surface->width, (float)uicon->surface->height};
    // initialize default text color to opaque black (ABGR format: 0xFF000000)
    rdcon->color.c = 0xFF000000;
    log_debug("render_init clip: [%.0f, %.0f, %.0f, %.0f]", rdcon->block.clip.left, rdcon->block.clip.top, rdcon->block.clip.right, rdcon->block.clip.bottom);
}

void render_clean_up(RenderContext* rdcon) {
    scratch_release(&rdcon->scratch);
    rdt_vector_destroy(&rdcon->vec);
}

/**
 * Get the canvas background color per CSS 2.1 spec section 14.2:
 * If the root element (html) has no background, propagate from body.
 * Returns white (0xFFFFFFFF) as default.
 */
static uint32_t get_canvas_background(View* root_view) {
    if (!root_view || root_view->view_type != RDT_VIEW_BLOCK) {
        return 0xFFFFFFFF;  // default white
    }

    ViewBlock* html_block = (ViewBlock*)root_view;

    // Check if html element has a background color
    bool html_has_bg = html_block->bound && html_block->bound->background &&
                       html_block->bound->background->color.a > 0;

    if (html_has_bg) {
        // HTML has background, use it for canvas
        return html_block->bound->background->color.c;
    }

    // HTML has no background, check for body element
    // Per CSS spec, propagate body's background to canvas
    View* child = html_block->first_child;
    while (child) {
        if (child->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* child_block = (ViewBlock*)child;
            const char* name = child_block->node_name();
            if (name && str_ieq_const(name, strlen(name), "body")) {
                // Found body element
                if (child_block->bound && child_block->bound->background &&
                    child_block->bound->background->color.a > 0) {
                    log_debug("[RENDER] Propagating body background #%08x to canvas",
                              child_block->bound->background->color.c);
                    return child_block->bound->background->color.c;
                }
                break;
            }
        }
        child = (View*)child->next_sibling;
    }

    return 0xFFFFFFFF;  // default white
}

void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    reset_render_stats();  // reset performance counters

    RenderContext rdcon;
    log_debug("Render HTML doc");
    render_init(&rdcon, uicon, view_tree);

    // Get canvas background color (may be propagated from body per CSS spec)
    uint32_t canvas_bg = get_canvas_background(view_tree->root);

    // Phase 12.4: selective clear — only clear dirty regions when available
    bool selective = false;
    RadiantState* state = uicon->document ? (RadiantState*)uicon->document->state : nullptr;
    bool force_full = state && state->is_dirty;
    if (!force_full && state && !state->dirty_tracker.full_repaint && dirty_has_regions(&state->dirty_tracker)) {
        // Clear only dirty regions to background color
        DirtyRect* dr = state->dirty_tracker.dirty_list;
        float scale = rdcon.scale;
        while (dr) {
            Rect dirty_rect = {dr->x * scale, dr->y * scale, dr->width * scale, dr->height * scale};
            fill_surface_rect(rdcon.ui_context->surface, &dirty_rect, canvas_bg, &rdcon.block.clip);
            dr = dr->next;
        }
        selective = true;
        // Phase 18: Pass dirty tracker to render context for subtree clipping
        rdcon.dirty_tracker = &state->dirty_tracker;
        // Precompute dirty union bbox (CSS pixels) for consistent culling & replay clipping
        DirtyRect* first = state->dirty_tracker.dirty_list;
        float du_l = first->x, du_t = first->y;
        float du_r = first->x + first->width, du_b = first->y + first->height;
        for (DirtyRect* d = first->next; d; d = d->next) {
            if (d->x < du_l) du_l = d->x;
            if (d->y < du_t) du_t = d->y;
            if (d->x + d->width > du_r) du_r = d->x + d->width;
            if (d->y + d->height > du_b) du_b = d->y + d->height;
        }
        rdcon.dirty_union = {du_l, du_t, du_r, du_b};
        rdcon.has_dirty_union = true;
        log_debug("render_html_doc: selective clear (dirty union: %.0f,%.0f - %.0f,%.0f)",
                  du_l, du_t, du_r, du_b);
    } else {
        // Full clear
        fill_surface_rect(rdcon.ui_context->surface, NULL, canvas_bg, &rdcon.block.clip);
    }

    // Initialize display list for deferred rendering (Phase 1)
    DisplayList display_list = {};
    dl_init(&display_list, view_tree->arena);
    rdcon.dl = &display_list;

    auto t_init = high_resolution_clock::now();

    View* root_view = view_tree->root;
    if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
        log_debug("Render root view");
        ViewBlock* root_block = (ViewBlock*)root_view;
        if (root_block->embed && root_block->embed->img) {
            render_image_view(&rdcon, root_block);
        } else {
            render_block_view(&rdcon, root_block);
        }
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
    log_info("[TIMING] render_block_view (record): %.1fms, %d display list items",
             duration<double, std::milli>(t_render - t_init).count(), dl_item_count(&display_list));
    log_render_stats();  // log detailed render statistics

    // Stderr-based profiling output (works with --no-log)
    double render_ms = duration<double, std::milli>(t_render - t_init).count();
    fprintf(stderr, "[RENDER_PROF] render_block_view: %.1fms  surface: %dx%d  dl_items: %d\n",
        render_ms, uicon->surface->width, uicon->surface->height, dl_item_count(&display_list));
    stderr_render_stats();

    // Render UI overlays (focus outline, caret, selection) on top of content
    if (uicon->document && uicon->document->state) {
        log_debug("[RENDER] calling render_ui_overlays, state=%p", (void*)uicon->document->state);
        render_ui_overlays(&rdcon, uicon->document->state);
    } else {
        log_debug("[RENDER] no state for overlays: doc=%p, state=%p",
            (void*)uicon->document, uicon->document ? (void*)uicon->document->state : nullptr);
    }

    // Phase 2: Replay display list — tile-based parallel or single-threaded
    rdcon.dl = nullptr;  // prevent re-recording during replay
    auto t_replay_start = high_resolution_clock::now();

    int render_threads = get_render_thread_count();
    int item_count = dl_item_count(&display_list);

    if (!selective && render_threads != 1 && item_count > 0) {
        // --- Tile-based parallel rasterization ---
        ImageSurface* surface = rdcon.ui_context->surface;
        TileGrid grid;
        tile_grid_init(&grid, surface->width, surface->height, rdcon.scale);
        tile_grid_clear(&grid, canvas_bg);

        // Lazily initialise the global render pool (thread-safe)
        g_render_pool_threads = render_threads;
        pthread_once(&g_render_pool_once, init_render_pool_once);

        // Build job array — one job per tile
        TileJob* jobs = (TileJob*)mem_alloc(grid.total * sizeof(TileJob), MEM_CAT_RENDER);
        for (int i = 0; i < grid.total; i++) {
            jobs[i].tile = &grid.tiles[i];
            jobs[i].display_list = &display_list;
            jobs[i].scale = rdcon.scale;
            jobs[i].bg_color = canvas_bg;
        }

        // Dispatch and wait
        render_pool_dispatch(g_render_pool, jobs, grid.total);

        // Composite tiles onto the final surface
        tile_grid_composite(&grid, surface);

        auto t_replay_end = high_resolution_clock::now();
        int total_tiles = grid.total;
        log_info("[TIMING] dl_replay_tiled: %.1fms (%d items, %d tiles, %d threads)",
                 duration<double, std::milli>(t_replay_end - t_replay_start).count(),
                 item_count, total_tiles, g_render_pool->thread_count);
        fprintf(stderr, "[RENDER_PROF] dl_replay_tiled: %.1fms  items: %d  tiles: %d  threads: %d\n",
                duration<double, std::milli>(t_replay_end - t_replay_start).count(),
                item_count, total_tiles, g_render_pool->thread_count);

        mem_free(jobs);
        tile_grid_destroy(&grid);
    } else {
        // --- Single-threaded replay (fallback) ---
        DirtyTracker* replay_dirty = selective ? &state->dirty_tracker : nullptr;
        dl_replay(&display_list, &rdcon.vec, rdcon.ui_context->surface,
                  &rdcon.block.clip, &rdcon.scratch, rdcon.scale, replay_dirty);

        auto t_replay_end = high_resolution_clock::now();
        log_info("[TIMING] dl_replay: %.1fms (%d items)",
                 duration<double, std::milli>(t_replay_end - t_replay_start).count(),
                 item_count);
        fprintf(stderr, "[RENDER_PROF] dl_replay: %.1fms  items: %d\n",
                duration<double, std::milli>(t_replay_end - t_replay_start).count(),
                item_count);
    }

    // Post-composite: blit video frames onto the final surface
    RadiantState* rstate = uicon->document ? uicon->document->state : nullptr;
    render_video_frames(&display_list, rdcon.ui_context->surface, rstate, rdcon.ui_context);

    auto t_sync = high_resolution_clock::now();
    log_info("[TIMING] render complete: %.1fms", duration<double, std::milli>(t_sync - t_render).count());

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

    dl_destroy(&display_list);
    render_clean_up(&rdcon);
    if (uicon->document->state) {
        uicon->document->state->is_dirty = false;
    }

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc total: %.1fms%s", duration<double, std::milli>(t_end - t_start).count(),
        selective ? " (selective)" : "");
}

/**
 * Tile-based PNG renderer for pages whose full-surface allocation would OOM.
 *
 * Instead of allocating a single (total_width × total_height) surface, this
 * function renders the page in horizontal strips of TILE_H physical pixels each,
 * writing each strip's rows directly to an already-open PNG stream.  Peak memory
 * for pixel data is O(total_width × TILE_H) regardless of page length.
 *
 * @param uicon       UiContext with document + view-tree already set up.
 *                    uicon->surface may be NULL or point to a smaller surface;
 *                    tiled rendering creates its own tile surfaces internally.
 * @param view_tree   Fully-laid-out view tree.
 * @param output_file Destination PNG file path.
 * @param total_width Physical pixel width of the full output image.
 * @param total_height Physical pixel height of the full output image.
 */
void render_html_doc_tiled(UiContext* uicon, ViewTree* view_tree,
                           const char* output_file,
                           int total_width, int total_height) {
    using namespace std::chrono;
    auto t_start = high_resolution_clock::now();

    static const int TILE_H = 4096;  // physical pixels per strip
    log_info("render_html_doc_tiled: %dx%d px -> %s (%d tiles of %d px)",
        total_width, total_height, output_file,
        (total_height + TILE_H - 1) / TILE_H, TILE_H);

    // --- Open PNG for streaming write ---
    FILE* fp = fopen(output_file, "wb");
    if (!fp) {
        log_error("render_html_doc_tiled: cannot open output file: %s", output_file);
        return;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return; }
    if (setjmp(png_jmpbuf(png))) {
        log_error("render_html_doc_tiled: PNG error during write");
        png_destroy_write_struct(&png, &info); fclose(fp); return;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, total_width, total_height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint32_t canvas_bg = get_canvas_background(view_tree->root);

    // save surface pointer so we can restore it (uicon may not have a surface yet)
    ImageSurface* saved_surface = uicon->surface;
    int saved_window_height = uicon->window_height;

    // --- Render each tile ---
    for (int tile_y = 0; tile_y < total_height; tile_y += TILE_H) {
        int tile_h = (tile_y + TILE_H <= total_height) ? TILE_H : (total_height - tile_y);

        ImageSurface* tile_surf = image_surface_create(total_width, tile_h);
        if (!tile_surf) {
            log_error("render_html_doc_tiled: failed to allocate tile surface %dx%d at y=%d",
                total_width, tile_h, tile_y);
            break;
        }

        // clear tile to background color before setting the tile offset
        {
            Bound tile_clip = {0, 0, (float)total_width, (float)tile_h};
            fill_surface_rect(tile_surf, NULL, canvas_bg, &tile_clip);
        }
        // tile_offset_y must be set AFTER the clear (clear uses tile-relative coords)
        tile_surf->tile_offset_y = tile_y;

        // point uicon to the tile surface for the duration of this tile
        uicon->surface = tile_surf;
        uicon->window_height = tile_h;

        RenderContext rdcon;
        render_init(&rdcon, uicon, view_tree);

        // override clip to page-absolute tile bounds so the rendering pipeline
        // naturally skips content outside this strip
        rdcon.block.clip = {0, (float)tile_y, (float)total_width, (float)(tile_y + tile_h)};

        // tell ThorVG to translate all shapes upward by tile_y
        rdt_vector_set_tile_offset_y(&rdcon.vec, (float)tile_y);

        reset_render_stats();
        View* root_view = view_tree->root;
        if (root_view && root_view->view_type == RDT_VIEW_BLOCK) {
            ViewBlock* root_block = (ViewBlock*)root_view;
            if (root_block->embed && root_block->embed->img) {
                render_image_view(&rdcon, root_block);
            } else {
                render_block_view(&rdcon, root_block);
            }
            // render absolutely/fixed positioned children
            if (root_block->position) {
                ViewBlock* child = root_block->position->first_abs_child;
                while (child) {
                    render_block_view(&rdcon, child);
                    child = child->position->next_abs_sibling;
                }
            }
        }
        render_clean_up(&rdcon);

        // write this tile's rows into the PNG stream
        for (int y = 0; y < tile_h; y++) {
            uint8_t* row = (uint8_t*)tile_surf->pixels + y * tile_surf->pitch;
            png_write_row(png, row);
        }

        image_surface_destroy(tile_surf);
        log_info("render_html_doc_tiled: tile y=%d..%d done", tile_y, tile_y + tile_h);
    }

    // restore uicon
    uicon->surface = saved_surface;
    uicon->window_height = saved_window_height;

    // finalize PNG
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    auto t_end = high_resolution_clock::now();
    log_info("[TIMING] render_html_doc_tiled total: %.1fms (%dx%d)",
        duration<double, std::milli>(t_end - t_start).count(), total_width, total_height);
}
