// Named TikZ nodes use Radiant's measured-child layout for shape bounds and anchors.
// Render validates and serializes each child's plan; the pure `scene` places nodes
// (positioning, calc, anchors) and routes edges from measured label sizes.
import radiant
import opts: .options
import labels: .labels
import svg: lambda.chart.svg
import shapes: .shapes
import coords: .coords
import arrows: .arrows

let PX_PER_CM = 96.0 / 2.54
let MARGIN = 16.0
// Named-node pictures keep the 1.2 px default stroke of their original renderer
// (RenderOutputParity.TikzNamedWorkflowPaintsShapesAndBranchArrow); direct
// drawings use 0.6 px. Line-width keys override both.
let DEFAULT_WIDTH = 1.2

let COLOR_FREE_NODE_KEYS = ["draw", "fill", "text", "color", "thick", "very thick",
    "ultra thick", "thin", "line width", "dashed", "dotted", "dash pattern", "anchor",
    "inner sep", "align", "text width", "font", "on grid", "node distance",
    "rectangle", "circle", "ellipse", "diamond", "regular polygon", "star",
    "trapezium", "coordinate"]

let EDGE_KEYS = ["draw", "color", "thick", "very thick", "ultra thick", "thin",
    "line width", "dashed", "dotted", "dash pattern", ">"]

let LABEL_KEYS = ["midway", "near start", "near end", "very near start",
    "very near end", "at start", "at end", "pos", "anchor", "color", "text", "fill",
    "font", "inner sep"]

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

let SEGMENT_POSITIONS = [{key: "at start", pos: 0.0}, {key: "very near start", pos: 0.125},
    {key: "near start", pos: 0.25}, {key: "midway", pos: 0.5}, {key: "near end", pos: 0.75},
    {key: "very near end", pos: 0.875}, {key: "at end", pos: 1.0}]

// A point element as a reference: calc, named node (with anchor) or literal cm.
// Node and pic targets carry `at_` attributes; path points carry plain ones.
fn element_ref(point, at_target, settings) any^ {
    let calc = if (at_target) point.at_calc else point.calc
    let ref = if (at_target) point.at_ref else point.ref
    let anchor = if (at_target) point.at_anchor else point.anchor
    if (calc != null)
        {calc: coords.parse_calc(calc, coords.calc_frame(settings.basis_x,
            settings.basis_y, settings.scale))^}
    else if (ref != null) {ref: ref, anchor: anchor}
    else if (point.coord_system == "axis" or point.coord_system == "axis-description")
        raise error("axis coordinates need a PGFPlots axis")
    else {
        let literal = coords.literal_point(point, settings.basis_x, settings.basis_y)^;
        {x: literal.x * settings.scale, y: literal.y * settings.scale}
    }
}

fn paint_of(node, outline, custom_colors) any^ {
    let base = opts.color(node, "black", custom_colors)^
    let draw_value = opts.value(node, "draw", null)
    let fill_value = opts.value(node, "fill", null)
    let stroke = if (draw_value == null and not outline) null
        else if (draw_value == null or draw_value == "") base
        else opts.color_value(draw_value, custom_colors)^
    let fill = if (fill_value == null) null
        else if (fill_value == "") base
        else opts.color_value(fill_value, custom_colors)^;
    {stroke: stroke, fill: fill, width: opts.stroke_width(node, DEFAULT_WIDTH)^,
     dash: opts.dash_array(node)^}
}

fn text_style(node, custom_colors) any^ {
    let text_value = opts.value(node, "text", null)
    let color = if (text_value != null) opts.color_value(text_value, custom_colors)^
        else opts.color(node, "black", custom_colors)^
    let inner = opts.value(node, "inner sep", null)
    let padding = if (inner == null) "4px 8px" else string(opts.dimension_px(inner)^) ++ "px"
    let width = opts.value(node, "text width", null)
    let font = opts.value(node, "font", null)
    let size = if (font == null) null else labels.font_size(font)
    let alignment = opts.value(node, "align", null)
    let valid_font = if (font != null and size == null)
        raise error("unsupported TikZ node font: " ++ font) else true
    let valid_align = if (alignment != null and alignment != "center" and
        alignment != "left" and alignment != "right")
        raise error("unsupported TikZ node alignment: " ++ alignment) else true
    "display:inline-block;padding:" ++ padding ++ ";font-size:16px;line-height:1.3;" ++
        "vertical-align:top;color:" ++ color ++ ";" ++
        (if (width == null) "white-space:nowrap;"
         else "white-space:normal;width:" ++ string(opts.dimension_px(width)^) ++ "px;") ++
        (if (size == null) "" else "font-size:" ++ size ++ ";") ++
        (if (alignment == null) "" else "text-align:" ++ alignment ++ ";")
}

