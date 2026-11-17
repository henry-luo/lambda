// Token input levels and undelimited TeX argument scanning.
import reader: .reader
let SourceRead = reader.ReadStep;

// A dynamic source reader is admitted once before stream traversal consumes it.
fn read_source(source: map) SourceRead | null | error => reader.next(source)

// Engine tokens include generated commands as well as reader tokens; extra fields survive snapshots.
pub type EngineToken = {kind: string, value?: any ! error, cat?: int?, start?: int, end?: int,
    file?: int?, direct?: bool, seq?: int, trace_internal?: bool}
pub type Level = {reader: map} | {tokens: map[], index: int}
pub type Trace = {valid: bool, file?: int, start?: int, first?: int, low?: int,
    high?: int, foreign?: bool, capture?: bool, captured?: map?, recent?: map}
pub type Stream = {reader: map, root_eof: bool, levels: Level[],
    everyeof: EngineToken[], last_main_offset: int, trace?: Trace?}
pub type StreamStep = {stream: Stream, token: EngineToken?}
pub type Argument = {stream: Stream, tokens: map[], error: string?, cause?: string}
pub type RawArgument = {stream: Stream, tokens: map[], raw: map[], error: string?}
pub type OptionalArgument = {stream: Stream, tokens: map[], raw: map[], present: bool, error: string?}
pub type RawReader = {index: int, reader: map}
pub type SourceTail = {file: int, start: int, text: string}
type InvalidTrace = {stream: Stream, valid: false}
type CompleteTrace = {stream: Stream, valid: true, file: int, start: int, end: int,
    first_seq: int, last_seq: int, tokens: EngineToken[]?}
pub type TraceResult = InvalidTrace | CompleteTrace
type TraceLimit = {limit: int, valid: bool}

pub fn from_reader(r: map) Stream => {reader: r, root_eof: false, levels: [], everyeof: [], last_main_offset: 0}

pub fn push(stream: Stream, tokens: map[]) Stream => if (len(tokens) == 0) stream
    else {*:stream, levels: [*stream.levels, {tokens: tokens, index: 0}]}

pub fn push_reader(stream: Stream, source_reader: map) Stream =>
    {*:stream, levels: [*stream.levels, {reader: source_reader}]}

fn active_index(levels: Level[], index: int) int => if (index < 0) -1
    else if (levels[index].reader != null) index else active_index(levels, index - 1)

pub fn active_reader(stream: Stream) map? {
    let index = active_index(stream.levels, len(stream.levels) - 1);
    if (index < 0) if (stream.root_eof == true) null else stream.reader else stream.levels[index].reader
}

// raw capture requires the current source reader with no tokens ahead of it.
pub fn raw_reader(stream: Stream, token: EngineToken) RawReader? {
    let index = active_index(stream.levels, len(stream.levels) - 1);
    let pending = any([for (i, level in stream.levels where i > index)
        level.reader == null and level.index < len(level.tokens)]);
    let source = if (index < 0) stream.reader else stream.levels[index].reader;
    if ((index < 0 and stream.root_eof == true) or pending or token.direct != true or source.file_id != token.file) null
    else {index: index, reader: source}
}

pub fn replace_reader(stream: Stream, index: int, source: map) Stream => if (index < 0) {*:stream, reader: source}
    else {*:stream, levels: [for (i, level in stream.levels)
        if (i == index) {*:level, reader: source} else level]}

pub fn end_input(stream: Stream) Stream {
    let index = active_index(stream.levels, len(stream.levels) - 1);
    if (index < 0) {*:stream, reader: {*:stream.reader, end_input: true}}
    else {*:stream, levels: [for (i, level in stream.levels)
        if (i == index) {*:level, reader: {*:level.reader, end_input: true}} else level]}
}

pub fn configure(stream: Stream, catcodes: map, endline: int) Stream => {*:stream,
    reader: {*:stream.reader, catcodes: catcodes, endline: endline},
    levels: [for (level in stream.levels) if (level.reader == null) level
        else {*:level, reader: {*:level.reader, catcodes: catcodes, endline: endline}}]}

