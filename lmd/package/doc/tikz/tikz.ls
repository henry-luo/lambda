// Public native TikZ entry point; graphics islands are parsed as data.
import opts: .options
import labels: .labels
import plots: .pgfplots
import lsystem: .lsystem
import named: .named
import svg: lambda.chart.svg
import math_css: lambda.doc.math.css
import latex_util: lambda.latex.util
import pgfmath: .pgfmath
import people: .people

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn drawing_nodes(picture) => [for (child in picture
    where child is element and
        (string(name(child)) == "path" or string(name(child)) == "node" or
         string(name(child)) == "coordinate")) child]

fn shift_centimeters(value) any^ {
    let raw = trim(value)
    if (ends_with(raw, "cm"))
        opts.numeric_value(slice(raw, 0, len(raw) - 2))^
    else if (ends_with(raw, "mm") or ends_with(raw, "pt") or
             ends_with(raw, "bp") or ends_with(raw, "in"))
        opts.dimension_px(raw)^ * 2.54 / 96.0
    else raise error("unsupported TikZ scope shift: " ++ raw)
}

fn foreach_range(first, step, ending, index, acc) any^ {
    if (index > 1000) raise error("TikZ foreach range exceeds 1000 values")
    else {
        let current = first + float(index) * step
        if ((step > 0.0 and current > ending + 0.0000001) or
            (step < 0.0 and current < ending - 0.0000001)) acc
        else foreach_range(first, step, ending, index + 1,
            [*acc, string(current)])^
    }
}

fn foreach_items(source, program_data) any^ {
    let parts = [for (part in latex_util.split_top_level(source, ",")) trim(part)]
    if (len(parts) == 4 and parts[2] == "...") {
        let first = pgfmath.evaluate_source(parts[0], program_data)^
        let second = pgfmath.evaluate_source(parts[1], program_data)^
        let ending = pgfmath.evaluate_source(parts[3], program_data)^
        let step = second - first
        if (step == 0.0) raise error("TikZ foreach range needs a nonzero step")
        else foreach_range(first, step, ending, 0, [])^
    } else parts
}

fn substitute_bindings(source, bindings, values, index) any^ {
    if (index >= len(bindings)) source
    else substitute_bindings(latex_util.replace_command_token(source,
        bindings[index], values[index]), bindings, values, index + 1)^
}

fn collect_foreach(node, dx, dy, stroke_width, basis_x, basis_y,
                   program_data, index, acc) any^ {
    let bindings = [for (part in latex_util.split_top_level(node.bindings, "/"))
        trim(part)]
    if (len(bindings) == 0 or len(bindings) > 4 or
        len([for (binding in bindings where not starts_with(binding, "\\"))
            binding]) > 0)
        raise error("unsupported TikZ foreach binding")
    else {
        let items = foreach_items(node.items, program_data)^
        if (index >= len(items)) acc
        else {
            let values = [for (part in latex_util.split_top_level(items[index], "/"))
                trim(part)]
            if (len(values) != len(bindings))
                raise error("TikZ foreach item does not match bindings")
            else {
                let expanded = substitute_bindings(node.body, bindings, values, 0)^
                let parsed = parse(expanded, {type: "tikz"})^
                let next = collect_drawing(parsed, dx, dy, stroke_width,
                    basis_x, basis_y, program_data, 0, acc)^
                collect_foreach(node, dx, dy, stroke_width,
                    basis_x, basis_y, program_data, index + 1,
                    next.records)^
            }
        }
    }
}

// Scope offsets and stroke defaults are inherited without changing the neutral AST.
fn collect_drawing(container, dx, dy, stroke_width,
                   basis_x, basis_y, program_data, index, acc) any^ {
    if (index >= len(container)) {records: acc, program: program_data}
    else {
        let child = container[index]
        let tag = if (child is element) string(name(child)) else ""
        let next = if (tag == "scope") {
            let checked = opts.check(child,
                ["thick", "very thick", "xshift", "yshift"])^
            let scope_dx = opts.value(child, "xshift", null)
            let scope_dy = opts.value(child, "yshift", null)
            let x = dx + if (scope_dx == null) 0.0 else shift_centimeters(scope_dx)^
            let y = dy + if (scope_dy == null) 0.0 else shift_centimeters(scope_dy)^
            let inherited = if (opts.has(child, "very thick")) 1.7
                else if (opts.has(child, "thick")) 1.1 else stroke_width
            let nested = collect_drawing(child, x, y, inherited,
                basis_x, basis_y, program_data, 0, acc)^;
            {records: nested.records, program: program_data}
        } else if (tag == "foreach")
            {records: collect_foreach(child, dx, dy, stroke_width,
                basis_x, basis_y, program_data, 0, acc)^,
             program: program_data}
        else if (tag == "pgf_definition")
            {records: acc,
             program: pgfmath.with_definition(program_data, child)^}
        else if (tag == "tikzmath")
            {records: acc,
             program: pgfmath.with_tikzmath(program_data, child.source)^}
        else if (tag == "path" or tag == "node" or tag == "coordinate")
            {records: [*acc, {source: child, dx: dx, dy: dy,
                basis_x: basis_x, basis_y: basis_y,
                stroke_width: stroke_width, program: program_data}],
             program: program_data}
        else {records: acc, program: program_data}
        collect_drawing(container, dx, dy, stroke_width,
            basis_x, basis_y, next.program, index + 1, next.records)^
    }
}

fn physical_point(point, dx, dy, basis_x, basis_y,
                  program_data = null) any^ {
    if (point.polar_angle != null) {
        let angle = float(point.polar_angle) * 3.141592653589793 / 180.0
        let radius = float(point.polar_radius)
        let x_basis = if (point.radius_explicit == true) 1.0 else basis_x
        let y_basis = if (point.radius_explicit == true) 1.0 else basis_y;
        {x: radius * math.cos(angle) * x_basis + dx,
         y: radius * math.sin(angle) * y_basis + dy}
    } else {
        let source_x = if (point.x_source == null) float(point.x)
            else pgfmath.evaluate_source(point.x_source, program_data)^
        let source_y = if (point.y_source == null) float(point.y)
            else pgfmath.evaluate_source(point.y_source, program_data)^
        let x = source_x *
            (if (point.x_explicit == true) 1.0 else basis_x) + dx
        let y = source_y *
            (if (point.y_explicit == true) 1.0 else basis_y) + dy;
        {x: x, y: y}
    }
}

