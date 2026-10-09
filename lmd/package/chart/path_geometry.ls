// Absolute SVG segments and adaptive polylines shared by projection and morphing.
import util: .util
import geometry: .geometry
import svg: .svg

type token = \(a | ("+" | "-")? (d+ ("." d*)? | "." d+) (("e" | "E") ("+" | "-")? d+)?)
type separator = \((s | ",")*)
let sizes = {M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7, Z: 0}

fn endpoint(values, offset, current, relative) => [for (axis in [0, 1]) values[offset + axis] + (if (relative) current[axis] else 0.0)]
fn reflected(control, current) => if (control == null) current else [2.0 * current[0] - control[0], 2.0 * current[1] - control[1]]

fn segment(command, values, current, start, previous) {
    let kind = upper(command);
    let relative = command != kind;
    let end = if (contains(["M", "L", "T"], kind)) endpoint(values, 0, current, relative)
        else if (kind == "H") [values[0] + (if (relative) current[0] else 0.0), current[1]]
        else if (kind == "V") [current[0], values[0] + (if (relative) current[1] else 0.0)]
        else if (kind == "Z") start
        else endpoint(values, if (kind == "A") 5 else if (kind == "C") 4 else 2, current, relative);
    let control = if (kind == "Q") endpoint(values, 0, current, relative)
        else if (kind == "T") reflected(if (previous.original == "Q" or previous.original == "T") previous.control else null, current) else null;
    let a = if (kind == "C") endpoint(values, 0, current, relative)
        else if (kind == "S") reflected(if (previous.original == "C" or previous.original == "S") previous.b else null, current)
        else if (control != null) geometry.interpolate(current, control, 2.0 / 3.0) else null;
    let b = if (kind == "C" or kind == "S") endpoint(values, if (kind == "C") 2 else 0, current, relative)
        else if (control != null) geometry.interpolate(end, control, 2.0 / 3.0) else null;
    {kind: if (contains(["H", "V"], kind)) "L" else if (contains(["S", "Q", "T"], kind)) "C" else kind,
        original: kind, begin: current, end: end, a: a, b: b, control: control,
        radius: if (kind == "A") [abs(values[0]), abs(values[1])] else null,
        rotation: if (kind == "A") values[2] * util.PI / 180.0 else null,
        large: if (kind == "A") values[3] else null, sweep: if (kind == "A") values[4] else null}
}

fn command_token(value) => value is string and len(value)==1 and value is \(a)
fn operands(tokens,count,arc,index=0,result=[]) {
    if (index>=count) {values:result,rest:tokens} else if (len(tokens)==0 or command_token(tokens[0])) error("chart: missing SVG path operand")
    else {
        let text=tokens[0];let flag=arc and contains([3,4],index);
        let value=if (flag) float(slice(text,0,1)) else float(text);
        let remaining=if (flag and len(text)>1) [slice(text,1,len(text)),*slice(tokens,1,len(tokens))] else slice(tokens,1,len(tokens));
        if (not util.finite_number(value) or flag and not contains([0,1],value)) error("chart: invalid SVG path operand or arc flag")
        else operands(remaining,count,arc,index+1,[*result,value])
    }
}
fn segments(tokens, command = null, current = [0.0, 0.0], start = null, result = []) {
    if (len(tokens)==0) result else {
        let explicit = command_token(tokens[0]);
        let cmd = if (explicit) tokens[0] else command;
        let kind = upper(cmd);
        let count = sizes[kind];
        let rest=if (explicit) slice(tokens,1,len(tokens)) else tokens;
        let parsed=if (count!=null) operands(rest,count,kind=="A") else null;
        let values=parsed.values;
        if (cmd == null or count == null or (len(result) == 0 and kind != "M") or
            (kind == "Z" and not explicit) or parsed is error or len(values) != count)
            error("chart: invalid SVG path command or operands")
        else if (kind == "A" and (not contains([0, 1], values[3]) or not contains([0, 1], values[4])))
            error("chart: SVG arc flags must be zero or one")
        else {
            let entry = segment(cmd, values, current, start, result[len(result) - 1]);
            segments(parsed.rest, if (kind == "Z") null else if (cmd == "M") "L" else if (cmd == "m") "l" else cmd,
                entry.end, if (kind == "M") entry.end else start, [*result, entry])
        }
    }
}

pub fn parse(source) {
    if (not (source is string)) error("chart: SVG path must be a string")
    else if (not (replace(source, token, "") is separator)) error("chart: invalid character in SVG path")
    else segments(find(source,token) |> ~.value)
}

