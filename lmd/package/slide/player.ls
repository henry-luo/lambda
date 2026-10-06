import c: .common
import sampler: .sample

pub fn initial_state(plan, options = {}) => {slide: 0, cue: -1, time_ms: 0.0,
    phase: 'waiting', paused: false, anchor_ms: 0.0, anchor_time: 0.0,
    generation: 0, outgoing: -1, reduced_motion: c.value(options.reduced_motion, false)}

fn activate(plan, ps, slide, final) {
    let index = max(0, min(len(plan.slides) - 1, slide))
    let cues = plan.slides[index].cues
    let cue = if (final) len(cues) - 1 else -1
    {*: ps, slide: index, cue: cue, time_ms: if (final and cue >= 0) cues[cue].duration_ms else 0.0,
        phase: 'waiting', paused: false, outgoing: -1, generation: ps.generation + 1}
}

fn begin_cue(plan, ps, index, now_ms) {
    let cue = plan.slides[ps.slide].cues[index]
    let snap = ps.reduced_motion or cue.duration_ms == 0.0
    {*: ps, cue: index, time_ms: if (snap) cue.duration_ms else 0.0,
        phase: if (snap) 'waiting' else 'cue', paused: false,
        anchor_ms: now_ms, anchor_time: 0.0, generation: ps.generation + 1}
}

fn enter(plan, ps, now_ms) {
    let cues = plan.slides[ps.slide].cues
    let next = ps.cue + 1
    if (next < len(cues) and cues[next].start == 'entry') {
        let begun = begin_cue(plan, ps, next, now_ms)
        if (begun.phase == 'waiting') enter(plan, begun, now_ms) else begun
    } else ps
}

fn advance(plan, ps, now_ms) {
    let scene = plan.slides[ps.slide]
    if (ps.phase == 'cue') {*: ps, time_ms: scene.cues[ps.cue].duration_ms, phase: 'waiting', paused: false}
    else if (ps.phase == 'transition') enter(plan, {*: ps, phase: 'waiting', time_ms: 0.0, outgoing: -1}, now_ms)
    else if (ps.cue + 1 < len(scene.cues)) begin_cue(plan, ps, ps.cue + 1, now_ms)
    else if (ps.slide + 1 >= len(plan.slides)) ps
    else {
        let next = activate(plan, ps, ps.slide + 1, false)
        let destination = plan.slides[next.slide]
        if (ps.reduced_motion or destination.transition == 'cut' or destination.transition_duration == 0.0) enter(plan, next, now_ms)
        else {*: next, phase: 'transition', outgoing: ps.slide, anchor_ms: now_ms, anchor_time: 0.0}
    }
}

fn tick(plan, ps, now_ms) {
    if (ps.phase == 'waiting' or ps.paused) ps
    else {
        let duration = if (ps.phase == 'transition') plan.slides[ps.slide].transition_duration
            else plan.slides[ps.slide].cues[ps.cue].duration_ms
        let sample_time = min(duration, max(0.0, ps.anchor_time + now_ms - ps.anchor_ms))
        let next = {*: ps, time_ms: sample_time}
        if (sample_time < duration) next
        else {
            // carry overdue time across automatic entry cues after a missed frame.
            let completed_at = ps.anchor_ms + duration - ps.anchor_time
            let completed = if (ps.phase == 'transition') {*: next, phase: 'waiting', outgoing: -1, time_ms: 0.0}
                else {*: next, phase: 'waiting'}
            let entered = enter(plan, completed, completed_at)
            if (entered.phase != 'waiting') tick(plan, entered, now_ms) else entered
        }
    }
}

pub fn reduce(plan, ps, event) map^ {
    let command = c.as_text(event.command)
    let now_ms = c.value(event.time_ms, ps.anchor_ms)
    let checked_time = if (not c.finite(now_ms)) raise c.fail("player", "invalid frame sample_time")
    if (command == "frame") tick(plan, ps, now_ms)
    else if (command == "play") enter(plan, ps, now_ms)
    else if (command == "next" or command == "skip") advance(plan, ps, now_ms)
    else if (command == "previous") {
        if (ps.phase == 'transition') activate(plan, ps, ps.outgoing, true)
        else if (ps.cue >= 0) {
            let previous = ps.cue - 1
            {*: ps, cue: previous, time_ms: if (previous >= 0) plan.slides[ps.slide].cues[previous].duration_ms else 0.0,
                phase: 'waiting', paused: false, generation: ps.generation + 1}
        } else activate(plan, ps, ps.slide - 1, true)
    } else if (command == "pause") {
        let current = tick(plan, ps, now_ms)
        {*: current, paused: true}
    } else if (command == "resume") {*: ps, paused: false, anchor_ms: now_ms, anchor_time: ps.time_ms}
    else if (command == "restart") enter(plan, activate(plan, ps, ps.slide, false), now_ms)
    else if (command == "home") enter(plan, activate(plan, ps, 0, false), now_ms)
    else if (command == "end") activate(plan, ps, len(plan.slides) - 1, true)
    else if (command == "jump") {
        let address = sampler.address(plan, {slide: event.slide})^
        enter(plan, activate(plan, ps, address.slide, false), now_ms)
    } else if (command == "seek") {
        let address = sampler.address(plan, event.address)^;
        {*: activate(plan, ps, address.slide, false), cue: address.cue, time_ms: address.time_ms}
    } else raise c.fail("player", "unknown command " ++ c.as_text(command))
}

pub fn needs_frame(ps) => ps.phase != 'waiting' and not ps.paused
pub fn sample(plan, ps) array^ => sampler.scene(plan.slides[ps.slide], ps.cue, ps.time_ms)^
