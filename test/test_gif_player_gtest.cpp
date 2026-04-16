/**
 * GIF Animation Unit Tests
 *
 * Tests: multi-frame GIF decoder (lib/image.h), GifAnimation tick/finish,
 * detection helpers, scheduler integration.
 */

#include <gtest/gtest.h>
#include <cstring>

#include "../radiant/gif_player.h"
#include "../radiant/view.hpp"

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/image.h"
}

// Stubs for unresolved symbols in standalone test builds
void dirty_mark_rect(DirtyTracker*, float, float, float, float) {}

// ============================================================================
// Minimal 2-frame animated GIF (2x2 pixels, red → blue)
// ============================================================================
// Manually constructed GIF89a binary:
//   - Logical screen: 2x2 with global color table (4 colors)
//   - NETSCAPE 2.0 extension (loop count = 0 = infinite)
//   - Frame 0: red (#FF0000), delay 100ms
//   - Frame 1: blue (#0000FF), delay 200ms
//   - Trailer

static const unsigned char ANIM_GIF_2FRAME[] = {
    // Header: GIF89a
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61,
    // Logical Screen Descriptor: 2x2, GCT flag=1, color_res=1, sort=0, GCT_size=1 (4 colors)
    0x02, 0x00, 0x02, 0x00, 0x91, 0x00, 0x00,
    // Global Color Table (4 entries × 3 bytes):
    //   0: red    (FF 00 00)
    //   1: blue   (00 00 FF)
    //   2: green  (00 FF 00)
    //   3: black  (00 00 00)
    0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF,
    0x00, 0xFF, 0x00,
    0x00, 0x00, 0x00,
    // Application Extension: NETSCAPE2.0 (loop count = 0 = infinite)
    0x21, 0xFF, 0x0B,
    0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, // "NETSCAPE2.0"
    0x03, 0x01, 0x00, 0x00, // sub-block: loop count = 0 (infinite)
    0x00, // block terminator
    // --- Frame 0: red ---
    // Graphic Control Extension: disposal=1 (do not dispose), delay=100 (=1000ms/10=100*10ms), transparent=none
    0x21, 0xF9, 0x04, 0x04, 0x0A, 0x00, 0xFF, 0x00,
    // Image Descriptor: 0,0 2x2, no local color table
    0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
    // LZW minimum code size = 2
    0x02,
    // LZW compressed data: all index 0 (red) for 4 pixels
    // Encoded: clear(4), 0, 0, 0, 0, EOI(5)
    0x03, 0x04, 0x01, 0x00, // sub-block: 3 bytes of compressed data
    0x00, // block terminator
    // --- Frame 1: blue ---
    // Graphic Control Extension: disposal=1 (do not dispose), delay=200 (=2000ms/10=200*10ms)
    0x21, 0xF9, 0x04, 0x04, 0x14, 0x00, 0xFF, 0x00,
    // Image Descriptor: 0,0 2x2, no local color table
    0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
    // LZW minimum code size = 2
    0x02,
    // LZW compressed data: all index 1 (blue) for 4 pixels
    0x03, 0x04, 0x05, 0x00, // sub-block: 3 bytes of compressed data
    0x00, // block terminator
    // Trailer
    0x3B
};
static const size_t ANIM_GIF_2FRAME_LEN = sizeof(ANIM_GIF_2FRAME);

// Single-frame GIF (not animated)
static const unsigned char STATIC_GIF[] = {
    // Header: GIF89a
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61,
    // Logical Screen Descriptor: 1x1, GCT flag=1, size=0 (2 colors)
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    // Global Color Table (2 entries): white, black
    0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00,
    // Image Descriptor: 1x1
    0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    // LZW min code size = 2
    0x02,
    // LZW data: clear, 0, EOI
    0x02, 0x44, 0x01,
    0x00, // block terminator
    // Trailer
    0x3B
};
static const size_t STATIC_GIF_LEN = sizeof(STATIC_GIF);

