import c: .common
import ease: .easing
import color: .color

pub fn compile(stops, channel, path) array^ {
    let checked_channel = if (not contains(['tx', 'ty', 'sx', 'sy', 'rotation', 'opacity', 'clip', 'paint'], channel))
        raise c.fail(path, "unsupported keyframe channel")
    let invalid = not (stops is array) or len(stops) < 2 or
        any([for (stop in stops) not (stop is array) or len(stop) != 2 or not c.finite(stop[0])])
    let checked_stops = if (invalid) raise c.fail(path, "keyframes need [fraction,value] stops")
    let checked_endpoints = if (stops[0][0] != 0.0 or stops[len(stops) - 1][0] != 1.0)
        raise c.fail(path, "keyframe fractions must start at 0 and end at 1")
    let checked_order = if (any([for (i, stop in stops where i > 0) stop[0] <= stops[i - 1][0]]))
        raise c.fail(path, "keyframe fractions must increase")
    let checked_values = if (channel != 'paint' and any([for (stop in stops) not c.finite(stop[1]) or
        (contains(['opacity', 'clip'], channel) and (stop[1] < 0.0 or stop[1] > 1.0)) or
        (contains(['sx', 'sy'], channel) and stop[1] <= 0.0)])) raise c.fail(path, "invalid keyframe value");
    [for (stop in stops) [stop[0], if (channel == 'paint') color.parse(stop[1], path)^ else stop[1]]]
}

pub fn value(stops, channel, progress) {
    let t = ease.clamp(progress)
    let after = [for (i, stop in stops where i > 0 and stop[0] >= t) i]
    let index = if (len(after) > 0) after[0] else len(stops) - 1
    let a = stops[index - 1]
    let b = stops[index]
    let fraction = (t - a[0]) / (b[0] - a[0])
    if (channel == 'paint') color.sample(a[1], b[1], fraction) else ease.lerp(a[1], b[1], fraction)
}

pub fn sample(stops, channel, progress) {
    let sampled = value(stops, channel, progress)
    if (channel == 'paint') color.css(sampled) else sampled
}
