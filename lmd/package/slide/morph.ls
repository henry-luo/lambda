import c: .common
import ease: .easing
import color: .color
import samples: .sample
import palette: .theme

fn compatible(a, b) => a.tag == b.tag and a.source.style == b.source.style and
    (if (a.tag == 'shape') c.value(a.source.kind, 'rect') == c.value(b.source.kind, 'rect') and
        a.source.stroke == b.source.stroke and a.source.stroke_width == b.source.stroke_width and a.source.radius == b.source.radius
     else if (a.tag == 'text') content(a.source) == content(b.source) and a.font_size == b.font_size and
        a.font_family == b.font_family and a.source.role == b.source.role
     else if (a.tag == 'image') a.source.src == b.source.src and a.source.fit == b.source.fit
     else if (a.tag == 'content') content(a.source) == content(b.source) else false)

fn endpoint_color(visual, path) array^ => if (visual.paint_rgba != null) visual.paint_rgba else color.parse(visual.paint, path)^

pub fn compile(previous, destination, theme) map^ {
    let path = destination.id ++ ".morph"
    let marked = [*previous.targets, *destination.targets]
    let checked_ids = if (any([for (obj in marked where obj.morph_id != null) obj.source.id == null]))
        raise c.fail(path, "Morph participants require explicit object IDs")
    let nested = [for (scene in [previous, destination]) for (obj in scene.targets where obj.morph_id != null)
        not contains([for (root in scene.objects) root.id], obj.id)]
    let checked_flat = if (any(nested)) raise c.fail(path, "Morph participants must have flat authored bounds")
    let checked_groups = if (any([for (obj in marked where obj.morph_id != null) obj.tag == 'group']))
        raise c.fail(path, "Morph groups require a flattened scene")
    let old_visuals = samples.scene(previous, len(previous.cues) - 1,
        if (len(previous.cues) > 0) previous.cues[len(previous.cues) - 1].duration_ms else 0.0)
    let new_visuals = samples.scene(destination, -1, 0.0)
    let pairs = [for (b in destination.objects where b.morph_id != null)
        for (a in previous.objects where a.morph_id == b.morph_id) {
            let ac = endpoint_color([for (v in old_visuals where v.id == a.id) v][0], path)^;
            let bc = endpoint_color([for (v in new_visuals where v.id == b.id) v][0], path)^;
            {a: a, b: b, compatible: compatible(a, b), ac: ac, bc: bc}
        }]
    let old_background = color.parse(palette.background(previous), path)^;
    let new_background = color.parse(palette.background(destination), path)^;
    {pairs: pairs, old_background: old_background, new_background: new_background}
}

fn geometry(a, b, t) => {*: b,
    x: ease.lerp(a.x, b.x, t), y: ease.lerp(a.y, b.y, t),
    width: ease.lerp(a.width, b.width, t), height: ease.lerp(a.height, b.height, t),
    rotation: ease.lerp(a.rotation, b.rotation, t), scale: ease.lerp(a.scale, b.scale, t)}

fn blend_visual(a, b, t) => {*: b, opacity: ease.lerp(a.opacity * a.visible, b.opacity * b.visible, t), visible: 1.0,
    tx: ease.lerp(a.tx, b.tx, t), ty: ease.lerp(a.ty, b.ty, t),
    sx: ease.lerp(a.sx, b.sx, t), sy: ease.lerp(a.sy, b.sy, t), rotation: ease.lerp(a.rotation, b.rotation, t),
    clip: ease.lerp(a.clip, b.clip, t)}

fn object_patch(obj, visual, roots, pairs, old_visuals, new_visuals, t, incoming) {
    let matching = [for (pair in pairs where (if (incoming) pair.b.id else pair.a.id) == obj.id) pair]
    if (len(matching) == 0) {geometry: obj, visual: {*: visual,
        opacity: visual.opacity * (if (not contains(roots, obj.id)) 1.0 else if (incoming) t else 1.0 - t)}}
    else {
        let pair = matching[0]
        let av = [for (v in old_visuals where v.id == pair.a.id) v][0]
        let bv = [for (v in new_visuals where v.id == pair.b.id) v][0]
        let blended = blend_visual(av, bv, t)
        let opacity = if (pair.compatible) (if (incoming) blended.opacity else 0.0)
            else blended.opacity * (if (incoming) t else 1.0 - t)
        {geometry: {*: geometry(pair.a, pair.b, t), dom_key: obj.dom_key},
         visual: {*: blended, id: obj.id, dom_key: obj.dom_key, opacity: opacity,
            paint: if (pair.compatible) color.css(color.sample(pair.ac, pair.bc, t)) else visual.paint}}
    }
}

pub fn sample(plan, ps) {
    let scene = plan.slides[ps.slide]
    let previous = plan.slides[ps.outgoing]
    let t = ease.sample('ease-in-out', min(1.0, max(0.0, ps.time_ms / scene.transition_duration)))
    let old_visuals = samples.scene(previous, len(previous.cues) - 1,
        if (len(previous.cues) > 0) previous.cues[len(previous.cues) - 1].duration_ms else 0.0)
    let new_visuals = samples.scene(scene, -1, 0.0)
    {incoming: [for (i, obj in scene.targets) object_patch(obj, new_visuals[i], [for (root in scene.objects) root.id], scene.morph.pairs, old_visuals, new_visuals, t, true)],
     outgoing: [for (i, obj in previous.targets) object_patch(obj, old_visuals[i], [for (root in previous.objects) root.id], scene.morph.pairs, old_visuals, new_visuals, t, false)],
     background: color.css(color.sample(scene.morph.old_background, scene.morph.new_background, t))}
}
