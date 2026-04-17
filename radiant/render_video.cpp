// render_video.cpp — Post-composite video frame blit + controls overlay for Radiant
//
// After tile compositing produces the final surface, this function scans the
// display list for DL_VIDEO_PLACEHOLDER items and blits the latest decoded
// video frame onto the surface at the recorded layout rect.
// Controls overlay (play/pause, seek bar, time, volume) is drawn on top.
// Vector graphics rendered via RdtVector (ThorVG), text via font.h glyph system.

#include "display_list.h"
#include "rdt_video.h"
#include "rdt_vector.hpp"
#include "state_store.hpp"
#include "view.hpp"
#include "../lib/font/font.h"
#include "../lib/log.h"
#include "../lib/mem.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

// ---------------------------------------------------------------------------
// blit_video_frame — scale + clip a video frame onto the target surface
// ---------------------------------------------------------------------------

static void blit_video_frame(ImageSurface* surface, const RdtVideoFrame* frame,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             const Bound* clip) {
    if (!surface || !surface->pixels || !frame || !frame->pixels) return;
    if (frame->width <= 0 || frame->height <= 0) return;
    if (dst_w <= 0 || dst_h <= 0) return;

    int surf_w = surface->width;
    int surf_h = surface->height;
    int surf_pitch = surface->pitch / 4;  // pixels per row
    uint32_t* dst_pixels = (uint32_t*)surface->pixels;

    // clip bounds
    float clip_left   = clip ? clip->left   : 0;
    float clip_top    = clip ? clip->top    : 0;
    float clip_right  = clip ? clip->right  : (float)surf_w;
    float clip_bottom = clip ? clip->bottom : (float)surf_h;

    // clamp destination to surface and clip bounds
    float x0 = dst_x < clip_left   ? clip_left   : dst_x;
    float y0 = dst_y < clip_top    ? clip_top    : dst_y;
    float x1 = (dst_x + dst_w) > clip_right  ? clip_right  : (dst_x + dst_w);
    float y1 = (dst_y + dst_h) > clip_bottom ? clip_bottom : (dst_y + dst_h);

    if (x0 >= x1 || y0 >= y1) return;

    int ix0 = (int)x0, iy0 = (int)y0;
    int ix1 = (int)x1, iy1 = (int)y1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > surf_w) ix1 = surf_w;
    if (iy1 > surf_h) iy1 = surf_h;

    float scale_x = (float)frame->width / dst_w;
    float scale_y = (float)frame->height / dst_h;

    const uint32_t* src_pixels = (const uint32_t*)frame->pixels;
    int src_stride = frame->stride / 4;

    for (int py = iy0; py < iy1; py++) {
        int sy = (int)((py - dst_y) * scale_y);
        if (sy < 0) sy = 0;
        if (sy >= frame->height) sy = frame->height - 1;
        const uint32_t* src_row = src_pixels + sy * src_stride;
        uint32_t* dst_row = dst_pixels + py * surf_pitch;

        for (int px = ix0; px < ix1; px++) {
            int sx = (int)((px - dst_x) * scale_x);
            if (sx < 0) sx = 0;
            if (sx >= frame->width) sx = frame->width - 1;
            dst_row[px] = src_row[sx];
        }
    }
}

// ---------------------------------------------------------------------------
// render_video_frames — scan display list, blit all video frames post-composite
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// is_video_visible — check if video rect intersects clip bounds and surface
// ---------------------------------------------------------------------------

static bool is_video_visible(float dst_x, float dst_y, float dst_w, float dst_h,
                             float clip_l, float clip_t, float clip_r, float clip_b,
                             int surf_w, int surf_h) {
    // check against clip bounds (container overflow, scroll)
    if (dst_x + dst_w <= clip_l || dst_x >= clip_r) return false;
    if (dst_y + dst_h <= clip_t || dst_y >= clip_b) return false;
    // check against surface bounds (viewport)
    if (dst_x + dst_w <= 0 || dst_x >= (float)surf_w) return false;
    if (dst_y + dst_h <= 0 || dst_y >= (float)surf_h) return false;
    return true;
}

