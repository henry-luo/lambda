#include <gtest/gtest.h>
#include "../radiant/rdt_audio.h"
#include "../radiant/audio_bus.hpp"
#include "../radiant/audio_cues.hpp"
#include "../radiant/audio_stream.hpp"
#include "../radiant/audio_capture_pcm.hpp"
#include "../lib/str.h"
#include "../lib/mem.h"
#include "../lib/file.h"
#include "test_audio_fixture.hpp"
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include "../lib/time_util.h"

class NativeAudioStream : public ::testing::Test {
protected:
    AudioStream* stream = nullptr;
    void SetUp() override { stream = audio_stream_create(8); ASSERT_NE(stream, nullptr); }
    void TearDown() override { audio_stream_destroy(stream); }
};
TEST_F(NativeAudioStream, TimestampGapsAndOverlapsPreserveExactStereoPcm) {
    const float first[] = {.1f,-.1f,.2f,-.2f,.3f,-.3f,.4f,-.4f};
    const float next[] = {.5f,-.5f,.6f,-.6f,.7f,-.7f,.8f,-.8f};
    ASSERT_EQ(audio_stream_write(stream, 2, first, 4), 4u);
    ASSERT_EQ(audio_stream_write(stream, 4, next, 4), 4u);
    audio_stream_finish(stream); ASSERT_TRUE(audio_stream_play(stream));
    float output[20]; for (auto& value : output) value = .125f;
    audio_stream_mix(stream, output, 10, 123);
    const float expected[] = {0,0,.1f,.2f,.3f,.4f,.7f,.8f,0,0};
    for (unsigned i = 0; i < 10; i++) {
        EXPECT_FLOAT_EQ(output[i * 2], .125f + expected[i]);
        EXPECT_FLOAT_EQ(output[i * 2 + 1], .125f - expected[i]);
    }
    auto info = audio_stream_snapshot(stream); EXPECT_EQ(info.frame, 8u);
    EXPECT_EQ(info.state, AUDIO_STREAM_ENDED); EXPECT_EQ(info.underrun_frames, 0u);
}
TEST_F(NativeAudioStream, PartialWritesAndPhysicalWrapRetainSourceOrder) {
    audio_stream_destroy(stream); stream = audio_stream_create(4); ASSERT_NE(stream, nullptr);
    float samples[16]; for (unsigned i = 0; i < 8; i++) samples[i * 2] = samples[i * 2 + 1] = (float)i / 10;
    ASSERT_EQ(audio_stream_write(stream, 0, samples, 8), 4u); ASSERT_TRUE(audio_stream_play(stream));
    float output[8] = {}; audio_stream_mix(stream, output, 3, 0);
    for (unsigned i = 0; i < 6; i++) EXPECT_FLOAT_EQ(output[i], samples[i]);
    ASSERT_EQ(audio_stream_write(stream, 4, samples + 8, 4), 3u);
    memset(output, 0, sizeof(output)); audio_stream_mix(stream, output, 4, 3);
    for (unsigned i = 0; i < 8; i++) EXPECT_FLOAT_EQ(output[i], samples[6 + i]);
    ASSERT_EQ(audio_stream_write(stream, 7, samples + 14, 1), 1u); audio_stream_finish(stream);
    memset(output, 0, sizeof(output)); audio_stream_mix(stream, output, 4, 7);
    EXPECT_FLOAT_EQ(output[0], .7f); EXPECT_FLOAT_EQ(output[1], .7f);
    for (unsigned i = 2; i < 8; i++) EXPECT_FLOAT_EQ(output[i], 0);
    EXPECT_EQ(audio_stream_snapshot(stream).frame, 8u);
}
TEST_F(NativeAudioStream, StarvationHoldsMediaTimeUntilTheProducerPublishesMore) {
    ASSERT_TRUE(audio_stream_play(stream)); float output[14] = {};
    audio_stream_mix(stream, output, 7, 999); auto before = audio_stream_snapshot(stream);
    EXPECT_EQ(before.frame, 0u); EXPECT_EQ(before.underrun_frames, 7u);
    const float input[] = {.25f,.5f,.75f,1}; ASSERT_EQ(audio_stream_write(stream, 0, input, 2), 2u);
    audio_stream_mix(stream, output, 5, 1006); auto after = audio_stream_snapshot(stream);
    EXPECT_EQ(after.frame, 2u); EXPECT_EQ(after.underrun_frames, 10u);
    for (unsigned i = 0; i < 4; i++) EXPECT_FLOAT_EQ(output[i], input[i]);
    for (unsigned i = 4; i < 14; i++) EXPECT_FLOAT_EQ(output[i], 0);
    audio_stream_finish(stream); audio_stream_mix(stream, output, 1, 1011);
    EXPECT_EQ(audio_stream_snapshot(stream).state, AUDIO_STREAM_ENDED);
}
TEST_F(NativeAudioStream, PauseGainAndSeekResetPreserveWideSampleCounters) {
    const float samples[] = {.5f,.25f,.5f,.25f,.5f,.25f,.5f,.25f};
    ASSERT_EQ(audio_stream_write(stream, 0, samples, 4), 4u); ASSERT_TRUE(audio_stream_gain(stream, .25f));
    ASSERT_TRUE(audio_stream_play(stream)); float output[4] = {}; audio_stream_mix(stream, output, 1, 0);
    EXPECT_FLOAT_EQ(output[0], .125f); EXPECT_FLOAT_EQ(output[1], .0625f);
    audio_stream_pause(stream); memset(output, 0, sizeof(output)); audio_stream_mix(stream, output, 2, 1);
    EXPECT_EQ(audio_stream_snapshot(stream).frame, 1u); for (float v : output) EXPECT_FLOAT_EQ(v, 0);
    EXPECT_FALSE(audio_stream_gain(stream, NAN)); EXPECT_FALSE(audio_stream_gain(stream, 2));
    ASSERT_TRUE(audio_stream_gain(stream, 0)); ASSERT_TRUE(audio_stream_play(stream)); audio_stream_mix(stream, output, 1, 1);
    EXPECT_EQ(audio_stream_snapshot(stream).frame, 2u); EXPECT_FLOAT_EQ(output[0], 0);
    uint64_t wide = (UINT64_C(1) << 53) + 17; audio_stream_reset(stream, wide);
    EXPECT_EQ(audio_stream_snapshot(stream).queued, 0u); ASSERT_EQ(audio_stream_write(stream, wide, samples, 2), 2u);
    audio_stream_finish(stream); ASSERT_TRUE(audio_stream_play(stream)); audio_stream_mix(stream, output, 2, 2);
    EXPECT_EQ(audio_stream_snapshot(stream).frame, wide + 2); EXPECT_FALSE(audio_stream_play(stream));
}
TEST_F(NativeAudioStream, MalformedPacketsNeverPublishAPrefixOrWrapTheirTimestamp) {
    const float valid[] = {.5f,.5f}; ASSERT_EQ(audio_stream_write(stream, 0, valid, 1), 1u);
    const float invalid[] = {.1f,.1f,NAN,.2f}; EXPECT_EQ(audio_stream_write(stream, 1, invalid, 2), 0u);
    auto info = audio_stream_snapshot(stream); EXPECT_EQ(info.queued, 1u); EXPECT_EQ(info.written, 1u);
    EXPECT_EQ(info.state, AUDIO_STREAM_ERROR); EXPECT_FALSE(audio_stream_play(stream));
    audio_stream_reset(stream, UINT64_MAX - 1); EXPECT_EQ(audio_stream_write(stream, UINT64_MAX - 1, nullptr, 2), 0u);
    EXPECT_EQ(audio_stream_snapshot(stream).state, AUDIO_STREAM_ERROR);
    audio_stream_reset(stream, 0); EXPECT_EQ(audio_stream_write(stream, 0, nullptr, AUDIO_STREAM_PACKET + 1), 0u);
    EXPECT_EQ(audio_stream_snapshot(stream).queued, 0u);
    EXPECT_EQ(audio_stream_create(0), nullptr); EXPECT_EQ(audio_stream_create(AUDIO_STREAM_CAPACITY + 1), nullptr);
}
TEST_F(NativeAudioStream, SharedBusRendersTheStreamAndRetiresItsReaderBeforeRelease) {
    bool previous = audio_bus_offline(); audio_bus_set_offline(true);
    auto mix = audio_submix_create(); ASSERT_NE(mix, 0u);
    const float input[] = {.25f,-.25f,.5f,-.5f}; EXPECT_EQ(audio_stream_write(stream, 0, input, 2), 2u);
    audio_stream_finish(stream); EXPECT_TRUE(audio_stream_play(stream));
    auto voice = audio_voice_add(mix, audio_stream_mix, stream, nullptr); EXPECT_NE(voice, 0u);
    float output[8]; audio_bus_mix(output, 4);
    for (unsigned i = 0; i < 4; i++) EXPECT_FLOAT_EQ(output[i], input[i]);
    for (unsigned i = 4; i < 8; i++) EXPECT_FLOAT_EQ(output[i], 0);
    audio_voice_remove(voice); audio_stream_destroy(stream); stream = nullptr;
    audio_bus_mix(output, 4); for (float v : output) EXPECT_FLOAT_EQ(v, 0);
    audio_submix_destroy(mix); audio_bus_set_offline(previous);
}