fn coordinate_definition(record) {
    let point = physical_point(record.source, record.dx, record.dy,
        record.basis_x, record.basis_y, record.program)^;
    {id: record.source.id, x: point.x, y: point.y}
}

fn coordinate_definitions(records) => [for (record in records
    where string(name(record.source)) == "coordinate")
    coordinate_definition(record)]

fn ellipse_extent(record, axis) any^ {
    let path_offset = path_shift(record.source, record.basis_x, record.basis_y)^
    let center = physical_point(children_named(record.source, "point")[0],
        record.dx + path_offset[0], record.dy + path_offset[1],
        record.basis_x, record.basis_y, record.program)^
    let coordinate = if (axis == "x") center.x else center.y
    let radius = if (axis == "x") float(record.source.rx) *
        (if (record.source.rx_explicit == true) 1.0 else record.basis_x)
        else float(record.source.ry) *
        (if (record.source.ry_explicit == true) 1.0 else record.basis_y);
    [coordinate - radius, coordinate + radius]
}

fn path_shift(path, basis_x, basis_y) any^ {
    let source = opts.value(path, "shift", null)
    if (source == null) [0.0, 0.0]
    else {
        let raw = trim(source)
        if (not starts_with(raw, "(") or not ends_with(raw, ")"))
            raise error("TikZ shift needs (x,y)")
        else {
            let parts = latex_util.split_top_level(slice(raw, 1, len(raw) - 1), ",")
            if (len(parts) != 2) raise error("TikZ shift needs two coordinates")
            else [opts.numeric_value(parts[0])^ * basis_x,
                  opts.numeric_value(parts[1])^ * basis_y]
        }
    }
}

fn arc_record_point(record, point) any^ {
    let offset = path_shift(record.source, record.basis_x, record.basis_y)^
    physical_point(point, record.dx + offset[0], record.dy + offset[1],
        record.basis_x, record.basis_y, record.program)^
}

fn resolved_point(point, coordinates) {
    if (point.ref == null) point
    else {
        let matches = [for (coordinate in coordinates
            where coordinate.id == point.ref) coordinate]
        if (len(matches) == 1) matches[0] else point
    }
}

fn path_point(point, coordinates, dx, dy, basis_x, basis_y,
              program_data) any^ {
    let position = if (point.ref == null)
        physical_point(point, dx, dy, basis_x, basis_y, program_data)^
        else resolved_point(point, coordinates)
    {x: position.x, y: position.y, move: point.move == true}
}

fn path_data_points(path, coordinates, dx = 0.0, dy = 0.0,
                    basis_x = 1.0, basis_y = 1.0,
                    program_data = null) any^ {
    let offset = path_shift(path, basis_x, basis_y)^
    if (path.shape == "plot")
        [for (point in plots.plot_points(path, null, program_data)^)
            physical_point(point, dx + offset[0], dy + offset[1],
                basis_x, basis_y, program_data)^]
    else if (path.shape == "l-system")
        [for (point in lsystem.points(path)^)
            {x: point.x + dx + offset[0], y: point.y + dy + offset[1]}]
    else if (path.shape == "arc-chain")
        [for (point in arc_chain_points(path.source, basis_x, basis_y)^)
            {x: point.x + dx + offset[0], y: point.y + dy + offset[1],
             move: point.move}]
    else [for (point in children_named(path, "point"))
        path_point(point, coordinates, dx + offset[0],
            dy + offset[1], basis_x, basis_y, program_data)^]
}

fn path_points(records, coordinates) any^ => [for (record in records,
    point in if (string(name(record.source)) == "path")
        path_data_points(record.source, coordinates, record.dx, record.dy,
            record.basis_x, record.basis_y, record.program)^ else [])
        point]

fn node_points(records) => [for (record in records
    where string(name(record.source)) == "node")
    physical_point(record.source, record.dx, record.dy,
        record.basis_x, record.basis_y, record.program)^]

// Arc geometry stays in script; the parser only preserves its angle/radius source.
fn arc_points(path) {
    if (path.arc_source == null) []
    else {
        let parts = split(path.arc_source, ":")
        let vertices = children_named(path, "point")
        if (len(parts) != 3 or len(vertices) == 0) null
        else {
            let first = float(trim(parts[0])) ^ { null }
            let ending = float(trim(parts[1])) ^ { null }
            let radius = float(trim(parts[2])) ^ { null }
            if (first == null or ending == null or radius == null or radius <= 0.0)
                null
            else {
                let origin = vertices[len(vertices) - 1]
                let start_rad = first * 3.141592653589793 / 180.0
                let cx = float(origin.x) - radius * math.cos(start_rad)
                let cy = float(origin.y) - radius * math.sin(start_rad)
                arc_sample_points(0, 32, first, ending, radius, cx, cy, [])
            }
        }
    }
}

fn arc_sample_points(index, steps, first, ending, radius, cx, cy, acc) {
    if (index > steps) acc
    else {
        let angle = (first + (ending - first) * float(index) / float(steps)) *
            3.141592653589793 / 180.0
        let point = {x: cx + radius * math.cos(angle),
                     y: cy + radius * math.sin(angle)}
        arc_sample_points(index + 1, steps, first, ending,
            radius, cx, cy, acc ++ [point])
    }
}

fn skip_path_space(source, at) =>
    if (at >= len(source)) at
    else if (slice(source, at, at + 1) == " " or
             slice(source, at, at + 1) == "\n" or
             slice(source, at, at + 1) == "\t")
        skip_path_space(source, at + 1)
    else at

fn path_component(raw, basis) any^ {
    let source = trim(raw)
    let units = if (ends_with(source, "cm")) "cm"
        else if (ends_with(source, "mm")) "mm"
        else if (ends_with(source, "pt")) "pt"
        else if (ends_with(source, "bp")) "bp" else null
    if (units == null) opts.numeric_value(source)^ * basis
    else {
        let magnitude = opts.numeric_value(slice(source, 0,
            len(source) - len(units)))^
        let factor = if (units == "cm") 1.0
            else if (units == "mm") 0.1
            else if (units == "pt") 2.54 / 72.27
            else 2.54 / 72.0
        magnitude * factor
    }
}

