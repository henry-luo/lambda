import c: .common

pub fn clamp(t) => max(0.0, min(1.0, t))
pub fn lerp(a, b, t) => a + (b - a) * t
fn curve(t, a, b) => 3.0 * (1.0 - t) ** 2 * t * a + 3.0 * (1.0 - t) * t ** 2 * b + t ** 3
// bisection solves x(t); bounded iterations keep sampling deterministic.
fn solve(x, x1, x2, lo, hi, remaining) {
    let mid = (lo + hi) / 2.0
    if (remaining == 0) mid
    else if (curve(mid, x1, x2) < x) solve(x, x1, x2, mid, hi, remaining - 1)
    else solve(x, x1, x2, lo, mid, remaining - 1)
}
pub fn valid(e) => contains(['linear', 'ease', 'ease-in', 'ease-out', 'ease-in-out',
    'ease-in-cubic', 'ease-out-cubic', 'ease-in-out-cubic'], e) or
    (e is array and len(e) == 4 and all([for (v in e) c.finite(v)]) and
        e[0] >= 0.0 and e[0] <= 1.0 and e[2] >= 0.0 and e[2] <= 1.0)

pub fn sample(e, progress) {
    let t = clamp(progress)
    if (t == 0.0 or t == 1.0 or e == 'linear') t
    else if (e == 'ease-in-cubic') t ** 3
    else if (e == 'ease-out-cubic') 1.0 - (1.0 - t) ** 3
    else if (e == 'ease-in-out-cubic')
        if (t < 0.5) 4.0 * t ** 3 else 1.0 - (-2.0 * t + 2.0) ** 3 / 2.0
    else {
        let points = if (e is array) e else if (e == 'ease') [0.25, 0.1, 0.25, 1.0]
            else if (e == 'ease-in') [0.42, 0.0, 1.0, 1.0]
            else if (e == 'ease-out') [0.0, 0.0, 0.58, 1.0] else [0.42, 0.0, 0.58, 1.0]
        curve(solve(t, points[0], points[2], 0.0, 1.0, 32), points[1], points[3])
    }
}