struct StreamProducerControl { AudioStream* stream; int stopped; };
static void* stream_test_producer(void* argument) {
    auto* control = (StreamProducerControl*)argument;
    unsigned next = 0; float samples[37 * 2];
    while (next < 24000 && !__atomic_load_n(&control->stopped, __ATOMIC_ACQUIRE)) {
        unsigned count = 24000 - next < 37 ? 24000 - next : 37;
        for (unsigned i = 0; i < count; i++) {
            samples[i * 2] = (float)((next + i) % 1000) / 1000;
            samples[i * 2 + 1] = -samples[i * 2];
        }
        unsigned written = audio_stream_write(control->stream, next, samples, count);
        next += written; if (!written) sched_yield();
    }
    audio_stream_finish(control->stream); return nullptr;
}
TEST_F(NativeAudioStream, ConcurrentProducerAndConsumerKeepSampleOrderAcrossRingWraps) {
    ASSERT_TRUE(audio_stream_play(stream)); StreamProducerControl control{stream, 0}; pthread_t producer;
    ASSERT_EQ(pthread_create(&producer, nullptr, stream_test_producer, &control), 0);
    uint64_t start = time_now_ns(), cursor = 0;
    while (cursor < 24000 && time_now_ns() - start < UINT64_C(10000000000)) {
        float samples[17 * 2] = {}; audio_stream_mix(stream, samples, 17, cursor);
        uint64_t after = audio_stream_snapshot(stream).frame, count = after - cursor;
        for (unsigned i = 0; i < 17; i++) {
            float expected = i < count ? (float)((cursor + i) % 1000) / 1000 : 0;
            EXPECT_FLOAT_EQ(samples[i * 2], expected); EXPECT_FLOAT_EQ(samples[i * 2 + 1], -expected);
        }
        cursor = after; if (!count) sched_yield();
    }
    __atomic_store_n(&control.stopped, 1, __ATOMIC_RELEASE); EXPECT_EQ(pthread_join(producer, nullptr), 0);
    EXPECT_EQ(cursor, 24000u); float samples[2] = {}; audio_stream_mix(stream, samples, 1, cursor);
    EXPECT_EQ(audio_stream_snapshot(stream).state, AUDIO_STREAM_ENDED);
}

class NativeAudioCapture : public ::testing::Test {
protected:
    AudioCapturePcm* pcm = nullptr;
    void SetUp() override { pcm = audio_capture_pcm_create(4); ASSERT_NE(pcm, nullptr); }
    void TearDown() override { audio_capture_pcm_destroy(pcm); }
};
TEST_F(NativeAudioCapture, OverrunPreservesExactTimestampsAndNeverFillsGaps) {
    const float input[] = {.1f,-.1f,.2f,-.2f,.3f,-.3f,.4f,-.4f,.5f,-.5f,.6f,-.6f};
    ASSERT_EQ(audio_capture_pcm_write(pcm, 0, input, 6), 4u);
    float output[16] = {}; uint64_t first = UINT64_MAX;
    ASSERT_EQ(audio_capture_pcm_read(pcm, output, 3, &first), 3u); EXPECT_EQ(first, 0u);
    for (unsigned i = 0; i < 6; i++) EXPECT_FLOAT_EQ(output[i], input[i]);
    ASSERT_EQ(audio_capture_pcm_write(pcm, 6, input + 8, 2), 2u);
    ASSERT_EQ(audio_capture_pcm_read(pcm, output, 8, &first), 1u); EXPECT_EQ(first, 3u);
    EXPECT_FLOAT_EQ(output[0], .4f); EXPECT_FLOAT_EQ(output[1], -.4f);
    ASSERT_EQ(audio_capture_pcm_read(pcm, output, 8, &first), 2u); EXPECT_EQ(first, 6u);
    for (unsigned i = 0; i < 4; i++) EXPECT_FLOAT_EQ(output[i], input[8 + i]);
    auto info = audio_capture_pcm_info(pcm); EXPECT_EQ(info.captured, 8u); EXPECT_EQ(info.next_frame, 8u);
    EXPECT_EQ(info.dropped, 2u); EXPECT_EQ(info.read_frames, 6u); EXPECT_EQ(info.queued, 0u);
    EXPECT_FLOAT_EQ(info.peak, .6f); EXPECT_NEAR(info.rms, sqrtf((.25f + .36f) / 2), 1e-6f);
}
TEST_F(NativeAudioCapture, MonitoringAndRecordingHaveIndependentReadersAndDefaultIsSilent) {
    const float input[] = {.25f,-.25f,.5f,-.5f}; float output[8] = {};
    ASSERT_EQ(audio_capture_pcm_write(pcm, 0, input, 2), 2u); audio_capture_pcm_mix(pcm, output, 2, 0);
    for (float value : output) EXPECT_FLOAT_EQ(value, 0);
    ASSERT_TRUE(audio_capture_pcm_monitor(pcm, true, .5f));
    ASSERT_EQ(audio_capture_pcm_write(pcm, 2, input, 2), 2u);
    uint64_t first = 0; ASSERT_EQ(audio_capture_pcm_read(pcm, output, 4, &first), 4u);
    memset(output, 0, sizeof(output)); audio_capture_pcm_mix(pcm, output, 4, 0);
    for (unsigned i = 0; i < 4; i++) EXPECT_FLOAT_EQ(output[i], input[i] * .5f);
    for (unsigned i = 4; i < 8; i++) EXPECT_FLOAT_EQ(output[i], 0);
    EXPECT_EQ(audio_capture_pcm_info(pcm).read_frames, 4u);
    ASSERT_TRUE(audio_capture_pcm_monitor(pcm, false, 1));
    ASSERT_EQ(audio_capture_pcm_write(pcm, 4, input, 2), 2u);
    EXPECT_EQ(audio_capture_pcm_info(pcm).monitor_queued, 0u);
    EXPECT_FALSE(audio_capture_pcm_monitor(pcm, true, NAN)); EXPECT_FALSE(audio_capture_pcm_monitor(pcm, true, 2));
}
TEST_F(NativeAudioCapture, OversizedCallbackIsBoundedAndItsRemainderIsVisibleAsDropped) {
    float input[(AUDIO_CAPTURE_PACKET + 1) * 2] = {};
    ASSERT_EQ(audio_capture_pcm_write(pcm, 0, input, AUDIO_CAPTURE_PACKET + 1), 4u);
    auto info = audio_capture_pcm_info(pcm); EXPECT_EQ(info.captured, AUDIO_CAPTURE_PACKET + 1u);
    EXPECT_EQ(info.dropped, AUDIO_CAPTURE_PACKET - 3u); EXPECT_EQ(info.next_frame, AUDIO_CAPTURE_PACKET + 1u);
    uint64_t first = 0; float output[8]; EXPECT_EQ(audio_capture_pcm_read(pcm, output, AUDIO_CAPTURE_PACKET + 1, &first), 0u);
    EXPECT_EQ(audio_capture_pcm_create(0), nullptr); EXPECT_EQ(audio_capture_pcm_create(AUDIO_CAPTURE_CAPACITY + 1), nullptr);
    EXPECT_GT(audio_capture_pcm_bytes(), AUDIO_CAPTURE_CAPACITY * 2u * sizeof(float) * 2u);
}
TEST_F(NativeAudioCapture, MalformedPacketsPublishNothingAndSilenceMonitoring) {
    ASSERT_TRUE(audio_capture_pcm_monitor(pcm, true, 1));
    const float valid[] = {.25f,-.25f}; ASSERT_EQ(audio_capture_pcm_write(pcm, 0, valid, 1), 1u);
    const float invalid[] = {.1f,.1f,NAN,.2f}; EXPECT_EQ(audio_capture_pcm_write(pcm, 1, invalid, 2), 0u);
    auto info = audio_capture_pcm_info(pcm); EXPECT_TRUE(info.faulted); EXPECT_EQ(info.captured, 1u); EXPECT_EQ(info.queued, 1u);
    float output[2] = {}; audio_capture_pcm_mix(pcm, output, 1, 0);
    EXPECT_FLOAT_EQ(output[0], 0); EXPECT_FLOAT_EQ(output[1], 0);
    EXPECT_EQ(audio_capture_pcm_write(pcm, 1, valid, 1), 0u);
}
TEST_F(NativeAudioCapture, RegressingOrOverflowingSampleTimeCannotAliasAValidInterval) {
    uint64_t wide = (UINT64_C(1) << 53) + 17; uint64_t first = 0; float output[4];
    ASSERT_EQ(audio_capture_pcm_write(pcm, wide, nullptr, 2), 2u);
    ASSERT_EQ(audio_capture_pcm_read(pcm, output, 2, &first), 2u); EXPECT_EQ(first, wide);
    for (float value : output) EXPECT_FLOAT_EQ(value, 0);
    EXPECT_EQ(audio_capture_pcm_write(pcm, wide + 1, nullptr, 2), 0u); EXPECT_TRUE(audio_capture_pcm_info(pcm).faulted);
    audio_capture_pcm_destroy(pcm); pcm = audio_capture_pcm_create(4); ASSERT_NE(pcm, nullptr);
    EXPECT_EQ(audio_capture_pcm_write(pcm, UINT64_MAX - 1, nullptr, 2), 0u);
    EXPECT_TRUE(audio_capture_pcm_info(pcm).faulted); EXPECT_EQ(audio_capture_pcm_info(pcm).queued, 0u);
}
struct CaptureProducerControl { AudioCapturePcm* pcm; int stopped, done; };
static void* capture_test_producer(void* argument) {
    auto* control = (CaptureProducerControl*)argument; float input[74];
    for (unsigned next = 0; next < 24000 && !__atomic_load_n(&control->stopped, __ATOMIC_ACQUIRE);) {
        unsigned count = 24000 - next < 37 ? 24000 - next : 37;
        for (unsigned i = 0; i < count; i++) { input[i * 2] = (float)(next + i); input[i * 2 + 1] = -input[i * 2]; }
        audio_capture_pcm_write(control->pcm, next, input, count); next += count; sched_yield();
    }
    __atomic_store_n(&control->done, 1, __ATOMIC_RELEASE); return nullptr;
}
TEST_F(NativeAudioCapture, ConcurrentReadersObserveSampleIdentityAndAccountForEveryOverrun) {
    CaptureProducerControl control{pcm, 0, 0}; pthread_t producer;
    ASSERT_EQ(pthread_create(&producer, nullptr, capture_test_producer, &control), 0);
    uint64_t start = time_now_ns(), end = 0, read = 0;
    while (time_now_ns() - start < UINT64_C(10000000000)) {
        float output[34]; uint64_t first = 0; unsigned count = audio_capture_pcm_read(pcm, output, 17, &first);
        if (count) {
            EXPECT_GE(first, end); end = first + count; read += count;
            for (unsigned i = 0; i < count; i++) {
                EXPECT_FLOAT_EQ(output[i * 2], (float)(first + i)); EXPECT_FLOAT_EQ(output[i * 2 + 1], -(float)(first + i));
            }
        } else if (__atomic_load_n(&control.done, __ATOMIC_ACQUIRE)) break;
        else sched_yield();
    }
    __atomic_store_n(&control.stopped, 1, __ATOMIC_RELEASE); EXPECT_EQ(pthread_join(producer, nullptr), 0);
    auto info = audio_capture_pcm_info(pcm); EXPECT_EQ(info.captured, 24000u);
    EXPECT_EQ(read + info.dropped + info.queued, info.captured); EXPECT_FALSE(info.faulted);
}

