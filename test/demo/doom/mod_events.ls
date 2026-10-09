// Simulation events are plain values; native effects run in the view handler.
pub fn with_events(game, events) {
    let first_id = game.next_effect_id or 0
    {*: game, effects: [*game.effects, *[for (i, event in events) {*: event, id: first_id + i}]],
        next_effect_id: first_id + len(events)}
}
pub fn visual_lifetime(kind) => if (kind == "puff") 0.2 else if (kind == "explosion") 0.3
    else if (kind == "fog") 1.714 else 0
pub fn sound(game, sound_name, position = null) => with_events(game,
    [{kind: "sound", sound: sound_name, position: position}])
pub fn damage(target, amount, source) => {kind: "damage", target: target, amount: amount, source: source}
