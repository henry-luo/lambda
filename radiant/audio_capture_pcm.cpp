#include "audio_capture_pcm.hpp"
#include "audio_miniaudio.hpp"
#include "../lib/mem.h"
#include <math.h>

struct CaptureFrame { uint64_t frame; float left, right; };
struct CaptureQueue { ma_rb ring; CaptureFrame* frames; unsigned capacity; };
struct AudioCapturePcm {
    CaptureQueue record, monitor;
    uint64_t next_frame, captured, dropped, monitor_dropped, read_frames;
    int producer_lock, faulted, monitoring;
    float gain, peak, rms;
};
size_t audio_capture_pcm_bytes(unsigned capacity) {
    return capacity && capacity <= AUDIO_CAPTURE_CAPACITY ? sizeof(AudioCapturePcm) + (size_t)capacity * 2 * sizeof(CaptureFrame) : 0;
}
static bool capture_queue_create(CaptureQueue* queue, unsigned capacity) {
    queue->frames = (CaptureFrame*)mem_calloc(capacity, sizeof(CaptureFrame), MEM_CAT_RENDER);
    if (!queue->frames || ma_rb_init((size_t)capacity * sizeof(CaptureFrame), queue->frames, nullptr, &queue->ring) != MA_SUCCESS) return false;
    queue->capacity = capacity; return true;
}
static void capture_queue_destroy(CaptureQueue* queue) {
    if (queue->capacity) ma_rb_uninit(&queue->ring);
    mem_free(queue->frames);
}
AudioCapturePcm* audio_capture_pcm_create(unsigned capacity) {
    if (!audio_capture_pcm_bytes(capacity)) return nullptr;
    auto* pcm = (AudioCapturePcm*)mem_calloc(1, sizeof(AudioCapturePcm), MEM_CAT_RENDER); if (!pcm) return nullptr;
    if (!capture_queue_create(&pcm->record, capacity) || !capture_queue_create(&pcm->monitor, capacity)) {
        audio_capture_pcm_destroy(pcm); return nullptr;
    }
    pcm->gain = 1; return pcm;
}
void audio_capture_pcm_destroy(AudioCapturePcm* pcm) {
    if (!pcm) return;
    capture_queue_destroy(&pcm->record); capture_queue_destroy(&pcm->monitor); mem_free(pcm);
}
static unsigned capture_queue_write(CaptureQueue* queue, uint64_t frame, const float* input, unsigned frames) {
    unsigned done = 0;
    while (done < frames) {
        size_t bytes = (size_t)(frames - done) * sizeof(CaptureFrame); void* target = nullptr;
        if (ma_rb_acquire_write(&queue->ring, &bytes, &target) != MA_SUCCESS || !bytes) break;
        unsigned count = (unsigned)(bytes / sizeof(CaptureFrame)); auto* output = (CaptureFrame*)target;
        for (unsigned i = 0; i < count; i++) output[i] = {frame + done + i,
            input ? input[(size_t)(done + i) * 2] : 0, input ? input[(size_t)(done + i) * 2 + 1] : 0};
        ma_rb_commit_write(&queue->ring, count * sizeof(CaptureFrame)); done += count;
    }
    return done;
}
unsigned audio_capture_pcm_write(AudioCapturePcm* pcm, uint64_t frame, const float* stereo, unsigned frames) {
    if (!pcm || !frames || __atomic_load_n(&pcm->faulted, __ATOMIC_ACQUIRE)) return 0;
    uint64_t next = __atomic_load_n(&pcm->next_frame, __ATOMIC_RELAXED);
    if (frame < next || frames > UINT64_MAX - frame) { __atomic_store_n(&pcm->faulted, 1, __ATOMIC_RELEASE); return 0; }
    unsigned count = frames < AUDIO_CAPTURE_PACKET ? frames : AUDIO_CAPTURE_PACKET;
    double sum = 0; float peak = 0;
    if (stereo) for (unsigned i = 0; i < count * 2; i++) {
        float value = stereo[i];
        if (!isfinite(value)) { __atomic_store_n(&pcm->faulted, 1, __ATOMIC_RELEASE); return 0; }
        sum += (double)value * value; float magnitude = fabsf(value); if (magnitude > peak) peak = magnitude;
    }
    __atomic_store_n(&pcm->next_frame, frame + frames, __ATOMIC_RELEASE);
    __atomic_fetch_add(&pcm->captured, frames, __ATOMIC_RELEASE);
    float rms = (float)sqrt(sum / (count * 2));
    __atomic_store(&pcm->peak, &peak, __ATOMIC_RELEASE); __atomic_store(&pcm->rms, &rms, __ATOMIC_RELEASE);
    int idle = 0; unsigned written = 0, monitored = 0;
    bool enabled = __atomic_load_n(&pcm->monitoring, __ATOMIC_ACQUIRE) != 0;
    if (__atomic_compare_exchange_n(&pcm->producer_lock, &idle, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        written = capture_queue_write(&pcm->record, frame, stereo, count);
        enabled = __atomic_load_n(&pcm->monitoring, __ATOMIC_ACQUIRE) != 0;
        if (enabled) monitored = capture_queue_write(&pcm->monitor, frame, stereo, count);
        __atomic_store_n(&pcm->producer_lock, 0, __ATOMIC_RELEASE);
    }
    __atomic_fetch_add(&pcm->dropped, frame - next + frames - written, __ATOMIC_RELEASE);
    if (enabled) __atomic_fetch_add(&pcm->monitor_dropped, frame - next + frames - monitored, __ATOMIC_RELEASE);
    return written;
}
static unsigned capture_queue_read(CaptureQueue* queue, float* output, unsigned frames, uint64_t* first, float gain, bool add) {
    unsigned done = 0;
    while (done < frames) {
        size_t bytes = (size_t)(frames - done) * sizeof(CaptureFrame); void* source = nullptr;
        if (ma_rb_acquire_read(&queue->ring, &bytes, &source) != MA_SUCCESS || !bytes) break;
        auto* input = (const CaptureFrame*)source; unsigned available = (unsigned)(bytes / sizeof(CaptureFrame)), count = 0;
        if (!done) *first = input[0].frame;
        // keep a discontinuity queued for the next host read, with its own exact first-frame stamp.
        while (count < available && (add || input[count].frame == *first + done + count)) {
            size_t index = (size_t)(done + count) * 2;
            if (add) { output[index] += input[count].left * gain; output[index + 1] += input[count].right * gain; }
            else { output[index] = input[count].left; output[index + 1] = input[count].right; }
            count++;
        }
        ma_rb_commit_read(&queue->ring, count * sizeof(CaptureFrame)); done += count;
        if (count != available) break;
    }
    return done;
}
unsigned audio_capture_pcm_read(AudioCapturePcm* pcm, float* stereo, unsigned frames, uint64_t* first_frame) {
    if (!pcm || !stereo || !first_frame || !frames || frames > AUDIO_CAPTURE_PACKET) return 0;
    unsigned count = capture_queue_read(&pcm->record, stereo, frames, first_frame, 1, false);
    __atomic_fetch_add(&pcm->read_frames, count, __ATOMIC_RELEASE); return count;
}
AudioCapturePcmInfo audio_capture_pcm_info(AudioCapturePcm* pcm) {
    if (!pcm) return {};
    AudioCapturePcmInfo result = {};
    result.next_frame = __atomic_load_n(&pcm->next_frame, __ATOMIC_ACQUIRE); result.captured = __atomic_load_n(&pcm->captured, __ATOMIC_ACQUIRE);
    result.dropped = __atomic_load_n(&pcm->dropped, __ATOMIC_ACQUIRE); result.monitor_dropped = __atomic_load_n(&pcm->monitor_dropped, __ATOMIC_ACQUIRE);
    result.read_frames = __atomic_load_n(&pcm->read_frames, __ATOMIC_ACQUIRE);
    result.queued = (unsigned)(ma_rb_available_read(&pcm->record.ring) / sizeof(CaptureFrame));
    result.monitor_queued = (unsigned)(ma_rb_available_read(&pcm->monitor.ring) / sizeof(CaptureFrame)); result.capacity = pcm->record.capacity;
    __atomic_load(&pcm->peak, &result.peak, __ATOMIC_ACQUIRE); __atomic_load(&pcm->rms, &result.rms, __ATOMIC_ACQUIRE);
    result.faulted = __atomic_load_n(&pcm->faulted, __ATOMIC_ACQUIRE); result.monitoring = __atomic_load_n(&pcm->monitoring, __ATOMIC_ACQUIRE);
    return result;
}
bool audio_capture_pcm_monitor(AudioCapturePcm* pcm, bool enabled, float gain) {
    if (!pcm || !isfinite(gain) || gain < 0 || gain > 1) return false;
    // the host has excluded the output reader; only the producer needs a brief exclusion here.
    for (;;) {
        int idle = 0;
        if (__atomic_compare_exchange_n(&pcm->producer_lock, &idle, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) break;
        audio_control_yield();
    }
    ma_rb_reset(&pcm->monitor.ring); __atomic_store(&pcm->gain, &gain, __ATOMIC_RELEASE);
    __atomic_store_n(&pcm->monitoring, enabled, __ATOMIC_RELEASE); __atomic_store_n(&pcm->producer_lock, 0, __ATOMIC_RELEASE); return true;
}
void audio_capture_pcm_mix(void* source, float* output, uint32_t frames, uint64_t) {
    auto* pcm = (AudioCapturePcm*)source;
    if (!pcm || !output || __atomic_load_n(&pcm->faulted, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&pcm->monitoring, __ATOMIC_ACQUIRE)) return;
    float gain; __atomic_load(&pcm->gain, &gain, __ATOMIC_ACQUIRE); uint64_t first = 0;
    capture_queue_read(&pcm->monitor, output, frames, &first, gain, true);
}