class DecodedAudioPlayer : public ::testing::Test {
protected:
    RdtAudio* player = nullptr;
    RdtAudio* peer = nullptr;
    void TearDown() override { rdt_audio_destroy(peer); rdt_audio_destroy(player); }
    void open(uint32_t rate, uint16_t channels, uint32_t frames, int16_t left = 16384, int16_t right = -8192) {
        unsigned char bytes[44 + 1024 * 4];
        size_t length = audio_fixture_wave(bytes, sizeof(bytes), rate, channels, frames, left, right);
        ASSERT_GT(length, 0u); player = rdt_audio_open_bytes(bytes, length); ASSERT_NE(player, nullptr);
        memset(bytes, 0, sizeof(bytes));
    }
    void check_encoded_file(const char* path, size_t minimum_frames) {
        char* bytes = nullptr; size_t length = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_RENDER, &bytes, &length));
        player = rdt_audio_open_bytes(bytes, length); mem_free(bytes); ASSERT_NE(player, nullptr);
        EXPECT_GT(rdt_audio_frames(player), minimum_frames); ASSERT_TRUE(rdt_audio_play(player, 1));
        float samples[4096 * 2]; double energy = 0;
        for (size_t offset = 0; offset < rdt_audio_frames(player);) {
            size_t remaining = rdt_audio_frames(player) - offset;
            uint32_t count = remaining < 4096 ? (uint32_t)remaining : 4096;
            memset(samples, 0, sizeof(samples)); rdt_audio_mix(player, samples, count, offset);
            for (unsigned i = 0; i < count * 2; i++) { EXPECT_TRUE(isfinite(samples[i])); energy += samples[i] * samples[i]; }
            offset += count;
        }
        EXPECT_GT(energy, 1e-6); EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ENDED);
    }
};

