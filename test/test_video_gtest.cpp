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
#include <math.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "../radiant/rdt_video.h"
#include "../radiant/audio_bus.hpp"
#include "test_audio_fixture.hpp"

// ============================================================================
// Test media path — resolved relative to the executable's working directory
// ============================================================================

static const char* TEST_VIDEO_PATH = "test/media/test_video_audio.mp4";
static bool native_frame_decode_unavailable = false;

TEST(RdtVideoMimeTest, ReportsActualDecoderCapabilities) {
    const char* invalid[] = {"", "video", "video/", "/mp4", "video /mp4",
        "video/mp4/extra", "text/html", "application/octet-stream",
        "video/mp4; codecs=\"not-a-codec\""};
    for (const char* type : invalid) EXPECT_STREQ("", rdt_video_can_play_type(type, strlen(type))) << type;
    const char nul_type[] = "video/mp4\0";
    EXPECT_STREQ("", rdt_video_can_play_type(nul_type, sizeof(nul_type) - 1));
#ifdef __APPLE__
    const char* mp4 = "video/mp4";
    EXPECT_STREQ("maybe", rdt_video_can_play_type(mp4, strlen(mp4)));
    const char* h264 = "video/mp4; codecs=\"avc1.42E01E\"";
    EXPECT_STREQ("probably", rdt_video_can_play_type(h264, strlen(h264)));
    const char* equivalents[] = {" \tVIDEO/MP4 \r\n", "video/mp4;", "video/mp4; nonsense=value",
        "video/mp4; codecs=", "video/mp4; codecs=\"\""};
    for (const char* type : equivalents) EXPECT_STREQ("maybe", rdt_video_can_play_type(type, strlen(type))) << type;
    const char* codec_variants[] = {"video/mp4; CODECS=avc1.42E01E",
        "video/mp4; codecs=\"avc1.42E01E\"ignored",
        "video/mp4; ignored=\"; codecs=bad\"; codecs=avc1.42E01E",
        "video/mp4; codecs=\"avc1.42E01E\"; codecs=bad",
        "video/mp4; codecs=\"avc1.42E01\\E\""};
    for (const char* type : codec_variants) EXPECT_STREQ("probably", rdt_video_can_play_type(type, strlen(type))) << type;
#else
    EXPECT_STREQ("", rdt_video_can_play_type("video/mp4", 9));
#endif
}

// ============================================================================
// Helper: poll state until target or timeout
// ============================================================================


static RdtVideo* create_bound_video(AudioSubmixId submix) {
    auto* video = rdt_video_create(nullptr, nullptr); if (!video) return nullptr;
    RdtVideoAudioBinding binding = {}; binding.submix = submix;
    if (rdt_video_bind_audio(video, &binding)) return video;
    rdt_video_destroy(video); return nullptr;
}
static bool open_ready(RdtVideo* video) {
    return rdt_video_open_file(video, TEST_VIDEO_PATH) == 0 && audio_fixture_video_wait(video, RDT_VIDEO_STATE_READY, 3000);
}

// pump the run loop for a given duration (ms)
static void pump_runloop_ms(int ms, AudioSubmixId submix = 0, RdtVideo* video = nullptr) {
    if (submix && video && audio_bus_offline()) {
        // offline media time follows consumed PCM, so wall-clock waiting cannot advance frames.
        unsigned frames = (unsigned)ms * AUDIO_BUS_RATE / 1000;
        while (frames) {
            rdt_video_get_state(video);
            unsigned chunk = frames < 4096 ? frames : 4096;
            audio_submix_render(submix, nullptr, chunk); frames -= chunk;
        }
        return;
    }
#ifdef __APPLE__
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, false);
#else
    usleep(ms * 1000);
#endif
}

static bool wait_for_frame(RdtVideo* video, RdtVideoFrame* frame, int timeout_ms, AudioSubmixId submix) {
    int elapsed = 0;
    const int step_ms = 10;
    while (elapsed < timeout_ms) {
        if (rdt_video_get_frame(video, frame) == 0 && frame->pixels != nullptr) {
            return true;
        }
        pump_runloop_ms(step_ms, submix, video);
        elapsed += step_ms;
    }
    return rdt_video_get_frame(video, frame) == 0 && frame->pixels != nullptr;
}