fn node_spec(node, settings, custom_colors) any^ {
    let checked = opts.check(node, [*COLOR_FREE_NODE_KEYS, *shapes.PARAMETER_KEYS,
        *coords.PLACEMENT_KEYS], custom_colors)^
    let place = coords.placement(node, settings.node_distance, settings.on_grid)^
    let anchor = opts.value(node, "anchor", null)
    let valid_anchor = if (place != null and anchor != null)
        raise error("TikZ anchor conflicts with a positioning key") else true
    let has_at = node.at_calc != null or node.at_ref != null or node.x != null or
        node.polar_angle != null or node.x_source != null;
    {kind: "node", id: node.id,
     shape: shapes.shape_spec(node)^,
     at: if (has_at) element_ref(node, true, settings)^ else null,
     place: place, anchor: anchor,
     paint: paint_of(node, false, custom_colors)^}
}

fn coordinate_spec(node, settings) any^ {
    let checked = opts.check(node, [])^;
    {kind: "coordinate", id: node.id, shape: {shape: "coordinate", min_width: 0.0,
        min_height: 0.0}, at: element_ref(node, true, settings)^, place: null,
     anchor: null, paint: {stroke: null, fill: null, width: 0.0, dash: null}}
}

fn label_position(node) any^ {
    let named = [for (entry in SEGMENT_POSITIONS where opts.has(node, entry.key)) entry.pos]
    let explicit = opts.value(node, "pos", null)
    if (explicit != null) opts.numeric_value(explicit)^
    else if (len(named) > 0) named[len(named) - 1]
    else null
}

// One label on a path: placed along a segment or at the current vertex.
fn label_spec(node, edge_index, vertex_count, settings, custom_colors) any^ {
    let checked = opts.check(node, [*LABEL_KEYS, *coords.PLACEMENT_KEYS], custom_colors)^
    let valid = if (opts.has(node, "sloped"))
        raise error("sloped labels are unsupported in named-node pictures") else true
    let place = coords.placement(node, settings.node_distance, false)^
    let valid_place = if (place != null and place.target != null)
        raise error("path labels cannot use positioning of") else true
    let position = label_position(node)^
    let valid_position = if (position != null and (position < 0.0 or position > 1.0))
        raise error("TikZ label position must be in [0,1]") else true;
    {kind: "edge-label", edge: edge_index,
     after: vertex_count - 1, on_segment: node.segment == true,
     pos: position, place: place, anchor: opts.value(node, "anchor", null)}
}

fn label_html(node, custom_colors) any^ {
    let prepared = labels.prepare(if (node.source == null) "" else node.source)^
    let fill_value = opts.value(node, "fill", null)
    let fill = if (fill_value == null) null else opts.color_value(fill_value, custom_colors)^
    let text_value = opts.value(node, "text", null)
    let color = if (text_value != null) opts.color_value(text_value, custom_colors)^
        else opts.color(node, "black", custom_colors)^
    let font = opts.value(node, "font", null)
    let size = if (font == null) null else labels.font_size(font)
    let valid_font = if (font != null and size == null)
        raise error("unsupported TikZ node font: " ++ font) else true;
    {element: prepared.element,
     style: "display:inline-block;white-space:nowrap;padding:2px 4px;font-size:14px;" ++
        "line-height:1.2;vertical-align:top;color:" ++ color ++ ";" ++
        (if (fill == null) "" else "background:" ++ fill ++ ";") ++
        (if (size == null) "" else "font-size:" ++ size ++ ";")}
}

