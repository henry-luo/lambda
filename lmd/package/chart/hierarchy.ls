// Hierarchy validation and layout retain source rows for encodings and tooltips.
import util: .util
import svg: .svg
import mark: .mark
import text: .text
import color: .color

fn ancestry(index, parents, visited = []) {
    if (index == null) true
    else if (contains(visited, index)) error("chart: hierarchy contains a parent cycle")
    else ancestry(parents[index], parents, [*visited, index])
}

fn node(index, data, parents, values, ids) {
    let children = [for (child in 0 to (len(data) - 1) where parents[child] == index) node(child, data, parents, values, ids)];
    {row: data[index], id: ids[index], children: children,
        value: if (len(children) > 0) sum(children |> ~.value) else values[index],
        height: if (len(children) > 0) 1 + max(children |> ~.height) else 0}
}

pub fn forest(data, options = {}) {
    let id_field = if (options.node_field != null) options.node_field else "id";
    let parent_field = if (options.parent_field != null) options.parent_field else "parent";
    let value_field = if (options.value_field != null) options.value_field else "value";
    let ids = data |> ~[id_field];
    let parents = [for (row in data) if (row[parent_field] == null) null else index_of(ids, row[parent_field])];
    let invalid_id = [for (id in ids where not (id is string or id is symbol or util.finite_number(id))) id];
    let missing_parent = [for (index, row in data where row[parent_field] != null and parents[index] == null) row];
    let values = [for (row in data) if (row[value_field] == null) 1.0 else row[value_field]];
    let invalid_value = [for (value in values where not util.finite_number(value) or value < 0) value];
    let cycles = util.first_error([for (index in 0 to (len(data) - 1)) ancestry(index, parents)]);
    if (len(invalid_id) > 0 or len(util.unique_vals(ids)) != len(ids)) error("chart: hierarchy requires unique string or finite numeric node IDs")
    else if (len(missing_parent) > 0) error("chart: hierarchy parent does not identify a node")
    else if (len(invalid_value) > 0) error("chart: hierarchy weights must be finite and nonnegative")
    else if (cycles is error) cycles
    else {
        let roots = [for (index in 0 to (len(data) - 1) where parents[index] == null) node(index, data, parents, values, ids)];
        if (len([for (root in roots where not util.finite_number(root.value)) true]) > 0) error("chart: hierarchy total weight exceeds the finite numeric range")
        else roots
    }
}

fn worst(row, side) {
    let areas = row |> ~.area;
    let total = sum(areas);
    max([side * side * max(areas) / (total * total), total * total / (side * side * min(areas))])
}

fn strip(row, rect) {
    let vertical = rect.width >= rect.height;
    let breadth = sum(row |> ~.area) / (if (vertical) rect.height else rect.width);
    let cells = [for (index, entry in row,
        let start = sum(slice(row, 0, index) |> ~.area) / breadth,
        let length = entry.area / breadth)
        {*:entry.node, x: rect.x + (if (vertical) 0.0 else start), y: rect.y + (if (vertical) start else 0.0),
            width: if (vertical) breadth else length, height: if (vertical) length else breadth}];
    {cells: cells, remaining: {x: rect.x + (if (vertical) breadth else 0.0), y: rect.y + (if (vertical) 0.0 else breadth),
        width: max(0.0, rect.width - (if (vertical) breadth else 0.0)), height: max(0.0, rect.height - (if (vertical) 0.0 else breadth))}}
}

// Greedily grow each strip while its worst aspect ratio improves.
fn squarify(rest, row, rect) {
    if (len(rest) == 0) if (len(row) == 0) [] else strip(row, rect).cells
    else if (len(row) == 0 or worst([*row, rest[0]], min(rect.width, rect.height)) <= worst(row, min(rect.width, rect.height)))
        squarify(slice(rest, 1, len(rest)), [*row, rest[0]], rect)
    else {
        let result = strip(row, rect);
        [*result.cells, *squarify(rest, [], result.remaining)]
    }
}

fn rectangles(nodes, rect, options, depth = 0, branch = null) {
    let positive = sort(nodes |: ~.value > 0, {by: (entry) => -entry.value});
    let total = sum(positive |> ~.value);
    let cells = if (total <= 0 or rect.width <= 0 or rect.height <= 0) [] else squarify(
        [for (entry in positive) {node: entry, area: entry.value / total * rect.width * rect.height}], [], rect);
    let padding = options.node_padding;
    let header = options.header_height;
    [for (index, cell in cells,
        let top = if (branch == null) index else branch,
        let inner = {x: cell.x + padding, y: cell.y + padding + header,
            width: max(0.0, cell.width - 2.0 * padding), height: max(0.0, cell.height - 2.0 * padding - header)}) (
        {*:cell, depth: depth, branch: top},
        *rectangles(cell.children, inner, options, depth + 1, top))]
}

fn partitions(nodes, start, end, inner, step, depth = 0, branch = null) {
    let positive = nodes |: ~.value > 0;
    let total = sum(positive |> ~.value);
    [for (index, entry in positive,
        let a = start + (end - start) * sum(slice(positive, 0, index) |> ~.value) / total,
        let b = a + (end - start) * entry.value / total,
        let top = if (branch == null) index else branch) (
        {*:entry, depth: depth, branch: top, start: a, end: b, inner: inner + step * depth, outer: inner + step * (depth + 1)},
        *partitions(entry.children, a, b, inner, step, depth + 1, top))]
}