// ===========================================================================
// Video controls overlay drawing (ThorVG vector graphics + font system text)
// ===========================================================================

// controls bar constants (in physical pixels)
static const float CONTROLS_HEIGHT = 40.0f;
static const float CONTROLS_PADDING = 8.0f;
static const float PLAY_BTN_SIZE = 24.0f;
static const float SEEK_BAR_HEIGHT = 4.0f;
static const float VOLUME_WIDTH = 60.0f;
static const float VOLUME_HEIGHT = 4.0f;
static const float ICON_MARGIN = 8.0f;

// format time as m:ss
static void format_time(double seconds, char* buf, int bufsize) {
    if (seconds < 0) seconds = 0;
    int total = (int)seconds; // INT_CAST_OK: time formatting
    int m = total / 60;
    int s = total % 60;
    snprintf(buf, bufsize, "%d:%02d", m, s);
}

// alpha-blend a pixel onto the surface (still needed for glyph blitting)
static inline void blend_pixel(uint32_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0) return;
    if (a == 255) { *dst = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | (0xFFu << 24); return; }
    uint32_t d = *dst;
    uint8_t dr = d & 0xFF, dg = (d >> 8) & 0xFF, db = (d >> 16) & 0xFF;
    uint8_t inv_a = 255 - a;
    uint8_t or_ = (uint8_t)((r * a + dr * inv_a) / 255);
    uint8_t og  = (uint8_t)((g * a + dg * inv_a) / 255);
    uint8_t ob  = (uint8_t)((b * a + db * inv_a) / 255);
    *dst = (uint32_t)or_ | ((uint32_t)og << 8) | ((uint32_t)ob << 16) | (0xFFu << 24);
}

// blit a grayscale glyph bitmap onto the surface with a given color
static void blit_glyph(ImageSurface* surface, GlyphBitmap* bitmap,
                       int x, int y, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    if (!bitmap || !bitmap->buffer || !surface || !surface->pixels) return;
    int sw = surface->width, sh = surface->height;
    int pitch = surface->pitch / 4;
    uint32_t* pixels = (uint32_t*)surface->pixels;

    for (int row = 0; row < bitmap->height; row++) {
        int py = y + row;
        if (py < 0 || py >= sh) continue;
        uint32_t* dst_row = pixels + py * pitch;
        for (int col = 0; col < bitmap->width; col++) {
            int px = x + col;
            if (px < 0 || px >= sw) continue;
            uint8_t intensity = bitmap->buffer[row * bitmap->pitch + col];
            if (intensity == 0) continue;
            uint8_t a = (uint8_t)((ca * intensity) / 255);
            blend_pixel(&dst_row[px], cr, cg, cb, a);
        }
    }
}

// draw a text string using font system glyphs, returns total advance width
static float draw_font_text(ImageSurface* surface, FontContext* font_ctx,
                            const char* text, float x, float y_baseline,
                            float font_size_px, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    if (!font_ctx || !text || !text[0]) return 0;

    FontStyleDesc style = {};
    style.family = "Helvetica,Arial,sans-serif";
    style.size_px = font_size_px;
    style.weight = FONT_WEIGHT_NORMAL;
    style.slant = FONT_SLANT_NORMAL;

    FontHandle* handle = font_resolve(font_ctx, &style);
    if (!handle) return 0;

    float cx = x;
    for (int i = 0; text[i]; i++) {
        uint32_t codepoint = (uint32_t)(unsigned char)text[i];
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, true);
        if (!glyph) { cx += font_size_px * 0.5f; continue; }

        int gx = (int)(cx + glyph->bitmap.bearing_x);
        int gy = (int)(y_baseline - glyph->bitmap.bearing_y);

        if (glyph->bitmap.pixel_mode == GLYPH_PIXEL_GRAY && glyph->bitmap.buffer) {
            blit_glyph(surface, &glyph->bitmap, gx, gy, cr, cg, cb, ca);
        }
        cx += glyph->advance_x;
    }

    font_handle_release(handle);
    return cx - x;
}

