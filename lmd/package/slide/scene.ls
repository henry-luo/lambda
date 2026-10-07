import c: .common
import theme_layout: .theme

let object_attrs = ["id", "x", "y", "width", "height", "role", "class", "style", "rotation", "scale", "opacity", "morph_id"]
fn attrs_for(tag) => [*object_attrs,
    *(if (tag == 'text') ["color", "font_size"] else if (tag == 'shape') ["kind", "fill", "stroke", "stroke_width", "radius"]
      else if (tag == 'image') ["src", "fit"] else [])]

fn build_object(node, path, width, height, theme, layout = 'blank', index = 0) map^ {
    let checked_1 = c.attributes(node, attrs_for(name(node)), path)^;
    let checked_2 = c.numbers(node, ["x", "y", "rotation", "stroke_width", "radius"], false, path)^;
    let checked_3 = c.numbers(node, ["width", "height", "font_size", "scale"], true, path)^;
    let checked_4 = c.numbers(node, ["opacity"], false, path)^;
    let guard_1 = if (node.opacity != null and (node.opacity < 0.0 or node.opacity > 1.0)) raise c.fail(path, "opacity outside [0,1]")
    let guard_2 = if (node.id != null and not c.text_id(node.id)) raise c.fail(path, "ID must be nonempty text")
    let checked_morph_id = if (node.morph_id != null and not c.text_id(node.morph_id)) raise c.fail(path, "morph_id must be nonempty text")
    let checked_strings = if (any([for (key in ["color", "fill", "stroke", "class", "style"] where node[key] != null) not (node[key] is string)]))
        raise c.fail(path, "paint/class/style attributes must be strings")
    let checked_role = if (node.role != null and not contains(theme_layout.roles, node.role) and
        not (layout is map and layout[c.as_text(node.role)] != null)) raise c.fail(path, "invalid role")
    let checked_fit = if (node.fit != null and not contains(['contain', 'cover', 'fill'], node.fit)) raise c.fail(path, "invalid image fit")
    let checked_nonnegative = if (any([for (key in ["stroke_width", "radius"] where node[key] != null) node[key] < 0.0]))
        raise c.fail(path, "stroke width/radius must be nonnegative")
    let tag = name(node)
    let checked_children = if (tag == 'group' and len(c.objects(node)) != len(content(node)))
        raise c.fail(path, "group accepts only slide objects")
    let role = if (tag == 'image' and node.role == null and (layout == 'image-left' or layout == 'image-right'))
        (if (layout == 'image-left') 'left' else 'right') else node.role
    let box = theme_layout.bounds(layout, role, width, height, index)
    let w = c.num(node, "width", box.width)
    let h = c.num(node, "height", box.height)
    let guard_3 = if (tag == 'image' and not (node.src is string)) raise c.fail(path, "image needs a src string")
    let guard_4 = if (tag == 'shape' and not contains(['rect', 'ellipse', 'circle', 'line'], c.value(node.kind, 'rect')))
        raise c.fail(path, "unsupported shape")
    let nested = if (tag == 'group') [for (i, child in c.objects(node)) build_object(child, path ++ "." ++ string(i), w, h, theme)^] else []
    {
        id: c.node_id(node, path), dom_key: replace(path, ".", "-"), tag: tag, source: node,
        x: c.num(node, "x", box.x), y: c.num(node, "y", box.y), width: w, height: h,
        rotation: c.num(node, "rotation", 0.0), scale: c.num(node, "scale", 1.0),
        opacity: c.num(node, "opacity", 1.0), children: nested,
        font_size: c.num(node, "font_size", if (node.role == 'title') theme.title_size else theme.body_size), font_family: theme.font_family,
        paint: c.value(if (tag == 'shape' and node.kind == 'line') c.value(node.stroke, node.fill)
            else if (tag == 'shape') node.fill else node.color,
            if (tag == 'shape') theme.accent else theme.foreground),
        morph_id: if (node.morph_id == null) null else c.as_text(node.morph_id)
    }
}

pub fn flatten_objects(objects) => [for (obj in objects) for (entry in [obj, *flatten_objects(obj.children)]) entry]

pub fn build(node, index, width, height, theme = 'light', layouts = {}, source_path = null) map^ {
    let path = c.value(source_path, "s" ++ string(index))
    let checked_5 = c.attributes(node, ["id", "title", "background", "transition", "transition_duration", "direction", "layout", "theme"], path)^;
    let checked_6 = c.numbers(node, ["transition_duration"], false, path)^;
    let checked_id = if (node.id != null and not c.text_id(node.id)) raise c.fail(path, "ID must be nonempty text")
    let layout_name = c.value(node.layout, 'blank')
    let layout = c.value(layouts[c.as_text(layout_name)], layout_name)
    let checked_layout = if (not (layout is map) and not contains(theme_layout.layouts, layout)) raise c.fail(path, "unsupported layout")
    let palette = theme_layout.resolve(c.value(node.theme, theme), path ++ ".theme")^;
    let guard_5 = if (node.transition_duration != null and node.transition_duration < 0.0) raise c.fail(path, "negative transition duration")
    let transition = c.value(node.transition, 'cut')
    let guard_6 = if (not contains(['cut', 'fade', 'push', 'slide', 'morph'], transition)) raise c.fail(path, "unsupported transition")
    let direction = c.value(node.direction, 'left')
    let guard_7 = if (not contains(['left', 'right', 'top', 'bottom'], direction)) raise c.fail(path, "invalid direction")
    let invalid = [for (child in content(node) where not (child is element) or
        not contains(['text', 'image', 'shape', 'group', 'content', 'cue', 'notes'], name(child))) child]
    let guard_8 = if (len(invalid) > 0) raise c.fail(path, "unsupported slide child")
    let objects = [for (i, child in c.objects(node)) build_object(child, path ++ ".o" ++ string(i), width, height, palette, layout, i)^]
    let flat = flatten_objects(objects)
    let checked_7 = c.check_unique(flat, path)^;
    let morphs = [for (obj in flat where obj.morph_id != null) {id: obj.morph_id}]
    let checked_8 = c.check_unique(morphs, path ++ ".morph")^;
    {id: c.node_id(node, path), index: index, objects: objects, targets: flat,
        source: node, palette: palette, notes: c.children(node, 'notes'), transition: transition,
        transition_duration: c.num(node, "transition_duration", 300.0), direction: direction}
}
