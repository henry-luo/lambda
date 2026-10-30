#pragma once
#include "animation_value.hpp"
#include <stdint.h>

static constexpr unsigned ANIMATION_VALUE_COMPONENTS = 16;
static constexpr unsigned ANIMATION_CHANNEL_LIMIT = 1024;
struct AnimationValue {
    double numbers[ANIMATION_VALUE_COMPONENTS];
    char text[256];
    unsigned count;
    AnimationValueType type;
};
static constexpr unsigned ANIMATION_MARKER_LIMIT = 64;
static constexpr unsigned ANIMATION_EVENT_LIMIT = 256;
static constexpr unsigned ANIMATION_MARKER_WORK_LIMIT = 65536;
static constexpr unsigned ANIMATION_ROOT_CHANNEL_LIMIT = 2; // one translation and one quaternion per clip
struct AnimationMarkerView {
    const char* name;
    double time;
    double min_weight = .5;
};
struct AnimationTimelineEvent {
    uint64_t action, sequence;
    const char* type; // static event type; marker text is copied independently of the clip
    char marker[128];
    double time, clip_time, iteration, direction, weight, detail;
};
struct AnimationBindingOps {
    bool (*read)(void*, uint64_t, AnimationValue*);
    bool (*write)(void*, uint64_t, const AnimationValue*);
    void (*event)(void*, uint64_t action, const char* type, double detail);
    void (*timeline)(void*, const AnimationTimelineEvent&) = nullptr;
};
struct AnimationChannelBlend {
    double weight = 1;
    const double* reference = nullptr; // optional additive reference, with track.components values
};
struct AnimationChannelView {
    AnimationTrackView track;
    uint64_t property;
    const AnimationChannelBlend* blend = nullptr;
    bool root_motion = false; // extract vector3 translation or quaternion rotation instead of writing the pose
};
struct AnimationClipView {
    uint64_t identity;
    double duration;
    const AnimationChannelView* channels;
    unsigned count;
    bool additive;
    const AnimationMarkerView* markers = nullptr;
    unsigned marker_count = 0;
};
enum AnimationLoopMode { ANIMATION_LOOP_ONCE, ANIMATION_LOOP_REPEAT, ANIMATION_LOOP_PINGPONG };
struct AnimationEnvelope { double begin, end, from, to; bool active; };
struct AnimationMixerState;
struct AnimationChannelState;
struct AnimationMarkerState;
struct AnimationActionPlayback {
    double time, time_scale, weight, effective_weight, effective_time_scale;
    double repetitions, loop_count, scheduled_start;
    double restore_time_scale;
    AnimationEnvelope fade, warp;
    AnimationLoopMode loop;
    AnimationEnding ending_start, ending_end;
    bool enabled, paused, clamp, scheduled, zero_slope_start, zero_slope_end;
    bool restore_warp_scale;
};
struct AnimationActionState : AnimationActionPlayback {
    AnimationActionState* next;
    AnimationMixerState* mixer;
    AnimationClipView clip; // immutable channel storage belongs to the adapter until uncache
    uint64_t identity;
    bool active;
    AnimationChannelState* channels; // copied masks/reference poses; null uses legacy defaults
    AnimationMarkerState* markers;   // copied marker names/times; independent of descriptor storage
    unsigned root_channels[ANIMATION_ROOT_CHANNEL_LIMIT]; // one-based translation/rotation indices; zero if absent
};
struct AnimationActionPair {
    AnimationActionState* target;
    const AnimationActionState* source;
    const unsigned* channels = nullptr; // target-to-source permutation, borrowed only during inheritance
    unsigned channel_count = 0;         // zero with a null map preserves ordered matching
};
struct AnimationActionSnapshot : AnimationActionPlayback {
    uint64_t identity;
    double duration;
    bool active;
};
struct AnimationRootMotion {
    uint64_t epoch, sequence;
    AnimationValueType type;
    unsigned count;
    double delta[4], total[4];
};
static constexpr unsigned ANIMATION_COMMAND_LIMIT = 256;
enum AnimationCommandKind {
    ANIMATION_COMMAND_PLAY,
    ANIMATION_COMMAND_STOP,
    ANIMATION_COMMAND_RESET,
    ANIMATION_COMMAND_WEIGHT,
    ANIMATION_COMMAND_RATE,
    ANIMATION_COMMAND_PAUSE,
    ANIMATION_COMMAND_PHASE,
    ANIMATION_COMMAND_MASK,
    ANIMATION_COMMAND_FADE,
    ANIMATION_COMMAND_WARP,
    ANIMATION_COMMAND_CROSSFADE
};
struct AnimationCommand {
    AnimationCommandKind kind;
    uint64_t action, other;
    double value, second, duration;
    unsigned channel;
    bool reset, paused, fade_in, warp;
};
struct AnimationMixerState;
AnimationMixerState* animation_mixer_create(void* owner, AnimationBindingOps ops);
void animation_mixer_destroy(AnimationMixerState* mixer);
AnimationActionState* animation_mixer_action(AnimationMixerState* mixer, const AnimationClipView& clip);
bool animation_mixer_update(AnimationMixerState* mixer, double delta, bool emit_timeline = true,
                            bool extract_root_motion = true);
// observational timing/root/cue budget admission; binding pose checks belong to the disposable stage.
bool animation_mixer_admit(AnimationMixerState* mixer, double delta);
// legacy loop/finish callbacks still drive native controllers during a silent pose seek.
bool animation_mixer_set_time(AnimationMixerState* mixer, double time, bool emit_timeline = false);
double animation_mixer_time(const AnimationMixerState* mixer);
bool animation_mixer_time_scale(AnimationMixerState* mixer, double scale);
bool animation_mixer_active(const AnimationMixerState* mixer);
bool animation_mixer_property_active(AnimationMixerState* mixer,uint64_t property);
bool animation_action_play(AnimationActionState* action);
bool animation_action_stop(AnimationActionState* action);
void animation_action_reset(AnimationActionState* action);
bool animation_action_fade(AnimationActionState* action, double duration, bool fade_in);
bool animation_action_warp(AnimationActionState* action, double from, double to, double duration);
bool animation_action_crossfade(AnimationActionState* from, AnimationActionState* to, double duration, bool warp);
// atomically replace copied per-track masks/references without changing playback time or envelopes.
bool animation_action_set_blend(AnimationActionState* action, const AnimationChannelBlend* channels, unsigned count);
bool animation_mixer_uncache(AnimationMixerState* mixer, AnimationActionState* action);
// the target is a disposable staged mixer; source owners and playback remain untouched on failure.
bool animation_mixer_inherit(AnimationMixerState* target, const AnimationMixerState* source,
                             const AnimationActionPair* pairs, unsigned count);
bool animation_mixer_rebind(AnimationMixerState* mixer, void* owner, const AnimationBindingOps* ops = nullptr);
// immutable clip/channel storage stays with the source adapter; playback, masks, markers and roots are copied.
AnimationMixerState* animation_mixer_clone(const AnimationMixerState* source, void* owner, AnimationBindingOps ops);
AnimationActionState* animation_mixer_find_action(AnimationMixerState* mixer, uint64_t identity);
// apply to a disposable staged mixer, then sample/admit it before publishing; failure may modify that stage.
bool animation_mixer_commands(AnimationMixerState* staged, const AnimationCommand* commands, unsigned count);
bool animation_action_snapshot(const AnimationActionState* action, AnimationActionSnapshot* result);
// copied parent-frame translation/rotation; a pose seek resets its epoch and total to zero/identity.
bool animation_mixer_root_motion(const AnimationMixerState* mixer, uint64_t property, AnimationRootMotion* result);
