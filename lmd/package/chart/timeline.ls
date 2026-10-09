// Playback maps an explicit elapsed clock onto ordered keyframes; no ambient time is read.
import util: .util
import parse: .parse
import cfg: .config

fn nested(spec) {
    let node = if (spec is element) parse.parse_top(spec) else spec;
    node.timeline != null or any([for (child in [*node.children, *node.layer, node.template] where child != null) nested(child)])
}
pub fn configure(spec) {
    let raw = if (spec is element) parse.parse_top(spec) else spec;
    let options = raw.timeline;
    let frames = options.keyframes;
    let repeat = if (options.repeat != null) options.repeat else 1;
    let direction = if (options.direction != null) options.direction else "normal";
    let fill = if (options.fill != null) options.fill else "forwards";
    let length = frames[len(frames) - 1].at;
    let duration = if (options.duration != null) options.duration else length;
    let failure = util.first_error([
        if (not (frames is array) or len(frames) < 1 or frames[0].at != 0) error("chart: timeline keyframes must start at zero"),
        for (i, frame in frames)
            if (not util.finite_number(frame.at) or frame.at < 0 or i > 0 and frame.at <= frames[i - 1].at)
                error("chart: keyframe times must be finite and strictly increasing")
            else if (not (frame.spec is map or frame.spec is element)) error("chart: keyframe requires a specification")
            else if (nested(frame.spec)) error("chart: recursive timelines are invalid"),
        if (repeat != "infinite" and (not util.finite_number(repeat) or repeat < 1 or floor(repeat) != repeat))
            error("chart: timeline repeat must be a positive integer or infinite"),
        if (not contains(["normal", "reverse", "alternate", "reverse_alternate"], direction)) error("chart: invalid playback direction"),
        if (not contains(["forwards", "none"], fill)) error("chart: invalid timeline fill"),
        if (not util.finite_number(duration) or duration < 0 or len(frames) > 1 and duration == 0)
            error("chart: timeline duration must be finite and positive")]);
    if (failure is error) failure else {keyframes: frames, repeat: repeat, direction: direction, fill: fill,
        duration: duration, length: length, autoplay: options.autoplay == true}
}
pub fn inherit(base, keyframe) {
    let spec = if (keyframe is element) parse.parse_top(keyframe) else keyframe;
    {*:base, *:spec, timeline: null, encoding: {*:parse.attributes(base.encoding), *:parse.attributes(spec.encoding)},
        config: cfg.inherit(base.config, spec.config)}
}
pub fn target(raw) {
    let spec = if (raw is element) parse.parse_top(raw) else raw;
    if (spec.timeline == null) spec else {
        let plan = configure(spec);
        if (plan is error) plan else inherit(spec, plan.keyframes[len(plan.keyframes) - 1].spec)
    }
}
pub fn position(plan, elapsed) {
    let completed = plan.repeat != "infinite" and elapsed >= plan.duration * plan.repeat;
    let cycle = if (plan.duration == 0) 0 else if (completed) plan.repeat - 1 else floor(elapsed / plan.duration);
    let local = if (plan.duration == 0) 0.0 else if (completed) plan.duration else elapsed - cycle * plan.duration;
    let reverse = contains(["reverse", "reverse_alternate"], plan.direction) !=
        (contains(["alternate", "reverse_alternate"], plan.direction) and cycle % 2 == 1);
    let position = if (completed and plan.fill == "none") 0.0 else
        (if (reverse) 1.0 - local / (if (plan.duration == 0) 1.0 else plan.duration) else local / (if (plan.duration == 0) 1.0 else plan.duration)) * plan.length;
    {at: position, complete: completed, cycle: cycle, reversed: reverse}
}
pub fn segment(spec, elapsed, exact = false) {
    let plan = configure(spec);
    if (plan is error) plan else if (not util.finite_number(elapsed) or elapsed < 0) error("chart: timeline time must be finite and nonnegative") else {
    let point = if (exact) {at: util.clamp_val(elapsed, 0.0, plan.length), complete: false, cycle: 0}
        else position(plan, elapsed);
    let indices = [for (i, frame in plan.keyframes where frame.at <= point.at) i];
    let index = if (len(indices) > 0) indices[len(indices) - 1] else 0;
    let first = plan.keyframes[index];
    let destination = plan.keyframes[min(index + 1, len(plan.keyframes) - 1)];
    {*:point, source: inherit(spec, first.spec), target: inherit(spec, destination.spec),
        elapsed: point.at - first.at, duration: destination.at - first.at,
        progress: if (destination.at == first.at) 1.0 else (point.at - first.at) / (destination.at - first.at)}
    }
}

pub fn initial(spec) {
    let plan = configure(spec);
    if (plan is error) plan else {phase: if (plan.autoplay) "playing" else "idle", elapsed: 0.0,
        anchor: null, reversed: false, token: 0, position: null, generation: 0}
}
pub fn command(playback, action, now, plan) {
    let command = action.command;
    let known = contains(["play", "pause", "resume", "seek", "reverse", "cancel", "frame"], command);
    let elapsed = if (playback.phase == "playing" and playback.anchor != null and util.finite_number(now))
        max(0.0, playback.elapsed + (now - playback.anchor) * (if (playback.reversed) -1.0 else 1.0)) else playback.elapsed;
    let requested = if (action.position != null) action.position else if (action.time_ms != null) action.time_ms else action.value;
    // seek positions use keyframe units; resume retains duration rescaling and cycle direction.
    let point = position(plan, elapsed);
    let seek_elapsed = point.cycle * plan.duration + (if (point.reversed) 1.0 - requested / (if (plan.length > 0) plan.length else 1.0)
        else requested / (if (plan.length > 0) plan.length else 1.0)) * plan.duration;
    let next = if (command == "cancel") {*:playback, phase: "idle", elapsed: 0.0, position: 0.0, anchor: null}
        else if (command == "play") {*:playback, phase: "playing", elapsed: 0.0, position: null, anchor: now}
        else if (command == "pause") {*:playback, phase: "paused", elapsed: elapsed, anchor: null}
        else if (command == "resume") {*:playback, phase: "playing", position: null, anchor: now}
        else if (command == "seek") {*:playback, elapsed: seek_elapsed, position: requested, anchor: if (playback.phase == "playing") now else null}
        else if (command == "reverse") {*:playback, elapsed: elapsed, reversed: not playback.reversed, anchor: if (playback.phase == "playing") now else null, position: null}
        else {*:playback, elapsed: elapsed, anchor: now, position: null,
            phase: if (position(plan, elapsed).complete or playback.reversed and elapsed == 0) "ended" else playback.phase};
    if (not known) error("chart: unknown playback command")
    else if (command == "seek" and (not util.finite_number(requested) or requested < 0 or requested > plan.length))
        error("chart: seek position must lie within the timeline")
    else {*:next, notification: if (command == "frame") (if (next.phase == "ended" and playback.phase != "ended") "end" else null)
        else if (command == "play") "start" else command}
}
