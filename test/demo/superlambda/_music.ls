// Original background music for superlambda.ls, composed as code: a bouncy 32-bar
// loop in C major at 150 BPM, in the form A A' B A''. The result is a performance map
// for dom.midi_open with the music package's bundled TimGM6mb SoundFont (GM programs).

import ~~.mod_midi

// 150 BPM: a beat is 0.4 s and a bar is four beats, so the loop lasts 51.2 s.
let BEAT = 0.4
let BAR = 4
pub let duration = 32 * BAR * BEAT

// channels and their General MIDI programs; channel 9 is the GM drum kit.
let LEAD = 0, KEYS = 1, BELL = 2, BASS = 3, DRUMS = 9
let setup = [
    {ch: LEAD, program: 80, volume: 88, pan: 64}, {ch: KEYS, program: 4, volume: 74, pan: 46},
    {ch: BELL, program: 9, volume: 62, pan: 86}, {ch: BASS, program: 38, volume: 100, pan: 64},
    {ch: DRUMS, program: 0, volume: 86, pan: 64}]
// GM drum keys
let KICK = 36, SNARE = 38, HAT = 42, CRASH = 49, RIDE = 51

// ---------------------------------------------------------------- events

// event constructors (on, off, cc, program) live in ../mod_midi.ls; times here are in beats.
// notes stop just before the loop's end so nothing rings past the performance.
fn note(b, d, ch, key, vel) array =>
    [on(b * BEAT, ch, key, vel), off(min((b + d) * BEAT, duration - 0.05), ch, key)]
// a fixed per-note wobble in -3..3, so the line never sounds machine-flat yet always renders the same.
fn wobble(i) => (i * 7919 + 13) % 7 - 3
fn level7(v) => max(1, min(127, int(round(v))))

// a melody of [beat, key, beats] rows from `beat0`, detached slightly; notes on the beat lean in.
fn line(beat0, ch, notes, vel, shift) array =>
    [for (i in 0 to (len(notes) - 1)) *note(beat0 + notes[i][0], notes[i][2] * 0.9, ch, notes[i][1] + shift,
        level7(vel + (if (notes[i][0] == floor(notes[i][0])) 6 else 0) + wobble(i)))]

// ---------------------------------------------------------------- harmony

let C = [60, 64, 67], F = [60, 65, 69], G = [59, 62, 67], AM = [60, 64, 69]
let EM = [59, 64, 67], DM = [62, 65, 69], G7 = [59, 62, 65, 67]
// one chord per bar, with the bass root under it.
let A_CHORDS = [[C, 36], [C, 36], [F, 41], [G, 43], [AM, 45], [F, 41], [G, 43], [C, 36]]
let B_CHORDS = [[F, 41], [G, 43], [EM, 40], [AM, 45], [F, 41], [G, 43], [DM, 38], [G7, 43]]

// A sections bounce: off-beat chord stabs over an octave-jumping bass.
fn stabs(beat0, chords) array =>
    [for (i in 0 to 7) *[for (o in [0.5, 1.5, 2.5, 3.5]) *[for (k in chords[i][0])
        *note(beat0 + i * BAR + o, 0.3, KEYS, k, if (o == 3.5) 54 else 62)]]]
// the B section holds its chords for two beats each.
fn held(beat0, chords) array =>
    [for (i in 0 to 7) *[for (h in [0, 2]) *[for (k in chords[i][0])
        *note(beat0 + i * BAR + h, 1.8, KEYS, k, 56)]]]
// bass patterns are [beat, interval above the root, beats] rows, repeated each bar.
let A_BASS = [[0, 0, 0.8], [1, 7, 0.4], [1.5, 12, 0.4], [2, 0, 0.8], [3, 7, 0.4], [3.5, 12, 0.4]]
let B_BASS = [[0, 0, 1.4], [1.5, 7, 0.4], [2, 12, 0.9], [3, 7, 0.9]]
fn bassline(beat0, chords, pattern, vel) array =>
    [for (i in 0 to 7) *[for (p in pattern)
        *note(beat0 + i * BAR + p[0], p[2], BASS, chords[i][1] + p[1], level7(vel + (if (p[0] == 0) 8 else 0)))]]

// ---------------------------------------------------------------- drums

fn hit(b, d, key, vel) array => note(b, d, DRUMS, key, vel)
// a bar of groove: kick on 1 and 3 (and the "and" of 3), snare on the backbeat, eighth cymbals.
fn groove(b, cymbal) array => [*hit(b, 0.25, KICK, 104), *hit(b + 2, 0.25, KICK, 96), *hit(b + 2.5, 0.25, KICK, 78),
    *hit(b + 1, 0.5, SNARE, 90), *hit(b + 3, 0.5, SNARE, 94),
    *[for (e in 0 to 7) *hit(b + e * 0.5, 0.25, cymbal, if (e % 2 == 0) 62 else 44)]]
// a rising snare roll that hands the B section back to the tune.
fn drum_fill(b) array => [*hit(b, 0.25, KICK, 104), *hit(b + 1, 0.5, SNARE, 90), *hit(b + 1.5, 0.25, KICK, 88),
    *[for (s in 0 to 7) *hit(b + 2 + s * 0.25, 0.25, SNARE, 70 + s * 6)]]
