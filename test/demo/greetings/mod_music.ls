// Native audio is document-owned and starts only in preview (D7.5.4).
import dom
import E: .mod_effects

pub fn initial() => {token: null, track: "none", muted: false, failed: false, played: 0}
pub fn status(sound) => if (sound.failed) "unavailable" else if (sound.muted) "muted"
    else if (sound.token != null) "playing" else "off"
pub fn source(base, key) string^ => url_resolve(base, "assets/music/" ++ E.music(key).key ++ ".wav")^

pub pn schedule(owner) {
    dom.set_attribute(owner, "data-frame", string(dom.request_frame(owner, "greetingframe")))
    return null
}
pub pn stop(owner, sound) {
    let frame = dom.get_attribute(owner, "data-frame")
    if (frame != null) dom.cancel_frame(owner, int(frame))
    dom.set_attribute(owner, "data-running", "false")
    if (sound.token != null) dom.audio_close(owner, sound.token)
    return {*:sound, token: null, track: "none"}
}
pub pn follow(owner, sound, card, preview, base) {
    let track = E.of(card).music
    if (preview == null or sound.muted or track == "none") { return stop(owner, sound) }
    if (sound.token != null and sound.track == track) { return sound }
    let quiet = stop(owner, sound)
    let bytes = (input(source(base, track)^, 'binary')^) ^ { null }
    let token = if (bytes == null) null else dom.audio_open(owner, bytes)
    if (token == null or not dom.audio_play(owner, token, .45)) {
        if (token != null) dom.audio_close(owner, token)
        return {*:quiet, failed: true}
    }
    dom.set_attribute(owner, "data-running", "true")
    schedule(owner)
    return {*:quiet, token: token, track: track, failed: false, played: sound.played + 1}
}
pub pn tick(owner, sound) {
    if (sound.token == null) { return sound }
    let playback = dom.audio_state(owner, sound.token)
    if (playback == "playing") { return sound }
    // Reuse one decoded voice at the end of the tune; a failed voice is closed, never retried per frame.
    if (playback == "ended" and dom.audio_play(owner, sound.token, .45)) {
        return {*:sound, played: sound.played + 1}
    }
    return {*:stop(owner, sound), failed: true}
}