// ============================================================================
// GIF Detection Tests
// ============================================================================

TEST(GifDetection, NullPathReturnsNull) {
    EXPECT_EQ(gif_detect_animated(nullptr), nullptr);
}

TEST(GifDetection, NonGifPathReturnsNull) {
    EXPECT_EQ(gif_detect_animated("test.png"), nullptr);
    EXPECT_EQ(gif_detect_animated("test.jpg"), nullptr);
    EXPECT_EQ(gif_detect_animated("image.bmp"), nullptr);
}

TEST(GifDetection, MemoryNullReturnsNull) {
    EXPECT_EQ(gif_detect_animated_from_memory(nullptr, 0), nullptr);
    EXPECT_EQ(gif_detect_animated_from_memory(nullptr, 100), nullptr);
}

TEST(GifDetection, MemoryTooSmallReturnsNull) {
    unsigned char tiny[] = {0x47, 0x49, 0x46}; // "GIF" only
    EXPECT_EQ(gif_detect_animated_from_memory(tiny, 3), nullptr);
}

TEST(GifDetection, MemoryNonGifMagicReturnsNull) {
    unsigned char png_header[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_EQ(gif_detect_animated_from_memory(png_header, sizeof(png_header)), nullptr);
}

TEST(GifDetection, StaticGifReturnsNull) {
    // Single-frame GIF → image_gif_load_from_memory returns NULL (< 2 frames)
    GifFrames* frames = gif_detect_animated_from_memory(STATIC_GIF, STATIC_GIF_LEN);
    EXPECT_EQ(frames, nullptr);
}

// ============================================================================
// GIF Frame Count Tests (file-based helpers)
// ============================================================================

TEST(GifFrameCount, NullReturnsZero) {
    EXPECT_EQ(image_gif_frame_count(nullptr), 0);
}

TEST(GifFrameCount, NonExistentFileReturnsZero) {
    EXPECT_EQ(image_gif_frame_count("/nonexistent/test.gif"), 0);
}

TEST(GifFrameCount, MemoryStaticGif) {
    int count = image_gif_frame_count_from_memory(STATIC_GIF, STATIC_GIF_LEN);
    EXPECT_EQ(count, 1);
}

// ============================================================================
// GIF Animation Create/Tick Tests
// ============================================================================

class GifAnimationTest : public ::testing::Test {
protected:
    Pool* pool;
    AnimationScheduler* scheduler;
    ImageSurface surface;

    void SetUp() override {
        timing_init_presets();
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
        memset(&surface, 0, sizeof(surface));
        surface.format = IMAGE_FORMAT_GIF;
        surface.width = 4;
        surface.height = 4;
    }

    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        pool_destroy(pool);
    }
};

TEST_F(GifAnimationTest, CreateNullArgsReturnsNull) {
    GifFrames frames;
    memset(&frames, 0, sizeof(frames));
    frames.frame_count = 2;

    EXPECT_EQ(gif_animation_create(nullptr, &surface, &frames, 0.0, pool), nullptr);
    EXPECT_EQ(gif_animation_create(scheduler, nullptr, &frames, 0.0, pool), nullptr);
    EXPECT_EQ(gif_animation_create(scheduler, &surface, nullptr, 0.0, pool), nullptr);
}

TEST_F(GifAnimationTest, CreateSingleFrameReturnsNull) {
    GifFrames frames;
    memset(&frames, 0, sizeof(frames));
    frames.frame_count = 1;

    EXPECT_EQ(gif_animation_create(scheduler, &surface, &frames, 0.0, pool), nullptr);
}

