// Source IO stays in Lambda; native handles own decoded bytes (D7.1.2v2).
import dom
pub let MAX_VOICES = 32
pub fn initial(generation) => {generation: generation, seen_id: -1, voices: []}
pub fn pending(game, seen_id) => [for (event in game.effects where event.kind == "sound" and event.id > seen_id)
    {id: event.id, sound: event.sound, volume: 1.0}]
pub fn paused(mode) => mode == "ready" or mode == "paused"
pub pn close(owner, audio) {
    for (voice in audio.voices) dom.audio_close(owner, voice.token)
}
pub pn synchronize(owner, previous, game, base) any^ {
    var current = previous
    if (current == null or current.generation != game.generation) {
        if (current != null) close(owner, current)
        current = initial(game.generation)
    }
    var voices = []
    for (voice in current.voices) {
        let status = dom.audio_state(owner, voice.token)
        if (status == "error" or status == null) {
            close(owner, current)
            raise error("DOOM audio failed: " ++ voice.sound)
        }
        if (status == "ended") { dom.audio_close(owner, voice.token); continue }
        if (paused(game.mode)) dom.audio_pause(owner, voice.token)
        if (not paused(game.mode) and status == "paused" and not dom.audio_play(owner, voice.token, voice.volume)) {
            close(owner, current)
            raise error("DOOM audio resume failed: " ++ voice.sound)
        }
        voices = [*voices, voice]
    }
    for (event in pending(game, current.seen_id)) {
        // The voice budget is application policy; steal the oldest active cue.
        if (len(voices) >= MAX_VOICES) {
            dom.audio_close(owner, voices[0].token)
            voices = [for (i in 1 to (len(voices) - 1)) voices[i]]
        }
        // A later IO failure must also release voices opened earlier this turn.
        let bytes = input(url_resolve(base, "assets/sounds/" ++ event.sound ++ ".wav"), 'binary') ^ {
            close(owner, {*: current, voices: voices})
            raise ^
        }
        let token = dom.audio_open(owner, bytes)
        if (token == null) { close(owner, {*: current, voices: voices}); raise error("DOOM audio decode failed: " ++ event.sound) }
        if (not dom.audio_play(owner, token, event.volume)) {
            dom.audio_close(owner, token)
            close(owner, {*: current, voices: voices})
            raise error("DOOM audio playback failed: " ++ event.sound)
        }
        if (paused(game.mode)) dom.audio_pause(owner, token)
        voices = [*voices, {token: token, sound: event.sound, volume: event.volume}]
    }
    return {*: current, seen_id: max(current.seen_id, (game.next_effect_id or 0) - 1), voices: voices}
}
