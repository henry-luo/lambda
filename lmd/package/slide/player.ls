import c: .common
import sampler: .sample
import clock: .clock
import transitions: .transitions

pub fn initial_state(plan, options = {}) => {slide: c.value(options.start_slide,0), cue: -1, time_ms: 0.0,
    phase: 'waiting', paused: false, anchor_ms: 0.0, anchor_time: 0.0,
    triggered:[], outgoing_triggered:[], generation: 0, outgoing: -1, reduced_motion: c.value(options.reduced_motion, false),
    autoplay: false, timed: false, after_ms: null, wait_ms: 0.0, playback_rate: 1.0,
    autoplay_dwell_ms: c.value(options.autoplay_dwell_ms, c.value(plan.autoplay_dwell_ms, 3000.0))}

fn activate(plan, ps, slide, final) {
    let index = max(0, min(len(plan.slides) - 1, slide))
    let cues = plan.slides[index].cues
    let cue = if (final) len(cues) - 1 else -1
    {*: ps, slide: index, cue: cue, time_ms: if (final and cue >= 0) cues[cue].duration_ms else 0.0,
        phase: 'waiting', triggered:[], outgoing_triggered:[], paused: false, outgoing: -1, timed:false, after_ms:null, wait_ms:0.0, generation: ps.generation + 1}
}

fn begin_cue(plan, ps, index, now_ms) {
    let cue = plan.slides[ps.slide].cues[index]
    let snap = ps.reduced_motion or cue.duration_ms == 0.0
    {*: ps, cue: index, time_ms: if (snap) cue.duration_ms else 0.0,
        phase: if (snap) 'waiting' else 'cue', paused: false, timed:false,
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
        if (ps.reduced_motion or not transitions.animated(destination)) enter(plan, next, now_ms)
        else {*: next, phase: 'transition', outgoing: ps.slide, outgoing_triggered:c.value(ps.triggered,[]), anchor_ms: now_ms, anchor_time: 0.0}
    }
}

fn finished(plan, ps) => ps.phase == 'waiting' and ps.slide == len(plan.slides) - 1 and
    ps.cue == len(plan.slides[ps.slide].cues) - 1

fn settle(plan, ps, now_ms) {
    let entered = if (ps.phase == 'waiting') enter(plan, ps, now_ms) else ps
    let scene = plan.slides[entered.slide]
    let timed = entered.phase == 'waiting' and entered.cue == len(scene.cues)-1 and
        scene.advance_after != null and entered.slide+1 < len(plan.slides) and
        not any([for (entry in c.value(entered.triggered,[])) not entry.done]);
    if (entered.phase != 'waiting') entered
    else if (finished(plan,entered)) {*:entered,autoplay:false,timed:false}
    else if (not entered.autoplay and not timed) entered
    // authored After timing starts once all builds finish; Play still supplies cue dwell.
    else {*:entered,timed:timed,after_ms:scene.advance_after,wait_ms:0.0,anchor_ms:now_ms,anchor_time:0.0,
        generation:entered.generation+1}
}

fn resume(ps, now_ms) => {*: ps, paused: false, anchor_ms: now_ms,
    anchor_time: if (ps.phase == 'waiting' and (ps.autoplay or ps.timed)) ps.wait_ms else ps.time_ms}

fn stop_autoplay(ps) => {*: ps, autoplay: false, timed:false}

fn tick_main(plan, ps, now_ms) {
    if (ps.paused or (ps.phase == 'waiting' and not (ps.autoplay or ps.timed))) ps
    else {
        let waiting = ps.phase == 'waiting'
        let duration = if (waiting) (if (ps.timed) ps.after_ms else ps.autoplay_dwell_ms)
            else if (ps.phase == 'transition') plan.slides[ps.slide].transition_duration
            else plan.slides[ps.slide].cues[ps.cue].duration_ms
        let sample_time = min(duration, max(0.0, ps.anchor_time + (now_ms - ps.anchor_ms) * ps.playback_rate))
        let next = if (waiting) {*: ps, wait_ms: sample_time} else {*: ps, time_ms: sample_time}
        if (sample_time < duration) next
        else {
            // carry overdue time across automatic entry cues after a missed frame.
            let completed_at = ps.anchor_ms + (duration - ps.anchor_time) / ps.playback_rate
            let completed = if (waiting) advance(plan, next, completed_at)
                else if (ps.phase == 'transition') {*: next, phase: 'waiting', outgoing: -1, time_ms: 0.0}
                else {*: next, phase: 'waiting'}
            let entered = settle(plan, completed, completed_at)
            if (needs_frame(entered)) tick_main(plan, entered, now_ms) else entered
        }
    }
}

fn trigger_tick(plan,ps,now_ms) => {*:ps,triggered:[for (entry in c.value(ps.triggered,[])) {
    let cue = [for (cue in plan.slides[ps.slide].triggers where cue.id == entry.id) cue][0]
    let elapsed = if (ps.paused or entry.done) entry.time_ms else min(cue.duration_ms,max(0.0,entry.anchor_time+(now_ms-entry.anchor_ms)*ps.playback_rate));
    {*:entry,time_ms:elapsed,done:elapsed >= cue.duration_ms}
}]}
fn tick(plan,ps,now_ms) {
    let current = trigger_tick(plan,ps,now_ms);
    let sampled = tick_main(plan,current,now_ms)
    let trigger_finished = any([for (entry in c.value(ps.triggered,[])) not entry.done]) and
        not any([for (entry in c.value(sampled.triggered,[])) not entry.done]);
    if (trigger_finished and sampled.phase == 'waiting' and not sampled.paused) settle(plan,sampled,now_ms) else sampled
}
fn anchor_triggers(ps,now_ms) => {*:ps,triggered:[for (entry in c.value(ps.triggered,[])) {*:entry,anchor_ms:now_ms,anchor_time:entry.time_ms}]}

