/**
 * Animation Scheduler Unit Tests
 *
 * Tests: timing functions (cubic-bezier, steps, linear),
 * animation scheduler lifecycle (create, add, tick, remove, destroy),
 * animation progress computation (direction, iteration, fill-mode),
 * pause/resume.
 */

#include <gtest/gtest.h>
#include <cmath>

#include "../radiant/animation.h"

// Stub for dirty_mark_rect (defined in state_store.cpp, not linked in this test)
void dirty_mark_rect(DirtyTracker*, float, float, float, float) {}

// ============================================================================
// Timing Function Tests
// ============================================================================

class TimingFunctionTest : public ::testing::Test {
protected:
    void SetUp() override {
        timing_init_presets();
    }
};

TEST_F(TimingFunctionTest, LinearIdentity) {
    TimingFunction tf;
    tf.type = TIMING_LINEAR;

    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.25f), 0.25f);
}

TEST_F(TimingFunctionTest, CubicBezierLinearDiagonal) {
    // cubic-bezier(0.5, 0.5, 0.5, 0.5) is effectively linear
    TimingFunction tf;
    timing_cubic_bezier_init(&tf, 0.5f, 0.5f, 0.5f, 0.5f);

    EXPECT_NEAR(timing_function_eval(&tf, 0.0f), 0.0f, 0.001f);
    EXPECT_NEAR(timing_function_eval(&tf, 0.5f), 0.5f, 0.001f);
    EXPECT_NEAR(timing_function_eval(&tf, 1.0f), 1.0f, 0.001f);
}

TEST_F(TimingFunctionTest, CubicBezierEase) {
    // CSS ease: cubic-bezier(0.25, 0.1, 0.25, 1.0)
    // Should start slow, end slow, overshoot slightly
    float v0 = timing_function_eval(&TIMING_EASE, 0.0f);
    float v25 = timing_function_eval(&TIMING_EASE, 0.25f);
    float v50 = timing_function_eval(&TIMING_EASE, 0.5f);
    float v75 = timing_function_eval(&TIMING_EASE, 0.75f);
    float v100 = timing_function_eval(&TIMING_EASE, 1.0f);

    EXPECT_NEAR(v0, 0.0f, 0.001f);
    EXPECT_NEAR(v100, 1.0f, 0.001f);
    // ease is not linear — mid-point should not be 0.5
    EXPECT_GT(v50, 0.5f);  // ease is faster in the middle
    // monotonically increasing
    EXPECT_LT(v0, v25);
    EXPECT_LT(v25, v50);
    EXPECT_LT(v50, v75);
    EXPECT_LT(v75, v100);
}

TEST_F(TimingFunctionTest, CubicBezierEaseInStartsSlow) {
    // ease-in: cubic-bezier(0.42, 0, 1, 1)
    float v25 = timing_function_eval(&TIMING_EASE_IN, 0.25f);
    float v50 = timing_function_eval(&TIMING_EASE_IN, 0.5f);

    // ease-in starts slow: at t=0.25, output should be less than 0.25
    EXPECT_LT(v25, 0.25f);
    // at t=0.5, should be less than 0.5 (still catching up)
    EXPECT_LT(v50, 0.5f);
}

TEST_F(TimingFunctionTest, CubicBezierEaseOutEndsSlow) {
    // ease-out: cubic-bezier(0, 0, 0.58, 1)
    float v50 = timing_function_eval(&TIMING_EASE_OUT, 0.5f);
    float v75 = timing_function_eval(&TIMING_EASE_OUT, 0.75f);

    // ease-out starts fast: at t=0.5, output should be greater than 0.5
    EXPECT_GT(v50, 0.5f);
    EXPECT_GT(v75, 0.75f);
}

TEST_F(TimingFunctionTest, CubicBezierBoundsClamping) {
    TimingFunction tf;
    timing_cubic_bezier_init(&tf, 0.25f, 0.1f, 0.25f, 1.0f);

    // out of bounds should clamp
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, -0.5f), 0.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 1.5f), 1.0f);
}

TEST_F(TimingFunctionTest, StepsJumpEnd) {
    TimingFunction tf;
    tf.type = TIMING_STEPS;
    tf.steps.count = 4;
    tf.steps.position = STEP_JUMP_END;

    // steps(4, jump-end): 0→0, 0.25→0.25, 0.5→0.5, 0.75→0.75, 1.0→1.0
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.24f), 0.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.49f), 0.25f);
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 1.0f), 1.0f);
}

TEST_F(TimingFunctionTest, StepsJumpStart) {
    TimingFunction tf;
    tf.type = TIMING_STEPS;
    tf.steps.count = 4;
    tf.steps.position = STEP_JUMP_START;

    // steps(4, jump-start): immediately jumps to 0.25
    EXPECT_FLOAT_EQ(timing_function_eval(&tf, 0.0f), 0.0f);
    float v01 = timing_function_eval(&tf, 0.01f);
    EXPECT_FLOAT_EQ(v01, 0.25f);
}