fn trace_read(stream: Stream, token: EngineToken?) StreamStep^ {
    let trace = stream.trace;
    if (trace == null or trace.valid != true or token == null) {stream: stream, token: token}
    // generated tokens already have their originating source reads in the trace.
    else if (token.trace_internal == true) {stream: stream, token: token}
    else if (token.direct != true or token.file != trace.file or token.seq == null)
        {stream: {*:stream, trace: {*:trace, foreign: true}}, token: token}
    else {
        let slot = string(token.seq % 64);
        let next = {*:trace, low: if (token.seq < trace.low) token.seq else trace.low,
            high: if (token.seq > trace.high) token.seq else trace.high,
            recent: {*:trace.recent, *:map([slot, {seq: token.seq, end: token.end}])},
            captured: if (trace.capture) {*:trace.captured, *:map([string(token.seq), token])} else null};
        {stream: {*:stream, trace: next}, token: token}
    }
}

// macro-backed script primitives can expand tokens without pushing their bodies.
pub fn note_foreign_expansion(stream: Stream) Stream => if (stream.trace == null) stream
    else {*:stream, trace: {*:stream.trace, foreign: true}}

// trace only the reads of the current command; pushed tokens keep their sequence.
pub fn begin_trace(stream: Stream, token: EngineToken?, main_file: int, capture: bool = false) Stream {
    if (stream.trace != null or token == null or token.direct != true or token.file != main_file or token.seq == null)
        {*:stream, trace: {valid: false}}
    else {*:stream, trace: {valid: true, file: main_file, start: token.start, first: token.seq,
        low: token.seq, high: token.seq, foreign: false, capture: capture,
        captured: if (capture) map([string(token.seq), token]) else null,
        recent: map([string(token.seq % 64), {seq: token.seq, end: token.end}])}}
}

// Trace bounds only consider still-pending direct source tokens.
fn limit_tokens(tokens: map[], index: int, limit: int, main_file: int) int {
    var at = index, bound = limit
    while (at < len(tokens)) {
        let token = tokens[at];
        if (token.direct == true and token.file == main_file and token.seq != null and
            token.seq > 0 and token.seq <= bound) { bound = token.seq - 1 }
        at = at + 1
    }
    return bound
}

fn limit_levels(levels: Level[], index: int, limit: int, main_file: int, root: map) TraceLimit^ {
    if (index < 0) {limit: limit, valid: root.file_id == main_file}
    else if (levels[index].reader != null) {limit: limit, valid: levels[index].reader.file_id == main_file}
    else limit_levels(levels, index - 1,
        limit_tokens(levels[index].tokens, levels[index].index, limit, main_file), main_file, root)^
}

// retain the full token sequence separately from the bounded trace ring.
fn captured_tokens(trace: Trace, index: int, limit_value: int, out: map[] = []) EngineToken[] | null | error {
    if (index > limit_value) out
    else {
        let token = trace.captured[string(index)];
        if (token == null) null
        else captured_tokens(trace, index + 1, limit_value, [*out, token])^
    }
}

pub fn end_trace(stream: Stream) TraceResult^ {
    let trace = stream.trace;
    let cleared = {*:stream, trace: null};
    if (trace == null or trace.valid != true) {stream: cleared, valid: false}
    else {
        let remaining = limit_levels(stream.levels, len(stream.levels) - 1, trace.high, trace.file, stream.reader)^;
        let end = trace.recent[string(remaining.limit % 64)];
        if (trace.foreign or trace.low < trace.first or not remaining.valid or remaining.limit < trace.first or
            end == null or end.seq != remaining.limit) {stream: cleared, valid: false}
        else {stream: cleared, valid: true, file: trace.file, start: trace.start, end: end.end,
            first_seq: trace.first, last_seq: end.seq,
            tokens: if (trace.capture) captured_tokens(trace, trace.first, end.seq)^ else null}
    }
}

// only fresh main-file reads advance the origin; pushed-back tokens do not.
fn record_main_read(stream: Stream, token: EngineToken?) Stream =>
    if (token != null and token.direct == true and token.file == stream.reader.file_id)
        {*:stream, last_main_offset: token.start} else stream