// eight bars that open on a crash; `turn` ends them with the fill.
fn drums(beat0, cymbal, turn) array => [*hit(beat0, 1.5, CRASH, 92),
    *[for (i in 0 to 7) *(if (turn and i == 7) drum_fill(beat0 + i * BAR) else groove(beat0 + i * BAR, cymbal))]]

// ---------------------------------------------------------------- tune

// the A head (bars 1-6), then two endings: CLOSE settles on C, RISE climbs into the B section.
let A_HEAD = [[0, 67, 0.5], [0.5, 72, 0.5], [1, 76, 0.5], [1.5, 79, 1], [2.5, 76, 0.5], [3, 79, 1],
    [4, 81, 0.5], [4.5, 79, 0.5], [5, 76, 0.5], [5.5, 72, 1], [6.5, 74, 0.5], [7, 76, 1],
    [8, 77, 0.5], [8.5, 81, 0.5], [9, 84, 1], [10, 81, 0.5], [10.5, 77, 0.5], [11, 81, 1],
    [12, 79, 1.5], [13.5, 77, 0.5], [14, 76, 0.5], [14.5, 74, 0.5], [15, 71, 1],
    [16, 72, 0.5], [16.5, 76, 0.5], [17, 81, 1], [18, 79, 0.5], [18.5, 76, 0.5], [19, 72, 1],
    [20, 77, 0.5], [20.5, 76, 0.5], [21, 77, 0.5], [21.5, 81, 1], [22.5, 79, 0.5], [23, 77, 1]]
let A_CLOSE = [[24, 76, 0.5], [24.5, 77, 0.5], [25, 79, 1], [26, 74, 0.5], [26.5, 76, 0.5], [27, 77, 0.5],
    [27.5, 74, 0.5], [28, 72, 1.5], [29.5, 67, 0.5], [30, 72, 1]]
let A_RISE = [[24, 79, 0.5], [24.5, 81, 0.5], [25, 83, 1], [26, 86, 0.5], [26.5, 83, 0.5], [27, 79, 1],
    [28, 84, 2], [30, 79, 0.5], [30.5, 76, 0.5], [31, 72, 1]]
// B sings longer notes, climbing to D6 before the G7 turnaround.
let B_TUNE = [[0, 81, 1.5], [1.5, 79, 0.5], [2, 77, 1], [3, 72, 1],
    [4, 74, 0.5], [4.5, 76, 0.5], [5, 77, 0.5], [5.5, 79, 1.5], [7, 71, 1],
    [8, 79, 1.5], [9.5, 77, 0.5], [10, 76, 1], [11, 71, 1],
    [12, 72, 0.5], [12.5, 74, 0.5], [13, 76, 0.5], [13.5, 81, 2.5],
    [16, 81, 1], [17, 84, 1], [18, 81, 0.5], [18.5, 79, 0.5], [19, 77, 1],
    [20, 79, 1], [21, 83, 1], [22, 86, 1], [23, 83, 1],
    [24, 81, 0.5], [24.5, 79, 0.5], [25, 77, 0.5], [25.5, 76, 0.5], [26, 74, 1], [27, 77, 1],
    [28, 79, 0.5], [28.5, 77, 0.5], [29, 76, 0.5], [29.5, 74, 0.5], [30, 71, 0.5], [30.5, 74, 0.5], [31, 79, 1]]
// glockenspiel sparkles answer the held notes of B.
let B_SPARKLE = [[14, 76, 0.5], [14.5, 81, 0.5], [15, 84, 0.5], [15.5, 88, 0.5],
    [30, 74, 0.5], [30.5, 79, 0.5], [31, 83, 0.5], [31.5, 86, 0.5]]

// an A section; `bell` doubles the tune an octave up for the last pass.
fn a_section(beat0, ending, bell) array => [*line(beat0, LEAD, [*A_HEAD, *ending], 100, 0),
    *(if (bell) line(beat0, BELL, [*A_HEAD, *ending], 66, 12) else []),
    *stabs(beat0, A_CHORDS), *bassline(beat0, A_CHORDS, A_BASS, 92), *drums(beat0, HAT, false)]
fn b_section(beat0) array => [*line(beat0, LEAD, B_TUNE, 96, 0), *line(beat0, BELL, B_SPARKLE, 70, 0),
    *held(beat0, B_CHORDS), *bassline(beat0, B_CHORDS, B_BASS, 88), *drums(beat0, RIDE, true)]

let init = [*[for (s in setup) program(0.0, s.ch, s.program)], *[for (s in setup) cc(0.0, s.ch, 7, s.volume)],
    *[for (s in setup) cc(0.0, s.ch, 10, s.pan)], *[for (s in setup) cc(0.0, s.ch, 11, 100)]]
// all notes off at the very end, so a loop restart starts clean.
let finish = [for (s in setup) cc(duration - 0.02, s.ch, 123, 0)]

// sections start every 32 beats: A at 0, A' at 32, B at 64, A'' at 96.
pub fn performance() map => performance_of([*init, *a_section(0, A_CLOSE, false), *a_section(32, A_RISE, false),
    *b_section(64), *a_section(96, A_CLOSE, true), *finish], duration)
