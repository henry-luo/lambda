#include "view.hpp"
#include "../lib/log.h"
#include <math.h>
#include <limits.h>

// forward declaration — defined in state_store.cpp
extern void dirty_mark_rect(DirtyTracker* tracker, float x, float y, float width, float height);

// ============================================================================
// Timing Function Implementation
// ============================================================================

// Cubic bezier helper functions (ported from ThorVG tvgLottieInterpolator.cpp,
// MIT license — see mac-deps/thorvg/src/loaders/lottie/tvgLottieInterpolator.cpp)

#define SPLINE_TABLE_SIZE 11
#define SAMPLE_STEP_SIZE (1.0f / (float)(SPLINE_TABLE_SIZE - 1))
#define NEWTON_MIN_SLOPE 0.02f
#define NEWTON_ITERATIONS 4
#define SUBDIVISION_PRECISION 0.0000001f
#define SUBDIVISION_MAX_ITERATIONS 10

static inline float bezier_A(float a1, float a2) { return 1.0f - 3.0f * a2 + 3.0f * a1; }
static inline float bezier_B(float a1, float a2) { return 3.0f * a2 - 6.0f * a1; }
static inline float bezier_C(float a1) { return 3.0f * a1; }

static inline float bezier_calc(float t, float a1, float a2) {
    return ((bezier_A(a1, a2) * t + bezier_B(a1, a2)) * t + bezier_C(a1)) * t;
}

static inline float bezier_slope(float t, float a1, float a2) {
    return 3.0f * bezier_A(a1, a2) * t * t + 2.0f * bezier_B(a1, a2) * t + bezier_C(a1);
}

static float bezier_newton_raphson(float aX, float guessT, float x1, float x2) {
    for (int i = 0; i < NEWTON_ITERATIONS; i++) {
        float slope = bezier_slope(guessT, x1, x2);
        if (slope == 0.0f) return guessT;
        float currentX = bezier_calc(guessT, x1, x2) - aX;
        guessT -= currentX / slope;
    }
    return guessT;
}

static float bezier_binary_subdivide(float aX, float aA, float aB, float x1, float x2) {
    float x, t;
    int i = 0;
    do {
        t = aA + (aB - aA) / 2.0f;
        x = bezier_calc(t, x1, x2) - aX;
        if (x > 0.0f) aB = t;
        else aA = t;
    } while (fabsf(x) > SUBDIVISION_PRECISION && ++i < SUBDIVISION_MAX_ITERATIONS);
    return t;
}

static float bezier_get_t_for_x(float aX, const float* samples, float x1, float x2) {
    // find interval where t lies
    float intervalStart = 0.0f;
    int currentSample = 1;
    int lastSample = SPLINE_TABLE_SIZE - 1;

    for (; currentSample < lastSample && samples[currentSample] <= aX; currentSample++) {
        intervalStart += SAMPLE_STEP_SIZE;
    }
    currentSample--;

    // interpolate to provide initial guess for t
    float dist = (aX - samples[currentSample]) / (samples[currentSample + 1] - samples[currentSample]);
    float guessT = intervalStart + dist * SAMPLE_STEP_SIZE;

    float initialSlope = bezier_slope(guessT, x1, x2);
    if (initialSlope >= NEWTON_MIN_SLOPE) return bezier_newton_raphson(aX, guessT, x1, x2);
    else if (initialSlope == 0.0f) return guessT;
    else return bezier_binary_subdivide(aX, intervalStart, intervalStart + SAMPLE_STEP_SIZE, x1, x2);
}

void timing_cubic_bezier_init(TimingFunction* tf, float x1, float y1, float x2, float y2) {
    tf->type = TIMING_CUBIC_BEZIER;
    tf->bezier.x1 = x1;
    tf->bezier.y1 = y1;
    tf->bezier.x2 = x2;
    tf->bezier.y2 = y2;

    // pre-compute spline sample table
    for (int i = 0; i < SPLINE_TABLE_SIZE; i++) {
        tf->bezier.samples[i] = bezier_calc((float)i * SAMPLE_STEP_SIZE, x1, x2);
    }
}

static float timing_eval_linear(float t) {
    return t;
}