pub fn next(stream: Stream) StreamStep^ {
    let depth = len(stream.levels);
    if (depth == 0) {
        let result = read_source(stream.reader)^^;
        // preserve the root reader for source data, while recording that it is exhausted.
        let updated = {*:stream, reader: if (result == null) stream.reader else result.reader,
            root_eof: result == null};
        trace_read(record_main_read(updated, result.token), result.token)^
    } else {
        let top = stream.levels[depth - 1];
        if (top.reader != null) {
            let result = read_source(top.reader)^^;
            let rest = slice(stream.levels, 0, depth - 1);
            if (result == null)
                next(push({*:stream, levels: rest}, stream.everyeof))^
            else {
                let updated = {*:stream, levels: [*rest, {*:top, reader: result.reader}]};
                trace_read(record_main_read(updated, result.token), result.token)^
            }
        }
        else if (top.index >= len(top.tokens))
            next({*:stream, levels: slice(stream.levels, 0, depth - 1)})^
        else {
            let updated = {*:top, index: top.index + 1};
            trace_read({*:stream, levels: [*slice(stream.levels, 0, depth - 1), updated]}, top.tokens[top.index])^
        }
    }
}

fn is_character(token, cat: int) bool => token != null and token.kind == "char" and token.cat == cat
fn paragraph(token) bool => token != null and token.kind == "control" and token.value == "par"
fn finish(stream: Stream, tokens: map[], code: string? = null, cause: string? = null) Argument => {stream: stream, tokens: tokens, error: code,
    *:if (cause == null) {} else {cause:cause}}
pub fn same_token(a, b) bool => a != null and b != null and a.kind == b.kind and a.value == b.value and
    (a.kind != "char" or a.cat == b.cat)

fn read_group(stream: Stream, depth: int, tokens: map[], long_ok: bool) Argument^ {
    let result = next(stream)^;
    let t = result.token;
    if (t == null) finish(result.stream, tokens, "tex-runaway-argument", "eof")
    else if (paragraph(t) and not long_ok)
        finish(push(result.stream, [t]), tokens, "tex-runaway-argument", "paragraph")
    else if (is_character(t, 2) and depth == 1) finish(result.stream, tokens)
    else read_group(result.stream,
        depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0,
        [*tokens, t], long_ok)^
}

// TeX ignores leading spaces and removes one balanced outer group.
pub fn argument(stream: Stream, long_ok: bool = false) Argument^ {
    let result = next(stream)^;
    let t = result.token;
    if (t == null) finish(result.stream, [], "tex-runaway-argument", "missing")
    else if (is_character(t, 10)) argument(result.stream, long_ok)^
    else if (paragraph(t) and not long_ok)
        finish(push(result.stream, [t]), [], "tex-runaway-argument", "paragraph")
    else if (is_character(t, 2)) finish(push(result.stream, [t]), [], "tex-extra-brace", "brace")
    else if (is_character(t, 1)) read_group(result.stream, 1, [], long_ok)^
    else finish(result.stream, [t])
}

fn matches_tail(tokens: map[], top: bool[], delimiter: map[], index: int = 0) bool^ {
    if (index == len(delimiter)) true
    else {
        let at = len(tokens) - len(delimiter) + index;
        top[at] and same_token(tokens[at], delimiter[index]) and
            matches_tail(tokens, top, delimiter, index + 1)^
    }
}

fn outer_group(tokens: map[], index: int, depth: int) bool^ {
    if (index >= len(tokens)) depth == 0
    else {
        let t = tokens[index];
        let next_depth = depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0;
        if (next_depth < 0 or (next_depth == 0 and index + 1 < len(tokens))) false
        else outer_group(tokens, index + 1, next_depth)^
    }
}

pub fn strip_outer(tokens: map[]) EngineToken[]^ {
    let n = len(tokens);
    if (n >= 2 and is_character(tokens[0], 1) and is_character(tokens[n - 1], 2) and
        outer_group(tokens, 0, 0)^) slice(tokens, 1, n - 1) else tokens
}

