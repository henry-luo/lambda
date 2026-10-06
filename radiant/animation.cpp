#include "view.hpp"
#include "../lib/log.h"
#include <math.h>

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

static float timing_eval_steps(const TimingFunction* tf, float t, bool before) {
    double n = tf->steps.count;
    if (n <= 0) return t;

    // interval count stays fixed; endpoint jumps change the output denominator.
    double step = floor((double)t * n);
    double jumps = n;
    if (tf->steps.position == STEP_JUMP_START || tf->steps.position == STEP_JUMP_BOTH)
        step += 1.0f;
    if (before && (double)t * n == floor((double)t * n)) step -= 1.0;
    if (tf->steps.position == STEP_JUMP_BOTH) jumps += 1.0f;
    if (tf->steps.position == STEP_JUMP_NONE) jumps -= 1.0f;
    if (t >= 0.0f && step < 0.0f) step = 0.0f;
    if (t <= 1.0f && step > jumps) step = jumps;
    return jumps > 0.0 ? (float)(step / jumps) : t;
}

float timing_function_eval(const TimingFunction* tf, float t, bool before) {
    switch (tf->type) {
        case TIMING_LINEAR: return timing_eval_linear(t);
        case TIMING_CUBIC_BEZIER: return timing_eval_cubic_bezier(tf, t);
        case TIMING_STEPS: return timing_eval_steps(tf, t, before);
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

static void animation_scheduler_link_before(AnimationScheduler* scheduler,
    AnimationInstance* anim, AnimationInstance* before) {
    anim->next = before;
    anim->prev = before ? before->prev : scheduler->last;
    if (anim->prev) anim->prev->next = anim;
    else scheduler->first = anim;
    if (before) before->prev = anim;
    else scheduler->last = anim;
}

static void animation_scheduler_unlink(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (anim->prev) anim->prev->next = anim->next;
    else scheduler->first = anim->next;
    if (anim->next) anim->next->prev = anim->prev;
    else scheduler->last = anim->prev;
}

void animation_scheduler_move_before(AnimationScheduler* scheduler,
    AnimationInstance* anim, AnimationInstance* before) {
    if (!scheduler || !anim || anim == before || anim->next == before) return;
    animation_scheduler_unlink(scheduler, anim);
    animation_scheduler_link_before(scheduler, anim, before);
}

void animation_scheduler_add(AnimationScheduler* scheduler, AnimationInstance* anim) {
    if (!scheduler || !anim) return;
    animation_scheduler_link_before(scheduler, anim, nullptr);
    scheduler->count++;
    scheduler->has_active_animations = true;

    log_debug("anim: added animation type=%d target=%p duration=%.3fs count=%.3f (total active: %d)",
              anim->type, anim->target, anim->duration, anim->iteration_count, scheduler->count);
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
static bool animation_keeps_finished(const AnimationInstance* anim) {
    return anim->retain_after_finish || anim->fill_mode == ANIM_FILL_FORWARDS ||
        anim->fill_mode == ANIM_FILL_BOTH;
}

static bool animation_is_reverse(const AnimationInstance* anim) {
    bool odd = fmod(anim->current_iteration, 2.0) != 0.0;
    return anim->direction == ANIM_DIR_REVERSE ||
        (anim->direction == ANIM_DIR_ALTERNATE && odd) ||
        (anim->direction == ANIM_DIR_ALTERNATE_REVERSE && !odd);
}

static bool animation_easing_before(const AnimationInstance* anim) {
    bool reversed = animation_is_reverse(anim);
    bool delayed = anim->sample_time - anim->start_time < anim->delay;
    bool completed = !delayed && (anim->duration <= 0.0 || (anim->iteration_count >= 0.0 &&
        anim->sample_time - anim->start_time - anim->delay >= anim->duration * anim->iteration_count));
    return (delayed && !reversed) || (completed && reversed);
}

static float compute_animation_progress(AnimationInstance* anim, double now) {
    anim->sample_time = now;
    double elapsed = now - anim->start_time;

    // still in delay period
    if (elapsed < anim->delay) {
        if (anim->fill_mode == ANIM_FILL_BACKWARDS || anim->fill_mode == ANIM_FILL_BOTH) {
            return anim->direction == ANIM_DIR_REVERSE ||
                anim->direction == ANIM_DIR_ALTERNATE_REVERSE ? 1.0f : 0.0f;
        }
        return -1.0f; // not yet active
    }

    double active_time = elapsed - anim->delay;

    double cycles = anim->duration > 0.0 ? active_time / anim->duration :
        (anim->iteration_count >= 0.0 ? anim->iteration_count : 1.0);
    bool finished = anim->duration <= 0.0 ||
        (anim->iteration_count >= 0.0 && cycles >= anim->iteration_count);
    if (finished) cycles = anim->iteration_count >= 0.0 ? anim->iteration_count : 1.0;
    double whole = floor(cycles);
    double iteration_progress = cycles - whole;
    if (finished && cycles > 0.0 && iteration_progress == 0.0) {
        whole -= 1.0;
        iteration_progress = 1.0;
    }
    anim->current_iteration = whole;
    if (finished) {
        anim->play_state = ANIM_PLAY_FINISHED;

        // fill-mode forwards/both: hold final value
        if (anim->fill_mode == ANIM_FILL_FORWARDS || anim->fill_mode == ANIM_FILL_BOTH) {
            // determine if final iteration was forward or reverse
            return animation_is_reverse(anim) ? 1.0f - (float)iteration_progress : (float)iteration_progress;
        }
        return -1.0f; // finished, no fill
    }

    float t = (float)iteration_progress;

    // apply direction
    if (animation_is_reverse(anim)) t = 1.0f - t;

    return t;
}

bool animation_scheduler_tick(AnimationScheduler* scheduler, double now,
                              DirtyTracker* dirty_tracker) {
    if (!scheduler || scheduler->count == 0) {
        scheduler->has_active_animations = false;
        return false;
    }

    scheduler->current_time = now;
    bool any_active = false;

    AnimationInstance* anim = scheduler->first;
    while (anim) {
        AnimationInstance* next = anim->next;

        if (anim->play_state == ANIM_PLAY_PAUSED) {
            any_active = true;
            anim = next;
            continue;
        }

        if (anim->play_state == ANIM_PLAY_FINISHED) {
            // finished animations with fill mode stay in the list but don't tick
            if (animation_keeps_finished(anim)) {
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
        float raw_t = compute_animation_progress(anim, now);

        if (raw_t < 0.0f) {
            // not yet active (in delay, no fill-backwards) or finished (no fill)
            if (anim->play_state == ANIM_PLAY_FINISHED) {
                if (anim->on_finish) anim->on_finish(anim);
                if (!animation_keeps_finished(anim)) animation_scheduler_remove(scheduler, anim);
            } else any_active = true;
            anim = next;
            continue;
        }

        // apply easing function
        float eased_t = timing_function_eval(&anim->timing, raw_t, animation_easing_before(anim));

        // save previous bounds before tick updates them (needed to clear
        // the old visual position when transforms move the element)
        float prev_bounds[4] = {anim->bounds[0], anim->bounds[1],
                                anim->bounds[2], anim->bounds[3]};

        // call the tick callback to apply the animated value
        if (anim->tick) {
            anim->tick(anim, eased_t);
        }

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

        if (anim->play_state == ANIM_PLAY_FINISHED) {
            // just finished this tick — fire callback but keep in list for fill
            if (anim->on_finish) anim->on_finish(anim);
            if (!animation_keeps_finished(anim)) {
                animation_scheduler_remove(scheduler, anim);
                anim = next;
                continue;
            }
        } else {
            any_active = true;
        }

        anim = next;
    }

    scheduler->has_active_animations = any_active;
    return any_active;
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

void animation_instance_sample(AnimationInstance* anim, double now) {
    if (!anim) return;
    bool paused = anim->play_state == ANIM_PLAY_PAUSED;
    float progress = compute_animation_progress(anim, paused ? anim->pause_time : now);
    if (paused) anim->play_state = ANIM_PLAY_PAUSED;
    // retained layout replaces view properties without changing the effect's timeline.
    if (progress >= 0.0f && anim->tick)
        anim->tick(anim, timing_function_eval(&anim->timing, progress, animation_easing_before(anim)));
}

void animation_instance_resume(AnimationInstance* anim, double now) {
    if (!anim || anim->play_state != ANIM_PLAY_PAUSED) return;
    double pause_duration = now - anim->pause_time;
    anim->start_time += pause_duration;
    anim->play_state = ANIM_PLAY_RUNNING;
    log_debug("anim: resumed animation type=%d target=%p (paused %.3fs)", anim->type, anim->target, pause_duration);
}
