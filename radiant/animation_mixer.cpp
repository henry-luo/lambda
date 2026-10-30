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
    double root_delta[4], root_total[4];
    bool root_motion;
};
struct AnimationPendingEvent {
    AnimationTimelineEvent value;
    double position; // order along this update, including reverse mixer time
    bool legacy;
};
struct AnimationEventBatch {
    AnimationPendingEvent events[ANIMATION_EVENT_LIMIT];
    unsigned count, marker_work;
};
struct AnimationMarkerState {
    char name[128];
    double time, min_weight;
};
struct AnimationChannelState {
    double weight;
    double reference[ANIMATION_VALUE_COMPONENTS];
    bool has_reference;
};
struct AnimationMixerState {
    void* owner;
    AnimationBindingOps ops;
    AnimationActionState* actions;
    AnimationPropertyState* properties;
    double time, time_scale;
    uint64_t next_identity, event_sequence, motion_epoch, sample_sequence;
    unsigned action_count, property_count;
    AnimationActionState* active[256];
    unsigned active_count;
    bool updating, dispatching, destroy_pending;
};

static AnimationPropertyState* animation_property(const AnimationMixerState* mixer, uint64_t identity) {
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
    if (mixer) {
        mixer->owner = owner;
        mixer->ops = ops;
        mixer->time_scale = 1;
        mixer->next_identity = 1;
        mixer->motion_epoch = 1;
    }
    return mixer;
}
void animation_mixer_destroy(AnimationMixerState* mixer) {
    if (!mixer) return;
    // an event may release its own mixer; keep the dispatch walk alive until that callback returns.
    if(mixer->updating||mixer->dispatching) {mixer->destroy_pending=true;return;}
    mixer->updating=true;
    while (mixer->actions) {
        auto* action = mixer->actions;
        mixer->actions = action->next;
        mem_free(action->channels);
        mem_free(action->markers);
        mem_free(action);
    }
    while (mixer->properties) { auto* property = mixer->properties; mixer->properties = property->next;
        if (property->active) mixer->ops.write(mixer->owner, property->identity, &property->base);
        mem_free(property);
    }
    mem_free(mixer);
}
static AnimationChannelBlend animation_channel_blend(const AnimationClipView& clip, const AnimationChannelBlend* inputs,
                                                     unsigned index) {
    return inputs ? inputs[index] : clip.channels[index].blend ? *clip.channels[index].blend : AnimationChannelBlend{};
}
static bool animation_channels_copy(const AnimationClipView& clip, const AnimationChannelBlend* inputs,
                                    AnimationChannelState** result) {
    *result = nullptr;
    bool custom = false;
    for (unsigned i = 0; i < clip.count; i++) {
        const auto blend = animation_channel_blend(clip, inputs, i);
        const auto& track = clip.channels[i].track;
        if (!isfinite(blend.weight) || blend.weight < 0 || blend.weight > 1 ||
            (blend.reference && (!clip.additive || track.type > ANIMATION_QUATERNION))) {
            return false;
        }
        if (blend.reference) {
            for (unsigned c = 0; c < track.components; c++) {
                if (!isfinite(blend.reference[c])) {
                    return false;
                }
            }
            if (track.type == ANIMATION_QUATERNION) {
                double reference[4];
                memcpy(reference, blend.reference, sizeof(reference));
                if (!animation_quaternion_normalize(reference)) {
                    return false;
                }
            }
        }
        custom = custom || blend.weight != 1 || blend.reference;
    }
    if (!custom) {
        return true;
    }
    auto* channels = (AnimationChannelState*)mem_calloc(clip.count, sizeof(AnimationChannelState), MEM_CAT_RENDER);
    if (!channels) {
        return false;
    }
    for (unsigned i = 0; i < clip.count; i++) {
        const auto blend = animation_channel_blend(clip, inputs, i);
        channels[i].weight = blend.weight;
        channels[i].has_reference = blend.reference != nullptr;
        if (blend.reference) {
            memcpy(channels[i].reference, blend.reference, clip.channels[i].track.components * sizeof(double));
            if (clip.channels[i].track.type == ANIMATION_QUATERNION) {
                animation_quaternion_normalize(channels[i].reference);
            }
        }
    }
    *result = channels;
    return true;
}

bool animation_action_set_blend(AnimationActionState* action, const AnimationChannelBlend* channels, unsigned count) {
    if (!action || action->mixer->updating || !channels || count != action->clip.count) {
        return false;
    }
    AnimationChannelState* copied = nullptr;
    if (!animation_channels_copy(action->clip, channels, &copied)) {
        return false;
    }
    mem_free(action->channels);
    action->channels = copied;
    return true;
}

static bool animation_markers_valid(const AnimationClipView& clip) {
    if (clip.marker_count > ANIMATION_MARKER_LIMIT || (clip.marker_count && !clip.markers)) {
        return false;
    }
    for (unsigned i = 0; i < clip.marker_count; i++) {
        const auto& marker = clip.markers[i];
        if (!marker.name || !*marker.name || strlen(marker.name) >= sizeof(AnimationMarkerState::name) ||
            !isfinite(marker.time) || marker.time < 0 || marker.time > clip.duration || !isfinite(marker.min_weight) ||
            marker.min_weight < 0 || marker.min_weight > 1 || (i && marker.time < clip.markers[i - 1].time)) {
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if (!strcmp(marker.name, clip.markers[j].name)) {
                return false;
            }
        }
    }
    return true;
}

