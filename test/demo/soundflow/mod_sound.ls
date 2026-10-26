// Live playback: every pluck opens its note's WAV as a native audio voice
// (D7.5.4). Tones are synthesized on first use and kept; `warm` fills the
// cache a tone per frame so a new layout rarely waits on synthesis.
import dom
import synth: .mod_synth

pub let MAX_VOICES = 14
// more plucks than this in one frame would only add mud
let MAX_PER_FRAME = 6

pub fn initial() => {enabled: true, failed: false, voices: [], tones: {}, played: 0, last: ""}
pub fn status(sound) => if (sound.failed) "unavailable" else if (sound.enabled) "on" else "off"
fn key_of(n) => n.voice ++ ":" ++ string(n.note)

pub pn silence(owner, sound) {
    for (token in sound.voices) dom.audio_close(owner, token)
    return {*: sound, voices: []}
}
pub pn toggle(owner, sound) {
    let quiet = silence(owner, sound)
    return {*: quiet, enabled: not sound.enabled, failed: false}
}

pn cached(sound, n) {
    let k = key_of(n)
    if (sound.tones[k] != null) { return sound }
    return {*: sound, tones: {*: sound.tones, [k]: synth.tone(n.voice, n.note)}}
}

// synthesizes at most one missing tone of `notes`
pub pn warm(sound, notes) {
    let missing = [for (n in notes where sound.tones[key_of(n)] == null) n]
    if (len(missing) == 0) { return sound }
    return cached(sound, missing[0])
}

pn play_one(owner, sound, n) {
    let ready = cached(sound, n)
    var voices = []
    for (token in ready.voices) {
        if (dom.audio_state(owner, token) == "playing") { voices = [*voices, token]; continue }
        dom.audio_close(owner, token)
    }
    // keep overlapping tails, but bound the native players when many loops run
    if (len(voices) >= MAX_VOICES) {
        dom.audio_close(owner, voices[0])
        voices = slice(voices, 1, len(voices))
    }
    let token = dom.audio_open(owner, ready.tones[key_of(n)])
    if (token == null or not dom.audio_play(owner, token, min(1.0, n.gain))) {
        if (token != null) dom.audio_close(owner, token)
        return {*: silence(owner, {*: ready, voices: voices}), failed: true}
    }
    return {*: ready, voices: [*voices, token], played: ready.played + 1, last: key_of(n)}
}

// plays notes [{voice, note, gain}]; a muted or failed engine stays silent
pub pn play(owner, sound, notes) {
    if (not sound.enabled or sound.failed or len(notes) == 0) { return sound }
    var out = sound
    for (n in take(notes, MAX_PER_FRAME)) {
        out = play_one(owner, out, n)
        if (out.failed) { return out }
    }
    return out
}