pub fn reduce(plan, ps, event) map^ {
    let command = c.as_text(event.command)
    let now_ms = c.value(event.time_ms, ps.anchor_ms)
    let checked_time = if (not c.finite(now_ms)) raise c.fail("player", "invalid frame sample_time")
    let checked_dwell = if ((command == "play" or ps.autoplay) and
        (not c.finite(ps.autoplay_dwell_ms) or ps.autoplay_dwell_ms <= 0.0))
        raise c.fail("player", "autoplay_dwell_ms must be positive and finite")
    if (command == "frame") tick(plan, ps, now_ms)
    else if (command == "trigger") {
        let current = resume(anchor_triggers(tick(plan,ps,now_ms),now_ms),now_ms)
        let matching = [for (cue in plan.slides[current.slide].triggers where cue.trigger == c.as_text(event.target)) cue]
        let checked_target = if (len(matching)==0) raise c.fail("player","unknown animation trigger")
        let ids = [for (cue in matching) cue.id];
        {*:current,paused:false,autoplay:false,timed:false,generation:current.generation+1,
            triggered:[*[for (entry in current.triggered where not contains(ids,entry.id)) entry],
                *[for (cue in matching) {id:cue.id,time_ms:if (current.reduced_motion) cue.duration_ms else 0.0,
                    done:current.reduced_motion or cue.duration_ms==0.0,anchor_ms:now_ms,anchor_time:0.0}]]}
    }
    else if (command == "speed") {
        let checked_rate = if (not c.finite(event.rate) or event.rate <= 0.0)
            raise c.fail("player", "playback rate must be positive and finite")
        // sample on the old clock before reanchoring, preserving progress and pause.
        let current = anchor_triggers(tick(plan, ps, now_ms),now_ms)
        {*: current, playback_rate: event.rate, anchor_ms: now_ms,
            anchor_time: if (current.phase == 'waiting' and (current.autoplay or current.timed)) current.wait_ms else current.time_ms,
            generation: current.generation + 1}
    }
    else if (command == "activate") settle(plan, ps, now_ms)
    else if (command == "play") {
        let current = if (finished(plan, ps)) activate(plan, ps, 0, false) else ps
        let playing = {*: current, autoplay: true}
        if (current.paused and (current.phase != 'waiting' or current.autoplay or current.timed)) resume(anchor_triggers(playing,now_ms), now_ms)
        else if (current.autoplay or current.timed or current.phase != 'waiting') playing
        else settle(plan, {*: playing, paused: false}, now_ms)
    }
    else if (command == "next" or command == "skip") settle(plan,advance(plan,stop_autoplay(anchor_triggers(trigger_tick(plan,ps,now_ms),now_ms)),now_ms),now_ms)
    else if (command == "previous") {
        if (ps.phase == 'transition') settle(plan,activate(plan,stop_autoplay(ps),ps.outgoing,true),now_ms)
        else if (ps.cue >= 0) {
            let previous = ps.cue - 1
            {*: ps, cue: previous, time_ms: if (previous >= 0) plan.slides[ps.slide].cues[previous].duration_ms else 0.0,
                phase: 'waiting', triggered:[], outgoing_triggered:[], paused: false, autoplay: false, timed:false,after_ms:null,wait_ms:0.0,generation: ps.generation + 1}
        } else settle(plan,activate(plan,stop_autoplay(ps),ps.slide-1,true),now_ms)
    } else if (command == "stop") {*:ps,paused:true,autoplay:false,timed:false,generation:ps.generation+1}
    else if (command == "pause") {
        let current = tick(plan, ps, now_ms)
        {*: current, paused: true}
    } else if (command == "resume") resume(anchor_triggers(ps,now_ms), now_ms)
    else if (command == "restart" or command == "home" or command == "restart-slide")
        settle(plan,activate(plan,stop_autoplay(ps),if (command == "restart-slide") ps.slide else 0,false),now_ms)
    else if (command == "end") activate(plan, stop_autoplay(ps), len(plan.slides) - 1, true)
    else if (command == "jump") {
        let address = sampler.address(plan, {slide: event.slide})^
        settle(plan,activate(plan,stop_autoplay(ps),address.slide,false),now_ms)
    } else if (command == "seek") {
        let address = sampler.address(plan, event.address)^;
        {*: activate(plan, stop_autoplay(ps), address.slide, false), cue: address.cue, time_ms: address.time_ms}
    } else if (command == "seek_time") clock.seek(plan, ps, event.position_ms, now_ms)^
    else raise c.fail("player", "unknown command " ++ c.as_text(command))
}

pub fn needs_frame(ps) => (ps.phase != 'waiting' or ps.autoplay or ps.timed or any([for (entry in c.value(ps.triggered,[])) not entry.done])) and not ps.paused
pub fn sample(plan, ps) array^ => sampler.triggered(plan.slides[ps.slide],sampler.scene(plan.slides[ps.slide], ps.cue, ps.time_ms),c.value(ps.triggered,[]))
