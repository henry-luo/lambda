#pragma once
#include "audio.hpp"

// proposal §9.2: copied commands and prepared readers, owned by the document submix.
static constexpr unsigned AUDIO_CUE_CAPACITY = 64, AUDIO_CUE_BATCH = 32, AUDIO_CUE_TRACKS = 32, AUDIO_CUE_HISTORY = 256;
enum AudioCueOp { AUDIO_CUE_PLAY, AUDIO_CUE_PAUSE, AUDIO_CUE_RESUME, AUDIO_CUE_STOP };
enum AudioCueLate { AUDIO_CUE_REJECT, AUDIO_CUE_NOW, AUDIO_CUE_DROP };
enum AudioCueStatus { AUDIO_CUE_PENDING, AUDIO_CUE_APPLIED, AUDIO_CUE_LATE, AUDIO_CUE_CANCELLED,
    AUDIO_CUE_LOST, AUDIO_CUE_BUSY, AUDIO_CUE_INVALID };
struct AudioCueCommand {
    char source[128];
    uint64_t identity, clip, frame;
    double seconds;
    AudioCueOp op;
    AudioCueLate late;
    bool scene;
};
struct AudioCueRow {
    AudioCueCommand command;
    uint64_t id, requested, actual, epoch;
    AudioCueStatus status;
    bool held;
};
struct AudioCueSnapshot {
    uint64_t epoch, sequence, next_id, history_dropped;
    unsigned pending_count, history_count, receipt_count;
    AudioCueRow pending[AUDIO_CUE_CAPACITY], history[AUDIO_CUE_HISTORY];
    uint64_t receipt[AUDIO_CUE_BATCH];
    bool scene_work;
};
struct AudioCueQueue;
AudioCueQueue* audio_cues_create(AudioSubmixId submix, uint64_t epoch);
void audio_cues_destroy(AudioCueQueue* queue);
// every operation below requires submix exclusion; no function allocates or frees readers.
const char* audio_cues_check_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t sequence,
    const AudioCueCommand* commands, unsigned count, uint64_t clock, bool* duplicate);
bool audio_cues_attach_locked(AudioCueQueue* queue, uint64_t source, const AudioPlayerLease& lease);
void audio_cues_detach_locked(AudioCueQueue* queue, uint64_t token, uint64_t clock, AudioCueStatus reason);
bool audio_cues_pending_locked(AudioCueQueue* queue, uint64_t token);
const char* audio_cues_commit_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t sequence,
    const AudioCueCommand* commands, const AudioPlayerLease* prepared, unsigned count, uint64_t clock);
void audio_cues_clock_locked(AudioCueQueue* queue, uint64_t clock, double seconds, double scale, bool paused);
void audio_cues_reset_locked(AudioCueQueue* queue, uint64_t epoch, uint64_t clock);
bool audio_cues_cancel_locked(AudioCueQueue* queue, uint64_t event, uint64_t clock);
unsigned audio_cues_collect_locked(AudioCueQueue* queue, uint64_t* tokens, unsigned capacity);
void audio_cues_snapshot_locked(AudioCueQueue* queue, AudioCueSnapshot* snapshot);
// explicit virtual owners also flush commands exactly at their selected final sample.
void audio_cues_apply_locked(AudioCueQueue* queue, uint64_t clock);
uint64_t audio_cues_next_locked(AudioCueQueue* queue);
bool audio_cues_scene_work_locked(AudioCueQueue* queue);
bool audio_cues_output_locked(AudioCueQueue* queue, const AudioCueCommand* commands, unsigned count, uint64_t clock);
const char* audio_cue_status_name(AudioCueStatus status);
const char* audio_cue_op_name(AudioCueOp op);
