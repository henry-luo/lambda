import rig: ~~.~~.demo.scene3d.skymaster.mod_rig
import cam: ~~.~~.demo.scene3d.skymaster.mod_camera
import mesh: ~~.~~.demo.scene3d.skymaster.mod_mesh

fn near(a, b) => abs(a - b) < 0.000001
fn same(a, b) => len(a) == len(b) and all([for (i, x in a) near(x, b[i])])
fn finite_mesh(m) => len(m.p) == len(m.n) and len(m.i) % 3 == 0 and
    all([for (i in m.i) i >= 0 and i < len(m.p)])

let land = rig.SEQUENCES.landing
let take = rig.SEQUENCES.takeoff
let down = rig.pose(0.0, 0.0, 0.0)
let folded = rig.pose(rig.PI, 0.0, 0.0)
let middle = rig.pose(rig.PI / 2.0, 0.03, 2.0)
let touch = rig.moment(land, 4.6)
let roll = rig.moment(land, 6.0)
let airborne = rig.moment(take, 4.0)
let times = rig.sample_times(land)
let tracks = rig.tracks(land)
let hero = cam.preset("hero")
let portrait = cam.screen(cam.frame(hero), {width: 390.0, height: 400.0}, hero.target)
let tri = {p: [[1.0, 0.0, 2.0], [0.0, 1.0, 2.0], [0.0, 0.0, 2.0]],
    n: [[0.0, 0.0, 1.0], [0.0, 0.0, 1.0], [0.0, 0.0, 1.0]], i: [0, 1, 2]}
{
    landing_endpoints: near(rig.moment(land, 0.0).gear, rig.PI) and near(rig.moment(land, land.duration).gear, 0.0),
    takeoff_endpoints: near(rig.moment(take, 0.0).gear, 0.0) and near(rig.moment(take, take.duration).gear, rig.PI),
    clamped_time: near(rig.moment(land, -1.0).sec, 0.0) and near(rig.moment(take, 100.0).sec, take.duration),
    touchdown: near(touch.height, 0.0) and near(touch.flex, 0.0) and near(touch.roll, 0.0),
    rollout: roll.flex > 0.0 and roll.roll < 0.0 and near(roll.height, 0.0),
    climb: airborne.height > 0.0 and airborne.roll < 0.0,
    rack_pitch: near(rig.STROKE, 0.2010619298) and near(rig.place(folded.rack, [0.0, 0.0, 0.0])[0], -rig.STROKE),
    fixed_pivots: same(rig.place(middle.'side-r', rig.JOINT), rig.JOINT) and same(rig.place(folded.'side-r', rig.ROOT), rig.ROOT),
    down_pose: same(rig.place(down.'wheel-r', rig.WHEEL), rig.WHEEL),
    mirrored_legs: same(rig.place(middle.'wheel-l', rig.mirror_point(rig.WHEEL)), rig.mirror_point(rig.place(middle.'wheel-r', rig.WHEEL))),
    folded_inward: abs(rig.place(folded.'wheel-r', rig.WHEEL)[2]) < abs(rig.WHEEL[2]) and rig.place(folded.'wheel-r', rig.WHEEL)[1] > rig.WHEEL[1],
    ground_contact: near(rig.contact(0.0), rig.WHEEL[1] - rig.TIRE) and rig.contact(0.05) > rig.contact(0.0),
    tick_seek: near(rig.seek_time(2.123, 1.0 / 120.0, 7.0), 255.0 / 120.0) and near(rig.seek_time(50.0, 1.0 / 120.0, 7.0), 7.0),
    configured_tick: near(rig.seek_time(1.07, 0.02, 7.0), 1.08),
    clip_shape: len(times) == 211 and near(times[0], 0.0) and near(times[len(times) - 1], 7.0) and len(tracks) == 20 and
        all([for (tr in tracks) len(tr.values) == len(times) * (if (tr.kind == 'quaternion') 4 else 3)]),
    camera_endpoints: same(cam.position(cam.blend(hero, cam.preset("front"), 0.0)), cam.position(hero)) and
        same(cam.position(cam.blend(hero, cam.preset("front"), 1.0)), cam.position(cam.preset("front"))),
    camera_limits: near(cam.zoom(hero, 100.0).dist, 7.0) and near(cam.orbit(hero, 0.0, 100.0).pitch, 1.48),
    portrait_projection: same(portrait, [195.0, 200.0]),
    mirrored_winding: mesh.mirrored(tri).i == [0, 2, 1] and same(mesh.mirrored(tri).n[0], [0.0, 0.0, -1.0]),
    buffer_shapes: finite_mesh(mesh.cylinder([0.0, 0.0, 0.0], [0.0, 1.0, 0.0], 0.1, 8)) and
        finite_mesh(mesh.torus([0.0, 0.0, 0.0], [0.0, 0.0, 1.0], 0.2, 0.05, 0.04, 8, 6))
}
