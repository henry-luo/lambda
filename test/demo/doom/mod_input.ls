// Held controls and edges are values owned by the session (S9.1.7).
pub fn empty() => {keys: [], held: [], edges: [], mouse_x: 0.0, mouse_y: 0.0}
pub fn command(key, camera_mode = "player") {
    let k = lower(key)
    if (camera_mode != "player" and k == "q") "camera_left"
    else if (camera_mode != "player" and k == "e") "camera_right"
    else if (camera_mode != "player" and k == "r") "zoom_in"
    else if (camera_mode != "player" and k == "f") "zoom_out"
    else if (contains(["w", "arrowup"], k)) "forward"
    else if (contains(["s", "arrowdown"], k)) "back"
    else if (k == "a") "strafe_left"
    else if (k == "d") "strafe_right"
    else if (k == "arrowleft") "turn_left"
    else if (k == "arrowright") "turn_right"
    else if (k == "shift") "run"
    else if (contains(["control", "ctrl", "mouse1"], k)) "fire"
    else if (contains([" ", "space", "e"], k)) "use"
    else if (contains(["escape", "p"], k)) "pause"
    else if (k == "r") "restart"
    else if (k == "tab") "spectator"
    else if (contains(["1", "2", "3", "4", "5", "6"], k)) "weapon_" ++ k
    else ""
}
pub fn set_key(controls, key, down, camera_mode = "player") {
    let action = command(key, camera_mode)
    let physical = lower(key)
    let was_down = contains(controls.keys, physical)
    let keys = if (down) unique([*controls.keys, physical])
        else [for (held in controls.keys where held != physical) held]
    if (action == "" or was_down == down) controls
    // Releasing one alias must leave a second physical key's action held.
    else {*: controls, keys: keys, held: unique([for (held in keys) command(held, camera_mode)]),
        edges: if (down and not contains(controls.held, action)) [*controls.edges, action] else controls.edges}
}
pub fn consume(controls) => {*: controls, edges: [], mouse_x: 0.0, mouse_y: 0.0}
pub fn axis(controls, positive, negative) =>
    (if (contains(controls.held, positive)) 1 else 0) - (if (contains(controls.held, negative)) 1 else 0)
pub fn mouse(controls, dx, dy) => {*: controls, mouse_x: controls.mouse_x + dx, mouse_y: controls.mouse_y + dy}