// SVG 2 Appendix B.2 endpoint-to-center conversion, including radius correction.
fn arc(segment) {
    let phi = segment.rotation; let cp = math.cos(phi); let sp = math.sin(phi);
    let dx = (segment.begin[0] - segment.end[0]) / 2.0; let dy = (segment.begin[1] - segment.end[1]) / 2.0;
    let x = cp * dx + sp * dy; let y = -sp * dx + cp * dy;
    let original = segment.radius;
    let factor = math.sqrt(max(1.0, x * x / (original[0] ** 2) + y * y / (original[1] ** 2)));
    let rx = original[0] * factor; let ry = original[1] * factor;
    let denominator = rx * rx * y * y + ry * ry * x * x;
    let ratio = (if (segment.large == segment.sweep) -1.0 else 1.0) *
        math.sqrt(max(0.0, (rx * rx * ry * ry - denominator) / denominator));
    let px = ratio * rx * y / ry; let py = -ratio * ry * x / rx;
    let center = [cp * px - sp * py + (segment.begin[0] + segment.end[0]) / 2.0,
        sp * px + cp * py + (segment.begin[1] + segment.end[1]) / 2.0];
    let a = [(x - px) / rx, (y - py) / ry]; let b = [(-x - px) / rx, (-y - py) / ry];
    let start = math.atan2(a[1], a[0]);
    let delta = math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]);
    {center: center, radius: [rx, ry], start: start,
        delta: if (segment.sweep == 0 and delta > 0) delta - util.TAU else if (segment.sweep == 1 and delta < 0) delta + util.TAU else delta,
        cp: cp, sp: sp}
}

fn arc_point(model, t) {
    let a = model.start + model.delta * t;
    let x = model.radius[0] * math.cos(a); let y = model.radius[1] * math.sin(a);
    [model.center[0] + model.cp * x - model.sp * y, model.center[1] + model.sp * x + model.cp * y]
}

pub fn point(segment, t) {
    if (t == 0) segment.begin else if (t == 1) segment.end
    else if (segment.kind == "C") [for (axis in [0, 1])
        (1.0 - t) ** 3 * segment.begin[axis] + 3.0 * (1.0 - t) ** 2 * t * segment.a[axis] +
            3.0 * (1.0 - t) * t * t * segment.b[axis] + t ** 3 * segment.end[axis]]
    else if (segment.kind == "A" and segment.radius[0] > 0 and segment.radius[1] > 0 and segment.begin != segment.end) arc_point(arc(segment), t)
    else geometry.interpolate(segment.begin, segment.end, t)
}

pub fn distance(a, b) => math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2)

fn subdivide(curve, begin, end, precision, remaining) {
    let a = curve(begin); let b = curve(end);
    let samples = [for (t in [0.25, 0.5, 0.75]) curve(util.lerp(begin, end, t))];
    let invalid = util.first_error([a, b, *samples]);
    let errors = [for (i, t in [0.25, 0.5, 0.75]) distance(samples[i], geometry.interpolate(a, b, t))];
    if (invalid is error) invalid
    else if (not all([for (point in [a, b, *samples]) point is array and len(point) >= 2 and all(point |> util.finite_number(~))]))
        error("chart: projected curve must have finite coordinates")
    else if (max(errors) <= precision) [a, b]
    else if (remaining <= 0) error("chart: curve exceeds projection precision")
    else {
        let mid = (begin + end) / 2.0;
        let left = subdivide(curve, begin, mid, precision, remaining - 1);
        let right = subdivide(curve, mid, end, precision, remaining - 1);
        if (left is error) left else if (right is error) right else [*left, *slice(right, 1, len(right))]
    }
}

pub fn sample_curve(curve, precision = 0.75, intervals = 1) {
    if (not util.finite_number(precision) or precision <= 0 or not util.finite_number(intervals) or intervals < 1)
        error("chart: curve sampling requires positive precision and intervals")
    else {
        let pieces = [for (i in 0 to (int(ceil(intervals)) - 1)) subdivide(curve,
            float(i) / ceil(intervals), float(i + 1) / ceil(intervals), precision, 20)];
        let invalid = util.first_error(pieces);
        if (invalid is error) invalid else [for (i, piece in pieces) for (j, point in piece where i == 0 or j > 0) point]
    }
}

fn polylines(segments, project, precision, intervals, index = 0, result = []) {
    if (index >= len(segments)) result else {
        let segment = segments[index];
        let points = if (segment.kind == "M") [project(segment.end)]
            else sample_curve((t) => project(point(segment, t)), precision, intervals);
        if (points is error) points else {
            let previous = result[len(result) - 1];
            // an already closed curve needs the Z flag without another zero-length edge.
            let tail = if (segment.kind == "Z" and segment.begin == segment.end) [] else slice(points, 1, len(points));
            let next = if (segment.kind == "M" or previous.closed==true) [*result, {points: if (segment.kind=="M") points else
                [project(segment.begin),*tail], closed: segment.kind=="Z"}]
                else [*slice(result, 0, len(result) - 1), {*:previous,
                    points: [*previous.points, *tail], closed: segment.kind == "Z"}];
            polylines(segments, project, precision, intervals, index + 1, next)
        }
    }
}

pub fn sample(source, project = (point) => point, precision = 0.75, intervals = 1) {
    let segments = if (source is array) source else parse(source);
    if (segments is error) segments else polylines(segments, project, precision, intervals)
}

pub fn path(polylines) => join([for (line in polylines) svg.line_path(line.points) ++ (if (line.closed) " Z" else "")], " ")

