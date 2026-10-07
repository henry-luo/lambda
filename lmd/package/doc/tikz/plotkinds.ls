// PGFPlots plot-type geometry: bars, stacking, error bars, closed areas and
// fill between. Data points are {x, y, errors}; screen geometry is axis px.
import opts: .options
import util: lambda.latex.util
import scale: lambda.chart.scale

let PT_PX = 96.0 / 72.27
let BAR_KEYS = ["ybar", "xbar", "ybar stacked", "xbar stacked"]
pub let ERROR_KEYS = ["y dir", "x dir", "y explicit", "x explicit", "y fixed",
    "x fixed", "error mark"]

pub let AXIS_KEYS = [*BAR_KEYS, "bar width"]

// Axis bar mode: direction, stacking, bar width and gap in px; null for no bars.
pub fn bar_mode(axis_node) any^ {
    let used = [for (key in BAR_KEYS where opts.has(axis_node, key)) key]
    if (len(used) == 0) null
    else if (len(used) > 1) raise error("PGFPlots axis has conflicting bar styles")
    else {
        let key = used[0]
        let gap_source = opts.value(axis_node, key, "")
        let width = opts.dimension_px(opts.value(axis_node, "bar width", "10pt"))^;
        {direction: if (starts_with(key, "y")) "y" else "x",
         stacked: ends_with(key, "stacked"), width: width,
         // `ybar=<gap>` sets the space between grouped bars; PGFPlots uses 2pt.
         gap: if (gap_source == "") 2.0 * PT_PX else opts.dimension_px(gap_source)^}
    }
}

fn category(point, direction) => if (direction == "y") point.x else point.y
fn magnitude(point, direction) => if (direction == "y") point.y else point.x

fn total_at(totals, key) {
    let matches = [for (entry in totals where entry.key == key) entry.value]
    if (len(matches) == 0) 0.0 else matches[0]
}

fn merge_totals(totals, updates) => [*[for (entry in totals
    where not any([for (update in updates) update.key == entry.key])) entry], *updates]

// Bar extents along the value axis: stacked plots start where earlier plots end.
pub fn stack_bars(series_list, mode, index = 0, totals = [], acc = []) {
    if (index >= len(series_list)) acc
    else {
        let series = series_list[index]
        if (series.kind != "bar")
            stack_bars(series_list, mode, index + 1, totals, [*acc, series])
        else {
            let points = [for (point in series.points)
                (let base = if (mode.stacked) total_at(totals, category(point, mode.direction))
                    else 0.0,
                 {*:point, base: base, top: base + magnitude(point, mode.direction)})]
            let updates = [for (point in points)
                {key: category(point, mode.direction), value: point.top}]
            stack_bars(series_list, mode, index + 1,
                if (mode.stacked) merge_totals(totals, updates) else totals,
                [*acc, {*:series, points: points}])
        }
    }
}

fn entry_value(entries, key, fallback) {
    let found = [for (entry in entries where entry.key == key) entry.value]
    if (len(found) == 0) fallback else found[len(found) - 1]
}

// Error bar settings from `error bars/.cd` key lists or full `error bars/...` keys.
pub fn error_spec(plot) any^ {
    let options = [for (child in plot where child is element and
        string(name(child)) == "option") {key: trim(child.key), value: child.value}]
    let cd_positions = [for (index, option in options where option.key == "error bars/.cd") index]
    let cd_at = if (len(cd_positions) == 0) null else cd_positions[0]
    let scoped = [for (index, option in options where cd_at != null and index > cd_at) option]
    let prefixed = [for (option in options where starts_with(option.key, "error bars/") and
        option.key != "error bars/.cd")
        {key: slice(option.key, len("error bars/"), len(option.key)), value: option.value}]
    let entries = [*scoped, *prefixed]
    let unknown = [for (entry in entries
        where not any([for (key in ERROR_KEYS) key == entry.key])) entry.key]
    let valid = if (len(unknown) > 0)
        raise error("unsupported PGFPlots error bars key: " ++ unknown[0]) else true
    let stray = [for (index, option in options where (cd_at == null or index < cd_at) and
        any([for (key in ERROR_KEYS) key == option.key])) option.key]
    let valid_scope = if (len(stray) > 0)
        raise error("PGFPlots error bar key needs error bars/.cd: " ++ stray[0]) else true
    let y_dir = entry_value(entries, "y dir", "none")
    let x_dir = entry_value(entries, "x dir", "none")
    let valid_dirs = if (not any([for (dir in ["none", "plus", "minus", "both"]) dir == y_dir]) or
        not any([for (dir in ["none", "plus", "minus", "both"]) dir == x_dir]))
        raise error("PGFPlots error bar direction must be none, plus, minus or both") else true
    let mark = entry_value(entries, "error mark", "-")
    let valid_mark = if (mark != "-" and mark != "|" and mark != "none")
        raise error("unsupported PGFPlots error mark: " ++ mark) else true
    let y_fixed = entry_value(entries, "y fixed", null)
    let x_fixed = entry_value(entries, "x fixed", null)
    let y_explicit = entry_value(entries, "y explicit", null) != null
    let x_explicit = entry_value(entries, "x explicit", null) != null
    let valid_source = if ((y_dir != "none" and not y_explicit and y_fixed == null) or
        (x_dir != "none" and not x_explicit and x_fixed == null))
        raise error("PGFPlots error bars need explicit or fixed errors") else true
    if (len(entries) == 0) null
    else {y_dir: y_dir, x_dir: x_dir, y_explicit: y_explicit, x_explicit: x_explicit,
        y_fixed: if (y_fixed == null) null else abs(opts.numeric_value(y_fixed)^),
        x_fixed: if (x_fixed == null) null else abs(opts.numeric_value(x_fixed)^),
        mark: mark != "none"}
}