fn arc_chain_coordinate(source, at, current_x, current_y, reference_x,
                        reference_y, basis_x, basis_y) any^ {
    let relative = slice(source, at, at + 1) == "+"
    let advance = slice(source, at, at + 2) == "++"
    let open_at = if (advance) at + 2 else if (relative) at + 1 else at
    let grouped = latex_util.read_balanced(source, open_at, "(", ")")^
    let cartesian = latex_util.split_top_level(grouped.raw, ",")
    let polar = latex_util.split_top_level(grouped.raw, ":")
    let vector = if (len(cartesian) == 2)
        [path_component(cartesian[0], basis_x)^,
         path_component(cartesian[1], basis_y)^]
        else if (len(polar) == 2) {
            let angle = opts.numeric_value(polar[0])^ *
                3.141592653589793 / 180.0
            let radius = path_component(polar[1], basis_x)^;
            [radius * math.cos(angle), radius * math.sin(angle)]
        } else raise error("unsupported arc-chain coordinate")
    let origin_x = if (relative) reference_x else 0.0
    let origin_y = if (relative) reference_y else 0.0
    let next_x = origin_x + vector[0]
    let next_y = origin_y + vector[1];
    {next: grouped.next, x: next_x, y: next_y,
     reference_x: if (relative and not advance) reference_x else next_x,
     reference_y: if (relative and not advance) reference_y else next_y}
}

fn arc_chain_steps(source, at, current_x, current_y, reference_x,
                   reference_y, basis_x, basis_y, points) any^ {
    let cursor = skip_path_space(source, at)
    if (cursor >= len(source)) points
    else {
        let line = slice(source, cursor, cursor + 2) == "--"
        let next = if (line) skip_path_space(source, cursor + 2) else cursor
        if (slice(source, next, next + 3) == "arc") {
            if (current_x == null) raise error("arc needs a starting coordinate")
            else {
                let group_at = skip_path_space(source, next + 3)
                let grouped = latex_util.read_balanced(source, group_at,
                    "(", ")")^
                let parts = latex_util.split_top_level(grouped.raw, ":")
                if (len(parts) != 3)
                    raise error("arc needs start angle, end angle and radius")
                else {
                    let first = opts.numeric_value(parts[0])^
                    let ending = opts.numeric_value(parts[1])^
                    let radius = path_component(parts[2], basis_x)^
                    if (radius <= 0.0 or basis_x != basis_y)
                        raise error("unsupported arc radius or nonuniform basis")
                    else {
                        let radians = first * 3.141592653589793 / 180.0
                        let cx = current_x - radius * math.cos(radians)
                        let cy = current_y - radius * math.sin(radians)
                        let samples = arc_sample_points(1, 32, first, ending,
                            radius, cx, cy, [])
                        let endpoint = samples[len(samples) - 1]
                        let appended = points ++ [for (point in samples)
                            {x: point.x, y: point.y, move: false}]
                        arc_chain_steps(source, grouped.next,
                            endpoint.x, endpoint.y, endpoint.x, endpoint.y,
                            basis_x, basis_y, appended)^
                    }
                }
            }
        } else if (slice(source, next, next + 1) == "(" or
                   slice(source, next, next + 1) == "+") {
            let coordinate = arc_chain_coordinate(source, next,
                current_x, current_y, reference_x, reference_y,
                basis_x, basis_y)^
            let move = current_x == null or not line
            arc_chain_steps(source, coordinate.next,
                coordinate.x, coordinate.y,
                coordinate.reference_x, coordinate.reference_y,
                basis_x, basis_y,
                [*points, {x: coordinate.x, y: coordinate.y, move: move}])^
        } else raise error("unsupported operator in TikZ arc chain")
    }
}

fn arc_chain_points(source, basis_x, basis_y) any^ =>
    arc_chain_steps(source, 0, null, null, 0.0, 0.0,
        basis_x, basis_y, [])^

fn arc_chain_svg(mapped, flags) {
    latex_util.str_join([for (at in 0 to (len(mapped) - 1))
        if (flags[at]) svg.M(mapped[at][0], mapped[at][1])
        else svg.L(mapped[at][0], mapped[at][1])], " ")
}

fn coil_spec(path) any^ {
    let source = opts.value(path, "decoration", null)
    let active = opts.has(path, "decorate")
    if (source == null and not active) null
    else if (source == null or not active)
        raise error("TikZ decoration requires both decoration and decorate")
    else {
        let parts = [for (part in split(source, ",")) trim(part)]
        let kind = [for (part in parts where part == "coil") part]
        let unknown = [for (part in parts
            where part != "coil" and not starts_with(part, "aspect=") and
                not starts_with(part, "segment length=") and
                not starts_with(part, "amplitude=")) part]
        let valid_kind = if (len(kind) != 1 or len(unknown) > 0)
            raise error("unsupported TikZ decoration: " ++ source) else true
        let aspects = [for (part in parts where starts_with(part, "aspect="))
            opts.numeric_value(slice(part, len("aspect="), len(part)))^]
        let segments = [for (part in parts where starts_with(part, "segment length="))
            opts.dimension_px(slice(part, len("segment length="), len(part)))^]
        let amplitudes = [for (part in parts where starts_with(part, "amplitude="))
            opts.dimension_px(slice(part, len("amplitude="), len(part)))^]
        let valid_dimensions = if (len(aspects) > 1 or len(segments) != 1 or len(amplitudes) != 1)
            raise error("TikZ coil needs one segment length and amplitude") else true
        let aspect = if (len(aspects) == 0) 0.5 else aspects[0]
        // Bind validation results so the function returns only its settings map.
        let valid_aspect = if (aspect < 0.0 or aspect > 1.0)
            raise error("TikZ coil aspect must be in [0,1]") else true;
        {aspect: aspect, segment: segments[0], amplitude: amplitudes[0]}
    }
}

fn coil_samples(start_point, end_point, spec, index, steps, cycles, acc) {
    if (index > steps) acc
    else {
        let fraction = float(index) / float(steps)
        let vx = end_point[0] - start_point[0]
        let vy = end_point[1] - start_point[1]
        let length = math.sqrt(vx * vx + vy * vy)
        let wave = spec.amplitude * math.sin(fraction * cycles * 6.283185307179586)
        let taper = math.sin(fraction * 3.141592653589793)
        let along = spec.aspect * spec.amplitude *
            (math.cos(fraction * cycles * 6.283185307179586) - 1.0) * taper
        let point = [start_point[0] + vx * fraction - vy * wave / length + vx * along / length,
                     start_point[1] + vy * fraction + vx * wave / length + vy * along / length]
        coil_samples(start_point, end_point, spec, index + 1, steps,
            cycles, [*acc, point])
    }
}