fn edge_parts(path, edge_index, settings, custom_colors, index, vertices, labels_acc) any^ {
    if (index >= len(path)) {vertices: vertices, labels: labels_acc}
    else {
        let child = path[index]
        let tag = if (child is element) string(name(child)) else ""
        if (tag == "point")
            edge_parts(path, edge_index, settings, custom_colors, index + 1,
                [*vertices, {ref: element_ref(child, false, settings)^, via: child.via}],
                labels_acc)^
        else if (tag == "node")
            edge_parts(path, edge_index, settings, custom_colors, index + 1, vertices,
                [*labels_acc, {node: child,
                    spec: label_spec(child, edge_index, len(vertices), settings,
                        custom_colors)^}])^
        else edge_parts(path, edge_index, settings, custom_colors, index + 1,
            vertices, labels_acc)^
    }
}

fn edge_spec(path, edge_index, settings, custom_colors) any^ {
    let valid_action = if (path.action != "draw" and path.action != "path")
        raise error("only drawn TikZ paths are supported between named nodes") else true
    let valid_shape = if (path.shape != null or path.arc_source != null)
        raise error("named-node pictures support only straight path segments") else true
    let checked = opts.check(path, [*EDGE_KEYS, *arrows.option_keys(path)], custom_colors)^
    let parts = edge_parts(path, edge_index, settings, custom_colors, 0, [], [])^
    let valid_count = if (len(parts.vertices) < 2)
        raise error("named TikZ paths need at least two coordinates") else true
    let visible = path.action == "draw" or opts.has(path, "draw")
    let paint = paint_of(path, visible, custom_colors)^
    let local_default = opts.value(path, ">", null)
    let fallback = if (local_default != null) local_default else settings.arrow_default
    let spec = arrows.spec(path)
    let start = arrows.resolve(spec.start, paint.width, fallback, custom_colors)^
    let end = arrows.resolve(spec.end, paint.width, fallback, custom_colors)^;
    {spec: {kind: "edge", vertices: parts.vertices, paint: paint,
        visible: visible, start_tip: start, end_tip: end},
     labels: parts.labels}
}

// Validated child plans in source order; each path's labels follow it.
fn plan(children, settings, custom_colors, index, acc) any^ {
    if (index >= len(children)) acc
    else {
        let child = children[index]
        let tag = string(name(child))
        let additions = if (tag == "node")
            [{spec: node_spec(child, settings, custom_colors)^, node: child}]
            else if (tag == "coordinate")
                [{spec: coordinate_spec(child, settings)^, node: null}]
            else if (tag == "path") {
                let edge = edge_spec(child, len(acc), settings, custom_colors)^;
                [{spec: edge.spec, node: null}, *edge.labels]
            }
            else if (tag == "tikzset") (let checked = opts.check_tikzset(child)^, [])
            else raise error("unsupported TikZ content in a named-node picture: " ++ tag)
        plan(children, settings, custom_colors, index + 1, [*acc, *additions])^
    }
}

fn place_node(spec, size, placed) any^ {
    let geom = shapes.geometry(spec.shape, size[0], size[1])
    let center = if (spec.place != null and spec.place.target != null) {
        let target = coords.named_point(placed, spec.place.target.ref,
            spec.place.target_anchor)^
        let own = shapes.anchor_offset(geom, spec.place.own)^;
        [target[0] + float(spec.place.x) - own[0], target[1] + float(spec.place.y) - own[1]]
    } else {
        let base = if (spec.at == null) [0.0, 0.0] else coords.evaluate_ref(spec.at, placed)^
        let own_anchor = if (spec.place != null) spec.place.own
            else if (spec.anchor != null) spec.anchor else "center"
        let shift = if (spec.place == null) [0.0, 0.0]
            else [float(spec.place.x), float(spec.place.y)]
        let own = shapes.anchor_offset(geom, own_anchor)^;
        [base[0] + shift[0] - own[0], base[1] + shift[1] - own[1]]
    };
    {id: spec.id, x: center[0], y: center[1], geom: geom, paint: spec.paint}
}

fn place_all(specs, sizes, index, placed, entries) any^ {
    if (index >= len(specs)) entries
    else {
        let spec = specs[index]
        if (spec.kind == "node" or spec.kind == "coordinate") {
            let located = place_node(spec, sizes[index], placed)^
            let entry = {*:located, index: index};
            // Unnamed nodes are drawn but cannot be referenced.
            place_all(specs, sizes, index + 1,
                if (spec.id == null) placed else [*placed, entry], [*entries, entry])^
        } else place_all(specs, sizes, index + 1, placed, entries)^
    }
}

