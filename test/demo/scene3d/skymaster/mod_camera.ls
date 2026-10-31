// Camera presets, orbit/zoom, eased transitions and projection into the stage's CSS pixels.
// A camera is {yaw, pitch, dist, target, fov}; yaw 0 looks from starboard, so forward reads to the right.
import c: lambda.scene3d.camera
import rig: .mod_rig

pub let VIEWS = [
    {id: "hero", label: "HERO", yaw: 0.62, pitch: 0.36, dist: 4.6, target: [0.0, -0.32, 0.0], fov: 32.0},
    {id: "front", label: "FRONT", yaw: 1.5707963, pitch: 0.12, dist: 4.4, target: [0.0, -0.3, 0.0], fov: 32.0},
    {id: "plan", label: "PLAN", yaw: 0.0, pitch: 1.45, dist: 4.8, target: [0.0, -0.2, 0.0], fov: 32.0},
    {id: "top", label: "TOP ¾", yaw: -0.78, pitch: 0.8, dist: 4.6, target: [0.0, -0.25, 0.0], fov: 32.0},
    {id: "gearbox", label: "GEARBOX", yaw: 1.22, pitch: 0.1, dist: 1.6, target: [0.0, -0.11, 0.0], fov: 30.0},
    {id: "tube", label: "SIDE TUBE", yaw: 0.95, pitch: 0.32, dist: 1.8, target: [-0.06, -0.2, 0.33], fov: 30.0}]
pub fn preset(id) => [for (v in VIEWS where v.id == id) {yaw: v.yaw, pitch: v.pitch, dist: v.dist, target: v.target, fov: v.fov}][0]

pub fn position(cam) => rig.add(cam.target, [cam.dist * cos(cam.pitch) * sin(cam.yaw), cam.dist * sin(cam.pitch),
    cam.dist * cos(cam.pitch) * cos(cam.yaw)])
// the shorter way round between two headings
fn turn_toward(from, to) => (let d = to - from, from + d - 2.0 * rig.PI * round(d / (2.0 * rig.PI)))
pub fn blend(a, b, u) => (let k = rig.ease(u), {yaw: a.yaw + (turn_toward(a.yaw, b.yaw) - a.yaw) * k,
    pitch: a.pitch + (b.pitch - a.pitch) * k, dist: a.dist + (b.dist - a.dist) * k, fov: a.fov + (b.fov - a.fov) * k,
    target: rig.add(a.target, rig.scale(rig.sub(b.target, a.target), k))})
pub fn orbit(cam, yaw, pitch) => {*: cam, yaw: cam.yaw + yaw, pitch: rig.clamp(cam.pitch + pitch, -0.15, 1.48)}
pub fn zoom(cam, factor) => {*: cam, dist: rig.clamp(cam.dist * factor, 0.7, 7.0)}

// the scene's viewBox is 1280 × 720, sliced to cover the stage: project there, then scale and centre
pub fn frame(cam) => c.prepare({position: position(cam), target: cam.target, fov: cam.fov, near: 0.05, far: 80.0}, 1280.0, 720.0)
pub fn screen(prepared, stage, point) {
    let k = max(stage.width / 1280.0, stage.height / 720.0)
    let p = c.project(prepared, point);
    if (p.x == null) null else [(stage.width - 1280.0 * k) / 2.0 + p.x * k, (stage.height - 720.0 * k) / 2.0 + p.y * k]
}
