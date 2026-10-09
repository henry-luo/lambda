import fixture: .mod_fixture
import camera: ~~.mod_camera
import controls: ~~.mod_input
import sim: ~~.mod_sim
let level = fixture.level()
let game = fixture.game(level)
let follow = camera.cycle(game)
let top = camera.cycle(follow)
let pan_keys = controls.set_key(controls.empty(), "w", true, "top")
let moved = camera.advance(top, pan_keys, 1 / 60)
let panned = sim.step(fixture.map_data(level), top, pan_keys, 1 / 60, fixture.rules)
let dragged = camera.drag(top, 10, 20, 600)
let zoomed = camera.advance(follow, controls.set_key(controls.empty(), "r", true, "follow"), 1 / 60);
[
    [follow.spectator, camera.pose(follow).x, camera.pose(follow).y, camera.pose(follow).z,
        round(camera.pose(follow).pitch * 180 / math.pi)],
    [top.spectator, camera.pose(top).z, round(camera.pose(top).pitch * 180 / math.pi)],
    [moved.camera_control.offset_x, moved.camera_control.offset_y, panned.player.y],
    [dragged.camera_control.offset_x, dragged.camera_control.offset_y],
    [zoomed.camera_control.height, camera.wheel(top, -2000).camera_control.height],
    [controls.command("r"), controls.command("r", "top"), controls.command("e", "top")],
    camera.cycle(top).spectator
]