fn decorated_points(path, mapped) any^ {
    let spec = coil_spec(path)^
    if (spec == null) mapped
    else if (len(mapped) != 2) raise error("TikZ coil needs a two-point path")
    else {
        let vx = mapped[1][0] - mapped[0][0]
        let vy = mapped[1][1] - mapped[0][1]
        let length = math.sqrt(vx * vx + vy * vy)
        let valid_length = if (length <= 0.0) raise error("TikZ coil path has zero length") else true
        let cycles = max([1, min([128, int(length / spec.segment)])])
        coil_samples(mapped[0], mapped[1], spec, 0,
            cycles * 12, float(cycles), [])
    }
}

fn terminal_bar(endpoint, neighbor, color, stroke_width) {
    let vx = neighbor[0] - endpoint[0]
    let vy = neighbor[1] - endpoint[1]
    let length = math.sqrt(vx * vx + vy * vy)
    let px = 0.0 - vy * 4.0 / length
    let py = vx * 4.0 / length;
    <path d: svg.M(endpoint[0] - px, endpoint[1] - py) ++ " " ++
        svg.L(endpoint[0] + px, endpoint[1] + py),
        fill: "none", stroke: color, 'stroke-width': stroke_width>
}

fn dash_array(path) any^ {
    let custom = opts.value(path, "dash pattern", null)
    if (custom != null) {
        let words = [for (part in split(trim(custom), " ") where trim(part) != "")
            trim(part)]
        if (len(words) != 4 or words[0] != "on" or words[2] != "off")
            raise error("unsupported TikZ dash pattern: " ++ custom)
        string(opts.dimension_px(words[1])^) ++ " " ++
            string(opts.dimension_px(words[3])^)
    } else if (opts.has(path, "dashed")) "4 4"
    else null
}

fn grid_step(raw, basis) any^ {
    let value = if (ends_with(raw, "cm") or ends_with(raw, "mm") or
        ends_with(raw, "pt") or ends_with(raw, "bp") or ends_with(raw, "in"))
        opts.dimension_px(raw)^
        else opts.numeric_value(raw)^ * basis
    if (value <= 0.0) raise error("TikZ grid step must be positive")
    else value
}

fn grid_paths(mapped, path, color, stroke_width, dash, px_per_cm,
              basis_x, basis_y, origin_x, origin_y) any^ {
    if (len(mapped) != 2) raise error("TikZ grid needs two corners")
    let x_min = min([mapped[0][0], mapped[1][0]])
    let x_max = max([mapped[0][0], mapped[1][0]])
    let y_min = min([mapped[0][1], mapped[1][1]])
    let y_max = max([mapped[0][1], mapped[1][1]])
    let step = opts.value(path, "step", "1")
    let xstep = grid_step(opts.value(path, "xstep", step),
        px_per_cm * basis_x)^
    let ystep = grid_step(opts.value(path, "ystep", step),
        px_per_cm * basis_y)^
    let x_first = origin_x + ceil((x_min - origin_x) / xstep) * xstep
    let y_first = origin_y + ceil((y_min - origin_y) / ystep) * ystep
    let nx = int(floor((x_max - x_first) / xstep + 0.000001)) + 1
    let ny = int(floor((y_max - y_first) / ystep + 0.000001)) + 1
    if (nx < 0 or ny < 0 or nx + ny > 1024)
        raise error("TikZ grid exceeds 1024 lines");
    <g
        for (i in 0 to (nx - 1))
            <path d: svg.M(x_first + float(i) * xstep, y_min) ++ " " ++
                svg.L(x_first + float(i) * xstep, y_max),
                fill: "none", stroke: color, 'stroke-width': stroke_width,
                'stroke-dasharray': dash>
        for (i in 0 to (ny - 1))
            <path d: svg.M(x_min, y_first + float(i) * ystep) ++ " " ++
                svg.L(x_max, y_first + float(i) * ystep),
                fill: "none", stroke: color, 'stroke-width': stroke_width,
                'stroke-dasharray': dash>
    >
}

fn gradient_spec(path) any^ {
    let left = opts.value(path, "left color", null)
    let right = opts.value(path, "right color", null)
    let middle = opts.value(path, "middle color", null)
    if (left == null and right == null and middle == null) null
    else if (left == null or right == null)
        raise error("TikZ gradient needs left and right colors")
    else {
        let first = opts.color_value(left)^
        let final_color = opts.color_value(right)^
        let center = if (middle == null) null else opts.color_value(middle)^
        let id = "tikz-gradient-" ++ replace(first, "#", "") ++ "-" ++
            (if (center == null) "none" else replace(center, "#", "")) ++
            "-" ++ replace(final_color, "#", "")
        {id: id, first: first, center: center, final_color: final_color}
    }
}

fn gradient_definition(spec) =>
    <defs
        <linearGradient id: spec.id,
            x1: "0%", y1: "0%", x2: "100%", y2: "0%",
            <stop offset: "0%", 'stop-color': spec.first>;
            if (spec.center != null)
                <stop offset: "50%", 'stop-color': spec.center>;
            <stop offset: "100%", 'stop-color': spec.final_color>
        >
    >

