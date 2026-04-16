#include "lottie_player.h"
#include "view.hpp"
#include "rdt_vector.hpp"
#include "state_store.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"

// ThorVG C API
#include <thorvg_capi.h>

#include <string.h>

// ============================================================================
// Lottie Animation Tick
// ============================================================================

void lottie_animation_tick(AnimationInstance* anim, float t) {
    LottiePlayer* lp = (LottiePlayer*)anim->state;
    if (!lp || !lp->tvg_animation || !lp->tvg_canvas || !lp->playing) return;

    Tvg_Animation tvg_anim = (Tvg_Animation)lp->tvg_animation;
    Tvg_Canvas canvas = (Tvg_Canvas)lp->tvg_canvas;

    // Compute frame number from normalized progress
    float frame_no = t * lp->total_frames;
    if (frame_no >= lp->total_frames) frame_no = lp->total_frames - 0.001f;
    if (frame_no < 0) frame_no = 0;

    // Set the frame on the ThorVG animation
    Tvg_Result res = tvg_animation_set_frame(tvg_anim, frame_no);
    if (res != TVG_RESULT_SUCCESS && res != TVG_RESULT_INSUFFICIENT_CONDITION) {
        log_debug("lottie tick: set_frame(%.1f) failed: %d", frame_no, res);
        return;
    }

    // Clear and re-draw
    memset(lp->pixels, 0, (size_t)lp->width * lp->height * sizeof(uint32_t));
    tvg_swcanvas_set_target(canvas, lp->pixels, lp->width,
                            lp->width, lp->height, TVG_COLORSPACE_ABGR8888);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);

    // Point the surface pixels at our buffer
    if (lp->surface) {
        lp->surface->pixels = lp->pixels;
    }

    log_debug("lottie tick: frame %.1f/%.0f (t=%.3f)", frame_no, lp->total_frames, t);
}

void lottie_animation_finish(AnimationInstance* anim) {
    LottiePlayer* lp = (LottiePlayer*)anim->state;
    if (!lp) return;

    log_info("lottie animation finished: %.0f frames, %.1fs duration",
             lp->total_frames, lp->duration);

    lp->playing = false;

    // Clean up ThorVG resources
    if (lp->tvg_canvas) {
        tvg_canvas_destroy((Tvg_Canvas)lp->tvg_canvas);
        lp->tvg_canvas = NULL;
    }
    // Note: tvg_animation_del also deletes the picture, so don't double-free
    if (lp->tvg_animation) {
        tvg_animation_del((Tvg_Animation)lp->tvg_animation);
        lp->tvg_animation = NULL;
    }

    // Free pixel buffer
    if (lp->pixels) {
        mem_free(lp->pixels);
        lp->pixels = NULL;
    }
    if (lp->surface) {
        lp->surface->pixels = NULL;
    }

    mem_free(lp);
    anim->state = NULL;
}

// ============================================================================
// Internal: Create LottiePlayer and register with scheduler
// ============================================================================

static AnimationInstance* lottie_player_register(LottiePlayer* lp,
                                                  AnimationScheduler* scheduler,
                                                  double start_time,
                                                  Pool* pool) {
    AnimationInstance* inst = animation_instance_create(scheduler);
    inst->type = ANIM_LOTTIE;
    inst->target = lp->surface;
    inst->state = lp;
    inst->start_time = start_time;
    inst->duration = (double)lp->duration;
    inst->delay = 0;
    inst->iteration_count = lp->loop ? -1 : 1;
    inst->current_iteration = 0;
    inst->direction = ANIM_DIR_NORMAL;
    inst->fill_mode = ANIM_FILL_NONE;
    inst->play_state = ANIM_PLAY_RUNNING;
    inst->timing.type = TIMING_LINEAR;
    inst->tick = lottie_animation_tick;
    inst->on_finish = lottie_animation_finish;

    inst->bounds[0] = 0;
    inst->bounds[1] = 0;
    inst->bounds[2] = (float)lp->width;
    inst->bounds[3] = (float)lp->height;

    animation_scheduler_add(scheduler, inst);
    return inst;
}

