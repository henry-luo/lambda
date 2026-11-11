// Token input levels and undelimited TeX argument scanning.
import reader: .reader

pub fn from_reader(r) => {reader: r, levels: [], everyeof: []}

pub fn push(stream, tokens) => if (len(tokens) == 0) stream
    else {*:stream, levels: [*stream.levels, {tokens: tokens, index: 0}]}

pub fn push_reader(stream, source_reader) =>
    {*:stream, levels: [*stream.levels, {reader: source_reader}]}

pub fn configure(stream, catcodes, endline) => {*:stream,
    reader: {*:stream.reader, catcodes: catcodes, endline: endline},
    levels: [for (level in stream.levels) if (level.reader == null) level
        else {*:level, reader: {*:level.reader, catcodes: catcodes, endline: endline}}]}

pub fn next(stream) any^ {
    let depth = len(stream.levels);
    if (depth == 0) {
        let result = reader.next(stream.reader);
        {stream: {*:stream, reader: if (result.reader == null) stream.reader else result.reader}, token: result.token}
    } else {
        let top = stream.levels[depth - 1];
        if (top.reader != null) {
            let result = reader.next(top.reader);
            let rest = slice(stream.levels, 0, depth - 1);
            if (result.token == null)
                next(push({*:stream, levels: rest}, stream.everyeof))
            else {stream: {*:stream, levels: [*rest, {*:top, reader: result.reader}]}, token: result.token}
        }
        else if (top.index >= len(top.tokens))
            next({*:stream, levels: slice(stream.levels, 0, depth - 1)})
        else {
            let updated = {*:top, index: top.index + 1};
            {stream: {*:stream, levels: [*slice(stream.levels, 0, depth - 1), updated]},
                token: top.tokens[top.index]}
        }
    }
}

fn is_character(token, cat) => token != null and token.kind == "char" and token.cat == cat
fn paragraph(token) => token != null and token.kind == "control" and token.value == "par"
fn finish(stream, tokens, code = null) => {stream: stream, tokens: tokens, error: code}
pub fn same_token(a, b) => a != null and b != null and a.kind == b.kind and a.value == b.value and
    (a.kind != "char" or a.cat == b.cat)

fn read_group(stream, depth, tokens, long_ok) any^ {
    let result = next(stream);
    let t = result.token;
    if (t == null) finish(result.stream, tokens, "tex-runaway-argument")
    else if (paragraph(t) and not long_ok)
        finish(push(result.stream, [t]), tokens, "tex-runaway-argument")
    else if (is_character(t, 2) and depth == 1) finish(result.stream, tokens)
    else read_group(result.stream,
        depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0,
        [*tokens, t], long_ok)
}

// TeX ignores leading spaces and removes one balanced outer group.
pub fn argument(stream, long_ok = false) any^ {
    let result = next(stream);
    let t = result.token;
    if (t == null) finish(result.stream, [], "tex-runaway-argument")
    else if (is_character(t, 10)) argument(result.stream, long_ok)
    else if (paragraph(t) and not long_ok)
        finish(push(result.stream, [t]), [], "tex-runaway-argument")
    else if (is_character(t, 2)) finish(push(result.stream, [t]), [], "tex-extra-brace")
    else if (is_character(t, 1)) read_group(result.stream, 1, [], long_ok)
    else finish(result.stream, [t])
}

fn matches_tail(tokens, top, delimiter, index = 0) any^ {
    if (index == len(delimiter)) true
    else {
        let at = len(tokens) - len(delimiter) + index;
        top[at] and same_token(tokens[at], delimiter[index]) and
            matches_tail(tokens, top, delimiter, index + 1)
    }
}

fn outer_group(tokens, index, depth) any^ {
    if (index >= len(tokens)) depth == 0
    else {
        let t = tokens[index];
        let next_depth = depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0;
        if (next_depth < 0 or (next_depth == 0 and index + 1 < len(tokens))) false
        else outer_group(tokens, index + 1, next_depth)
    }
}

fn strip_outer(tokens) any^ {
    let n = len(tokens);
    if (n >= 2 and is_character(tokens[0], 1) and is_character(tokens[n - 1], 2) and
        outer_group(tokens, 0, 0)) slice(tokens, 1, n - 1) else tokens
}

// Only top-level tokens can match a parameter delimiter.
fn delimited_scan(input, delimiter, depth, tokens, top, long_ok, brace_delim) any^ {
    let read = next(input);
    let t = read.token;
    if (t == null) finish(read.stream, tokens, "tex-runaway-argument")
    else if (paragraph(t) and not long_ok)
        finish(push(read.stream, [t]), tokens, "tex-runaway-argument")
    else if (is_character(t, 2) and depth == 0)
        finish(push(read.stream, [t]), tokens, "tex-extra-brace")
    else if (brace_delim and depth == 0 and is_character(t, 1) and
        len(tokens) >= len(delimiter) and matches_tail(tokens, top, delimiter))
        finish(read.stream, strip_outer(slice(tokens, 0, len(tokens) - len(delimiter))))
    else {
        let next_depth = depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0;
        let all_tokens = [*tokens, t];
        let all_top = [*top, next_depth == 0 and not is_character(t, 2)];
        if (not brace_delim and next_depth == 0 and len(all_tokens) >= len(delimiter) and
            matches_tail(all_tokens, all_top, delimiter))
            finish(read.stream, strip_outer(slice(all_tokens, 0, len(all_tokens) - len(delimiter))))
        else delimited_scan(read.stream, delimiter, next_depth, all_tokens, all_top, long_ok, brace_delim)
    }
}

pub fn delimited(input, delimiter, long_ok = false, brace_delim = false) any^ =>
    if (len(delimiter) == 0 and not brace_delim) argument(input, long_ok)
    else delimited_scan(input, delimiter, 0, [], [], long_ok, brace_delim)
