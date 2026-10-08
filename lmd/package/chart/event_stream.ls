// Vega-Lite event selectors, evaluated within the chart's bounded event vocabulary.
import expr: .expression
import util: .util
import parse: .parse

let kinds = ["click", "dblclick", "pointerdown", "pointermove", "pointerup", "pointercancel",
    "pointerover", "pointerout", "pointerenter", "pointerleave", "mousedown", "mousemove", "mouseup",
    "mouseover", "mouseout", "mouseenter", "mouseleave", "wheel", "keydown", "keyup", "input", "change", "focusin", "focusout"]

fn split_parts(text, separator = ",", index = 0, begin = 0, depth = 0, quote = null, parts = []) {
    let ch = slice(text, index, index + 1);
    if (index >= len(text)) [*parts, trim(slice(text, begin, len(text)))]
    else if (quote != null) split_parts(text, separator, index + (if (ch == "\\") 2 else 1), begin, depth,
        if (ch == quote) null else quote, parts)
    else if (ch == "'" or ch == "\"") split_parts(text, separator, index + 1, begin, depth, ch, parts)
    else if (ch == separator and depth == 0) split_parts(text, separator, index + 1, index + 1, 0, null, [*parts, trim(slice(text, begin, index))])
    else split_parts(text, separator, index + 1, begin, depth + (if (ch == "[") 1 else if (ch == "]") -1 else 0), null, parts)
}

pub fn lifecycle(stream) {
    if (stream is array) {
        let lifecycles=[for (part in stream) lifecycle(part)];
        let failure=util.first_error(lifecycles);
        if (failure is error) failure else {start:lifecycles |> ~.start,end:lifecycles |> ~.end,move:lifecycles |> ~.move}
    } else if (stream is map and stream.between != null)
        if (len(stream.between) != 2) error("chart: interval between requires start and end streams")
        else {start: stream.between[0], end: stream.between[1], move: stream}
    else if (stream is string and starts_with(trim(stream), "[")) {
        let text = trim(stream);
        let segments = split_parts(text, ">");
        let head = trim(segments[0]);
        let parts = split_parts(slice(head, 1, len(head) - 1));
        if (len(segments) != 2 or not ends_with(head, "]") or len(parts) != 2) error("chart: interval event stream requires [start, end] > move")
        else {start: parts[0], end: parts[1], move: segments[1]}
    } else {start: if (stream != null) stream else "pointerdown", end: "pointerup", move: "pointermove"}
}

fn atom(text) {
    let raw = trim(text);
    let consume = ends_with(raw, "!");
    let source = if (consume) slice(raw, 0, len(raw) - 1) else raw;
    let bracket = index_of(source, "[");
    let event = if (bracket == null) source else slice(source, 0, bracket);
    let origin = split(event, ":");
    let filter = if (bracket == null) null else slice(source, bracket + 1, len(source) - 1);
    if (len(origin) > 2 or len(origin) == 2 and not contains(["window", "view"], origin[0]))
        error("chart: event source must be view or window")
    else if (bracket != null and not ends_with(source, "]")) error("chart: unterminated event filter")
    else {type: origin[len(origin) - 1], filter: filter, consume: consume}
}

pub fn validate(stream, bindings = null) {
    if (stream == null or stream == false or stream == true) null
    else if (stream is array) util.first_error([for (part in stream) validate(part, bindings)])
    else if (stream is string and starts_with(trim(stream), "[") or stream.between != null) {
        let parts = lifecycle(stream);
        if (parts is error) parts else util.first_error([validate(parts.start, bindings), validate(parts.end, bindings),
            validate(if (parts.move is map) {*:parts.move, between: null} else parts.move, bindings)])
    } else if (stream is string and len(split_parts(stream)) > 1) util.first_error([for (part in split_parts(stream)) validate(part, bindings)])
    else {
        let parsed = if (stream is string) atom(stream) else stream;
        if (parsed is error) parsed
        else if (not contains(kinds, parsed.type)) error("chart: unsupported interaction event " ++ string(parsed.type))
        else if (parsed.source != null and not contains(["view", "window"], parsed.source))
            error("chart: event source must be view or window")
        else if (parsed.throttle != null or parsed.debounce != null) error("chart: timed event streams are not supported")
        else util.first_error([for (filter in (if (parsed.filter is array) parsed.filter else [parsed.filter]) where filter != null)
            expr.compile(filter, {*:parse.attributes(bindings), event: {}})])
    }
}

fn event_kind(expected, actual) => expected == actual or
    (expected == "pointerover" and actual == "mouseover") or (expected == "pointerout" and actual == "mouseout") or
    (expected == "pointerenter" and actual == "mouseenter") or (expected == "pointerleave" and actual == "mouseleave")

pub fn matches(stream, event, bindings = null) {
    if (stream == false or stream == null) false
    else if (stream is array) any([for (part in stream) matches(part, event, bindings)])
    else if (stream is string and len(split_parts(stream)) > 1) any([for (part in split_parts(stream)) matches(part, event, bindings)])
    else {
        let parsed = if (stream is string) atom(stream) else stream;
        event_kind(parsed.type, event.type) and
            all([for (filter in (if (parsed.filter is array) parsed.filter else [parsed.filter]) where filter != null)
                expr.test(expr.compile(filter, {*:parse.attributes(bindings), event: event}), event.row)])
    }
}