static float timing_eval_cubic_bezier(const TimingFunction* tf, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    // linear shortcut: if control points lie on the diagonal
    if (tf->bezier.x1 == tf->bezier.y1 && tf->bezier.x2 == tf->bezier.y2) return t;

    float tForX = bezier_get_t_for_x(t, tf->bezier.samples, tf->bezier.x1, tf->bezier.x2);
    return bezier_calc(tForX, tf->bezier.y1, tf->bezier.y2);
}

static float timing_eval_steps(const TimingFunction* tf, float t) {
    int n = tf->steps.count;
    if (n <= 0) return t;

    float step;
    switch (tf->steps.position) {
        case STEP_JUMP_START:
            step = (floorf(t * (float)n) + 1.0f) / (float)n;
            break;
        case STEP_JUMP_END:
            step = floorf(t * (float)n) / (float)n;
            break;
        case STEP_JUMP_BOTH:
            step = (floorf(t * (float)n) + 1.0f) / ((float)n + 1.0f);
            break;
        case STEP_JUMP_NONE:
            if (n <= 1) return t;
            step = floorf(t * (float)n) / ((float)n - 1.0f);
            break;
        default:
            step = floorf(t * (float)n) / (float)n;
            break;
    }
    step = clamp_unit(step);
    return step;
}

float timing_function_eval(const TimingFunction* tf, float t) {
    switch (tf->type) {
        case TIMING_LINEAR: return timing_eval_linear(t);
        case TIMING_CUBIC_BEZIER: return timing_eval_cubic_bezier(tf, t);
        case TIMING_STEPS: return timing_eval_steps(tf, t);
    }
    return t;
}

// ============================================================================
// Built-in CSS Easing Presets
// ============================================================================

TimingFunction TIMING_EASE;
TimingFunction TIMING_EASE_IN;
TimingFunction TIMING_EASE_OUT;
TimingFunction TIMING_EASE_IN_OUT;

void timing_init_presets() {
    timing_cubic_bezier_init(&TIMING_EASE,         0.25f, 0.1f,  0.25f, 1.0f);
    timing_cubic_bezier_init(&TIMING_EASE_IN,      0.42f, 0.0f,  1.0f,  1.0f);
    timing_cubic_bezier_init(&TIMING_EASE_OUT,     0.0f,  0.0f,  0.58f, 1.0f);
    timing_cubic_bezier_init(&TIMING_EASE_IN_OUT,  0.42f, 0.0f,  0.58f, 1.0f);
}

// ============================================================================
// Animation Scheduler
// ============================================================================

AnimationScheduler* animation_scheduler_create(Pool* pool) {
    AnimationScheduler* scheduler = (AnimationScheduler*)pool_calloc(pool, sizeof(AnimationScheduler));
    if (!scheduler) return nullptr;
    scheduler->pool = pool;
    scheduler->first = nullptr;
    scheduler->last = nullptr;
    scheduler->count = 0;
    scheduler->current_time = 0.0;
    scheduler->has_active_animations = false;
    scheduler->host_time_anchored = false;
    return scheduler;
}

void animation_scheduler_destroy(AnimationScheduler* scheduler) {
    if (!scheduler) return;
    // walk the list and free all instances
    AnimationInstance* anim = scheduler->first;
    while (anim) {
        AnimationInstance* next = anim->next;
        // Media players keep decoded frames outside the document pool; cancel
        // them before their scheduler entry disappears at document teardown.
        // CSS event targets are being torn down, so their cancel callbacks
        // must release state without dispatching into the dead document.
        anim->suppress_cancel_event = true;
        if (anim->on_cancel) anim->on_cancel(anim);
        pool_free(scheduler->pool, anim);
        anim = next;
    }
    scheduler->first = nullptr;
    scheduler->last = nullptr;
    scheduler->count = 0;
    scheduler->has_active_animations = false;
    // scheduler itself was pool-allocated, freed when pool is destroyed
}