TEST_F(GifAnimationTest, CreateAndTickFrames) {
    // Build synthetic 3-frame GifFrames
    const int W = 2, H = 2;
    uint32_t pixels0[W * H] = {0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF}; // red
    uint32_t pixels1[W * H] = {0x0000FFFF, 0x0000FFFF, 0x0000FFFF, 0x0000FFFF}; // blue
    uint32_t pixels2[W * H] = {0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF}; // green

    GifFrameData frame_data[3];
    frame_data[0] = {pixels0, 100, 1}; // 100ms
    frame_data[1] = {pixels1, 200, 1}; // 200ms
    frame_data[2] = {pixels2, 100, 1}; // 100ms

    // Manually allocate GifFrames (gif_animation_create takes ownership)
    GifFrames* gif = (GifFrames*)malloc(sizeof(GifFrames));
    gif->frames = (GifFrameData*)malloc(sizeof(GifFrameData) * 3);
    memcpy(gif->frames, frame_data, sizeof(GifFrameData) * 3);
    // Override pixel pointers to point to our stack arrays
    // (normally these are heap-allocated but for testing we point to stack)
    gif->frames[0].pixels = pixels0;
    gif->frames[1].pixels = pixels1;
    gif->frames[2].pixels = pixels2;
    gif->frame_count = 3;
    gif->width = W;
    gif->height = H;
    gif->loop_count = 1; // play once

    surface.width = W;
    surface.height = H;

    AnimationInstance* inst = gif_animation_create(scheduler, &surface, gif, 0.0, pool);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->type, ANIM_GIF);
    EXPECT_EQ(inst->play_state, ANIM_PLAY_RUNNING);

    // Initial frame should be frame 0 (red)
    EXPECT_EQ(surface.pixels, pixels0);

    // Total duration = 100+200+100 = 400ms = 0.4s
    EXPECT_NEAR(inst->duration, 0.4, 0.001);

    // Tick at t=0.0 → still frame 0 (not yet past frame_end_time)
    gif_animation_tick(inst, 0.0f);
    EXPECT_EQ(surface.pixels, pixels0);

    // Access GifAnimation state to check internals
    GifAnimation* ga = (GifAnimation*)inst->state;
    ASSERT_NE(ga, nullptr);
    EXPECT_EQ(ga->current_frame, 0);

    // Simulate time advancing past frame 0's end time (100ms from start)
    // We need to manipulate the frame_end_time to test advancement
    ga->frame_end_time = 0.0; // force immediate advance
    gif_animation_tick(inst, 0.3f);
    EXPECT_EQ(ga->current_frame, 1);
    EXPECT_EQ(surface.pixels, pixels1);

    // Force advance to frame 2
    ga->frame_end_time = 0.0;
    gif_animation_tick(inst, 0.8f);
    EXPECT_EQ(ga->current_frame, 2);
    EXPECT_EQ(surface.pixels, pixels2);

    // Force advance past end → with loop_count=1, should finish
    ga->frame_end_time = 0.0;
    gif_animation_tick(inst, 1.0f);
    EXPECT_EQ(inst->play_state, ANIM_PLAY_FINISHED);

    // Clean up: null out pixel pointers before finish (since they're stack arrays)
    gif->frames[0].pixels = nullptr;
    gif->frames[1].pixels = nullptr;
    gif->frames[2].pixels = nullptr;
    gif_animation_finish(inst);
    EXPECT_EQ(inst->state, nullptr);
}

TEST_F(GifAnimationTest, InfiniteLoopDoesNotFinish) {
    const int W = 1, H = 1;
    uint32_t pixels0[1] = {0xFF};
    uint32_t pixels1[1] = {0x00};

    GifFrames* gif = (GifFrames*)malloc(sizeof(GifFrames));
    gif->frames = (GifFrameData*)malloc(sizeof(GifFrameData) * 2);
    gif->frames[0] = {pixels0, 100, 0};
    gif->frames[1] = {pixels1, 100, 0};
    gif->frame_count = 2;
    gif->width = W;
    gif->height = H;
    gif->loop_count = 0; // infinite

    surface.width = W;
    surface.height = H;

    AnimationInstance* inst = gif_animation_create(scheduler, &surface, gif, 0.0, pool);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->iteration_count, -1); // infinite

    GifAnimation* ga = (GifAnimation*)inst->state;

    // Advance past all frames multiple times — should wrap around, never finish
    for (int i = 0; i < 10; i++) {
        ga->frame_end_time = 0.0;
        gif_animation_tick(inst, (float)i / 20.0f);
        EXPECT_NE(inst->play_state, ANIM_PLAY_FINISHED);
    }

    // Clean up
    gif->frames[0].pixels = nullptr;
    gif->frames[1].pixels = nullptr;
    gif_animation_finish(inst);
}

