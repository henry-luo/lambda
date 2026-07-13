#define Rect RadiantRect
#include "radiant.hpp"
#undef Rect

#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#include <mach/mach_time.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <poll.h>
#include <sys/timerfd.h>
#endif
#endif

struct RadiantFrameClockPlatform {
    volatile int running;
    volatile int wants_frame_wake;
    volatile int has_tick;
    RadiantFrameWakeCallback wake_callback;
    void* wake_user_data;
    double refresh_interval;

#if defined(__APPLE__)
    CVDisplayLinkRef display_link;
    mach_timebase_info_data_t timebase;
    volatile uint64_t last_host_time;
#elif defined(_WIN32)
    HANDLE thread_handle;
    HMODULE dwm_module;
    HRESULT (WINAPI *dwm_flush)(void);
    LARGE_INTEGER qpc_frequency;
    volatile int64_t last_counter;
#elif defined(__linux__)
    pthread_t thread;
    int thread_started;
    int timer_fd;
    volatile uint64_t last_ns;
#endif
};

static void frame_clock_store_int(volatile int* target, int value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static int frame_clock_load_int(volatile int* target) {
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
}

static void frame_clock_store_u64(volatile uint64_t* target, uint64_t value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static uint64_t frame_clock_load_u64(volatile uint64_t* target) {
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
}

#if defined(_WIN32)
static void frame_clock_store_i64(volatile int64_t* target, int64_t value) {
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static int64_t frame_clock_load_i64(volatile int64_t* target) {
    return __atomic_load_n(target, __ATOMIC_ACQUIRE);
}
#endif

static double radiant_frame_clock_clamp_refresh_hz(double refresh_hz) {
    if (refresh_hz < 24.0 || refresh_hz > 240.0) {
        return 60.0;
    }
    return refresh_hz;
}

#if defined(_WIN32)
static double radiant_frame_clock_monotonic_seconds(void) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}
#else
static double radiant_frame_clock_monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        log_error("frame_clock: clock_gettime(CLOCK_MONOTONIC) failed");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
#endif

static void frame_clock_wake_if_requested(RadiantFrameClockPlatform* platform) {
    if (!platform) return;
    if (!frame_clock_load_int(&platform->wants_frame_wake)) return;
    if (platform->wake_callback) {
        platform->wake_callback(platform->wake_user_data);
    }
}

#if defined(__APPLE__)
static double frame_clock_mach_seconds(RadiantFrameClockPlatform* platform,
                                       uint64_t host_time) {
    if (!platform || host_time == 0) return radiant_frame_clock_monotonic_seconds();
    double nanos = (double)host_time * (double)platform->timebase.numer /
        (double)platform->timebase.denom;
    return nanos / 1000000000.0;
}

static CVReturn frame_clock_cv_display_link_cb(CVDisplayLinkRef display_link,
                                               const CVTimeStamp* now,
                                               const CVTimeStamp* output_time,
                                               CVOptionFlags flags_in,
                                               CVOptionFlags* flags_out,
                                               void* display_link_context) {
    (void)display_link;
    (void)output_time;
    (void)flags_in;
    (void)flags_out;

    RadiantFrameClockPlatform* platform =
        (RadiantFrameClockPlatform*)display_link_context;
    uint64_t host_time = now && now->hostTime ? now->hostTime : mach_absolute_time();
    frame_clock_store_u64(&platform->last_host_time, host_time);
    frame_clock_store_int(&platform->has_tick, 1);
    frame_clock_wake_if_requested(platform);
    return kCVReturnSuccess;
}
#elif defined(_WIN32)
static DWORD WINAPI frame_clock_windows_thread_main(void* user_data) {
    RadiantFrameClockPlatform* platform = (RadiantFrameClockPlatform*)user_data;
    DWORD fallback_ms = (DWORD)(platform->refresh_interval * 1000.0);
    if (fallback_ms < 1) fallback_ms = 1;

    while (frame_clock_load_int(&platform->running)) {
        if (platform->dwm_flush) {
            platform->dwm_flush();
        } else {
            Sleep(fallback_ms);
        }

        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        frame_clock_store_i64(&platform->last_counter, counter.QuadPart);
        frame_clock_store_int(&platform->has_tick, 1);
        frame_clock_wake_if_requested(platform);
    }
    return 0;
}
#elif defined(__linux__)
static uint64_t frame_clock_linux_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void* frame_clock_linux_thread_main(void* user_data) {
    RadiantFrameClockPlatform* platform = (RadiantFrameClockPlatform*)user_data;
    struct pollfd pfd;
    pfd.fd = platform->timer_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (frame_clock_load_int(&platform->running)) {
        int rc = poll(&pfd, 1, 100);
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) continue;

        uint64_t expirations = 0;
        ssize_t bytes_read = read(platform->timer_fd, &expirations, sizeof(expirations));
        if (bytes_read != (ssize_t)sizeof(expirations)) continue;

        frame_clock_store_u64(&platform->last_ns, frame_clock_linux_now_ns());
        frame_clock_store_int(&platform->has_tick, 1);
        frame_clock_wake_if_requested(platform);
    }
    return NULL;
}
#endif

static void frame_clock_platform_destroy(RadiantFrameClockPlatform* platform) {
    if (!platform) return;
#if defined(__APPLE__)
    if (platform->display_link) {
        if (CVDisplayLinkIsRunning(platform->display_link)) {
            CVDisplayLinkStop(platform->display_link);
        }
        CVDisplayLinkRelease(platform->display_link);
        platform->display_link = NULL;
    }
#elif defined(_WIN32)
    if (platform->thread_handle) {
        WaitForSingleObject(platform->thread_handle, INFINITE);
        CloseHandle(platform->thread_handle);
        platform->thread_handle = NULL;
    }
    if (platform->dwm_module) {
        FreeLibrary(platform->dwm_module);
        platform->dwm_module = NULL;
    }
#elif defined(__linux__)
    if (platform->thread_started) {
        pthread_join(platform->thread, NULL);
        platform->thread_started = 0;
    }
    if (platform->timer_fd >= 0) {
        close(platform->timer_fd);
        platform->timer_fd = -1;
    }
#endif
    mem_free(platform);
}

static RadiantFrameClockPlatform* frame_clock_platform_create(RadiantFrameClock* clock) {
    RadiantFrameClockPlatform* platform =
        (RadiantFrameClockPlatform*)mem_calloc(1, sizeof(RadiantFrameClockPlatform), MEM_CAT_RENDER);
    if (!platform) return NULL;

    platform->refresh_interval = clock->refresh_interval;
#if defined(__linux__)
    platform->timer_fd = -1;
#endif

#if defined(__APPLE__)
    mach_timebase_info(&platform->timebase);
    CVReturn rc = CVDisplayLinkCreateWithActiveCGDisplays(&platform->display_link);
    if (rc != kCVReturnSuccess || !platform->display_link) {
        log_warn("frame_clock: CVDisplayLinkCreateWithActiveCGDisplays failed (%d)", (int)rc);
        frame_clock_platform_destroy(platform);
        return NULL;
    }

    CVTime nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(platform->display_link);
    if (nominal.timeValue > 0 && nominal.timeScale > 0) {
        clock->refresh_interval = (double)nominal.timeValue / (double)nominal.timeScale;
        clock->refresh_hz = 1.0 / clock->refresh_interval;
        platform->refresh_interval = clock->refresh_interval;
    }

    CVDisplayLinkSetOutputCallback(platform->display_link,
                                   frame_clock_cv_display_link_cb,
                                   platform);
    frame_clock_store_u64(&platform->last_host_time, mach_absolute_time());
    frame_clock_store_int(&platform->has_tick, 1);
    clock->mode = RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK;
    return platform;
#elif defined(_WIN32)
    QueryPerformanceFrequency(&platform->qpc_frequency);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    frame_clock_store_i64(&platform->last_counter, counter.QuadPart);
    frame_clock_store_int(&platform->has_tick, 1);

    platform->dwm_module = LoadLibraryA("dwmapi.dll");
    if (platform->dwm_module) {
        platform->dwm_flush = (HRESULT (WINAPI *)(void))
            GetProcAddress(platform->dwm_module, "DwmFlush");
    }
    clock->mode = platform->dwm_flush ?
        RADIANT_FRAME_CLOCK_WINDOWS_DWM : RADIANT_FRAME_CLOCK_WINDOWS_QPC_TIMER;
    return platform;
#elif defined(__linux__)
    platform->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (platform->timer_fd < 0) {
        log_warn("frame_clock: timerfd_create failed, using monotonic timer");
        frame_clock_platform_destroy(platform);
        return NULL;
    }
    frame_clock_store_u64(&platform->last_ns, frame_clock_linux_now_ns());
    frame_clock_store_int(&platform->has_tick, 1);
    clock->mode = RADIANT_FRAME_CLOCK_LINUX_TIMERFD;
    return platform;
#else
    return platform;
#endif
}

bool radiant_frame_clock_init(RadiantFrameClock* clock, double refresh_hz) {
    if (!clock) return false;

    memset(clock, 0, sizeof(RadiantFrameClock));
    clock->mode = RADIANT_FRAME_CLOCK_MONOTONIC;
    clock->refresh_hz = radiant_frame_clock_clamp_refresh_hz(refresh_hz);
    clock->refresh_interval = 1.0 / clock->refresh_hz;
    clock->last_frame_time = radiant_frame_clock_monotonic_seconds();
    clock->next_frame_time = clock->last_frame_time;
    clock->platform = frame_clock_platform_create(clock);
    clock->initialized = true;
    return true;
}

void radiant_frame_clock_set_wake_callback(RadiantFrameClock* clock,
                                           RadiantFrameWakeCallback callback,
                                           void* user_data) {
    if (!clock) return;
    clock->wake_callback = callback;
    clock->wake_user_data = user_data;
    if (clock->platform) {
        clock->platform->wake_callback = callback;
        clock->platform->wake_user_data = user_data;
    }
}

bool radiant_frame_clock_start(RadiantFrameClock* clock) {
    if (!clock || !clock->initialized || !clock->platform) return false;
    RadiantFrameClockPlatform* platform = clock->platform;
    frame_clock_store_int(&platform->running, 1);

#if defined(__APPLE__)
    if (clock->mode == RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK) {
        CVReturn rc = CVDisplayLinkStart(platform->display_link);
        if (rc != kCVReturnSuccess) {
            frame_clock_store_int(&platform->running, 0);
            log_warn("frame_clock: CVDisplayLinkStart failed (%d)", (int)rc);
            return false;
        }
        clock->native_started = true;
        return true;
    }
#elif defined(_WIN32)
    platform->thread_handle = CreateThread(NULL, 0, frame_clock_windows_thread_main,
                                           platform, 0, NULL);
    if (!platform->thread_handle) {
        frame_clock_store_int(&platform->running, 0);
        log_warn("frame_clock: CreateThread failed for Windows frame clock");
        return false;
    }
    clock->native_started = true;
    return true;
#elif defined(__linux__)
    struct itimerspec spec;
    memset(&spec, 0, sizeof(spec));
    long interval_ns = (long)(clock->refresh_interval * 1000000000.0);
    if (interval_ns < 1000000L) interval_ns = 1000000L;
    spec.it_interval.tv_sec = interval_ns / 1000000000L;
    spec.it_interval.tv_nsec = interval_ns % 1000000000L;
    spec.it_value = spec.it_interval;

    if (timerfd_settime(platform->timer_fd, 0, &spec, NULL) != 0) {
        frame_clock_store_int(&platform->running, 0);
        log_warn("frame_clock: timerfd_settime failed for Linux frame clock");
        return false;
    }
    if (pthread_create(&platform->thread, NULL, frame_clock_linux_thread_main, platform) != 0) {
        frame_clock_store_int(&platform->running, 0);
        log_warn("frame_clock: pthread_create failed for Linux frame clock");
        return false;
    }
    platform->thread_started = 1;
    clock->native_started = true;
    return true;
#else
    frame_clock_store_int(&platform->running, 0);
    return false;
#endif

    frame_clock_store_int(&platform->running, 0);
    return false;
}

void radiant_frame_clock_shutdown(RadiantFrameClock* clock) {
    if (!clock) return;
    if (clock->platform) {
        frame_clock_store_int(&clock->platform->running, 0);
    }
    frame_clock_platform_destroy(clock->platform);
    clock->platform = NULL;
    clock->initialized = false;
    clock->native_started = false;
}

double radiant_frame_clock_now(RadiantFrameClock* clock) {
    if (!clock || !clock->initialized || !clock->platform ||
        !frame_clock_load_int(&clock->platform->has_tick)) {
        return radiant_frame_clock_monotonic_seconds();
    }

#if defined(__APPLE__)
    if (clock->mode == RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK) {
        uint64_t host_time = frame_clock_load_u64(&clock->platform->last_host_time);
        return frame_clock_mach_seconds(clock->platform, host_time);
    }
#elif defined(_WIN32)
    if (clock->mode == RADIANT_FRAME_CLOCK_WINDOWS_DWM ||
        clock->mode == RADIANT_FRAME_CLOCK_WINDOWS_QPC_TIMER) {
        int64_t counter = frame_clock_load_i64(&clock->platform->last_counter);
        if (counter > 0 && clock->platform->qpc_frequency.QuadPart > 0) {
            return (double)counter / (double)clock->platform->qpc_frequency.QuadPart;
        }
    }
#elif defined(__linux__)
    if (clock->mode == RADIANT_FRAME_CLOCK_LINUX_TIMERFD) {
        uint64_t ns = frame_clock_load_u64(&clock->platform->last_ns);
        if (ns > 0) return (double)ns / 1000000000.0;
    }
#endif

    return radiant_frame_clock_monotonic_seconds();
}

double radiant_frame_clock_next_timeout(RadiantFrameClock* clock, double now,
                                        bool frame_driven, bool needs_redraw) {
    if (!clock || !clock->initialized || needs_redraw) return 0.0;

    if (clock->platform) {
        frame_clock_store_int(&clock->platform->wants_frame_wake,
                              frame_driven ? 1 : 0);
    }

    if (!frame_driven) {
        return 1.0;
    }

    if (clock->native_started) {
        return 1.0;
    }

    double timeout = clock->next_frame_time - now;
    if (timeout < 0.0) return 0.0;
    if (timeout > clock->refresh_interval) return clock->refresh_interval;
    return timeout;
}

void radiant_frame_clock_mark_presented(RadiantFrameClock* clock, double frame_time) {
    if (!clock || !clock->initialized) return;
    clock->last_frame_time = frame_time;
    clock->next_frame_time = frame_time + clock->refresh_interval;
}

const char* radiant_frame_clock_mode_name(const RadiantFrameClock* clock) {
    if (!clock || !clock->initialized) return "uninitialized";
    switch (clock->mode) {
        case RADIANT_FRAME_CLOCK_MONOTONIC:
            return "monotonic";
        case RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK:
            return "macos-cvdisplaylink";
        case RADIANT_FRAME_CLOCK_WINDOWS_DWM:
            return "windows-dwm";
        case RADIANT_FRAME_CLOCK_WINDOWS_QPC_TIMER:
            return "windows-qpc-timer";
        case RADIANT_FRAME_CLOCK_LINUX_TIMERFD:
            return "linux-timerfd";
        default:
            return "unknown";
    }
}
