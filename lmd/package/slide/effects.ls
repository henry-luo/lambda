import c: .common
import ease: .easing
import motion: .motion
import color: .color
import keyframes: .keyframes

pub let kinds = ['appear', 'disappear', 'fade-in', 'fade-out', 'fly-in', 'fly-out',
    'zoom-in', 'zoom-out', 'spin', 'pulse', 'wipe-in', 'wipe-out', 'motion', 'highlight', 'keyframes']
fn track(effect, target, channel, a, b, begin, duration, repeat, easing, mode) =>
    {target: target.id, channel: channel, a: a, b: b, begin_ms: begin,
        duration_ms: duration, repeat: repeat, easing: easing, mode: mode,
        entrance: contains(['appear', 'fade-in', 'fly-in', 'zoom-in', 'wipe-in'], effect.kind),
        exit: contains(['disappear', 'fade-out', 'fly-out', 'zoom-out', 'wipe-out'], effect.kind)}

pub fn expand(effect, targets, begin, path) map^ {
    let extras = if (contains(['fly-in', 'fly-out'], effect.kind)) ["from", "distance"]
        else if (contains(['wipe-in', 'wipe-out'], effect.kind)) ["from"]
        else if (contains(['zoom-in', 'zoom-out', 'pulse'], effect.kind)) ["scale"]
        else if (effect.kind == 'spin') ["angle"] else if (effect.kind == 'motion') ["path", "orient"]
        else if (effect.kind == 'highlight') ["color"] else if (effect.kind == 'keyframes') ["channel", "stops", "from"] else []
    let checked_1 = c.attributes(effect, ["target", "kind", "duration", "delay", "easing", "repeat", *extras], path)^;
    let checked_2 = c.numbers(effect, ["duration", "delay", "distance", "angle", "scale", "repeat"], false, path)^;
    let matching = [for (obj in targets where obj.id == c.as_text(effect.target)) obj]
    let guard_1 = if (len(matching) != 1) raise c.fail(path, "unknown target " ++ c.as_text(effect.target))
    let target = matching[0]
    let explicit_id = if (target.source.id == null) raise c.fail(path, "animated target requires an explicit ID")
    let kind = effect.kind
    let guard_2 = if (not contains(kinds, kind)) raise c.fail(path, "unsupported effect " ++ c.as_text(kind))
    let duration = c.num(effect, "duration", if (kind == 'appear' or kind == 'disappear') 0.0 else 400.0)
    let delay = c.num(effect, "delay", 0.0)
    let repeat = c.num(effect, "repeat", 1.0)
    let guard_3 = if (duration < 0.0 or delay < 0.0 or repeat < 1.0 or floor(repeat) != repeat)
        raise c.fail(path, "duration/delay must be nonnegative; repeat must be a positive integer")
    let easing = c.value(effect.easing, 'linear')
    let guard_4 = if (not ease.valid(easing)) raise c.fail(path, "invalid easing")
    let direction = c.value(effect.from, 'bottom')
    let guard_5 = if (not contains(['left', 'right', 'top', 'bottom'], direction)) raise c.fail(path, "invalid effect direction")
    let distance = c.num(effect, "distance", 40.0)
    let entering = contains(['appear', 'fade-in', 'fly-in', 'zoom-in', 'wipe-in'], kind)
    let leaving = contains(['disappear', 'fade-out', 'fly-out', 'zoom-out', 'wipe-out'], kind)
    let track_begin = begin + delay
    let visibility = if (entering or leaving)
        [track(effect, target, 'visible', if (entering) 0.0 else 1.0, if (entering) 1.0 else 0.0,
            track_begin, duration, repeat, 'linear', if (entering) 'enter' else 'exit')] else []
    let opacity = if (contains(['fade-in', 'fade-out', 'fly-in', 'fly-out', 'zoom-in', 'zoom-out'], kind))
        [track(effect, target, 'opacity', if (entering) 0.0 else target.opacity,
            if (entering) target.opacity else 0.0, track_begin, duration, repeat, easing, 'linear')] else []
    let transforms = if (kind == 'fly-in' or kind == 'fly-out') {
        let channel = if (direction == 'left' or direction == 'right') 'tx' else 'ty'
        let offset = if (direction == 'left' or direction == 'top') -distance else distance;
        [track(effect, target, channel, if (entering) offset else 0.0,
            if (entering) 0.0 else offset, track_begin, duration, repeat, easing, 'linear')]
    } else if (kind == 'zoom-in' or kind == 'zoom-out' or kind == 'pulse') {
        let small = c.num(effect, "scale", if (kind == 'pulse') 1.15 else 0.5)
        let guard_6 = if (small <= 0.0) raise c.fail(path, "scale must be positive");
        [for (channel in ['sx', 'sy']) track(effect, target, channel,
            if (kind == 'pulse' or leaving) 1.0 else small,
            if (kind == 'pulse' or leaving) small else 1.0,
            track_begin, duration, repeat, easing, if (kind == 'pulse') 'pulse' else 'linear')]
    } else if (kind == 'spin') [track(effect, target, 'rotation', 0.0, c.num(effect, "angle", 360.0), track_begin, duration, repeat, easing, 'linear')]
    else if (kind == 'wipe-in' or kind == 'wipe-out') [track(effect, target, 'clip', if (entering) 1.0 else 0.0,
        if (entering) 0.0 else 1.0, track_begin, duration, repeat, easing, direction)]
    else if (kind == 'motion') {
        let route = motion.compile(effect.path, path)^;
        let checked_orient = if (effect.orient != null and effect.orient != false and effect.orient != 'auto')
            raise c.fail(path, "motion orient must be false or auto");
        [for (channel in (if (effect.orient == 'auto') ['tx', 'ty', 'rotation'] else ['tx', 'ty']))
            {*: track(effect, target, channel, 0.0, 0.0, track_begin, duration, repeat, easing, 'motion'), motion: route}]
    }
    else if (kind == 'highlight') {
        let a = color.parse(target.paint, path)^;
        let b = color.parse(effect.color, path)^;
        [track(effect, target, 'paint', a, b, track_begin, duration, repeat, easing, 'color')]
    }
    else if (kind == 'keyframes') {
        let stops = keyframes.compile(effect.stops, effect.channel, path)^;
        [{*: track(effect, target, effect.channel,
            keyframes.value(stops, effect.channel, 0.0), keyframes.value(stops, effect.channel, 1.0),
            track_begin, duration, repeat, easing, 'keyframes'), stops: stops, direction: direction}]
    }
    else []
    {duration_ms: delay + duration * repeat, tracks: [*visibility, *opacity, *transforms]}
}
