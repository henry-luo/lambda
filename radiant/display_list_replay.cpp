#include "render.hpp"
#include "../lib/log.h"

static bool dl_replay_item_intersects_dirty(const DisplayReplayDirtyClip* dirty_clip,
                                            const DisplayItem* item) {
    if (!dirty_clip || !dirty_clip->active || !item) return true;
    return dl_item_intersects_rect(item,
        dirty_clip->bounds.left,
        dirty_clip->bounds.top,
        dirty_clip->bounds.right - dirty_clip->bounds.left,
        dirty_clip->bounds.bottom - dirty_clip->bounds.top);
}

static bool dl_replay_skip_clean_item(const DisplayReplayDirtyClip* dirty_clip,
                                      const DisplayItem* item, DisplayListCursor* cursor) {
    if (!dirty_clip || !dirty_clip->active || !item || !cursor) return false;
    if (item->op == DL_BEGIN_ELEMENT &&
        item->element_marker.matching_index > cursor->index &&
        !dl_replay_item_intersects_dirty(dirty_clip, item)) {
        cursor->skip_element();
        return true;
    }
    return dl_op_has_flags(item->op, DL_OP_FLAG_DIRTY_SKIPPABLE) &&
        !dl_replay_item_intersects_dirty(dirty_clip, item);
}

static bool dl_replay_vector_or_descriptor_skip(RdtVector* vec, const DisplayItem* item) {
    if (!item) return true;
    if (dl_replay_vector_item(vec, item) != DL_REPLAY_VECTOR_NOT_HANDLED) {
        return true;
    }
    if (dl_op_has_flags(item->op, DL_OP_FLAG_REPLAY_NOOP_AFTER_VECTOR)) {
        return true;
    }
    if (dl_op_has_flags(item->op, DL_OP_FLAG_FLUSHES_VECTOR_BATCH)) {
        rdt_vector_flush_batch(vec);
    }
    return false;
}

static void dl_replay_draw_glyph_dirty_clipped(ImageSurface* surface,
                                               const DisplayReplayDirtyClip* dirty_clip,
                                               const DlDrawGlyph* glyph) {
    if (!dirty_clip || !dirty_clip->active) {
        dl_replay_draw_glyph(surface, glyph);
        return;
    }
    DlDrawGlyph tightened = *glyph;
    dl_replay_intersect_dirty_clip(dirty_clip, &tightened.clip);
    dl_replay_draw_glyph(surface, &tightened);
}

void dl_replay(DisplayList* dl, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker) {
    ValidatedDisplayList recording;
    if (!dl_validate_recording(dl, dl ? dl->item_count() : 0, &recording, "dl_replay")) return;
    dl_replay_validated(recording, vec, surface, clip, scratch, scale, dirty_tracker);
}

void dl_replay_validated(const ValidatedDisplayList& recording, RdtVector* vec,
               ImageSurface* surface, Bound* clip,
               ScratchArena* scratch, float scale,
               DirtyTracker* dirty_tracker) {
    DisplayList* dl = recording.get();
    if (!dl) {
        log_error("[DL_REPLAY] invalid or stale validated recording");
        return;
    }
    (void)clip;
    // image owners resolved during this replay stay alive until it ends
    ImageSurfaceReadScope read_scope;
    log_debug("[DL_REPLAY] replaying %d items", dl->item_count());

    DisplayReplayDirtyClip dirty_clip = dl_replay_push_dirty_clip(vec, dirty_tracker, scale);
    const RenderBackendCaps* caps = render_backend_get_caps(vec);

    DisplayReplayBackdropStack backdrop_stack;
    dl_replay_backdrop_init(&backdrop_stack);

    DisplayReplayShadowClip shadow_clip;
    dl_replay_shadow_clip_init(&shadow_clip);

    rdt_vector_begin_batch(vec);

    DisplayListCursor cursor(dl);
    while (const DisplayItem* item = cursor.next()) {

        if (dl_replay_skip_clean_item(&dirty_clip, item, &cursor)) {
            continue;
        }

        if (dl_replay_vector_or_descriptor_skip(vec, item)) continue;

        switch (item->op) {
        case DL_DRAW_GLYPH: {
            dl_replay_draw_glyph_dirty_clipped(surface, &dirty_clip, &item->draw_glyph);
            break;
        }

        case DL_FILL_SURFACE_RECT: {
            const DlFillSurfaceRect* r = &item->fill_surface_rect;
            dl_replay_fill_surface_rect(surface, &dirty_clip, r);
            break;
        }

        case DL_BLIT_SURFACE_SCALED: {
            const DlBlitSurfaceScaled* r = &item->blit_surface_scaled;
            dl_replay_blit_surface_scaled(surface, &dirty_clip, r);
            break;
        }

        case DL_COMPOSITE_OPACITY: {
            const DlCompositeOpacity* r = &item->composite_opacity;
            dl_replay_backdrop_composite_opacity(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_SAVE_BACKDROP: {
            const DlSaveBackdrop* r = &item->save_backdrop;
            dl_replay_backdrop_save(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_APPLY_BLEND_MODE: {
            const DlApplyBlendMode* r = &item->apply_blend_mode;
            dl_replay_backdrop_apply_blend_mode(&backdrop_stack, surface, scratch, r);
            break;
        }

        case DL_APPLY_FILTER: {
            const DlApplyFilter* r = &item->apply_filter;
            dl_replay_apply_filter(scratch, surface, caps, &dirty_clip, r);
            break;
        }

        case DL_BOX_BLUR_REGION: {
            const DlBoxBlurRegion* r = &item->box_blur_region;
            dl_replay_box_blur_region(scratch, surface, r);
            break;
        }

        case DL_BOX_BLUR_INSET: {
            const DlBoxBlurInset* r = &item->box_blur_inset;
            dl_replay_box_blur_inset(scratch, surface, r);
            break;
        }

        case DL_SHADOW_CLIP_SAVE: {
            const DlShadowClipSave* r = &item->shadow_clip_save;
            dl_replay_shadow_clip_save(&shadow_clip, surface, scratch, r);
            break;
        }

        case DL_SHADOW_CLIP_RESTORE: {
            const DlShadowClipRestore* r = &item->shadow_clip_restore;
            dl_replay_shadow_clip_restore(&shadow_clip, surface, r);
            break;
        }

        case DL_OUTER_SHADOW: {
            const DlOuterShadow* o = &item->outer_shadow;
            dl_replay_outer_shadow(scratch, surface, o);
            break;
        }

        case DL_WEBVIEW_LAYER_PLACEHOLDER: {
            const DlWebviewLayerPlaceholder* r = &item->webview_layer_placeholder;
            dl_replay_webview_layer_placeholder(surface, r);
            break;
        }

        default:
            break;
        }
    }

    if (cursor.failed) log_error("[DL_REPLAY] invalid fragment graph");
    rdt_vector_end_batch(vec);

    int backdrop_depth = dl_replay_backdrop_depth(&backdrop_stack);
    if (backdrop_depth > 0) {
        log_error("[DL_REPLAY] unbalanced backdrop stack: %d entries left", backdrop_depth);
    }
    dl_replay_close_open_scopes(&backdrop_stack, &shadow_clip, scratch);

    dl_replay_pop_dirty_clip(vec, &dirty_clip);

    log_debug("[DL_REPLAY] done");
}
