#include "render.hpp"

#include "event.hpp"
#include "rdt_video.h"

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

bool radiant_video_events_pending(DocState* state) {
    if (!state || !state->view_state_map) return false;
    size_t iter = 0;
    void* item;
    while (hashmap_iter(state->view_state_map, &iter, &item)) {
        const ViewStateEntry* entry = (const ViewStateEntry*)item;
        if (entry->kind != VIEW_STATE_MEDIA || !entry->state) continue;
        const MediaViewState* media = &entry->state->data.media;
        if (media->connected && media->opened && rdt_video_events_pending(media->player)) return true;
    }
    return false;
}
