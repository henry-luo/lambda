#include "animation_mixer.hpp"
#include "../lib/mem.h"
#include "../lib/str.h"
#include <math.h>
#include <string.h>

struct AnimationPropertyState {
    AnimationPropertyState* next;
    uint64_t identity;
    AnimationValue base, sampled, additive, applied;
    double weight, additive_weight;
    unsigned references, active;
};
struct AnimationPendingEvent { uint64_t action; const char* type; double detail; };
struct AnimationMixerState {
    void* owner;
    AnimationBindingOps ops;
    AnimationActionState* actions;
    AnimationPropertyState* properties;
    double time, time_scale;
    uint64_t next_identity;
    unsigned action_count, property_count, event_count;
    AnimationActionState* active[256];
    unsigned active_count;
    AnimationPendingEvent events[256];
    bool updating, dispatching, destroy_pending;
};

static AnimationPropertyState* animation_property(AnimationMixerState* mixer, uint64_t identity) {
    for (AnimationPropertyState* property = mixer->properties; property; property = property->next)
        if (property->identity == identity) return property;
    return nullptr;
}
static void animation_properties_prune(AnimationMixerState* mixer) {
    auto** link = &mixer->properties;
    while (*link) {
        auto* property = *link;
        if (!property->references) { *link = property->next; mem_free(property); mixer->property_count--; }
        else link = &property->next;
    }
}
AnimationMixerState* animation_mixer_create(void* owner, AnimationBindingOps ops) {
    if (!ops.read || !ops.write) return nullptr;
    auto* mixer = (AnimationMixerState*)mem_calloc(1, sizeof(AnimationMixerState), MEM_CAT_RENDER);
    if (mixer) { mixer->owner = owner; mixer->ops = ops; mixer->time_scale = 1; mixer->next_identity = 1; }
    return mixer;
}
void animation_mixer_destroy(AnimationMixerState* mixer) {
    if (!mixer) return;
    // an event may release its own mixer; keep the dispatch walk alive until that callback returns.
    if(mixer->updating||mixer->dispatching) {mixer->destroy_pending=true;return;}
    mixer->updating=true;
    while (mixer->actions) { auto* action = mixer->actions; mixer->actions = action->next; mem_free(action); }
    while (mixer->properties) { auto* property = mixer->properties; mixer->properties = property->next;
        if (property->active) mixer->ops.write(mixer->owner, property->identity, &property->base);
        mem_free(property);
    }
    mem_free(mixer);
}
AnimationActionState* animation_mixer_action(AnimationMixerState* mixer, const AnimationClipView& clip) {
    if (!mixer || mixer->updating || !clip.identity || !clip.channels || !clip.count || clip.count > 256 ||
        !isfinite(clip.duration) || clip.duration < 0) return nullptr;
    for (auto* action = mixer->actions; action; action = action->next)
        if (action->clip.identity == clip.identity) return action;
    if(mixer->action_count>=256) return nullptr;
    for (unsigned i = 0; i < clip.count; i++)
        if (!animation_track_validate(clip.channels[i].track) || clip.channels[i].track.components > ANIMATION_VALUE_COMPONENTS) return nullptr;
    // prepare all bindings before linking an action; failed preparation leaves no playable partial action.
    for (unsigned i = 0; i < clip.count; i++) {
        const auto& channel = clip.channels[i]; auto* property = animation_property(mixer, channel.property);
        if (!property) {
            if (mixer->property_count >= 1024) { animation_properties_prune(mixer); return nullptr; }
            property = (AnimationPropertyState*)mem_calloc(1, sizeof(AnimationPropertyState), MEM_CAT_RENDER);
            if (!property) { animation_properties_prune(mixer); return nullptr; }
            if (!mixer->ops.read(mixer->owner, channel.property, &property->base) ||
                property->base.type != channel.track.type || property->base.count != channel.track.components) {
                mem_free(property); animation_properties_prune(mixer); return nullptr;
            }
            property->identity = channel.property; property->applied = property->base;
            property->next = mixer->properties; mixer->properties = property; mixer->property_count++;
        } else if (property->base.type != channel.track.type || property->base.count != channel.track.components) {
            animation_properties_prune(mixer); return nullptr;
        }
    }
    auto* action = (AnimationActionState*)mem_calloc(1, sizeof(AnimationActionState), MEM_CAT_RENDER);
    if (!action) { animation_properties_prune(mixer); return nullptr; }
    action->mixer = mixer; action->clip = clip; action->identity = mixer->next_identity++;
    action->time_scale = action->weight = action->effective_time_scale = action->effective_weight = 1;
    action->enabled = action->zero_slope_start = action->zero_slope_end = true;
    action->repetitions = INFINITY; action->loop_count = -1; action->loop = ANIMATION_LOOP_REPEAT;
    action->next = mixer->actions; mixer->actions = action; mixer->action_count++;
    for (unsigned i = 0; i < clip.count; i++) animation_property(mixer, clip.channels[i].property)->references++;
    return action;
}
static bool animation_property_activate(AnimationActionState* action, bool active) {
    if (!action || action->mixer->updating) return false;
    if (action->active == active) return true;
    // validate every newly active target before changing any reference count.
    if (active) for (unsigned i = 0; i < action->clip.count; i++) {
        auto* property = animation_property(action->mixer, action->clip.channels[i].property);
        if (!property->active) {
            AnimationValue base = {};
            if (!action->mixer->ops.read(action->mixer->owner, property->identity, &base) ||
                base.type != property->base.type || base.count != property->base.count) return false;
            property->base = property->applied = base;
        }
    }
    bool valid = true;
    for (unsigned i = 0; i < action->clip.count; i++) {
        auto* property = animation_property(action->mixer, action->clip.channels[i].property);
        if (active) property->active++;
        else if (--property->active == 0) {
            valid = action->mixer->ops.write(action->mixer->owner, property->identity, &property->base) && valid;
            property->applied = property->base;
        }
    }
    auto* mixer = action->mixer;
    if (active) mixer->active[mixer->active_count++] = action;
    else for (unsigned i = 0; i < mixer->active_count; i++) if (mixer->active[i] == action) {
        mixer->active[i] = mixer->active[--mixer->active_count]; break;
    }
    action->active = active; return valid;
}
bool animation_action_play(AnimationActionState* action) { return animation_property_activate(action, true); }
void animation_action_reset(AnimationActionState* action) {
    if (!action) return;
    action->paused = false; action->enabled = true; action->time = 0; action->loop_count = -1;
    action->scheduled = action->fade.active = action->warp.active = false;
    action->restore_warp_scale = false;
}
bool animation_action_stop(AnimationActionState* action) {
    if (!animation_property_activate(action, false)) return false;
    animation_action_reset(action); return true;
}
bool animation_action_fade(AnimationActionState* action, double duration, bool fade_in) {
    if (!action || !isfinite(duration) || duration < 0) return false;
    action->fade = {(float)action->mixer->time, (float)(action->mixer->time + duration), fade_in ? 0.0 : 1.0, fade_in ? 1.0 : 0.0, true};
    return true;
}
bool animation_action_warp(AnimationActionState* action, double from, double to, double duration) {
    if (!action || !isfinite(from) || !isfinite(to) || !isfinite(duration) || duration < 0 || action->time_scale == 0) return false;
    action->warp = {(float)action->mixer->time, (float)(action->mixer->time + duration), (float)(from / action->time_scale), (float)(to / action->time_scale), true};
    return true;
}
bool animation_action_crossfade(AnimationActionState* from, AnimationActionState* to, double duration, bool warp) {
    if (!from || !to || from->mixer != to->mixer || !isfinite(duration) || duration < 0) return false;
    if (warp && (!(from->clip.duration > 0) || !(to->clip.duration > 0)||!from->time_scale||!to->time_scale)) return false;
    animation_action_fade(from, duration, false); animation_action_fade(to, duration, true);
    if (warp) {
        double ratio = from->clip.duration / to->clip.duration;
        from->restore_time_scale = from->time_scale; to->restore_time_scale = to->time_scale;
        from->restore_warp_scale = to->restore_warp_scale = true;
        return animation_action_warp(from, 1, ratio, duration) && animation_action_warp(to, 1 / ratio, 1, duration);
    }
    return true;
}
static double animation_envelope_value(const AnimationEnvelope& envelope, double time) {
    double progress = envelope.end > envelope.begin ? fmin(1, fmax(0, (time - envelope.begin) / (envelope.end - envelope.begin))) : 1;
    // upstream control interpolants store both endpoints and their result in Float32 buffers.
    return (float)(envelope.from * (1 - progress) + envelope.to * progress);
}
static void animation_action_endings(AnimationActionState* action, bool start, bool end) {
    bool pingpong = action->loop == ANIMATION_LOOP_PINGPONG;
    action->ending_start = pingpong || (start && action->zero_slope_start) ? ANIMATION_ZERO_SLOPE : start ? ANIMATION_ZERO_CURVATURE : ANIMATION_WRAP;
    action->ending_end = pingpong || (end && action->zero_slope_end) ? ANIMATION_ZERO_SLOPE : end ? ANIMATION_ZERO_CURVATURE : ANIMATION_WRAP;
}
static void animation_queue_event(AnimationActionState* action, const char* type, double detail) {
    auto* mixer = action->mixer;
    if (mixer->event_count < 256) mixer->events[mixer->event_count++] = {action->identity, type, detail};
}
static double animation_action_advance(AnimationActionState* action, double delta) {
    double duration = action->clip.duration, time = action->time + delta, loops = action->loop_count;
    bool pingpong = action->loop == ANIMATION_LOOP_PINGPONG;
    if (!delta) return pingpong && loops >= 0 && fmod(loops, 2) == 1 ? duration - time : time;
    if (action->loop == ANIMATION_LOOP_ONCE || duration == 0) {
        if (loops == -1) { action->loop_count = 0; animation_action_endings(action, true, true); }
        if (time >= duration || time < 0) {
            time = time < 0 ? 0 : duration;
            if (action->clamp) action->paused = true; else action->enabled = false;
            animation_queue_event(action, "finished", delta < 0 ? -1 : 1);
        }
        action->time = time; return time;
    }
    if (loops == -1) {
        if (delta >= 0) loops = 0;
        animation_action_endings(action, delta >= 0 || action->repetitions == 0, delta < 0 || action->repetitions == 0);
    }
    if (time >= duration || time < 0) {
        double crossed = floor(time / duration); time -= duration * crossed; loops += fabs(crossed);
        double pending = action->repetitions - loops;
        if (pending <= 0) {
            if (action->clamp) action->paused = true; else action->enabled = false;
            time = delta > 0 ? duration : 0;
            animation_queue_event(action, "finished", delta > 0 ? 1 : -1);
        } else {
            animation_action_endings(action, pending == 1 && delta < 0, pending == 1 && delta >= 0);
            action->loop_count = loops; animation_queue_event(action, "loop", crossed);
        }
    } else action->loop_count = loops;
    action->time = time;
    return pingpong && fmod(loops, 2) == 1 ? duration - time : time;
}
static bool animation_value_equal(const AnimationValue& a, const AnimationValue& b) {
    return a.type == b.type && a.count == b.count && (a.type == ANIMATION_STRING ?
        strcmp(a.text, b.text) == 0 : memcmp(a.numbers, b.numbers, a.count * sizeof(double)) == 0);
}
bool animation_mixer_update(AnimationMixerState* mixer, double delta) {
    if (!mixer || mixer->updating || mixer->dispatching || !isfinite(delta) || !isfinite(delta * mixer->time_scale + mixer->time)) return false;
    mixer->updating = true; mixer->event_count = 0;
    delta *= mixer->time_scale; mixer->time += delta; bool valid = true;
    for (auto* property = mixer->properties; property; property = property->next) {
        property->weight = property->additive_weight = 0; property->sampled = property->base;
        property->additive = {}; property->additive.type = property->base.type; property->additive.count = property->base.count;
        if (property->base.type == ANIMATION_QUATERNION) property->additive.numbers[3] = 1;
        else if(property->base.type==ANIMATION_BOOLEAN||property->base.type==ANIMATION_STRING) property->additive=property->base;
    }
    for (unsigned index = 0; index < mixer->active_count && valid; index++) {
        auto* action = mixer->active[index];
        double step = delta;
        if (!action->enabled) { action->effective_weight = 0; continue; }
        if (action->scheduled) {
            double running = (mixer->time - action->scheduled_start) * (delta < 0 ? -1 : delta > 0 ? 1 : 0);
            if (running < 0 || delta == 0) step = 0;
            else { step = delta < 0 ? -running : running; action->scheduled = false; }
        }
        double scale = action->time_scale;
        if (!action->paused && action->warp.active) {
            scale *= animation_envelope_value(action->warp, mixer->time);
            if (mixer->time > action->warp.end) {
                action->warp.active = false;
                if (!scale) action->paused = true;
                else { if(action->restore_warp_scale) scale=action->restore_time_scale;action->time_scale=scale; }
                action->restore_warp_scale=false;
            }
        }
        action->effective_time_scale = action->paused ? 0 : scale;
        double time = animation_action_advance(action, step * action->effective_time_scale);
        double weight = action->enabled ? action->weight : 0;
        if (action->enabled && action->fade.active) {
            double factor = animation_envelope_value(action->fade, mixer->time); weight *= factor;
            if (mixer->time > action->fade.end) { action->fade.active = false; if (!factor) action->enabled = false; }
        }
        action->effective_weight = weight;
        if (!(weight > 0)) continue;
        for (unsigned i = 0; i < action->clip.count && valid; i++) {
            const auto& channel = action->clip.channels[i]; auto track = channel.track;
            track.ending_start = action->ending_start; track.ending_end = action->ending_end;
            auto* property = animation_property(mixer, channel.property); AnimationValue value = property->base;
            const char* text = nullptr;
            valid = animation_track_sample(track, time, value.numbers, &text);
            if (!valid) break;
            if (track.type == ANIMATION_STRING) str_copy(value.text, sizeof(value.text), text, strlen(text));
            AnimationValue* accumulator = action->clip.additive ? &property->additive : &property->sampled;
            double* accumulated = action->clip.additive ? &property->additive_weight : &property->weight;
            if (!*accumulated && !action->clip.additive) *accumulator = value;
            else if (value.type == ANIMATION_STRING) {
                if ((action->clip.additive?weight:weight / (*accumulated + weight)) >= .5) *accumulator = value;
            } else valid = animation_value_mix(value.type, accumulator->numbers, value.numbers,
                value.count, action->clip.additive ? weight : weight / (*accumulated + weight), action->clip.additive);
            *accumulated += weight;
        }
    }
    for (auto* property = mixer->properties; property && valid; property = property->next) if (property->active) {
        AnimationValue value = property->sampled;
        if (property->weight < 1) {
            if (value.type == ANIMATION_STRING) { if (property->weight <= .5) value = property->base; }
            else valid = animation_value_mix(value.type, value.numbers, property->base.numbers, value.count, 1 - property->weight);
        }
        if (property->additive_weight > 0) {
            if (value.type == ANIMATION_STRING) value = property->additive;
            else valid = valid && animation_value_mix(value.type, value.numbers, property->additive.numbers, value.count, 1, true);
        }
        if (valid && !animation_value_equal(value, property->applied)) {
            valid = mixer->ops.write(mixer->owner, property->identity, &value);
            if (valid) property->applied = value;
        }
    }
    // callbacks run after sampling commits; reentrant event handlers cannot mutate an in-flight walk.
    mixer->updating = false;
    unsigned events = mixer->event_count; mixer->event_count = 0;
    AnimationPendingEvent delivered[256]; memcpy(delivered, mixer->events, events * sizeof(AnimationPendingEvent));
    mixer->dispatching=true;
    if (mixer->ops.event) for (unsigned i = 0; i < events&&!mixer->destroy_pending; i++) mixer->ops.event(mixer->owner, delivered[i].action, delivered[i].type, delivered[i].detail);
    mixer->dispatching=false;
    if(mixer->destroy_pending) animation_mixer_destroy(mixer);
    return valid;
}
bool animation_mixer_set_time(AnimationMixerState* mixer, double time) {
    if (!mixer || mixer->updating || !isfinite(time)) return false;
    mixer->time = 0;
    for (auto* action = mixer->actions; action; action = action->next) action->time = 0;
    return animation_mixer_update(mixer, time);
}
double animation_mixer_time(const AnimationMixerState* mixer) { return mixer ? mixer->time : 0; }
bool animation_mixer_time_scale(AnimationMixerState* mixer, double scale) {
    if (!mixer || !isfinite(scale)) return false; mixer->time_scale = scale; return true;
}
bool animation_mixer_property_active(AnimationMixerState* mixer,uint64_t identity) {
    auto* property=mixer?animation_property(mixer,identity):nullptr;return property&&property->active;
}
bool animation_mixer_active(const AnimationMixerState* mixer) {
    if (!mixer || !mixer->time_scale) return false;
    for (auto* action = mixer->actions; action; action = action->next)
        if (action->active && action->enabled && (action->scheduled || action->fade.active || action->warp.active || (!action->paused && action->time_scale))) return true;
    return false;
}
bool animation_mixer_uncache(AnimationMixerState* mixer, AnimationActionState* action) {
    if (!mixer || !action || action->mixer != mixer || mixer->updating) return false;
    if (!animation_action_stop(action)) return false;
    auto** link = &mixer->actions; while (*link && *link != action) link = &(*link)->next;
    if (!*link) return false; *link = action->next;
    for (unsigned i = 0; i < action->clip.count; i++)
        animation_property(mixer, action->clip.channels[i].property)->references--;
    animation_properties_prune(mixer);
    mem_free(action); mixer->action_count--; return true;
}