fn draw_path(record, coordinates, min_x, max_y, left, top,
             px_per_cm, custom_colors) any^ {
    let path = record.source
    if (path.action != "draw" and path.action != "fill" and
        path.action != "filldraw")
        raise error("unsupported TikZ path action")
    let checked = opts.check(path, ["black", "blue", "red", "green", "orange",
        "darkgreen", "purple", "gray", "color", "fill", "draw",
        "thick", "very thick", "ultra thick",
        "line width", "pattern", "domain", "samples", "->", "<-", "<->",
        "|-|", "latex-latex", "step", "xstep", "ystep", "dash pattern", "dashed",
        "decoration", "decorate", "shift", "rotate", "rounded corners",
        "left color", "right color", "middle color", "opacity",
        "smooth", "variable"], custom_colors)^
    let color = opts.color(path, "black", custom_colors)^
    let points = path_data_points(path, coordinates, record.dx, record.dy,
        record.basis_x, record.basis_y, record.program)^
    let arc = arc_points(path)
    if (arc == null) raise error("unsupported TikZ arc: " ++ path.arc_source)
    let mapped_raw = [for (point in points ++
            [for (arc_point in arc) arc_record_point(record, arc_point)^])
        [left + (float(point.x) - min_x) * px_per_cm,
         top + (max_y - float(point.y)) * px_per_cm]];
    let mapped = decorated_points(path, mapped_raw)^
    let move_flags = [for (point in points) point.move == true]
    let explicit_width = opts.value(path, "line width", null)
    let stroke_width = if (explicit_width != null) opts.dimension_px(explicit_width)^
        else if (opts.has(path, "ultra thick")) 2.2
        else if (opts.has(path, "very thick")) 1.7
        else if (opts.has(path, "thick")) 1.1 else record.stroke_width
    let pattern = opts.value(path, "pattern", null)
    if (pattern != null and pattern != "north east lines")
        raise error("unsupported TikZ pattern: " ++ pattern)
    let fill_source = opts.value(path, "fill", null)
    let fill_color = if (fill_source == null or fill_source == "") color
        else opts.color_value(fill_source, custom_colors)^
    let gradient = gradient_spec(path)^
    if (gradient != null and (fill_source != null or pattern != null))
        raise error("TikZ gradient conflicts with fill or pattern")
    let fill = if (pattern == "north east lines") "url(#tikz-north-east-lines)"
        else if (gradient != null) "url(#" ++ gradient.id ++ ")"
        else if (path.action == "fill" or path.action == "filldraw" or
                 opts.has(path, "fill")) fill_color else "none"
    let stroke = if (path.action == "fill" and not opts.has(path, "draw"))
        "none" else color
    let dash = dash_array(path)^
    let opacity_source = opts.value(path, "opacity", null)
    let opacity = if (opacity_source == null) null
        else opts.numeric_value(opacity_source)^
    if (opacity != null and (opacity < 0.0 or opacity > 1.0))
        raise error("TikZ opacity must be in [0,1]")
    let stealth = opts.stealth_length(path)^
    let rounded = opts.value(path, "rounded corners", null)
    if (rounded != null and path.shape != "l-system")
        raise error("rounded corners are unsupported for this TikZ path")
    if (rounded != null) opts.dimension_px(rounded)^
    let graphic = if (path.shape == "rectangle") {
        let x = min([mapped[0][0], mapped[1][0]])
        let y = min([mapped[0][1], mapped[1][1]])
        let width = abs(mapped[1][0] - mapped[0][0])
        let height = abs(mapped[1][1] - mapped[0][1]);
        <rect x: x, y: y, width: width, height: height,
            fill: fill, stroke: stroke, 'stroke-width': stroke_width,
            'stroke-dasharray': dash>
    } else if (path.shape == "grid") {
        grid_paths(mapped, path, stroke, stroke_width, dash,
            px_per_cm, record.basis_x, record.basis_y,
            left - min_x * px_per_cm,
            top + max_y * px_per_cm)^
    } else if (path.shape == "ellipse")
        <ellipse cx: mapped[0][0], cy: mapped[0][1],
            rx: float(path.rx) *
                (if (path.rx_explicit == true) 1.0 else record.basis_x) * px_per_cm,
            ry: float(path.ry) *
                (if (path.ry_explicit == true) 1.0 else record.basis_y) * px_per_cm,
            fill: fill, stroke: stroke, 'stroke-width': stroke_width,
            'stroke-dasharray': dash>
    else {
        let final_index = len(mapped) - 1
        let start_arrow = opts.has(path, "<-") or opts.has(path, "<->") or
            opts.has(path, "latex-latex")
        let end_arrow = opts.has(path, "->") or opts.has(path, "<->") or
            opts.has(path, "latex-latex") or stealth > 0.0
        let barred = opts.has(path, "|-|")
        if ((start_arrow or end_arrow or barred) and len(mapped) < 2)
            raise error("TikZ terminal path needs two distinct points")
        let first_distinct = if (len(mapped) < 2) false
            else mapped[0][0] != mapped[1][0] or mapped[0][1] != mapped[1][1]
        let last_distinct = if (len(mapped) < 2) false
            else mapped[final_index][0] != mapped[final_index - 1][0] or
                mapped[final_index][1] != mapped[final_index - 1][1]
        if (((start_arrow or barred) and not first_distinct) or
            ((end_arrow or barred) and not last_distinct))
            raise error("TikZ terminal endpoint is coincident with its neighbor");
        <g opacity: opacity,
            <path d: if (path.shape == "arc-chain" or
                    len([for (flag in move_flags where flag) flag]) > 0)
                    arc_chain_svg(mapped, move_flags)
                    else svg.line_path(mapped),
                fill: fill, stroke: stroke,
                'stroke-width': stroke_width, 'stroke-dasharray': dash,
                'stroke-linejoin': if (rounded == null) null else "round",
                'stroke-linecap': if (rounded == null) null else "round">
            if (start_arrow) svg.arrow_head(mapped[1][0], mapped[1][1],
                mapped[0][0], mapped[0][1], stroke)
            if (end_arrow) svg.arrow_head(mapped[final_index - 1][0],
                mapped[final_index - 1][1], mapped[final_index][0],
                mapped[final_index][1], stroke,
                if (stealth > 0.0) stealth else 8.0, stealth > 0.0)
            if (barred) terminal_bar(mapped[0], mapped[1], stroke, stroke_width)
            if (barred) terminal_bar(mapped[final_index],
                mapped[final_index - 1], stroke, stroke_width)
        >
    }
    if (gradient == null) graphic
    else <g gradient_definition(gradient) graphic>
}

fn draw_paths(records, coordinates, index, min_x, max_y, left, top,
              px_per_cm, custom_colors, acc) any^ {
    if (index >= len(records)) acc
    else {
        let record = records[index]
        let rendered = if (string(name(record.source)) == "path" and
            record.source.action != "clip")
            draw_path(record, coordinates, min_x, max_y, left, top,
                px_per_cm, custom_colors)^
        else null
        let next = if (rendered == null) acc else [*acc, rendered]
        draw_paths(records, coordinates, index + 1,
            min_x, max_y, left, top, px_per_cm, custom_colors, next)^
    }
}

