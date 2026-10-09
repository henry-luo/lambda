// Spectator distances and angles follow cssDOOM spectator.css. Controls are
// normalized to 60 Hz reference motion, independent of presentation delivery.
import controls: .mod_input
import geo: .mod_geometry

pub fn initial(mode) => {offset_x: 0.0, offset_y: 0.0, height: if (mode == "follow") 300 else 3000, angle: 0.0}
pub fn cycle(game) {
    let mode = if (game.spectator == "player") "follow" else if (game.spectator == "follow") "top" else "player"
    {*: game, spectator: mode, camera_control: initial(mode), input: controls.empty()}
}
pub fn pose(game) {
    let control = game.camera_control
    if (game.spectator == "player") {x: game.player.x, y: game.player.y, z: game.player.z,
        angle: game.player.angle, pitch: game.player.pitch, offset_y: 0}
    else if (game.spectator == "follow") {
        let forward = geo.forward(game.player.angle)
        {x: game.player.x - forward.x * control.height * 0.7,
            y: game.player.y - forward.y * control.height * 0.7,
            z: game.player.floor + control.height, angle: game.player.angle, pitch: -55 * math.pi / 180, offset_y: 0.1}
    } else {x: game.player.x + control.offset_x, y: game.player.y + control.offset_y,
        z: control.height, angle: -control.angle, pitch: -70 * math.pi / 180, offset_y: 0}
}
pub fn advance(game, input_state, dt) {
    if (game.spectator == "player") game else {
        let control = game.camera_control
        let frames = dt * 60
        let speed = control.height * 0.02 * frames
        let along = if (game.spectator == "top") controls.axis(input_state, "forward", "back") else 0
        let across = if (game.spectator == "top") controls.axis(input_state, "strafe_right", "strafe_left") else 0
        let forward = geo.forward(control.angle), right = geo.right(control.angle)
        let zoom = controls.axis(input_state, "zoom_out", "zoom_in")
        {*: game, camera_control: {*: control,
            offset_x: control.offset_x + (forward.x * along + right.x * across) * speed,
            offset_y: control.offset_y + (forward.y * along + right.y * across) * speed,
            angle: control.angle + controls.axis(input_state, "camera_right", "camera_left") * 0.03 * frames,
            height: max(if (game.spectator == "follow") 100 else 200,
                control.height * (1 + zoom * 0.02) ** frames)}}
    }
}
pub fn wheel(game, dy) => if (game.spectator == "player") game else {*: game,
    camera_control: {*: game.camera_control, height: max(200, game.camera_control.height + dy * 2)}}
pub fn drag(game, dx, dy, viewport_height = 336) {
    let control = game.camera_control
    let scale = control.height / viewport_height * 2
    let right = geo.right(control.angle), forward = geo.forward(control.angle)
    if (game.spectator != "top") game else {*: game, camera_control: {*: control,
        offset_x: control.offset_x - (dx * right.x + dy * right.y) * scale,
        offset_y: control.offset_y + (dx * right.y - dy * forward.y) * scale}}
}
