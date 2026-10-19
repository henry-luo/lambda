// Original soundtrack for cityscape.ls, composed as code. 60 BPM, so a beat is one
// second and every cue below is a plain film time. Melodies use D major pentatonic
// (the gong mode); the 1958 chapter turns to its B (yu) mode. The result is a
// performance map for dom.midi_open with the bundled TimGM6mb SoundFont (GM programs).

pub let duration = 150.0

// channels and their General MIDI programs; channel 9 is the GM drum kit.
let PAD = 0, STRINGS = 1, KOTO = 2, FLUTE = 3, HARP = 4, BELL = 5, TIMPANI = 6, BASS = 7
let PIANO = 8, DRUMS = 9, HORN = 10, CHOIR = 11
let setup = [
    {ch: PAD, program: 89, volume: 68, pan: 64}, {ch: STRINGS, program: 48, volume: 88, pan: 64},
    {ch: KOTO, program: 107, volume: 96, pan: 48}, {ch: FLUTE, program: 73, volume: 84, pan: 72},
    {ch: HARP, program: 46, volume: 84, pan: 82}, {ch: BELL, program: 8, volume: 72, pan: 88},
    {ch: TIMPANI, program: 47, volume: 100, pan: 64}, {ch: BASS, program: 43, volume: 84, pan: 60},
    {ch: PIANO, program: 0, volume: 88, pan: 64}, {ch: DRUMS, program: 0, volume: 88, pan: 64},
    {ch: HORN, program: 60, volume: 80, pan: 56}, {ch: CHOIR, program: 52, volume: 76, pan: 64}]
// GM drum keys
let BASS_DRUM = 36, LOW_DRUM = 35, CRASH = 49, CHINA = 52

// ---------------------------------------------------------------- events

// times snap to whole milliseconds: 27.0 + 2 * 0.1 must equal 27.2, or a tie sorted by
// `order` could step backwards and the native player rejects the stream.
fn at(t) => round(t * 1000.0) / 1000.0
// `order` keeps same-time events valid: programs, then controllers, then note-offs, then note-ons.
fn on(t, ch, key, vel) map => {seconds: at(t), status: 144 + ch, a: key, b: vel, order: 3}
fn off(t, ch, key) map => {seconds: at(t), status: 128 + ch, a: key, b: 0, order: 2}
fn cc(t, ch, control, value) map => {seconds: at(t), status: 176 + ch, a: control, b: value, order: 1}
fn program(t, ch, preset) map => {seconds: at(t), status: 192 + ch, a: preset, b: 0, order: 0}
// notes stop just before the end so nothing rings past the performance.
fn note(t, dur, ch, key, vel) array => [on(t, ch, key, vel), off(min(t + dur, duration - 0.05), ch, key)]
fn chord(t, dur, ch, keys, vel) array => [for (k in keys) *note(t, dur, ch, k, vel)]
fn hit(t, key, vel) array => note(t, 1.5, DRUMS, key, vel)
// controller ramp in quarter-second steps (CC11 expression shapes swells and fades).
fn ramp(t0, t1, ch, control, v0, v1) array {
    let n = max(1, int((t1 - t0) / 0.25));
    [for (i in 0 to n) cc(t0 + (t1 - t0) * i / n, ch, control, int(round(v0 + (v1 - v0) * i / n)))]
}
// phrase: [beat offset, key, beats] triples played from `t`.
fn phrase(t, ch, notes, vel) array => [for (n in notes) *note(t + n[0], n[2], ch, n[1], vel)]
fn roll(t, ch, keys, gap, dur, vel) array => [for (i in 0 to (len(keys) - 1)) *note(t + i * gap, dur, ch, keys[i], vel)]

// degree i of D major pentatonic counted from D4 (MIDI 62); negative degrees go down.
let steps = [0, 2, 4, 7, 9]
pub fn penta(i) int {
    let octave = int(floor(i / 5.0));
    62 + 12 * octave + steps[i - octave * 5]
}

// voicings
let D_CHORD = [50, 57, 62, 66], BM_CHORD = [47, 54, 59, 62], G_CHORD = [43, 50, 59, 62]
let A_CHORD = [45, 52, 57, 62], EM_CHORD = [52, 59, 62, 67]
let D_PAD = [50, 57, 64, 66], BM_PAD = [47, 54, 57, 62], G_PAD = [43, 50, 59, 66], A_PAD = [45, 52, 57, 64]
let D2 = 38, A1 = 33, B1 = 35, G1 = 31, E2 = 40, A2 = 45