// Route points compare with a tolerance: anchors and calc results carry rounding noise.
fn same(a, b) => abs(a[0] - b[0]) < 1e-9 and abs(a[1] - b[1]) < 1e-9

fn vertex_point(vertex, placed, limit) any^ {
    let ref = vertex.ref
    let entries = if (ref.ref == null) [] else [for (entry in placed
        where entry.id == ref.ref and entry.index < limit) entry]
    let valid = if (ref.ref != null and len(entries) == 0)
        raise error("unknown TikZ node: " ++ ref.ref) else true
    let entry = if (len(entries) == 0) null else entries[len(entries) - 1]
    let earlier = [for (item in placed where item.index < limit) item]
    // A bare node name clips to that node's border; anchors and points are exact.
    if (entry != null and ref.anchor == null and entry.geom.kind != "coordinate")
        {center: [entry.x, entry.y], node: entry}
    else {center: coords.evaluate_ref(ref, earlier)^, node: null}
}

fn border_toward(vertex, toward) {
    if (vertex.node == null) vertex.center
    else {
        let offset = shapes.border_offset(vertex.node.geom, toward[0] - vertex.center[0],
            toward[1] - vertex.center[1]);
        [vertex.center[0] + offset[0], vertex.center[1] + offset[1]]
    }
}

fn segment_route(from, to, via) any^ {
    let corner = if (via == "-|") [to.center[0], from.center[1]]
        else if (via == "|-") [from.center[0], to.center[1]] else null
    let waypoints = if (corner == null or same(corner, from.center) or same(corner, to.center))
        [] else [corner]
    let start = border_toward(from, if (len(waypoints) > 0) corner else to.center)
    let end = border_toward(to, if (len(waypoints) > 0) corner else from.center)
    let valid = if (same(start, end))
        raise error("named TikZ edge has no visible length") else true;
    if (len(waypoints) > 0) [start, waypoints[0], end] else [start, end]
}

// Edge segments in cm; each segment is the polyline between two vertices.
pub fn route(spec, placed, limit) any^ {
    let vertices = [for (vertex in spec.vertices) vertex_point(vertex, placed, limit)^];
    [for (index in 0 to (len(vertices) - 2))
        segment_route(vertices[index], vertices[index + 1],
            spec.vertices[index + 1].via)^]
}

fn interpolate(a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]

// TikZ splits a -| or |- segment at the corner: pos 0.5 is the corner.
fn along(segment, t) any^ {
    if (len(segment) == 2) interpolate(segment[0], segment[1], t)^
    else if (t <= 0.5) interpolate(segment[0], segment[1], t * 2.0)^
    else interpolate(segment[1], segment[2], (t - 0.5) * 2.0)^
}

fn label_point(spec, segments) any^ {
    if (spec.on_segment)
        along(segments[spec.after], if (spec.pos == null) 0.5 else float(spec.pos))^
    else if (spec.pos != null and spec.after > 0)
        along(segments[spec.after - 1], float(spec.pos))^
    else if (spec.after == 0) segments[0][0]
    else {
        let previous = segments[spec.after - 1];
        previous[len(previous) - 1]
    }
}

fn label_box(spec, routes, size) any^ {
    let matches = [for (item in routes where item.index == spec.edge) item.segments]
    let point = label_point(spec, matches[0])^
    let geom = shapes.geometry({shape: "rectangle", min_width: 0.0, min_height: 0.0,
        radius: 0.0}, size[0], size[1])
    let own_anchor = if (spec.place != null) spec.place.own
        else if (spec.anchor != null) spec.anchor else "center"
    let own = shapes.anchor_offset(geom, own_anchor)^
    let shift = if (spec.place == null) [0.0, 0.0]
        else [float(spec.place.x), float(spec.place.y)];
    {x: point[0] + shift[0] - own[0], y: point[1] + shift[1] - own[1],
     half_w: size[0] / 2.0, half_h: size[1] / 2.0}
}