// ============================================================================
// Animation Scheduler Tests
// ============================================================================

class AnimationSchedulerTest : public ::testing::Test {
protected:
    Pool* pool;
    AnimationScheduler* scheduler;

    void SetUp() override {
        timing_init_presets();
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
    }

    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        pool_destroy(pool);
    }
};

TEST_F(AnimationSchedulerTest, CreateDestroy) {
    ASSERT_NE(scheduler, nullptr);
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_FALSE(scheduler->has_active_animations);
}

TEST_F(AnimationSchedulerTest, AddRemove) {
    AnimationInstance* anim = animation_instance_create(scheduler);
    ASSERT_NE(anim, nullptr);

    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;

    animation_scheduler_add(scheduler, anim);
    EXPECT_EQ(scheduler->count, 1);
    EXPECT_TRUE(scheduler->has_active_animations);

    animation_scheduler_remove(scheduler, anim);
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_FALSE(scheduler->has_active_animations);
}

// Track values from tick callback
static float g_last_tick_value = -1.0f;
static int g_tick_count = 0;
static int g_finish_count = 0;

static void test_tick_fn(AnimationInstance* anim, float t) {
    g_last_tick_value = t;
    g_tick_count++;
}

static void test_finish_fn(AnimationInstance* anim) {
    g_finish_count++;
}

TEST_F(AnimationSchedulerTest, TickLinearAnimation) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;
    g_finish_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 1;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;
    anim->on_finish = test_finish_fn;

    animation_scheduler_add(scheduler, anim);

    // tick at t=0.5 → progress should be 0.5
    animation_scheduler_tick(scheduler, 0.5, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.5f, 0.001f);
    EXPECT_EQ(g_tick_count, 1);

    // tick at t=0.75 → progress should be 0.75
    animation_scheduler_tick(scheduler, 0.75, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.75f, 0.001f);
    EXPECT_EQ(g_tick_count, 2);

    // tick at t=1.5 → animation should finish
    animation_scheduler_tick(scheduler, 1.5, nullptr);
    EXPECT_EQ(g_finish_count, 1);
    EXPECT_EQ(scheduler->count, 0);
}

TEST_F(AnimationSchedulerTest, TickWithDelay) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.5;  // 500ms delay
    anim->iteration_count = 1;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;

    animation_scheduler_add(scheduler, anim);

    // tick at t=0.25 — still in delay, no tick should happen
    animation_scheduler_tick(scheduler, 0.25, nullptr);
    EXPECT_EQ(g_tick_count, 0);

    // tick at t=1.0 — active_time = 0.5, progress = 0.5
    animation_scheduler_tick(scheduler, 1.0, nullptr);
    EXPECT_EQ(g_tick_count, 1);
    EXPECT_NEAR(g_last_tick_value, 0.5f, 0.001f);
}

TEST_F(AnimationSchedulerTest, TickReverseDirection) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 1;
    anim->direction = ANIM_DIR_REVERSE;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;

    animation_scheduler_add(scheduler, anim);

    // tick at t=0.25 → reverse: progress = 1.0 - 0.25 = 0.75
    animation_scheduler_tick(scheduler, 0.25, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.75f, 0.001f);

    // tick at t=0.75 → reverse: progress = 1.0 - 0.75 = 0.25
    animation_scheduler_tick(scheduler, 0.75, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.25f, 0.001f);
}

TEST_F(AnimationSchedulerTest, TickAlternateDirection) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 3;
    anim->direction = ANIM_DIR_ALTERNATE;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;

    animation_scheduler_add(scheduler, anim);

    // iteration 0 (normal): t=0.5 → progress 0.5
    animation_scheduler_tick(scheduler, 0.5, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.5f, 0.001f);

    // iteration 1 (reverse): t=1.5 → active=1.5, iter=1, prog=0.5, reversed=0.5
    animation_scheduler_tick(scheduler, 1.5, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.5f, 0.001f);

    // iteration 1 (reverse): t=1.25 → active=1.25, iter=1, prog=0.25, reversed=0.75
    animation_scheduler_tick(scheduler, 1.25, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.75f, 0.001f);
}

TEST_F(AnimationSchedulerTest, InfiniteIterations) {
    g_tick_count = 0;
    g_finish_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = -1;  // infinite
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;
    anim->on_finish = test_finish_fn;

    animation_scheduler_add(scheduler, anim);

    // should keep ticking forever
    for (int i = 0; i < 100; i++) {
        animation_scheduler_tick(scheduler, (double)i * 0.5, nullptr);
    }
    EXPECT_EQ(g_tick_count, 100);
    EXPECT_EQ(g_finish_count, 0);
    EXPECT_EQ(scheduler->count, 1);
}

