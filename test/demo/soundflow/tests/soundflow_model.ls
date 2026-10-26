import m: ~~.mod_model

// a level stroke across the harp at y=500 crosses all thirteen strings
fn draw(st, y, steps) => reduce([m.begin_stroke(st, 200.0, y, 1000.0), *[for (i in 1 to steps) i]],
    (s, i) => m.extend(s, 200.0 + i * 900.0 / steps, y, 1000.0 + i * 20.0))
fn run(st, from, frames) => reduce([st, *[for (i in 1 to frames) i]], (s, i) => m.tick(s, from + i * 16.0))
let st = m.initial()
let drawn = draw(st, 500.0, 45)
let looped = m.finish(drawn, 1900.0)
let k = looped.strokes[0]
let later = run(m.tick(looped, 0.0), 0.0, 150)
let paused = run(m.tick(m.command(looped, "pause", 0), 0.0), 0.0, 150)
let fast = run(m.tick(m.command(looped, "speed", 4), 0.0), 0.0, 150)
let slow = run(m.tick(m.command(looped, "speed", 0), 0.0), 0.0, 150)
let tapped = m.finish(m.begin_stroke(st, 251.0, 400.0, 0.0), 10.0)
// the top end of the highest harp string, dragged up and off to the side
let grab = m.grab_at(st, 1030.0, 449.0)
let retuned = m.end_drag(m.drag_to(m.begin_drag(st, grab, 1030.0, 449.0), 1100.0, 250.0), 50.0)
let squashed = m.drag_to(m.begin_drag(st, grab, 1030.0, 449.0), 1030.0, 520.0)
let stretched = m.drag_to(m.begin_drag(st, m.grab_at(st, 250.0, 61.0), 250.0, 61.0), 250.0, -500.0)
// the body of the middle harp string (x = 640), slid right and up
let body = m.grab_at(st, 640.0, 500.0)
let slid = m.end_drag(m.drag_to(m.begin_drag(st, body, 640.0, 500.0), 700.0, 300.0), 60.0)
let pushed = m.drag_to(m.begin_drag(st, body, 640.0, 500.0), 5000.0, 500.0)
let plucked = m.end_drag(m.begin_drag(st, body, 640.0, 500.0), 70.0)
let sample = m.command(st, "sample", 0)
{
    pitches: [for (j of st.strings) m.note_name(m.string_note(st, j))],
    grabs: [grab, body, m.grab_at(st, 1030.0, 455.0), m.grab_at(st, 600.0, 500.0)],
    longer_is_lower: m.string_note(retuned, 12) < m.string_note(st, 12),
    end_keeps_line: retuned.strings[12].a.x == 1030.0 and retuned.strings[12].b == st.strings[12].b,
    retune_undo: m.string_note(m.command(retuned, "undo", 0), 12) == m.string_note(st, 12),
    retune_plucks: retuned.hits == [{s: 12, voice: 0, gain: 0.75}],
    shortest: abs(m.length(squashed.strings[12]) - m.LMIN) < 0.001,
    longest: abs(m.length(stretched.strings[0]) - m.LMAX) < 0.001,
    slides_sideways: slid.strings[6] == {a: {x: 700.0, y: st.strings[6].a.y}, b: {x: 700.0, y: st.strings[6].b.y}},
    slide_keeps_pitch: m.string_note(slid, 6) == m.string_note(st, 6) and len(slid.history) == 1,
    slide_stays_in_field: pushed.strings[6].a.x == m.W - 10.0,
    tap_plucks: plucked.hits == [{s: 6, voice: 0, gain: 0.75}] and plucked.history == [] and plucked.strings == st.strings,
    live_plucks: drawn.count,
    crossings: len(k.cross),
    timed: k.dur == 900.0 and k.period == 1350.0,
    first_pass_is_the_drawing: m.phase(k, looped.clock) == k.dur,
    // 2400 ms after the pen lifts: a 450 ms rest, a full 900 ms pass, then two thirds of the next
    replayed: later.count - looped.count,
    paused_silent: paused.count == looped.count and paused.clock == looped.clock,
    // at 2× the same 2400 ms covers 4800 ms of loop; at 0.5× only 1200
    speeds: [m.speed_name(st), m.speed_name(fast), m.speed_name(slow)],
    fast_clock: fast.clock - looped.clock,
    fast_replayed: fast.count - looped.count,
    slow_replayed: slow.count - looped.count,
    speed_steps: [m.command(slow, "slower", 0).speed, m.command(fast, "faster", 0).speed, m.command(fast, "next-speed", 0).speed],
    // a save records real time: at 2× each loop lasts half as long
    export_at_speed: (let plan = m.export_plan(m.command(looped, "speed", 4))[0],
        plan.period == k.period / 2.0 and plan.hits[0].at == k.cross[0].at / 2.0),
    ribbons: [st.ribbons, m.command(st, "ribbons", 0).ribbons, m.command(m.command(st, "ribbons", 0), "ribbons", 0).ribbons],
    tip_moves: m.tip(k, 450.0) == {x: 650.0, y: 500.0},
    tip_rests: m.tip(k, 1000.0) == null,
    tap: tapped.strokes == [] and tapped.hits == [{s: 0, voice: 0, gain: 0.7}],
    voices: [for (v in [0, 1, 2]) m.playable(m.command(st, "voice", v), [{s: 5, voice: v, gain: 1.0}])[0].note],
    scales: [for (i of m.SCALES) m.note_name(m.string_note(m.command(st, "scale", i), 12))],
    layouts: [for (i of m.LAYOUTS) len(m.layout(i))],
    relayout_keeps_strokes: len(m.command(looped, "layout", 1).strokes) == 1,
    sample: len(sample.strokes) == 4 and len(sample.history) == 1,
    clear_undo: len(m.command(m.command(looped, "clear", 0), "undo", 0).strokes) == 1,
    cap: len(reduce([st, *[for (i in 1 to 14) i]], (s, i) => m.finish(draw(s, 300.0 + i * 20.0, 10), 0.0)).strokes) == m.MAX_STROKES,
    export_seconds: m.export_seconds(looped),
    export_hits: len(m.export_plan(looped)[0].hits)
}