class NativeAudioCues : public DecodedAudioPlayer {
protected:
    AudioSubmixId submix = 0;
    AudioVoiceId voice = 0;
    AudioCueQueue* queue = nullptr;
    bool previous_offline = false;
    void SetUp() override {
        previous_offline = audio_bus_offline(); audio_bus_set_offline(true); submix = audio_submix_create();
        ASSERT_NE(submix, 0u); queue = audio_cues_create(submix, 1); ASSERT_NE(queue, nullptr);
        open(AUDIO_BUS_RATE, 2, 256, 16384, 16384); ASSERT_TRUE(rdt_audio_gain(player, 1));
        voice = audio_voice_add(submix, rdt_audio_mix, player, nullptr, 0, false); ASSERT_NE(voice, 0u);
        ASSERT_TRUE(audio_submix_lock(submix)); ASSERT_TRUE(audio_cues_attach_locked(queue, 1, {1, player, voice}));
        audio_cues_clock_locked(queue, 0, 0, 1, false); audio_submix_unlock(submix);
    }
    void TearDown() override {
        audio_cues_destroy(queue); audio_voice_remove(voice); audio_submix_destroy(submix);
        DecodedAudioPlayer::TearDown(); audio_bus_set_offline(previous_offline);
    }
    AudioCueCommand command(AudioCueOp op, uint64_t frame) {
        AudioCueCommand result = {}; str_copy(result.source, sizeof(result.source), "source", 6);
        result.identity = 1; result.op = op; result.frame = frame; result.clip = op == AUDIO_CUE_PLAY ? 9 : 0; return result;
    }
};
TEST_F(NativeAudioCues, AContendedVoiceDropsTheCueWithoutBlockingOrDeliveringItLater) {
    peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr); auto event = command(AUDIO_CUE_PLAY, 17);
    AudioPlayerLease prepared{2, peer, 0}; ASSERT_TRUE(audio_submix_lock(submix));
    ASSERT_EQ(audio_cues_commit_locked(queue, 1, 1, &event, &prepared, 1, 0), nullptr); audio_submix_unlock(submix);
    ASSERT_TRUE(audio_voice_lock(voice)); float output[64 * 2]; ASSERT_TRUE(audio_submix_render(submix, output, 64)); audio_voice_unlock(voice);
    for (float sample : output) EXPECT_FLOAT_EQ(sample, 0);
    AudioCueSnapshot snapshot; ASSERT_TRUE(audio_submix_lock(submix)); audio_cues_snapshot_locked(queue, &snapshot); audio_submix_unlock(submix);
    ASSERT_EQ(snapshot.history_count, 1u); EXPECT_EQ(snapshot.history[0].status, AUDIO_CUE_BUSY); EXPECT_EQ(snapshot.history[0].actual, 17u);
    ASSERT_TRUE(audio_submix_render(submix, output, 64)); for (float sample : output) EXPECT_FLOAT_EQ(sample, 0);
}
TEST_F(NativeAudioCues, EqualSampleCommandsKeepAdmissionOrder) {
    peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr);
    AudioCueCommand events[] = {command(AUDIO_CUE_PLAY, 17), command(AUDIO_CUE_STOP, 17)};
    AudioPlayerLease prepared[] = {{2, peer, 0}, {}}; ASSERT_TRUE(audio_submix_lock(submix));
    ASSERT_EQ(audio_cues_commit_locked(queue, 1, 1, events, prepared, 2, 0), nullptr); audio_submix_unlock(submix);
    float output[64 * 2]; ASSERT_TRUE(audio_submix_render(submix, output, 64)); for (float sample : output) EXPECT_FLOAT_EQ(sample, 0);
    AudioCueSnapshot snapshot; ASSERT_TRUE(audio_submix_lock(submix)); audio_cues_snapshot_locked(queue, &snapshot); audio_submix_unlock(submix);
    ASSERT_EQ(snapshot.history_count, 2u); EXPECT_EQ(snapshot.history[0].id, 1u); EXPECT_EQ(snapshot.history[1].id, 2u);
    EXPECT_EQ(snapshot.history[0].status, AUDIO_CUE_APPLIED); EXPECT_EQ(snapshot.history[1].status, AUDIO_CUE_APPLIED);
}
TEST_F(NativeAudioCues, HistoryRetainsItsBoundAndExactWideDeadlinesNeverRoundThroughDouble) {
    AudioCueCommand events[AUDIO_CUE_BATCH]; AudioPlayerLease prepared[AUDIO_CUE_BATCH] = {};
    for (unsigned batch = 0; batch < 10; batch++) {
        for (auto& event : events) event = command(AUDIO_CUE_PAUSE, batch);
        ASSERT_TRUE(audio_submix_lock(submix)); ASSERT_EQ(audio_cues_commit_locked(queue, 1, batch + 1, events, prepared, AUDIO_CUE_BATCH, batch), nullptr);
        audio_submix_unlock(submix); float output[2]; ASSERT_TRUE(audio_submix_render(submix, output, 1));
    }
    AudioCueSnapshot snapshot; ASSERT_TRUE(audio_submix_lock(submix)); audio_cues_snapshot_locked(queue, &snapshot); audio_submix_unlock(submix);
    EXPECT_EQ(snapshot.history_count, 256u); EXPECT_EQ(snapshot.history_dropped, 64u); EXPECT_EQ(snapshot.history[0].id, 65u);
    auto wide = command(AUDIO_CUE_STOP, (UINT64_C(1) << 53) + 17); ASSERT_TRUE(audio_submix_lock(submix));
    ASSERT_EQ(audio_cues_commit_locked(queue, 1, 11, &wide, prepared, 1, 10), nullptr); audio_cues_snapshot_locked(queue, &snapshot);
    audio_submix_unlock(submix); ASSERT_EQ(snapshot.pending_count, 1u); EXPECT_EQ(snapshot.pending[0].requested, wide.frame);
}

TEST_F(DecodedAudioPlayer, CopiesStereoSamplesAndAddsOnlyLivingFrames) {
    open(AUDIO_BUS_RATE, 2, 16); ASSERT_TRUE(rdt_audio_play(player, 0.5f));
    float output[32 * 2]; for (float& value : output) value = 0.0625f;
    rdt_audio_mix(player, output, 32, 123);
    for (unsigned i = 0; i < 32; i++) {
        EXPECT_FLOAT_EQ(output[i * 2], i < 16 ? 0.3125f : 0.0625f);
        EXPECT_FLOAT_EQ(output[i * 2 + 1], i < 16 ? -0.0625f : 0.0625f);
    }
    EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ENDED); EXPECT_EQ(rdt_audio_frames(player), 16u);
}

TEST_F(DecodedAudioPlayer, PlatformDecoderReadsTheExistingVideoAudioTrack) {
#ifdef __APPLE__
    char* bytes = nullptr; size_t length = 0;
    ASSERT_TRUE(file_read_all_limit("test/media/test_video_audio.mp4", MEM_CAT_RENDER,
        RDT_AUDIO_SOURCE_LIMIT, &bytes, &length));
    player = rdt_audio_open_bytes(bytes, length); mem_free(bytes); ASSERT_NE(player, nullptr);
    ASSERT_TRUE(rdt_audio_play(player, 1)); float output[4096 * 2] = {}; rdt_audio_mix(player, output, 4096, 0);
    double energy = 0; for (float value : output) { EXPECT_TRUE(isfinite(value)); energy += value * value; }
    EXPECT_GT(energy, 0); RdtAudioInfo info; ASSERT_TRUE(rdt_audio_info(player, &info));
    EXPECT_GT(info.duration, 1); EXPECT_NEAR(info.position, 4096. / AUDIO_BUS_RATE, 1e-9);
#else
    GTEST_SKIP() << "platform video/audio decoder is macOS only";
#endif
}
TEST_F(DecodedAudioPlayer, OwnedFloatSnapshotsValidateSamplesAndRetainSharedPcmAcrossClones) {
    EXPECT_EQ(rdt_audio_pcm_alloc(0), nullptr); EXPECT_EQ(rdt_audio_pcm_alloc(SIZE_MAX), nullptr);
    auto* invalid = rdt_audio_pcm_alloc(4); ASSERT_NE(invalid, nullptr); invalid[3] = NAN;
    EXPECT_EQ(rdt_audio_adopt_pcm(invalid, 4), nullptr);
    auto* samples = rdt_audio_pcm_alloc(4); ASSERT_NE(samples, nullptr);
    for (unsigned i = 0; i < 4; i++) { samples[i * 2] = .5f; samples[i * 2 + 1] = -.25f; }
    player = rdt_audio_adopt_pcm(samples, 4); ASSERT_NE(player, nullptr);
    EXPECT_EQ(rdt_audio_storage_bytes(player), rdt_audio_pcm_storage_bytes(4)); peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr);
    rdt_audio_destroy(player); player = nullptr; ASSERT_TRUE(rdt_audio_play(peer, .5f)); float output[12] = {};
    rdt_audio_mix(peer, output, 6, 0);
    for (unsigned i = 0; i < 6; i++) { EXPECT_FLOAT_EQ(output[i * 2], i < 4 ? .25f : 0); EXPECT_FLOAT_EQ(output[i * 2 + 1], i < 4 ? -.125f : 0); }
}

TEST_F(DecodedAudioPlayer, PausePreservesCursorAndEndedPlaybackRestarts) {
    open(AUDIO_BUS_RATE, 2, 20); ASSERT_TRUE(rdt_audio_play(player, 1));
    float output[64 * 2] = {}; rdt_audio_mix(player, output, 8, 0);
    rdt_audio_pause(player); EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_PAUSED);
    memset(output, 0, sizeof(output)); rdt_audio_mix(player, output, 64, 8);
    for (float value : output) EXPECT_EQ(value, 0);
    ASSERT_TRUE(rdt_audio_play(player, 0)); rdt_audio_mix(player, output, 12, 72);
    EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ENDED);
    for (float value : output) EXPECT_EQ(value, 0);
    ASSERT_TRUE(rdt_audio_play(player, 1)); rdt_audio_mix(player, output, 20, 84);
    EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ENDED); EXPECT_FLOAT_EQ(output[0], 0.5f);
}

TEST_F(DecodedAudioPlayer, RejectsInvalidGainWithoutChangingPlayback) {
    open(AUDIO_BUS_RATE, 1, 16);
    EXPECT_FALSE(rdt_audio_play(player, NAN)); EXPECT_FALSE(rdt_audio_play(player, -0.1f));
    EXPECT_FALSE(rdt_audio_play(player, 1.1f)); EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_READY);
    ASSERT_TRUE(rdt_audio_play(player, 1)); EXPECT_FALSE(rdt_audio_play(player, INFINITY));
    EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_PLAYING);
    rdt_audio_fail(player); EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ERROR);
    EXPECT_FALSE(rdt_audio_play(player, 1)); rdt_audio_pause(player); EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ERROR);
}