void animation_scheduler_anchor_host_time(AnimationScheduler* scheduler, double now) {
    if (!scheduler) return;
    if (!scheduler->host_time_anchored) {
        // Layout uses time zero before the host clock exists; preserve each
        // animation's elapsed time when its document first joins that clock.
        double offset = now - scheduler->current_time;
        for (AnimationInstance* anim = scheduler->first; anim; anim = anim->next) {
            anim->start_time += offset;
            anim->pause_time += offset;
        }
        scheduler->host_time_anchored = true;
    }
    scheduler->current_time = now;
}

AnimationInstance* animation_instance_create(AnimationScheduler* scheduler) {
    AnimationInstance* anim = (AnimationInstance*)pool_calloc(scheduler->pool, sizeof(AnimationInstance));
    if (!anim) return nullptr;
    anim->play_state = ANIM_PLAY_RUNNING;
    anim->iteration_count = 1;
    anim->timing.type = TIMING_LINEAR;
    return anim;
}

void animation_scheduler_add(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (!scheduler || !anim) return;

    // append to end of doubly-linked list
    anim->prev = scheduler->last;
    anim->next = nullptr;
    if (scheduler->last) {
        scheduler->last->next = anim;
    } else {
        scheduler->first = anim;
    }
    scheduler->last = anim;
    scheduler->count++;
    scheduler->has_active_animations = true;

    log_debug("anim: added animation type=%d target=%p duration=%.3fs count=%g (total active: %d)",
              anim->type, anim->target, anim->duration, anim->iteration_count, scheduler->count);
}

static void animation_scheduler_unlink(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (anim->prev) anim->prev->next = anim->next;
    else scheduler->first = anim->next;

    if (anim->next) anim->next->prev = anim->prev;
    else scheduler->last = anim->prev;
    anim->prev = nullptr;
    anim->next = nullptr;
}

void animation_scheduler_move_before(AnimationScheduler* scheduler, AnimationInstance* anim,
                                     AnimationInstance* before) {
    if (!scheduler || !anim || anim == before || anim->next == before) return;
    animation_scheduler_unlink(scheduler, anim);
    anim->next = before;
    anim->prev = before ? before->prev : scheduler->last;
    if (anim->prev) anim->prev->next = anim;
    else scheduler->first = anim;
    if (before) before->prev = anim;
    else scheduler->last = anim;
}

void animation_scheduler_remove(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (!scheduler || !anim) return;
    animation_scheduler_unlink(scheduler, anim);

    scheduler->count--;
    if (scheduler->count == 0) {
        scheduler->has_active_animations = false;
    }

    log_debug("anim: removed animation type=%d target=%p (remaining: %d)",
              anim->type, anim->target, scheduler->count);

    pool_free(scheduler->pool, anim);
}

void animation_scheduler_cancel(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (!scheduler || !anim) return;
    if (anim->on_cancel) anim->on_cancel(anim);
    animation_scheduler_remove(scheduler, anim);
}

void animation_scheduler_remove_by_target(AnimationScheduler* scheduler, void* target) {
    if (!scheduler || !target) return;

    AnimationInstance* anim = scheduler->first;
    while (anim) {
        AnimationInstance* next = anim->next;
        if (anim->target == target) {
            animation_scheduler_cancel(scheduler, anim);
        }
        anim = next;
    }
}

void animation_scheduler_remove_views(AnimationScheduler* scheduler) {
    if (!scheduler) return;

    AnimationInstance* anim = scheduler->first;
    while (anim) {
        AnimationInstance* next = anim->next;
        // CSS animations/transitions hold a View* target in the (now-freed) view pool;
        // GIF/Lottie target surfaces in the image cache and must be left running.
        if (anim->type == ANIM_CSS_ANIMATION || anim->type == ANIM_CSS_TRANSITION) {
            // Relayout invalidates live animation views before their natural end;
            // cancellation callbacks preserve the DOM event lifecycle.
            animation_scheduler_cancel(scheduler, anim);
        }
        anim = next;
    }
}