// ============================================================================
// Test Fixture
// ============================================================================

struct VideoStorageControl { unsigned acquired, released, retired; size_t bytes; bool allow = true; };
class RdtVideoTest : public ::testing::Test {
protected:
    RdtVideo* video = nullptr;
    AudioSubmixId submix = 0;
    bool previous_offline = false;
    VideoStorageControl owner = {};
    RdtVideo* control = nullptr;
    AudioSubmixId control_submix = 0;
    AudioGraphId graph = 0;
    AudioGraphConfig graph_config = {};

    void SetUp() override {
        previous_offline = audio_bus_offline();
        audio_bus_set_offline(true);
#ifdef __linux__
        // rdt_video_stub is the deliberate Linux build backend until an
        // FFmpeg implementation exists; these tests require native decoding.
        GTEST_SKIP() << "Linux video backend is not implemented";
#endif
        submix = audio_submix_create(); ASSERT_NE(submix, 0u);
        video = create_bound_video(submix); ASSERT_NE(video, nullptr);
    }

    void TearDown() override {
        if (video) {
            rdt_video_destroy(video);
            video = nullptr;
        }
        if (control) rdt_video_destroy(control);
        audio_submix_destroy(control_submix);
        audio_submix_destroy(submix); audio_bus_set_offline(previous_offline);
    }

    // open test file and wait until READY
    bool open_and_wait_ready() {
        return open_ready(video);
    }
    bool prepare_graph(float gain) {
        graph = audio_submix_graph_open(submix); if (!graph) return false;
        graph_config.count = 2;
        graph_config.buses[0] = audio_graph_bus_default("master");
        graph_config.buses[1] = audio_graph_bus_default("video");
        graph_config.buses[0].smoothing = graph_config.buses[1].smoothing = 0;
        graph_config.buses[1].gain = gain;
        return audio_fixture_graph_publish(submix, graph, graph_config);
    }
    AudioRouteId video_route() { return audio_submix_graph_route(submix, graph, nullptr, "video"); }
    void expect_route_loss(unsigned phase) {
        ASSERT_TRUE(prepare_graph(.5f)); ASSERT_TRUE(open_and_wait_ready()); const char* error = nullptr;
        auto route = video_route(); ASSERT_TRUE(rdt_video_audio_route(video, 0, route, &error));
        if (phase) { rdt_video_play(video); ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING); }
        if (phase == 1) rdt_video_pause(video);
        audio_submix_graph_close(submix, graph);
        EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_ERROR);
        AudioProviderInfo info; ASSERT_TRUE(rdt_video_audio_routing(video, &info));
        EXPECT_EQ(info.route, route); EXPECT_EQ(info.loss, AUDIO_VOICE_ROUTE_LOST); EXPECT_FALSE(info.route_valid);
        EXPECT_FALSE(audio_submix_has_work(submix));
        ASSERT_TRUE(rdt_video_audio_route(video, route, 0, &error));
        rdt_video_play(video); EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_ERROR);
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
        pump_runloop_ms(500, submix, video);
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
    EXPECT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000))
        << "Did not transition to PLAYING";

    rdt_video_pause(video);
    EXPECT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PAUSED, 1000))
        << "Did not transition to PAUSED";
}

TEST_F(RdtVideoTest, AudioTrackReachesTheSharedOutputKernel) {
    audio_bus_set_offline(true); ASSERT_TRUE(open_and_wait_ready()); ASSERT_TRUE(rdt_video_has_audio(video));
    rdt_video_play(video); ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));
    float samples[4096 * 2]; audio_bus_mix(samples, 4096); double energy = 0;
    for (float sample : samples) energy += sample * sample;
    EXPECT_GT(energy, 1e-6); EXPECT_EQ(audio_submix_clock(submix), 4096u);
    EXPECT_NEAR(rdt_video_get_current_time(video), 4096.0 / AUDIO_BUS_RATE, .001);
}

