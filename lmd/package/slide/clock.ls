// The deck clock: autoplay laid out as one timeline, so a host can show and seek deck time.
// Autoplay runs each slide as [transition] entry cues, then a dwell before every click cue
// and before the next slide (player.ls); the last slide ends with its final cue.
import c: .common

// segments of one slide in autoplay order: {kind: 'transition' | 'cue' | 'dwell', cue, ms};
// `cue` is the cue a segment plays, or for a dwell the cue completed before it
fn segments(plan, index, reduced, dwell_ms) {
    let scene = plan.slides[index]
    let transition = if (index == 0 or reduced or scene.transition == 'cut' or scene.transition_duration == 0.0) []
        else [{kind: 'transition', cue: -1, ms: scene.transition_duration}]
    let cues = [for (j, cue in scene.cues) for (seg in [
        *(if (cue.start == 'click') [{kind: 'dwell', cue: j - 1, ms: dwell_ms}] else []),
        {kind: 'cue', cue: j, ms: if (reduced) 0.0 else cue.duration_ms}]) seg]
    let hold = if (index + 1 < len(plan.slides)) [{kind: 'dwell', cue: len(scene.cues) - 1, ms: dwell_ms}] else [];
    [*transition, *cues, *hold]
}

fn total(segs) => sum([for (s in segs) s.ms])

// one slide's segments with their start times on the deck clock
fn laid_out(plan, index, reduced, dwell_ms, start_ms) {
    let segs = segments(plan, index, reduced, dwell_ms);
    [for (k, s in segs) {*: s, start_ms: start_ms + total(slice(segs, 0, k))}]
}

// {duration_ms, slides: [{id, start_ms, end_ms}]}; reduced motion snaps cues and skips transitions
pub fn timeline(plan, options = {}) map {
    let reduced = c.value(options.reduced_motion, false)
    let dwell = c.value(options.autoplay_dwell_ms, plan.autoplay_dwell_ms)
    let lengths = [for (i, scene in plan.slides) total(segments(plan, i, reduced, dwell))]
    let starts = [for (i, n in lengths) sum(slice(lengths, 0, i))];
    {duration_ms: sum(lengths), slides: [for (i, scene in plan.slides) {id: scene.id, start_ms: starts[i], end_ms: starts[i] + lengths[i]}]}
}

fn slide_layout(plan, ps, index) {
    let line = timeline(plan, ps)
    laid_out(plan, index, ps.reduced_motion, ps.autoplay_dwell_ms, line.slides[index].start_ms)
}

// the deck time a player state shows: its segment's start plus the time sampled within it
pub fn position(plan, ps) float {
    let segs = slide_layout(plan, ps, ps.slide)
    let start = timeline(plan, ps).slides[ps.slide].start_ms
    let cue_end = [for (s in segs where s.kind == 'cue' and s.cue == ps.cue) s.start_ms + s.ms]
    let dwell = [for (s in segs where s.kind == 'dwell' and s.cue == ps.cue) s]
    let transition = [for (s in segs where s.kind == 'transition') s];
    if (ps.phase == 'transition') start + ps.time_ms
    else if (ps.phase == 'cue') [for (s in segs where s.kind == 'cue' and s.cue == ps.cue) s.start_ms][0] + ps.time_ms
    else if (ps.autoplay and len(dwell) > 0) dwell[0].start_ms + ps.wait_ms
    else if (len(cue_end) > 0) cue_end[0]
    // parked before any cue: after the incoming transition
    else start + (if (len(transition) > 0) transition[0].ms else 0.0)
}

// the player state at deck time `position_ms`, keeping autoplay, pause, rate and dwell
pub fn seek(plan, ps, position_ms, now_ms) map^ {
    let checked = if (not c.finite(position_ms)) raise c.fail("player", "seek position must be finite")
    let line = timeline(plan, ps)
    let at = max(0.0, min(line.duration_ms, float(position_ms)))
    let final_slide = len(plan.slides) - 1
    let index = [for (i, s in line.slides where at >= s.start_ms and (at < s.end_ms or i == final_slide)) i][0]
    let segs = laid_out(plan, index, ps.reduced_motion, ps.autoplay_dwell_ms, line.slides[index].start_ms)
    let cues = plan.slides[index].cues
    // a seek to the very end parks on the final build, as finished autoplay does
    let found = [for (s in segs where at >= s.start_ms and at < s.start_ms + s.ms) s]
    let seg = if (len(found) > 0) found[0] else {kind: 'dwell', cue: len(cues) - 1, ms: 0.0, start_ms: at}
    let offset = at - seg.start_ms
    let done_ms = if (seg.cue >= 0) cues[seg.cue].duration_ms else 0.0
    let finished = len(found) == 0 and index == final_slide
    let base = {*: ps, slide: index, outgoing: -1, generation: ps.generation + 1, anchor_ms: float(now_ms),
        autoplay: ps.autoplay and not finished};
    if (seg.kind == 'transition') {*: base, cue: -1, phase: 'transition', outgoing: index - 1, time_ms: offset, anchor_time: offset}
    else if (seg.kind == 'cue') {*: base, cue: seg.cue, phase: 'cue', time_ms: offset, anchor_time: offset}
    else {*: base, cue: seg.cue, phase: 'waiting', time_ms: done_ms, wait_ms: offset,
        anchor_time: if (base.autoplay) offset else done_ms}
}