void animation_scheduler_prune_disconnected_css_views(AnimationScheduler* scheduler,
                                                       DomDocument* document) {
    if (!scheduler || !document || !document->root) return;

    for (AnimationInstance* anim = scheduler->first; anim; ) {
        AnimationInstance* next = anim->next;
        if (anim->type == ANIM_CSS_ANIMATION || anim->type == ANIM_CSS_TRANSITION) {
            DomElement* element = static_cast<DomElement*>(anim->target);
            bool connected = element && element->doc == document;
            for (DomNode* node = connected ? static_cast<DomNode*>(element) : nullptr;
                 connected && node; node = node->parent) {
                if (node == static_cast<DomNode*>(document->root)) break;
                if (!node->parent) connected = false;
            }
            if (!connected) {
                // A detached DOM target cannot participate in a later layout epoch.
                animation_scheduler_cancel(scheduler, anim);
            }
        }
        anim = next;
    }
}

// ============================================================================
// Animation Tick
// ============================================================================

// Compute normalized progress for an animation at the given time
static float compute_animation_progress(AnimationInstance* anim, double now) {
    double elapsed = now - anim->start_time;
    anim->active_time = elapsed - anim->delay;

    // still in delay period
    if (elapsed < anim->delay) {
        if (anim->fill_mode == ANIM_FILL_BACKWARDS || anim->fill_mode == ANIM_FILL_BOTH) {
            anim->current_iteration = 0;
            return anim->direction == ANIM_DIR_REVERSE ||
                anim->direction == ANIM_DIR_ALTERNATE_REVERSE ? 1.0f : 0.0f;
        }
        return -1.0f; // not yet active
    }

    double active_time = elapsed - anim->delay;

    double overall = anim->duration > 0.0 ? active_time / anim->duration
        : (anim->iteration_count >= 0.0 ? anim->iteration_count : 1.0);
    bool finished = anim->duration <= 0.0 ||
        (anim->iteration_count >= 0.0 && overall >= anim->iteration_count);
    if (finished) {
        if (anim->iteration_count >= 0.0) overall = anim->iteration_count;
        anim->play_state = ANIM_PLAY_FINISHED;
    }
    double whole = floor(overall);
    double iteration_progress = overall - whole;
    // integral active ends belong to the end of the previous iteration.
    if (finished && overall > 0.0 && iteration_progress == 0.0) {
        whole -= 1.0;
        iteration_progress = 1.0;
    }
    int iteration = (int)fmin(whole, (double)INT_MAX); // INT_CAST_OK: animation iteration counter
    anim->current_iteration = iteration;
    if (finished && anim->fill_mode != ANIM_FILL_FORWARDS &&
        anim->fill_mode != ANIM_FILL_BOTH) return -1.0f;
    float t = (float)iteration_progress;

    // apply direction
    bool is_reverse = false;
    bool odd_iteration = fmod(whole, 2.0) != 0.0;
    switch (anim->direction) {
        case ANIM_DIR_NORMAL: break;
        case ANIM_DIR_REVERSE: is_reverse = true; break;
        case ANIM_DIR_ALTERNATE: is_reverse = odd_iteration; break;
        case ANIM_DIR_ALTERNATE_REVERSE: is_reverse = !odd_iteration; break;
    }
    if (is_reverse) t = 1.0f - t;

    return t;
}

static void animation_notify_finished(AnimationInstance* anim) {
    if (anim->finish_notified) return;
    anim->finish_notified = true;
    if (anim->on_finish) anim->on_finish(anim);
}