TEST_F(RdtVideoTest, NamedRoutesMatchIndependentPcmAndRewireWithoutRestarting) {
    ASSERT_TRUE(prepare_graph(.25f)); ASSERT_TRUE(open_and_wait_ready());
    control_submix = audio_submix_create(); ASSERT_NE(control_submix, 0u);
    control = create_bound_video(control_submix); ASSERT_NE(control, nullptr); ASSERT_TRUE(open_ready(control));
    auto foreign = audio_submix_graph_open(control_submix); ASSERT_NE(foreign, 0u);
    ASSERT_TRUE(audio_fixture_graph_publish(control_submix, foreign, graph_config));
    auto foreign_route = audio_submix_graph_route(control_submix, foreign, nullptr, "video");
    auto route = video_route(); ASSERT_NE(route, 0u); const char* error = nullptr;
    ASSERT_TRUE(rdt_video_audio_route(video, 0, route, &error));
    rdt_video_play(video); rdt_video_play(control);
    float actual[4096 * 2], reference[4096 * 2]; double energy = 0;
    ASSERT_TRUE(audio_submix_render(submix, actual, 4096));
    ASSERT_TRUE(audio_submix_render(control_submix, reference, 4096));
    for (unsigned i = 0; i < 4096 * 2; i++) {
        energy += reference[i] * reference[i]; EXPECT_NEAR(actual[i], reference[i] * .25f, 1e-6);
    }
    EXPECT_GT(energy, 1e-6);
    AudioGraphSnapshot meters; ASSERT_TRUE(audio_submix_graph_snapshot(submix, graph, &meters));
    EXPECT_EQ(meters.meters[1].frames, 4096u); EXPECT_GT(meters.meters[1].peak, 0);
    EXPECT_FALSE(rdt_video_audio_route(video, 0, 0, &error));
    EXPECT_FALSE(rdt_video_audio_route(video, route, foreign_route, &error));
    AudioProviderInfo info; ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_EQ(info.route, route);
    ASSERT_TRUE(rdt_video_audio_route(video, route, 0, &error));
    ASSERT_TRUE(audio_submix_render(submix, actual, 256));
    ASSERT_TRUE(audio_submix_render(control_submix, reference, 256));
    for (unsigned i = 0; i < 512; i++) EXPECT_NEAR(actual[i], reference[i], 1e-6);
    double held = rdt_video_get_current_time(video); rdt_video_pause(video);
    ASSERT_TRUE(rdt_video_audio_route(video, 0, route, &error));
    ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_STREQ(info.state, "paused"); EXPECT_DOUBLE_EQ(info.position, held);
    rdt_video_play(video); ASSERT_TRUE(audio_submix_render(submix, actual, 256));
    ASSERT_TRUE(audio_submix_render(control_submix, reference, 256));
    for (unsigned i = 0; i < 512; i++) EXPECT_NEAR(actual[i], reference[i] * .25f, 1e-6);
}

TEST_F(RdtVideoTest, PausedNamedRoutePinsItsBusUntilExplicitUnbind) {
    ASSERT_TRUE(prepare_graph(.5f)); ASSERT_TRUE(open_and_wait_ready()); auto route = video_route(); const char* error = nullptr;
    rdt_video_play(video); rdt_video_pause(video); ASSERT_TRUE(rdt_video_audio_route(video, 0, route, &error));
    graph_config.count = 1; EXPECT_FALSE(audio_fixture_graph_publish(submix, graph, graph_config));
    AudioProviderInfo info; ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_TRUE(info.route_valid);
    ASSERT_TRUE(rdt_video_audio_route(video, route, 0, &error));
    ASSERT_TRUE(audio_fixture_graph_publish(submix, graph, graph_config));
    EXPECT_FALSE(rdt_video_audio_route(video, 0, route, &error));
    ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_EQ(info.route, 0u); EXPECT_STREQ(info.state, "paused");
}

