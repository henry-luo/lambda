// The deck clock: timeline layout, position while autoplaying, and seek_time round trips.
import slide: lambda.slide

let deck = <presentation
    <slide id: "a", <text id: "title", "A"> <text id: "detail", "Detail">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 400.0>>
        <cue <effect target: "detail", kind: 'fade-in', duration: 200.0>>>
    <slide id: "b", transition: 'fade', transition_duration: 300.0, <text id: "title", "B">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 500.0>>>
    <slide id: "c", <text id: "end", "C">
        <cue start: 'entry', <effect target: "end", kind: 'fade-in', duration: 250.0>>>>
let options = {autoplay_dwell_ms: 1000.0}
let plan = slide.compile(deck, options)^
fn control(ps, event) => slide.reduce(plan, ps, event)^

// autoplay from the start, sampled every 50 ms: the clock must read the elapsed time
fn run(ps, t, end) => if (t > end) [] else (
    let next = control(ps, {command: 'frame', time_ms: t}),
    [{t: t, at: slide.position(plan, next), slide: next.slide, phase: next.phase, playing: next.autoplay}, *run(next, t + 50.0, end)])
let started = control(slide.initial_state(plan), {command: 'play', time_ms: 0.0})
let samples = run(started, 0.0, 5000.0)
let line = slide.timeline(deck, options)^
fn r(v) => round(v * 1000.0) / 1000.0

// a seek lands where asked, keeps playing, and the clock continues from there
fn seeked(ps, at, when) => control(ps, {command: 'seek_time', position_ms: at, time_ms: when})
let points = [0.0, 200.0, 400.0, 900.0, 1450.0, 1500.0, 2650.0, 2800.0, 3400.0, 4500.0, 4650.0, 4700.0, 9000.0]
let paused = control(started, {command: 'pause', time_ms: 0.0})
let mid = seeked(started, 1450.0, 10000.0)
let later = control(mid, {command: 'frame', time_ms: 10100.0});
{
    timeline: line,
    tracks_elapsed: all([for (s in samples where s.playing) abs(s.at - s.t) < 0.001]),
    ends_at_duration: [for (s in samples where not s.playing) r(s.at)][0] == line.duration_ms,
    phases: [for (s in samples where s.t == 0.0 or s.t == 600.0 or s.t == 1450.0 or s.t == 2650.0 or s.t == 3000.0 or s.t == 4400.0)
        [s.t, s.slide, s.phase]],
    round_trip: [for (p in points) r(slide.position(plan, seeked(started, p, 50.0)))],
    states: [for (p in points) (let s = seeked(started, p, 50.0), [s.slide, s.cue, s.phase, s.autoplay])],
    paused_stays: [seeked(paused, 2800.0, 70.0).paused, r(slide.position(plan, seeked(paused, 2800.0, 70.0)))],
    continues: [r(slide.position(plan, mid)), r(slide.position(plan, later)), later.slide, later.phase],
    reduced: slide.timeline(deck, {*: options, reduced_motion: true})^
}