// Pure scene in cm from child plans and their measured label sizes (cm).
pub fn scene(specs, sizes) any^ {
    let entries = place_all(specs, sizes, 0, [], [])^
    let named = [for (entry in entries where entry.id != null) entry]
    let routes = [for (index, spec in specs where spec.kind == "edge")
        {index: index, segments: route(spec, named, index)^}]
    let label_boxes = [for (index, spec in specs where spec.kind == "edge-label")
        {*:label_box(spec, routes, sizes[index])^, index: index}];
    {nodes: entries, edges: routes, labels: label_boxes}
}

fn scene_bounds(found) {
    let xs = [for (node in found.nodes, edge in [node.x - node.geom.half_w,
            node.x + node.geom.half_w]) edge,
        for (route in found.edges, segment in route.segments, point in segment) point[0],
        for (box in found.labels, edge in [box.x - box.half_w, box.x + box.half_w]) edge]
    let ys = [for (node in found.nodes, edge in [node.y - node.geom.half_h,
            node.y + node.geom.half_h]) edge,
        for (route in found.edges, segment in route.segments, point in segment) point[1],
        for (box in found.labels, edge in [box.y - box.half_h, box.y + box.half_h]) edge];
    {left: min(xs), right: max(xs), bottom: min(ys), top: max(ys)}
}

// Consecutive segments sharing an endpoint continue one subpath.
fn segment_commands(segments, index) {
    let segment = segments[index]
    let previous = if (index == 0) null else segments[index - 1]
    let joined = previous != null and same(previous[len(previous) - 1], segment[0]);
    [for (at in 0 to (len(segment) - 1) where at > 0 or not joined)
        if (at == 0) svg.M(segment[at][0], segment[at][1])
        else svg.L(segment[at][0], segment[at][1])]
}

fn subpath_data(segments) => join([for (index in 0 to (len(segments) - 1),
    part in segment_commands(segments, index)) part], " ")

fn edge_svg(spec, segments, map_point) {
    let paint = spec.paint
    let first = segments[0]
    let final_segment = segments[len(segments) - 1]
    let start_from = map_point(first[1])
    let end_from = map_point(final_segment[len(final_segment) - 2])
    let start = map_point(first[0])
    let end = map_point(final_segment[len(final_segment) - 1])
    // Tips cover the stroke end, so the drawn line stops at each tip's back.
    let trimmed_start = if (spec.start_tip == null) start
        else arrows.shortened(start_from, start, float(spec.start_tip.line_end))
    let trimmed_end = if (spec.end_tip == null) end
        else arrows.shortened(end_from, end, float(spec.end_tip.line_end))
    let mapped = [for (index, segment in segments)
        [for (at, point in segment)
            if (index == 0 and at == 0) trimmed_start
            else if (index == len(segments) - 1 and at == len(segment) - 1) trimmed_end
            else map_point(point)]]
    let data = subpath_data(mapped)
    if (not spec.visible) null
    else <g class: "tikz-edge",
        <path d: data, fill: "none", stroke: paint.stroke, 'stroke-width': paint.width,
            'stroke-dasharray': paint.dash>
        if (spec.start_tip != null)
            arrows.tip_svg(spec.start_tip, start_from, start, paint.stroke, paint.width)
        if (spec.end_tip != null)
            arrows.tip_svg(spec.end_tip, end_from, end, paint.stroke, paint.width)
    >
}

