#ifndef RADIANT_FRAME_CLOCK_H
#define RADIANT_FRAME_CLOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RadiantFrameClockMode {
    RADIANT_FRAME_CLOCK_MONOTONIC = 0,
    RADIANT_FRAME_CLOCK_MACOS_CV_DISPLAY_LINK,
    RADIANT_FRAME_CLOCK_WINDOWS_DWM,
    RADIANT_FRAME_CLOCK_WINDOWS_QPC_TIMER,
    RADIANT_FRAME_CLOCK_LINUX_TIMERFD
} RadiantFrameClockMode;

typedef void (*RadiantFrameWakeCallback)(void* user_data);
typedef struct RadiantFrameClockPlatform RadiantFrameClockPlatform;

typedef struct RadiantFrameClock {
    RadiantFrameClockMode mode;
    double refresh_hz;
    double refresh_interval;
    double last_frame_time;
    double next_frame_time;
    RadiantFrameWakeCallback wake_callback;
    void* wake_user_data;
    RadiantFrameClockPlatform* platform;
    bool initialized;
    bool native_started;
} RadiantFrameClock;

bool radiant_frame_clock_init(RadiantFrameClock* clock, double refresh_hz);
bool radiant_frame_clock_start(RadiantFrameClock* clock);
void radiant_frame_clock_shutdown(RadiantFrameClock* clock);
void radiant_frame_clock_set_wake_callback(RadiantFrameClock* clock,
                                           RadiantFrameWakeCallback callback,
                                           void* user_data);
double radiant_frame_clock_now(RadiantFrameClock* clock);
double radiant_frame_clock_next_timeout(RadiantFrameClock* clock, double now,
                                        bool frame_driven, bool needs_redraw);
void radiant_frame_clock_mark_presented(RadiantFrameClock* clock, double frame_time);
const char* radiant_frame_clock_mode_name(const RadiantFrameClock* clock);

#ifdef __cplusplus
}
#endif

#endif