AnimationActionState* animation_mixer_action(AnimationMixerState* mixer, const AnimationClipView& clip) {
    if (!mixer || mixer->updating || !clip.identity || !clip.channels || !clip.count ||
        clip.count > ANIMATION_CHANNEL_LIMIT || !isfinite(clip.duration) || clip.duration < 0 ||
        !animation_markers_valid(clip))
        return nullptr;
    for (auto* action = mixer->actions; action; action = action->next)
        if (action->clip.identity == clip.identity) return action;
    if (mixer->action_count >= 256 || !mixer->next_identity || mixer->next_identity == UINT64_MAX) {
        return nullptr;
    }
    bool new_root = false;
    unsigned root_channels[ANIMATION_ROOT_CHANNEL_LIMIT] = {};
    for (unsigned i = 0; i < clip.count; i++) {
        const auto& channel = clip.channels[i];
        if (!animation_track_validate(channel.track) || channel.track.components > ANIMATION_VALUE_COMPONENTS) {
            return nullptr;
        }
        if (channel.root_motion) {
            bool rotation = channel.track.type == ANIMATION_QUATERNION;
            if (clip.additive || (!rotation && channel.track.type != ANIMATION_VECTOR) ||
                channel.track.components != (rotation ? 4u : 3u) || root_channels[rotation]) {
                return nullptr;
            }
            root_channels[rotation] = i + 1;
            new_root = new_root || !animation_property(mixer, channel.property);
        }
    }
    if (new_root && mixer->motion_epoch == UINT64_MAX) {
        return nullptr;
    }
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
            property->root_motion = channel.root_motion;
            if (channel.root_motion && channel.track.type == ANIMATION_QUATERNION) {
                property->root_delta[3] = property->root_total[3] = 1;
            }
            property->next = mixer->properties; mixer->properties = property; mixer->property_count++;
        } else if (property->base.type != channel.track.type || property->base.count != channel.track.components ||
                   property->root_motion != channel.root_motion) {
            animation_properties_prune(mixer); return nullptr;
        }
    }
    auto* action = (AnimationActionState*)mem_calloc(1, sizeof(AnimationActionState), MEM_CAT_RENDER);
    if (!action) { animation_properties_prune(mixer); return nullptr; }
    if (!animation_channels_copy(clip, nullptr, &action->channels)) {
        mem_free(action);
        animation_properties_prune(mixer);
        return nullptr;
    }
    if (clip.marker_count) {
        action->markers =
            (AnimationMarkerState*)mem_calloc(clip.marker_count, sizeof(AnimationMarkerState), MEM_CAT_RENDER);
        if (!action->markers) {
            mem_free(action->channels);
            mem_free(action);
            animation_properties_prune(mixer);
            return nullptr;
        }
        for (unsigned i = 0; i < clip.marker_count; i++) {
            const auto& marker = clip.markers[i];
            str_copy(action->markers[i].name, sizeof(action->markers[i].name), marker.name, strlen(marker.name));
            action->markers[i].time = marker.time;
            action->markers[i].min_weight = marker.min_weight;
        }
    }
    action->mixer = mixer;
    action->clip = clip;
    action->clip.markers = nullptr;
    action->identity = mixer->next_identity++;
    memcpy(action->root_channels, root_channels, sizeof(root_channels));
    action->time_scale = action->weight = action->effective_time_scale = action->effective_weight = 1;
    action->enabled = action->zero_slope_start = action->zero_slope_end = true;
    action->repetitions = INFINITY; action->loop_count = -1; action->loop = ANIMATION_LOOP_REPEAT;
    action->next = mixer->actions; mixer->actions = action; mixer->action_count++;
    if (new_root) {
        mixer->motion_epoch++;
    }
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
struct AnimationAdvance {
    const char* type;
    double detail, step, delta, time, contribution, mask;
};
static double animation_action_advance(AnimationActionState* action, double delta, AnimationAdvance* advance) {
    double duration = action->clip.duration, time = action->time + delta, loops = action->loop_count;
    bool pingpong = action->loop == ANIMATION_LOOP_PINGPONG;
    if (!delta) return pingpong && loops >= 0 && fmod(loops, 2) == 1 ? duration - time : time;
    if (action->loop == ANIMATION_LOOP_ONCE || duration == 0) {
        if (loops == -1) { action->loop_count = 0; animation_action_endings(action, true, true); }
        if (time >= duration || time < 0) {
            time = time < 0 ? 0 : duration;
            if (action->clamp) action->paused = true; else action->enabled = false;
            advance->type = "finished";
            advance->detail = delta < 0 ? -1 : 1;
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
            advance->type = "finished";
            advance->detail = delta > 0 ? 1 : -1;
        } else {
            animation_action_endings(action, pending == 1 && delta < 0, pending == 1 && delta >= 0);
            action->loop_count = loops;
            advance->type = "loop";
            advance->detail = crossed;
        }
    } else action->loop_count = loops;
    action->time = time;
    return pingpong && fmod(loops, 2) == 1 ? duration - time : time;
}
static double animation_action_mask(const AnimationActionState* action) {
    double mask = action->channels ? 0 : 1;
    if (action->channels) {
        for (unsigned i = 0; i < action->clip.count; i++) {
            mask = fmax(mask, action->channels[i].weight);
        }
    }
    return mask;
}
static double animation_action_contribution(const AnimationActionState* action, double time, double mask,
                                            bool fade = true) {
    if (!action->enabled) {
        return 0;
    }
    double weight = action->weight;
    if (fade && action->fade.active) {
        weight *= animation_envelope_value(action->fade, time);
    }
    return fmax(0, weight) * mask;
}