TEST_F(AnimationSchedulerTest, FillModeForwards) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;
    g_finish_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 1;
    anim->fill_mode = ANIM_FILL_FORWARDS;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;
    anim->on_finish = test_finish_fn;

    animation_scheduler_add(scheduler, anim);

    // tick past end
    animation_scheduler_tick(scheduler, 1.5, nullptr);
    EXPECT_EQ(g_finish_count, 1);

    // animation should still be in the list (fill-forwards keeps it)
    EXPECT_EQ(scheduler->count, 1);

    // ticking again should not call tick again (finished with fill)
    int prev_tick = g_tick_count;
    animation_scheduler_tick(scheduler, 2.0, nullptr);
    EXPECT_EQ(g_tick_count, prev_tick);
}

TEST_F(AnimationSchedulerTest, PauseResume) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 2.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 1;
    anim->timing.type = TIMING_LINEAR;
    anim->tick = test_tick_fn;

    animation_scheduler_add(scheduler, anim);

    // tick at t=0.5 → progress = 0.25
    animation_scheduler_tick(scheduler, 0.5, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.25f, 0.001f);

    // pause at t=0.5
    animation_instance_pause(anim, 0.5);
    EXPECT_EQ(anim->play_state, ANIM_PLAY_PAUSED);

    // tick at t=1.5 — should not advance (paused)
    int prev_tick = g_tick_count;
    animation_scheduler_tick(scheduler, 1.5, nullptr);
    EXPECT_EQ(g_tick_count, prev_tick);

    // resume at t=1.5 (paused for 1.0s)
    animation_instance_resume(anim, 1.5);
    EXPECT_EQ(anim->play_state, ANIM_PLAY_RUNNING);
    // start_time should be shifted by 1.0s
    EXPECT_NEAR(anim->start_time, 1.0, 0.001);

    // tick at t=2.0 — active_time = 2.0 - 1.0 = 1.0, progress = 0.5
    animation_scheduler_tick(scheduler, 2.0, nullptr);
    EXPECT_NEAR(g_last_tick_value, 0.5f, 0.001f);
}

TEST_F(AnimationSchedulerTest, RemoveByTarget) {
    int targets[3] = {1, 2, 3};

    for (int i = 0; i < 3; i++) {
        AnimationInstance* anim = animation_instance_create(scheduler);
        anim->type = ANIM_CSS_ANIMATION;
        anim->duration = 1.0;
        anim->start_time = 0.0;
        anim->target = &targets[i];
        animation_scheduler_add(scheduler, anim);
    }
    EXPECT_EQ(scheduler->count, 3);

    // remove all animations targeting targets[1]
    animation_scheduler_remove_by_target(scheduler, &targets[1]);
    EXPECT_EQ(scheduler->count, 2);
}

TEST_F(AnimationSchedulerTest, EasedTick) {
    g_last_tick_value = -1.0f;
    g_tick_count = 0;

    AnimationInstance* anim = animation_instance_create(scheduler);
    anim->type = ANIM_CSS_ANIMATION;
    anim->duration = 1.0;
    anim->start_time = 0.0;
    anim->delay = 0.0;
    anim->iteration_count = 1;
    anim->timing = TIMING_EASE_IN;  // ease-in: starts slow
    anim->tick = test_tick_fn;

    animation_scheduler_add(scheduler, anim);

    // tick at t=0.25 → with ease-in, output should be less than 0.25
    animation_scheduler_tick(scheduler, 0.25, nullptr);
    EXPECT_LT(g_last_tick_value, 0.25f);
    EXPECT_GT(g_last_tick_value, 0.0f);
}

TEST_F(AnimationSchedulerTest, MultipleAnimations) {
    static float values[3] = {-1, -1, -1};
    static auto tick0 = [](AnimationInstance* a, float t) { values[0] = t; };
    static auto tick1 = [](AnimationInstance* a, float t) { values[1] = t; };
    static auto tick2 = [](AnimationInstance* a, float t) { values[2] = t; };

    AnimTickFn fns[] = {tick0, tick1, tick2};

    for (int i = 0; i < 3; i++) {
        AnimationInstance* anim = animation_instance_create(scheduler);
        anim->type = ANIM_CSS_ANIMATION;
        anim->duration = 1.0;
        anim->start_time = 0.0;
        anim->delay = 0.0;
        anim->iteration_count = 1;
        anim->timing.type = TIMING_LINEAR;
        anim->tick = fns[i];
        animation_scheduler_add(scheduler, anim);
    }
    EXPECT_EQ(scheduler->count, 3);

    animation_scheduler_tick(scheduler, 0.5, nullptr);
    EXPECT_NEAR(values[0], 0.5f, 0.001f);
    EXPECT_NEAR(values[1], 0.5f, 0.001f);
    EXPECT_NEAR(values[2], 0.5f, 0.001f);
}