fn extra_node_label(raw, x, y) any^ {
    let source = trim(raw)
    let options_end = if (starts_with(source, "[")) index_of(source, "]") else null
    if (starts_with(source, "[") and options_end == null)
        raise error("unclosed TikZ label options")
    let option_source = if (options_end == null) ""
        else slice(source, 1, options_end)
    let position_source = trim(if (options_end == null) source
        else slice(source, options_end + 1, len(source)))
    let colon = index_of(position_source, ":")
    if (colon == null) raise error("TikZ label needs placement:text")
    let side = trim(slice(position_source, 0, colon))
    let content = trim(slice(position_source, colon + 1, len(position_source)))
    let parts = if (option_source == "") []
        else [for (part in split(option_source, ",")) trim(part)]
    let unsupported = [for (part in parts
        where not starts_with(part, "label distance=") and
            not starts_with(part, "rotate=") and
            not starts_with(part, "text depth=")) part]
    if (len(unsupported) > 0)
        raise error("unsupported TikZ label option: " ++ unsupported[0])
    let distances = [for (part in parts where starts_with(part, "label distance="))
        opts.dimension_px(slice(part, len("label distance="), len(part)))^]
    let rotations = [for (part in parts where starts_with(part, "rotate="))
        opts.numeric_value(slice(part, len("rotate="), len(part)))^]
    let depths = [for (part in parts where starts_with(part, "text depth="))
        trim(slice(part, len("text depth="), len(part)))]
    if (len(distances) > 1 or len(rotations) > 1 or len(depths) > 1)
        raise error("duplicate TikZ label option")
    let distance = if (len(distances) == 0) 4.0 else distances[0]
    let depth = if (len(depths) == 0) 0.0
        else if (ends_with(depths[0], "ex"))
            opts.numeric_value(slice(depths[0], 0, len(depths[0]) - 2))^ * 8.0
        else raise error("unsupported TikZ label text depth")
    let offset_x = if (side == "right") distance
        else if (side == "left") 0.0 - distance else 0.0
    let offset_y = if (side == "above") 0.0 - distance
        else if (side == "below") distance else 0.0
    if (side != "right" and side != "left" and
        side != "above" and side != "below")
        raise error("unsupported TikZ label placement: " ++ side)
    let align = if (side == "right") "translate(0,-50%)"
        else if (side == "left") "translate(-100%,-50%)"
        else if (side == "above") "translate(-50%,-100%)"
        else "translate(-50%,0)"
    let rotation = if (len(rotations) == 0) ""
        else " rotate(" ++ string(rotations[0]) ++ "deg)"
    labels.positioned(labels.prepare(content)^,
        x + offset_x, y + offset_y + depth,
        "transform:" ++ align ++ rotation ++ ";")
}

fn render_label_node(node, x, y, min_x, max_y, left, top, px_per_cm,
                     tangent = null, inherited_color = "black") any^ {
    let checked = opts.check(node,
        ["above", "below", "left", "right", "midway", "anchor", "label",
         "sloped"])^
    let dx = if (opts.has(node, "left")) -12.0
        else if (opts.has(node, "right")) 12.0 else 0.0
    let dy = if (opts.has(node, "above")) -12.0
        else if (opts.has(node, "below")) 12.0 else 0.0
    let anchor = opts.value(node, "anchor", "center")
    let translation = if (anchor == "center") ""
        else if (anchor == "north") "translate(-50%,0)"
        else if (anchor == "south") "translate(-50%,-100%)"
        else if (anchor == "east") "translate(-100%,-50%)"
        else if (anchor == "west") "translate(0,-50%)"
        else if (anchor == "north west") "translate(0,0)"
        else if (anchor == "north east") "translate(-100%,0)"
        else if (anchor == "south west") "translate(0,-100%)"
        else if (anchor == "south east") "translate(-100%,-100%)"
        else raise error("unsupported TikZ node anchor: " ++ anchor)
    if (opts.has(node, "sloped") and tangent == null)
        raise error("sloped TikZ node requires a path tangent")
    let rotation = if (opts.has(node, "sloped"))
        " rotate(" ++ string(math.atan2(0.0 - tangent[1], tangent[0]) *
            180.0 / 3.141592653589793) ++ "deg)"
        else ""
    let node_color = opts.color(node, inherited_color)^
    let px = left + (float(x) - min_x) * px_per_cm + dx
    let py = top + (max_y - float(y)) * px_per_cm + dy
    let source = if (node.source == null) "" else trim(node.source)
    let main = if (source == "") null
        else labels.positioned(labels.prepare(source)^, px, py,
            "color:" ++ node_color ++ ";" ++
                if (translation == "" and rotation == "") ""
                else "transform:" ++
                    (if (translation == "") "translate(-50%,-50%)"
                        else translation) ++ rotation ++ ";")
    let extra_source = opts.value(node, "label", null)
    let extra = if (extra_source == null) null
        else extra_node_label(extra_source, px, py)^;
    <span
        if (main != null) main
        if (extra != null) extra
    >
}

fn path_midpoint(path) {
    let points = children_named(path, "point")
    if (len(points) == 0) null
    else if (len(points) == 1) points[0]
    else if (len(points) == 2)
        {x: (float(points[0].x) + float(points[1].x)) / 2.0,
         y: (float(points[0].y) + float(points[1].y)) / 2.0}
    else points[int((len(points) - 1) / 2)]
}

fn render_coordinate_label(node, shift_x, shift_y, basis_x, basis_y,
                           min_x, max_y, left, top, px_per_cm) any^ {
    let label = opts.value(node, "label", null)
    if (label == null) null
    else {
        let checked = opts.check(node, ["label"])^
        let colon = index_of(label, ":")
        if (colon == null) raise error("coordinate label needs placement:text")
        let side = trim(slice(label, 0, colon))
        let text = trim(slice(label, colon + 1, len(label)))
        let dx = if (side == "left") -12.0
            else if (side == "right") 12.0 else 0.0
        let dy = if (side == "above") -12.0
            else if (side == "below") 12.0 else 0.0
        if (side != "left" and side != "right" and
            side != "above" and side != "below")
            raise error("unsupported coordinate label placement: " ++ side)
        labels.positioned(labels.prepare(text)^,
            left + (float(node.x) * basis_x + shift_x - min_x) * px_per_cm + dx,
            top + (max_y - float(node.y) * basis_y - shift_y) * px_per_cm + dy)
    }
}

// Inline path nodes inherit the last parsed vertex, so labels follow the path
// even when the node appears after its final coordinate.
fn inline_path_labels(path, shift_x, shift_y, basis_x, basis_y,
                      index, current, previous, min_x, max_y,
                      left, top, px_per_cm, program_data, acc) any^ {
    if (index >= len(path)) acc
    else {
        let child = path[index]
        let tag = if (child is element) string(name(child)) else ""
        let next_previous = if (tag == "point") current else previous
        let next_point = if (tag == "point") child else current
        let target = if (tag == "node" and opts.has(child, "midway"))
            path_midpoint(path) else current
        let position = if (target == null) null
            else physical_point(target, shift_x, shift_y, basis_x, basis_y,
                program_data)^
        let start_position = if (previous == null) null
            else physical_point(previous, shift_x, shift_y, basis_x, basis_y,
                program_data)^
        let current_position = if (current == null) null
            else physical_point(current, shift_x, shift_y, basis_x, basis_y,
                program_data)^
        let tangent = if (path.shape == "ellipse") [0.0, 1.0]
            else if (start_position == null or current_position == null) null
            else [current_position.x - start_position.x,
                  current_position.y - start_position.y]
        let inherited_color = opts.color(path, "black")^
        let rendered = if (tag == "node" and position != null)
            render_label_node(child, position.x, position.y,
                min_x, max_y, left, top, px_per_cm,
                tangent, inherited_color)^
            else null
        inline_path_labels(path, shift_x, shift_y, basis_x, basis_y, index + 1,
            next_point, next_previous, min_x, max_y,
            left, top, px_per_cm, program_data,
            if (rendered == null) acc else [*acc, rendered])^
    }
}

