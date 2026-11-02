#include "audio_cues.hpp"
#include "../lib/mem.h"
#include <math.h>
#include <string.h>

struct AudioCueTrack { uint64_t identity; AudioPlayerLease lease; };
struct AudioCueEntry { AudioCueRow row; AudioPlayerLease prepared; bool used; };
struct AudioCueQueue {
    AudioSubmixId submix;
    AudioCueTrack tracks[AUDIO_CUE_TRACKS];
    AudioCueEntry entries[AUDIO_CUE_CAPACITY];
    AudioCueRow history[AUDIO_CUE_HISTORY];
    AudioCueCommand last[AUDIO_CUE_BATCH];
    uint64_t receipt[AUDIO_CUE_BATCH];
    uint64_t epoch, sequence, next_id, history_dropped, anchor_frame;
    double anchor_scene, scale;
    unsigned history_start, history_count, last_count;
    bool paused, anchored;
};
const char* audio_cue_status_name(AudioCueStatus status) {
    static const char* names[] = {"pending", "applied", "dropped_late", "cancelled", "device_lost", "dropped_busy", "invalid_transport"};
    return status <= AUDIO_CUE_INVALID ? names[status] : "invalid";
}
const char* audio_cue_op_name(AudioCueOp op) {
    static const char* names[] = {"play", "pause", "resume", "stop"};
    return op <= AUDIO_CUE_STOP ? names[op] : "invalid";
}
static AudioCueTrack* cue_track(AudioCueQueue* queue, uint64_t identity) {
    for (auto& track : queue->tracks) if (track.identity == identity) return &track;
    return nullptr;
}
static bool cue_equal(const AudioCueCommand& a, const AudioCueCommand& b) {
    return !strcmp(a.source, b.source) && a.identity == b.identity && a.clip == b.clip && a.frame == b.frame &&
        a.seconds == b.seconds && a.op == b.op && a.late == b.late && a.scene == b.scene;
}
static bool cue_deadline(AudioCueQueue* queue, const AudioCueCommand& command, uint64_t* frame, bool* held) {
    *held = false;
    if (!command.scene) { *frame = command.frame; return *frame != UINT64_MAX; }
    if (!queue->anchored || !isfinite(command.seconds) || command.seconds < 0) return false;
    if (command.seconds <= queue->anchor_scene) { *frame = queue->anchor_frame; return true; }
    if (queue->paused || queue->scale == 0) { *frame = UINT64_MAX; *held = true; return true; }
    double delta = ceil((command.seconds - queue->anchor_scene) * AUDIO_BUS_RATE / queue->scale);
    if (!isfinite(delta) || delta >= (long double)(UINT64_MAX - queue->anchor_frame)) return false;
    *frame = queue->anchor_frame + (uint64_t)delta; return true;
}
static bool cue_past(AudioCueQueue* queue, const AudioCueCommand& command, uint64_t frame, bool held, uint64_t clock) {
    return !held && (frame < clock || (command.scene && command.seconds < queue->anchor_scene));
}
static void cue_finish(AudioCueQueue* queue, AudioCueEntry* entry, AudioCueStatus status, uint64_t clock) {
    entry->row.status = status; entry->row.actual = clock;
    unsigned index = (queue->history_start + queue->history_count) % AUDIO_CUE_HISTORY;
    queue->history[index] = entry->row;
    if (queue->history_count < AUDIO_CUE_HISTORY) queue->history_count++;
    else { queue->history_start = (queue->history_start + 1) % AUDIO_CUE_HISTORY; queue->history_dropped++; }
    // prepared readers now own displaced PCM; only the main-thread collector closes them.
    if (!entry->prepared.token) *entry = {};
}
uint64_t audio_cues_next_locked(AudioCueQueue* queue) {
    uint64_t next = UINT64_MAX;
    for (const auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING && !entry.row.held && entry.row.requested < next)
        next = entry.row.requested;
    return next;
}
bool audio_cues_scene_work_locked(AudioCueQueue* queue) {
    for (const auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING && entry.row.command.scene) return true;
    return false;
}
bool audio_cues_output_locked(AudioCueQueue* queue, const AudioCueCommand* commands, unsigned count, uint64_t clock) {
    for (unsigned i = 0; i < count; i++) {
        uint64_t frame; bool held;
        if (cue_deadline(queue, commands[i], &frame, &held) && !held && !(cue_past(queue, commands[i], frame, held, clock) && commands[i].late == AUDIO_CUE_DROP)) return true;
    }
    return false;
}
static void cue_apply(void* owner, uint64_t clock) {
    auto* queue = (AudioCueQueue*)owner;
    for (;;) {
        AudioCueEntry* due = nullptr;
        for (auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING && !entry.row.held && entry.row.requested <= clock &&
            (!due || entry.row.requested < due->row.requested || (entry.row.requested == due->row.requested && entry.row.id < due->row.id))) due = &entry;
        if (!due) break;
        auto* track = cue_track(queue, due->row.command.identity);
        AudioCueStatus status = AUDIO_CUE_INVALID;
        if (track && track->lease.player && track->lease.voice) {
            auto& lease = track->lease;
            if (!audio_voice_try_lock(lease.voice)) status = AUDIO_CUE_BUSY;
            else {
                status = AUDIO_CUE_APPLIED;
                switch (due->row.command.op) {
                case AUDIO_CUE_PLAY:
                    if (!due->prepared.player) status = AUDIO_CUE_INVALID;
                    else { rdt_audio_replace(lease.player, due->prepared.player); audio_voice_set_active_locked(lease.voice, true); }
                    break;
                case AUDIO_CUE_PAUSE: rdt_audio_pause(lease.player); audio_voice_set_active_locked(lease.voice, false); break;
                case AUDIO_CUE_RESUME: {
                    RdtAudioInfo info = {}; rdt_audio_info(lease.player, &info);
                    if (info.state != RDT_AUDIO_PAUSED || !rdt_audio_play(lease.player, (float)info.gain)) status = AUDIO_CUE_INVALID;
                    else audio_voice_set_active_locked(lease.voice, true);
                    break;
                }
                case AUDIO_CUE_STOP: rdt_audio_stop(lease.player); audio_voice_set_active_locked(lease.voice, false); break;
                }
                audio_voice_unlock(lease.voice);
            }
        }
        cue_finish(queue, due, status, clock);
    }
}
static uint64_t cue_next(void* owner) { return audio_cues_next_locked((AudioCueQueue*)owner); }
void audio_cues_apply_locked(AudioCueQueue* queue, uint64_t clock) { cue_apply(queue, clock); }
static void cue_after(void* owner, uint64_t) {
    auto* queue = (AudioCueQueue*)owner;
    for (auto& track : queue->tracks) if (track.lease.player && track.lease.voice && rdt_audio_state(track.lease.player) != RDT_AUDIO_PLAYING)
        audio_voice_set_active_locked(track.lease.voice, false);
}
static void cue_lost(void* owner, uint64_t clock) {
    auto* queue = (AudioCueQueue*)owner;
    for (auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING) cue_finish(queue, &entry, AUDIO_CUE_LOST, clock);
}
AudioCueQueue* audio_cues_create(AudioSubmixId submix, uint64_t epoch) {
    auto* queue = (AudioCueQueue*)mem_calloc(1, sizeof(AudioCueQueue), MEM_CAT_RENDER);
    if (!queue) return nullptr;
    queue->submix = submix; queue->epoch = epoch; queue->next_id = 1;
    AudioSampleDriver driver{queue, cue_apply, cue_next, cue_after, cue_lost};
    if (!audio_submix_bind_driver(submix, driver)) { mem_free(queue); return nullptr; }
    return queue;
}
void audio_cues_destroy(AudioCueQueue* queue) {
    if (!queue) return;
    audio_submix_unbind_driver(queue->submix, queue); mem_free(queue);
}
const char* audio_cues_check_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t sequence,
    const AudioCueCommand* commands, unsigned count, uint64_t clock, bool* duplicate) {
    *duplicate = false;
    if (!queue || !epoch || epoch != queue->epoch || !sequence || !count || count > AUDIO_CUE_BATCH) return "audio cues: invalid epoch, sequence or batch size";
    if (sequence == queue->sequence) {
        if (count != queue->last_count) return "audio cues: changed retry payload";
        for (unsigned i = 0; i < count; i++) if (!cue_equal(commands[i], queue->last[i])) return "audio cues: changed retry payload";
        *duplicate = true; return nullptr;
    }
    if (queue->sequence == UINT64_MAX || sequence != queue->sequence + 1 || count > UINT64_MAX - queue->next_id)
        return "audio cues: stale sequence or exhausted identity";
    unsigned free = 0;
    for (const auto& entry : queue->entries) if (!entry.used) free++;
    if (count > free) return "audio cues: retained queue capacity exceeded";
    for (unsigned i = 0; i < count; i++) {
        uint64_t frame; bool held;
        if (!cue_deadline(queue, commands[i], &frame, &held)) return "audio cues: invalid or overflowing deadline";
        if (cue_past(queue, commands[i], frame, held, clock) && commands[i].late == AUDIO_CUE_REJECT) return "audio cues: deadline already passed";
    }
    return nullptr;
}
bool audio_cues_attach_locked(AudioCueQueue* queue, uint64_t source, const AudioPlayerLease& lease) {
    auto* track = cue_track(queue, source);
    if (!track) for (auto& empty : queue->tracks) if (!empty.identity) { track = &empty; break; }
    if (!track || !lease.player || !lease.voice) return false;
    *track = {source, lease}; return true;
}
bool audio_cues_pending_locked(AudioCueQueue* queue, uint64_t token) {
    for (const auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING) {
        auto* track = cue_track(queue, entry.row.command.identity);
        if (entry.prepared.token == token || (track && track->lease.token == token)) return true;
    }
    return false;
}
void audio_cues_detach_locked(AudioCueQueue* queue, uint64_t token, uint64_t clock, AudioCueStatus reason) {
    uint64_t identity = 0;
    for (auto& track : queue->tracks) if (track.lease.token == token) { identity = track.identity; track = {}; }
    for (auto& entry : queue->entries) if (entry.used) {
        bool bank = entry.prepared.token == token;
        if (bank) entry.prepared = {}; // a caller closing/taking over a player owns its subsequent lifetime.
        if (entry.row.status == AUDIO_CUE_PENDING && (bank || (identity && entry.row.command.identity == identity))) cue_finish(queue, &entry, reason, clock);
        else if (bank) entry = {};
    }
}
const char* audio_cues_commit_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t sequence,
    const AudioCueCommand* commands, const AudioPlayerLease* prepared, unsigned count, uint64_t clock) {
    bool duplicate;
    if (const char* error = audio_cues_check_locked(queue, epoch, sequence, commands, count, clock, &duplicate)) return error;
    if (duplicate) return nullptr;
    for (unsigned i = 0; i < count; i++) if (!cue_track(queue, commands[i].identity) ||
        (commands[i].op == AUDIO_CUE_PLAY && !prepared[i].player)) return "audio cues: missing prepared reader";
    for (unsigned i = 0; i < count; i++) {
        AudioCueEntry* entry = nullptr;
        for (auto& empty : queue->entries) if (!empty.used) { entry = &empty; break; }
        *entry = {}; entry->used = true; entry->prepared = prepared[i]; entry->row.command = commands[i];
        entry->row.id = queue->next_id++; entry->row.epoch = epoch;
        cue_deadline(queue, commands[i], &entry->row.requested, &entry->row.held);
        queue->last[i] = commands[i]; queue->receipt[i] = entry->row.id;
        if (cue_past(queue, commands[i], entry->row.requested, entry->row.held, clock)) {
            if (commands[i].late == AUDIO_CUE_DROP) cue_finish(queue, entry, AUDIO_CUE_LATE, clock);
            else entry->row.requested = clock;
        }
    }
    queue->last_count = count; queue->sequence = sequence; audio_submix_refresh_work_locked(queue->submix); return nullptr;
}
void audio_cues_clock_locked(AudioCueQueue* queue, uint64_t clock, double seconds, double scale, bool paused) {
    bool pending = false;
    for (const auto& entry : queue->entries) pending |= entry.used && entry.row.status == AUDIO_CUE_PENDING && entry.row.command.scene;
    bool changed = !queue->anchored || !pending || queue->paused != paused || queue->scale != scale ||
        (paused && queue->anchor_scene != seconds);
    if (!changed) return;
    queue->anchored = true; queue->anchor_frame = clock; queue->anchor_scene = seconds; queue->scale = scale; queue->paused = paused;
    for (auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING && entry.row.command.scene) {
        if (!cue_deadline(queue, entry.row.command, &entry.row.requested, &entry.row.held)) cue_finish(queue, &entry, AUDIO_CUE_INVALID, clock);
    }
    audio_submix_refresh_work_locked(queue->submix);
}
void audio_cues_reset_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t clock) {
    for (auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING) cue_finish(queue, &entry, AUDIO_CUE_CANCELLED, clock);
    queue->epoch = epoch; queue->sequence = 0; queue->last_count = 0; queue->anchored = false;
    audio_submix_refresh_work_locked(queue->submix);
}
bool audio_cues_cancel_locked(AudioCueQueue* queue, uint64_t event, uint64_t clock) {
    bool found = !event;
    for (auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING && (!event || entry.row.id == event)) {
        cue_finish(queue, &entry, AUDIO_CUE_CANCELLED, clock); found = true;
    }
    audio_submix_refresh_work_locked(queue->submix); return found;
}
unsigned audio_cues_collect_locked(AudioCueQueue* queue, uint64_t* tokens, unsigned capacity) {
    unsigned count = 0;
    for (auto& entry : queue->entries) if (entry.used && entry.row.status != AUDIO_CUE_PENDING && count < capacity) {
        if (entry.prepared.token) tokens[count++] = entry.prepared.token;
        entry = {};
    }
    return count;
}
void audio_cues_snapshot_locked(AudioCueQueue* queue, AudioCueSnapshot* snapshot) {
    *snapshot = {}; snapshot->epoch = queue->epoch; snapshot->sequence = queue->sequence;
    snapshot->next_id = queue->next_id; snapshot->history_dropped = queue->history_dropped;
    snapshot->history_count = queue->history_count; snapshot->receipt_count = queue->last_count;
    for (const auto& entry : queue->entries) if (entry.used && entry.row.status == AUDIO_CUE_PENDING) {
        snapshot->pending[snapshot->pending_count++] = entry.row; snapshot->scene_work |= entry.row.command.scene;
    }
    for (unsigned i = 0; i < queue->history_count; i++) snapshot->history[i] = queue->history[(queue->history_start + i) % AUDIO_CUE_HISTORY];
    memcpy(snapshot->receipt, queue->receipt, queue->last_count * sizeof(*queue->receipt));
}
