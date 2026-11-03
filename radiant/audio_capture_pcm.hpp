#pragma once
#include "audio_bus.hpp"

// D7.5.9: capture timestamps travel with samples; overrun drops input rather than inventing silence.
static constexpr unsigned AUDIO_CAPTURE_CAPACITY = 2 * AUDIO_BUS_RATE, AUDIO_CAPTURE_PACKET = 4096;
struct AudioCapturePcm;
struct AudioCapturePcmInfo {
    uint64_t next_frame, captured, dropped, monitor_dropped, read_frames;
    unsigned queued, monitor_queued, capacity;
    float peak, rms;
    bool faulted, monitoring;
};
size_t audio_capture_pcm_bytes(unsigned capacity = AUDIO_CAPTURE_CAPACITY);
AudioCapturePcm* audio_capture_pcm_create(unsigned capacity = AUDIO_CAPTURE_CAPACITY);
// the producer and monitor voice must be retired before destruction (D4.5.2).
void audio_capture_pcm_destroy(AudioCapturePcm* pcm);
// one producer only; no allocation, waiting, DOM, GC or scripts. Frame stamps never regress.
unsigned audio_capture_pcm_write(AudioCapturePcm* pcm, uint64_t frame, const float* stereo, unsigned frames);
// one host consumer; copies one contiguous sample-time interval, at most AUDIO_CAPTURE_PACKET frames.
unsigned audio_capture_pcm_read(AudioCapturePcm* pcm, float* stereo, unsigned frames, uint64_t* first_frame);
AudioCapturePcmInfo audio_capture_pcm_info(AudioCapturePcm* pcm);
// control thread excludes the monitor voice before reconfiguration; capture callbacks never wait.
bool audio_capture_pcm_monitor(AudioCapturePcm* pcm, bool enabled, float gain);
void audio_capture_pcm_mix(void* pcm, float* output, uint32_t frames, uint64_t clock);