pub fn layout(data, width, height, options = {}) {
    let settings = {node_padding: 1.0, header_height: 0.0, *:options};
    let roots = forest(data, settings);
    let inner = if (settings.inner_radius != null) settings.inner_radius else 0.0;
    let outer = if (settings.outer_radius != null) settings.outer_radius else min(width, height) / 2.0;
    let radial = settings.kind == "sunburst";
    if (roots is error) roots
    else if (not util.finite_number(width) or not util.finite_number(height) or width <= 0 or height <= 0)
        error("chart: hierarchy layout requires positive finite dimensions")
    else if (radial and (not util.finite_number(inner) or not util.finite_number(outer) or inner < 0 or outer < inner))
        error("chart: sunburst requires 0 <= inner_radius <= outer_radius")
    else if (not radial and (not util.finite_number(settings.node_padding) or settings.node_padding < 0 or
        not util.finite_number(settings.header_height) or settings.header_height < 0))
        error("chart: treemap padding and header height must be finite and nonnegative")
    else if (len(roots) == 0) []
    else if (radial) partitions(roots, 0.0, util.TAU, inner, (outer - inner) / float(1 + max(roots |> ~.height)))
    else rectangles(roots, {x: 0.0, y: 0.0, width: width, height: height}, settings)
}

fn radial_label_fits(cell, radius, angle, metric) {
    let x = radius * math.cos(angle);
    let y = radius * math.sin(angle);
    let left = x + metric.left - metric.width / 2.0;
    let right = x + metric.right - metric.width / 2.0;
    let top = y - metric.height / 2.0;
    let bottom = y + metric.height / 2.0;
    let closest_x = util.clamp_val(0.0, left, right);
    let closest_y = util.clamp_val(0.0, top, bottom);
    let outside = [for (px in [left, right], py in [top, bottom],
        let distance = math.sqrt(px * px + py * py),
        let a = math.atan2(py, px) + util.PI / 2.0,
        let normalized = a - util.TAU * floor(a / util.TAU)
        where distance > cell.outer or (cell.end - cell.start < util.TAU and (normalized < cell.start or normalized > cell.end))) true];
    len(outside) == 0 and closest_x * closest_x + closest_y * closest_y >= cell.inner * cell.inner
}

fn label(cell, ctx, options, font) {
    let field = if (options.label_field != null) options.label_field else if (options.node_field != null) options.node_field else "id";
    let title = string(cell.row[field]);
    let radial = options.kind == "sunburst";
    let radius = if (radial) (cell.inner + cell.outer) / 2.0 else 0.0;
    let angle = if (radial) (cell.start + cell.end) / 2.0 - util.PI / 2.0 else 0.0;
    let width = if (radial) 2.0 * radius * math.sin(min(util.PI, cell.end - cell.start) / 2.0) - 4.0 else cell.width - 6.0;
    let height = if (radial) cell.outer - cell.inner - 4.0
        else if (len(cell.children) > 0) min(cell.height - 6.0, options.header_height - 3.0) else cell.height - 6.0;
    let fit = if (width > 0 and height > 0) text.fit([title], font, width)[0] else null;
    if (fit is error) fit
    else if (fit == null or fit.text == "" or fit.metric.height > height or
        (radial and not radial_label_fits(cell, radius, angle, fit.metric))) null
    else <text class: "hierarchy-label", x: if (radial) ctx.plot_w / 2.0 + radius * math.cos(angle) else cell.x + 3.0 - fit.metric.left,
        y: if (radial) ctx.plot_h / 2.0 + radius * math.sin(angle) - (fit.metric.top + fit.metric.bottom) / 2.0 else cell.y + 3.0 - fit.metric.top,
        'text-anchor': if (radial) "middle" else "start", *:text.attributes(font),
        fill: if (options.label_color != null) options.label_color else "#222", fit.text>
}

pub fn render(data, ctx, options) {
    let settings = {*:options, value_field: if (options.value_field != null) options.value_field
        else if (ctx.encoding.size.field != null) ctx.encoding.size.field else "value"};
    let cells = layout(data, ctx.plot_w, ctx.plot_h, settings);
    let font = text.style(options, "label", 11);
    let labels = if (cells is error or options.labels == false) [] else [for (cell in cells
        where options.kind == "sunburst" or len(cell.children) == 0 or options.header_height > 0) label(cell, ctx, settings, font)];
    let failure = util.first_error([cells, *labels]);
    if (failure is error) failure else svg.group_class("marks " ++ options.kind, [
        for (cell in cells) (
            let appearance = mark.style(ctx, cell.row, options, {fill: color.category10[cell.branch % 10], stroke: "white", 'stroke-width': 1, opacity: 1.0}),
            if (options.kind == "sunburst") <path class: "sunburst-node", 'data-node': string(cell.id), 'data-value': cell.value,
                d: svg.arc_path(ctx.plot_w / 2.0, ctx.plot_h / 2.0, cell.inner, cell.outer, cell.start - util.PI / 2.0, cell.end - util.PI / 2.0),
                *:appearance, mark.tooltip(ctx, cell.row)>
            else <rect class: "treemap-node", 'data-node': string(cell.id), 'data-value': cell.value,
                x: cell.x, y: cell.y, width: cell.width, height: cell.height, *:appearance, mark.tooltip(ctx, cell.row)>),
        *labels])
}
