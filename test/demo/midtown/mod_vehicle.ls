// Arcade car dynamics for the player's car: an automatic five-speed gearbox, speed-sensitive steering,
// tyre grip that bleeds sideways velocity (less under the handbrake, so the car drifts), curbs, ramps
// and airtime, and box collisions resolved by separating axes. Pure: step() returns the next car.
import u: .mod_util
import city: .mod_city

pub let SPEC = {length: 4.5, width: 1.86, wheelbase: 2.7, track: 1.58, wheel_r: 0.36,
    gears: [13.0, 22.0, 31.0, 40.0, 49.0],    // top speed of each gear, m/s
    pull: [9.0, 7.4, 5.9, 4.6, 3.6],          // full-throttle acceleration in each gear, m/s²
    reverse_max: 10.0, reverse_pull: 5.0, brake: 15.0, roll: 0.3, drag: 0.0012, engine_brake: 1.2,
    steer_max: 0.6, steer_rate: 2.6, steer_return: 4.5, grip: 11.0, lateral_damp: 8.0, drift_damp: 1.3,
    shift_time: 0.22, idle_rpm: 850.0, red_rpm: 7000.0, step_up: 0.5, gravity: 9.81}
pub let HALF_LENGTH = SPEC.length / 2.0
pub let HALF_WIDTH = SPEC.width / 2.0

pub fn spawn(x, z, heading) => {x: x, y: city.ground(x, z), z: z, heading: heading, vx: 0.0, vz: 0.0, vy: 0.0,
    steer: 0.0, gear: 1, shift: 0.0, rpm: SPEC.idle_rpm, grounded: true, air: 0.0, pitch: 0.0, roll: 0.0,
    spin: 0.0, accel: 0.0, impact: 0.0, landed: 0.0, airtime: 0.0, hits: [], damage: 0.0, odometer: 0.0}
pub let IDLE = {throttle: 0.0, brake: 0.0, steer: 0.0, handbrake: false}

pub fn forward_speed(car) => car.vx * sin(car.heading) + car.vz * cos(car.heading)
// steering lock shrinks with speed
pub fn steer_lock(speed) => SPEC.steer_max / (1.0 + abs(speed) / 15.0)
pub fn speed(car) => sqrt(car.vx * car.vx + car.vz * car.vz)
// R while rolling backwards, otherwise the engaged forward gear
pub fn gear_label(car) any^ => if (forward_speed(car) < -0.3) "R" else if (speed(car) < 0.3 and car.rpm < SPEC.idle_rpm + 50.0) "N" else string(car.gear)

// ---- oriented boxes and the separating-axis test (2D, in x/z)
pub fn box(x: any, z: any, heading: any, half_length: any, half_width: any) => {x: x, z: z, f: u.forward(heading), r: u.right(heading), hl: half_length, hw: half_width}
pub fn car_box(car) => box(car.x, car.z, car.heading, HALF_LENGTH, HALF_WIDTH)
pub fn aabb(o) => {x: (o[0] + o[2]) / 2.0, z: (o[1] + o[3]) / 2.0, f: [0.0, 1.0], r: [-1.0, 0.0], hl: (o[3] - o[1]) / 2.0, hw: (o[2] - o[0]) / 2.0}
fn radius(b, a) => b.hl * abs(b.f[0] * a[0] + b.f[1] * a[1]) + b.hw * abs(b.r[0] * a[0] + b.r[1] * a[1])
// the minimum translation that pushes a out of b, or null when they are apart
pub fn separate(a, b) {
    let dx = a.x - b.x, dz = a.z - b.z
    let tests = [for (axis in [a.f, a.r, b.f, b.r], let d = dx * axis[0] + dz * axis[1])
        {n: if (d < 0.0) [0.0 - axis[0], 0.0 - axis[1]] else axis, depth: radius(a, axis) + radius(b, axis) - abs(d)}]
    let least = reduce(tests, (m, t) => if (t.depth < m.depth) t else m);
    if (least.depth <= 0.0) null else least
}

