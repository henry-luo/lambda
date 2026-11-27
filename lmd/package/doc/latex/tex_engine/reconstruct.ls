// Experimental native-compatible reconstruction of emitted TeX tokens.
import utf8: lambda.io.utf8
import scan: lambda.io.source_scan
import reader: .reader
pub type Segment = {out_start: int, length: int, src_start: int, synthesized: bool}
pub type Fragment = {span: bool, direct: bool, adjacent: bool, gap: string?, body: string?}
pub type Reconstruction = {text: string, segments: Segment[], definition_spans: map[],
    issue: string?, prev_valid: bool, prev_file?: int?, prev_seq?: int?, prev_end: int,
    track_tokens?: bool, token_spans?: map[], failed_index?: int}
type Builder = {chunks: map?, length: int, definition_spans: map?, token_spans: map?,
    track_tokens: bool, issue: string?, failed_index?: int, previous: map,
    odd_slashes: bool, control_word_end: bool}

// The source remains immutable throughout reconstruction and digestion.
pub fn cache_sources(sources: map[]) array^ => [for (source in sources)
    {*:source, reconstruct_bytes:binary(source.data) ^ { null }}]
pub fn source_bytes(source) any^ => if (source.reconstruct_bytes is binary) source.reconstruct_bytes
    else binary(source.data)
fn find_source(sources: map[], file: int?, at: int = 0) map | null | error {
    if (at >= len(sources)) null
    else {
        let source = sources[at];
        if (source.id == file) source else find_source(sources, file, at + 1)
    }
}
fn source_text(source: map?, start: int, end: int) string | null | error {
    if (source == null or start < 0 or end < start) null
    else {
        let bytes = source_bytes(source)^;
        // stale spans can end inside a UTF-8 character; canonicalize those tokens.
        if (end > len(bytes)) null else utf8.decode(slice(bytes, start, end)) ^ { null }
    }
}
fn catcode(c: string) int => if (c == "\\") 0 else if (c == "{") 1 else if (c == "}") 2
    else if (c == "$") 3 else if (c == "&") 4 else if (c == "#") 6
    else if (c == "^") 7 else if (c == "_") 8 else if (c == "%") 14
    else if (c == "~") 13 else if (c == " " or c == "\t") 10
    else if ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z")) 11 else 12
fn whitespace(text: string) bool => all([for (c in (split(text, "") or [])^) contains([" ", "\t", "\n", "\r"], c)])
fn faithful(token: map, raw: string?) bool^ {
    if (raw == null or raw == "") false
    else if (token.kind == "active") raw == token.value
    else if (token.kind == "control") raw == "\\" ++ token.value
    else if (token.kind != "char") false
    else if (token.cat == 10) whitespace(raw)
    else raw == token.value and (if (token.value == "@" or ord(token.value) >= 128)
        token.cat == 11 or token.cat == 12 else token.cat == catcode(token.value))
}
fn canonical(token: map) string | null | error {
    if (token.kind == "active") token.value
    else if (token.kind == "control") {
        if (token.value == "par") "\n\n"
        else if (token.value == "") "\\csname\\endcsname"
        else "\\" ++ token.value
    }
    else if (token.kind != "char") null
    else if (token.cat == 1) "{" else if (token.cat == 2) "}" else if (token.cat == 3) "$"
    else if (token.cat == 4) "&" else if (token.cat == 6) "#" else if (token.cat == 7) "^"
    else if (token.cat == 8) "_" else if (token.cat == 10) " "
    else if (token.value == "\\") "\\textbackslash{}"
    else if (token.value == "{") "\\{" else if (token.value == "}") "\\}"
    else if (token.value == "$") "\\$" else if (token.value == "&") "\\&"
    else if (token.value == "#") "\\#" else if (token.value == "^") "\\textasciicircum{}"
    else if (token.value == "_") "\\_" else if (token.value == "%") "\\%"
    else if (token.value == "~") "\\textasciitilde{}" else token.value
}
fn letter_byte(byte: int) bool => scan.ascii_alpha(byte) or byte == 64
fn suffix_start(bytes: binary?, end: int, word: bool) int^ {
    var at = end;
    while (at > 0) {
        let byte = bytes[at - 1];
        if ((if (word) not letter_byte(byte) else byte != 92)^) { break }
        at = at - 1
    }
    return at
}

