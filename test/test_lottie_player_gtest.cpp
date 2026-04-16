/**
 * Lottie Animation Unit Tests
 *
 * Tests: detection helpers (by path, by content), player lifecycle stubs.
 * Note: ThorVG-dependent tests (create_from_file/data) are lightweight
 * since we can't guarantee Lottie files exist in the test environment.
 */

#include <gtest/gtest.h>
#include <cstring>

#include "../radiant/lottie_player.h"
#include "../radiant/view.hpp"

extern "C" {
#include "../lib/mempool.h"
}

// Stubs for unresolved symbols in standalone test builds
void dirty_mark_rect(DirtyTracker*, float, float, float, float) {}

// ============================================================================
// Lottie Path Detection Tests
// ============================================================================

TEST(LottieDetectPath, NullReturnsFalse) {
    EXPECT_FALSE(lottie_detect_by_path(nullptr));
}

TEST(LottieDetectPath, EmptyReturnsFalse) {
    EXPECT_FALSE(lottie_detect_by_path(""));
}

TEST(LottieDetectPath, JsonExtension) {
    EXPECT_TRUE(lottie_detect_by_path("animation.json"));
    EXPECT_TRUE(lottie_detect_by_path("/path/to/anim.json"));
    EXPECT_TRUE(lottie_detect_by_path("a.json"));
}

TEST(LottieDetectPath, LottieExtension) {
    EXPECT_TRUE(lottie_detect_by_path("animation.lottie"));
    EXPECT_TRUE(lottie_detect_by_path("/path/to/file.lottie"));
}

TEST(LottieDetectPath, OtherExtensions) {
    EXPECT_FALSE(lottie_detect_by_path("image.png"));
    EXPECT_FALSE(lottie_detect_by_path("style.css"));
    EXPECT_FALSE(lottie_detect_by_path("data.xml"));
    EXPECT_FALSE(lottie_detect_by_path("video.mp4"));
    EXPECT_FALSE(lottie_detect_by_path("animation.gif"));
}

TEST(LottieDetectPath, NoExtension) {
    EXPECT_FALSE(lottie_detect_by_path("json"));
    EXPECT_FALSE(lottie_detect_by_path("lottie"));
}

// ============================================================================
// Lottie Content Detection Tests
// ============================================================================

TEST(LottieDetectContent, NullReturnsFalse) {
    EXPECT_FALSE(lottie_detect_by_content(nullptr, 0));
    EXPECT_FALSE(lottie_detect_by_content(nullptr, 100));
}

TEST(LottieDetectContent, TooSmallReturnsFalse) {
    unsigned char tiny[] = "{\"v\"}";
    EXPECT_FALSE(lottie_detect_by_content(tiny, 5));
}

TEST(LottieDetectContent, NotJsonReturnsFalse) {
    const char* xml = "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
    EXPECT_FALSE(lottie_detect_by_content((const unsigned char*)xml, strlen(xml)));
}

TEST(LottieDetectContent, ValidLottieJson) {
    // Minimal Lottie-like JSON with required keys: "v", "fr", "ip"
    const char* lottie_json =
        "{ \"v\": \"5.7.4\", \"fr\": 30, \"ip\": 0, \"op\": 60, "
        "\"w\": 512, \"h\": 512, \"nm\": \"test\", \"layers\": [] }";
    EXPECT_TRUE(lottie_detect_by_content((const unsigned char*)lottie_json, strlen(lottie_json)));
}

TEST(LottieDetectContent, MissingOneKey) {
    // Has "v" and "fr" but not "ip"
    const char* partial = "{ \"v\": \"5.7.4\", \"fr\": 30, \"op\": 60 }";
    EXPECT_FALSE(lottie_detect_by_content((const unsigned char*)partial, strlen(partial)));
}

TEST(LottieDetectContent, RegularJsonNotLottie) {
    const char* regular = "{ \"name\": \"test\", \"value\": 42, \"array\": [1, 2, 3] }";
    EXPECT_FALSE(lottie_detect_by_content((const unsigned char*)regular, strlen(regular)));
}

TEST(LottieDetectContent, WithLeadingWhitespace) {
    const char* lottie_json =
        "  \n  { \"v\": \"5.7.4\", \"fr\": 30, \"ip\": 0, \"op\": 60 }";
    EXPECT_TRUE(lottie_detect_by_content((const unsigned char*)lottie_json, strlen(lottie_json)));
}

// ============================================================================
// Lottie Player Create Null/Invalid Args
// ============================================================================

class LottiePlayerTest : public ::testing::Test {
protected:
    Pool* pool;
    AnimationScheduler* scheduler;
    ImageSurface surface;

    void SetUp() override {
        timing_init_presets();
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
        memset(&surface, 0, sizeof(surface));
        surface.width = 100;
        surface.height = 100;
    }

    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        pool_destroy(pool);
    }
};

TEST_F(LottiePlayerTest, CreateFromFileNullArgs) {
    EXPECT_EQ(lottie_player_create_from_file(nullptr, &surface, "a.json", 100, 100, 0.0, pool), nullptr);
    EXPECT_EQ(lottie_player_create_from_file(scheduler, nullptr, "a.json", 100, 100, 0.0, pool), nullptr);
    EXPECT_EQ(lottie_player_create_from_file(scheduler, &surface, nullptr, 100, 100, 0.0, pool), nullptr);
}

TEST_F(LottiePlayerTest, CreateFromDataNullArgs) {
    const char* data = "{}";
    EXPECT_EQ(lottie_player_create_from_data(nullptr, &surface, data, 2, 100, 100, 0.0, pool), nullptr);
    EXPECT_EQ(lottie_player_create_from_data(scheduler, nullptr, data, 2, 100, 100, 0.0, pool), nullptr);
    EXPECT_EQ(lottie_player_create_from_data(scheduler, &surface, nullptr, 0, 100, 100, 0.0, pool), nullptr);
}

TEST_F(LottiePlayerTest, CreateFromFileNonExistent) {
    // Should fail gracefully — file doesn't exist
    AnimationInstance* inst = lottie_player_create_from_file(
        scheduler, &surface, "/nonexistent/anim.json", 100, 100, 0.0, pool);
    EXPECT_EQ(inst, nullptr);
}

TEST_F(LottiePlayerTest, CreateFromDataInvalidJson) {
    // Invalid JSON — not a Lottie animation
    const char* bad = "this is not json";
    AnimationInstance* inst = lottie_player_create_from_data(
        scheduler, &surface, bad, strlen(bad), 100, 100, 0.0, pool);
    EXPECT_EQ(inst, nullptr);
}

TEST_F(LottiePlayerTest, TickWithNullState) {
    // Ensure tick doesn't crash with null state
    AnimationInstance dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.state = nullptr;
    lottie_animation_tick(&dummy, 0.5f); // should not crash
}

TEST_F(LottiePlayerTest, FinishWithNullState) {
    // Ensure finish doesn't crash with null state
    AnimationInstance dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.state = nullptr;
    lottie_animation_finish(&dummy); // should not crash
}
