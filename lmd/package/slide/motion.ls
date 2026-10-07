import c: .common
import ease: .easing

fn point(segment, t) {
    if (len(segment) == 4) {x: ease.lerp(segment[0], segment[2], t), y: ease.lerp(segment[1], segment[3], t)}
    else {
        let u = 1.0 - t
        {x: u ** 3 * segment[0] + 3.0 * u ** 2 * t * segment[2] + 3.0 * u * t ** 2 * segment[4] + t ** 3 * segment[6],
         y: u ** 3 * segment[1] + 3.0 * u ** 2 * t * segment[3] + 3.0 * u * t ** 2 * segment[5] + t ** 3 * segment[7]}
    }
}

fn tangent(segment, t) {
    if (len(segment) == 4) {x: segment[2] - segment[0], y: segment[3] - segment[1]}
    else {
        let u = 1.0 - t
        {x: 3.0 * u ** 2 * (segment[2] - segment[0]) + 6.0 * u * t * (segment[4] - segment[2]) + 3.0 * t ** 2 * (segment[6] - segment[4]),
         y: 3.0 * u ** 2 * (segment[3] - segment[1]) + 6.0 * u * t * (segment[5] - segment[3]) + 3.0 * t ** 2 * (segment[7] - segment[5])}
    }
}

fn accumulate(points, index, previous, distance, rows) {
    if (index >= len(points)) {length: distance, rows: rows}
    else {
        let p = points[index]
        let length = if (index == 0) 0.0 else math.sqrt((p.x - previous.x) ** 2 + (p.y - previous.y) ** 2)
        let next = distance + length
        accumulate(points, index + 1, p, next, [*rows, {*: p, distance: next}])
    }
}

// A bounded table in the plan amortizes arc-length approximation across frames.
pub fn compile(segments, path) map^ {
    let invalid = if (not (segments is array) or len(segments) == 0) true else
        any([for (seg in segments) not (seg is array) or not contains([4, 8], len(seg)) or not all([for (n in seg) c.finite(n)])])
    let checked = if (invalid) raise c.fail(path, "motion path needs numeric line[4] or cubic[8] segments")
    let discontinuous = any([for (i, seg in segments where i > 0)
        seg[0] != segments[i - 1][len(segments[i - 1]) - 2] or seg[1] != segments[i - 1][len(segments[i - 1]) - 1]])
    let checked_join = if (discontinuous) raise c.fail(path, "motion segments must join")
    let points = [for (i, seg in segments) for (j in 0 to (if (len(seg) == 4) 1 else 64)) {
        let t = j / (if (len(seg) == 4) 1.0 else 64.0)
        {*: point(seg, t), segment: i, t: t}}]
    let table = accumulate(points, 0, points[0], 0.0, [])
    let checked_length = if (not c.finite(table.length)) raise c.fail(path, "motion path length overflow")
    {*: table, segments: segments}
}

pub fn sample(path, progress) {
    let distance = ease.clamp(progress) * path.length
    let after = [for (i, row in path.rows where i > 0 and row.distance >= distance and row.distance > path.rows[i - 1].distance) i]
    let index = if (len(after) > 0) after[0] else len(path.rows) - 1
    let b = path.rows[index]
    let a = path.rows[max(0, index - 1)]
    let fraction = if (b.distance == a.distance) 0.0 else (distance - a.distance) / (b.distance - a.distance)
    let t = ease.lerp(if (a.segment == b.segment) a.t else 0.0, b.t, fraction)
    let seg = path.segments[b.segment]
    let v = tangent(seg, t)
    {*: point(seg, t), angle: if (v.x == 0.0 and v.y == 0.0) 0.0 else math.atan2(v.y, v.x) * 180.0 / math.pi}
}