// measure text width without drawing
static float measure_font_text(FontContext* font_ctx, const char* text, float font_size_px) {
    if (!font_ctx || !text || !text[0]) return 0;

    FontStyleDesc style = {};
    style.family = "Helvetica,Arial,sans-serif";
    style.size_px = font_size_px;
    style.weight = FONT_WEIGHT_NORMAL;
    style.slant = FONT_SLANT_NORMAL;

    FontHandle* handle = font_resolve(font_ctx, &style);
    if (!handle) return (float)strlen(text) * font_size_px * 0.6f;

    float width = 0;
    for (int i = 0; text[i]; i++) {
        uint32_t codepoint = (uint32_t)(unsigned char)text[i];
        LoadedGlyph* glyph = font_load_glyph(handle, &style, codepoint, false);
        if (glyph) width += glyph->advance_x;
        else width += font_size_px * 0.5f;
    }

    font_handle_release(handle);
    return width;
}

// ---------------------------------------------------------------------------
// render_video_controls — draw controls overlay using ThorVG + font text
// ---------------------------------------------------------------------------

static void render_video_controls(ImageSurface* surface, RdtVideo* video,
                                  float dst_x, float dst_y, float dst_w, float dst_h,
                                  UiContext* uicon) {
    if (!surface || !video || dst_w < 120 || dst_h < 60) return;

    RdtVideoState state = rdt_video_get_state(video);

    // create a temporary RdtVector targeting the surface
    RdtVector vec = {};
    int stride = surface->pitch / 4;
    rdt_vector_init(&vec, (uint32_t*)surface->pixels, surface->width, surface->height, stride);

    // controls bar at the bottom
    float bar_h = CONTROLS_HEIGHT;
    float bar_y = dst_y + dst_h - bar_h;
    float bar_x = dst_x;
    float bar_w = dst_w;

    // semi-transparent dark background
    Color bg_color = {0x000000B0};  // ABGR: alpha=B0
    bg_color.r = 0x00; bg_color.g = 0x00; bg_color.b = 0x00; bg_color.a = 0xB0;
    rdt_fill_rounded_rect(&vec, bar_x, bar_y, bar_w, bar_h, 4, 4, bg_color);

    float cx = bar_x + CONTROLS_PADDING;
    float cy_center = bar_y + bar_h / 2.0f;

    // --- Play/Pause button (vector path) ---
    float btn_x = cx;
    float btn_y = cy_center - PLAY_BTN_SIZE / 2.0f;
    Color icon_color = {};
    icon_color.r = 0xFF; icon_color.g = 0xFF; icon_color.b = 0xFF; icon_color.a = 0xE0;

    if (state == RDT_VIDEO_STATE_PLAYING) {
        // pause: two vertical bars
        float bw = PLAY_BTN_SIZE * 0.25f;
        float gap = PLAY_BTN_SIZE * 0.15f;
        float lx = btn_x + PLAY_BTN_SIZE * 0.2f;
        float rx = lx + bw + gap;
        rdt_fill_rounded_rect(&vec, lx, btn_y, bw, PLAY_BTN_SIZE, 1, 1, icon_color);
        rdt_fill_rounded_rect(&vec, rx, btn_y, bw, PLAY_BTN_SIZE, 1, 1, icon_color);
    } else {
        // play: right-pointing triangle
        RdtPath* tri = rdt_path_new();
        float tx = btn_x + PLAY_BTN_SIZE * 0.2f;
        rdt_path_move_to(tri, tx, btn_y);
        rdt_path_line_to(tri, tx + PLAY_BTN_SIZE * 0.7f, cy_center);
        rdt_path_line_to(tri, tx, btn_y + PLAY_BTN_SIZE);
        rdt_path_close(tri);
        rdt_fill_path(&vec, tri, icon_color, RDT_FILL_WINDING, NULL);
        rdt_path_free(tri);
    }
    cx = btn_x + PLAY_BTN_SIZE + ICON_MARGIN;

    // --- Current time (font-rendered text) ---
    double cur_time = rdt_video_get_current_time(video);
    double duration = rdt_video_get_duration(video);
    char time_buf[32];
    format_time(cur_time, time_buf, sizeof(time_buf));
    float font_size = 12.0f;
    FontContext* font_ctx = uicon ? uicon->font_ctx : NULL;
    float text_y_baseline = cy_center + font_size * 0.35f;  // approximate vertical centering
    float tw = draw_font_text(surface, font_ctx, time_buf, cx, text_y_baseline,
                              font_size, 0xFF, 0xFF, 0xFF, 0xD0);
    if (tw < 1) tw = (float)strlen(time_buf) * font_size * 0.6f;
    cx += tw + ICON_MARGIN;

    // --- Seek bar (vector rounded rects) ---
    float seek_x = cx;
    float seek_y = cy_center - SEEK_BAR_HEIGHT / 2.0f;
    char dur_buf[32]; format_time(duration, dur_buf, sizeof(dur_buf));
    float dur_text_w = measure_font_text(font_ctx, dur_buf, font_size);
    if (dur_text_w < 1) dur_text_w = (float)strlen(dur_buf) * font_size * 0.6f;
    float volume_total = 16 + ICON_MARGIN / 2 + VOLUME_WIDTH + ICON_MARGIN;
    float seek_w = bar_w - (cx - bar_x) - ICON_MARGIN - dur_text_w - ICON_MARGIN - volume_total - CONTROLS_PADDING;
    if (seek_w < 40) seek_w = 40;

    // track background
    Color track_color = {};
    track_color.r = 0x80; track_color.g = 0x80; track_color.b = 0x80; track_color.a = 0x80;
    rdt_fill_rounded_rect(&vec, seek_x, seek_y, seek_w, SEEK_BAR_HEIGHT, 2, 2, track_color);

    // progress fill
    float fraction = (duration > 0) ? (float)(cur_time / duration) : 0;
    if (fraction > 1.0f) fraction = 1.0f;
    float prog_w = seek_w * fraction;
    if (prog_w > 1) {
        Color prog_color = {};
        prog_color.r = 0xFF; prog_color.g = 0xFF; prog_color.b = 0xFF; prog_color.a = 0xD0;
        rdt_fill_rounded_rect(&vec, seek_x, seek_y, prog_w, SEEK_BAR_HEIGHT, 2, 2, prog_color);
    }

    // thumb circle
    float thumb_cx = seek_x + prog_w;
    float thumb_r = 5.0f;
    RdtPath* thumb = rdt_path_new();
    rdt_path_add_circle(thumb, thumb_cx, cy_center, thumb_r, thumb_r);
    Color thumb_color = {};
    thumb_color.r = 0xFF; thumb_color.g = 0xFF; thumb_color.b = 0xFF; thumb_color.a = 0xE0;
    rdt_fill_path(&vec, thumb, thumb_color, RDT_FILL_WINDING, NULL);
    rdt_path_free(thumb);

    cx = seek_x + seek_w + ICON_MARGIN;

    // --- Duration text ---
    draw_font_text(surface, font_ctx, dur_buf, cx, text_y_baseline,
                   font_size, 0xFF, 0xFF, 0xFF, 0xD0);
    cx += dur_text_w + ICON_MARGIN;

    // --- Speaker icon (vector path) ---
    float spk_size = 16.0f;
    float spk_x = cx;
    float spk_y = cy_center - spk_size / 2.0f;
    {
        RdtPath* spk = rdt_path_new();
        // speaker body (rectangle)
        float bw = spk_size / 3, bh = spk_size / 2;
        float bx = spk_x, by = spk_y + spk_size / 4;
        rdt_path_add_rect(spk, bx, by, bw, bh, 0, 0);
        rdt_fill_path(&vec, spk, icon_color, RDT_FILL_WINDING, NULL);
        rdt_path_free(spk);

        // speaker cone (triangle)
        RdtPath* cone = rdt_path_new();
        rdt_path_move_to(cone, bx + bw, by);
        rdt_path_line_to(cone, spk_x + spk_size * 2.0f / 3.0f, spk_y);
        rdt_path_line_to(cone, spk_x + spk_size * 2.0f / 3.0f, spk_y + spk_size);
        rdt_path_line_to(cone, bx + bw, by + bh);
        rdt_path_close(cone);
        rdt_fill_path(&vec, cone, icon_color, RDT_FILL_WINDING, NULL);
        rdt_path_free(cone);
    }
    cx += spk_size + ICON_MARGIN / 2;

    // --- Volume slider (vector rounded rects) ---
    float vol_y = cy_center - VOLUME_HEIGHT / 2.0f;
    rdt_fill_rounded_rect(&vec, cx, vol_y, VOLUME_WIDTH, VOLUME_HEIGHT, 2, 2, track_color);

    float vol_level = rdt_video_get_volume(video);
    if (vol_level < 0) vol_level = 0; if (vol_level > 1) vol_level = 1;
    float vol_fill = VOLUME_WIDTH * vol_level;
    if (vol_fill > 1) {
        Color vol_color = {};
        vol_color.r = 0xFF; vol_color.g = 0xFF; vol_color.b = 0xFF; vol_color.a = 0xD0;
        rdt_fill_rounded_rect(&vec, cx, vol_y, vol_fill, VOLUME_HEIGHT, 2, 2, vol_color);
    }

    rdt_vector_destroy(&vec);
}