fn segment_length(segment, end = 1.0) {
    let points = sample_curve((t) => point(segment, t * end), 0.1);
    if (points is error) points else sum([for (i in 1 to (len(points) - 1)) distance(points[i - 1], points[i])])
}

fn length_parameter(segment, length, lo = 0.0, hi = 1.0, remaining = 20) {
    let mid = (lo + hi) / 2.0;
    let measured = segment_length(segment, mid);
    if (measured is error) measured else if (remaining == 0) mid
    else if (measured < length) length_parameter(segment, length, mid, hi, remaining - 1)
    else length_parameter(segment, length, lo, mid, remaining - 1)
}

// de Casteljau subdivision retains the original cubic rather than replacing it with a polyline.
fn segment_path(segment, end = 1.0) {
    let destination = point(segment, end);
    if (segment.kind == "M") svg.M(destination[0], destination[1])
    else if (segment.kind == "Z" and end == 1.0) "Z"
    else if (segment.kind == "C") (
        let a = geometry.interpolate(segment.begin, segment.a, end),
        let b = geometry.interpolate(a, geometry.interpolate(segment.a, segment.b, end), end),
        svg.C(a[0], a[1], b[0], b[1], destination[0], destination[1]))
    else if (segment.kind == "A" and segment.radius[0] > 0 and segment.radius[1] > 0 and segment.begin != segment.end) (
        let model = arc(segment),
        svg.A(model.radius[0], model.radius[1], segment.rotation * 180.0 / util.PI,
            if (abs(model.delta * end) > util.PI) 1 else 0, segment.sweep, destination[0], destination[1]))
    else svg.L(destination[0], destination[1])
}

fn end_direction(segment) {
    if (segment.kind == "C") (
        let controls = [segment.b, segment.a, segment.begin] |: ~ != segment.end,
        if (len(controls) == 0) [0.0, 0.0] else [for (axis in [0, 1]) segment.end[axis] - controls[0][axis]])
    else if (segment.kind == "A" and segment.radius[0] > 0 and segment.radius[1] > 0 and segment.begin != segment.end) (
        let model = arc(segment), let angle = model.start + model.delta,
        let x = -model.radius[0] * math.sin(angle), let y = model.radius[1] * math.cos(angle),
        [(model.cp * x - model.sp * y) * model.delta, (model.sp * x + model.cp * y) * model.delta])
    else [for (axis in [0, 1]) segment.end[axis] - segment.begin[axis]]
}

fn path_cut(entries, segments, remaining) {
    let cuts = [for (i, entry in entries,
        let after = sum(slice(entries, i + 1, len(entries)) |> ~.length)
        where entry.length > 0 and after < remaining and after + entry.length >= remaining)
        {index: entry.index, t: if (entry.length + after == remaining) 0.0 else
            length_parameter(segments[entry.index], entry.length + after - remaining)}];
    cuts[0]
}

// Arrow tips remain at the true endpoint; the head follows the tail of the original curve.
pub fn arrow_geometry(source, length, width = 0.0) {
    let segments = parse(source);
    if (segments is error) segments
    else if (not util.finite_number(length) or length < 0 or not util.finite_number(width) or width < 0)
        error("chart: arrow size and stroke width must be finite and nonnegative")
    else if (length == 0 or len(segments) == 0) {body: source, length: 0.0}
    else {
        let start = max([for (index, segment in segments where segment.kind == "M") index]);
        let entries = [for (index, segment in segments where index > start)
            {index: index, length: segment_length(segment)}];
        let failure = util.first_error(entries |> ~.length);
        if (failure is error) failure else {
            let positive = entries |: ~.length > 0;
            let total = sum(entries |> ~.length);
            if (total == 0) {body: source, length: 0.0} else {
                let actual = min(length, total);
                let head_cut = path_cut(entries, segments, actual);
                let cut = path_cut(entries, segments, actual * 0.85);
                let body = join([for (index, segment in segments where index < start or
                    (actual < total and index <= cut.index)) segment_path(segment, if (index == cut.index) cut.t else 1.0)], " ");
                let terminal = segments[positive[len(positive) - 1].index];
                let direction = end_direction(terminal);
                let pieces = [for (entry in positive where entry.index >= head_cut.index,
                    let segment = segments[entry.index], let begin = if (entry.index == head_cut.index) head_cut.t else 0.0)
                    // offset flanks need finer sampling than the centerline near a tight terminal bend.
                    sample_curve((t) => point(segment, util.lerp(begin, 1.0, t)), 0.01)];
                let failure = util.first_error([cut.t, head_cut.t, *pieces]);
                let centers = [for (i, piece in pieces) for (j, point in piece where i == 0 or j > 0) point];
                let from = [for (axis in [0, 1]) terminal.end[axis] - direction[axis]];
                let half_width = max(actual * 0.375, if (actual < total) width * 0.9 else 0.0);
                if (failure is error) failure else {body: body, to: terminal.end, from: from,
                    length: actual, half_width: half_width,
                    head: svg.arrow_head(from[0], from[1], terminal.end[0], terminal.end[1], null,
                        actual, false, half_width, centers).d}
            }
        }
    }
}
