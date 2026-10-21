// The film's soundtrack (vibe/Lambda_Demo_Photo_Album.md §3.4), composed from the film plan: 120 BPM,
// the film's beat, so chords change on shot boundaries and chapter cards. A chapter repeats a
// four-bar progression from its card; quick-cut runs add a driving beat; the credits slow to
// held chords and end on the tonic. The result plays with the music package's bundled SoundFont.
import ~~.mod_midi
import F: .mod_film

let PIANO = 0, PAD = 1, BASS = 2, BELL = 3, DRUMS = 9
let setup = [{ch:PIANO, program:0, volume:92, pan:58}, {ch:PAD, program:89, volume:70, pan:64},
    {ch:BASS, program:33, volume:84, pan:64}, {ch:BELL, program:10, volume:70, pan:76}, {ch:DRUMS, program:0, volume:70, pan:64}]
let KICK = 36, HAT = 42, CRASH = 49

// chords in C major as [root bass note, voicing]; chapters alternate two progressions
let C = [36, [60, 64, 67]], G = [43, [59, 62, 67]], AM = [45, [57, 60, 64]], FM = [41, [57, 60, 65]]
let PROGRESSIONS = [[C, G, AM, FM], [AM, FM, C, G]]

pub let SOUNDFONT = sys.lambda.home# ++ "/package/music/soundfonts/TimGM6mb.sf2"

// a fixed per-note wobble in -3..3, so velocities are never flat yet every rendering is identical
fn wobble(i) => (i * 7919 + 13) % 7 - 3
fn vel(v, i) => max(1, min(127, v + wobble(i)))

// what plays at one beat: the shot covering it, its chapter's chord, and the beat in its bar
fn moment(film, b) {
    let shot = [for (s in film.shots where b >= s.start_beat and b < s.start_beat + s.beats) s][0]
    let k = max(0, min(len(film.chapters) - 1, shot.chapter))
    let from = if (shot.chapter < 0) 0 else film.chapters[k].start_beat
    let bar = int(floor((b - from) / 4))
    {shot:shot, chapter:k, pos:(b - from) % 4, chord:PROGRESSIONS[k % 2][bar % 4]}
}

// notes stop just before the end so nothing rings past the performance
fn note(seconds, at, dur, ch, key, v) => [on(at, ch, key, v), off(min(at + dur, seconds - 0.05), ch, key)]

fn beat_events(film, b, seconds) {
    let m = moment(film, b)
    let beat = F.BEAT_MS / 1000.0
    let t = b * beat
    let half = beat / 2.0
    let kind = m.shot.kind
    let tones = m.chord[1];
    [
        // pad and bass on the bar; the credits and the title hold whole bars
        *(if (m.pos == 0) [for (key in tones) for (e in note(seconds, t, 4.0 * beat - 0.02, PAD, key, vel(54, b + key))) e] else []),
        *(if (m.pos == 0 or (m.pos == 2 and kind != "credits" and kind != "title")) note(seconds, t, 2.0 * beat - 0.05, BASS, m.chord[0], vel(70, b)) else []),
        // piano arpeggio in eighths; held chords in the credits
        *(if (kind == "credits") (if (m.pos == 0) [for (key in tones) for (e in note(seconds, t, 3.8, PIANO, key + 12, vel(56, b + key))) e] else [])
          else [for (i in [0, 1]) for (e in note(seconds, t + half * float(i), half * 1.6, PIANO,
              [tones[0], tones[1], tones[2], tones[1]][(b * 2 + i) % 4] + 12, vel(if (i == 0) 66 else 54, b * 2 + i))) e]),
        // a bell phrase over the title
        *(if (kind == "title" and b % 2 == 0) note(seconds, t, 1.2, BELL, [79, 76, 74][int(floor(b / 2)) % 3], vel(60, b)) else []),
        // a soft cymbal on each chapter card, a kick on the bar once the story is under way
        *(if (kind == "chapter" and b == m.shot.start_beat) note(seconds, t, 2.0, DRUMS, CRASH, vel(52, b)) else []),
        *(if (kind == "quick" or (m.pos == 0 and m.chapter > 0 and kind != "credits" and kind != "chapter")) note(seconds, t, 0.2, DRUMS, KICK, vel(78, b)) else []),
        *(if (kind == "quick") [for (i in [0, 1]) for (e in note(seconds, t + half * float(i), 0.1, DRUMS, HAT, vel(if (i == 0) 58 else 44, b + i))) e] else [])
    ]
}

// the soundtrack as a performance map for dom.midi_open; it lasts exactly as long as the film
pub fn performance(film) map {
    let seconds = film.duration_ms / 1000.0
    let init = [*[for (s in setup) program(0.0, s.ch, s.program)], *[for (s in setup) cc(0.0, s.ch, 7, s.volume)],
        *[for (s in setup) cc(0.0, s.ch, 10, s.pan)]]
    // the closing tonic rings under the end of the credits
    let tail = (let t = seconds - 4.0, [*[for (key in [48, 60, 64, 67, 72]) for (e in [on(t, PAD, key, 50), off(seconds - 0.05, PAD, key)]) e]])
    let body = [for (b in 0 to film.beats - 1, e in beat_events(film, b, seconds)) e]
    performance_of([*init, *body, *tail, *[for (s in setup) cc(seconds - 0.02, s.ch, 123, 0)]], seconds)
}