// ---------------------------------------------------------------------------
// render_poster_image — draw poster image when video hasn't started
// ---------------------------------------------------------------------------

static void render_poster_image(ImageSurface* surface, ImageSurface* poster,
                                float dst_x, float dst_y, float dst_w, float dst_h) {
    if (!surface || !poster || !poster->pixels) return;
    if (poster->width <= 0 || poster->height <= 0 || dst_w <= 0 || dst_h <= 0) return;

    int surf_w = surface->width, surf_h = surface->height;
    int surf_pitch = surface->pitch / 4;
    uint32_t* dst_pixels = (uint32_t*)surface->pixels;

    int ix0 = (int)dst_x, iy0 = (int)dst_y;
    int ix1 = (int)(dst_x + dst_w), iy1 = (int)(dst_y + dst_h);
    if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0;
    if (ix1 > surf_w) ix1 = surf_w; if (iy1 > surf_h) iy1 = surf_h;

    float scale_x = (float)poster->width / dst_w;
    float scale_y = (float)poster->height / dst_h;
    const uint32_t* src_pixels = (const uint32_t*)poster->pixels;
    int src_pitch = poster->pitch / 4;

    for (int py = iy0; py < iy1; py++) {
        int sy = (int)((py - dst_y) * scale_y);
        if (sy >= poster->height) sy = poster->height - 1;
        const uint32_t* src_row = src_pixels + sy * src_pitch;
        uint32_t* dst_row = dst_pixels + py * surf_pitch;
        for (int px = ix0; px < ix1; px++) {
            int sx = (int)((px - dst_x) * scale_x);
            if (sx >= poster->width) sx = poster->width - 1;
            dst_row[px] = src_row[sx];
        }
    }
}