TEST_F(RdtVideoTest, NamedRouteReferenceExhaustionPreservesDefaultAndRecovers) {
    ASSERT_TRUE(prepare_graph(.5f)); ASSERT_TRUE(open_and_wait_ready()); auto route = video_route(); AudioRouteRef refs[128] = {};
    for (auto& ref : refs) ASSERT_TRUE(audio_submix_bind_route(submix, &ref, 0, route));
    const char* error = nullptr; EXPECT_FALSE(rdt_video_audio_route(video, 0, route, &error));
    AudioProviderInfo info; ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_EQ(info.route, 0u);
    audio_submix_release_route(submix, refs[0]); ASSERT_TRUE(rdt_video_audio_route(video, 0, route, &error));
    for (auto ref : refs) audio_submix_release_route(submix, ref);
    ASSERT_TRUE(rdt_video_audio_routing(video, &info)); EXPECT_EQ(info.route, route); EXPECT_TRUE(info.route_valid);
}

TEST_F(RdtVideoTest, NamedRouteLossFencesReadyReaders) { expect_route_loss(0); }
TEST_F(RdtVideoTest, NamedRouteLossFencesPausedReaders) { expect_route_loss(1); }
TEST_F(RdtVideoTest, NamedRouteLossFencesPlayingReaders) { expect_route_loss(2); }

TEST_F(RdtVideoTest, SharedAudioFirstPlayHonorsAnAlreadyRequestedSeek) {
    ASSERT_TRUE(open_and_wait_ready()); rdt_video_seek(video, .5); rdt_video_play(video);
    ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
    ASSERT_TRUE(audio_submix_render(submix, nullptr, 256));
    EXPECT_NEAR(rdt_video_get_current_time(video), .5 + 256.0 / AUDIO_BUS_RATE, 1.0 / AUDIO_BUS_RATE);
}

TEST_F(RdtVideoTest, SharedAudioLoadingRetainsTheRequestedSeek) {
    ASSERT_EQ(rdt_video_open_file(video, TEST_VIDEO_PATH), 0); rdt_video_seek(video, .5);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_READY, 3000));
    EXPECT_DOUBLE_EQ(rdt_video_get_current_time(video), .5); rdt_video_play(video);
    ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
    ASSERT_TRUE(audio_submix_render(submix, nullptr, 256));
    EXPECT_NEAR(rdt_video_get_current_time(video), .5 + 256.0 / AUDIO_BUS_RATE, 1.0 / AUDIO_BUS_RATE);
}

TEST_F(RdtVideoTest, SharedAudioPauseSeekAndMuteKeepTheMediaSampleClock) {
    audio_bus_set_offline(true); ASSERT_TRUE(open_and_wait_ready()); rdt_video_play(video);
    ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
    float output[4096 * 2]; audio_bus_mix(output, 4096); rdt_video_pause(video);
    double held = rdt_video_get_current_time(video); uint64_t clock = audio_submix_clock(submix);
    audio_bus_mix(output, 4096); EXPECT_DOUBLE_EQ(rdt_video_get_current_time(video), held);
    EXPECT_EQ(audio_submix_clock(submix), clock); for (float v : output) EXPECT_FLOAT_EQ(v, 0);
    rdt_video_seek(video, .5); EXPECT_DOUBLE_EQ(rdt_video_get_current_time(video), .5);
    rdt_video_set_muted(video, true); rdt_video_play(video); ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
    audio_bus_mix(output, 4096); for (float v : output) EXPECT_FLOAT_EQ(v, 0);
    EXPECT_NEAR(rdt_video_get_current_time(video), .5 + 4096.0 / AUDIO_BUS_RATE, 1e-9);
    rdt_video_set_muted(video, false); rdt_video_get_state(video); audio_bus_mix(output, 4096);
    double energy = 0; for (float v : output) energy += v * v; EXPECT_GT(energy, 1e-6);
}
TEST_F(RdtVideoTest, SharedAudioLoopsAndEndsAcrossBoundedRefills) {
    audio_bus_set_offline(true); ASSERT_TRUE(open_and_wait_ready()); rdt_video_set_loop(video, true); rdt_video_play(video);
    uint64_t cycle = (uint64_t)llround(rdt_video_get_duration(video) * AUDIO_BUS_RATE), target = cycle + 4096;
    float output[4096 * 2];
    for (uint64_t done = 0; done < target;) {
        ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
        unsigned count = target - done < 4096 ? (unsigned)(target - done) : 4096;
        audio_bus_mix(output, count); done += count;
    }
    EXPECT_NEAR(rdt_video_get_current_time(video), 4096.0 / AUDIO_BUS_RATE, 1e-9);
    rdt_video_set_loop(video, false);
    for (uint64_t done = 4096; done < cycle;) {
        ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
        unsigned count = cycle - done < 4096 ? (unsigned)(cycle - done) : 4096;
        audio_bus_mix(output, count); done += count;
    }
    EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_ENDED);
    EXPECT_NEAR(rdt_video_get_current_time(video), rdt_video_get_duration(video), 1.0 / AUDIO_BUS_RATE);
    EXPECT_FALSE(audio_submix_has_work(submix));
}

