import audio: ~~.mod_audio
import events: ~~.mod_events
let game = {generation: 3, next_effect_id: 20, effects: []}
let emitted = events.sound(events.with_events(game, [{kind: "damage", amount: 7}]), "DSPISTOL", {x: 10, y: 20})
let duplicate = events.sound(emitted, "DSPISTOL")
{
    initial: audio.initial(3),
    ordered: audio.pending(duplicate, 20),
    consumed: audio.pending(duplicate, 22),
    pause: [for (mode in ["ready", "playing", "paused", "dead", "won"]) audio.paused(mode)],
    budget: audio.MAX_VOICES
}