fn positioned_nodes(records, index, min_x, max_y, left, top, px_per_cm, acc) any^ {
    if (index >= len(records)) acc
    else {
        let record = records[index]
        let child = record.source
        let tag = string(name(child))
        let position = if (tag == "node") physical_point(child, record.dx,
            record.dy, record.basis_x, record.basis_y, record.program)^
            else null
        let rendered = if (tag == "node")
            render_label_node(child, position.x, position.y,
                min_x, max_y, left, top, px_per_cm)^
        else if (tag == "coordinate")
                render_coordinate_label(child, record.dx, record.dy,
                    record.basis_x, record.basis_y,
                    min_x, max_y,
                    left, top, px_per_cm)^
            else null
        let path_offset = if (tag == "path")
            path_shift(child, record.basis_x, record.basis_y)^
            else [0.0, 0.0]
        let path_labels = if (tag == "path")
            inline_path_labels(child, record.dx + path_offset[0],
                record.dy + path_offset[1],
                record.basis_x, record.basis_y, 0, null, null,
                min_x, max_y, left, top, px_per_cm, record.program, [])^
            else []
        let next = (if (rendered == null) acc else [*acc, rendered]) ++ path_labels
        positioned_nodes(records, index + 1, min_x, max_y,
            left, top, px_per_cm, next)^
    }
}

fn render_drawing(picture, external_declarations = [],
                  custom_colors = [], clip_id = "tikz-clip-0") any^ {
    let x_basis = opts.dimension_px(opts.value(picture, "x", "1cm"))^ * 2.54 / 96.0
    let y_basis = opts.dimension_px(opts.value(picture, "y", "1cm"))^ * 2.54 / 96.0
    let initial_program = pgfmath.external_program(external_declarations)^
    let collected = collect_drawing(picture, 0.0, 0.0, 0.6,
        x_basis, y_basis, initial_program, 0, [])^
    let records = collected.records
    let coordinates = coordinate_definitions(records)
    let references = [for (record in records,
        point in if (string(name(record.source)) == "path")
            children_named(record.source, "point") else []
        where point.ref != null) point.ref]
    let missing = [for (reference in references
        where not any([for (coordinate in coordinates)
            coordinate.id == reference])) reference]
    if (len(missing) > 0) raise error("unknown TikZ coordinate: " ++ missing[0])
    let invalid_arcs = [for (record in records
        where string(name(record.source)) == "path" and
            record.source.arc_source != null and
            arc_points(record.source) == null) record.source]
    if (len(invalid_arcs) > 0)
        raise error("unsupported TikZ arc: " ++ invalid_arcs[0].arc_source)
    let arc_coordinates = [for (record in records,
        point in if (string(name(record.source)) == "path" and
            record.source.arc_source != null) arc_points(record.source) else [])
        arc_record_point(record, point)^]
    let path_coordinates = path_points(records, coordinates)^ ++ arc_coordinates
    let node_coordinates = node_points(records)
    let clips = [for (record in records where
        string(name(record.source)) == "path" and
        record.source.action == "clip") record]
    if (len(clips) > 1) raise error("multiple TikZ clips are unsupported")
    let clip_positions = [for (index, record in records where
        record.source.action == "clip") index]
    if (len(clips) == 1 and any([for (index, record in records)
        index < clip_positions[0] and (string(name(record.source)) == "path" or
            string(name(record.source)) == "node")]))
        raise error("TikZ clipping after drawing needs ordered clip groups")
    let clip_raw = if (len(clips) == 0) [] else
        path_data_points(clips[0].source, coordinates,
            clips[0].dx, clips[0].dy, clips[0].basis_x,
            clips[0].basis_y, clips[0].program)^
    let clip_points = if (len(clips) == 1 and clips[0].source.shape == "rectangle" and
        len(clip_raw) == 2)
        [{x: clip_raw[0].x, y: clip_raw[0].y}, {x: clip_raw[1].x, y: clip_raw[0].y},
         {x: clip_raw[1].x, y: clip_raw[1].y}, {x: clip_raw[0].x, y: clip_raw[1].y}]
        else clip_raw
    if (len(clips) == 1 and len(clip_points) < 3)
        raise error("TikZ clip needs a closed area")
    let ellipses = [for (record in records
        where record.source.shape == "ellipse") record]
    let ellipse_x = [for (record in ellipses) ellipse_extent(record, "x")^]
    let ellipse_y = [for (record in ellipses) ellipse_extent(record, "y")^]
    let x_values = if (len(clips) == 1)
        [for (point in clip_points) float(point.x)]
        else [for (point in [*path_coordinates, *node_coordinates]) float(point.x),
            for (bounds in ellipse_x, edge in bounds) float(edge)]
    let y_values = if (len(clips) == 1)
        [for (point in clip_points) float(point.y)]
        else [for (point in [*path_coordinates, *node_coordinates]) float(point.y),
            for (bounds in ellipse_y, edge in bounds) float(edge)]
    if (len(x_values) == 0) raise error("TikZ picture has no drawable coordinates")
    let min_x = float(min(x_values))
    let max_x = float(max(x_values))
    let min_y = float(min(y_values))
    let max_y = float(max(y_values))
    let scale = opts.numeric_value(opts.value(picture, "scale", "1"))^
    if (scale <= 0.0) raise error("TikZ picture scale must be positive")
    let px_per_cm = 96.0 / 2.54 * scale
    let left = 24.0
    let top = 24.0
    let width = (max_x - min_x) * px_per_cm + left * 2.0
    let height = (max_y - min_y) * px_per_cm + top * 2.0
    let paths = draw_paths(records, coordinates, 0,
        min_x, max_y, left, top, px_per_cm, custom_colors, [])^
    let clip_mapped = [for (point in clip_points)
        [left + (float(point.x) - min_x) * px_per_cm,
         top + (max_y - float(point.y)) * px_per_cm]]
    let label_elements = positioned_nodes(records, 0, min_x, max_y,
        left, top, px_per_cm, [])^
    let background_source = opts.value(picture, "background rectangle/.style", null)
    let background = if (opts.has(picture, "show background rectangle")) {
        if (background_source == null) "white"
        else if (starts_with(background_source, "fill="))
            opts.color_value(trim(slice(background_source, 5,
                len(background_source))))^
        else raise error("unsupported TikZ background rectangle style")
    } else null
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
        <defs
            <pattern id: "tikz-north-east-lines", patternUnits: "userSpaceOnUse",
                width: 6, height: 6,
                <path d: "M0,6 L6,0", stroke: "#444", 'stroke-width': 0.8>>
            if (len(clips) == 1)
                <clipPath id: clip_id,
                    <path d: svg.line_path(clip_mapped) ++ " Z">>
        >
        if (background != null) <rect x: 0, y: 0,
            width: width, height: height, fill: background>
        if (len(clips) == 1)
            <g 'clip-path': "url(#" ++ clip_id ++ ")",
                for (path in paths) path>
        else for (path in paths) path>
    let style = "position:relative;display:inline-block;width:" ++ string(width) ++
        "px;height:" ++ string(height) ++ "px;vertical-align:bottom;" ++
        (if (len(clips) == 0) "" else "clip-path:polygon(" ++ join([for (point in clip_mapped)
            string(point[0]) ++ "px " ++ string(point[1]) ++ "px"], ",") ++ ");");
    <span class: "tikz-picture", style: style,
        graphic
        for (label in label_elements) label
    >
}

