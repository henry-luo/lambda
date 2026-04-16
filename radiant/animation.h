#ifndef RADIANT_ANIMATION_H
#define RADIANT_ANIMATION_H

#include "../lib/mempool.h"

// Forward declarations
struct DirtyTracker;

// ============================================================================
// Animation Types
// ============================================================================

typedef enum AnimationType {
    ANIM_CSS_ANIMATION = 0,    // CSS @keyframes animation
    ANIM_CSS_TRANSITION,       // CSS transition
    ANIM_GIF,                  // animated GIF frame sequence
    ANIM_LOTTIE,               // Lottie JSON animation
} AnimationType;

typedef enum AnimationDirection {
    ANIM_DIR_NORMAL = 0,       // 0% → 100%
    ANIM_DIR_REVERSE,          // 100% → 0%
    ANIM_DIR_ALTERNATE,        // normal on even iterations, reverse on odd
    ANIM_DIR_ALTERNATE_REVERSE,// reverse on even iterations, normal on odd
} AnimationDirection;

typedef enum AnimationFillMode {
    ANIM_FILL_NONE = 0,       // no styles applied outside active period
    ANIM_FILL_FORWARDS,       // retain end-state after completion
    ANIM_FILL_BACKWARDS,      // apply start-state during delay
    ANIM_FILL_BOTH,           // forwards + backwards
} AnimationFillMode;

typedef enum AnimationPlayState {
    ANIM_PLAY_RUNNING = 0,
    ANIM_PLAY_PAUSED,
    ANIM_PLAY_FINISHED,
} AnimationPlayState;

// ============================================================================
// Timing Functions (CSS easing)
// ============================================================================

typedef enum TimingFunctionType {
    TIMING_LINEAR = 0,
    TIMING_CUBIC_BEZIER,
    TIMING_STEPS,
} TimingFunctionType;

typedef enum StepPosition {
    STEP_JUMP_END = 0,         // default (step-end)
    STEP_JUMP_START,           // step-start
    STEP_JUMP_BOTH,
    STEP_JUMP_NONE,
} StepPosition;

typedef struct TimingFunction {
    TimingFunctionType type;
    union {
        struct {
            float x1, y1, x2, y2;
            float samples[11];     // pre-computed spline table for fast lookup
        } bezier;
        struct {
            int count;
            StepPosition position;
        } steps;
    };
} TimingFunction;

// Initialize a cubic-bezier timing function with pre-computed sample table
void timing_cubic_bezier_init(TimingFunction* tf, float x1, float y1, float x2, float y2);

// Evaluate a timing function: input t ∈ [0,1] → eased output ∈ [0,1]
float timing_function_eval(const TimingFunction* tf, float t);

// Built-in CSS easing presets (call timing_init_presets() first)
extern TimingFunction TIMING_EASE;
extern TimingFunction TIMING_EASE_IN;
extern TimingFunction TIMING_EASE_OUT;
extern TimingFunction TIMING_EASE_IN_OUT;

// Initialize built-in presets (call once at startup)
void timing_init_presets();

// ============================================================================
// Animation Instance
// ============================================================================

typedef struct AnimationInstance AnimationInstance;

// Callback: apply the current animation value at normalized progress t ∈ [0,1]
typedef void (*AnimTickFn)(AnimationInstance* anim, float t);

// Callback: called when animation completes (all iterations done or cancelled)
typedef void (*AnimFinishFn)(AnimationInstance* anim);

struct AnimationInstance {
    AnimationInstance* next;
    AnimationInstance* prev;

    AnimationType type;
    void* target;               // ViewBlock*, ImageSurface*, etc. — type-specific
    void* state;                // type-specific state (CssAnimState*, GifAnimation*, etc.)

    // Timing
    double start_time;          // absolute start time (seconds, monotonic)
    double duration;            // single iteration duration (seconds)
    double delay;               // delay before first iteration (seconds)
    int iteration_count;        // number of iterations (-1 = infinite)
    int current_iteration;      // current iteration (0-based)

    // Direction and fill
    AnimationDirection direction;
    AnimationFillMode fill_mode;
    AnimationPlayState play_state;

    // Easing
    TimingFunction timing;

    // Callbacks
    AnimTickFn tick;
    AnimFinishFn on_finish;

    // Dirty tracking: bounds of the animated element (for marking dirty tiles)
    float bounds[4];            // x, y, w, h in CSS pixels

    // Internal: pause timestamp (set when paused)
    double pause_time;
};

// ============================================================================
// Animation Scheduler
// ============================================================================

typedef struct AnimationScheduler {
    // Doubly-linked list of active animations
    AnimationInstance* first;
    AnimationInstance* last;
    int count;

    // Timing
    double current_time;        // last tick timestamp (seconds, monotonic)
    bool has_active_animations; // true if any animation is running

    // Memory
    Pool* pool;                 // pool for AnimationInstance allocations
} AnimationScheduler;

// Create a new scheduler (pool used for instance allocations)
AnimationScheduler* animation_scheduler_create(Pool* pool);

// Destroy scheduler and all active animation instances
void animation_scheduler_destroy(AnimationScheduler* scheduler);

// Tick all active animations. Returns true if any animation is still active.
// Caller should mark dirty regions based on animation bounds.
bool animation_scheduler_tick(AnimationScheduler* scheduler, double now,
                              DirtyTracker* dirty_tracker);

// Add a new animation instance to the scheduler. Instance must be allocated
// from the scheduler's pool (use animation_instance_create).
void animation_scheduler_add(AnimationScheduler* scheduler, AnimationInstance* anim);

// Remove and free an animation instance.
void animation_scheduler_remove(AnimationScheduler* scheduler, AnimationInstance* anim);

// Remove all animations targeting a specific view (e.g., when element is removed from DOM)
void animation_scheduler_remove_by_target(AnimationScheduler* scheduler, void* target);

// Create a new animation instance from the scheduler's pool
AnimationInstance* animation_instance_create(AnimationScheduler* scheduler);

// Pause/resume
void animation_instance_pause(AnimationInstance* anim, double now);
void animation_instance_resume(AnimationInstance* anim, double now);

#endif // RADIANT_ANIMATION_H