// thrust the car out of one obstacle and reflect the closing part of its velocity
fn collide(car, other: any, id: any) any^ {
    let m = separate(car_box(car), other)
    if (m == null) car
    else {
        let vn = car.vx * m.n[0] + car.vz * m.n[1]
        let closing = min(0.0, vn)
        // a glancing hit keeps most of the sliding speed; a square hit bounces back a little
        let vx = (car.vx - 1.3 * closing * m.n[0]) * (if (closing < 0.0) 0.92 else 1.0)
        let vz = (car.vz - 1.3 * closing * m.n[1]) * (if (closing < 0.0) 0.92 else 1.0);
        {*: car, x: car.x + m.n[0] * (m.depth + 0.001), z: car.z + m.n[1] * (m.depth + 0.001), vx: vx, vz: vz,
         impact: max(car.impact, 0.0 - closing), hits: if (id == null) car.hits else [*car.hits, id]}
    }
}
fn collide_all(car, boxes, i) any^ => if (i >= len(boxes)) car
    else collide_all(collide(car, boxes[i].box, boxes[i].id), boxes, i + 1)
// world boxes tall enough to meet the car, plus the traffic around it
pub fn obstacles_near(car, traffic) any^ => [
    *[for (o in city.near_obstacles(car.x, car.z, 4.0)^ where o[4] > car.y + 0.25) {box: aabb(o), id: null}],
    *[for (t in traffic where u.dist2(t.x, t.z, car.x, car.z)^ < 64.0) {box: box(t.x, t.z, t.heading, t.hl, t.hw), id: t.id}]]