fn render_picture(picture, options,
                  external_declarations = [], custom_colors = [],
                  clip_id = "tikz-clip-0", people_active = false) any^ {
    let tag = string(name(picture))
    // Named style values are checked on the nodes and paths where they take effect.
    let style_keys = [for (child in picture where child is element and
        string(name(child)) == "option" and ends_with(child.key, "/.style")) child.key]
    let picture_keys = [">", "scale", "show background rectangle", "background rectangle/.style",
        "help lines/.style", "color", "x", "y", "declare function"] ++
        style_keys ++
        (if (people_active) ["pin distance", "every pin/.append style"] else [])
    let valid = if (tag == "tikzpicture") opts.check(picture,
        picture_keys)^
        else if (tag == "tikz_picture") true
        else raise error("expected TikZ picture")
    let picture_color = opts.value(picture, "color", "black")
    if (picture_color != "black")
        raise error("unsupported TikZ picture default color: " ++ picture_color)
    let arrow_tip = opts.value(picture, ">", null)
    if (arrow_tip != null and arrow_tip != "stealth" and
        arrow_tip != "LaTeX")
        raise error("unsupported TikZ arrow tip")
    let axes = [for (child in picture
        where child is element and
            (string(name(child)) == "axis" or
             string(name(child)) == "semilogxaxis" or
             string(name(child)) == "semilogyaxis" or
             string(name(child)) == "loglogaxis" or
             string(name(child)) == "polaraxis")) child]
    let paths = drawing_nodes(picture)
    let content = [for (child in picture where child is element and
        string(name(child)) != "option") child]
    let named_refs = [for (child in paths,
        point in if (string(name(child)) == "path") children_named(child, "point") else []
        where point.ref != null) point]
    let named_nodes = [for (child in paths where string(name(child)) == "node" and
        (opts.has(child, "draw") or opts.has(child, "diamond") or
         len([for (reference in named_refs where reference.ref == child.id) reference]) > 0)) child]
    let people_nodes = if (people_active) [for (child in paths where
        string(name(child)) == "node" and
        len([for (shape in people.names() where opts.has(child, shape)) shape]) > 0)
        child] else []
    if (len(axes) == 1 and len(paths) == 0 and len(content) == 1)
        plots.render_axis(axes[0], options, picture)^
    else if (len(people_nodes) > 0)
        people.render_picture(picture, custom_colors)^
    else if (len(axes) == 0 and len(content) > 0 and
        len([for (child in content where string(name(child)) == "scope" or
            string(name(child)) == "path" or string(name(child)) == "node" or
            string(name(child)) == "coordinate" or
            string(name(child)) == "foreach" or
            string(name(child)) == "pgf_definition" or
            string(name(child)) == "tikzmath") child]) == len(content))
        if (len(named_nodes) > 0)
            named.render(picture)^
        else render_drawing(picture, external_declarations,
            custom_colors, clip_id)^
    else raise error("mixed or scoped TikZ pictures are not supported yet")
}

fn render_parsed(parsed, options = null,
                 external_declarations = [], custom_colors = [],
                 clip_prefix = "tikz-clip", people_active = false) any^ {
    let wrapper = children_named(parsed, "tikzpicture")
    if (len(wrapper) == 0) render_picture(parsed, options,
        external_declarations, custom_colors, clip_prefix ++ "-0",
        people_active)^
    else if (len(wrapper) == 1) render_picture(wrapper[0], options,
        external_declarations, custom_colors, clip_prefix ++ "-0",
        people_active)^
    else <div class: "tikz-fragment-gallery",
        for (index in 0 to (len(wrapper) - 1))
            <div style: "margin-bottom:20px;",
                render_picture(wrapper[index], options,
                    external_declarations, custom_colors,
                    clip_prefix ++ "-" ++ string(index), people_active)^>
    >
}

pub fn render(source) any^ {
    render_parsed(parse(source, {type: "tikz"})^, null)^
}

pub fn render_with_program(source, external_declarations,
                           custom_colors, source_offset = 0,
                           people_active = false) any^ {
    render_parsed(parse(source, {type: "tikz"})^, null,
        external_declarations, custom_colors,
        "tikz-clip-" ++ string(source_offset), people_active)^
}

// Direct .pgf documents use the parsed TikZ tree, including multiple pictures.
pub fn render_document(ast, options) any^ {
    let graphic = render_parsed(ast, options)^
    let math_stylesheet = math_css.get_stylesheet(options);
    <html lang: "en",
        <head
            <meta charset: "utf-8">
            <meta name: "viewport", content: "width=device-width, initial-scale=1">
            <title "TikZ/PGF Picture">
            <style math_stylesheet>
        >
        <body style: "margin:0;padding:24px;background:white;color:#222;" ++
            "font-family:Georgia,serif;",
            graphic>
    >
}

pub fn render_ast(graphics_island) any^ {
    if (graphics_island == null or graphics_island.raw_source == null)
        raise error("TikZ graphics island has no preserved source")
    render(graphics_island.raw_source)^
}
