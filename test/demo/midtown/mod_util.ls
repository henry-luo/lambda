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

