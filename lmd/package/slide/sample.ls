import c: .common
import ease: .easing
import motion: .motion
import color: .color
import keyframes: .keyframes

fn track_value(track, time_ms) {
    let elapsed = time_ms - track.begin_ms
    let total = track.duration_ms * track.repeat
    let t = if (elapsed < 0.0) 0.0 else if (track.duration_ms == 0.0 or elapsed >= total) 1.0
        else (elapsed % track.duration_ms) / track.duration_ms
    if (track.channel == 'visible') {
        if (track.mode == 'enter') (if (elapsed >= 0.0) 1.0 else 0.0)
        else (if (elapsed >= total) 0.0 else 1.0)
    } else {
        let eased = ease.sample(track.easing, t)
        let factor = if (track.mode == 'pulse') 1.0 - abs(2.0 * eased - 1.0) else eased
        if (track.mode == 'motion') {
            let position = motion.sample(track.motion, eased)
            if (track.channel == 'tx') position.x else if (track.channel == 'ty') position.y else position.angle
        } else if (track.mode == 'keyframes') keyframes.value(track.stops, track.channel, eased)
        else if (track.mode == 'color') color.sample(track.a, track.b, factor)
        else ease.lerp(track.a, track.b, factor)
    }
}

pub fn track(track, time_ms) {
    let sampled = track_value(track, time_ms)
    if (track.channel == 'paint') color.css(sampled) else sampled
}

pub fn baseline(obj) => {id: obj.id, dom_key: obj.dom_key, opacity: obj.opacity, visible: 1.0,
    tx: 0.0, ty: 0.0, sx: 1.0, sy: 1.0, rotation: 0.0, clip: 0.0, clip_direction: 'bottom', paint: obj.paint, paint_rgba: null}

fn assign(visual, track, value) => {*: visual,
    *: map([string(track.channel), if (track.channel == 'paint') color.css(value) else value]),
    paint_rgba: if (track.channel == 'paint') value else visual.paint_rgba,
    clip_direction: if (track.channel == 'clip') c.value(track.direction, track.mode) else visual.clip_direction}

// the first writer establishes backwards fill; later cues do not rewrite history.
fn initial(tracks, index, visual, seen) {
    if (index >= len(tracks)) visual
    else {
        let tr = tracks[index]
        let first = not contains(seen, tr.channel)
        let next = if (first and (tr.entrance or tr.mode == 'keyframes')) assign(visual, tr, tr.a) else visual
        initial(tracks, index + 1, next, [*seen, tr.channel])
    }
}

fn apply_tracks(tracks, time_ms, index, visual) {
    if (index >= len(tracks)) visual
    else {
        let tr = tracks[index]
        let next = if (time_ms >= tr.begin_ms) assign(visual, tr, track_value(tr, time_ms)) else visual
        apply_tracks(tracks, time_ms, index + 1, next)
    }
}

fn apply_cues(cues, cue_index, time_ms, index, visual) {
    if (index >= len(cues) or index > cue_index) visual
    else {
        let cue = cues[index]
        let sample_time = if (index < cue_index) cue.duration_ms else time_ms
        apply_cues(cues, cue_index, time_ms, index + 1, apply_tracks(cue.tracks, sample_time, 0, visual))
    }
}

pub fn prepare_object(scene, obj) {
    let tracks = [for (cue in scene.cues) for (tr in cue.tracks where tr.target == obj.id) tr]
    {baseline: initial(tracks, 0, baseline(obj), []),
        cues: [for (cue in scene.cues) {*: cue, tracks: [for (tr in cue.tracks where tr.target == obj.id) tr]}]}
}

fn prepared_object(prepared, cue_index, time_ms) =>
    apply_cues(prepared.cues, cue_index, time_ms, 0, prepared.baseline)

pub fn sample_object(scene, obj, cue_index, time_ms) {
    let found = [for (prepared in c.value(scene.prepared_targets, []) where prepared.baseline.id == obj.id) prepared]
    prepared_object(if (len(found) > 0) found[0] else prepare_object(scene, obj), cue_index, time_ms)
}

pub fn scene(scene, cue_index, time_ms) => if (scene.prepared_targets != null)
    [for (prepared in scene.prepared_targets) prepared_object(prepared, cue_index, time_ms)]
    else [for (obj in scene.targets) sample_object(scene, obj, cue_index, time_ms)]

pub fn address(plan, address) map^ {
    let checked_address = if (not (address is map)) raise c.fail("address", "expected map")
    let checked_attrs = c.attributes(address, ["slide", "cue", "time_ms"], "address")^;
    let slide = c.value(address.slide, 0)
    let found = [for (i, scene in plan.slides where i == slide or scene.id == slide) i]
    let guard_1 = if (len(found) != 1) raise c.fail("address", "unknown slide")
    let scene = plan.slides[found[0]]
    let requested = c.value(address.cue, 'initial')
    let named = c.as_text(requested)
    let cue = if (named == "initial") -1 else if (named == "final") len(scene.cues) - 1
        else {
            let matches = [for (i, cue in scene.cues where i == requested or cue.id == requested) i]
            let guard_2 = if (len(matches) != 1) raise c.fail("address", "unknown cue")
            matches[0]
        }
    let sample_time = c.value(address.time_ms, if (named == "final" and cue >= 0) scene.cues[cue].duration_ms else 0.0)
    let guard_3 = if (not c.finite(sample_time) or sample_time < 0.0) raise c.fail("address", "invalid time_ms")
    {slide: found[0], cue: cue, time_ms: float(sample_time)}
}