TEST_F(DecodedAudioPlayer, ConvertsMonoAndSampleRateBeforeTheCallback) {
    open(24000, 1, 128); ASSERT_EQ(rdt_audio_frames(player), 256u);
    float output[256 * 2] = {}; ASSERT_TRUE(rdt_audio_play(player, 1)); rdt_audio_mix(player, output, 256, 0);
    // the anti-alias filter primes from silence; measure DC after its startup transient.
    for (unsigned i = 80; i < 200; i++) {
        EXPECT_NEAR(output[i * 2], 0.5f, 1e-5f); EXPECT_FLOAT_EQ(output[i * 2], output[i * 2 + 1]);
    }
    EXPECT_EQ(rdt_audio_state(player), RDT_AUDIO_ENDED);
}

TEST_F(DecodedAudioPlayer, RenderingPartitionsDoNotChangePcmOrPlayhead) {
    unsigned char bytes[44 + 64 * 4];
    size_t length = audio_fixture_wave(bytes, sizeof(bytes), AUDIO_BUS_RATE, 2, 64, 0, 0);
    for (unsigned i = 0; i < 128; i++) audio_fixture_le(bytes + 44 + i * 2, i * 64, 2);
    player = rdt_audio_open_bytes(bytes, length); ASSERT_NE(player, nullptr);
    peer = rdt_audio_open_bytes(bytes, length); ASSERT_NE(peer, nullptr);
    ASSERT_TRUE(rdt_audio_play(player, 0.75f)); ASSERT_TRUE(rdt_audio_play(peer, 0.75f));
    float all[128] = {}, parts[128] = {};
    rdt_audio_mix(player, all, 64, 100);
    rdt_audio_mix(peer, parts, 7, 100); rdt_audio_mix(peer, parts + 14, 11, 107);
    rdt_audio_mix(peer, parts + 36, 46, 118);
    for (unsigned i = 0; i < 128; i++) EXPECT_FLOAT_EQ(all[i], parts[i]);
    EXPECT_EQ(rdt_audio_state(peer), RDT_AUDIO_ENDED);
}

TEST_F(DecodedAudioPlayer, ValidatesSourceAndNativeStorageBeforePublication) {
    unsigned char bytes[44 + 64 * 4];
    size_t length = audio_fixture_wave(bytes, sizeof(bytes), AUDIO_BUS_RATE, 2, 64, 16384, 0);
    EXPECT_EQ(rdt_audio_open_bytes(nullptr, length), nullptr); EXPECT_EQ(rdt_audio_open_bytes(bytes, 0), nullptr);
    EXPECT_EQ(rdt_audio_open_bytes(bytes, (size_t)RDT_AUDIO_SOURCE_LIMIT + 1), nullptr);
    EXPECT_EQ(rdt_audio_open_bytes_limit(bytes, length, 0), nullptr);
    EXPECT_EQ(rdt_audio_open_bytes_limit(bytes, length, 256), nullptr);
    player = rdt_audio_open_bytes_limit(bytes, length, 1024); ASSERT_NE(player, nullptr);
    EXPECT_LE(rdt_audio_storage_bytes(player), 1024u);
    bytes[0] = '!'; EXPECT_EQ(rdt_audio_open_bytes(bytes, length), nullptr);
}

TEST_F(DecodedAudioPlayer, RejectsNonfiniteDecodedFloatPcm) {
    unsigned char bytes[44 + 4 * 4];
    size_t length = audio_fixture_wave(bytes, sizeof(bytes), AUDIO_BUS_RATE, 2, 4, 0, 0);
    audio_fixture_le(bytes + 20, 3, 2); audio_fixture_le(bytes + 34, 32, 2);
    audio_fixture_le(bytes + 28, AUDIO_BUS_RATE * 8, 4); audio_fixture_le(bytes + 32, 8, 2);
    float samples[4] = {0.5f, NAN, 0, 0}; memcpy(bytes + 44, samples, sizeof(samples));
    EXPECT_EQ(rdt_audio_open_bytes(bytes, length), nullptr);
}

TEST_F(DecodedAudioPlayer, RetainsAacPlatformDecodingWithoutOpeningOutput) {
#ifdef __APPLE__
    check_encoded_file("test/media/test_audio.m4a", AUDIO_BUS_RATE);
#else
    GTEST_SKIP() << "AAC uses the macOS platform decoder; portable PCM/MP3/FLAC paths have separate controls";
#endif
}

TEST_F(DecodedAudioPlayer, DecodesPortableMp3IntoOwnedFinitePcm) {
    check_encoded_file("ref/wpt/webcodecs/sfx.mp3", 0);
}

TEST_F(DecodedAudioPlayer, DecodesPortableFlacIntoOwnedFinitePcm) {
    check_encoded_file("ref/wpt/webcodecs/sfx.flac", 0);
}

TEST_F(DecodedAudioPlayer, SharedClonesRetainPcmAfterTheOriginalPlayerRetires) {
    open(AUDIO_BUS_RATE, 2, 16); auto before = rdt_audio_storage(player);
    peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr);
    auto copied = rdt_audio_storage(peer);
    EXPECT_EQ(copied.owner, before.owner); EXPECT_EQ(copied.shared_bytes, before.shared_bytes);
    rdt_audio_destroy(player); player = nullptr;
    ASSERT_TRUE(rdt_audio_play(peer, 1)); float output[32] = {}; rdt_audio_mix(peer, output, 16, 0);
    EXPECT_FLOAT_EQ(output[0], 0.5f); EXPECT_FLOAT_EQ(output[1], -0.25f);
    EXPECT_EQ(rdt_audio_state(peer), RDT_AUDIO_ENDED);
}

TEST_F(DecodedAudioPlayer, MonoPanPitchAndLoopingKeepIndependentNativeCursors) {
    unsigned char bytes[44 + 8 * 4]; size_t length = audio_fixture_wave(bytes, sizeof(bytes), AUDIO_BUS_RATE, 2, 8, 0, 0);
    for (unsigned i = 0; i < 8; i++) for (unsigned c = 0; c < 2; c++) audio_fixture_le(bytes + 44 + (i * 2 + c) * 2, i * 1024, 2);
    player = rdt_audio_open_bytes(bytes, length); ASSERT_NE(player, nullptr);
    peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr);
    auto parameters = rdt_audio_default_parameters(); parameters.mono = parameters.loop = true;
    parameters.left = 0; parameters.pitch = 1.5f; ASSERT_TRUE(rdt_audio_parameters(player, &parameters));
    ASSERT_TRUE(rdt_audio_play(player, 1)); float output[20] = {}; rdt_audio_mix(player, output, 10, 0);
    const double indices[] = {0, 1.5, 3, 4.5, 6, 3.5, 1, 2.5, 4, 5.5};
    for (unsigned i = 0; i < 10; i++) { EXPECT_EQ(output[i * 2], 0); EXPECT_NEAR(output[i * 2 + 1], indices[i] / 32, 1e-7); }
    RdtAudioInfo info = {}; ASSERT_TRUE(rdt_audio_info(player, &info));
    EXPECT_EQ(info.state, RDT_AUDIO_PLAYING); EXPECT_EQ(info.rendered, 10u); EXPECT_DOUBLE_EQ(info.pitch, 1.5);
    EXPECT_DOUBLE_EQ(info.position, 7.0 / AUDIO_BUS_RATE); EXPECT_EQ(rdt_audio_state(peer), RDT_AUDIO_READY);
}