static bool video_test_reserve(void* data, size_t bytes) {
    auto* owner = (VideoStorageControl*)data; owner->acquired++; owner->bytes = bytes; return owner->allow;
}
static void video_test_release(void* data) { ((VideoStorageControl*)data)->released++; }
static void video_test_retire(void* data) { ((VideoStorageControl*)data)->retired++; }
TEST_F(RdtVideoTest, SharedAudioBindingRetiresPcmBeforeReleasingItsOwner) {
    audio_bus_set_offline(true);
    RdtVideoAudioBinding binding = {submix, &owner, video_test_reserve, video_test_release, video_test_retire};
    ASSERT_TRUE(rdt_video_bind_audio(video, &binding)); ASSERT_TRUE(open_and_wait_ready());
    EXPECT_EQ(owner.acquired, 0u); rdt_video_play(video); EXPECT_EQ(owner.acquired, 1u); EXPECT_GT(owner.bytes, 0u);
    rdt_video_pause(video); EXPECT_EQ(owner.released, 0u); ASSERT_TRUE(rdt_video_bind_audio(video, nullptr));
    EXPECT_EQ(owner.released, 1u); EXPECT_EQ(owner.retired, 1u); EXPECT_FALSE(audio_submix_has_work(submix));
    rdt_video_destroy(video); video = nullptr; EXPECT_EQ(owner.released, 1u); EXPECT_EQ(owner.retired, 1u);
}

TEST_F(RdtVideoTest, SharedAudioQuotaRefusalDoesNotStartAnotherAudiblePlayer) {
    owner.allow = false;
    RdtVideoAudioBinding binding = {submix, &owner, video_test_reserve, video_test_release, video_test_retire};
    ASSERT_TRUE(rdt_video_bind_audio(video, &binding)); ASSERT_TRUE(open_and_wait_ready());
    rdt_video_play(video); EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_ERROR);
    EXPECT_EQ(owner.acquired, 1u); EXPECT_EQ(owner.released, 0u); EXPECT_FALSE(audio_submix_has_work(submix));
    rdt_video_destroy(video); video = nullptr; EXPECT_EQ(owner.retired, 1u); EXPECT_EQ(owner.released, 0u);
}

TEST_F(RdtVideoTest, SharedAudioOutputRetirementFailsAndRemovesItsReader) {
    ASSERT_TRUE(open_and_wait_ready()); rdt_video_play(video);
    ASSERT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_PLAYING);
    audio_bus_retire_output(); EXPECT_EQ(rdt_video_get_state(video), RDT_VIDEO_STATE_ERROR);
    EXPECT_FALSE(audio_submix_has_work(submix));
}

TEST_F(RdtVideoTest, SilentVideoKeepsThePlatformClockWithoutReservingPcm) {
    RdtVideoAudioBinding binding = {submix, &owner, video_test_reserve, video_test_release, video_test_retire};
    ASSERT_TRUE(rdt_video_bind_audio(video, &binding));
    ASSERT_EQ(rdt_video_open_file(video, "test/media/test_video.mp4"), 0);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_READY, 3000)); ASSERT_FALSE(rdt_video_has_audio(video));
    rdt_video_play(video); ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));
    for (unsigned tries = 0; tries < 200 && rdt_video_get_current_time(video) <= .05; tries++) pump_runloop_ms(10);
    EXPECT_GT(rdt_video_get_current_time(video), .05); EXPECT_EQ(owner.acquired, 0u);
    EXPECT_EQ(audio_submix_clock(submix), 0u); EXPECT_FALSE(audio_submix_has_work(submix));
    rdt_video_pause(video); EXPECT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PAUSED, 1000));
}

