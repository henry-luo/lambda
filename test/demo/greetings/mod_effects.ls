// Saved effects use catalog keys, so old card files and unknown keys stay still and silent.
pub let ANIMATIONS = [
    {key: "none", name: "Still", hint: "A classic printed card."},
    {key: "confetti", name: "Confetti", hint: "Colourful confetti and bobbing art."},
    {key: "snow", name: "Snowfall", hint: "Snowflakes drifting past the card."},
    {key: "hearts", name: "Floating hearts", hint: "A little love in the air."},
    {key: "twinkle", name: "Starlight", hint: "A field of softly twinkling stars."},
    {key: "float", name: "Gentle float", hint: "Art floating like a lullaby."},
    {key: "sway", name: "Garden breeze", hint: "Flowers and leaves swaying gently."}]
pub let MUSIC = [
    {key: "none", name: "No music", hint: "Keep this card quiet."},
    {key: "celebrate", name: "A little celebration", hint: "Bright, playful music box · 12 s"},
    {key: "winter", name: "Winter lights", hint: "Soft holiday bells · 16 s"},
    {key: "affection", name: "With all my heart", hint: "A warm, lilting melody · 16 s"},
    {key: "lullaby", name: "Sleepy little star", hint: "A gentle bedtime tune · 20 s"},
    {key: "sunshine", name: "Pocketful of sunshine", hint: "A cheerful plucked melody · 12 s"}]

pub fn animation(key) => (let hit = [for (a in ANIMATIONS where a.key == key) a], if (len(hit) == 0) ANIMATIONS[0] else hit[0])
pub fn music(key) => (let hit = [for (m in MUSIC where m.key == key) m], if (len(hit) == 0) MUSIC[0] else hit[0])
pub fn of(card) => {animation: animation(card.effects.animation).key, music: music(card.effects.music).key}
pub fn configure(card, field, key) => {*:card, effects: {*:of(card), [field]: if (field == "animation") animation(key).key else music(key).key}}
