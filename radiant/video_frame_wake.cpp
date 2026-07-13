#include "render.hpp"

#include "state_store.hpp"

static RadiantVideoWakeCallback g_video_wake_callback = nullptr;
static void* g_video_wake_user_data = nullptr;

void radiant_video_set_wake_callback(RadiantVideoWakeCallback callback, void* user_data) {
    g_video_wake_callback = callback;
    g_video_wake_user_data = user_data;
}

void radiant_video_notify_frame_ready(DocState* state) {
    doc_state_mark_video_frame_pending(state);
    if (g_video_wake_callback) {
        g_video_wake_callback(g_video_wake_user_data);
    }
}
