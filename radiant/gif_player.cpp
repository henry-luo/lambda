#include "gif_player.h"
#include "view.hpp"
#include "state_store.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include <string.h>

// ============================================================================
// GIF Animation Tick
// ============================================================================

void gif_animation_tick(AnimationInstance* anim, float t) {
    GifAnimation* ga = (GifAnimation*)anim->state;
    if (!ga || !ga->frames || !ga->surface) return;

    double now = anim->start_time + anim->duration * t;
    int n = ga->frames->frame_count;

    // Check if it's time to advance the frame
    if (now >= ga->frame_end_time) {
        int next = ga->current_frame + 1;
        if (next >= n) {
            // End of sequence — check loop
            ga->loops_completed++;
            if (ga->loop_count != 0 && ga->loops_completed >= ga->loop_count) {
                // All loops done — stop
                anim->play_state = ANIM_PLAY_FINISHED;
                return;
            }
            next = 0;  // restart from first frame
        }

        ga->current_frame = next;
        GifFrameData* frame = &ga->frames->frames[next];

        // Swap the surface pixel pointer to the new frame
        ga->surface->pixels = frame->pixels;

        // Set next frame end time
        ga->frame_end_time = now + frame->delay_ms / 1000.0;

        log_debug("gif tick: frame %d/%d, delay %dms", next, n, frame->delay_ms);
    }
}

void gif_animation_finish(AnimationInstance* anim) {
    GifAnimation* ga = (GifAnimation*)anim->state;
    if (!ga) return;

    log_debug("gif animation finished: %d frames, %d loops", 
              ga->frames ? ga->frames->frame_count : 0, ga->loops_completed);

    // Free the GifFrames data
    if (ga->frames) {
        image_gif_free(ga->frames);
        ga->frames = NULL;
    }
    // Note: surface->pixels now points to freed memory.
    // The surface itself is not freed here — the caller owns it.
    // We reset to NULL so the renderer shows nothing (or the caller can re-set).
    if (ga->surface) {
        ga->surface->pixels = NULL;
    }
    mem_free(ga);
    anim->state = NULL;
}

// ============================================================================
// GIF Animation Creation
// ============================================================================

AnimationInstance* gif_animation_create(AnimationScheduler* scheduler,
                                         ImageSurface* surface,
                                         GifFrames* gif_frames,
                                         double start_time,
                                         Pool* pool) {
    if (!scheduler || !surface || !gif_frames || gif_frames->frame_count < 2) {
        return NULL;
    }

    // Calculate total duration (sum of all frame delays)
    double total_ms = 0;
    for (int i = 0; i < gif_frames->frame_count; i++) {
        total_ms += gif_frames->frames[i].delay_ms;
    }

    GifAnimation* ga = (GifAnimation*)mem_calloc(1, sizeof(GifAnimation), MEM_CAT_RENDER);
    ga->frames = gif_frames;
    ga->current_frame = 0;
    ga->surface = surface;
    ga->loop_count = gif_frames->loop_count;
    ga->loops_completed = 0;
    ga->frame_end_time = start_time + gif_frames->frames[0].delay_ms / 1000.0;

    // Set the initial frame pixels on the surface
    surface->pixels = gif_frames->frames[0].pixels;

    AnimationInstance* inst = animation_instance_create(scheduler);
    inst->type = ANIM_GIF;
    inst->target = surface;
    inst->state = ga;
    inst->start_time = start_time;
    // GIF duration for the scheduler = total of one loop (infinite iterations handled internally)
    inst->duration = total_ms / 1000.0;
    inst->delay = 0;
    // Infinite iterations — GIF manages its own loop counting in the tick callback
    inst->iteration_count = (gif_frames->loop_count == 0) ? -1 : gif_frames->loop_count;
    inst->current_iteration = 0;
    inst->direction = ANIM_DIR_NORMAL;
    inst->fill_mode = ANIM_FILL_NONE;
    inst->play_state = ANIM_PLAY_RUNNING;
    inst->timing.type = TIMING_LINEAR;
    inst->tick = gif_animation_tick;
    inst->on_finish = gif_animation_finish;

    // Bounds: use the image surface dimensions (will be updated by layout)
    inst->bounds[0] = 0;
    inst->bounds[1] = 0;
    inst->bounds[2] = (float)gif_frames->width;
    inst->bounds[3] = (float)gif_frames->height;

    animation_scheduler_add(scheduler, inst);

    log_info("gif animation created: %d frames, total %.1fms, loop=%d",
             gif_frames->frame_count, total_ms, gif_frames->loop_count);

    return inst;
}

// ============================================================================
// GIF Detection
// ============================================================================

GifFrames* gif_detect_animated(const char* path) {
    if (!path) return NULL;

    // Quick check: is it a GIF file?
    size_t len = strlen(path);
    if (len < 4) return NULL;
    const char* ext = path + len - 4;
    if (!(ext[0] == '.' && (ext[1] == 'g' || ext[1] == 'G') &&
          (ext[2] == 'i' || ext[2] == 'I') && (ext[3] == 'f' || ext[3] == 'F'))) {
        return NULL;
    }

    // Load and check if multi-frame
    return image_gif_load(path);
}

GifFrames* gif_detect_animated_from_memory(const unsigned char* data, size_t length) {
    if (!data || length < 6) return NULL;

    // Check GIF magic bytes
    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F') return NULL;

    return image_gif_load_from_memory(data, length);
}
