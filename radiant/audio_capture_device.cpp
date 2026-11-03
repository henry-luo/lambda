#include "audio_capture.hpp"
#include "audio_miniaudio.hpp"
#include "../lib/mem.h"

#ifdef __APPLE__
AudioCapturePermission audio_capture_mac_permission(bool request);
#endif
struct CaptureDevice { ma_device device; AudioCapturePcm* sink; uint64_t frame; int stopping, lost; };
static void capture_device_data(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* capture = (CaptureDevice*)device->pUserData;
    if (__atomic_load_n(&capture->stopping, __ATOMIC_ACQUIRE) || __atomic_load_n(&capture->lost, __ATOMIC_ACQUIRE)) return;
    if (frames > UINT64_MAX - capture->frame) { __atomic_store_n(&capture->lost, 1, __ATOMIC_RELEASE); return; }
    audio_capture_pcm_write(capture->sink, capture->frame, (const float*)input, frames); capture->frame += frames;
}
static void capture_device_notice(const ma_device_notification* notice) {
    auto* capture = (CaptureDevice*)notice->pDevice->pUserData;
    if (!__atomic_load_n(&capture->stopping, __ATOMIC_ACQUIRE) &&
        (notice->type == ma_device_notification_type_stopped || notice->type == ma_device_notification_type_interruption_began))
        __atomic_store_n(&capture->lost, 1, __ATOMIC_RELEASE);
}
static AudioCapturePermission capture_permission(void*, bool request) {
#ifdef __APPLE__
    return audio_capture_mac_permission(request);
#else
    // other hosts must provide their OS permission owner before enabling input.
    return AUDIO_CAPTURE_UNAVAILABLE;
#endif
}
static void* capture_device_open(void*, AudioCapturePcm* sink) {
    auto* capture = (CaptureDevice*)mem_calloc(1, sizeof(CaptureDevice), MEM_CAT_RENDER); if (!capture) return nullptr;
    capture->sink = sink; ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32; config.capture.channels = 2; config.sampleRate = AUDIO_BUS_RATE;
    config.periodSizeInFrames = 256; config.dataCallback = capture_device_data;
    config.notificationCallback = capture_device_notice; config.pUserData = capture;
    if (ma_device_init(nullptr, &config, &capture->device) != MA_SUCCESS) { mem_free(capture); return nullptr; }
    if (capture->device.pContext->backend == ma_backend_null || ma_device_start(&capture->device) != MA_SUCCESS) {
        __atomic_store_n(&capture->stopping, 1, __ATOMIC_RELEASE); ma_device_uninit(&capture->device); mem_free(capture); return nullptr;
    }
    return capture;
}
static bool capture_device_lost(void*, void* device) {
    return __atomic_load_n(&((CaptureDevice*)device)->lost, __ATOMIC_ACQUIRE) != 0;
}
static void capture_device_close(void*, void* device) {
    auto* capture = (CaptureDevice*)device;
    __atomic_store_n(&capture->stopping, 1, __ATOMIC_RELEASE);
    ma_device_uninit(&capture->device); mem_free(capture);
}
const AudioCaptureBackend* audio_capture_native_backend() {
    static const AudioCaptureBackend backend = {nullptr, capture_permission, capture_device_open, capture_device_lost, capture_device_close};
    return &backend;
}
