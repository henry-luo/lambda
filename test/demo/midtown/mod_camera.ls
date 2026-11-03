// Camera rigs that follow the car: a springy chase view, a high wide view, a bumper view and a
// top-down map view. Dragging orbits the chase rigs; the orbit drifts back once the car is moving.
import u: .mod_util

pub let MODES = ["chase", "far", "hood", "top"]
pub let LABELS = {chase: "CHASE", far: "WIDE", hood: "BUMPER", top: "OVERHEAD"}
pub let FOV = {chase: 62.0, far: 55.0, hood: 72.0, top: 50.0}
pub fn next_mode(mode) => (let i = [for (k, m in MODES where m == mode) k][0], MODES[(i + 1) % len(MODES)])

pub fn initial(car, mode = "chase") {
    let rig = desired({mode: mode, orbit: 0.0, tilt: 0.0, look_back: false}, car)
    {mode: mode, position: rig.position, target: rig.target, fov: rig.fov, orbit: 0.0, tilt: 0.0, shake: 0.0, look_back: false,
     anchor: [car.x, car.y + 1.3, car.z]}
}
// where the rig wants to be for this car pose
fn desired(cam, car) {
    let v = sqrt(car.vx * car.vx + car.vz * car.vz)
    // the chase view swings a little toward the direction of travel, so drifts show the car's flank
    let travel = if (v > 3.0) u.heading_of(car.vx, car.vz) else car.heading
    let slip = u.clamp(u.wrap(travel - car.heading), -0.6, 0.6) * 0.45
    let behind = car.heading + slip + cam.orbit + (if (cam.look_back) u.PI else 0.0)
    let f = u.forward(behind)
    let ahead = u.forward(car.heading);
    if (cam.mode == "hood")
        {position: [car.x + ahead[0] * 2.3, car.y + 1.0, car.z + ahead[1] * 2.3],
         target: [car.x + f[0] * 30.0, car.y + 0.9, car.z + f[1] * 30.0], fov: FOV.hood}
    else if (cam.mode == "top")
        {position: [car.x - f[0] * 6.0, car.y + 55.0, car.z - f[1] * 6.0], target: [car.x + ahead[0] * 4.0, car.y, car.z + ahead[1] * 4.0], fov: FOV.top}
    else {
        let far = cam.mode == "far"
        let dist = (if (far) 15.0 else 7.0) + v * (if (far) 0.12 else 0.07)
        let rise = (if (far) 6.5 else 2.3) + v * 0.02 + cam.tilt * dist;
        {position: [car.x - f[0] * dist, car.y + rise, car.z - f[1] * dist],
         target: [car.x + ahead[0] * (2.5 + v * 0.12), car.y + 1.1, car.z + ahead[1] * (2.5 + v * 0.12)],
         fov: FOV[cam.mode]}
    }
}
pub fn follow(cam, car, dt) {
    let rig = desired(cam, car)
    let v = sqrt(car.vx * car.vx + car.vz * car.vz)
    let snap = cam.mode == "hood"
    let kp = if (snap) 1.0 else u.ease(if (cam.mode == "top") 5.0 else 7.5, dt)
    let kt = if (snap) 1.0 else u.ease(12.0, dt)
    // a dragged orbit relaxes once the car moves off
    let relax = if (v > 3.0) u.ease(1.2, dt) else 0.0
    let shake = max(0.0, cam.shake - dt * 2.5)
    let jolt = shake * shake * 0.6
    let pos = [for (k in 0 to 2) u.lerp(cam.position[k], rig.position[k], kp)];
    {*: cam, position: [pos[0] + jolt * sin(car.odometer * 7.1), pos[1] + jolt * sin(car.odometer * 9.3), pos[2] + jolt * cos(car.odometer * 8.7)],
     target: [for (k in 0 to 2) u.lerp(cam.target[k], rig.target[k], kt)],
     fov: rig.fov, orbit: cam.orbit * (1.0 - relax), tilt: cam.tilt * (1.0 - relax), shake: shake,
     anchor: [car.x, car.y + 1.3, car.z]}
}
// a camera body's rotation: yaw then pitch, so its local -Z looks from position at target
pub fn look(position, target) any^ {
    let dx = target[0] - position[0], dy = target[1] - position[1], dz = target[2] - position[2]
    let length = max(sqrt(dx * dx + dy * dy + dz * dz), 0.0001);
    u.quat(math.atan2(0.0 - dx, 0.0 - dz), math.asin(u.clamp(dy / length, -1.0, 1.0)))
}
pub fn jolt(cam, impact) => {*: cam, shake: max(cam.shake, u.clamp(impact / 12.0, 0.0, 1.0))}
pub fn drag(cam, dx, dy) => {*: cam, orbit: cam.orbit + dx, tilt: u.clamp(cam.tilt + dy, -0.15, 0.6)}
// a rig change cuts straight to the new view
pub fn cut_to(cam, car, mode) => (let rig = desired({*: cam, mode: mode, orbit: 0.0, tilt: 0.0}, car),
    {*: cam, mode: mode, position: rig.position, target: rig.target, fov: rig.fov, orbit: 0.0, tilt: 0.0})