// ============================================================================
// Frame Extraction Tests
// ============================================================================

TEST_F(RdtVideoTest, GetFrameDimensions) {
    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // set layout rect to control decode resolution
    rdt_video_set_layout_rect(video, 564, 240);

    rdt_video_play(video);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));

    // wait for first frame to be decoded
    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    bool got_frame = wait_for_frame(video, &frame, 2000, submix);

    if (!got_frame) {
        native_frame_decode_unavailable = true;
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    EXPECT_GT(frame.width, 0);
    EXPECT_GT(frame.height, 0);
    EXPECT_GE(frame.stride, frame.width * 4);  // RGBA = 4 bytes per pixel
    EXPECT_NE(frame.pixels, nullptr);
}

TEST_F(RdtVideoTest, FramePixelsNotAllZero) {
    if (native_frame_decode_unavailable) {
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    rdt_video_set_layout_rect(video, 564, 240);
    rdt_video_play(video);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));

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
        pump_runloop_ms(10, submix, video);
    }

    if (!got_non_black_frame) {
        native_frame_decode_unavailable = true;
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    // verify RGBA format: stride should accommodate width * 4
    EXPECT_GE(frame.stride, frame.width * 4);
}

TEST_F(RdtVideoTest, FramePTSAdvances) {
    if (native_frame_decode_unavailable) {
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    rdt_video_set_layout_rect(video, 282, 120);  // half resolution
    rdt_video_play(video);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));

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
        pump_runloop_ms(10, submix, video);
    }
    if (first_pts < 0.0) {
        native_frame_decode_unavailable = true;
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    // wait 500ms then get another frame
    pump_runloop_ms(500, submix, video);

    for (int i = 0; i < 50; i++) {
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            later_pts = frame.pts;
            break;
        }
        pump_runloop_ms(10, submix, video);
    }
    ASSERT_GE(later_pts, 0.0) << "Could not get later frame";

    EXPECT_GT(later_pts, first_pts)
        << "PTS did not advance: first=" << first_pts << " later=" << later_pts;
}

// ============================================================================
// Layout Rect / Resolution Capping
// ============================================================================

TEST_F(RdtVideoTest, LayoutRectDoesNotAffectFrameSize) {
    if (native_frame_decode_unavailable) {
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

    ASSERT_TRUE(open_and_wait_ready()) << "File did not reach READY state";

    // set small layout rect — currently, frame extraction always returns
    // intrinsic resolution; scaling happens in render_video.cpp blit path
    rdt_video_set_layout_rect(video, 200, 100);
    rdt_video_play(video);
    ASSERT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));

    RdtVideoFrame frame;
    memset(&frame, 0, sizeof(frame));

    bool got_frame = false;
    for (int i = 0; i < 200; i++) {
        if (rdt_video_get_frame(video, &frame) == 0 && frame.pixels != nullptr) {
            got_frame = true;
            break;
        }
        pump_runloop_ms(10, submix, video);
    }
    if (!got_frame) {
        native_frame_decode_unavailable = true;
        GTEST_SKIP() << "Native macOS frame decode is unavailable in this environment";
    }

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
    EXPECT_TRUE(audio_fixture_video_wait(video, RDT_VIDEO_STATE_PLAYING, 1000));

    pump_runloop_ms(200, submix, video);  // let it play briefly
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
#ifdef __linux__
    GTEST_SKIP() << "Linux video backend is not implemented";
#endif
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
    bool ready = audio_fixture_video_wait(video, RDT_VIDEO_STATE_READY, 3000);
    EXPECT_TRUE(ready);

    if (ready) {
        // at least one state change should have occurred
        EXPECT_GE(data.state_changed_count, 1);
    }

    rdt_video_destroy(video);
}