// Signed data extents of a point's error bars along one axis.
pub fn error_extent(spec, point, axis) {
    let dir = if (spec == null) "none" else if (axis == "y") spec.y_dir else spec.x_dir
    let fixed = if (spec == null) null else if (axis == "y") spec.y_fixed else spec.x_fixed
    let plus = if (fixed != null) fixed
        else if (axis == "y") point.errors.plus_y else point.errors.plus_x
    let minus = if (fixed != null) fixed
        else if (axis == "y") point.errors.minus_y else point.errors.minus_x;
    {plus: if (dir == "plus" or dir == "both") plus else 0.0,
     minus: if (dir == "minus" or dir == "both") minus else 0.0,
     active: dir != "none"}
}

fn clamp(value, low, high) => max([low, min([high, value])])

// A bar along one screen axis with optional end caps; `fixed` is the other coordinate.
fn whisker(low, high, fixed, vertical, cap) {
    // A screen point `along` the whisker, offset `across` it.
    let at = (along, across) => if (vertical) [fixed + across, along] else [along, fixed + across]
    let main = [at(low, 0.0), at(high, 0.0)]
    if (cap <= 0.0) [main]
    else [main, [at(low, 0.0 - cap), at(low, cap)], [at(high, 0.0 - cap), at(high, cap)]]
}

// Error bar segments in px for one point drawn at screen position `at`.
pub fn error_segments(spec, point, at, xs, ys, width, height) {
    let y_extent = error_extent(spec, point, "y")
    let x_extent = error_extent(spec, point, "x")
    let cap = if (spec.mark) 3.0 else 0.0
    let vertical = if (not y_extent.active) []
        else whisker(clamp(float(scale.scale_apply(ys, point.y - y_extent.minus)), 0.0, height),
            clamp(float(scale.scale_apply(ys, point.y + y_extent.plus)), 0.0, height),
            at[0], true, cap)
    let horizontal = if (not x_extent.active) []
        else whisker(clamp(float(scale.scale_apply(xs, point.x - x_extent.minus)), 0.0, width),
            clamp(float(scale.scale_apply(xs, point.x + x_extent.plus)), 0.0, width),
            at[1], false, cap);
    vertical ++ horizontal
}

// Data-space extents a point contributes to the axis limits.
pub fn point_extents(spec, point, axis) {
    let extent = error_extent(spec, point, axis)
    let value = if (axis == "y") point.y else point.x;
    [value - extent.minus, value + extent.plus]
}

// One bar rectangle in px, clipped to the plot area; null when nothing remains.
pub fn bar_rect(point, mode, shift, xs, ys, width, height) {
    let half = mode.width / 2.0
    let center = if (mode.direction == "y") float(scale.scale_apply(xs, point.x)) + shift
        else float(scale.scale_apply(ys, point.y)) - shift
    let low = if (mode.direction == "y") float(scale.scale_apply(ys, point.base))
        else float(scale.scale_apply(xs, point.base))
    let high = if (mode.direction == "y") float(scale.scale_apply(ys, point.top))
        else float(scale.scale_apply(xs, point.top))
    let x0 = if (mode.direction == "y") center - half else min([low, high])
    let x1 = if (mode.direction == "y") center + half else max([low, high])
    let y0 = if (mode.direction == "y") min([low, high]) else center - half
    let y1 = if (mode.direction == "y") max([low, high]) else center + half
    let left = clamp(x0, 0.0, width)
    let right = clamp(x1, 0.0, width)
    let top = clamp(y0, 0.0, height)
    let bottom = clamp(y1, 0.0, height)
    if (right <= left or bottom <= top) null
    else {x: left, y: top, width: right - left, height: bottom - top,
        anchor: if (mode.direction == "y") [center, float(scale.scale_apply(ys, point.top))]
            else [float(scale.scale_apply(xs, point.top)), center]}
}