pub fn slash_count(text: string, i: int, count: int = 0) int^ {
    if (i <= 0 or i > len(text)) count
    else {
        let prefix = binary(reader.text_span(text, 0, i))^;
        let size = len(prefix);
        count + size - suffix_start(prefix, size, false)^
    }
}

fn append(st: Builder, data: string, src: int?, synthetic: bool) Builder^ {
    if (data == "") st
    else {
        let bytes = binary(data)^;
        let length = len(bytes);
        let word_start = suffix_start(bytes, length, true)^;
        let slashes = word_start - suffix_start(bytes, word_start, false)^;
        // A suffix reaching the chunk boundary inherits the preceding escape state.
        let odd = (slashes % 2 == 1) != (slashes == word_start and st.odd_slashes);
        let ends_in_letters = word_start < length;
        let segment: Segment = {out_start:st.length, length:length, src_start:src, synthesized:synthetic};
        {*:st, chunks:scan.add(st.chunks,{text:data,segment:segment}), length:st.length + length,
            odd_slashes:not ends_in_letters and odd,
            control_word_end:ends_in_letters and (odd or (word_start == 0 and st.control_word_end))}
    }
}
fn separate(st: Builder, data: string, origin: int?) Builder^ {
    if (not st.control_word_end or not letter_byte(ord(reader.text_span(data, 0, 1)) or 0)) st
    else if (origin == null) {*:st, issue: "tex-reconstruct-missing-origin"}
    else append(st, " ", origin, true)
}

// share emitted source spelling with source-aware digestion.
pub fn source_fragment(t: map, sources: map[], prev_valid: bool = false, prev_file: int? = null, prev_seq: int? = null, prev_end: int = 0) Fragment^ {
    let span = t.kind == "source-span";
    let direct = span or (t.direct == true and t.file != null and t.file >= 0);
    let adjacent = direct and prev_valid and prev_file == t.file and t.seq == prev_seq + 1;
    // The token spelling and any gap belong to the same source.
    let source = if (t.file == null) null else find_source(sources, t.file)^;
    let gap = if (adjacent and t.start > prev_end) source_text(source, prev_end, t.start)^ else "";
    // synthetic parser events have no source range to read.
    let raw = if (t.file == null or t.start == null or t.end == null) null
        else source_text(source, t.start, t.end)^;
    let parline = t.kind == "control" and t.value == "par" and raw != null and raw != "\\par";
    let use_source = span or (faithful(t, raw)^ and (direct or t.kind == "control" or t.cat != 10));
    {span:span,direct:direct,adjacent:adjacent,gap:gap,
        body:if (parline) if (adjacent) raw else "\n\n" else if (use_source) raw else canonical(t)^}
}

// raw echoes occupy their full captured source span, not just the command token.
pub fn effective_source_token(item: map) map => if (item.echo_span == null) item else {
    kind:"source-span", file:item.echo_span.file, start:item.echo_span.start, end:item.echo_span.end,
    seq:item.echo_span.first_seq, last_seq:item.echo_span.last_seq,
    read_offset:item.read_offset, emit_origin:item.emit_origin}

