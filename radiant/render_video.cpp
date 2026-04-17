// render_video.cpp — Post-composite video frame blit for Radiant
//
// After tile compositing produces the final surface, this function scans the
// display list for DL_VIDEO_PLACEHOLDER items and blits the latest decoded
// video frame onto the surface at the recorded layout rect.

#include "display_list.h"
#include "rdt_video.h"
#include "state_store.hpp"
#include "view.hpp"
#include "../lib/log.h"
#include "../lib/mem.h"

#include <string.h>

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

void render_video_frames(DisplayList* dl, ImageSurface* surface, RadiantState* rstate) {
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
            cached++;
        }

        RdtVideoState state = rdt_video_get_state(video);
        if (state != RDT_VIDEO_STATE_PLAYING && state != RDT_VIDEO_STATE_PAUSED &&
            state != RDT_VIDEO_STATE_READY) {
            continue;
        }

        // skip blit for off-viewport videos
        if (!is_video_visible(vp->dst_x, vp->dst_y, vp->dst_w, vp->dst_h,
                              vp->clip.left, vp->clip.top, vp->clip.right, vp->clip.bottom,
                              surface->width, surface->height)) {
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
    }

    if (rstate) rstate->video_placement_count = cached;
}

// ---------------------------------------------------------------------------
// render_video_frames_cached — blit video frames using cached placements
// Used for video-only dirty path: skips DL rebuild + tile replay
// ---------------------------------------------------------------------------

void render_video_frames_cached(RadiantState* rstate, ImageSurface* surface) {
    if (!rstate || !surface || rstate->video_placement_count <= 0) return;

    for (int i = 0; i < rstate->video_placement_count; i++) {
        auto& p = rstate->video_placements[i];
        RdtVideo* video = (RdtVideo*)p.video;
        if (!video) continue;

        RdtVideoState state = rdt_video_get_state(video);
        if (state != RDT_VIDEO_STATE_PLAYING && state != RDT_VIDEO_STATE_PAUSED &&
            state != RDT_VIDEO_STATE_READY) {
            continue;
        }

        // skip blit for off-viewport videos
        if (!is_video_visible(p.dst_x, p.dst_y, p.dst_w, p.dst_h,
                              p.clip_left, p.clip_top, p.clip_right, p.clip_bottom,
                              surface->width, surface->height)) {
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
    }
}
