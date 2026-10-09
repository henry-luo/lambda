// Switching and firing share simulation deadlines; CSS owns sheet sampling.
import controls: .mod_input
import pickups: .mod_pickups

pub fn changed(game, slot) => if (slot == game.player.weapon) game else {*: game, shot_at: null,
    player: {*: game.player, weapon: slot, weapon_from: game.player.weapon, switch_at: game.time}}
pub fn switching(game) => game.player.switch_at != null and game.time - game.player.switch_at < 0.4
pub fn displayed_slot(game) => if (switching(game) and game.time - game.player.switch_at < 0.2)
    game.player.weapon_from else game.player.weapon
pub fn pose(game, rules, images) {
    let rule = rules.WEAPONS[string(game.player.weapon)]
    let sheet = images.weapons[rules.WEAPONS[string(displayed_slot(game))].name]
    let firing = game.shot_at != null and game.time < game.player.next_fire and not switching(game)
    let moving = controls.axis(game.input, "forward", "back") != 0 or
        controls.axis(game.input, "strafe_right", "strafe_left") != 0
    let hidden = game.mode == "dead" or game.spectator != "player"
    {sheet: sheet, firing: firing, switching: switching(game), hidden: hidden,
        animation: if (hidden) "none" else if (switching(game)) "doom-weapon-switch .4s ease-in-out both"
            else if (firing) (if (rule.continuous) "doom-weapon-fire .3s steps(2) infinite"
                else "doom-weapon-fire " ++ string(rule.fireRate / 1000) ++ "s steps(" ++ string(sheet.frames - 1) ++ ") infinite")
            else if (moving) "doom-weapon-bob 1.4s linear infinite" else "none",
        opacity: if (pickups.active(game.player, "invisibility", game.time)) 0.3 else 1,
        play_state: if (game.mode == "playing") "running" else "paused"}
}
pub fn face_row(health) => if (health >= 80) 0 else if (health >= 60) 1 else if (health >= 40) 2 else if (health >= 20) 3 else 4
pub fn viewport_filter(game) => if (pickups.active(game.player, "radsuit", game.time))
    "sepia(.3) hue-rotate(60deg) brightness(1.1)"
    else if (pickups.active(game.player, "invulnerability", game.time)) "saturate(0) brightness(1.5) sepia(1) hue-rotate(90deg)"
    else if (pickups.active(game.player, "berserk", game.time)) "saturate(1.3)" else "none"