// a harmony span: chord on strings and pad, root on the bass.
fn harmony(t, dur, strings, pad, root, vel) array =>
    [*chord(t, dur, STRINGS, strings, vel), *chord(t, dur, PAD, pad, vel - 15), *note(t, dur, BASS, root, vel - 10)]

// ---------------------------------------------------------------- chapters

// 0-7.5 title: the pad opens out of silence, bells state the motif.
let title = [*ramp(0.0, 3.0, PAD, 11, 30, 110), *chord(0.0, 8.0, PAD, D_PAD, 70), *note(0.5, 7.5, BASS, D2, 55),
    *hit(0.3, CHINA, 45),
    *phrase(1.5, BELL, [[0.0, 81, 0.7], [0.75, 78, 0.7], [1.5, 76, 0.7], [2.25, 74, 1.5], [3.75, 76, 0.7], [4.5, 81, 1.5]], 62)]

// 7.5-16 flyover: strings enter under a koto ostinato.
fn ostinato(t, beats, keys, vel) array =>
    [for (i in 0 to (beats * 2 - 1)) *note(t + i * 0.5, 0.45, KOTO, keys[i % len(keys)], if (i % 2 == 0) vel + 10 else vel)]
let flyover = [*ramp(7.5, 11.0, STRINGS, 11, 40, 100),
    *harmony(7.5, 4.0, D_CHORD, D_PAD, D2, 70), *harmony(11.5, 2.5, BM_CHORD, BM_PAD, B1, 70),
    *harmony(14.0, 2.0, G_CHORD, G_PAD, G1, 68),
    *ostinato(7.5, 4, [62, 69, 66, 69, 64, 69, 66, 69], 55), *ostinato(11.5, 2, [59, 66, 62, 66], 55),
    *ostinato(13.5, 2, [55, 62, 59, 62], 55)]

// 16-28 Jinsha at night: a gong, a rising harp, the Sun Bird's bells over a B drone.
let jinsha = [*hit(16.0, CHINA, 82), *note(16.0, 2.0, TIMPANI, D2, 105),
    *ramp(16.0, 18.0, PAD, 11, 110, 80), *chord(16.0, 12.0, PAD, [47, 54, 59, 62], 62), *note(16.0, 12.0, BASS, B1, 50),
    *roll(16.5, HARP, [59, 62, 64, 66, 69, 71, 74, 76, 78, 81, 83, 86, 88, 90], 0.143, 1.6, 64),
    *phrase(19.5, BELL, [[0.0, 78, 1.2], [1.5, 86, 1.2], [3.0, 83, 1.2], [4.5, 81, 1.2], [6.0, 78, 1.2], [7.5, 76, 1.2],
        [9.0, 83, 2.0]], 66),
    *note(22.0, 1.5, DRUMS, LOW_DRUM, 50), *ramp(22.0, 27.0, STRINGS, 11, 30, 90),
    *chord(22.0, 5.8, STRINGS, [47, 54, 62], 60)]

// 28-38 today: a timpani roll, then the city grows to a rising koto run.
let present = [*[for (i in 0 to 9) *note(27.0 + i * 0.1, 0.1, TIMPANI, D2, 40 + i * 6)],
    *hit(28.0, CRASH, 100), *note(28.0, 1.5, TIMPANI, D2, 118), *ramp(28.0, 29.0, STRINGS, 11, 90, 115),
    *harmony(28.0, 3.0, [50, 57, 62, 66, 69], D_PAD, D2, 92), *harmony(31.0, 3.0, [43, 50, 59, 62, 67], G_PAD, G1, 86),
    *harmony(34.0, 2.0, A_CHORD, A_PAD, A1, 82), *harmony(36.0, 2.0, D_CHORD, D_PAD, D2, 80),
    *ramp(28.0, 30.0, HORN, 11, 60, 110),
    *phrase(28.0, HORN, [[0.0, 57, 1.0], [1.0, 62, 1.0], [2.0, 66, 2.0], [4.0, 69, 2.5]], 84),
    *[for (k in 0 to 14) *note(28.0 + k * 0.35, 0.5, KOTO, penta(k - 2), 70 + k * 2)]]

