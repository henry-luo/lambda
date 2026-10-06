import c: .common
import ease: .easing

fn layer(opacity, tx, ty) => {opacity: opacity, tx: tx, ty: ty}

// whole-slide wrappers compose outside authored object and effect transforms.
pub fn sample(plan, ps) {
    let scene = plan.slides[ps.slide]
    let p = if (ps.phase == 'transition') min(1.0, max(0.0, ps.time_ms / scene.transition_duration)) else 1.0
    let t = ease.sample('ease-in-out', p)
    let horizontal = scene.direction == 'left' or scene.direction == 'right'
    let distance = if (horizontal) plan.width else plan.height
    let sign = if (scene.direction == 'left' or scene.direction == 'top') -1.0 else 1.0
    let incoming = (t - 1.0) * sign * distance
    let outgoing = t * sign * distance
    if (ps.phase != 'transition') {incoming: layer(1.0, 0.0, 0.0), outgoing: layer(0.0, 0.0, 0.0)}
    else if (scene.transition == 'fade') {
        incoming: layer(t, 0.0, 0.0), outgoing: layer(1.0, 0.0, 0.0)
    } else if (scene.transition == 'morph') {
        incoming: layer(1.0, 0.0, 0.0), outgoing: layer(1.0, 0.0, 0.0)
    } else {
        incoming: layer(1.0, if (horizontal) incoming else 0.0, if (horizontal) 0.0 else incoming),
        outgoing: if (scene.transition == 'push') layer(1.0, if (horizontal) outgoing else 0.0, if (horizontal) 0.0 else outgoing)
            else layer(1.0, 0.0, 0.0)
    }
}

pub fn style(v) => "opacity:" ++ c.fmt(v.opacity) ++ ";transform:translate(" ++ c.px(v.tx) ++ "," ++ c.px(v.ty) ++ ");"