// Grouped bars sit side by side around their coordinate (PGFPlots bar shift auto).
pub fn bar_shift(mode, ordinal, total) =>
    if (mode.stacked) 0.0
    else (float(ordinal) - float(total - 1) / 2.0) * (mode.width + mode.gap)

fn crossing(a, b, axis, limit) {
    let t = (limit - a[axis]) / (b[axis] - a[axis]);
    [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
}

fn inside(point, axis, limit, keep_less) =>
    if (keep_less) point[axis] <= limit else point[axis] >= limit

// Output of one polygon vertex against the edge (Sutherland-Hodgman).
fn clip_vertex(points, index, axis, limit, keep_less) {
    let total = len(points)
    let current = points[index]
    let previous = points[(index + total - 1) % total]
    let current_in = inside(current, axis, limit, keep_less)
    let previous_in = inside(previous, axis, limit, keep_less)
    if (current_in and previous_in) [current]
    else if (current_in) [crossing(previous, current, axis, limit), current]
    else if (previous_in) [crossing(previous, current, axis, limit)]
    else []
}

// One Sutherland-Hodgman pass against an axis-aligned edge.
fn clip_against(points, axis, limit, keep_less) =>
    if (len(points) == 0) []
    else [for (index in 0 to (len(points) - 1),
        kept in clip_vertex(points, index, axis, limit, keep_less)) kept]

// A filled polygon clipped to the plot rectangle [0,width] x [0,height].
pub fn clip_polygon(points, width, height) =>
    clip_against(clip_against(clip_against(clip_against(points, 0, 0.0, false),
        0, width, true), 1, 0.0, false), 1, height, true)

// A closed area under a plot: down to the zero line, clamped to the plot area.
pub fn area_polygon(transformed, ys, height) {
    let base = clamp(float(scale.scale_apply(ys, 0.0)), 0.0, height)
    let first = transformed[0]
    let final_point = transformed[len(transformed) - 1];
    [for (part in [transformed, [[final_point[0], base], [first[0], base]]], point in part)
        point]
}

fn bound_crossings(point, following, low, high) => [for (bound in [low, high]
    where (point.x - bound) * (following.x - bound) < 0.0)
    {x: bound, y: point.y + (following.y - point.y) * (bound - point.x) / (following.x - point.x)}]

fn domain_piece(points, index, low, high) {
    let point = points[index]
    let kept = if (point.x >= low and point.x <= high) [point] else []
    if (index + 1 >= len(points)) kept
    else [*kept, *bound_crossings(point, points[index + 1], low, high)]
}

// Keep the parts of a polyline with x in [low, high], cutting at the bounds.
pub fn clip_domain(points, low, high) => [for (index in 0 to (len(points) - 1),
    point in domain_piece(points, index, low, high)) point]

// `soft clip={domain=a:b}` limits fill between to an x interval.
pub fn soft_clip(fill_options) any^ {
    let raw = opts.value(fill_options, "soft clip", null)
    if (raw == null) null
    else {
        let text = trim(raw)
        let valid = if (not starts_with(text, "domain="))
            raise error("unsupported PGFPlots soft clip: " ++ text) else true
        let parts = split(slice(text, len("domain="), len(text)), ":")
        if (len(parts) != 2) raise error("PGFPlots soft clip domain must be min:max")
        else [opts.numeric_value(parts[0])^, opts.numeric_value(parts[1])^]
    }
}

// `of=A and B` names two earlier paths.
pub fn fill_between_names(fill_options) any^ {
    let checked = opts.check(fill_options, ["of", "soft clip"])^
    let raw = opts.value(fill_options, "of", null)
    let parts = if (raw == null) [] else [for (part in split(" " ++ trim(raw) ++ " ", " and "))
        trim(part)]
    if (len(parts) != 2 or parts[0] == "" or parts[1] == "")
        raise error("PGFPlots fill between needs of=<first> and <second>")
    else parts
}