// 38-50 Qin: a war-drum pulse; walls rise on a horn chord; the horn carries the theme.
let qin = [*hit(38.0, CHINA, 90), *note(38.0, 1.0, TIMPANI, D2, 110),
    *[for (b in 0 to 11) *note(38.0 + b, 0.4, DRUMS, BASS_DRUM, if (b % 4 == 0) 92 else 66)],
    *[for (b in 0 to 11) *note(38.0 + b, 0.8, TIMPANI, if (b % 2 == 0) D2 else A2, 64)],
    *[for (i in 0 to 23) *note(38.0 + i * 0.5, 0.45, BASS, if (i % 2 == 0) D2 else A2, 58)],
    *chord(38.0, 12.0, STRINGS, [50, 57, 62], 56),
    *ramp(39.0, 40.5, HORN, 11, 40, 112), *chord(39.0, 4.0, HORN, [50, 57, 62], 80),
    *phrase(43.0, HORN, [[0.0, 62, 1.5], [1.5, 64, 0.5], [2.0, 66, 2.0], [4.0, 69, 1.0], [5.0, 66, 1.5]], 82)]

// 50-62 Dujiangyan: harp water flows; a droplet falls with each water slice's pulse.
fn flow(t, beats, base, vel) array {
    let contour = [0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1];
    [for (i in 0 to (beats * 8 - 1)) *note(t + i * 0.125, 0.3, HARP, penta(base + contour[i % 16]), vel + (i % 8))]
}
let water = [*hit(50.0, CHINA, 60), *ramp(50.0, 53.0, STRINGS, 11, 60, 90),
    *harmony(50.0, 4.5, D_CHORD, D_PAD, D2, 58), *harmony(54.5, 4.0, BM_CHORD, BM_PAD, B1, 58),
    *harmony(58.5, 3.5, G_CHORD, G_PAD, G1, 56),
    *flow(50.5, 4, 0, 52), *flow(54.5, 4, -1, 52), *flow(58.5, 3, -2, 50),
    *[for (k in 0 to 7) *note(52.6 + k * 0.9, 0.6, BELL, [93, 90, 88, 86, 83, 81, 78, 76][k], 76)],
    *phrase(56.0, FLUTE, [[0.0, 69, 1.0], [1.0, 71, 0.5], [1.5, 69, 0.5], [2.0, 66, 1.5], [3.5, 64, 0.5], [4.0, 62, 1.75]], 72)]

// 62-74 the Brocade City: a koto song over a quiet pad.
let brocade = [*hit(62.0, CHINA, 55), *roll(62.0, KOTO, [62, 69, 74], 0.05, 1.2, 74),
    *harmony(62.0, 4.0, D_CHORD, D_PAD, D2, 52), *harmony(66.0, 2.0, G_CHORD, G_PAD, G1, 52),
    *harmony(68.0, 2.0, A_CHORD, A_PAD, A1, 52), *harmony(70.0, 4.0, D_CHORD, D_PAD, D2, 50),
    *phrase(63.0, KOTO, [[0.0, 69, 0.9], [1.0, 71, 0.45], [1.5, 74, 0.45], [2.0, 71, 0.9], [3.0, 69, 0.45],
        [3.5, 66, 0.45], [4.0, 64, 1.4], [5.5, 62, 0.45]], 80),
    *phrase(69.0, KOTO, [[0.0, 74, 0.45], [0.5, 76, 0.45], [1.0, 78, 0.9], [2.0, 76, 0.45], [2.5, 74, 0.45],
        [3.0, 71, 0.9], [4.0, 69, 1.9]], 80),
    *[for (i in 0 to 11) *note(62.0 + i, 0.9, HARP, [50, 57, 62][i % 3], 46)]]

// 74-84 the Luo City: a flute line while harp eighths circle like the two rivers.
let luo = [*hit(74.0, CHINA, 50), *note(75.0, 1.5, TIMPANI, D2, 72),
    *harmony(74.0, 4.0, BM_CHORD, BM_PAD, B1, 54), *harmony(78.0, 3.0, G_CHORD, G_PAD, G1, 54),
    *harmony(81.0, 3.0, A_CHORD, A_PAD, A1, 54),
    *[for (i in 0 to 17) *note(74.5 + i * 0.5, 0.45, HARP, [50, 57, 62, 64, 66, 64, 62, 57][i % 8], 50)],
    *phrase(75.0, FLUTE, [[0.0, 66, 1.0], [1.0, 69, 1.0], [2.0, 71, 1.5], [3.5, 69, 0.5], [4.0, 66, 1.0],
        [5.0, 64, 0.5], [5.5, 62, 0.5], [6.0, 64, 2.0], [8.0, 62, 1.5]], 74)]

