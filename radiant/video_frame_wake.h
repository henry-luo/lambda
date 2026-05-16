#ifndef RADIANT_VIDEO_FRAME_WAKE_H
#define RADIANT_VIDEO_FRAME_WAKE_H

struct DocState;

typedef void (*RadiantVideoWakeCallback)(void* user_data);

void radiant_video_set_wake_callback(RadiantVideoWakeCallback callback, void* user_data);
void radiant_video_notify_frame_ready(DocState* state);

#endif