// stage timing and event admission before changing the live mixer or calling a binding.
static bool animation_action_step(AnimationActionState* action, double delta, double mixer_time,
                                  AnimationAdvance* advance) {
    advance->step = delta;
    if (!action->enabled) {
        action->effective_weight = 0;
        advance->time = action->time;
        return true;
    }
    if (!isfinite(action->time) || !isfinite(action->time_scale) || !isfinite(action->weight) ||
        !isfinite(action->loop_count) || action->loop_count < -1 || action->loop > ANIMATION_LOOP_PINGPONG ||
        isnan(action->repetitions) || action->repetitions < 0) {
        return false;
    }
    if (action->scheduled) {
        double running = (mixer_time - action->scheduled_start) * (delta < 0 ? -1 : delta > 0 ? 1 : 0);
        if (!isfinite(running)) {
            return false;
        }
        if (running < 0 || delta == 0) {
            advance->step = 0;
        } else {
            advance->step = delta < 0 ? -running : running;
            action->scheduled = false;
        }
    }
    double scale = action->time_scale;
    if (!action->paused && action->warp.active) {
        scale *= animation_envelope_value(action->warp, mixer_time);
        if (mixer_time > action->warp.end) {
            action->warp.active = false;
            if (!scale) {
                action->paused = true;
            } else {
                if (action->restore_warp_scale) {
                    scale = action->restore_time_scale;
                }
                action->time_scale = scale;
            }
            action->restore_warp_scale = false;
        }
    }
    action->effective_time_scale = action->paused ? 0 : scale;
    advance->delta = advance->step * action->effective_time_scale;
    if (!isfinite(advance->delta) || !isfinite(action->time + advance->delta)) {
        return false;
    }
    advance->mask = animation_action_mask(action);
    advance->contribution = animation_action_contribution(action, mixer_time, advance->mask);
    advance->time = animation_action_advance(action, advance->delta, advance);
    double weight = action->weight;
    if (action->fade.active) {
        double factor = animation_envelope_value(action->fade, mixer_time);
        weight *= factor;
        if (action->enabled && mixer_time > action->fade.end) {
            action->fade.active = false;
            if (!factor) {
                action->enabled = false;
            }
        }
    }
    action->effective_weight = action->enabled ? weight : 0;
    return isfinite(advance->time) && isfinite(advance->contribution);
}

struct AnimationTimelineContext {
    AnimationMixerState* mixer;
    const AnimationAdvance* advances;
    double time, delta;
    bool absolute;
};
static double animation_terminal_distance(const AnimationActionState* action, const AnimationAdvance& advance,
                                          double* iteration = nullptr, double* direction_out = nullptr) {
    double direction = advance.delta > 0 ? 1 : -1;
    double duration = action->clip.duration;
    double distance = fmax(0, direction > 0 ? duration - action->time : action->time);
    double loops = action->loop_count < 0 && direction > 0 ? 0 : action->loop_count;
    double crossed = duration > 0 ? fabs(floor((action->time + advance.delta) / duration)) : 0;
    if (action->loop == ANIMATION_LOOP_ONCE || !duration) {
        loops = 0;
    } else {
        if (!strcmp(advance.type, "finished")) {
            crossed = fmin(crossed, fmax(1, ceil(action->repetitions - loops)));
        }
        distance += fmax(0, crossed - 1) * duration;
        if (action->loop == ANIMATION_LOOP_PINGPONG && loops + crossed - 1 >= 0 && fmod(loops + crossed - 1, 2) == 1) {
            direction = -direction;
        }
        loops += crossed;
    }
    if (iteration) {
        *iteration = fmax(0, loops);
    }
    if (direction_out) {
        *direction_out = direction;
    }
    return fmin(fabs(advance.delta), distance);
}
static double animation_event_position(const AnimationAdvance& advance, double delta, double distance) {
    double fraction = advance.delta ? distance / fabs(advance.delta) : 0;
    return delta ? 1 - advance.step * (1 - fraction) / delta : 0;
}
static double animation_event_weight(const AnimationTimelineContext& context, const AnimationActionState* selected,
                                     double time, double position) {
    double total = 0, contribution = 0;
    for (unsigned i = 0; i < context.mixer->active_count; i++) {
        auto action = *context.mixer->active[i];
        if (context.absolute) {
            action.time = 0;
        }
        const auto& advance = context.advances[i];
        if (advance.type && !strcmp(advance.type, "finished") && !action.clamp &&
            position >
                animation_event_position(advance, context.delta, animation_terminal_distance(&action, advance))) {
            continue;
        }
        double weight = animation_action_contribution(&action, time, advance.mask);
        total += weight;
        if (context.mixer->active[i]->identity == selected->identity) {
            contribution = weight;
        }
    }
    return contribution / fmax(1, total);
}

static bool animation_append_event(AnimationEventBatch* batch, const AnimationActionState* action,
                                   const AnimationAdvance& advance, const AnimationTimelineContext& context,
                                   const char* type, const AnimationMarkerState* marker, double distance,
                                   double clip_time, double iteration, double direction, bool legacy) {
    double position = animation_event_position(advance, context.delta, distance);
    // add elapsed motion to the retained origin; subtracting from the rounded frame end loses boundary precision.
    double fraction = advance.delta ? distance / fabs(advance.delta) : 0;
    double time =
        (context.absolute ? 0 : context.mixer->time) + (context.delta - advance.step) + advance.step * fraction;
    double weight = animation_event_weight(context, action, time, position);
    if (marker && !(weight > marker->min_weight)) {
        return true;
    }
    if (batch->count == ANIMATION_EVENT_LIMIT || !isfinite(weight)) {
        return false;
    }
    auto& pending = batch->events[batch->count++];
    pending = {};
    pending.position = position;
    pending.legacy = legacy;
    auto& event = pending.value;
    event.action = action->identity;
    event.type = type;
    event.time = time;
    event.clip_time = clip_time;
    event.iteration = iteration;
    event.direction = direction;
    event.weight = weight;
    event.detail = legacy ? advance.detail : 0;
    if (marker) {
        memcpy(event.marker, marker->name, sizeof(event.marker));
    }
    return isfinite(event.time) && isfinite(event.iteration);
}

