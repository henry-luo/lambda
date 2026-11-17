// Small numeric helpers shared by the Midtown modules. Headings are radians about +Y with
// forward = [sin h, 0, cos h], so h = 0 faces +Z and turning right decreases h.

pub let PI = math.pi
pub let TAU = 2.0 * math.pi

pub fn clamp(x, lo, hi) => if (x < lo) lo else if (x > hi) hi else x
pub fn lerp(a, b, k) => a + (b - a) * k
// move toward a target by at most step, as a steering wheel or pedal does
pub fn approach(x, target, step) => if (x < target) min(target, x + step) else max(target, x - step)
// frame-rate independent smoothing: the fraction of the gap closed in dt at the given rate
pub fn ease(rate, dt) => 1.0 - exp(0.0 - rate * dt)
// wrap an angle into (-pi, pi]
pub fn wrap(a) => a - TAU * round(a / TAU)
pub fn sq(x) => x * x
// an sRGB colour in linear light: vertex colours are shaded as linear values
pub fn lin(c) => [for (x in c) math.pow(x, 2.2)]
// Agents far from (x, z) take turns: tiers are [reach, every] pairs, farthest first; an agent beyond a tier's
// reach steps every `every`th frame (by its number n), carrying the time it is owed, so distant traffic
// costs a fraction of a frame and catches up exactly. `frame` null steps all.
// (at most two tiers; a stepped agent's record is copied again only when it carried owed time)
fn cadence(a, x, z, tiers) => (let d = max(abs(a.x - x), abs(a.z - z)),
    if (d > tiers[0][0]) tiers[0][1] else if (len(tiers) > 1 and d > tiers[1][0]) tiers[1][1] else 1)
pub fn staggered(agents, x, z, tiers, frame, dt, step_one) any^ => [for (a in agents, let every = if (frame == null) 1 else cadence(a, x, z, tiers),
        let owed = a.owed or 0.0)
    if (every > 1 and (a.n + frame) % every != 0) {*: a, owed: owed + dt}
    else if (owed == 0.0) step_one(a, dt)^ else {*: step_one(a, dt + owed)^, owed: 0.0}]

pub fn forward(h) => [sin(h), cos(h)]
pub fn right(h) => [0.0 - cos(h), sin(h)]
pub fn heading_of(dx, dz) => math.atan2(dx, dz)
pub fn dist2(ax, az, bx, bz) => sq(ax - bx) + sq(az - bz)
pub fn dist(ax, az, bx, bz) => sqrt(sq(ax - bx) + sq(az - bz))

// a body pose: yaw about +Y, then pitch about the body's right axis, then roll about forward
pub fn quat(yaw, pitch = 0.0, roll = 0.0) {
    let cy = cos(yaw / 2.0), sy = sin(yaw / 2.0)
    let cp = cos(pitch / 2.0), sp = sin(pitch / 2.0)
    let cr = cos(roll / 2.0), sr = sin(roll / 2.0);
    // q = qy * qx(pitch) * qz(roll), components [x, y, z, w]
    [cy * sp * cr + sy * cp * sr, sy * cp * cr - cy * sp * sr, cy * cp * sr - sy * sp * cr, cy * cp * cr + sy * sp * sr]
}