fn rebuild(st: Builder, tokens: map[], sources: map[], main: int, index: int) Builder^ {
    if (st.issue != null or index >= len(tokens)) st
    else {
        let item = tokens[index];
        let echo = item.echo_span;
        // echoed spans from other sources retain their main-file emission origin.
        let t = effective_source_token(item);
        let previous = st.previous;
        let piece = source_fragment(t,sources,previous.prev_valid,previous.prev_file,previous.prev_seq,previous.prev_end)^;
        let span = piece.span;
        let direct = piece.direct;
        let adjacent = piece.adjacent;
        let gap = piece.gap;
        let emission = if (t.emit_origin != null) t.emit_origin else t.read_offset;
        let gap_origin = if (t.file == main) previous.prev_end else emission;
        let gap_state = if (gap == null or (gap != "" and gap_origin == null))
            {*:st, issue: "tex-reconstruct-missing-origin"}
            else append(st, gap, gap_origin, t.file != main)^;
        let body = if (item.reconstruct_text != null) item.reconstruct_text
            else piece.body;
        let main_direct = direct and t.file == main;
        let origin = if (main_direct) t.start else emission;
        let before = if (body == null or origin == null) {*:gap_state, issue: "tex-reconstruct-missing-origin"}
            else if (adjacent) gap_state else separate(gap_state, body, emission)^;
        let appended = if (before.issue != null) before else append(before, body, origin, not main_direct)^;
        let after = if (item.kind != "source-span" or appended.issue != null) appended
            else {*:appended, definition_spans: scan.add(appended.definition_spans,
                {start:before.length, end:appended.length, value:body, catcodes:item.catcodes})};
        let count = if (item.reconstruct_text != null) item.reconstruct_count
            else if (echo == null) 1 else item.echo_count;
        // spans exclude gaps; coalesced output has no interior token positions.
        let marked = if (after.issue != null or st.track_tokens != true) after else
            {*:after, token_spans: scan.add(after.token_spans, {index:index, count:count,
                start:before.length, end:appended.length})};
        if (marked.issue != null) {*:marked, failed_index: index}
        else rebuild({*:marked, previous:{prev_valid:direct, prev_file:t.file, prev_seq:if (span) t.last_seq else t.seq,
            prev_end:t.end}}, tokens, sources, main, index + count)
    }
}

fn append_tail(built: Builder, result: map, sources: map[]) Builder^ {
    let tail = result.trailing_source;
    if (built.issue != null or tail == null or tail.text == "") built
    else {
        let previous = built.previous;
        let adjacent = previous.prev_valid and previous.prev_file == tail.file and
            result.input.reader.seq + 1 == previous.prev_seq + 1;
        let gap = if (adjacent and tail.start > previous.prev_end)
            source_text(find_source(sources, tail.file)^, previous.prev_end, tail.start)^ else "";
        let with_gap = if (gap == null) {*:built, issue: "tex-reconstruct-source-range"}
            else append(built, gap, previous.prev_end, false)^;
        let before = if (adjacent) with_gap else separate(with_gap, tail.text, tail.start)^;
        if (before.issue != null) before else append(before, tail.text, tail.start, false)
    }
}

pub fn reconstruct(result: map, track_tokens: bool = false) Reconstruction^ {
    let sources = cache_sources(result.sources)^;
    let built = rebuild({chunks:null, length:0, definition_spans:null, token_spans:null, track_tokens:track_tokens,
            issue:null, previous:{prev_valid:false, prev_end:0}, odd_slashes:false, control_word_end:false},
        result.output, sources, result.main_file, 0)^;
    let completed = append_tail(built, result, sources)^;
    // Materialize the collected fragments once, in emission order.
    let chunks = scan.finish(completed.chunks)^;
    let previous = completed.previous;
    {text:join([for (chunk in chunks) chunk.text], ""),
        segments:[for (chunk in chunks) chunk.segment], definition_spans:scan.finish(completed.definition_spans)^,
        issue:completed.issue, prev_valid:previous.prev_valid, prev_end:previous.prev_end,
        *:if (track_tokens) {track_tokens:true, token_spans:scan.finish(completed.token_spans)^} else {},
        *:if ('prev_file' at previous) {prev_file:previous.prev_file, prev_seq:previous.prev_seq} else {},
        *:if (completed.failed_index != null) {failed_index:completed.failed_index} else {}}
}

// Appended segments have increasing output offsets, so locate the last match.
fn segment_index(segments: Segment[], offset: int, low: int, high: int) int^ {
    if (low >= high) low
    else {
        let middle = low + (high - low + 1) div 2;
        if (segments[middle].out_start <= offset) segment_index(segments, offset, middle, high)
        else segment_index(segments, offset, low, middle - 1)
    }
}

pub fn map_offset(segments: Segment[], offset: int, index: int = 0) int^ {
    if (len(segments) == 0) offset
    else {
        let found = segment_index(segments, offset, index, len(segments) - 1)^;
        let seg = segments[found];
        if (seg.synthesized or offset < seg.out_start) seg.src_start
        else seg.src_start + min(offset - seg.out_start, seg.length)
    }
}