struct AnimationTraversalSegment {
    double from, to, walked, iteration, direction;
    bool entrance;
};
typedef bool (*AnimationTraverseFn)(void*, const AnimationTraversalSegment&);
static bool animation_action_traverse(const AnimationActionState* action, const AnimationAdvance& advance, void* owner,
                                      AnimationTraverseFn visit) {
    if (!advance.delta || !action->clip.duration) {
        return true;
    }
    double duration = action->clip.duration, raw = action->time;
    double remaining = fabs(advance.delta), walked = 0;
    double direction = advance.delta > 0 ? 1 : -1;
    double loops = action->loop_count < 0 && direction > 0 ? 0 : action->loop_count;
    // markers and motion share exactly the same bounded loop/turn/finite-ending intervals.
    for (unsigned segment = 0; segment <= ANIMATION_EVENT_LIMIT; segment++) {
        double boundary = fmax(0, direction > 0 ? duration - raw : raw);
        double distance = fmin(remaining, boundary);
        double end = raw + direction * distance;
        bool reflected = action->loop == ANIMATION_LOOP_PINGPONG && loops >= 0 && fmod(loops, 2) == 1;
        double from = reflected ? duration - raw : raw, to = reflected ? duration - end : end;
        if (!visit(owner, {from, to, walked, loops, reflected ? -direction : direction, false})) {
            return false;
        }
        bool crossed = direction > 0 ? remaining >= boundary : remaining > boundary;
        if (!crossed || action->loop == ANIMATION_LOOP_ONCE) {
            return true;
        }
        remaining -= distance;
        walked += distance;
        loops++;
        if (!isfinite(loops) || action->repetitions - loops <= 0) {
            return true;
        }
        raw = direction > 0 ? 0 : duration;
        // the repeat's incoming marker is a cue crossing, not a displacement back to its first key.
        if (action->loop == ANIMATION_LOOP_REPEAT && !visit(owner, {raw, raw, walked, loops, direction, true})) {
            return false;
        }
        if (!remaining) {
            return true;
        }
    }
    return false;
}

static bool animation_marker_segment(AnimationEventBatch* batch, const AnimationActionState* action,
                                     const AnimationAdvance& advance, const AnimationTimelineContext& context,
                                     double from, double to, double walked, double iteration, double direction,
                                     bool entrance = false) {
    for (unsigned i = 0; i < action->clip.marker_count; i++) {
        if (++batch->marker_work > ANIMATION_MARKER_WORK_LIMIT) {
            return false;
        }
        const auto& marker = action->markers[i];
        bool crossed = entrance        ? marker.time == to
                       : direction > 0 ? from < marker.time && marker.time <= to
                                       : to <= marker.time && marker.time < from;
        if (crossed && !animation_append_event(batch, action, advance, context, "marker", &marker,
                                               walked + fabs(marker.time - from), marker.time, fmax(0, iteration),
                                               direction, false)) {
            return false;
        }
    }
    return true;
}

static bool animation_marker_events(AnimationEventBatch* batch, const AnimationActionState* action,
                                    const AnimationAdvance& advance, const AnimationTimelineContext& context) {
    if (!advance.delta || !action->clip.marker_count || !action->clip.duration) {
        return true;
    }
    bool eligible = false;
    for (unsigned i = 0; i < action->clip.marker_count; i++) {
        eligible = eligible || animation_action_contribution(action, context.time, advance.mask, false) >
                                   action->markers[i].min_weight;
    }
    if (!eligible) {
        return true;
    }
    struct Traversal {
        AnimationEventBatch* batch;
        const AnimationActionState* action;
        const AnimationAdvance* advance;
        const AnimationTimelineContext* context;
        static bool visit(void* owner, const AnimationTraversalSegment& segment) {
            const auto& traversal = *(Traversal*)owner;
            return animation_marker_segment(traversal.batch, traversal.action, *traversal.advance, *traversal.context,
                                            segment.from, segment.to, segment.walked, segment.iteration,
                                            segment.direction, segment.entrance);
        }
    } traversal = {batch, action, &advance, &context};
    return animation_action_traverse(action, advance, &traversal, Traversal::visit);
}

struct AnimationRootDraft {
    AnimationPropertyState* property;
    double delta[4], total[4], weight;
};
struct AnimationRootTraversal {
    AnimationTrackView track;
    double delta[4];
    static bool visit(void* owner, const AnimationTraversalSegment& segment) {
        auto* traversal = (AnimationRootTraversal*)owner;
        if (segment.entrance || segment.from == segment.to) {
            return true;
        }
        double from[4], to[4];
        if (!animation_track_sample(traversal->track, segment.from, from) ||
            !animation_track_sample(traversal->track, segment.to, to)) {
            return false;
        }
        if (traversal->track.type == ANIMATION_QUATERNION) {
            for (unsigned c = 0; c < 3; c++) {
                from[c] = -from[c];
            }
            // parent-frame change is end * inverse(start); chronological changes compose on the left.
            return animation_quaternion_multiply(to, from, to) &&
                   animation_quaternion_multiply(to, traversal->delta, traversal->delta);
        }
        return animation_value_combine(to, from, 3, 1, -1) && animation_value_combine(traversal->delta, to, 3, 1, 1);
    }
};
static bool animation_root_drafts(AnimationMixerState* mixer, const AnimationAdvance* advances,
                                  const AnimationActionPlayback* playback, double time, bool absolute,
                                  AnimationRootDraft drafts[256 * ANIMATION_ROOT_CHANNEL_LIMIT], unsigned* count) {
    *count = 0;
    if (absolute) {
        return true;
    }
    for (unsigned i = 0; i < mixer->active_count; i++) {
        const auto* action = mixer->active[i];
        for (unsigned kind = 0; kind < ANIMATION_ROOT_CHANNEL_LIMIT; kind++) {
            if (!action->root_channels[kind]) {
                continue;
            }
            unsigned index = action->root_channels[kind] - 1;
            const auto& channel = action->clip.channels[index];
            bool rotation = channel.track.type == ANIMATION_QUATERNION;
            double mask = action->channels ? action->channels[index].weight : 1;
            double weight = animation_action_contribution(action, time, mask);
            if (!isfinite(weight) || action->time < 0 || action->time > action->clip.duration) {
                return false;
            }
            if (!weight) {
                continue;
            }
            AnimationRootTraversal traversal = {channel.track, {0, 0, 0, rotation ? 1.0 : 0.0}};
            traversal.track.ending_start = playback[i].ending_start;
            traversal.track.ending_end = playback[i].ending_end;
            if (!animation_action_traverse(action, advances[i], &traversal, AnimationRootTraversal::visit)) {
                return false;
            }
            auto* property = animation_property(mixer, channel.property);
            unsigned draft = 0;
            while (draft < *count && drafts[draft].property != property) {
                draft++;
            }
            if (draft == *count) {
                drafts[(*count)++] = {property, {0, 0, 0, rotation ? 1.0 : 0.0}, {}, 0};
            }
            auto& output = drafts[draft];
            output.weight += weight;
            if (!isfinite(output.weight)) {
                return false;
            }
            if (rotation) {
                // use the same incremental normalized slerp as pose blending, including paused identity contributors.
                if (!animation_quaternion_slerp(output.delta, traversal.delta, weight / output.weight, output.delta)) {
                    return false;
                }
            } else if (!animation_value_combine(output.delta, traversal.delta, 3, 1, weight)) {
                return false;
            }
        }
    }
    for (unsigned i = 0; i < *count; i++) {
        auto& draft = drafts[i];
        if (draft.property->base.type == ANIMATION_QUATERNION) {
            const double identity[4] = {0, 0, 0, 1};
            if (!animation_quaternion_slerp(identity, draft.delta, fmin(1, draft.weight), draft.delta) ||
                !animation_quaternion_multiply(draft.delta, draft.property->root_total, draft.total)) {
                return false;
            }
        } else {
            for (unsigned c = 0; c < 3; c++) {
                draft.delta[c] /= fmax(1, draft.weight);
            }
            memcpy(draft.total, draft.property->root_total, sizeof(draft.total));
            if (!animation_value_combine(draft.total, draft.delta, 3, 1, 1)) {
                return false;
            }
        }
    }
    return true;
}