// 84-96 the Ming and Qing cities: the fullest ensemble yet; two strikes as the inner walls rise.
let mingqing = [*hit(84.0, CHINA, 84), *note(84.0, 1.5, TIMPANI, D2, 100), *ramp(84.0, 88.0, STRINGS, 11, 85, 105),
    *harmony(84.0, 4.0, D_CHORD, D_PAD, D2, 74), *harmony(88.0, 2.0, EM_CHORD, [52, 59, 62, 67], E2, 72),
    *harmony(90.0, 2.0, G_CHORD, G_PAD, G1, 72), *harmony(92.0, 2.0, A_CHORD, A_PAD, A1, 72),
    *harmony(94.0, 2.0, D_CHORD, D_PAD, D2, 70),
    *note(86.0, 1.0, TIMPANI, A2, 88), *note(86.5, 1.0, TIMPANI, D2, 92), *note(86.0, 1.5, BELL, 81, 70), *note(86.5, 1.5, BELL, 86, 70),
    *chord(84.0, 12.0, HORN, [50, 57, 66], 54),
    *phrase(85.0, KOTO, [[0.0, 74, 0.45], [0.5, 76, 0.45], [1.0, 78, 0.9], [2.0, 81, 0.9], [3.0, 78, 0.45],
        [3.5, 76, 0.45], [4.0, 74, 1.4]], 80),
    *phrase(90.0, FLUTE, [[0.0, 69, 1.0], [1.0, 71, 1.0], [2.0, 69, 0.5], [2.5, 66, 0.5], [3.0, 64, 1.0], [4.0, 62, 1.8]], 74)]

// 96-106 1958 and 1968: the ensemble drops out; a solo piano in B as the walls fall.
let demolition = [*ramp(95.6, 96.2, STRINGS, 11, 105, 20),
    *phrase(96.5, PIANO, [[0.0, 59, 1.4], [1.5, 66, 1.4], [3.0, 62, 1.4], [4.5, 64, 1.4], [6.0, 62, 1.4],
        [7.5, 59, 1.4], [8.5, 57, 1.4]], 56),
    *chord(97.5, 3.0, PIANO, [35, 42], 60), *chord(101.5, 3.0, PIANO, [35, 42, 47], 66),
    *note(97.5, 1.5, DRUMS, LOW_DRUM, 50), *note(101.5, 1.5, DRUMS, LOW_DRUM, 58),
    *ramp(97.0, 98.0, PAD, 11, 60, 70), *chord(97.0, 9.0, PAD, [47, 54], 40)]

// 106-124 the ring roads: an ostinato climbs with each growth band; ring roads light on strikes.
let progression = [{strings: D_CHORD, pad: D_PAD, root: D2}, {strings: BM_CHORD, pad: BM_PAD, root: B1},
    {strings: G_CHORD, pad: G_PAD, root: G1}, {strings: A_CHORD, pad: A_PAD, root: A1}]
let growth = [*ramp(107.0, 122.0, STRINGS, 11, 50, 118), *ramp(107.0, 108.0, PAD, 11, 70, 100),
    *[for (k in 0 to 14, let base = int(k / 2) - 2) *[for (j in 0 to 3)
        *note(107.0 + k + j * 0.25, 0.24, KOTO, penta(base + [0, 2, 4, 2][j]), 62 + k * 2)]],
    *[for (c in 0 to 3, let h = progression[c % 4]) *harmony(107.0 + c * 4.0, 4.0, h.strings, h.pad, h.root, 64 + c * 6)],
    *[for (i in 0 to 31, let h = progression[int(i / 8) % 4]) *note(107.0 + i * 0.5, 0.4, BASS, h.root, 62)],
    *note(112.5, 1.2, TIMPANI, A2, 92), *note(112.5, 1.5, BELL, 86, 76), *hit(112.5, CHINA, 72),
    *note(116.0, 1.2, TIMPANI, D2, 100), *note(116.0, 1.5, BELL, 90, 80), *hit(116.0, CRASH, 86),
    *[for (i in 0 to 9) *note(118.5 + i * 0.1, 0.1, TIMPANI, D2, 50 + i * 6)],
    *note(119.5, 2.0, TIMPANI, D2, 122), *hit(119.5, CRASH, 112),
    *ramp(119.5, 120.5, HORN, 11, 60, 115), *chord(119.5, 4.0, HORN, [62, 66, 69], 90),
    *ramp(118.0, 121.0, CHOIR, 11, 30, 110), *chord(118.0, 5.8, CHOIR, [62, 69], 70)]

