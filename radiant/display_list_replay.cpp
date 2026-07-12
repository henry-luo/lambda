#include "display_list_replay.hpp"

#include "display_list_bounds.hpp"
#include "display_list_replay_backdrop.hpp"
#include "display_list_replay_effects.hpp"
#include "display_list_replay_glyph.hpp"
#include "display_list_replay_raster.hpp"
#include "display_list_replay_shadow.hpp"
#include "display_list_replay_state.hpp"
#include "display_list_replay_vector.hpp"
#include "render_backend_caps.hpp"
#include "../lib/log.h"

static bool dl_replay_can_skip_item_for_dirty(DisplayOp op) {
    switch (op) {
        case DL_FILL_RECT:
        case DL_FILL_ROUNDED_RECT:
        case DL_FILL_PATH:
        case DL_STROKE_PATH:
        case DL_FILL_LINEAR_GRADIENT:
        case DL_FILL_RADIAL_GRADIENT:
        case DL_DRAW_IMAGE:
        case DL_DRAW_GLYPH:
        case DL_DRAW_PICTURE:
        case DL_FILL_SURFACE_RECT:
        case DL_BLIT_SURFACE_SCALED:
        case DL_APPLY_FILTER:
        case DL_BOX_BLUR_REGION:
        case DL_BOX_BLUR_INSET:
        case DL_OUTER_SHADOW:
        case DL_VIDEO_PLACEHOLDER:
        case DL_WEBVIEW_LAYER_PLACEHOLDER:
            return true;
        default:
            return false;
    }
}

static bool dl_replay_item_intersects_dirty(const DisplayReplayDirtyClip* dirty_clip,
                                            const DisplayItem* item) {
    if (!dirty_clip || !dirty_clip->active || !item) return true;
    return dl_item_intersects_rect(item,
        dirty_clip->bounds.left,
        dirty_clip->bounds.top,
        dirty_clip->bounds.right - dirty_clip->bounds.left,
        dirty_clip->bounds.bottom - dirty_clip->bounds.top);
}

void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker) {
    (void)clip;
    if (!dl_validate_or_log(dl, "dl_replay")) {
        return;
    }
    log_debug("[DL_REPLAY] replaying %d items", dl->count);

    DisplayReplayDirtyClip dirty_clip = dl_replay_push_dirty_clip(vec, dirty_tracker, scale);
    const RenderBackendCaps* caps = render_backend_get_caps(vec);

    DisplayReplayBackdropStack backdrop_stack;
    dl_replay_backdrop_init(&backdrop_stack);

    DisplayReplayShadowClip shadow_clip;
    dl_replay_shadow_clip_init(&shadow_clip);

    rdt_vector_begin_batch(vec);

    for (int i = 0; i < dl->count; i++) {
        DisplayItem* item = &dl->items[i];

        if (dirty_clip.active) {
            if (item->op == DL_BEGIN_ELEMENT &&
                item->element_marker.matching_index > i &&
                !dl_replay_item_intersects_dirty(&dirty_clip, item)) {
                i = item->element_marker.matching_index;
                continue;
            }
            if (dl_replay_can_skip_item_for_dirty(item->op) &&
                !dl_replay_item_intersects_dirty(&dirty_clip, item)) {
                continue;
            }
        }

        if (dl_replay_vector_item(vec, item, false) != DL_REPLAY_VECTOR_NOT_HANDLED) {
            continue;
        }

        switch (item->op) {
        // vector ops are handled before this switch by dl_replay_vector_item().
        case DL_FILL_RECT:
        case DL_FILL_ROUNDED_RECT:
        case DL_FILL_PATH:
        case DL_STROKE_PATH:
        case DL_FILL_LINEAR_GRADIENT:
        case DL_FILL_RADIAL_GRADIENT:
        case DL_DRAW_IMAGE:
        case DL_DRAW_PICTURE:
        case DL_PUSH_CLIP:
        case DL_POP_CLIP:
            break;

        case DL_DRAW_GLYPH: {
            rdt_vector_flush_batch(vec);
            if (dirty_clip.active) {
                DlDrawGlyph tightened = item->draw_glyph;
                dl_replay_intersect_dirty_clip(&dirty_clip, &tightened.clip);
                dl_replay_draw_glyph(surface, &tightened);
            } else {
                dl_replay_draw_glyph(surface, &item->draw_glyph);
            }
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            rdt_vector_flush_batch(vec);
            DlFillSurfaceRect* r = &item->fill_surface_rect;
            dl_replay_fill_surface_rect(surface, &dirty_clip, r);
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            rdt_vector_flush_batch(vec);
            DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            dl_replay_blit_surface_scaled(surface, &dirty_clip, r);
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            rdt_vector_flush_batch(vec);
            DlCompositeOpacity* r = &item->composite_opacity;
            dl_replay_backdrop_composite_opacity(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_SAVE_BACKDROP: {
            rdt_vector_flush_batch(vec);
            DlSaveBackdrop* r = &item->save_backdrop;
            dl_replay_backdrop_save(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            rdt_vector_flush_batch(vec);
            DlApplyBlendMode* r = &item->apply_blend_mode;
            dl_replay_backdrop_apply_blend_mode(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_APPLY_FILTER: {
            rdt_vector_flush_batch(vec);
            DlApplyFilter* r = &item->apply_filter;
            dl_replay_apply_filter(scratch, surface, caps, &dirty_clip, r);
            break;
        }

        case DL_BOX_BLUR_REGION: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurRegion* r = &item->box_blur_region;
            dl_replay_box_blur_region(scratch, surface, r);
            break;
        }

        case DL_BOX_BLUR_INSET: {
            rdt_vector_flush_batch(vec);
            DlBoxBlurInset* r = &item->box_blur_inset;
            dl_replay_box_blur_inset(scratch, surface, r);
            break;
        }

        case DL_SHADOW_CLIP_SAVE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipSave* r = &item->shadow_clip_save;
            dl_replay_shadow_clip_save(&shadow_clip, surface, scratch, r);
            break;
        }

        case DL_SHADOW_CLIP_RESTORE: {
            rdt_vector_flush_batch(vec);
            DlShadowClipRestore* r = &item->shadow_clip_restore;
            dl_replay_shadow_clip_restore(&shadow_clip, surface, r);
            break;
        }

        case DL_OUTER_SHADOW: {
            rdt_vector_flush_batch(vec);
            DlOuterShadow* o = &item->outer_shadow;
            dl_replay_outer_shadow(scratch, surface, o);
            break;
        }

        case DL_BEGIN_ELEMENT:
        case DL_END_ELEMENT:
            break;

        case DL_VIDEO_PLACEHOLDER:
            break;

        case DL_WEBVIEW_LAYER_PLACEHOLDER: {
            rdt_vector_flush_batch(vec);
            DlWebviewLayerPlaceholder* r = &item->webview_layer_placeholder;
            dl_replay_webview_layer_placeholder(surface, r);
            break;
        }
        }
    }

    rdt_vector_end_batch(vec);

    int backdrop_depth = dl_replay_backdrop_depth(&backdrop_stack);
    if (backdrop_depth > 0) {
        log_error("[DL_REPLAY] unbalanced backdrop stack: %d entries left", backdrop_depth);
    }

    dl_replay_pop_dirty_clip(vec, &dirty_clip);

    log_debug("[DL_REPLAY] done");
}