// ---------------------------------------------------------------------------
// render_error_state — draw error placeholder (X mark)
// ---------------------------------------------------------------------------

static void render_error_state(ImageSurface* surface,
                               float dst_x, float dst_y, float dst_w, float dst_h) {
    RdtVector vec = {};
    int stride = surface->pitch / 4;
    rdt_vector_init(&vec, (uint32_t*)surface->pixels, surface->width, surface->height, stride);

    // dark background
    Color bg = {}; bg.r = 0x20; bg.g = 0x20; bg.b = 0x20; bg.a = 0xFF;
    rdt_fill_rect(&vec, dst_x, dst_y, dst_w, dst_h, bg);

    // X mark
    float ccx = dst_x + dst_w / 2, ccy = dst_y + dst_h / 2;
    float s = fminf(dst_w, dst_h) * 0.15f;
    if (s < 8) s = 8; if (s > 40) s = 40;
    float stroke_w = 3.0f;
    Color x_color = {}; x_color.r = 0xCC; x_color.g = 0x33; x_color.b = 0x33; x_color.a = 0xD0;

    RdtPath* xp = rdt_path_new();
    rdt_path_move_to(xp, ccx - s, ccy - s);
    rdt_path_line_to(xp, ccx + s, ccy + s);
    rdt_stroke_path(&vec, xp, x_color, stroke_w, RDT_CAP_ROUND, RDT_JOIN_ROUND, NULL, 0, NULL);
    rdt_path_free(xp);

    RdtPath* xp2 = rdt_path_new();
    rdt_path_move_to(xp2, ccx + s, ccy - s);
    rdt_path_line_to(xp2, ccx - s, ccy + s);
    rdt_stroke_path(&vec, xp2, x_color, stroke_w, RDT_CAP_ROUND, RDT_JOIN_ROUND, NULL, 0, NULL);
    rdt_path_free(xp2);

    rdt_vector_destroy(&vec);
}

