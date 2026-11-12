// Experimental native-compatible reconstruction of emitted TeX tokens.
import utf8: lambda.io.utf8

fn utf8_length(text) any^ => len(binary(text)^)
fn source_text(sources, file, start, end) any^ {
    let found = [for (source in sources where source.id == file) source];
    if (len(found) == 0 or start < 0 or end < start) null
    else {
        let bytes = binary(found[0].data)^;
        // stale spans can end inside a UTF-8 character; canonicalize those tokens.
        if (end > len(bytes)) null else utf8.decode(slice(bytes, start, end)^) ^ { null }
    }
}
fn catcode(c) => if (c == "\\") 0 else if (c == "{") 1 else if (c == "}") 2
    else if (c == "$") 3 else if (c == "&") 4 else if (c == "#") 6
    else if (c == "^") 7 else if (c == "_") 8 else if (c == "%") 14
    else if (c == "~") 13 else if (c == " " or c == "\t") 10
    else if ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z")) 11 else 12
fn whitespace(text) any^ => all([for (c in split(text, "")^) contains([" ", "\t", "\n", "\r"], c)])
fn faithful(token, raw) any^ {
    if (raw == null or raw == "") false
    else if (token.kind == "active") raw == token.value
    else if (token.kind == "control") raw == "\\" ++ token.value
    else if (token.kind != "char") false
    else if (token.cat == 10) whitespace(raw)^
    else raw == token.value and (if (token.value == "@" or ord(token.value) >= 128)
        token.cat == 11 or token.cat == 12 else token.cat == catcode(token.value))
}
fn canonical(token) any^ {
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
fn letter(c) => c != "" and ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or c == "@")
fn ending_letters(text, i) any^ => if (i > 0 and letter(slice(text, i - 1, i)^)) ending_letters(text, i - 1) else i
fn slash_count(text, i, count = 0) any^ => if (i > 0 and slice(text, i - 1, i)^ == "\\")
    slash_count(text, i - 1, count + 1) else count
fn needs_separator(text, next) any^ {
    let end = len(text);
    let begin = ending_letters(text, end)^;
    next != "" and letter(slice(next, 0, 1)^) and begin < end and slash_count(text, begin)^ % 2 == 1
}
fn append(st, data, src, synthetic) any^ {
    if (data == "") st else {*:st, text: st.text ++ data,
        segments: [*st.segments, {out_start: utf8_length(st.text)^, length: utf8_length(data)^,
            src_start: src, synthesized: synthetic}]}
}
fn separate(st, data, origin) any^ {
    if (not needs_separator(st.text, data)^) st
    else if (origin == null) {*:st, issue: "tex-reconstruct-missing-origin"}
    else append(st, " ", origin, true)
}
fn rebuild(st, tokens, sources, main, index) any^ {
    if (st.issue != null or index >= len(tokens)) st
    else {
        let item = tokens[index];
        let echo = item.echo_span;
        // echoed spans from other sources retain their main-file emission origin.
        let t = if (echo == null) item else {kind: "source-span", file: echo.file,
            start: echo.start, end: echo.end, seq: echo.first_seq, last_seq: echo.last_seq,
            read_offset: item.read_offset, emit_origin: item.emit_origin};
        let span = t.kind == "source-span";
        let direct = span or (t.direct == true and t.file != null and t.file >= 0);
        let adjacent = direct and st.prev_valid and st.prev_file == t.file and t.seq == st.prev_seq + 1;
        let gap = if (adjacent and t.start > st.prev_end) source_text(sources, t.file, st.prev_end, t.start)^ else "";
        let emission = if (t.emit_origin != null) t.emit_origin else t.read_offset;
        let gap_origin = if (t.file == main) st.prev_end else emission;
        let gap_state = if (gap == null or (gap != "" and gap_origin == null))
            {*:st, issue: "tex-reconstruct-missing-origin"}
            else append(st, gap, gap_origin, t.file != main)^;
        let raw = source_text(sources, t.file, t.start, t.end)^;
        let parline = t.kind == "control" and t.value == "par" and raw != null and raw != "\\par";
        let use_source = span or (faithful(t, raw)^ and (direct or t.kind == "control" or t.cat != 10));
        let body = if (item.reconstruct_text != null) item.reconstruct_text
            else if (parline) if (adjacent) raw else "\n\n" else if (use_source) raw else canonical(t)^;
        let main_direct = direct and t.file == main;
        let origin = if (main_direct) t.start else emission;
        let before = if (body == null or origin == null) {*:gap_state, issue: "tex-reconstruct-missing-origin"}
            else if (adjacent) gap_state else separate(gap_state, body, emission)^;
        let appended = if (before.issue != null) before else append(before, body, origin, not main_direct)^;
        let after = if (item.kind != "source-span" or appended.issue != null) appended
            else {*:appended, definition_spans: [*appended.definition_spans,
                {start: utf8_length(before.text)^, end: utf8_length(appended.text)^,
                    value: body, catcodes: item.catcodes}]};
        if (after.issue != null) {*:after, failed_index: index}
        else rebuild({*:after, prev_valid: direct, prev_file: t.file, prev_seq: if (span) t.last_seq else t.seq,
            prev_end: t.end}, tokens, sources, main, index + if (item.reconstruct_text != null) item.reconstruct_count
                else if (echo == null) 1 else item.echo_count)
    }
}

pub fn reconstruct(result) any^ {
    let built = rebuild({text: "", segments: [], definition_spans: [], issue: null, prev_valid: false},
        result.output, result.sources, result.main_file, 0)^;
    let tail = result.trailing_source;
    if (built.issue != null or tail == null or tail.text == "") built
    else {
        let adjacent = built.prev_valid and built.prev_file == tail.file and
            result.input.reader.seq + 1 == built.prev_seq + 1;
        let gap = if (adjacent and tail.start > built.prev_end)
            source_text(result.sources, tail.file, built.prev_end, tail.start)^ else "";
        let with_gap = if (gap == null) {*:built, issue: "tex-reconstruct-source-range"}
            else append(built, gap, built.prev_end, false)^;
        let before = if (adjacent) with_gap else separate(with_gap, tail.text, tail.start)^;
        if (before.issue != null) before else append(before, tail.text, tail.start, false)
    }
}

pub fn map_offset(segments, offset, index = 0) any^ {
    if (len(segments) == 0) offset
    else if (index + 1 < len(segments) and segments[index + 1].out_start <= offset)
        map_offset(segments, offset, index + 1)
    else {
        let seg = segments[index];
        if (seg.synthesized or offset < seg.out_start) seg.src_start
        else seg.src_start + min(offset - seg.out_start, seg.length)
    }
}
