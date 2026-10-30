#pragma once
#include "animation_value.hpp"
#include <stdint.h>

static constexpr unsigned ANIMATION_VALUE_COMPONENTS = 16;
struct AnimationValue {
    double numbers[ANIMATION_VALUE_COMPONENTS];
    char text[256];
    unsigned count;
    AnimationValueType type;
};
struct AnimationBindingOps {
    bool (*read)(void*, uint64_t, AnimationValue*);
    bool (*write)(void*, uint64_t, const AnimationValue*);
    void (*event)(void*, uint64_t action, const char* type, double detail);
};
struct AnimationChannelBlend {
    double weight = 1;
    const double* reference = nullptr; // optional additive reference, with track.components values
};
struct AnimationChannelView {
    AnimationTrackView track;
    uint64_t property;
    const AnimationChannelBlend* blend = nullptr;
};
struct AnimationClipView {
    uint64_t identity;
    double duration;
    const AnimationChannelView* channels;
    unsigned count;
    bool additive;
};
enum AnimationLoopMode { ANIMATION_LOOP_ONCE, ANIMATION_LOOP_REPEAT, ANIMATION_LOOP_PINGPONG };
struct AnimationEnvelope { double begin, end, from, to; bool active; };
struct AnimationMixerState;
struct AnimationChannelState;
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
};
struct AnimationActionPair {
    AnimationActionState* target;
    const AnimationActionState* source;
};
struct AnimationActionSnapshot : AnimationActionPlayback {
    uint64_t identity;
    double duration;
    bool active;
};
struct AnimationMixerState;
AnimationMixerState* animation_mixer_create(void* owner, AnimationBindingOps ops);
void animation_mixer_destroy(AnimationMixerState* mixer);
AnimationActionState* animation_mixer_action(AnimationMixerState* mixer, const AnimationClipView& clip);
bool animation_mixer_update(AnimationMixerState* mixer, double delta);
bool animation_mixer_set_time(AnimationMixerState* mixer, double time);
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
bool animation_mixer_rebind(AnimationMixerState* mixer, void* owner);
bool animation_action_snapshot(const AnimationActionState* action, AnimationActionSnapshot* result);