bool animation_scheduler_tick(AnimationScheduler* scheduler, double now,
                              DirtyTracker* dirty_tracker, bool force_css_sample,
                              AnimationInstance* only) {
    if (!scheduler || scheduler->count == 0) {
        if (scheduler) {
            scheduler->has_active_animations = false;
            scheduler->needs_layout = false;
        }
        return false;
    }

    scheduler->current_time = now;
    bool any_active = only ? scheduler->has_active_animations : false;
    if (!only) scheduler->needs_layout = false;

    AnimationInstance* anim = scheduler->first;
    while (anim) {
        AnimationInstance* next = anim->next;
        // a style pass samples its own effects without overwriting pending targets.
        if (only && anim != only) {
            anim = next;
            continue;
        }

        bool css_animation = anim->type == ANIM_CSS_ANIMATION;
        bool paused = anim->play_state == ANIM_PLAY_PAUSED;
        if (paused && (!css_animation || (anim->sampled && !force_css_sample))) {
            any_active = true;
            anim = next;
            continue;
        }
        if (css_animation && anim->play_state == ANIM_PLAY_FINISHED && !force_css_sample) {
            anim = next;
            continue;
        }
        if (anim->play_state == ANIM_PLAY_FINISHED && !css_animation) {
            // finished animations with fill mode stay in the list but don't tick
            if (anim->fill_mode == ANIM_FILL_FORWARDS || anim->fill_mode == ANIM_FILL_BOTH) {
                anim = next;
                continue;
            }
            // no fill — call finish callback and remove
            if (anim->on_finish) anim->on_finish(anim);
            animation_scheduler_remove(scheduler, anim);
            anim = next;
            continue;
        }

        // compute progress
        if (css_animation && anim->play_state == ANIM_PLAY_FINISHED) {
            anim->play_state = ANIM_PLAY_RUNNING;
        }
        float raw_t = compute_animation_progress(anim, paused ? anim->pause_time : now);
        anim->sampled = true;
        bool finished = anim->play_state == ANIM_PLAY_FINISHED;
        if (!finished) anim->finish_notified = false;
        if (paused) anim->play_state = ANIM_PLAY_PAUSED;

        if (raw_t < 0.0f) {
            // not yet active (in delay, no fill-backwards) or finished (no fill)
            if (finished) animation_notify_finished(anim);
            else any_active = true;
            if (finished && !css_animation) {
                animation_scheduler_remove(scheduler, anim);
            }
            anim = next;
            continue;
        }

        // CSS animation easing belongs to each property's keyframe interval.
        float eased_t = css_animation ? raw_t : timing_function_eval(&anim->timing, raw_t);

        // save previous bounds before tick updates them (needed to clear
        // the old visual position when transforms move the element)
        float prev_bounds[4] = {anim->bounds[0], anim->bounds[1],
                                anim->bounds[2], anim->bounds[3]};

        // call the tick callback to apply the animated value
        if (anim->tick) {
            anim->tick(anim, eased_t);
        }
        // style-time samples are consumed by the current layout pass.
        if (!only && anim->layout_changed) scheduler->needs_layout = true;
        anim->layout_changed = false;

        // mark dirty region for both old and new bounds (the old position
        // must be repainted to clear the previous frame's content)
        if (dirty_tracker) {
            if (prev_bounds[2] > 0.0f || prev_bounds[3] > 0.0f) {
                dirty_mark_rect(dirty_tracker,
                                prev_bounds[0], prev_bounds[1],
                                prev_bounds[2], prev_bounds[3]);
            }
            if (anim->bounds[2] > 0.0f || anim->bounds[3] > 0.0f) {
                dirty_mark_rect(dirty_tracker,
                                anim->bounds[0], anim->bounds[1],
                                anim->bounds[2], anim->bounds[3]);
            }
        }

        // media tick callbacks may finish their own lifetime independently of duration.
        if (finished || anim->play_state == ANIM_PLAY_FINISHED) {
            animation_notify_finished(anim);
            if (!css_animation && anim->fill_mode != ANIM_FILL_FORWARDS && anim->fill_mode != ANIM_FILL_BOTH) {
                animation_scheduler_remove(scheduler, anim);
                anim = next;
                continue;
            }
        } else {
            any_active = true;
        }

        anim = next;
    }

    scheduler->has_active_animations = any_active && scheduler->count > 0;
    return scheduler->has_active_animations;
}

// ============================================================================
// Pause / Resume
// ============================================================================

void animation_instance_pause(AnimationInstance* anim, double now) {
    if (!anim || anim->play_state != ANIM_PLAY_RUNNING) return;
    anim->play_state = ANIM_PLAY_PAUSED;
    anim->pause_time = now;
    log_debug("anim: paused animation type=%d target=%p", anim->type, anim->target);
}

void animation_instance_resume(AnimationInstance* anim, double now) {
    if (!anim || anim->play_state != ANIM_PLAY_PAUSED) return;
    double pause_duration = now - anim->pause_time;
    anim->start_time += pause_duration;
    anim->play_state = ANIM_PLAY_RUNNING;
    log_debug("anim: resumed animation type=%d target=%p (paused %.3fs)", anim->type, anim->target, pause_duration);
}