// ---------------------------------------------------------------------------
// render_play_button_overlay — large centered play button for idle/paused video
// ---------------------------------------------------------------------------

static void render_play_button_overlay(ImageSurface* surface,
                                       float dst_x, float dst_y, float dst_w, float dst_h) {
    RdtVector vec = {};
    int stride = surface->pitch / 4;
    rdt_vector_init(&vec, (uint32_t*)surface->pixels, surface->width, surface->height, stride);

    float ccx = dst_x + dst_w / 2, ccy = dst_y + dst_h / 2;
    float btn_r = fminf(dst_w, dst_h) * 0.12f;
    if (btn_r < 16) btn_r = 16; if (btn_r > 40) btn_r = 40;

    // semi-transparent circle background
    RdtPath* circle = rdt_path_new();
    rdt_path_add_circle(circle, ccx, ccy, btn_r, btn_r);
    Color circle_color = {}; circle_color.r = 0x00; circle_color.g = 0x00; circle_color.b = 0x00; circle_color.a = 0x80;
    rdt_fill_path(&vec, circle, circle_color, RDT_FILL_WINDING, NULL);
    rdt_path_free(circle);

    // play triangle inside circle
    float tri_s = btn_r * 0.6f;
    float tx = ccx - tri_s * 0.35f;
    RdtPath* tri = rdt_path_new();
    rdt_path_move_to(tri, tx, ccy - tri_s);
    rdt_path_line_to(tri, tx + tri_s * 1.1f, ccy);
    rdt_path_line_to(tri, tx, ccy + tri_s);
    rdt_path_close(tri);
    Color tri_color = {}; tri_color.r = 0xFF; tri_color.g = 0xFF; tri_color.b = 0xFF; tri_color.a = 0xE0;
    rdt_fill_path(&vec, tri, tri_color, RDT_FILL_WINDING, NULL);
    rdt_path_free(tri);

    rdt_vector_destroy(&vec);
}