TEST_F(DecodedAudioPlayer, SmoothedControlChangesAreIndependentOfCallbackPartitions) {
    open(AUDIO_BUS_RATE, 2, 32); peer = rdt_audio_clone(player); ASSERT_NE(peer, nullptr);
    auto parameters = rdt_audio_default_parameters(); parameters.loop = true; parameters.smoothing = .01f; parameters.pitch = 2;
    ASSERT_TRUE(rdt_audio_parameters(player, &parameters)); ASSERT_TRUE(rdt_audio_parameters(peer, &parameters));
    ASSERT_TRUE(rdt_audio_play(player, 1)); ASSERT_TRUE(rdt_audio_play(peer, 1)); float warm[512] = {};
    rdt_audio_mix(player, warm, 256, 0); memset(warm, 0, sizeof(warm)); rdt_audio_mix(peer, warm, 256, 0);
    parameters.left = 0; parameters.right = 1; parameters.pitch = .5f; parameters.lowpass = 600;
    ASSERT_TRUE(rdt_audio_parameters(player, &parameters)); ASSERT_TRUE(rdt_audio_parameters(peer, &parameters));
    float whole[1024] = {}, parts[1024] = {}; rdt_audio_mix(player, whole, 512, 256);
    rdt_audio_mix(peer, parts, 73, 256); rdt_audio_mix(peer, parts + 146, 439, 329);
    for (unsigned i = 0; i < 1024; i++) EXPECT_FLOAT_EQ(whole[i], parts[i]);
    RdtAudioInfo info = {}; ASSERT_TRUE(rdt_audio_info(player, &info)); EXPECT_GT(info.pitch, .5); EXPECT_LT(info.pitch, 2);
    EXPECT_LT(info.left, info.right); EXPECT_TRUE(isfinite(info.peak)); EXPECT_TRUE(isfinite(info.rms));
    parameters.pitch = NAN; EXPECT_FALSE(rdt_audio_parameters(player, &parameters));
}

TEST_F(DecodedAudioPlayer, NativeLowpassAttenuatesNyquistWithoutChangingItsSampleClock) {
    unsigned char bytes[44 + 16 * 4]; size_t length = audio_fixture_wave(bytes, sizeof(bytes), AUDIO_BUS_RATE, 2, 16, 0, 0);
    for (unsigned i = 0; i < 16; i++) for (unsigned c = 0; c < 2; c++)
        audio_fixture_le(bytes + 44 + (i * 2 + c) * 2, (uint16_t)(i % 2 ? -16384 : 16384), 2);
    player = rdt_audio_open_bytes(bytes, length); ASSERT_NE(player, nullptr);
    auto parameters = rdt_audio_default_parameters(); parameters.loop = true; parameters.lowpass = 200;
    ASSERT_TRUE(rdt_audio_parameters(player, &parameters)); ASSERT_TRUE(rdt_audio_play(player, 1));
    float output[4096 * 2] = {}; rdt_audio_mix(player, output, 4096, 0);
    double energy = 0; for (unsigned i = 2048 * 2; i < 4096 * 2; i++) energy += (double)output[i] * output[i];
    EXPECT_LT(sqrt(energy / 4096), .02);
    RdtAudioInfo info = {}; ASSERT_TRUE(rdt_audio_info(player, &info)); EXPECT_EQ(info.rendered, 4096u);
}

class NativeAudioGraph : public ::testing::Test {
protected:
    struct Signal {
        float left = .5f, right = .5f;
        double frequency = 0;
        uint64_t frame = 0, limit = UINT64_MAX;
        static void mix(void* owner, float* output, uint32_t frames, uint64_t) {
            auto* signal = (Signal*)owner;
            for (unsigned i = 0; i < frames; i++, signal->frame++) if (signal->frame < signal->limit) {
                double sample = signal->frequency ? sin(2 * M_PI * signal->frequency * signal->frame / AUDIO_BUS_RATE) : 1;
                output[i * 2] += (float)(signal->left * sample); output[i * 2 + 1] += (float)(signal->right * sample);
            }
        }
    } signals[8];
    AudioVoiceId voices[8] = {};
    AudioSubmixId submix = 0;
    AudioGraphId graph = 0;
    AudioGraphConfig config = {};
    float samples[4800 * 2];
    bool previous_offline;
    void SetUp() override {
        previous_offline = audio_bus_offline(); audio_bus_set_offline(true);
        submix = audio_submix_create(); ASSERT_NE(submix, 0u); graph = audio_submix_graph_open(submix); ASSERT_NE(graph, 0u);
        config.count = 1; config.buses[0] = audio_graph_bus_default("master"); config.buses[0].smoothing = 0;
    }
    void TearDown() override { audio_submix_destroy(submix); audio_bus_set_offline(previous_offline); }
    AudioBusConfig& bus(const char* id) {
        auto& value = config.buses[config.count++]; value = audio_graph_bus_default(id); value.smoothing = 0; return value;
    }
    bool publish() {
        return audio_fixture_graph_publish(submix, graph, config);
    }
    void voice(unsigned index, const char* bus_name = "master") {
        ASSERT_LT(index, 8u); auto route = bus_name ? audio_submix_graph_route(submix, graph, nullptr, bus_name) : 0;
        if (bus_name) ASSERT_NE(route, 0u);
        voices[index] = audio_voice_add(submix, Signal::mix, signals + index, nullptr, route); ASSERT_NE(voices[index], 0u);
    }
    void render(unsigned frames) { ASSERT_LE(frames, 4800u); ASSERT_TRUE(audio_submix_render(submix, samples, frames)); }
    void constant(float left, float right, unsigned frames = 32) {
        render(frames); for (unsigned i = 0; i < frames; i++) {
            EXPECT_NEAR(samples[i * 2], left, 2e-6); EXPECT_NEAR(samples[i * 2 + 1], right, 2e-6);
        }
    }
    AudioGraphSnapshot snapshot() {
        AudioGraphSnapshot value = {}; EXPECT_TRUE(audio_submix_graph_snapshot(submix, graph, &value)); return value;
    }
    void insert(AudioEffectKind kind) { config.buses[0].insert_count = 1; config.buses[0].inserts[0] = audio_effect_default(kind); }
};