// ---- one integration step of at most 1/60 s
fn gearbox(car, vf, throttle, dt) {
    let g = car.gear
    let up = g < 5 and throttle > 0.0 and vf > SPEC.gears[g - 1] * 0.96
    let down = g > 1 and vf < SPEC.gears[g - 2] * 0.72
    let gear = if (up) g + 1 else if (down) g - 1 else g
    let lo = if (gear == 1) 0.0 else SPEC.gears[gear - 2] * 0.62
    let ratio = u.clamp((abs(vf) - lo) / (SPEC.gears[gear - 1] - lo), 0.0, 1.0)
    // a stationary car revs with the throttle; a moving one follows its wheels
    let target = max(SPEC.idle_rpm + throttle * 2400.0 * (1.0 - min(1.0, abs(vf) / 4.0)),
        SPEC.idle_rpm + (SPEC.red_rpm - SPEC.idle_rpm) * ratio);
    {gear: gear, shift: if (up) SPEC.shift_time else max(0.0, car.shift - dt),
     rpm: u.lerp(car.rpm, if (up) target * 0.7 else target, u.ease(14.0, dt))}
}
fn longitudinal(car, vf, pedals, box) {
    let t = pedals.throttle, b = pedals.brake
    let drive = if (car.shift > 0.0) 0.0 else SPEC.pull[box.gear - 1] * (if (vf >= SPEC.gears[4]) 0.0 else 1.0)
    let thrust = if (vf > 0.5) t * drive - b * SPEC.brake
        else if (vf < -0.5) (if (vf > 0.0 - SPEC.reverse_max) 0.0 - b * SPEC.reverse_pull else 0.0) + t * SPEC.brake
        else t * drive - b * SPEC.reverse_pull
    let resist = SPEC.roll + SPEC.drag * vf * vf + (if (t == 0.0 and b == 0.0) SPEC.engine_brake else 0.0) +
        (if (pedals.handbrake) 7.0 else 0.0);
    {thrust: thrust, resist: resist}
}
fn ground_step(car, dt) any^ {
    let g = city.ground(car.x, car.z)
    if (car.grounded) {
        // a sharp drop under a moving car (a ramp lip) launches it with its climbing speed
        if (g < car.y - 0.35) {*: car, grounded: false, air: 0.0}
        else {
            let smooth = abs(g - car.y) < 0.12
            let y = if (smooth) g else u.lerp(car.y, g, u.ease(22.0, dt));
            {*: car, y: y, vy: if (smooth) u.clamp((g - car.y) / dt, -10.0, 10.0) else 0.0}
        }
    } else {
        let vy = car.vy - SPEC.gravity * dt
        let y = car.y + vy * dt;
        if (y <= g) {*: car, y: g, vy: 0.0, grounded: true, landed: 0.0 - vy, airtime: car.air}
        else {*: car, y: y, vy: vy, air: car.air + dt}
    }
}
fn integrate(car, pedals, dt, boxes) any^ {
    let h = car.heading
    let f = u.forward(h), r = u.right(h)
    let vf = car.vx * f[0] + car.vz * f[1]
    let vl = car.vx * r[0] + car.vz * r[1]
    let gb = gearbox(car, vf, pedals.throttle, dt)
    let lon = longitudinal({*: car, shift: gb.shift}, vf, pedals, gb)
    let grounded = car.grounded
    let raw = vf + (if (grounded) lon.thrust else 0.0) * dt
    // resistance slows the car toward rest but never reverses it
    let slowed = if (grounded) raw - u.clamp(lon.resist * dt, 0.0, abs(raw)) * (if (raw > 0.0) 1.0 else -1.0) else raw
    let vf2 = u.clamp(slowed, 0.0 - SPEC.reverse_max, SPEC.gears[4] + 2.0)
    // the wheel recentres faster than it turns
    let lock = steer_lock(vf2)
    let goal = pedals.steer * lock
    let steer = u.approach(car.steer, goal, (if (abs(goal) < abs(car.steer)) SPEC.steer_return else SPEC.steer_rate) * dt)
    let yaw_raw = if (grounded) vf2 * math.tan(steer) / SPEC.wheelbase * (if (pedals.handbrake) 1.4 else 1.0) else 0.0
    let limit = SPEC.grip * (if (pedals.handbrake) 1.5 else 1.0) / max(abs(vf2), 1.0)
    let yaw = u.clamp(yaw_raw, 0.0 - limit, limit)
    // tyres bleed sideways velocity; the handbrake lets the rear step out
    let vl2 = if (grounded) vl * exp(0.0 - (if (pedals.handbrake) SPEC.drift_damp else SPEC.lateral_damp) * dt) else vl
    let vx = f[0] * vf2 + r[0] * vl2, vz = f[1] * vf2 + r[1] * vl2
    let moved = {*: car, heading: u.wrap(h + yaw * dt), vx: vx, vz: vz, x: car.x + vx * dt, z: car.z + vz * dt,
        steer: steer, gear: gb.gear, shift: gb.shift, rpm: gb.rpm, accel: (vf2 - vf) / dt,
        spin: u.wrap(car.spin + vf2 / SPEC.wheel_r * dt), odometer: car.odometer + abs(vf2) * dt}
    // curbs and ramp faces taller than a step block the car like a wall
    let blocked = moved.grounded and city.ground(moved.x, moved.z) > car.y + SPEC.step_up
    let kept = if (blocked) {*: moved, x: car.x, z: car.z, vx: 0.0 - vx * 0.2, vz: 0.0 - vz * 0.2, impact: max(car.impact, abs(vf2))} else moved
    ground_step(collide_all(collide_all(kept, boxes, 0), boxes, 0), dt)
}
// body attitude for presentation: nose follows the climb, leans against cornering and braking
fn attitude(car, dt) {
    let vf = forward_speed(car)
    let climb = if (car.grounded) 0.0 - math.atan2(car.vy, max(abs(vf), 2.0)) else 0.0 - math.atan2(car.vy, max(abs(vf), 4.0)) * 0.7
    let pitch = climb + (if (car.grounded) u.clamp(0.0 - car.accel * 0.004, -0.05, 0.05) else 0.0)
    let yaw_rate = vf * math.tan(car.steer) / SPEC.wheelbase
    let roll = if (car.grounded) u.clamp(yaw_rate * vf * 0.0045, -0.07, 0.07) else car.roll;
    {*: car, pitch: u.lerp(car.pitch, pitch, u.ease(9.0, dt)), roll: u.lerp(car.roll, roll, u.ease(7.0, dt))}
}
// advance by dt, in substeps short enough that the car moves under half a metre each
pub fn step(car, pedals, dt, traffic = []) {
    let fresh = {*: car, impact: 0.0, landed: 0.0, airtime: 0.0, hits: []}
    let boxes = obstacles_near(car, traffic)
    let n = max(1, int(ceil(max(speed(car) * dt / 0.45, dt * 60.0))) ^ { 1 })
    let h = dt / n
    let after = reduce([fresh, *[for (i in 1 to n) i]], (c, i) any^ => integrate(c, pedals, h, boxes))
    let hurt = max(0.0, after.impact - 5.0) * 1.6;
    attitude({*: after, damage: min(100.0, after.damage + hurt)}, dt)
}

// the car's wheels in body coordinates: front pair steers; all four spin
pub let WHEELS = [for (front in [true, false], side in [-1.0, 1.0])
    {front: front, x: side * SPEC.track / 2.0, z: if (front) SPEC.wheelbase / 2.0 else 0.0 - SPEC.wheelbase / 2.0}]