void render_video_frames(DisplayList* dl, ImageSurface* surface, RadiantState* rstate, UiContext* uicon) {
    if (!dl || !surface) return;

    int count = dl_item_count(dl);
    DisplayItem* items = dl->items;

    // cache video placements for video-only dirty optimisation
    int cached = 0;

    for (int i = 0; i < count; i++) {
        DisplayItem* item = &items[i];
        if (item->op != DL_VIDEO_PLACEHOLDER) continue;

        DlVideoPlaceholder* vp = &item->video_placeholder;
        RdtVideo* video = (RdtVideo*)vp->video;
        if (!video) continue;

        // cache placement for video-only blit path
        if (rstate && cached < MAX_CACHED_VIDEO_PLACEMENTS) {
            auto& p = rstate->video_placements[cached];
            p.video = vp->video;
            p.dst_x = vp->dst_x; p.dst_y = vp->dst_y;
            p.dst_w = vp->dst_w; p.dst_h = vp->dst_h;
            p.clip_left = vp->clip.left; p.clip_top = vp->clip.top;
            p.clip_right = vp->clip.right; p.clip_bottom = vp->clip.bottom;
            p.has_controls = (vp->object_fit & 0x100) != 0;  // controls flag packed in high bits
            cached++;
        }

        // skip blit for off-viewport videos
        if (!is_video_visible(vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h,
                              vp->clip.left, vp->clip.top, vp->clip.right, vp->clip.bottom,
                              surface->width, surface->height)) {
            continue;
        }

        RdtVideoState state = rdt_video_get_state(video);

        // error state: draw error overlay
        if (state == RDT_VIDEO_STATE_ERROR) {
            render_error_state(surface, vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h);
            continue;
        }

        // idle/loading: show poster if available, else large play button
        if (state == RDT_VIDEO_STATE_IDLE || state == RDT_VIDEO_STATE_LOADING) {
            // poster image would need to be passed through; for now show play button
            render_play_button_overlay(surface, vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h);
            continue;
        }

        RdtVideoFrame frame = {};
        if (rdt_video_get_frame(video, &frame) != 0) {
            continue;  // no frame available yet
        }

        log_debug("[VIDEO BLIT] frame %dx%d pts=%.3f → (%.0f,%.0f) %.0fx%.0f",
                  frame.width, frame.height, frame.pts,
                  vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h);

        blit_video_frame(surface, &frame,
                         vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h,
                         &vp->clip);

        // draw controls overlay if attribute is present
        bool has_controls = (vp->object_fit & 0x100) != 0;
        if (has_controls) {
            render_video_controls(surface, video, vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h, uicon);
        }

        // show play button for paused/ready video (when not playing)
        if (state == RDT_VIDEO_STATE_PAUSED || state == RDT_VIDEO_STATE_READY) {
            render_play_button_overlay(surface, vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h);
        }
    }

    if (rstate) rstate->video_placement_count = cached;
}

// ---------------------------------------------------------------------------
// render_video_frames_cached — blit video frames using cached placements
// Used for video-only dirty path: skips DL rebuild + tile replay
// ---------------------------------------------------------------------------

void render_video_frames_cached(RadiantState* rstate, ImageSurface* surface, UiContext* uicon) {
    if (!rstate || !surface || rstate->video_placement_count <= 0) return;

    for (int i = 0; i < rstate->video_placement_count; i++) {
        auto& p = rstate->video_placements[i];
        RdtVideo* video = (RdtVideo*)p.video;
        if (!video) continue;

        RdtVideoState state = rdt_video_get_state(video);

        // skip blit for off-viewport videos
        if (!is_video_visible(p.dst_x, p.dst_y, p.dst_w, p.dst_h,
                              p.clip_left, p.clip_top, p.clip_right, p.clip_bottom,
                              surface->width, surface->height)) {
            continue;
        }

        // error state: draw error overlay
        if (state == RDT_VIDEO_STATE_ERROR) {
            render_error_state(surface, p.dst_x, p.dst_y, p.dst_w, p.dst_h);
            continue;
        }

        if (state != RDT_VIDEO_STATE_PLAYING && state != RDT_VIDEO_STATE_PAUSED &&
            state != RDT_VIDEO_STATE_READY) {
            continue;
        }

        RdtVideoFrame frame = {};
        if (rdt_video_get_frame(video, &frame) != 0) continue;

        log_debug("[VIDEO BLIT cached] frame %dx%d pts=%.3f → (%.0f,%.0f) %.0fx%.0f",
                  frame.width, frame.height, frame.pts,
                  p.dst_x, p.dst_y, p.dst_w, p.dst_h);

        Bound clip = { p.clip_left, p.clip_top, p.clip_right, p.clip_bottom };
        blit_video_frame(surface, &frame,
                         p.dst_x, p.dst_y, p.dst_w, p.dst_h,
                         &clip);

        // draw controls overlay
        if (p.has_controls) {
            render_video_controls(surface, video, p.dst_x, p.dst_y, p.dst_w, p.dst_h, uicon);
        }

        // play button overlay for paused state
        if (state == RDT_VIDEO_STATE_PAUSED || state == RDT_VIDEO_STATE_READY) {
            render_play_button_overlay(surface, p.dst_x, p.dst_y, p.dst_w, p.dst_h);
        }
    }
}