// 124-134 Shudu Mansion: a horn line climbs with the beam; staccato koto as towers rise.
let shudu = [*hit(124.0, CRASH, 90), *note(124.0, 1.5, TIMPANI, D2, 108),
    *harmony(124.0, 4.0, [50, 57, 62, 66, 69], D_PAD, D2, 88), *harmony(128.0, 2.0, G_CHORD, G_PAD, G1, 80),
    *harmony(130.0, 2.0, A_CHORD, A_PAD, A1, 80), *harmony(132.0, 2.0, BM_CHORD, BM_PAD, B1, 76),
    *phrase(124.5, HORN, [[0.0, 57, 1.0], [1.0, 62, 1.0], [2.0, 66, 1.0], [3.0, 69, 2.5]], 94),
    *[for (i in 0 to 7) *note(129.5 + i * 0.5, 0.3, KOTO, penta(i), 60 + i * 3)]]

// 134-143 the tree rings: full ensemble and choir; a bell for every ring as it appears.
let rings = [*hit(134.0, CRASH, 96), *hit(134.0, CHINA, 70), *note(134.0, 1.5, TIMPANI, D2, 110),
    *harmony(134.0, 3.0, [50, 57, 62, 66, 69, 74], D_PAD, D2, 90), *harmony(137.0, 2.0, BM_CHORD, BM_PAD, B1, 84),
    *harmony(139.0, 2.0, G_CHORD, G_PAD, G1, 84), *harmony(141.0, 2.0, A_CHORD, A_PAD, A1, 82),
    *chord(134.0, 9.0, CHOIR, [62, 66, 69], 80), *chord(134.0, 9.0, HORN, [57, 62], 70),
    *[for (i in 0 to 15) *note(135.0 + i * 0.22, 0.8, BELL, penta(5 + int(i * 0.6)), 70)],
    *phrase(136.0, FLUTE, [[0.0, 69, 1.0], [1.0, 71, 0.5], [1.5, 74, 0.5], [2.0, 78, 1.5], [3.5, 76, 0.5],
        [4.0, 74, 1.0], [5.0, 71, 1.0], [6.0, 69, 1.0]], 82),
    *phrase(136.0, KOTO, [[0.0, 69, 0.9], [1.0, 71, 0.45], [1.5, 74, 0.45], [2.0, 78, 1.4], [3.5, 76, 0.45],
        [4.0, 74, 0.9], [5.0, 71, 0.9], [6.0, 69, 0.9]], 70)]

// 143-150 ending: night again; the title motif returns and the pad fades out.
let ending = [*hit(143.5, CHINA, 68), *ramp(146.0, 149.9, PAD, 11, 110, 20),
    *chord(143.0, 7.0, PAD, D_PAD, 66), *note(143.0, 6.9, BASS, D2, 50),
    *phrase(144.0, BELL, [[0.0, 81, 0.7], [0.75, 78, 0.7], [1.5, 76, 0.7], [2.25, 74, 2.5]], 62),
    *roll(147.0, HARP, [50, 57, 62, 66, 69, 74], 0.12, 2.8, 54),
    *[for (s in setup) cc(149.98, s.ch, 123, 0)]]

let init = [*[for (s in setup) program(0.0, s.ch, s.program)], *[for (s in setup) cc(0.0, s.ch, 7, s.volume)],
    *[for (s in setup) cc(0.0, s.ch, 10, s.pan)], *[for (s in setup) cc(0.0, s.ch, 11, 100)]]

// chapter cues, for documentation and checks: film seconds → what the music does.
pub let cues = [{t: 0.0, cue: "title"}, {t: 7.5, cue: "flyover"}, {t: 16.0, cue: "jinsha"}, {t: 28.0, cue: "today"},
    {t: 38.0, cue: "qin"}, {t: 50.0, cue: "dujiangyan"}, {t: 62.0, cue: "brocade"}, {t: 74.0, cue: "luo"},
    {t: 84.0, cue: "mingqing"}, {t: 96.0, cue: "1958"}, {t: 106.0, cue: "rings roads"}, {t: 124.0, cue: "shudu"},
    {t: 134.0, cue: "tree rings"}, {t: 143.0, cue: "ending"}]

pub fn performance() map {
    let merged = [*init, *title, *flyover, *jinsha, *present, *qin, *water, *brocade, *luo, *mingqing, *demolition,
        *growth, *shudu, *rings, *ending]
    // the native player requires non-decreasing times; `order` breaks ties.
    let sorted = sort(merged, (e) => e.seconds + e.order * 0.000001);
    {kind: "music-performance", seconds: duration,
     events: [for (e in sorted) {seconds: e.seconds, status: e.status, a: e.a, b: e.b}]}
}