TEST_F(GifAnimationTest, FinishCleansUp) {
    uint32_t pixels0[4] = {0};
    uint32_t pixels1[4] = {0};

    GifFrames* gif = (GifFrames*)malloc(sizeof(GifFrames));
    gif->frames = (GifFrameData*)malloc(sizeof(GifFrameData) * 2);
    gif->frames[0] = {pixels0, 100, 0};
    gif->frames[1] = {pixels1, 100, 0};
    gif->frame_count = 2;
    gif->width = 2;
    gif->height = 2;
    gif->loop_count = 1;

    surface.width = 2;
    surface.height = 2;

    AnimationInstance* inst = gif_animation_create(scheduler, &surface, gif, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    // Override pixel pointers so finish doesn't free stack memory
    gif->frames[0].pixels = nullptr;
    gif->frames[1].pixels = nullptr;

    gif_animation_finish(inst);
    EXPECT_EQ(inst->state, nullptr);
    EXPECT_EQ(surface.pixels, nullptr);
}

// ============================================================================
// GIF Scheduler Integration Tests
// ============================================================================

TEST_F(GifAnimationTest, SchedulerAddsGifAnimation) {
    uint32_t pixels0[1] = {0xFF};
    uint32_t pixels1[1] = {0xAA};

    GifFrames* gif = (GifFrames*)malloc(sizeof(GifFrames));
    gif->frames = (GifFrameData*)malloc(sizeof(GifFrameData) * 2);
    gif->frames[0] = {pixels0, 100, 0};
    gif->frames[1] = {pixels1, 100, 0};
    gif->frame_count = 2;
    gif->width = 1;
    gif->height = 1;
    gif->loop_count = 0;

    surface.width = 1;
    surface.height = 1;

    int initial_count = scheduler->count;
    AnimationInstance* inst = gif_animation_create(scheduler, &surface, gif, 0.0, pool);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(scheduler->count, initial_count + 1);

    // Clean up
    gif->frames[0].pixels = nullptr;
    gif->frames[1].pixels = nullptr;
    gif_animation_finish(inst);
}

TEST_F(GifAnimationTest, BoundsMatchDimensions) {
    uint32_t pixels0[4] = {0};
    uint32_t pixels1[4] = {0};

    GifFrames* gif = (GifFrames*)malloc(sizeof(GifFrames));
    gif->frames = (GifFrameData*)malloc(sizeof(GifFrameData) * 2);
    gif->frames[0] = {pixels0, 50, 0};
    gif->frames[1] = {pixels1, 50, 0};
    gif->frame_count = 2;
    gif->width = 100;
    gif->height = 200;
    gif->loop_count = 0;

    surface.width = 100;
    surface.height = 200;

    AnimationInstance* inst = gif_animation_create(scheduler, &surface, gif, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    EXPECT_FLOAT_EQ(inst->bounds[0], 0.0f);
    EXPECT_FLOAT_EQ(inst->bounds[1], 0.0f);
    EXPECT_FLOAT_EQ(inst->bounds[2], 100.0f);
    EXPECT_FLOAT_EQ(inst->bounds[3], 200.0f);

    gif->frames[0].pixels = nullptr;
    gif->frames[1].pixels = nullptr;
    gif_animation_finish(inst);
}