// Only top-level tokens can match a parameter delimiter.
fn delimited_scan(input: Stream, delimiter: map[], depth: int, tokens: map[], top: bool[], long_ok: bool, brace_delim: bool) Argument^ {
    let read = next(input)^;
    let t = read.token;
    if (t == null) finish(read.stream, tokens, "tex-runaway-argument", "eof")
    else if (paragraph(t) and not long_ok)
        finish(push(read.stream, [t]), tokens, "tex-runaway-argument", "paragraph")
    else if (is_character(t, 2) and depth == 0)
        finish(push(read.stream, [t]), tokens, "tex-extra-brace", "brace")
    else if (brace_delim and depth == 0 and is_character(t, 1) and
        len(tokens) >= len(delimiter) and matches_tail(tokens, top, delimiter)^)
        finish(read.stream, strip_outer(slice(tokens, 0, len(tokens) - len(delimiter)))^)
    else {
        let next_depth = depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0;
        let all_tokens = [*tokens, t];
        let all_top = [*top, next_depth == 0 and not is_character(t, 2)];
        if (not brace_delim and next_depth == 0 and len(all_tokens) >= len(delimiter) and
            matches_tail(all_tokens, all_top, delimiter)^)
            finish(read.stream, strip_outer(slice(all_tokens, 0, len(all_tokens) - len(delimiter)))^)
        else delimited_scan(read.stream, delimiter, next_depth, all_tokens, all_top, long_ok, brace_delim)^
    }
}

pub fn delimited(input: Stream, delimiter: map[], long_ok: bool = false, brace_delim: bool = false) Argument^ =>
    if (len(delimiter) == 0 and not brace_delim) argument(input, long_ok)^
    else delimited_scan(input, delimiter, 0, [], [], long_ok, brace_delim)^

fn raw_group(input: Stream, depth: int, inner: map[], raw: map[]) RawArgument^ {
    let read = next(input)^;
    let t = read.token;
    if (t == null) {stream: read.stream, tokens: inner, raw: raw, error: "tex-runaway-argument"}
    else if (is_character(t, 2) and depth == 1)
        {stream: read.stream, tokens: inner, raw: [*raw, t], error: null}
    else raw_group(read.stream, depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0,
        [*inner, t], [*raw, t])^
}

pub fn raw_argument(input: Stream) RawArgument^ {
    let read = next(input)^;
    let t = read.token;
    if (t == null) {stream: read.stream, tokens: [], raw: [], error: "tex-runaway-argument"}
    else if (is_character(t, 10)) raw_argument(read.stream)^
    else if (is_character(t, 2)) {stream: push(read.stream, [t]), tokens: [], raw: [], error: "tex-extra-brace"}
    else if (is_character(t, 1)) raw_group(read.stream, 1, [], [t])^
    else {stream: read.stream, tokens: [t], raw: [t], error: null}
}

pub fn root_tail(input: Stream) SourceTail? {
    let pending = [for (level in input.levels where
        ((level.reader != null or level.index < len(level.tokens)) ^ { false })) level];
    if (len(pending) > 0) null
    else {
        let r = input.reader;
        let position = if (r.loaded) r.line_start + r.position else r.next_line;
        {file: r.file_id, start: if (r.loaded) r.line_byte + r.position_byte else r.next_byte,
            text: slice(r.source, position, len(r.source))}
    }
}

fn bracket_tail(input: Stream, depth: int, tokens: map[], raw: map[]) OptionalArgument^ {
    let read = next(input)^;
    let t = read.token;
    if (t == null) {stream: read.stream, tokens: tokens, raw: raw, present: true, error: "tex-runaway-argument"}
    else if (t.kind == "char" and t.cat == 12 and t.value == "]" and depth == 0)
        {stream: read.stream, tokens: strip_outer(tokens)^, raw: [*raw, t], present: true, error: null}
    else if (is_character(t, 2) and depth == 0)
        {stream: push(read.stream, [t]), tokens: tokens, raw: raw, present: true, error: "tex-extra-brace"}
    else bracket_tail(read.stream, depth + if (is_character(t, 1)) 1 else if (is_character(t, 2)) -1 else 0,
        [*tokens, t], [*raw, t])^
}

pub fn optional_argument(input: Stream) OptionalArgument^ {
    let read = next(input)^;
    let t = read.token;
    if (is_character(t, 10)) optional_argument(read.stream)^
    else if (t != null and t.kind == "char" and t.cat == 12 and t.value == "[")
        bracket_tail(read.stream, 0, [], [t])^
    else {stream: if (t == null) read.stream else push(read.stream, [t]), tokens: [], raw: [], present: false, error: null}
}