static bool animation_terminal_event(AnimationEventBatch* batch, const AnimationActionState* action,
                                     const AnimationAdvance& advance, const AnimationTimelineContext& context) {
    if (!advance.type) {
        return true;
    }
    double iteration, direction;
    double distance = animation_terminal_distance(action, advance, &iteration, &direction);
    return animation_append_event(batch, action, advance, context, advance.type, nullptr, distance,
                                  direction > 0 ? action->clip.duration : 0, iteration, direction, true);
}

static bool animation_value_equal(const AnimationValue& a, const AnimationValue& b) {
    return a.type == b.type && a.count == b.count && (a.type == ANIMATION_STRING ?
        strcmp(a.text, b.text) == 0 : memcmp(a.numbers, b.numbers, a.count * sizeof(double)) == 0);
}
static bool animation_mixer_sample(AnimationMixerState* mixer, double delta, bool emit_timeline, bool absolute,
                                   bool extract_root_motion, bool admission_only = false) {
    if (!mixer || mixer->updating || mixer->dispatching || !isfinite(delta) ||
        !isfinite(delta * mixer->time_scale + (absolute ? 0 : mixer->time)) || mixer->sample_sequence == UINT64_MAX ||
        (absolute && mixer->motion_epoch == UINT64_MAX)) {
        return false;
    }
    delta *= mixer->time_scale;
    double mixer_time = (absolute ? 0 : mixer->time) + delta;
    AnimationActionPlayback playback[256];
    AnimationAdvance advances[256] = {};
    AnimationEventBatch batch = {};
    double contribution = 0;
    for (unsigned i = 0; i < mixer->active_count; i++) {
        auto staged = *mixer->active[i];
        if (absolute) {
            staged.time = 0;
        }
        if (!animation_action_step(&staged, delta, mixer_time, advances + i)) {
            return false;
        }
        playback[i] = staged;
        contribution += advances[i].contribution;
    }
    if (!isfinite(contribution)) {
        return false;
    }
    AnimationRootDraft root_drafts[256 * ANIMATION_ROOT_CHANNEL_LIMIT];
    unsigned root_count = 0;
    if (!animation_root_drafts(mixer, advances, playback, mixer_time, absolute || !extract_root_motion, root_drafts,
                               &root_count)) {
        return false;
    }
    AnimationTimelineContext context = {mixer, advances, mixer_time, delta, absolute};
    for (unsigned i = 0; i < mixer->active_count; i++) {
        auto action = *mixer->active[i];
        if (absolute) {
            action.time = 0;
        }
        if ((emit_timeline && mixer->ops.timeline && !animation_marker_events(&batch, &action, advances[i], context)) ||
            !animation_terminal_event(&batch, &action, advances[i], context)) {
            return false;
        }
    }
    if (batch.count > UINT64_MAX - mixer->event_sequence) {
        return false;
    }
    if (admission_only) {
        return true;
    }
    mixer->updating = true;
    mixer->time = mixer_time;
    if (absolute) {
        for (auto* action = mixer->actions; action; action = action->next) {
            action->time = 0;
        }
    }
    bool valid = true;
    for (auto* property = mixer->properties; property; property = property->next) {
        property->weight = property->additive_weight = 0; property->sampled = property->base;
        property->additive = {}; property->additive.type = property->base.type; property->additive.count = property->base.count;
        if (property->base.type == ANIMATION_QUATERNION) property->additive.numbers[3] = 1;
        else if(property->base.type==ANIMATION_BOOLEAN||property->base.type==ANIMATION_STRING) property->additive=property->base;
    }
    for (unsigned index = 0; index < mixer->active_count && valid; index++) {
        auto* action = mixer->active[index];
        *(AnimationActionPlayback*)action = playback[index];
        double time = advances[index].time;
        double weight = action->effective_weight;
        if (!(weight > 0)) {
            continue;
        }
        for (unsigned i = 0; i < action->clip.count && valid; i++) {
            const auto& channel = action->clip.channels[i];
            if (channel.root_motion) {
                continue;
            }
            auto track = channel.track;
            double channel_weight = weight * (action->channels ? action->channels[i].weight : 1);
            if (!(channel_weight > 0)) {
                continue;
            }
            track.ending_start = action->ending_start; track.ending_end = action->ending_end;
            auto* property = animation_property(mixer, channel.property); AnimationValue value = property->base;
            const char* text = nullptr;
            valid = animation_track_sample(track, time, value.numbers, &text);
            if (!valid) break;
            if (action->channels && action->channels[i].has_reference) {
                valid = animation_value_reference_delta(track.type, value.numbers, action->channels[i].reference,
                                                        track.components);
                if (!valid) {
                    break;
                }
            }
            if (track.type == ANIMATION_STRING) str_copy(value.text, sizeof(value.text), text, strlen(text));
            AnimationValue* accumulator = action->clip.additive ? &property->additive : &property->sampled;
            double* accumulated = action->clip.additive ? &property->additive_weight : &property->weight;
            if (!*accumulated && !action->clip.additive) *accumulator = value;
            else if (value.type == ANIMATION_STRING) {
                if ((action->clip.additive ? channel_weight : channel_weight / (*accumulated + channel_weight)) >= .5)
                    *accumulator = value;
            } else
                valid = animation_value_mix(value.type, accumulator->numbers, value.numbers, value.count,
                                            action->clip.additive ? channel_weight
                                                                  : channel_weight / (*accumulated + channel_weight),
                                            action->clip.additive);
            *accumulated += channel_weight;
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
    if (valid) {
        mixer->sample_sequence++;
        if (absolute) {
            mixer->motion_epoch++;
        }
        for (auto* property = mixer->properties; property; property = property->next) {
            memset(property->root_delta, 0, sizeof(property->root_delta));
            property->root_delta[3] = property->base.type == ANIMATION_QUATERNION ? 1 : 0;
            if (absolute) {
                memcpy(property->root_total, property->root_delta, sizeof(property->root_total));
            }
        }
        for (unsigned i = 0; i < root_count; i++) {
            auto* property = root_drafts[i].property;
            memcpy(property->root_delta, root_drafts[i].delta, sizeof(property->root_delta));
            memcpy(property->root_total, root_drafts[i].total, sizeof(property->root_total));
        }
    }
    // copied records survive handlers releasing an action; mixer destruction waits until dispatch unwinds.
    mixer->updating = false;
    mixer->dispatching = true;
    if (valid && mixer->ops.event) {
        for (unsigned i = 0; i < batch.count && !mixer->destroy_pending; i++) {
            const auto& event = batch.events[i];
            if (event.legacy) {
                mixer->ops.event(mixer->owner, event.value.action, event.value.type, event.value.detail);
            }
        }
    }
    if (valid && emit_timeline && mixer->ops.timeline) {
        // stable order follows crossing progress; ties retain active-action and declaration order.
        for (unsigned i = 1; i < batch.count; i++) {
            auto event = batch.events[i];
            unsigned j = i;
            while (j && batch.events[j - 1].position > event.position) {
                batch.events[j] = batch.events[j - 1];
                j--;
            }
            batch.events[j] = event;
        }
        for (unsigned i = 0; i < batch.count && !mixer->destroy_pending; i++) {
            auto& event = batch.events[i].value;
            event.sequence = ++mixer->event_sequence;
            mixer->ops.timeline(mixer->owner, event);
        }
    }
    mixer->dispatching = false;
    if (mixer->destroy_pending) {
        animation_mixer_destroy(mixer);
    }
    return valid;
}
bool animation_mixer_update(AnimationMixerState* mixer, double delta, bool emit_timeline, bool extract_root_motion) {
    return animation_mixer_sample(mixer, delta, emit_timeline, false, extract_root_motion);
}
bool animation_mixer_admit(AnimationMixerState* mixer, double delta) {
    return animation_mixer_sample(mixer, delta, true, false, true, true);
}
bool animation_mixer_set_time(AnimationMixerState* mixer, double time, bool emit_timeline) {
    return animation_mixer_sample(mixer, time, emit_timeline, true, false);
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
    bool removes_root = false;
    for (unsigned i = 0; i < action->clip.count; i++) {
        const auto& channel = action->clip.channels[i];
        auto* property = animation_property(mixer, channel.property);
        removes_root = removes_root || (channel.root_motion && property && property->references == 1);
    }
    if (removes_root && mixer->motion_epoch == UINT64_MAX) {
        return false;
    }
    if (!animation_action_stop(action)) return false;
    auto** link = &mixer->actions; while (*link && *link != action) link = &(*link)->next;
    if (!*link) return false; *link = action->next;
    for (unsigned i = 0; i < action->clip.count; i++)
        animation_property(mixer, action->clip.channels[i].property)->references--;
    animation_properties_prune(mixer);
    if (removes_root) {
        mixer->motion_epoch++;
    }
    mem_free(action->channels);
    mem_free(action->markers);
    mem_free(action); mixer->action_count--; return true;
}

bool animation_mixer_rebind(AnimationMixerState* mixer, void* owner, const AnimationBindingOps* ops) {
    if (!mixer || mixer->updating || mixer->dispatching || mixer->destroy_pending ||
        (ops && (!ops->read || !ops->write))) {
        return false;
    }
    mixer->owner = owner;
    if (ops) {
        mixer->ops = *ops;
    }
    return true;
}

bool animation_action_snapshot(const AnimationActionState* action, AnimationActionSnapshot* result) {
    if (!action || !result || action->mixer->updating || action->mixer->dispatching) {
        return false;
    }
    *(AnimationActionPlayback*)result = *(const AnimationActionPlayback*)action;
    result->identity = action->identity;
    result->duration = action->clip.duration;
    result->active = action->active;
    return true;
}

bool animation_mixer_root_motion(const AnimationMixerState* mixer, uint64_t identity, AnimationRootMotion* result) {
    if (!mixer || !result || mixer->updating || mixer->dispatching) {
        return false;
    }
    for (auto* property = mixer->properties; property; property = property->next) {
        if (property->identity == identity && property->root_motion) {
            AnimationRootMotion motion = {};
            motion.epoch = mixer->motion_epoch;
            motion.sequence = mixer->sample_sequence;
            motion.type = property->base.type;
            motion.count = property->base.count;
            memcpy(motion.delta, property->root_delta, sizeof(motion.delta));
            memcpy(motion.total, property->root_total, sizeof(motion.total));
            *result = motion;
            return true;
        }
    }
    return false;
}

static bool animation_has_action(const AnimationMixerState* mixer, const AnimationActionState* selected) {
    for (auto* action = mixer->actions; action; action = action->next) {
        if (action == selected) {
            return true;
        }
    }
    return false;
}

static AnimationChannelBlend animation_channel_current(const AnimationActionState* action, unsigned index) {
    const auto* state = action->channels ? action->channels + index : nullptr;
    return state ? AnimationChannelBlend{state->weight, state->has_reference ? state->reference : nullptr}
                 : AnimationChannelBlend{};
}
static unsigned animation_pair_channel(const AnimationActionPair& pair, unsigned channel) {
    return pair.channels ? pair.channels[channel] : channel;
}
static bool animation_channels_inherit(const AnimationActionPair& pair) {
    auto* target = pair.target;
    const auto* source = pair.source;
    AnimationChannelBlend inputs[ANIMATION_CHANNEL_LIMIT];
    for (unsigned i = 0; i < target->clip.count; i++) {
        unsigned previous = animation_pair_channel(pair, i);
        auto before = animation_channel_blend(source->clip, nullptr, previous);
        auto after = animation_channel_blend(target->clip, nullptr, i);
        bool same = before.weight == after.weight && !!before.reference == !!after.reference;
        if (same && before.reference) {
            same =
                !memcmp(before.reference, after.reference, target->clip.channels[i].track.components * sizeof(double));
        }
        // only a changed authored mask/reference supersedes a native per-track override (S12.1.5v2).
        inputs[i] = same ? animation_channel_current(source, previous) : animation_channel_current(target, i);
    }
    return animation_action_set_blend(target, inputs, target->clip.count);
}

static AnimationPropertyState* animation_root_previous(AnimationPropertyState* property,
                                                       const AnimationActionPair* pairs, unsigned count) {
    AnimationPropertyState* previous = nullptr;
    for (unsigned i = 0; i < count; i++) {
        for (unsigned c = 0; c < pairs[i].target->clip.count; c++) {
            const auto& channel = pairs[i].target->clip.channels[c];
            if (channel.property != property->identity || !channel.root_motion) {
                continue;
            }
            unsigned previous_channel = animation_pair_channel(pairs[i], c);
            auto* before =
                animation_property(pairs[i].source->mixer, pairs[i].source->clip.channels[previous_channel].property);
            if (!before || !before->root_motion || (previous && previous != before)) {
                return nullptr;
            }
            previous = before;
        }
    }
    return previous;
}
static bool animation_roots_inherit(AnimationMixerState* target, const AnimationMixerState* source,
                                    const AnimationActionPair* pairs, unsigned count) {
    AnimationPropertyState* matched[256 * ANIMATION_ROOT_CHANNEL_LIMIT];
    unsigned matched_count = 0;
    bool changed = false;
    for (auto* property = target->properties; property; property = property->next) {
        if (!property->root_motion) {
            continue;
        }
        auto* previous = animation_root_previous(property, pairs, count);
        if (!previous) {
            changed = true;
            continue;
        }
        memcpy(property->root_total, previous->root_total, sizeof(property->root_total));
        if (matched_count == 256 * ANIMATION_ROOT_CHANNEL_LIMIT) {
            return false;
        }
        matched[matched_count++] = previous;
    }
    for (auto* property = source->properties; property; property = property->next) {
        if (!property->root_motion) {
            continue;
        }
        bool found = false;
        for (unsigned i = 0; i < matched_count; i++) {
            found = found || matched[i] == property;
        }
        changed = changed || !found;
    }
    if (changed && source->motion_epoch == UINT64_MAX) {
        return false;
    }
    target->motion_epoch = source->motion_epoch + (changed ? 1 : 0);
    target->sample_sequence = source->sample_sequence;
    return true;
}

bool animation_mixer_inherit(AnimationMixerState* target, const AnimationMixerState* source,
                             const AnimationActionPair* pairs, unsigned count) {
    if (!target || !source || target == source || target->updating || target->dispatching || source->updating ||
        source->dispatching || count > target->action_count || (count && !pairs) ||
        source->next_identity > UINT64_MAX - (target->action_count - count)) {
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        const auto& pair = pairs[i];
        if (!animation_has_action(target, pair.target) || !animation_has_action(source, pair.source) ||
            pair.target->clip.count != pair.source->clip.count ||
            pair.target->clip.duration != pair.source->clip.duration ||
            pair.target->clip.additive != pair.source->clip.additive ||
            pair.target->clip.count > ANIMATION_CHANNEL_LIMIT ||
            (pair.channels ? pair.channel_count != pair.target->clip.count : pair.channel_count != 0)) {
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if (pairs[j].target == pair.target || pairs[j].source == pair.source) {
                return false;
            }
        }
        bool used[ANIMATION_CHANNEL_LIMIT] = {};
        for (unsigned c = 0; c < pair.target->clip.count; c++) {
            unsigned previous = animation_pair_channel(pair, c);
            if (previous >= pair.source->clip.count || used[previous]) {
                return false;
            }
            used[previous] = true;
            const auto& a = pair.target->clip.channels[c].track;
            const auto& b = pair.source->clip.channels[previous].track;
            if (a.type != b.type || a.components != b.components ||
                pair.target->clip.channels[c].root_motion != pair.source->clip.channels[previous].root_motion) {
                return false;
            }
        }
    }
    if (!animation_roots_inherit(target, source, pairs, count)) {
        return false;
    }
    AnimationActionState* defaults[256];
    unsigned default_count = 0;
    // new clips keep their declared activation; inherited clips follow the previous active-list order.
    for (unsigned i = 0; i < target->active_count; i++) {
        bool inherited = false;
        for (unsigned j = 0; j < count; j++) {
            inherited = inherited || pairs[j].target == target->active[i];
        }
        if (!inherited) {
            defaults[default_count++] = target->active[i];
        }
    }
    while (target->active_count) {
        if (!animation_property_activate(target->active[target->active_count - 1], false)) {
            return false;
        }
    }
    target->time = source->time;
    target->event_sequence = source->event_sequence;
    target->time_scale = source->time_scale;
    target->next_identity = source->next_identity;
    for (auto* action = target->actions; action; action = action->next) {
        const AnimationActionPair* previous = nullptr;
        for (unsigned i = 0; i < count; i++) {
            if (pairs[i].target == action) {
                previous = pairs + i;
                break;
            }
        }
        if (!previous) {
            action->identity = target->next_identity++;
            continue;
        }
        if (!animation_channels_inherit(*previous)) {
            return false;
        }
        *(AnimationActionPlayback*)action = *(const AnimationActionPlayback*)previous->source;
        action->identity = previous->source->identity;
    }
    for (unsigned i = 0; i < source->active_count; i++) {
        for (unsigned j = 0; j < count; j++) {
            if (pairs[j].source == source->active[i] && !animation_action_play(pairs[j].target)) {
                return false;
            }
        }
    }
    for (unsigned i = 0; i < default_count; i++) {
        if (!animation_action_play(defaults[i])) {
            return false;
        }
    }
    return animation_mixer_update(target, 0);
}

AnimationActionState* animation_mixer_find_action(AnimationMixerState* mixer, uint64_t identity) {
    if (mixer) {
        for (auto* action = mixer->actions; action; action = action->next) {
            if (action->identity == identity) {
                return action;
            }
        }
    }
    return nullptr;
}
AnimationMixerState* animation_mixer_clone(const AnimationMixerState* source, void* owner, AnimationBindingOps ops) {
    if (!source || source->updating || source->dispatching || source->destroy_pending) {
        return nullptr;
    }
    // stage evaluation cannot execute completion/controller callbacks against the live adapter.
    auto silent = ops;
    silent.event = nullptr;
    silent.timeline = nullptr;
    auto* target = animation_mixer_create(owner, silent);
    if (!target) {
        return nullptr;
    }
    AnimationActionPair pairs[256];
    unsigned count = 0;
    for (auto* original = source->actions; original; original = original->next) {
        auto clip = original->clip;
        AnimationMarkerView markers[ANIMATION_MARKER_LIMIT];
        for (unsigned i = 0; i < clip.marker_count; i++) {
            const auto& marker = original->markers[i];
            markers[i] = {marker.name, marker.time, marker.min_weight};
        }
        clip.markers = markers;
        auto* action = animation_mixer_action(target, clip);
        if (!action) {
            animation_mixer_destroy(target);
            return nullptr;
        }
        pairs[count++] = {action, original};
    }
    if (!animation_mixer_inherit(target, source, pairs, count)) {
        animation_mixer_destroy(target);
        return nullptr;
    }
    // a binding may currently expose its animated value; retain the captured authored baseline when forking playback.
    for (auto* property = target->properties; property; property = property->next) {
        property->base = animation_property(source, property->identity)->base;
    }
    if (!animation_mixer_update(target, 0, false, false)) {
        animation_mixer_destroy(target);
        return nullptr;
    }
    target->ops = ops;
    return target;
}
static bool animation_command_mask(AnimationActionState* action, unsigned channel, double weight) {
    if (channel >= action->clip.count || !isfinite(weight) || weight < 0 || weight > 1) {
        return false;
    }
    AnimationChannelBlend inputs[ANIMATION_CHANNEL_LIMIT];
    for (unsigned i = 0; i < action->clip.count; i++) {
        inputs[i] = animation_channel_current(action, i);
    }
    inputs[channel].weight = weight;
    return animation_action_set_blend(action, inputs, action->clip.count);
}
bool animation_mixer_commands(AnimationMixerState* mixer, const AnimationCommand* commands, unsigned count) {
    if (!mixer || mixer->updating || mixer->dispatching || mixer->destroy_pending || !commands || !count ||
        count > ANIMATION_COMMAND_LIMIT) {
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        const auto& command = commands[i];
        auto* action = animation_mixer_find_action(mixer, command.action);
        if (!action) {
            return false;
        }
        switch (command.kind) {
        case ANIMATION_COMMAND_PLAY:
            if (command.reset) {
                animation_action_reset(action);
            }
            action->enabled = true;
            action->paused = false;
            if (!animation_action_play(action)) {
                return false;
            }
            break;
        case ANIMATION_COMMAND_STOP:
            if (!animation_action_stop(action)) {
                return false;
            }
            break;
        case ANIMATION_COMMAND_RESET:
            animation_action_reset(action);
            break;
        case ANIMATION_COMMAND_WEIGHT:
            if (!isfinite(command.value) || command.value < 0) {
                return false;
            }
            action->weight = command.value;
            break;
        case ANIMATION_COMMAND_RATE:
            if (!isfinite(command.value)) {
                return false;
            }
            action->time_scale = command.value;
            action->warp.active = action->restore_warp_scale = false;
            break;
        case ANIMATION_COMMAND_PAUSE:
            action->paused = command.paused;
            break;
        case ANIMATION_COMMAND_PHASE:
            if (!isfinite(command.value) || command.value < 0 || command.value > action->clip.duration) {
                return false;
            }
            // a phase change samples the chosen pose, without traversing skipped root motion or cues.
            action->time = command.value;
            action->loop_count = -1;
            break;
        case ANIMATION_COMMAND_MASK:
            if (!animation_command_mask(action, command.channel, command.value)) {
                return false;
            }
            break;
        case ANIMATION_COMMAND_FADE:
            if (!animation_action_fade(action, command.duration, command.fade_in)) {
                return false;
            }
            break;
        case ANIMATION_COMMAND_WARP:
            if (!animation_action_warp(action, command.value, command.second, command.duration)) {
                return false;
            }
            break;
        case ANIMATION_COMMAND_CROSSFADE: {
            auto* other = animation_mixer_find_action(mixer, command.other);
            if (!other || other == action) {
                return false;
            }
            if (command.reset) {
                animation_action_reset(other);
            }
            other->enabled = true;
            other->paused = false;
            if (!animation_action_play(other) ||
                !animation_action_crossfade(action, other, command.duration, command.warp)) {
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
    return animation_mixer_update(mixer, 0, false, false);
}