static LottiePlayer* lottie_player_init(ImageSurface* surface,
                                          Tvg_Animation tvg_anim,
                                          int render_width, int render_height) {
    // Query animation properties
    float total_frames = 0, duration = 0;
    tvg_animation_get_total_frame(tvg_anim, &total_frames);
    tvg_animation_get_duration(tvg_anim, &duration);

    if (total_frames <= 0 || duration <= 0) {
        log_debug("lottie init: not a valid animation (frames=%.0f, dur=%.1f)", total_frames, duration);
        tvg_animation_del(tvg_anim);
        return NULL;
    }

    // Size the picture to render dimensions
    Tvg_Paint pic = tvg_animation_get_picture(tvg_anim);
    tvg_picture_set_size(pic, (float)render_width, (float)render_height);

    // Create a dedicated SwCanvas for this animation
    Tvg_Canvas canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    if (!canvas) {
        log_error("lottie init: failed to create sw canvas");
        tvg_animation_del(tvg_anim);
        return NULL;
    }

    // Allocate pixel buffer
    uint32_t* pixels = (uint32_t*)mem_calloc(render_width * render_height,
                                              sizeof(uint32_t), MEM_CAT_RENDER);

    tvg_swcanvas_set_target(canvas, pixels, render_width,
                            render_width, render_height, TVG_COLORSPACE_ABGR8888);

    // Push the animation's picture to the canvas
    tvg_canvas_push(canvas, pic);

    LottiePlayer* lp = (LottiePlayer*)mem_calloc(1, sizeof(LottiePlayer), MEM_CAT_RENDER);
    lp->tvg_animation = tvg_anim;
    lp->tvg_canvas = canvas;
    lp->total_frames = total_frames;
    lp->frame_rate = total_frames / duration;
    lp->duration = duration;
    lp->pixels = pixels;
    lp->width = render_width;
    lp->height = render_height;
    lp->surface = surface;
    lp->loop = true;   // default: loop forever (like browsers)
    lp->playing = true;

    // Set first frame and render it
    tvg_animation_set_frame(tvg_anim, 0);
    tvg_canvas_draw(canvas, false);
    tvg_canvas_sync(canvas);

    // Point the surface at our pixels
    if (surface) {
        surface->pixels = pixels;
    }

    log_info("lottie player init: %.0f frames, %.1f fps, %.1fs, %dx%d",
             total_frames, lp->frame_rate, duration, render_width, render_height);

    return lp;
}

// ============================================================================
// Public API
// ============================================================================

AnimationInstance* lottie_player_create_from_file(AnimationScheduler* scheduler,
                                                   ImageSurface* surface,
                                                   const char* path,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool) {
    if (!scheduler || !surface || !path) return NULL;

    Tvg_Animation tvg_anim = tvg_animation_new();
    if (!tvg_anim) {
        log_error("lottie create: failed to create tvg_animation");
        return NULL;
    }

    Tvg_Paint pic = tvg_animation_get_picture(tvg_anim);
    if (tvg_picture_load(pic, path) != TVG_RESULT_SUCCESS) {
        log_debug("lottie create: failed to load %s", path);
        tvg_animation_del(tvg_anim);
        return NULL;
    }

    LottiePlayer* lp = lottie_player_init(surface, tvg_anim, render_width, render_height);
    if (!lp) return NULL;

    return lottie_player_register(lp, scheduler, start_time, pool);
}

AnimationInstance* lottie_player_create_from_data(AnimationScheduler* scheduler,
                                                   ImageSurface* surface,
                                                   const char* data, size_t length,
                                                   int render_width, int render_height,
                                                   double start_time,
                                                   Pool* pool) {
    if (!scheduler || !surface || !data || length == 0) return NULL;

    Tvg_Animation tvg_anim = tvg_animation_new();
    if (!tvg_anim) {
        log_error("lottie create: failed to create tvg_animation");
        return NULL;
    }

    Tvg_Paint pic = tvg_animation_get_picture(tvg_anim);
    if (tvg_picture_load_data(pic, data, (uint32_t)length, "lottie", NULL, false) != TVG_RESULT_SUCCESS) {
        log_debug("lottie create: failed to load from memory (%zu bytes)", length);
        tvg_animation_del(tvg_anim);
        return NULL;
    }

    LottiePlayer* lp = lottie_player_init(surface, tvg_anim, render_width, render_height);
    if (!lp) return NULL;

    return lottie_player_register(lp, scheduler, start_time, pool);
}

// ============================================================================
// Detection helpers
// ============================================================================

bool lottie_detect_by_path(const char* path) {
    if (!path) return false;
    size_t len = strlen(path);

    // Check .json extension
    if (len > 5 && strcmp(path + len - 5, ".json") == 0) return true;

    // Check .lottie extension
    if (len > 7 && strcmp(path + len - 7, ".lottie") == 0) return true;

    return false;
}

bool lottie_detect_by_content(const unsigned char* data, size_t length) {
    if (!data || length < 20) return false;

    // Skip whitespace
    size_t i = 0;
    while (i < length && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r')) i++;

    // Must start with '{'
    if (i >= length || data[i] != '{') return false;

    // Look for Lottie-specific keys in the first 512 bytes
    size_t check_len = length < 512 ? length : 512;
    const char* s = (const char*)data;

    // Lottie JSON typically contains these top-level keys
    bool has_v = false, has_fr = false, has_ip = false;
    for (size_t j = i; j + 4 < check_len; j++) {
        if (s[j] == '"' && s[j+1] == 'v' && s[j+2] == '"') has_v = true;
        if (s[j] == '"' && s[j+1] == 'f' && s[j+2] == 'r' && s[j+3] == '"') has_fr = true;
        if (s[j] == '"' && s[j+1] == 'i' && s[j+2] == 'p' && s[j+3] == '"') has_ip = true;
    }

    return has_v && has_fr && has_ip;
}