pn layout(parent, children, ctx) any^ {
    let specs = [for (child in children) parse(string(child.attrs["data-tikz-spec"]), "json")^]
    let sizes = [for (child in children) [float(child.width) / PX_PER_CM,
        float(child.height) / PX_PER_CM]]
    let found = scene(specs, sizes)^
    let bounds = scene_bounds(found)
    let width = (bounds.right - bounds.left) * PX_PER_CM + 2.0 * MARGIN
    let height = (bounds.top - bounds.bottom) * PX_PER_CM + 2.0 * MARGIN
    let map_point = (point) => [MARGIN + (point[0] - bounds.left) * PX_PER_CM,
        MARGIN + (bounds.top - point[1]) * PX_PER_CM]
    let shape_elements = [for (node in found.nodes)
        (let center = map_point([node.x, node.y]),
         shapes.outline_svg(node.geom, center[0], center[1], PX_PER_CM, node.paint))]
    let edge_elements = [for (route in found.edges)
        edge_svg(specs[route.index], route.segments, map_point)]
    let boxes = [*[for (node in found.nodes where node.geom.kind != "coordinate")
            {index: node.index, x: node.x, y: node.y,
            half_w: sizes[node.index][0] / 2.0, half_h: sizes[node.index][1] / 2.0}],
        *found.labels]
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
        style: "overflow:visible;pointer-events:none;",
        for (item in [*shape_elements, *edge_elements] where item != null) item
    >
    let placements = [for (index, child in children)
        (let matches = [for (box in boxes where box.index == index) box],
         if (len(matches) == 1)
             (let corner = map_point([matches[0].x - matches[0].half_w,
                 matches[0].y + matches[0].half_h]),
              {index: index, x: corner[0], y: corner[1], z: 0})
         else {index: index, x: 0.0, y: 0.0, z: -2})]
    return {width: width, height: height, placements: placements,
        paint_layers: [{z: -1, content: graphic}]}
}

fn hidden_style() => "display:block;width:0;height:0;overflow:hidden;" ++
    "visibility:hidden;pointer-events:none;"

fn child_html(item, custom_colors) any^ {
    let spec = item.spec
    let encoded = format(spec, 'json')
    if (spec.kind == "node") {
        let prepared = labels.prepare(if (item.node.source == null) "" else item.node.source)^;
        <span class: "tikz-node-label", 'data-tikz-kind': "node",
            'data-node-id': spec.id, 'data-tikz-spec': encoded,
            style: text_style(item.node, custom_colors)^, prepared.element>
    } else if (spec.kind == "edge-label") {
        let label = label_html(item.node, custom_colors)^;
        <span class: "tikz-edge-label", 'data-tikz-kind': "edge-label",
            'data-tikz-spec': encoded, style: label.style, label.element>
    } else <span 'data-tikz-kind': spec.kind, 'data-tikz-spec': encoded,
        style: hidden_style()>
}

// Measurement-free validation sizes: every reference and option is checked at render.
fn nominal_sizes(items) => [for (item in items)
    if (item.spec.kind == "node" or item.spec.kind == "edge-label") [1.0, 0.5] else [0.0, 0.0]]

pub fn settings(picture) any^ {
    let scale = opts.numeric_value(opts.value(picture, "scale", "1"))^
    let valid_scale = if (scale <= 0.0) raise error("TikZ picture scale must be positive")
        else true;
    {node_distance: opts.value(picture, "node distance", "1cm and 1cm"),
     on_grid: opts.has(picture, "on grid"),
     arrow_default: opts.value(picture, ">", null),
     scale: scale,
     basis_x: opts.dimension_px(opts.value(picture, "x", "1cm"))^ / PX_PER_CM,
     basis_y: opts.dimension_px(opts.value(picture, "y", "1cm"))^ / PX_PER_CM}
}

// Child plans for a named-node picture (exported for semantic tests).
pub fn plans(picture, custom_colors = []) any^ {
    let children = [for (child in picture
        where child is element and string(name(child)) != "option") child]
    let items = plan(children, settings(picture)^, custom_colors, 0, [])^;
    [for (item in items) item.spec]
}

pub fn render(picture, custom_colors = []) any^ {
    let children = [for (child in picture
        where child is element and string(name(child)) != "option") child]
    let items = plan(children, settings(picture)^, custom_colors, 0, [])^
    let ids = [for (item in items where item.spec.kind == "node" and item.spec.id != null)
        item.spec.id]
    let duplicates = [for (index, id in ids
        where len([for (earlier in slice(ids, 0, index) where earlier == id) earlier]) > 0) id]
    let valid_ids = if (len(duplicates) > 0)
        raise error("duplicate TikZ node: " ++ duplicates[0]) else true
    let validated = scene([for (item in items) item.spec], nominal_sizes(items))^
    let installed = radiant.register_layout("lambda-tikz", layout);
    <span class: "tikz-picture tikz-named-picture",
        'data-radiant-layout': "lambda-tikz",
        style: "position:relative;display:inline-block;vertical-align:bottom;",
        for (item in items) child_html(item, custom_colors)^
    >
}
