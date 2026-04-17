/**
 * RdtVideo Unit Tests
 *
 * Tests: rdt_video lifecycle, MP4 decode (H.264+AAC),
 * frame dimensions, pixel output, audio detection,
 * playback state transitions, intrinsic size reporting.
 *
 * NOTE: AVFoundation requires an NSRunLoop to be pumped for async
 * operations (asset loading, playback). We use CFRunLoopRunInMode()
 * in the wait helper to drive the run loop from this headless test.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "../radiant/rdt_video.h"

// ============================================================================
// Test media path — resolved relative to the executable's working directory
// ============================================================================

static const char* TEST_VIDEO_PATH = "test/media/test_video_audio.mp4";

// ============================================================================
// Helper: poll state until target or timeout
// ============================================================================

static bool wait_for_state(RdtVideo* video, RdtVideoState target, int timeout_ms) {
    int elapsed = 0;
    const int step_ms = 10;
    while (elapsed < timeout_ms) {
        // pump the run loop so AVFoundation can process async operations
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, step_ms / 1000.0, false);
#else
        usleep(step_ms * 1000);
#endif
        if (rdt_video_get_state(video) == target) return true;
        elapsed += step_ms;
    }
    return rdt_video_get_state(video) == target;
}

// pump the run loop for a given duration (ms)
static void pump_runloop_ms(int ms) {
#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, false);
#else
    usleep(ms * 1000);
#endif
}

// ============================================================================
// Test Fixture
// ============================================================================

class RdtVideoTest : public ::testing::Test {
protected:
    RdtVideo* video = nullptr;

    void SetUp() override {
        video = rdt_video_create(nullptr, nullptr);
        ASSERT_NE(video, nullptr);
    }

    void TearDown() override {
        if (video) {
            rdt_video_destroy(video);
            video = nullptr;
        }
    }

    // open test file and wait until READY
    bool open_and_wait_ready() {
        int rc = rdt_video_open_file(video, TEST_VIDEO_PATH);
        if (rc != 0) return false;
        return wait_for_state(video, RDT_VIDEO_STATE_READY, 3000);
    }
};

// ============================================================================
// Lifecycle Tests
// ============================================================================

TEST_F(RdtVideoTest, CreateDestroy) {
    // video was created in SetUp, will be destroyed in TearDown
    EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_IDLE);
}

TEST_F(RdtVideoTest, OpenInvalidFile) {
    int rc = rdt_video_open_file(video, "nonexistent_file.mp4");
    // open_file may return 0 (async) and transition to ERROR, or return -1 immediately
    if (rc == 0) {
        // wait a bit for ERROR state
        pump_runloop_ms(500);
        RdtVideoState state = rdt_video_get_state(video);
        EXPECT_TRUE(state == RDT_VIDEO_STATE_ERROR || state == RDT_VIDEO_STATE_LOADING);
    } else {
        EXPECT_EQ(rc, -1);
    }
}

TEST_F(RdtVideoTest, OpenValidFile) {
    int rc = rdt_video_open_file(video, TEST_VIDEO_PATH);
    ASSERT_EQ(rc, 0);

    RdtVideoState state = rdt_video_get_state(video);
    // immediately after open, state should be LOADING or already READY
    EXPECT_TRUE(state == RDT_VIDEO_STATE_LOADING || state == RDT_VIDEO_STATE_READY);
}

// ============================================================================
// Metadata Tests (requires file to reach READY state)
// ============================================================================

TEST_F(RdtVideoTest, IntrinsicDimensions) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    int w = rdt_video_get_width(video);
    int h = rdt_video_get_height(video);

    // test_video_audio.mp4 is Sintel 564x240
    EXPECT_EQ(w, 564);
    EXPECT_EQ(h, 240);
}

TEST_F(RdtVideoTest, HasAudioTrack) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // test_video_audio.mp4 has AAC stereo audio
    EXPECT_TRUE(rdt_video_has_audio(video));
}

TEST_F(RdtVideoTest, Duration) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    double duration = rdt_video_get_duration(video);
    // test_video_audio.mp4 is 12 seconds
    EXPECT_GT(duration, 11.0);
    EXPECT_LT(duration, 13.0);
}

// ============================================================================
// Playback State Transitions
// ============================================================================

TEST_F(RdtVideoTest, PlayPauseTransitions) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    rdt_video_play(video);
    EXPECT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000))
        << "Did not transition to PLAYING";

    rdt_video_pause(video);
    EXPECT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PAUSED, 1000))
        << "Did not transition to PAUSED";
}

// ============================================================================
// Frame Extraction Tests
// ============================================================================

TEST_F(RdtVideoTest, GetFrameDimensions) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // set layout rect to control decode resolution
    rdt_video_set_layout_rect(video, 564, 240);

    rdt_video_play(video);
    ASSERT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000));

    // wait for first frame to be decoded
    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    bool got_frame = false;
    for (int i = 0; i < 200; i++) {  // up to 2 seconds
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            got_frame = true;
            break;
        }
        pump_runloop_ms(10);
    }

    ASSERT_TRUE(got_frame) << "No frame decoded within 2 seconds";

    EXPECT_GT(frame.width, 0);
    EXPECT_GT(frame.height, 0);
    EXPECT_GE(frame.stride, frame.width * 4);  // RGBA = 4 bytes per pixel
    EXPECT_NE(frame.pixels, nullptr);
}

TEST_F(RdtVideoTest, FramePixelsNotAllZero) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    rdt_video_set_layout_rect(video, 564, 240);
    rdt_video_play(video);
    ASSERT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000));

    // wait for a non-black frame (skip first few which might be black leader)
    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    bool got_non_black_frame = false;
    for (int attempt = 0; attempt < 300; attempt++) {  // up to 3 seconds
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            // check if frame has any non-zero pixel data (not all black)
            int total_bytes = frame.stride * frame.height;
            bool has_content = false;
            // sample every 1000th byte to avoid slow full scan
            for (int i = 0; i < total_bytes; i += 1000) {
                if (frame.pixels[i] != 0) {
                    has_content = true;
                    break;
                }
            }
            if (has_content && frame.pts > 0.1) {
                got_non_black_frame = true;
                break;
            }
        }
        pump_runloop_ms(10);
    }

    ASSERT_TRUE(got_non_black_frame)
        << "All decoded frames were black/empty within 3 seconds";

    // verify RGBA format: stride should accommodate width * 4
    EXPECT_GE(frame.stride, frame.width * 4);
}

TEST_F(RdtVideoTest, FramePTSAdvances) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    rdt_video_set_layout_rect(video, 282, 120);  // half resolution
    rdt_video_play(video);
    ASSERT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000));

    double first_pts = -1.0;
    double later_pts = -1.0;
    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    // get first frame
    for (int i = 0; i < 200; i++) {
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            first_pts = frame.pts;
            break;
        }
        pump_runloop_ms(10);
    }
    ASSERT_GE(first_pts, 0.0) << "Could not get first frame";

    // wait 500ms then get another frame
    pump_runloop_ms(500);

    for (int i = 0; i < 50; i++) {
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            later_pts = frame.pts;
            break;
        }
        pump_runloop_ms(10);
    }
    ASSERT_GE(later_pts, 0.0) << "Could not get later frame";

    EXPECT_GT(later_pts, first_pts)
        << "PTS did not advance: first=" << first_pts << " later=" << later_pts;
}

// ============================================================================
// Layout Rect / Resolution Capping
// ============================================================================

TEST_F(RdtVideoTest, LayoutRectDoesNotAffectFrameSize) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // set small layout rect — currently, frame extraction always returns
    // intrinsic resolution; scaling happens in render_video.cpp blit path
    rdt_video_set_layout_rect(video, 200, 100);
    rdt_video_play(video);
    ASSERT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000));

    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    bool got_frame = false;
    for (int i = 0; i < 200; i++) {
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            got_frame = true;
            break;
        }
        pump_runloop_ms(10);
    }
    ASSERT_TRUE(got_frame);

    // frame comes back at intrinsic dimensions (564x240), not layout rect
    // render_video.cpp handles the scaling during blit
    EXPECT_EQ(frame.width, 564);
    EXPECT_EQ(frame.height, 240);
}

// ============================================================================
// Volume / Mute
// ============================================================================

TEST_F(RdtVideoTest, VolumeAndMute) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // these should not crash
    rdt_video_set_volume(video, 0.5f);
    rdt_video_set_muted(video, true);
    rdt_video_set_muted(video, false);
    rdt_video_set_volume(video, 1.0f);

    // play muted to verify no crash during audio playback
    rdt_video_set_muted(video, true);
    rdt_video_play(video);
    EXPECT_TRUE(wait_for_state(video, RDT_VIDEO_STATE_PLAYING, 1000));

    pump_runloop_ms(200);  // let it play briefly
    rdt_video_pause(video);
}

// ============================================================================
// Callbacks
// ============================================================================

struct CallbackData {
    int state_changed_count;
    int size_known_count;
    int duration_known_count;
    int last_width;
    int last_height;
    double last_duration;
    RdtVideoState last_state;
};

static void on_state_changed(RdtVideo*, RdtVideoState state, void* ud) {
    auto* d = static_cast<CallbackData*>(ud);
    d->state_changed_count++;
    d->last_state = state;
}

static void on_video_size_known(RdtVideo*, int w, int h, void* ud) {
    auto* d = static_cast<CallbackData*>(ud);
    d->size_known_count++;
    d->last_width = w;
    d->last_height = h;
}

static void on_duration_known(RdtVideo*, double seconds, void* ud) {
    auto* d = static_cast<CallbackData*>(ud);
    d->duration_known_count++;
    d->last_duration = seconds;
}

TEST(RdtVideoCallbackTest, CallbacksFireOnOpen) {
    CallbackData data = {};

    RdtVideoCallbacks cb = {};
    cb.on_state_changed = on_state_changed;
    cb.on_video_size_known = on_video_size_known;
    cb.on_duration_known = on_duration_known;

    RdtVideo* video = rdt_video_create(&cb, &data);
    ASSERT_NE(video, nullptr);

    int rc = rdt_video_open_file(video, TEST_VIDEO_PATH);
    ASSERT_EQ(rc, 0);

    // poll state to drive the KVO-fallback path (callbacks fire from get_state)
    bool ready = wait_for_state(video, RDT_VIDEO_STATE_READY, 3000);
    EXPECT_TRUE(ready);

    if (ready) {
        // at least one state change should have occurred
        EXPECT_GE(data.state_changed_count, 1);
    }

    rdt_video_destroy(video);
}