TEST_F(NativeAudioGraph, ParentAndExplicitSendsSumWithDefaultVoicesAndCopiedMeters) {
    auto& effects = bus("effects"); effects.gain = .5f; str_copy(effects.parent, 128, "group", 5);
    effects.send_count = 1; str_copy(effects.sends[0].bus, 128, "return", 6); effects.sends[0].gain = .5f;
    bus("group").gain = .5f; bus("return").gain = .25f;
    ASSERT_TRUE(publish()); voice(0, "effects"); signals[1].left = signals[1].right = .1f; voice(1, nullptr);
    constant(.25625f, .25625f);
    auto copied = snapshot(); EXPECT_NEAR(copied.meters[0].peak, .15625, 1e-7);
    EXPECT_NEAR(copied.meters[1].rms, .25, 1e-7); EXPECT_EQ(copied.meters[0].frames, 32u);
    copied.config.buses[1].gain = 4; constant(.25625f, .25625f);
}
TEST_F(NativeAudioGraph, SoloSelectsInputsAndSendPathsWhileMuteSilencesTheBus) {
    auto& effects = bus("effects"); effects.solo = true; effects.send_count = 1;
    str_copy(effects.sends[0].bus, 128, "return", 6); effects.sends[0].gain = 1;
    bus("other"); bus("return"); ASSERT_TRUE(publish());
    signals[0].left = signals[0].right = .25f; voice(0, "effects"); voice(1, "other");
    signals[2].left = signals[2].right = .1f; voice(2, "master"); constant(.5f, .5f);
    config.buses[1].mute = true; ASSERT_TRUE(publish()); constant(0, 0);
    config.buses[1].mute = config.buses[1].solo = false; config.buses[0].solo = true; ASSERT_TRUE(publish()); constant(1, 1);
}
TEST_F(NativeAudioGraph, SendGainEditsSmoothOnTheSampleClockWithoutResettingTheBus) {
    auto& effects = bus("effects"); effects.smoothing = .01f; effects.send_count = 1;
    str_copy(effects.sends[0].bus, 128, "return", 6); effects.sends[0].gain = 1; bus("return");
    ASSERT_TRUE(publish()); signals[0].left = signals[0].right = .25f; voice(0, "effects"); constant(.5f, .5f);
    config.buses[1].sends[0].gain = 0; ASSERT_TRUE(publish()); render(64);
    for (unsigned i = 0; i < 64; i++) EXPECT_NEAR(samples[i * 2], .25 * (1 + exp(-(i + 1.0) / 480.0)), 2e-6);
    EXPECT_NEAR(snapshot().send_gains[1][0], exp(-64.0 / 480), 2e-7);
}
TEST_F(NativeAudioGraph, InvalidTopologyAndStorageRejectWithoutReplacingTheLivePlan) {
    bus("effects").gain = .25f; ASSERT_TRUE(publish()); voice(0, "effects"); constant(.125f, .125f);
    auto route = snapshot().routes[1];
    str_copy(config.buses[1].parent, 128, "missing", 7); EXPECT_FALSE(publish()); constant(.125f, .125f);
    str_copy(config.buses[1].parent, 128, "master", 6); config.buses[0].send_count = 1;
    str_copy(config.buses[0].sends[0].bus, 128, "effects", 7); config.buses[0].sends[0].gain = 1;
    EXPECT_FALSE(publish()); EXPECT_EQ(snapshot().routes[1], route); constant(.125f, .125f);
    config.buses[0].send_count = 0; config.buses[1].gain = NAN; EXPECT_FALSE(publish()); config.buses[1].gain = .25f;
    for (unsigned i = 0; i < 16; i++) {
        if (i >= config.count) { char id[32]; snprintf(id, sizeof(id), "bus%u", i); bus(id); }
        auto& b = config.buses[i]; b.insert_count = AUDIO_GRAPH_INSERTS;
        for (auto& effect : b.inserts) { effect = audio_effect_default(AUDIO_EFFECT_DELAY); effect.seconds = 2; }
    }
    EXPECT_FALSE(publish()); constant(.125f, .125f); EXPECT_EQ(snapshot().routes[1], route);
}
TEST_F(NativeAudioGraph, StalePreparedPlansAndRemovedRouteHandlesStayInert) {
    bus("effects"); ASSERT_TRUE(publish()); auto route = snapshot().routes[1];
    AudioGraph *first = nullptr, *second = nullptr; const char* error = nullptr;
    config.buses[1].gain = .5f; ASSERT_TRUE(audio_submix_graph_prepare(submix, graph, config, &first, &error));
    config.buses[1].gain = .25f; ASSERT_TRUE(audio_submix_graph_prepare(submix, graph, config, &second, &error));
    ASSERT_TRUE(audio_submix_graph_commit(submix, graph, first, nullptr, 0));
    EXPECT_FALSE(audio_submix_graph_commit(submix, graph, second, nullptr, 0)); audio_graph_destroy(second);
    EXPECT_EQ(snapshot().routes[1], route); config.count = 1; ASSERT_TRUE(publish());
    EXPECT_FALSE(audio_submix_route_valid(submix, route)); EXPECT_EQ(audio_voice_add(submix, Signal::mix, signals, nullptr, route), 0u);
    bus("effects"); ASSERT_TRUE(publish()); EXPECT_NE(snapshot().routes[1], route);
}
TEST_F(NativeAudioGraph, RetainedRoutesRejectRemovalBeforeSourcePublicationWithoutKeepingOutputAlive) {
    bus("music"); ASSERT_TRUE(publish()); auto route = snapshot().routes[1]; AudioRouteRef ref = 0;
    ASSERT_TRUE(audio_submix_bind_route(submix, &ref, 0, route)); EXPECT_TRUE(audio_submix_route_ref_valid(submix, ref));
    EXPECT_FALSE(audio_submix_has_voices(submix)); EXPECT_FALSE(audio_submix_has_work(submix)); config.count = 1;
    AudioGraph* prepared = nullptr; const char* error = nullptr;
    EXPECT_FALSE(audio_submix_graph_prepare(submix, graph, config, &prepared, &error)); EXPECT_EQ(prepared, nullptr); ASSERT_NE(error, nullptr);
    EXPECT_NE(strstr(error, "provider"), nullptr); EXPECT_EQ(snapshot().config.count, 2u);
    auto stale = ref; ASSERT_TRUE(audio_submix_bind_route(submix, &ref, 0, 0)); EXPECT_EQ(ref, 0u); ASSERT_TRUE(publish());
    EXPECT_FALSE(audio_submix_route_ref_valid(submix, stale)); EXPECT_FALSE(audio_submix_route_valid(submix, route));
}
TEST_F(NativeAudioGraph, PreparedRemovalRechecksReferencesAddedAfterCompilation) {
    bus("music"); ASSERT_TRUE(publish()); auto route = snapshot().routes[1]; config.count = 1;
    AudioGraph* prepared = nullptr; const char* error = nullptr; ASSERT_TRUE(audio_submix_graph_prepare(submix, graph, config, &prepared, &error));
    AudioRouteRef ref = 0; ASSERT_TRUE(audio_submix_bind_route(submix, &ref, 0, route));
    EXPECT_FALSE(audio_submix_graph_commit(submix, graph, prepared, nullptr, 0, &error)); EXPECT_EQ(snapshot().config.count, 2u);
    audio_submix_release_route(submix, ref); ASSERT_TRUE(audio_submix_graph_commit(submix, graph, prepared, nullptr, 0, &error));
    EXPECT_EQ(snapshot().config.count, 1u);
}
TEST_F(NativeAudioGraph, GraphCloseSilencesVoicesAndRetiresReferencesWithoutDryFallback) {
    bus("music"); ASSERT_TRUE(publish()); voice(0, "music"); constant(.5f, .5f); AudioRouteRef ref = 0;
    ASSERT_TRUE(audio_submix_bind_route(submix, &ref, voices[0], snapshot().routes[1])); audio_submix_graph_close(submix, graph); graph = 0;
    EXPECT_FALSE(audio_submix_route_ref_valid(submix, ref)); EXPECT_EQ(audio_voice_loss(voices[0]), AUDIO_VOICE_ROUTE_LOST);
    EXPECT_FALSE(audio_submix_has_work(submix)); constant(0, 0);
    audio_voice_remove(voices[0]); EXPECT_EQ(audio_voice_loss(voices[0]), AUDIO_VOICE_OK); audio_submix_release_route(submix, ref);
}
TEST_F(NativeAudioGraph, RouteReferenceCapacityAndStaleHandlesPreserveExistingConsumers) {
    bus("music"); ASSERT_TRUE(publish()); auto route = snapshot().routes[1]; AudioRouteRef refs[AUDIO_ROUTE_REFERENCES] = {};
    for (auto& ref : refs) ASSERT_TRUE(audio_submix_bind_route(submix, &ref, 0, route)); AudioRouteRef overflow = 0;
    EXPECT_FALSE(audio_submix_bind_route(submix, &overflow, 0, route)); EXPECT_EQ(overflow, 0u); auto old = refs[0];
    audio_submix_release_route(submix, old); ASSERT_TRUE(audio_submix_bind_route(submix, &overflow, 0, route)); EXPECT_NE(overflow, old);
    EXPECT_FALSE(audio_submix_bind_route(submix, &old, 0, route)); audio_submix_release_route(submix, old); EXPECT_TRUE(audio_submix_route_ref_valid(submix, overflow));
    for (auto ref : refs) audio_submix_release_route(submix, ref); audio_submix_release_route(submix, overflow);
    config.count = 1; ASSERT_TRUE(publish());
}
TEST_F(NativeAudioGraph, LowHighPassAndPeakingEqMatchIndependentSignals) {
    for (auto kind : {AUDIO_EFFECT_LOWPASS, AUDIO_EFFECT_HIGHPASS, AUDIO_EFFECT_EQ}) {
        insert(kind); auto& effect = config.buses[0].inserts[0]; effect.frequency = 1000; effect.gain_db = 6;
        ASSERT_TRUE(publish()); audio_submix_graph_reset(submix, graph);
        signals[0] = {}; signals[0].left = signals[0].right = .1f;
        signals[0].frequency = kind == AUDIO_EFFECT_HIGHPASS ? 0 : kind == AUDIO_EFFECT_LOWPASS ? 12000 : 1000;
        voice(0); render(4800); double sum = 0;
        for (unsigned i = 2400; i < 4800; i++) sum += (double)samples[i * 2] * samples[i * 2];
        double rms = sqrt(sum / 2400);
        if (kind == AUDIO_EFFECT_EQ) EXPECT_NEAR(rms, .1 / sqrt(2) * pow(10, .3), 2e-5);
        else EXPECT_LT(rms, kind == AUDIO_EFFECT_LOWPASS ? .001 : 1e-6);
        audio_voice_remove(voices[0]);
    }
}
TEST_F(NativeAudioGraph, DelayHasExactSampleEchoesAndDrainsAfterItsSourceRetires) {
    insert(AUDIO_EFFECT_DELAY); auto& effect = config.buses[0].inserts[0]; effect.seconds = 4.f / AUDIO_BUS_RATE;
    effect.feedback = .5f; effect.wet = 1; ASSERT_TRUE(publish()); signals[0].left = signals[0].right = 1; signals[0].limit = 1;
    voice(0); render(1); EXPECT_EQ(samples[0], 0); audio_voice_remove(voices[0]); EXPECT_TRUE(audio_submix_has_work(submix));
    render(16); EXPECT_FLOAT_EQ(samples[3 * 2], 1); EXPECT_FLOAT_EQ(samples[7 * 2], .5f); EXPECT_FLOAT_EQ(samples[11 * 2], .25f);
    render(1024); EXPECT_FALSE(audio_submix_has_work(submix));
}
TEST_F(NativeAudioGraph, CompatibleGainAndWetEditsPreserveDelayedHistoryAndRouteIdentity) {
    insert(AUDIO_EFFECT_DELAY); auto& effect = config.buses[0].inserts[0]; effect.seconds = 4.f / AUDIO_BUS_RATE;
    effect.feedback = 0; effect.wet = 1; ASSERT_TRUE(publish()); auto route = snapshot().routes[0];
    signals[0].left = signals[0].right = 1; signals[0].limit = 1; voice(0); render(1); audio_voice_remove(voices[0]);
    config.buses[0].gain = .5f; effect.wet = .5f; ASSERT_TRUE(publish()); EXPECT_EQ(snapshot().routes[0], route);
    render(4); EXPECT_FLOAT_EQ(samples[3 * 2], .25f); EXPECT_FALSE(audio_submix_has_work(submix));
}
TEST_F(NativeAudioGraph, ReverbTailsSurviveVoiceRetirementAndResetClearsTheirWork) {
    insert(AUDIO_EFFECT_REVERB); config.buses[0].inserts[0].wet = 1; ASSERT_TRUE(publish());
    signals[0].left = signals[0].right = 1; signals[0].limit = 1; voice(0); render(1); audio_voice_remove(voices[0]);
    EXPECT_TRUE(audio_submix_has_work(submix)); render(4800); double energy = 0;
    for (float value : samples) energy += value * value; EXPECT_GT(energy, 0); EXPECT_TRUE(audio_submix_has_work(submix));
    audio_submix_graph_reset(submix, graph); EXPECT_FALSE(audio_submix_has_work(submix)); render(4800);
    for (float value : samples) EXPECT_EQ(value, 0);
}
TEST_F(NativeAudioGraph, SidechainCompressionUsesAnEarlierBusAndReleasesAfterItsKeyStops) {
    auto& music = bus("music"); music.insert_count = 1; music.inserts[0] = audio_effect_default(AUDIO_EFFECT_COMPRESSOR);
    auto& compressor = music.inserts[0]; compressor.threshold = -24; compressor.ratio = 4; compressor.attack = 0;
    str_copy(compressor.sidechain, 128, "key", 3); bus("key"); ASSERT_TRUE(publish());
    signals[0].left = signals[0].right = .25f; signals[1].left = signals[1].right = .25f;
    voice(0, "music"); voice(1, "key"); float compressed = (float)(.25 * pow(.25 / pow(10, -24.0 / 20), -.75));
    constant(.25f + compressed, .25f + compressed); EXPECT_NEAR(snapshot().meters[1].peak, compressed, 2e-6);
    audio_voice_remove(voices[1]); for (unsigned i = 0; i < 10; i++) render(4800); constant(.25f, .25f);
}
TEST_F(NativeAudioGraph, LimiterLinksStereoChannelsAtItsConfiguredCeiling) {
    insert(AUDIO_EFFECT_LIMITER); config.buses[0].inserts[0].ceiling = .5f; ASSERT_TRUE(publish());
    signals[0].left = .8f; signals[0].right = .4f; voice(0); constant(.5f, .25f);
}
TEST_F(NativeAudioGraph, DeviceLossResetsTailWorkAndNeverRestoresOldDelayedSamples) {
    insert(AUDIO_EFFECT_DELAY); config.buses[0].inserts[0].seconds = 4.f / AUDIO_BUS_RATE; config.buses[0].inserts[0].wet = 1;
    ASSERT_TRUE(publish()); signals[0].limit = 1; voice(0); render(1); EXPECT_TRUE(audio_submix_has_work(submix));
    audio_bus_retire_output(); EXPECT_FALSE(audio_submix_has_work(submix)); audio_voice_remove(voices[0]); render(32);
    for (unsigned i = 0; i < 64; i++) EXPECT_EQ(samples[i], 0);
}
TEST_F(NativeAudioGraph, DeviceKernelSkipsUnstartedAndIdleGraphsWhileAnotherDocumentPlays) {
    ASSERT_TRUE(publish()); auto other = audio_submix_create(); ASSERT_NE(other, 0u);
    auto voice_id = audio_voice_add(other, Signal::mix, signals, nullptr); ASSERT_NE(voice_id, 0u);
    audio_bus_mix(samples, 64); EXPECT_EQ(audio_submix_clock(other), 64u); EXPECT_EQ(audio_submix_clock(submix), 0u);
    voice(1); audio_bus_mix(samples, 64); EXPECT_EQ(audio_submix_clock(submix), 64u);
    audio_voice_remove(voices[1]); audio_bus_mix(samples, 64); EXPECT_EQ(audio_submix_clock(submix), 64u);
    audio_submix_destroy(other); EXPECT_FALSE(audio_submix_has_work(submix));
}
TEST_F(NativeAudioGraph, NonfiniteProducerFaultsTheGraphWithoutPublishingInvalidOutput) {
    ASSERT_TRUE(publish()); signals[0].left = INFINITY; voice(0); render(64);
    EXPECT_TRUE(snapshot().faulted); for (unsigned i = 0; i < 128; i++) EXPECT_TRUE(isfinite(samples[i]));
    signals[0].left = .5f; audio_submix_graph_reset(submix, graph); constant(.5f, .5f); EXPECT_FALSE(snapshot().faulted);
}
TEST_F(NativeAudioGraph, OrderedInsertsAndGainSmoothingAreIndependentOfRenderPartitions) {
    auto& b = config.buses[0]; b.insert_count = 4; b.smoothing = .01f;
    b.inserts[0] = audio_effect_default(AUDIO_EFFECT_DELAY); b.inserts[0].seconds = 13.f / AUDIO_BUS_RATE;
    b.inserts[1] = audio_effect_default(AUDIO_EFFECT_EQ); b.inserts[1].gain_db = 6;
    b.inserts[2] = audio_effect_default(AUDIO_EFFECT_COMPRESSOR); b.inserts[2].threshold = -24;
    b.inserts[3] = audio_effect_default(AUDIO_EFFECT_REVERB);
    const char* error = nullptr; auto* a = audio_graph_compile(config, 0, nullptr, &error); ASSERT_NE(a, nullptr);
    auto* peer = audio_graph_compile(config, 0, nullptr, &error); ASSERT_NE(peer, nullptr);
    float input[4800 * 2], out[4800 * 2] = {}, reference[4800 * 2] = {};
    for (unsigned i = 0; i < 4800; i++) input[i * 2] = input[i * 2 + 1] = (float)(.5 * sin(i * .1));
    for (unsigned side = 0; side < 2; side++) {
        auto* plan = side ? peer : a; float* output = side ? reference : out;
        auto route = audio_graph_route(plan, "master");
        for (unsigned at = 0; at < 4800;) {
            if (at == 2400) {
                config.buses[0].gain = .25f;
                auto* edited = audio_graph_compile(config, 0, plan, &error); ASSERT_NE(edited, nullptr);
                audio_graph_adopt_state(edited, plan); audio_graph_destroy(plan); plan = edited;
                route = audio_graph_route(plan, "master");
            }
            unsigned block = side ? (at % 2 ? 17 : 113) : 256; if (block > 4800 - at) block = 4800 - at;
            if (at < 2400 && block > 2400 - at) block = 2400 - at;
            audio_graph_begin(plan, block); ASSERT_TRUE(audio_graph_input(plan, route, input + at * 2, block));
            audio_graph_process(plan, output + at * 2, block); at += block;
        }
        if (side) peer = plan; else a = plan;
    }
    for (unsigned i = 0; i < 4800 * 2; i++) EXPECT_NEAR(out[i], reference[i], 2e-6);
    audio_graph_destroy(a); audio_graph_destroy(peer);
}
