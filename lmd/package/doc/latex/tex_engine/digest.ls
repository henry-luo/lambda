// Experimental digestion of a bounded set of expanded LaTeX tokens.
// Unsupported structure is reported until the corresponding parser is ported.
import utf8: lambda.io.utf8
import tex_reader: .reader
import rebuild: .reconstruct

// Structural parsing keeps partial position and output when it reports an unsupported construct.
type Parsed = {index: int, issue: string?, tokens?: map[], nodes?: array,
    items?: array, unsupported?: string?}
pub type Digested = {ast: element?, issue: string?, unsupported?: string?, complete?: bool}
type GroupScan = {issue: string?, end: int, index?: int, tokens?: map[], start_offset?: int,
    end_offset?: int, file?: int?, direct?: bool}
type OptionalScan = {issue: string?, end?: int, index?: int, present?: bool, tokens?: map[],
    start_offset?: int, end_offset?: int, file?: int?, direct?: bool, unsupported?: string?}
fn control(token, name) => token != null and token.kind == "control" and token.value == name
fn opening(token) => token != null and token.kind == "char" and token.cat == 1
fn closing(token) => token != null and token.kind == "char" and token.cat == 2
fn block(token) any^ => token != null and token.kind == "control" and
    contains(["begin", "end", "documentclass", "usepackage", "title", "author", "date", "maketitle", "tableofcontents", "part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"], token.value)
fn unsupported(index, name) => {index: index, issue: "latex-digest-unsupported", unsupported: name}

// These commands have native syntax or output shapes beyond generic arguments.
fn special_command(name: string) bool => tex_reader.definition_command(name) or contains(["verb", "lstinline", "Verb", "mint", "mintinline",
    "directlua", "luaexec", "setlength", "underline", "quad", "qquad"], name) or
    (len(name) == 1 and contains(["\\", "$", "%", "#", "&", "_", "{", "}",
        "'", "`", "^", "\"", "~", "=", ".", "u", "v", "H", "c", "d", "b", "t", "k", ",", ";", ":", "!"], name))

fn inline_format_command(name) => contains(["textbf", "textit", "emph", "texttt", "textsf", "textrm", "underline"], name)
// These arguments are stored code, so unmatched environment boundaries are literal.
fn code_fragment_command(name) => contains(["pretitle", "posttitle", "preauthor", "postauthor", "predate", "postdate"], name)

fn group_end(tokens: map[], index: int, depth: int) GroupScan^ {
    // Group failures can propagate through inline parsing, which requires an index.
    if (index >= len(tokens)) {end: index, index: index, issue: "latex-digest-unclosed-group"}
    else {
        let token = tokens[index];
        let next = depth + if (opening(token)) 1 else if (closing(token)) -1 else 0;
        if (next == 0) {end: index + 1, issue: null}
        else group_end(tokens, index + 1, next)
    }
}

fn read_group(tokens: map[], index: int) GroupScan^ {
    if (index >= len(tokens) or not opening(tokens[index]))
        {issue: "latex-digest-expected-group", end: index, index: index}
    else {
        let end = group_end(tokens, index + 1, 1)^;
        if (end.issue != null) end
        else {issue: null, end: end.end, tokens: slice(tokens, index + 1, end.end - 1),
            start_offset: tokens[index].start, end_offset: tokens[end.end - 1].end,
            file: tokens[index].file, direct: tokens[index].direct == true and tokens[end.end - 1].direct == true and
                tokens[index].file == tokens[end.end - 1].file}
    }
}

fn bracket_end(tokens: map[], index: int) OptionalScan^ {
    if (index >= len(tokens)) {issue: "latex-digest-unclosed-optional", end: index}
    else if (tokens[index].kind == "char" and tokens[index].value == "]") {issue: null, end: index + 1}
    else if (tokens[index].kind != "char" or opening(tokens[index]) or closing(tokens[index]) or tokens[index].value == "[")
        unsupported(index, "optional argument")
    else bracket_end(tokens, index + 1)
}

fn read_optional(tokens: map[], index: int) OptionalScan^ {
    if (index >= len(tokens) or tokens[index].kind != "char" or tokens[index].value != "[")
        {issue: null, present: false, end: index}
    else {
        let end = bracket_end(tokens, index + 1)^;
        if (end.issue != null) end
        else {issue: null, present: true, end: end.end,
            tokens: slice(tokens, index + 1, end.end - 1),
            start_offset: tokens[index].start, end_offset: tokens[end.end - 1].end,
            file: tokens[index].file, direct: tokens[index].direct == true and tokens[end.end - 1].direct == true and
                tokens[index].file == tokens[end.end - 1].file}
    }
}

fn plain_group(group) => not any([for (token in group.tokens) token.kind != "char" or token.cat == 1 or token.cat == 2])
fn group_text(group) => join([for (token in group.tokens) token.value], "")
fn append_text(items, text) => if (text == "") items else [*items, text]

fn inline_element(name, offset, children) => <[name] source_offset: offset, *children>

fn escaped_text_name(token) => if (token.kind != "char") null
    else if (token.value == "\\") "textbackslash"
    else if (token.value == "^") "textasciicircum"
    else if (token.value == "~") "textasciitilde" else null

// native reparses canonical character escapes as commands with an empty group.
fn escaped_text_command(token) any^ {
    let name = escaped_text_name(token);
    if (name == null or token.output_text == null or token.output_start == null or token.output_end == null) false
    else utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ == "\\" ++ name ++ "{}"
}
fn escaped_text_node(token) any^ {
    let name = escaped_text_name(token);
    let start = rebuild.map_offset(token.output_segments,token.output_start)^;
    let group_start = rebuild.map_offset(token.output_segments,
        token.output_start + len(binary("\\" ++ name)^))^;
    let group_end = rebuild.map_offset(token.output_segments,token.output_end)^;
    <[name] source_offset:start, argument_groups:[{kind:"required",raw:"",start:group_start,end:group_end}],<curly_group>>
}

// nested native groups scan raw inline bytes before the inline command parser.
fn inline_group_safe(bytes, nested) any^ {
    if (not nested) true else {
        let curly = scan_group_bytes(bytes,0,1,false,false,123,125)^;
        let bracket = scan_group_bytes(bytes,0,1,false,false,91,93)^;
        let paren = scan_group_bytes(bytes,0,1,false,false,40,41)^;
        all([for (scan in [curly,bracket,paren])
            not scan.closed and scan.depth == 1 and not scan.comment and not scan.escaped])
    }
}

// A complete group can balance delimiters across multiple verbatim commands.
fn inline_group_verbatim_safe(tokens) any^ {
    if (len(tokens) == 0 or not any([for (token in tokens) token.raw_verb == true])) false
    else {
        let first = tokens[0];
        let final = tokens[len(tokens) - 1];
        if (first.output_text == null or first.output_start == null or final.output_end == null or
            final.output_text != first.output_text or final.output_end < first.output_start) false
        else inline_group_safe(slice(binary(first.output_text)^, first.output_start, final.output_end)^, true)
    }
}

fn inline_code_node(name, offset, starred, options, language, raw, body) =>
    <[name] source_offset:offset, *:if(starred){starred:true}else{},
        *:if(options==null){}else{options_raw:options}, *:if(language==null){}else{language:language},
        raw_source:raw, *(if(body=="")[] else [body])>

// native parses the reconstructed raw command, with its offset mapped to the source.
fn verb_element(token, index, nested) any^ {
    if (token.value != "verb" or token.output_text == null or token.output_start == null or token.output_end == null or
            token.output_segments == null) unsupported(index, "verbatim source mapping")
    else {
        let bytes = slice(binary(token.output_text)^,token.output_start,token.output_end);
        let raw = utf8.decode(bytes) ^ {null};
        let starred = len(bytes) > 5 and bytes[5] == 42;
        let delimiter = if (starred) 6 else 5;
        if (raw == null or not starts_with(raw,"\\verb") or len(bytes) < delimiter + 2 or
                bytes[delimiter] != bytes[len(bytes) - 1]) unsupported(index, "verbatim source mapping")
        else {
            let safe = inline_group_safe(bytes,nested)^;
            let body = utf8.decode(slice(bytes,delimiter + 1,len(bytes) - 1)) ^ {null};
            let offset = rebuild.map_offset(token.output_segments,token.output_start)^;
            if (body == null or not safe) unsupported(index, "verbatim group source mapping")
            else {issue:null,node:inline_code_node("verb",offset,starred,null,null,raw,body)}
        }
    }
}

fn inline_raw_group(bytes, at, open_byte, close_byte) any^ {
    if (at >= len(bytes) or bytes[at] != open_byte) null
    else {
        let scanned = scan_group_bytes(bytes,at + 1,1,false,false,open_byte,close_byte)^;
        if (not scanned.closed) null
        else {end:scanned.offset,raw:utf8.decode(slice(bytes,at + 1,scanned.offset - 1)) ^ {null}}
    }
}
fn inline_delimiter_end(bytes, at, delimiter) any^ {
    if (at >= len(bytes) or bytes[at] == 10 or bytes[at] == 13) null
    else if (bytes[at] == delimiter) at + 1
    else inline_delimiter_end(bytes,at + 1,delimiter)
}

// A percent inside a raw command is safe when its comment cannot hide an environment boundary.
fn raw_comment_tail_safe(token) any^ {
    if (token.output_text == null or token.output_end == null) false
    else {
        let bytes = binary(token.output_text)^;
        let tail = utf8.decode(slice(bytes, token.output_end, len(bytes)))^;
        let newline = index_of(tail, "\n");
        let carriage = index_of(tail, "\r");
        let end = min(if (newline == null) len(tail) else newline, if (carriage == null) len(tail) else carriage);
        let comment = slice(tail, 0, end)^;
        not contains(comment, "\\begin{") and not contains(comment, "\\end{")
    }
}

// the enclosing native environment scan also sees these raw command bytes.
fn raw_command_safe(bytes, raw, nested, token) any^ {
    let env_scan = scan_group_bytes(bytes,0,1,false,false,-1,-1)^;
    raw != null and inline_group_safe(bytes,nested)^ and (not env_scan.comment or raw_comment_tail_safe(token)^) and
        not env_scan.escaped and not contains(raw,"\\begin{") and not contains(raw,"\\end{") and
        not contains(raw,"\\verb")
}

fn raw_group_element(token, index, nested) any^ {
    let name = token.raw_group_kind;
    if (name == null or token.value != name or token.output_text == null or token.output_start == null or
            token.output_end == null or token.output_segments == null) unsupported(index,"raw group source mapping")
    else {
        let bytes = slice(binary(token.output_text)^,token.output_start,token.output_end);
        let raw = utf8.decode(bytes) ^ {null};
        let prefix = "\\" ++ name;
        let prefix_end = len(binary(prefix)^);
        let first = list_first_nonspace(bytes,prefix_end)^;
        let group = inline_raw_group(bytes,first,123,125)^;
        let output_bytes = binary(token.output_text)^;
        let next = list_first_nonspace(output_bytes,token.output_end)^;
        // native retains whitespace when the command has no following group.
        let no_group = group == null and first == len(bytes) and
            (next == len(output_bytes) or output_bytes[next] != 123);
        if (raw == null or not starts_with(raw,prefix) or
                (not no_group and (group == null or group.end != len(bytes) or group.raw == null)) or
                not raw_command_safe(bytes,raw,nested,token)^)
            unsupported(index,"raw group source mapping")
        else {
            let offset = rebuild.map_offset(token.output_segments,token.output_start)^;
            let tail = if (no_group) utf8.decode(slice(bytes,prefix_end,len(bytes)))^ else "";
            {issue:null,node:<[name] source_offset:offset, *:if(no_group){}else{source:group.raw}>, tail:tail}
        }
    }
}

// listings and minted nodes parse their reconstructed options, language and code.
fn raw_inline_element(token, index, nested) any^ {
    let name = token.raw_inline_kind;
    if (name == null or token.value != name or token.output_text == null or token.output_start == null or
            token.output_end == null or token.output_segments == null) unsupported(index,"raw inline source mapping")
    else {
        let bytes = slice(binary(token.output_text)^,token.output_start,token.output_end);
        let raw = utf8.decode(bytes) ^ {null};
        let prefix = "\\" ++ name;
        let prefix_end = len(binary(prefix)^);
        let starred = len(bytes) > prefix_end and bytes[prefix_end] == 42;
        let first = prefix_end + if(starred) 1 else 0;
        let option = inline_raw_group(bytes,first,91,93)^;
        let after_option = if(option==null)first else option.end;
        let language = if(contains(["mint","mintinline"],name)) inline_raw_group(bytes,after_option,123,125)^ else null;
        let after_language = if(language==null)after_option else language.end;
        let grouped = inline_raw_group(bytes,after_language,123,125)^;
        let delimiter = if(after_language < len(bytes))bytes[after_language] else null;
        let end = if(delimiter==null)null else if(delimiter==123)
                if(grouped==null)null else grouped.end
            else inline_delimiter_end(bytes,after_language + 1,delimiter)^;
        let body_begin = after_language + 1;
        let body = if(end==null)null else utf8.decode(slice(bytes,body_begin,end - 1)) ^ {null};
        let safe = raw_command_safe(bytes,raw,nested,token)^;
        if(raw==null or not starts_with(raw,prefix) or end==null or end!=len(bytes) or body==null or not safe or
                (option!=null and option.raw==null) or (language!=null and language.raw==null))
            unsupported(index,"raw inline source mapping")
        else {
            let offset = rebuild.map_offset(token.output_segments,token.output_start)^;
            {issue:null,node:inline_code_node(name,offset,starred,if(option==null)null else option.raw,
                if(language==null)null else language.raw,raw,body)}
        }
    }
}

fn source_span(token, sources) any^ {
    let found = [for (entry in sources where entry.id == token.file) entry];
    if (token.direct != true or token.start < 0 or token.end < token.start or len(found) == 0) null
    else {
        let bytes = binary(found[0].data)^;
        if (token.end > len(bytes)) null else utf8.decode(slice(bytes, token.start, token.end))^
    }
}

fn group_raw(group, sources) any^ => if (group.output_text != null)
    utf8.decode(slice(binary(group.output_text)^,group.output_start + 1,group.output_end - 1))
    else source_span({direct: group.direct, file: group.file,
        start: group.start_offset + 1, end: group.end_offset - 1}, sources)

// whitespace skipped while reading a control word still belongs to the source.
fn control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let gap_token = if (next != null and token.direct == true and next.direct == true and
            token.file == next.file and next.start > token.end)
        // this synthetic gap occupies the preceding token's source sequence.
        {kind: "char", value: " ", cat: 10, start: token.end, end: next.start,
            file: token.file, direct: true, seq: token.seq} else null;
    let gap = if (gap_token == null) null else source_span(gap_token, sources)^;
    // paragraph collection reads the skipped gap together with the blank line.
    if (paragraph_space(next,sources)^) {token:null,text:""}
    else if (gap != null and gap != "" and (trim(gap) ^ { null }) == "") {token: gap_token, text: gap}
    else {token: null, text: ""}
}

// preserve whitespace while matching the native comment line-ending rule.
fn source_gap_bytes(bytes, index = 0, comment = false, seen = false, out = "") any^ {
    if (index >= len(bytes)) {valid: true, seen: seen, text: out}
    else {
        let b = bytes[index];
        if (comment) source_gap_bytes(bytes, index + 1, b != 10 and b != 13, seen, out)
        else if (b == 37) source_gap_bytes(bytes, index + 1, true, true, out)
        else if (contains([9, 10, 12, 13, 32], b)) source_gap_bytes(bytes, index + 1, false, seen, out ++ chr(b))
        else {valid: false, seen: seen, text: out}
    }
}

// the last token in a group can still have skipped source before its closing brace.
fn trailing_gap_end(bytes, index, comment = false) any^ {
    if (index >= len(bytes)) index
    else {
        let b = bytes[index];
        if (comment) trailing_gap_end(bytes, index + 1, b != 10 and b != 13)
        else if (b == 37) trailing_gap_end(bytes, index + 1, true)
        else if (contains([9, 10, 12, 13, 32], b)) trailing_gap_end(bytes, index + 1)
        else index
    }
}

fn format_control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let found = [for (source in sources where source.id == token.file) source];
    if (token.direct != true or len(found) == 0 or (next != null and
            (next.direct != true or next.file != token.file or next.start < token.end)))
        {valid: false, seen: false, text: ""}
    else {
        let bytes = binary(found[0].data)^;
        let end = if (next != null and next.direct == true and next.file == token.file and next.start >= token.end)
            next.start else if (next == null) trailing_gap_end(bytes, token.end)^ else token.end;
        let bounded = end <= len(bytes) and (next != null or end == len(bytes) or bytes[end] == 125);
        if (not bounded) {valid: false, seen: false, text: ""}
        else source_gap_bytes(slice(bytes, token.end, end))
    }
}

fn source_control_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let adjacent = next != null and token.direct == true and next.direct == true and
        token.file == next.file and token.seq != null and next.seq == token.seq + 1;
    if (adjacent and next.start >= token.end) {
        let raw = source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^;
        if (raw == null) null else {
            let gap = source_gap_bytes(binary(raw)^)^;
            if (gap.valid) gap.text else null
        }
    }
    else ""
}

// the native command scanner accepts ASCII letters and @ in a control word.
fn native_word_char(value) bool^ => contains("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@", value)
fn native_suffix_char(value) bool^ => native_word_char(value)^ or contains("0123456789_:-.", value)
// Ordinary ASCII punctuation can be a TeX letter but ends a native control word.
fn native_word_suffix_char(value) bool^ => native_suffix_char(value)^ or
    (len(binary(value)^) == 1 and value >= "!" and value <= "~" and
        not contains("\\{}[$%#&^~", value))

// reconstructed control words need a separator before an adjacent ASCII letter.
fn spacing_word_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let adjacent = next != null and token.direct == true and next.direct == true and
        token.file == next.file and token.seq != null and next.seq == token.seq + 1;
    if (paragraph_space(next,sources)^) ""
    else if (adjacent) source_control_gap(tokens, sources, index)
    else if (next != null and contains(["char", "active"], next.kind) and len(next.value) == 1 and
        native_word_char(next.value)^) " "
    else ""
}

// collect source-backed whitespace so blank lines are split from one byte stream.
fn direct_paragraph_end(tokens, sources, index, file, end) any^ {
    if (index >= len(tokens)) index
    else {
        let token = tokens[index];
        if (token.direct != true or token.file != file or token.start == null or token.end == null or
                token.start < end or not paragraph_space(token,sources)^) index
        else direct_paragraph_end(tokens,sources,index + 1,file,token.end)
    }
}

fn restore_control_gap(tokens, sources, index) any^ {
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let anchor = tokens[index];
    let mapped = if (next != null) output_between(anchor,next)^ else null;
    // echoed definitions retain source gaps even without a reconstruction map.
    let raw = if (mapped != null) mapped else if (next != null and anchor.direct == true and
            (next.direct == true or next.kind == "source-span") and anchor.file == next.file and
            anchor.end != null and next.start != null and next.start >= anchor.end)
        source_span({direct:true,file:anchor.file,start:anchor.end,end:next.start},sources)^ else null;
    let parsed = if (raw == null or raw == "") null else source_gap_bytes(binary(raw)^)^;
    // a raw echo can end after its last token, retaining whitespace native parses.
    if (control(next,"par") and paragraph_space(next,sources)^) {
        let anchor = tokens[index];
        let end = if (anchor.direct == true and anchor.end != null and anchor.file != null)
            direct_paragraph_end(tokens,sources,index + 1,anchor.file,anchor.end)^ else index + 1;
        let final_token = if (end > index + 1) tokens[end - 1] else null;
        let following = if (end < len(tokens)) tokens[end] else null;
        let gap = if (final_token != null and following != null and following.direct == true and
                following.file == anchor.file and following.start != null and following.start >= final_token.end)
            source_span({direct:true,file:anchor.file,start:final_token.end,end:following.start},sources)^ else null;
        let gap_checked = if (gap == null) null else source_gap_bytes(binary(gap)^)^;
        let joins_next = gap_checked != null and gap_checked.valid;
        let source_end = if (joins_next) following.start else if (final_token != null) final_token.end else null;
        let source = if (source_end != null) source_span({direct:true,file:anchor.file,
            start:anchor.end,end:source_end},sources)^ else null;
        let output_end = if (joins_next) following.output_start else if (final_token != null) final_token.output_end else null;
        let mapped = if (source != null and anchor.output_end != null and output_end != null and
                anchor.output_text != null and output_end >= anchor.output_end)
            utf8.decode(slice(binary(anchor.output_text)^,anchor.output_end,output_end))^ else source;
        let checked = if (source == null) null else source_gap_bytes(binary(source)^)^;
        if (source == null or mapped != source or checked.valid != true) tokens
        else [*slice(tokens,0,index + 1),*paragraph_events(source)^,*slice(tokens,end,len(tokens))]
    }
    else if (parsed != null and parsed.valid)
        [*slice(tokens,0,index + 1),*paragraph_events(raw)^,*slice(tokens,index + 1,len(tokens))]
    else {
        let gap = control_gap(tokens, sources, index)^;
        if (gap.token == null) tokens
        else [*slice(tokens, 0, index + 1), gap.token, *slice(tokens, index + 1, len(tokens))]
    }
}

// declaration scanners consume inter-argument whitespace before echoing raw tokens.
fn restore_declaration_gaps(tokens, sources, index, limit_value, out = []) any^ {
    if (index >= limit_value) {issue: null, tokens: [*out, *slice(tokens, limit_value, len(tokens))]}
    else {
        let token = tokens[index];
        let next = if (index + 1 < limit_value) tokens[index + 1] else null;
        let can_gap = next != null and token.direct == true and next.direct == true and
            token.file == next.file and token.kind != "control" and token.cat != 10 and next.start > token.end;
        let raw = if (can_gap) source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^ else null;
        let gap = if (raw != null) source_gap_bytes(binary(raw)^)^ else null;
        let plain = gap != null and gap.valid and not gap.seen and raw != "";
        if (plain and contains(raw, "\n\n")) unsupported(index, "declaration paragraph source gap")
        else {
            let space = if (plain) {kind: "char", value: " ", cat: 10, direct: true,
                file: token.file, start: token.end, end: next.start} else null;
            restore_declaration_gaps(tokens, sources, index + 1, limit_value,
                [*out, token, *(if (space == null) [] else [space])])
        }
    }
}

fn whitespace(token, next, sources) any^ {
    let raw = source_span(token, sources)^;
    let gap = if (next != null and token.direct == true and next.direct == true and token.file == next.file and
        next.start > token.end) source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^ else null;
    let value = (if (raw == null or raw == "") token.value else raw) ++
        (if (gap != null and (trim(gap) ^ { null }) == "") gap else "");
    let after = if (control(next, "par")) source_span(next, sources)^ else null;
    // a blank line contributes the first newline to the preceding space token.
    if (after != null and (starts_with(after, "\n") or starts_with(after, "\r")) and ends_with(value, "\n"))
        slice(value, 0, len(value) - if (ends_with(value, "\r\n")) 2 else 1)
    else value
}

// the native parser sees reconstructed source bytes, including skipped comments.
fn paragraph_space(token, sources) any^ {
    if (token == null or token.paragraph_normalized == true) false
    else if (token.kind == "char" and token.cat == 10) true
    else if (control(token, "par")) {
        // reconstructed spelling distinguishes an expanded control word from a blank line.
        let raw = if (token.output_text != null and token.output_start != null and token.output_end != null)
            utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^
            // the source fragment also spells a non-direct macro expansion.
            else (rebuild.source_fragment(token,sources)^).body;
        raw != "\\par"
    } else false
}

fn paragraph_collect(tokens, sources, index, previous, raw = "") any^ {
    if (index >= len(tokens)) {index:index, raw:raw}
    else {
        let token = tokens[index];
        let prior = if (previous == null) null else rebuild.effective_source_token(previous);
        let prior_seq = if (prior == null) null else if (prior.kind == "source-span") prior.last_seq else prior.seq;
        // normalized whitespace has no source end, so it cannot anchor a gap.
        let valid = prior != null and (prior.direct == true or prior.kind == "source-span") and
            prior_seq != null and prior.end != null;
        let piece = rebuild.source_fragment(token,sources,valid,if(prior == null)null else prior.file,
            prior_seq,if(valid)prior.end else 0)^;
        // Restored gaps contain comments, so canonical whitespace would lose their source spelling.
        let body = if (token.normalize_source_gap == true) source_span(token,sources)^
            else if ((token.normalize_output_gap == true or (previous != null and previous.normalize_output_gap == true)) and
                    token.output_text != null and token.output_start != null and token.output_end != null)
                utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ else piece.body;
        if (not paragraph_space(token,sources)^) {
            if (piece.gap == null) unsupported(index,"paragraph source gap")
            else {
                let checked = source_gap_bytes(binary(piece.gap)^)^;
                if (checked.valid) {index:index, raw:raw ++ piece.gap, issue:null}
                else unsupported(index,"paragraph source gap")
            }
        }
        else {
            if (piece.gap == null or body == null) unsupported(index,"paragraph source mapping")
            else paragraph_collect(tokens,sources,index + 1,token,raw ++ piece.gap ++ body)
        }
    }
}

// comments consume their line ending before a later LF pair can end a paragraph.
fn paragraph_events(raw, current = "", events = []) any^ {
    let comment = index_of(raw,"%");
    let boundary = index_of(raw,"\n\n");
    if (comment == null and boundary == null) {
        let final_text = current ++ raw;
        [*events, *(if (final_text == "") [] else [{kind:"char",cat:10,value:final_text,paragraph_normalized:true}])]
    }
    else if (comment != null and (boundary == null or comment < boundary)) {
        let prefix = slice(raw,0,comment);
        let rest = slice(raw,comment + 1,len(raw));
        let lf = index_of(rest,"\n"); let cr = index_of(rest,"\r");
        let end = if (lf == null) cr else if (cr == null) lf else min(lf,cr);
        // a comment can start an otherwise empty native paragraph.
        let before = current ++ prefix;
        paragraph_events(if(end == null) "" else slice(rest,end + 1,len(rest)),"",[*events,
            *(if(before == "") [] else [{kind:"char",cat:10,value:before,paragraph_normalized:true}]),
            {kind:"paragraph-comment"}])
    }
    else {
        let before = current ++ slice(raw,0,boundary);
        paragraph_events(slice(raw,boundary + 2,len(raw)),"",[*events,
            *(if (before == "") [] else [{kind:"char",cat:10,value:before,paragraph_normalized:true}]),
            {kind:"control",value:"par",paragraph_normalized:true}])
    }
}

fn paragraph_run(tokens,sources,index) any^ {
    let prior = if (index > 0) tokens[index - 1] else null;
    let run = paragraph_collect(tokens,sources,index,prior)^;
    if (run.issue != null) run
    else {
        let events = paragraph_events(run.raw)^;
        {index:run.index,events:events,issue:null,fast:not contains(run.raw,"%") and
            not any([for (event in events) control(event,"par")])}
    }
}
fn normalize_paragraph(tokens,index,run) any^ {
    [*slice(tokens,0,index),*run.events,*slice(tokens,run.index,len(tokens))]
}

fn comment_between_text(tokens,sources,index) any^ {
    let token = tokens[index];
    let prev = if (index == 0) null else tokens[index - 1];
    let seq = if (prev != null and prev.kind == "source-span") prev.last_seq else if (prev != null) prev.seq else null;
    let valid = token.paragraph_gap_seen != true and prev != null and prev.kind != "control" and
        (prev.direct == true or prev.kind == "source-span") and seq != null and
        token.start != null and prev.end != null and token.start > prev.end;
    let piece = if (valid) rebuild.source_fragment(token,sources,true,prev.file,seq,prev.end)^ else null;
    let raw = if (piece == null) null else piece.gap;
    let gap = if (raw == null) null else source_gap_bytes(binary(raw)^)^;
    if (gap != null and gap.valid and gap.seen) gap.text else null
}

fn skip_par_blanks(chars,index = 0) any^ => if (index < len(chars) and
    contains([" ","\t","\n","\r"],chars[index])) skip_par_blanks(chars,index + 1) else index

// the native parser skips whitespace after an emitted control-word par.
fn generated_par_next(tokens,index) any^ {
    if (index >= len(tokens)) index
    else {
        let token = tokens[index];
        let raw = if (token.output_text != null and token.output_start != null and token.output_end != null)
            utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ else null;
        let gap = output_between(tokens[index - 1],token)^;
        let blank = if (raw == null or gap == null) false else
            all([for (c in split(raw ++ gap,"")^) contains([" ","\t","\n","\r"],c)]);
        if (((token.kind == "char" and token.cat == 10) or control(token,"par")) and blank)
            generated_par_next(tokens,index + 1)
        else index
    }
}

// explicit par skips initial blanks, but a following comment starts a paragraph.
fn explicit_par_gap(tokens,sources,index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    let raw = if (token.output_text != null and token.output_start != null and token.output_end != null)
        utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ else source_span(token,sources)^;
    let valid = raw == "\\par" and next != null and token.direct == true and token.seq != null;
    let piece = if (valid) rebuild.source_fragment(next,sources,true,token.file,token.seq,token.end)^ else null;
    let gap = if (token.output_end != null) output_between(token,next)^ else if (piece == null) null else piece.gap;
    let chars = if (gap == null) [] else split(gap,"")^;
    let at = skip_par_blanks(chars)^;
    if (at >= len(chars) or chars[at] != "%") tokens
    else {
        let tail = join(slice(chars,at,len(chars)),"");
        let events = if (paragraph_space(next,sources)^) [] else paragraph_events(tail)^;
        // initial blanks have already been consumed before the comment marker.
        let marker = {kind:"paragraph-comment",direct:true,file:token.file,seq:token.seq,start:token.start,end:token.end + at};
        [*slice(tokens,0,index + 1),marker,*events,*slice(tokens,index + 1,len(tokens))]
    }
}

fn dollar(token) => token != null and token.kind == "char" and token.cat == 3
fn math_digit(token) any^ => token != null and token.kind == "char" and len(token.value) == 1 and
    ord(token.value) >= 48 and ord(token.value) <= 57
fn math_letter(token) any^ => token != null and token.kind == "char" and len(token.value) == 1 and
    ((ord(token.value) >= 65 and ord(token.value) <= 90) or (ord(token.value) >= 97 and ord(token.value) <= 122))

fn math_close(tokens, index, display, closing = null) any^ {
    if (index >= len(tokens)) {issue: "latex-digest-unclosed-math", index: index}
    else if (closing != null and control(tokens[index], closing)) {issue: null, index: index, end: index + 1}
    else if (closing == null and dollar(tokens[index])) {
        if (not display) {issue: null, index: index, end: index + 1}
        else if (index + 1 < len(tokens) and dollar(tokens[index + 1])) {issue: null, index: index, end: index + 2}
        else unsupported(index, "display math delimiter")
    }
    else math_close(tokens, index + 1, display, closing)
}

fn math_number(tokens, index, text = "") any^ {
    if (index >= len(tokens) or not (math_digit(tokens[index]) or
        (tokens[index].kind == "char" and tokens[index].value == "."))) {index: index, text: text}
    else math_number(tokens, index + 1, text ++ tokens[index].value)
}

// native math adjacency follows reconstructed bytes when expansion crosses sources.
fn math_adjacent(left, right) =>
    if (left.output_text != null and right.output_text != null and left.output_end != null and right.output_start != null)
        left.output_end == right.output_start
    else left.direct == true and right.direct == true and left.file == right.file and left.end == right.start

fn math_positioned(token) => token.direct == true or
    (token.output_text != null and token.output_start != null and token.output_end != null)

fn math_unicode(token) any^ => token != null and token.kind == "char" and
    len(token.value) == 1 and ord(token.value) >= 128

// Native math groups adjacent raw-marker or non-ASCII bytes into one atom.
fn math_text_run(tokens, index, unicode: bool, text = "", previous = null) any^ {
    if (index >= len(tokens)) {issue: null, index: index, text: text}
    else {
        let token = tokens[index];
        let member = if (unicode) math_unicode(token)^ else
            token.kind == "char" and (contains(["#", "!"], token.value) or math_digit(token));
        let mapped = math_positioned(token) and (previous == null or math_positioned(previous));
        if (not member) {issue: null, index: index, text: text}
        else if (not mapped) unsupported(index, if (unicode) "math unicode source mapping" else "math raw text source mapping")
        else if (previous != null and not math_adjacent(previous, token)) {issue: null, index: index, text: text}
        else math_text_run(tokens, index + 1, unicode, text ++ token.value, token)
    }
}
fn math_raw_run(tokens, index) any^ => math_text_run(tokens, index, false)

fn flush_math_strings(items, run) => if (len(run) == 0) items
    else if (len(run) == 1) [*items, run[0]] else [*items, run]

fn math_content(items, index = 0, out = [], run = []) any^ {
    if (index >= len(items)) flush_math_strings(out, run)
    else if (items[index] is string) math_content(items, index + 1, out, [*run, items[index]])
    else math_content(items, index + 1, [*flush_math_strings(out, run), items[index]], [])
}

fn math_nonspace(tokens, index) any^ => if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
    math_nonspace(tokens, index + 1) else index

// Math options contain math lists, including controls and nested groups.
fn math_optional(tokens, index) any^ {
    if (index >= len(tokens) or tokens[index].kind != "char" or tokens[index].value != "[")
        {issue:null,present:false,end:index}
    else {*:read_generic_delimited(tokens,index,"[","]","latex-digest-unclosed-optional")^,present:true}
}

// match delimiter pairs only at the current brace and left/right nesting level.
fn math_delimiter_boundary(tokens, index, nested = 0, braces = 0) any^ {
    if (index >= len(tokens)) {index: index, kind: "end"}
    else {
        let token = tokens[index];
        if (braces == 0 and control(token, "right") and nested == 0) {index: index, kind: "right"}
        else if (braces == 0 and control(token, "middle") and nested == 0) {index: index, kind: "middle"}
        else {
            let next_braces = braces + if (opening(token)) 1 else if (closing(token)) -1 else 0;
            let next_nested = if (braces != 0) nested else if (control(token, "left")) nested + 1
                else if (control(token, "right")) nested - 1 else nested;
            math_delimiter_boundary(tokens, index + 1, next_nested, next_braces)
        }
    }
}
fn math_delimiter_control(name) => contains(["{", "}", "|", "vert", "Vert",
    "lvert", "rvert", "lVert", "rVert", "langle", "rangle", "lbrace", "rbrace", "lceil", "rceil",
    "lfloor", "rfloor", "uparrow", "downarrow", "updownarrow", "Uparrow", "Downarrow", "Updownarrow",
    "backslash", "arrowvert", "Arrowvert", "bracevert", "lmoustache", "rmoustache", "lgroup", "rgroup",
    "lbrack", "rbrack", "lparen", "rparen"], name)

fn math_delimiter_token(tokens, index, middle = false, right_side = false) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) if (middle) unsupported(at, "math delimiter")
        else {issue: null, index: at, value: "."}
    else if (right_side and opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else if (len(group.tokens) == 0 and math_adjacent(tokens[at], tokens[group.end - 1]))
            {issue: null, index: group.end, value: ""}
        else {
            let inner_at = math_nonspace(group.tokens, 0)^;
            let comments = any([for (token in group.tokens) token.kind == "source-comment"]);
            if (comments or inner_at + 1 != len(group.tokens) or
                not math_adjacent(group.tokens[inner_at], tokens[group.end - 1])) unsupported(at, "braced math delimiter")
            else {
                let inner = math_delimiter_token(group.tokens, inner_at)^;
                if (inner.issue != null) inner else {issue: null, index: group.end, value: inner.value}
            }
        }
    }
    else {
        let token = tokens[at];
        let char_ok = token.kind == "char" and not opening(token) and not closing(token) and
            len(token.value) == 1 and ord(token.value) < 128;
        let control_ok = token.kind == "control" and (not middle or math_delimiter_control(token.value));
        if (char_ok and (not middle or contains(["(", ")", "[", "]", "|", "<", ">", "/", "."], token.value)))
            {issue: null, index: at + 1, value: token.value}
        else if (control_ok) {issue: null, index: at + 1, value: "\\" ++ token.value}
        else unsupported(at, "math delimiter")
    }
}
fn math_delimiter_body(tokens, index, items = []) any^ {
    let boundary = math_delimiter_boundary(tokens, index)^;
    let section = math_atoms(slice(tokens, index, boundary.index))^;
    if (section.issue != null) section
    else {
        let combined = [*items, *section.items];
        if (boundary.kind == "end") {issue: null, index: boundary.index, right: ".", items: combined}
        else {
            let delimiter = math_delimiter_token(tokens, boundary.index + 1, boundary.kind == "middle", boundary.kind == "right")^;
            if (delimiter.issue != null) delimiter
            else if (boundary.kind == "right")
                {issue: null, index: delimiter.index, right: delimiter.value, items: combined}
            else math_delimiter_body(tokens, delimiter.index, [*combined, <middle_delim delim: delimiter.value>])
        }
    }
}
fn math_delimiter_group(tokens, index) any^ {
    let left = math_delimiter_token(tokens, index + 1)^;
    if (left.issue != null) left
    else {
        let body = math_delimiter_body(tokens, left.index)^;
        if (body.issue != null) body
        else {issue: null, index: body.index,
            value: <delimiter_group left: left.value, right: body.right, *math_content(body.items)^>}
    }
}

// command arguments use the same atom and group rules as scripts.
fn math_arguments(tokens, index, count, items = []) any^ {
    if (count == 0) {issue: null, index: index, items: items}
    else {
        let arg = math_script_arg(tokens, index)^;
        if (arg.issue != null) arg
        else math_arguments(tokens, arg.index, count - 1, [*items, arg.value])
    }
}

fn math_dimension_signs(tokens, index) any^ =>
    if (index < len(tokens) and tokens[index].kind == "char" and contains(["+", "-"], tokens[index].value))
        math_dimension_signs(tokens, math_nonspace(tokens,index + 1)^) else index
fn math_dimension_digits(tokens, index) any^ =>
    if (index < len(tokens) and (math_digit(tokens[index]) or
        (tokens[index].kind == "char" and tokens[index].value == ".")))
        math_dimension_digits(tokens,index + 1) else index
fn math_dimension_tail(tokens, index) any^ =>
    if (index < len(tokens) and not (tokens[index].kind == "char" and tokens[index].cat == 10))
        math_dimension_tail(tokens,index + 1) else index

fn math_dimension_argument(tokens, index) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) unsupported(at, "math dimension argument")
    else if (opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else {
            let raw = output_between(tokens[at], tokens[group.end - 1])^;
            if (raw == null) unsupported(at, "math dimension source mapping")
            else {issue:null, index:group.end, value:raw}
        }
    }
    else {
        let numeric_start = math_dimension_signs(tokens,at)^;
        let digits = math_dimension_digits(tokens,numeric_start)^;
        let unit = if (digits > numeric_start) math_nonspace(tokens,digits)^ else digits;
        let end = if (digits == numeric_start) math_dimension_tail(tokens,at)^
            else if (unit + 1 < len(tokens) and math_letter(tokens[unit]) and math_letter(tokens[unit + 1])) unit + 2
            else unit;
        let first = tokens[at];
        let final_token = if (end > at) tokens[end - 1] else null;
        if (final_token == null or first.output_text == null or first.output_start == null or final_token.output_end == null)
            unsupported(at, "math dimension source mapping")
        else {issue:null, index:end,
            value:utf8.decode(slice(binary(first.output_text)^,first.output_start,final_token.output_end))^}
    }
}

// generic math commands consume adjacent or space-separated groups as children.
fn math_generic_arguments(tokens, index, items = []) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) {issue: null, index: at, items: items}
    else if (opening(tokens[at])) {
        let arg = math_script_arg(tokens, at)^;
        if (arg.issue != null) arg else math_generic_arguments(tokens, arg.index, [*items, arg.value])
    }
    else if (tokens[at].kind == "char" and tokens[at].value == "[") {
        let arg = read_generic_delimited(tokens, at, "[", "]", "latex-digest-unclosed-optional")^;
        // An unmatched bracket remains punctuation after the command.
        if (arg.issue == "latex-digest-unclosed-optional") {issue:null, index:at, items:items}
        else if (arg.issue != null) arg
        else {
            let content = math_atoms(arg.tokens)^;
            if (content.issue != null) content
            else math_generic_arguments(tokens, arg.end, [*items, <brack_group *math_content(content.items)^>])
        }
    }
    else {issue: null, index: at, items: items}
}

// Matrix separators delimit independent math lists; nested environments own
// their own separators and a row break can carry a raw spacing argument.
fn math_matrix_boundary(tokens, index, braces = 0, environments = 0) any^ {
    if (index >= len(tokens)) {kind:"end", index:index}
    else {
        let token = tokens[index];
        if (braces == 0 and token.kind == "control" and contains(["begin", "end"], token.value)) {
            let at = math_nonspace(tokens,index + 1)^;
            let group = read_group(tokens,at)^;
            if (group.issue != null) group
            else math_matrix_boundary(tokens,group.end,braces,environments + if (token.value == "begin") 1 else -1)
        }
        else if (braces == 0 and environments == 0 and token.kind == "char" and token.value == "&")
            {kind:"column", index:index}
        else if (braces == 0 and environments == 0 and token.kind == "control" and
                (token.value == "\\" or token.value == "cr")) {kind:"row", index:index}
        else math_matrix_boundary(tokens,index + 1,
            braces + if (opening(token)) 1 else if (closing(token)) -1 else 0,environments)
    }
}
fn math_matrix_items(tokens, index = 0, items = [], column = 0, text_columns = false) any^ {
    let boundary = math_matrix_boundary(tokens,index)^;
    if (boundary.issue != null) boundary
    else {
        let parts = slice(tokens,index,boundary.index);
        let cell = if (text_columns and column > 0) math_text_cell(parts)^ else math_list(parts)^;
        if (cell.issue != null) cell
        else if (boundary.kind == "end") {issue:null,items:[*items,*cell.items]}
        else if (boundary.kind == "column")
            math_matrix_items(tokens,boundary.index + 1,[*items,*cell.items,'col_sep'],column + 1,text_columns)
        else {
            let at = math_nonspace(tokens,boundary.index + 1)^;
            let opt = math_optional(tokens,at)^;
            if (opt.issue == "latex-digest-unclosed-optional")
                math_matrix_items(tokens,at,[*items,*cell.items,'row_sep'],0,text_columns)
            else if (opt.issue != null) opt
            else {
                let separator = if (opt.present) <row_sep gap:group_text(opt)> else 'row_sep';
                math_matrix_items(tokens,opt.end,[*items,*cell.items,separator],0,text_columns)
            }
        }
    }
}

fn math_cd_boundary(tokens, index, depth = 0, direction = null) any^ {
    if (index >= len(tokens)) index
    else {
        let token = tokens[index];
        let matches = depth == 0 and token.kind == "char" and
            (if (direction == null) token.value == "@" and index + 1 < len(tokens) else token.value == direction);
        let row = direction == null and depth == 0 and control(token,"\\");
        if (matches or row) index
        else math_cd_boundary(tokens,index + 1,depth + if (opening(token)) 1 else if (closing(token)) -1 else 0,direction)
    }
}
fn math_cd_label(tokens,index,direction) any^ {
    let end = math_cd_boundary(tokens,index,0,direction)^;
    let body = math_list(slice(tokens,index,end))^;
    if (body.issue != null) body
    else {issue:null,index:if (end < len(tokens)) end + 1 else end,
        value:<group *math_content(body.items)^>}
}
fn math_cd_items(tokens,index = 0,items = [],vertical_cell = false) any^ {
    let at = math_nonspace(tokens,index)^;
    if (at >= len(tokens)) {issue:null,items:items}
    else if (control(tokens[at],"\\")) math_cd_items(tokens,at + 1,[*items,'row_sep'],false)
    else if (tokens[at].kind == "char" and tokens[at].value == "@" and at + 1 < len(tokens)) {
        let direction = tokens[at + 1].value;
        let vertical = contains(["A", "V", "|"],direction);
        let labeled = contains(["<", ">", "A", "V"],direction);
        let upper = if (labeled) math_cd_label(tokens,at + 2,direction)^ else {issue:null,index:at + 2};
        let lower = if (upper.issue != null) upper else if (labeled) math_cd_label(tokens,upper.index,direction)^
            else {issue:null,index:upper.index};
        if (lower.issue != null) lower
        else {
            let final_token = tokens[lower.index - 1];
            let first = tokens[at];
            let raw = if (first.output_text == null or first.output_start == null or final_token.output_end == null) null
                else utf8.decode(slice(binary(first.output_text)^,first.output_start,final_token.output_end))^;
            if (raw == null) unsupported(at,"CD arrow source mapping")
            else {
                let arrow = <cd_arrow direction:direction, *:if (labeled) {upper:upper.value,lower:lower.value} else {}, source:raw>;
                let before = if (not vertical or vertical_cell) ['col_sep'] else [];
                let second = if (vertical and vertical_cell) ['col_sep'] else [];
                let after = if (vertical) [] else ['col_sep'];
                math_cd_items(tokens,lower.index,[*items,*before,*second,arrow,*after],vertical)
            }
        }
    }
    else {
        let end = math_cd_boundary(tokens,at)^;
        let cell = math_list(slice(tokens,at,end))^;
        if (cell.issue != null) cell else math_cd_items(tokens,end,[*items,*cell.items],vertical_cell)
    }
}

// Text arguments retain nested groups and math switches instead of parsing all
// of their contents as math atoms.
fn math_text_items(tokens, index = 0, items = [], text = "") any^ {
    if (index >= len(tokens)) {issue:null, items:append_text(items, text)}
    else {
        let token = tokens[index];
        if (opening(token)) {
            let group = math_text_argument(tokens, index)^;
            if (group.issue != null) group
            else math_text_items(tokens, group.index, [*append_text(items,text), group.value])
        }
        else if (dollar(token)) {
            let close = math_close(tokens, index + 1, false)^;
            // An unmatched dollar in text is literal, as in the native scanner.
            if (close.issue == "latex-digest-unclosed-math") math_text_items(tokens, index + 1, items, text ++ "$")
            else if (close.issue != null) close
            else {
                let body = math_atoms(slice(tokens, index + 1, close.index))^;
                if (body.issue != null) body
                else math_text_items(tokens, close.end,
                    [*append_text(items,text), <embedded_math *math_content(body.items)^>])
            }
        }
        else if (token.kind == "control") {
            let accents = {["'"]:"acute", ["`"]:"grave", ["^"]:"hat", ["~"]:"tilde", ["="]:"bar",
                ["."]:"dot", ['"']:"ddot", u:"breve", v:"check", H:"doubleacute", r:"mathring", c:"cedilla"};
            let accent = accents[token.value];
            if (accent != null) {
                let arg = math_text_argument(tokens, index + 1)^;
                if (arg.issue != null) arg
                else math_text_items(tokens, arg.index,
                    [*append_text(items,text), <accent cmd:accent, base:arg.value>])
            }
            else if (contains(["$", "%", "#", "&", "_", "{", "}", " ", ","], token.value))
                math_text_items(tokens, index + 1, items, text ++ if (token.value == ",") " " else token.value)
            else {
                let command = math_command(tokens, index)^;
                if (command.issue != null) command
                else math_text_items(tokens, command.index, [*append_text(items,text), command.value])
            }
        }
        else if (token.kind == "active" and token.value == "~") math_text_items(tokens, index + 1, items, text ++ " ")
        else if (token.kind == "char") math_text_items(tokens, index + 1, items, text ++ token.value)
        else unsupported(index, "math text token")
    }
}

// Keep the original bytes while decoding only literal TeX punctuation escapes.
fn math_text_content(bytes, tokens, base, index = 0, cursor = 0, text = "") any^ {
    if (index >= len(tokens)) text ++ utf8.decode(slice(bytes, cursor, len(bytes)))^
    else {
        let token = tokens[index];
        let start = if (token.output_start == null) -1 else token.output_start - base;
        let end = if (token.output_end == null) -1 else token.output_end - base;
        let literal = token.kind == "control" and contains(["$", "%", "#", "&", "_", "{", "}"], token.value) and
            start >= cursor and end <= len(bytes) and end > start and
            utf8.decode(slice(bytes,start,end))^ == "\\" ++ token.value;
        if (literal) math_text_content(bytes,tokens,base,index + 1,end,
            text ++ utf8.decode(slice(bytes,cursor,start))^ ++ token.value)
        else math_text_content(bytes,tokens,base,index + 1,cursor,text)
    }
}

fn math_text_node(name, source, content, items) any^ {
    let nested = any([for (marker in ["\\", "$", "{", "~"]) contains(source, marker)]);
    <text_command cmd:"\\" ++ name, content:content,
        *:if (content != source) {content_raw:source} else {},
        *:if (nested) {body:<text_group *items>} else {}>
}
fn math_text_cell_end(tokens, end) any^ =>
    if (end > 0 and tokens[end - 1].kind == "char" and tokens[end - 1].cat == 10)
        math_text_cell_end(tokens,end - 1) else end
fn math_text_cell(tokens) any^ {
    let start = math_nonspace(tokens,0)^;
    let end = math_text_cell_end(tokens,len(tokens))^;
    if (start >= end) {issue:null,items:[]}
    else {
        let first = tokens[start]; let final_token = tokens[end - 1];
        if (first.output_text == null or first.output_start == null or final_token.output_end == null)
            unsupported(start,"math text cell source mapping")
        else {
            let source = utf8.decode(slice(binary(first.output_text)^,first.output_start,final_token.output_end))^;
            let parts = slice(tokens,start,end);
            let body = math_text_items(parts)^;
            if (body.issue != null) body
            else {
                let content = math_text_content(binary(source)^,parts,first.output_start)^;
                {issue:null,items:[math_text_node("text",source,content,body.items)^]}
            }
        }
    }
}

fn math_text_argument(tokens, index) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) unsupported(at, "math text argument")
    else if (opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else {
            let source = output_between(tokens[at], tokens[group.end - 1])^;
            let body = math_text_items(group.tokens)^;
            if (body.issue != null) body
            else if (source == null) unsupported(at, "math text source mapping")
            else {issue:null, index:group.end, source:source, items:body.items,
                content:math_text_content(binary(source)^, group.tokens, tokens[at].output_end)^,
                value:<text_group source:source, *body.items>}
        }
    }
    else if (tokens[at].kind == "control") math_command(tokens, at)
    else {issue:null, index:at + 1, value:<raw_math_text value:tokens[at].value>}
}

// adjacent escaped punctuation is represented as one native math atom.
fn math_escaped_run(tokens, index, previous, text) any^ {
    if (index >= len(tokens)) {issue: null, index: index, text: text}
    else {
        let token = tokens[index];
        let member = token.kind == "control" and contains(["{", "}", "[", "]", "(", ")", "$", "%", "#", "&"], token.value);
        let mapped = math_positioned(token) and math_positioned(previous);
        if (not member) {issue: null, index: index, text: text}
        else if (not mapped) unsupported(index, "math escaped symbol source mapping")
        else if (not math_adjacent(previous, token)) {issue: null, index: index, text: text}
        else math_escaped_run(tokens, index + 1, token, text ++ token.value)
    }
}

// Both document and standalone math recognize these environment names.
fn common_math_environment(name) => contains(["array", "alignat", "alignat*", "aligned",
    "gather", "gather*", "split", "IEEEeqnarray", "cases", "cases*", "matrix", "matrix*",
    "pmatrix", "pmatrix*", "bmatrix", "bmatrix*", "vmatrix", "vmatrix*", "Vmatrix", "Vmatrix*",
    "smallmatrix"], name)
fn standalone_math_environment(name) => common_math_environment(name) or contains([
    "Bmatrix", "Bmatrix*", "rcases", "dcases", "alignedat", "align", "align*", "gathered",
    "equation", "equation*", "subarray", "CD", "psmallmatrix", "psmallmatrix*",
    "bsmallmatrix", "bsmallmatrix*", "Bsmallmatrix", "Bsmallmatrix*", "vsmallmatrix", "vsmallmatrix*",
    "Vsmallmatrix", "Vsmallmatrix*", "drcases", "dcases*", "rcases*", "drcases*",
    "multlined", "lgathered", "rgathered"], name)

// Environment bodies reuse math_atoms so nested groups and scripts keep their AST shapes.
fn math_environment(tokens, index) any^ {
    let at = math_nonspace(tokens, index + 1)^;
    if (at >= len(tokens) or not opening(tokens[at])) {issue:null, index:at, value:null}
    else {
        let name_group = read_group(tokens, at)^;
        if (name_group.issue != null or not plain_group(name_group)) unsupported(at, "math environment name")
        else {
            let env = group_text(name_group);
            if (not standalone_math_environment(env))
                {issue:null, index:name_group.end, value:<ignored_begin name:env>}
            else {
                let header_at = math_nonspace(tokens, name_group.end)^;
                let pairs = contains(["alignedat", "alignat", "alignat*"], env);
                let columns = contains(["array", "subarray"], env);
                let aligned = (ends_with(env, "*") or false) and (contains(env, "matrix") or false);
                let arg = if ((pairs or columns) and header_at < len(tokens) and opening(tokens[header_at]))
                    read_group(tokens, header_at)^ else null;
                let opt = if (aligned and arg == null) math_optional(tokens, header_at)^ else null;
                if ((arg != null and arg.issue != null) or (opt != null and opt.issue != null))
                    unsupported(header_at, "math environment header")
                else {
                    let begin = if (arg != null) arg.end else if (opt != null and opt.present) opt.end else header_at;
                    let finish = math_environment_end(tokens, begin, env, 0, false)^;
                    if (finish.issue != null) finish
                    else {
                        let text_columns = len(env) > 6 and ends_with(env,"cases*");
                        let body_tokens = slice(tokens, begin, finish.index);
                        let body = if (env == "CD") math_cd_items(body_tokens)^
                            else math_matrix_items(body_tokens,0,[],0,text_columns)^;
                        if (body.issue != null) body
                        else {
                            let attrs = if (arg != null and pairs) {pairs:group_text(arg)}
                                else if (arg != null and columns) {columns:group_text(arg)}
                                else if (opt != null and opt.present) {alignment:group_text(opt)}
                                else {};
                            {issue:null, index:finish.end,
                                value:<environment name:env, *:attrs, body:<env_body *math_content(body.items)^>>}
                        }
                    }
                }
            }
        }
    }
}

// These native special forms must not be mistaken for generic commands.
fn math_unported_special(name) => contains(["above", "angl", "anglk", "angln", "anglr",
    "atop", "brace", "brack", "choose", "class", "cssId", "htmlData", "middle", "over", "right", "substack"], name)

fn math_command(tokens, index) any^ {
    let name = tokens[index].value;
    if (name == " " or name == "\t" or name == "\n") {issue:null, index:index + 1, value:" "}
    else if (contains(["{", "}", "[", "]", "(", ")", "$", "%", "#", "&", "_"], name)) {
        let token = tokens[index];
        let parsed = if (not math_positioned(token)) unsupported(index, "math escaped symbol source mapping")
            else if (name == "_") {issue: null, index: index + 1, text: name}
            else math_escaped_run(tokens, index + 1, token, name)^;
        if (parsed.issue != null) parsed
        else {issue: null, index: parsed.index, value: <escaped_symbol value: parsed.text>}
    }
    else if (name == "\\" or name == "cr")
        {issue: null, index: index + 1, value: 'row_sep'}
    else if (contains([",", ":", ";", "!", "quad", "qquad"], name))
        {issue: null, index: index + 1, value: <space_command value:"\\" ++ name>}
    else if (contains(["hspace", "vspace", "hskip", "vskip", "kern", "mkern"], name)) {
        let dimension = math_dimension_argument(tokens,index + 1)^;
        if (dimension.issue != null) dimension
        else {issue:null,index:dimension.index,value:<space_command cmd:"\\" ++ name,value:dimension.value>}
    }
    else if (name == "limits" or name == "nolimits")
        {issue: null, index: index + 1, value: symbol(name)}
    else if (name == "begin") math_environment(tokens, index)
    else if (name == "end") {issue:null, index:index + 1, value:<malformed_end>}
    else if (contains(["nonumber", "notag", "nobreak", "allowbreak", "newline"], name))
        {issue:null,index:index + 1,value:<layout_control cmd:"\\" ++ name>}
    else if (name == "hline" or name == "hdashline")
        {issue:null,index:index + 1,value:<array_rule cmd:"\\" ++ name>}
    else if (name == "Set" or name == "Braket") {
        let body = math_script_arg(tokens,index + 1)^;
        if (body.issue != null) body
        else {issue:null,index:body.index,value:<delimiter_group
            left:if (name == "Set") "\\{" else "\\langle",
            right:if (name == "Set") "\\}" else "\\rangle", body:body.value>}
    }
    else if (name == "verb") {
        let parsed = verb_element(tokens[index], index, false)^;
        if (parsed.issue != null) parsed
        else {
            let raw = binary(parsed.node.raw_source)^;
            let starred = parsed.node.starred == true;
            let delimiter = if (starred) 6 else 5;
            let value = utf8.decode(slice(raw, delimiter + 1, len(raw) - 1))^;
            let separator = utf8.decode(slice(raw, delimiter, delimiter + 1))^;
            {issue:null, index:index + 1, value:<verbatim value:value, starred:starred, delimiter:separator>}
        }
    }
    else if (name == "tag") {
        let star = index + 1 < len(tokens) and tokens[index + 1].kind == "char" and
            tokens[index + 1].value == "*" and math_adjacent(tokens[index], tokens[index + 1]);
        let arg = math_text_argument(tokens, index + if (star) 2 else 1)^;
        if (arg.issue != null) arg
        else {issue:null, index:arg.index, value:<equation_tag starred:star, body:arg.value>}
    }
    else if (name == "ce" or name == "pu") {
        let source = math_dimension_argument(tokens,index + 1)^;
        if (source.issue != null) source
        else {issue:null,index:source.index,value:<chemistry cmd:"\\" ++ name,source:source.value>}
    }
    else if (tex_reader.math_text_command(name)) {
        let arg = math_text_argument(tokens, index + 1)^;
        if (arg.issue != null) arg
        else if (arg.source == null) unsupported(index, "math text command argument")
        else {
            {issue:null, index:arg.index, value:math_text_node(name,arg.source,arg.content,arg.items)^}
        }
    }
    else if (contains(["rm", "it", "bf", "sf", "tt", "cal"], name))
        {issue:null, index:index + 1, value:<font_switch cmd:"\\" ++ name>}
    else if (name == "raisebox" or name == "reflectbox") {
        let offset = if (name == "raisebox") math_text_argument(tokens, index + 1)^
            else {issue:null, index:index + 1};
        if (offset.issue != null) offset
        else if (name == "raisebox" and offset.source == null) unsupported(index, "raisebox dimension")
        else {
            let first = if (name == "raisebox") math_optional(tokens, math_nonspace(tokens, offset.index)^)^
                else {issue:null, present:false, end:offset.index};
            let second = if (first.issue == null and first.present)
                math_optional(tokens, math_nonspace(tokens, first.end)^)^ else {issue:null, present:false, end:offset.index};
            let body = if (first.issue != null) first else if (second.issue != null) second
                else math_text_argument(tokens, second.end)^;
            if (body.issue != null) body
            else {issue:null, index:body.index, value:<box_transform cmd:"\\" ++ name,
                *:if (name == "raisebox") {raise:offset.source} else {},
                *:if (first.present) {height:group_text(first)} else {},
                *:if (second.present) {depth:group_text(second)} else {}, body:body.value>}
        }
    }
    else if (contains(["mathreflectbox", "cancel", "bcancel", "xcancel", "cancelto", "sout", "pmb", "phase"], name)) {
        let label = if (name == "cancelto") math_script_arg(tokens,index + 1)^
            else {issue:null,index:index + 1};
        let body = if (label.issue != null) label else math_script_arg(tokens,label.index)^;
        if (body.issue != null) body
        else {issue:null,index:body.index,value:<box_transform cmd:"\\" ++ name,
            *:if (name == "cancelto") {value:label.value} else {},body:body.value>}
    }
    else if (contains(["phantom", "hphantom", "vphantom", "smash"], name)) {
        let at = math_nonspace(tokens, index + 1)^;
        let opt = if (name == "smash") math_optional(tokens, at)^ else {issue:null, present:false, end:at};
        if (opt.issue != null) opt
        else {
            let arg = math_script_arg(tokens, opt.end)^;
            if (arg.issue != null) arg
            else {issue:null, index:arg.index, value:<phantom_command cmd:"\\" ++ name,
                *:if (opt.present) {options:<brack_group group_text(opt)>} else {}, content:arg.value>}
        }
    }
    else if (contains(["bbox", "boxed", "fbox", "llap", "rlap", "clap", "mathllap", "mathrlap", "mathclap"], name)) {
        let at = math_nonspace(tokens,index + 1)^;
        let opt = if (name == "bbox") math_optional(tokens,at)^ else {issue:null,present:false,end:at};
        let content = if (opt.issue != null) opt else math_script_arg(tokens,opt.end)^;
        if (content.issue != null) content
        else {issue:null,index:content.index,value:<box_command cmd:"\\" ++ name,
            *:if (opt.present) {options:<brack_group group_text(opt)>,options_raw:group_text(opt)} else {},
            content:content.value>}
    }
    else if (contains(["color", "textcolor", "colorbox"], name)) {
        let opt = math_optional(tokens,math_nonspace(tokens,index + 1)^)^;
        let at = if (opt.issue == null) math_nonspace(tokens,opt.end)^ else index;
        let raw = if (opt.issue != null) opt else if (at < len(tokens) and opening(tokens[at]))
            math_dimension_argument(tokens,at)^ else {issue:null,value:null};
        let color = if (raw.issue != null) raw else math_script_arg(tokens,at)^;
        let body = if (color.issue != null) color else if (name == "color") {issue:null,index:color.index}
            else if (name == "colorbox") math_text_argument(tokens,color.index)^
            else math_script_arg(tokens,color.index)^;
        if (body.issue != null) body
        else {
            let attrs = {cmd:"\\" ++ name, color:color.value,
                *:if (raw.value != null) {color_raw:raw.value} else {},
                *:if (opt.present) {options:<brack_group group_text(opt)>} else {},
                *:if (name == "color") {} else {content:body.value}};
            {issue:null,index:body.index,value:if (name == "color") <color_switch *:attrs> else <color_command *:attrs>}
        }
    }
    else if (name == "fcolorbox") {
        let border = math_dimension_argument(tokens,index + 1)^;
        let background = if (border.issue != null) border else math_dimension_argument(tokens,border.index)^;
        let body = if (background.issue != null) background else math_text_argument(tokens,background.index)^;
        if (body.issue != null) body
        else {issue:null,index:body.index,value:<color_command cmd:"\\fcolorbox",
            border_color:border.value,color_raw:background.value,content:body.value>}
    }
    // stackrel uses the same native AST command as overset.
    else if (contains(["overset", "underset", "stackrel"], name)) {
        let args = math_arguments(tokens, index + 1, 2)^;
        if (args.issue != null) args
        else {issue: null, index: args.index, value: <overunder_command
            cmd: if (name == "stackrel") "\\overset" else "\\" ++ name,
            annotation: args.items[0], base: args.items[1]>}
    }
    else if (contains(["frac", "dfrac", "tfrac", "cfrac", "binom", "dbinom", "tbinom"], name)) {
        let at = math_nonspace(tokens,index + 1)^;
        let opt = if (name == "cfrac") math_optional(tokens,at)^ else {issue:null, present:false, end:at};
        let args = if (opt.issue != null) opt else math_arguments(tokens, opt.end, 2)^;
        if (args.issue != null) args
        else {
            // Both forms share the renderer's numerator and denominator contract.
            let value = if (contains(["binom", "dbinom", "tbinom"], name))
                <binomial cmd:"\\" ++ name, numer:args.items[0], denom:args.items[1]>
                else <fraction cmd:"\\" ++ name, *:if (opt.present) {options:<brack_group group_text(opt)>} else {},
                    numer:args.items[0], denom:args.items[1]>;
            {issue:null, index:args.index, value:value}
        }
    }
    else if (name == "nicefrac" or name == "sfrac") {
        let options = math_optional(tokens,math_nonspace(tokens,index + 1)^)^;
        let numer = if (options.issue != null) options else math_script_arg(tokens,options.end)^;
        let separator = if (numer.issue != null) numer else math_optional(tokens,math_nonspace(tokens,numer.index)^)^;
        let sep = if (separator.issue != null or not separator.present) separator else math_atoms(separator.tokens)^;
        let denom = if (sep.issue != null) sep else math_script_arg(tokens,separator.end)^;
        if (denom.issue != null) denom
        else {issue:null,index:denom.index,value:<fraction cmd:"\\" ++ name,numer:numer.value,denom:denom.value,
            *:if (separator.present) {separator:<brack_group *math_content(sep.items)^>} else {}>}
    }
    else if (name == "genfrac") {
        let left = math_delimiter_token(tokens, index + 1, false, true)^;
        let right = if (left.issue != null) left else math_delimiter_token(tokens, left.index, false, true)^;
        let thickness = if (right.issue != null) right else math_dimension_argument(tokens, right.index)^;
        let style = if (thickness.issue != null) thickness else math_dimension_argument(tokens, thickness.index)^;
        let args = if (style.issue != null) style else math_arguments(tokens, style.index, 2)^;
        if (args.issue != null) args
        else {issue:null, index:args.index, value:<genfrac left_delim:left.value, right_delim:right.value,
            thickness:thickness.value, style:style.value, numer:args.items[0], denom:args.items[1]>}
    }
    else if (name == "includegraphics") {
        let at = math_nonspace(tokens,index + 1)^;
        let opt = if (at < len(tokens) and tokens[at].kind == "char" and tokens[at].value == "[")
            {*:read_generic_delimited(tokens,at,"[","]","latex-digest-unclosed-optional")^, present:true}
            else {issue:null, present:false, end:at};
        let next = if (opt.issue == null) math_nonspace(tokens,opt.end)^ else at;
        if (opt.issue != null) opt
        else if (next >= len(tokens) or not opening(tokens[next])) unsupported(index,"includegraphics source")
        else {
            let path = math_dimension_argument(tokens,next)^;
            let options = if (opt.present) output_between(tokens[at],tokens[opt.end - 1])^ else null;
            if (path.issue != null) path
            else if (opt.present and options == null) unsupported(index,"includegraphics options source mapping")
            else {issue:null, index:path.index, value:<image_command cmd:"\\includegraphics", src:path.value,
                *:if (opt.present) {options:options} else {}>}
        }
    }
    else if (contains(["xrightarrow", "xleftarrow", "xleftrightarrow", "xRightarrow", "xLeftarrow",
        "xLeftrightarrow", "xhookrightarrow", "xhookleftarrow", "xtwoheadrightarrow", "xtwoheadleftarrow",
        "xmapsto", "xrightleftharpoons", "xleftrightharpoons", "xrightleftarrows", "xrightequilibrium", "xleftequilibrium"], name)) {
        let opt = math_optional(tokens, math_nonspace(tokens,index + 1)^)^;
        let lower = if (opt.issue != null or not opt.present) opt else math_atoms(opt.tokens)^;
        let upper = if (lower.issue != null) lower else math_script_arg(tokens, opt.end)^;
        if (upper.issue != null) upper
        else {issue:null, index:upper.index, value:<extended_arrow cmd:"\\" ++ name, upper:upper.value,
            *:if (opt.present) {lower:<brack_group *math_content(lower.items)^>} else {}>}
    }
    else if (contains(["bmod", "pmod", "mod", "pod"], name)) {
        let body = if (name == "bmod") {issue:null, index:index + 1} else math_script_arg(tokens, index + 1)^;
        if (body.issue != null) body
        else {issue:null, index:body.index, value:<mod_command cmd:"\\" ++ name,
            *:if (name == "bmod") {} else {body:body.value}>}
    }
    else if (contains(["tiny", "sixptsize", "scriptsize", "footnotesize", "small", "normalsize",
        "large", "Large", "LARGE", "huge", "Huge"], name))
        {issue:null, index:index + 1, value:<size_command cmd:"\\" ++ name>}
    else if (contains(["big", "bigl", "bigr", "bigm", "Big", "Bigl", "Bigr", "Bigm",
        "bigg", "biggl", "biggr", "biggm", "Bigg", "Biggl", "Biggr", "Biggm"], name)) {
        let at = math_nonspace(tokens,index + 1)^;
        let next = if (at < len(tokens)) tokens[at] else null;
        let has_delimiter = next != null and
            ((next.kind == "char" and contains(["(", ")", "[", "]", "{", "}", "|", "<", ">", "/", "."],next.value)) or
             (next.kind == "control" and math_delimiter_control(next.value)));
        let delimiter = if (has_delimiter) math_delimiter_token(tokens,at,false,true)^
            else {issue:null,index:at};
        if (delimiter.issue != null) delimiter
        else {issue:null,index:delimiter.index,value:<sized_delimiter size:"\\" ++ name,
            *:if (has_delimiter) {delim:delimiter.value} else {}>}
    }
    // Native math digestion treats the vcenter body as a script argument.
    else if (name == "vcenter") {
        let body = math_script_arg(tokens, index + 1)^;
        if (body.issue != null) body
        else {issue: null, index: body.index, value: <box_transform cmd: "\\vcenter", body: body.value>}
    }
    // atom class commands retain their body and spacing class in the math AST.
    else if (contains(["mathord", "mathop", "mathbin", "mathrel", "mathopen", "mathclose", "mathpunct", "mathinner"], name)) {
        let body = math_script_arg(tokens, index + 1)^;
        if (body.issue != null) body
        else {
            let node = if (name == "mathop") <mathop body: body.value>
                else <math_atom cmd: "\\" ++ name, atom: "m" ++ slice(name, 4, len(name)), body: body.value>;
            {issue: null, index: body.index, value: node}
        }
    }
    else if (name == "mathchoice") {
        let args = math_arguments(tokens, index + 1, 4)^;
        if (args.issue != null) args
        else {issue: null, index: args.index, value: <mathchoice display: args.items[0], text: args.items[1],
            script: args.items[2], scriptscript: args.items[3]>}
    }
    else if (name == "left") math_delimiter_group(tokens, index)
    // style declarations change following math without consuming an argument.
    else if (contains(["displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle"], name))
        {issue: null, index: index + 1, value: <style_command cmd: "\\" ++ name>}
    else if (name == "operatorname") {
        let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
        let star = next != null and next.kind == "char" and next.value == "*";
        let adjacent = star and math_adjacent(tokens[index], next);
        if (star and not adjacent) unsupported(index, "operatorname star source mapping")
        else {
            let arg = math_script_arg(tokens, index + if (star) 2 else 1)^;
            if (arg.issue != null) arg
            else {issue: null, index: arg.index,
                value: <style_command cmd: "\\operatorname", *:if (star) {limits: true} else {}, arg: arg.value>}
        }
    }
    else if (contains(["mathrm", "mathbf", "boldsymbol", "mathit", "mathsf", "mathsfit",
        "mathtt", "mathcal", "mathbb", "mathfrak", "mathscr", "mathnormal"], name)) {
        let arg = math_script_arg(tokens, index + 1)^;
        if (arg.issue != null) arg
        else {issue: null, index: arg.index, value: <style_command cmd: "\\" ++ name, arg: arg.value>}
    }
    else if (contains(["hat", "bar", "vec", "dot", "ddot", "dddot", "ddddot", "tilde", "acute", "breve", "check", "grave",
        "widehat", "widetilde", "widecheck", "overline", "underline", "underbar", "overbrace", "underbrace", "overbracket",
        "underbracket", "overrightarrow", "Overrightarrow", "underrightarrow", "overleftarrow", "underleftarrow",
        "overleftrightarrow", "underleftrightarrow", "overleftharpoon", "overrightharpoon", "overgroup", "undergroup",
        "overlinesegment", "underlinesegment", "utilde", "mathring"], name)) {
        let at = math_nonspace(tokens, index + 1)^;
        if (at >= len(tokens)) {issue: null, index: at, value: <accent cmd: "\\" ++ name>}
        else {
            let base = math_script_arg(tokens, at)^;
            if (base.issue != null) base
            else {issue: null, index: base.index, value: <accent cmd: "\\" ++ name, base: base.value>}
        }
    }
    else if (name == "ne" or name == "neq")
        {issue: null, index: index + 1, value: <not_overlay target: "=">}
    else if (name == "not") {
        let at = math_nonspace(tokens,index + 1)^;
        if (at + 1 < len(tokens) and opening(tokens[at]) and closing(tokens[at + 1]) and
                math_adjacent(tokens[at],tokens[at + 1]))
            {issue:null,index:at + 2,value:<not_empty>}
        else {
            let target = math_script_arg(tokens,at)^;
            if (target.issue != null) target
            else {issue:null,index:target.index,value:<not_overlay target:target.value>}
        }
    }
    else if (name == "sqrt") {
        let at = math_nonspace(tokens, index + 1)^;
        let opt = math_optional(tokens, at)^;
        if (opt.issue != null) opt
        else {
            let degree = if (opt.present) math_atoms(opt.tokens)^ else {issue: null, items: []};
            if (degree.issue != null) degree
            else {
                let body = math_script_arg(tokens, opt.end)^;
                if (body.issue != null) body
                else {
                    let idx = <brack_group *math_content(degree.items)^>;
                    let value = if (opt.present) <radical index: idx, radicand: body.value>
                        else <radical radicand: body.value>;
                    {issue: null, index: body.index, value: value}
                }
            }
        }
    }
    else if (contains(["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
        "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
        "upsilon", "phi", "chi", "psi", "omega", "Gamma", "Delta", "Theta", "Lambda", "Xi",
        "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega", "infty", "partial", "nabla", "cdot",
        "times", "div", "pm", "mp", "leq", "geq", "approx", "equiv", "in", "propto", "subset", "supset",
        "notin", "forall", "exists", "to", "mapsto", "rightarrow", "leftarrow", "leftrightarrow",
        "circ", "ast", "star", "bullet", "oplus", "otimes", "odot", "oslash", "wedge", "vee", "cap", "cup",
        "sqcap", "sqcup", "triangleleft", "triangleright", "wr", "bigcirc", "diamond", "bigtriangleup",
        "bigtriangledown", "boxplus", "boxminus", "boxtimes", "boxdot", "square", "blacksquare", "parallel",
        "perp", "angle", "triangle", "cong", "sim", "simeq", "prec", "succ", "preceq", "succeq", "ll", "gg",
        "asymp", "bowtie", "models", "vdash", "dashv", "top", "bot", "neg", "lnot"], name))
        {issue: null, index: index + 1, value: <symbol_command name: name>}
    else if (contains(["sum", "prod", "coprod", "int", "iint", "iiint", "oint", "intop", "oiint", "oiiint", "lim", "sin", "cos", "tan", "log", "ln", "exp",
        "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "arsinh", "arcosh", "artanh",
        "sech", "csch", "coth", "lg", "max", "min", "arg", "det", "gcd", "lcm", "deg", "dim", "ker", "hom",
        "limsup", "liminf", "sup", "inf"], name))
        {issue: null, index: index + 1, value: <command name: name>}
    else if (math_unported_special(name)) unsupported(index, "math command " ++ name)
    // Native math preserves unknown commands and their adjacent argument groups.
    else {
        let args = math_generic_arguments(tokens, index + 1)^;
        if (args.issue != null) args else {issue: null, index: args.index, value: <command name: name, *args.items>}
    }
}

// the native math scanner shares punctuation rules between atoms and script arguments.
fn math_punctuation(tokens, index) any^ {
    let token = if (index < len(tokens)) tokens[index] else null;
    if (token == null or token.kind != "char" or len(token.value) != 1 or ord(token.value) >= 128)
        unsupported(index, "math punctuation")
    else {
        let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
        let adjacent = next != null and next.kind == "char" and math_adjacent(token,next);
        let pair = if (adjacent) token.value ++ next.value else "";
        let joined = contains(["<=", ">=", "!=", "->", ";;"], pair);
        let value = if (token.value == "&") 'col_sep' else if (pair == ";;") 'row_sep'
            else if (contains(["<=", ">=", "!=", "->"], pair)) <relation value:pair>
            else if (contains(["<", ">", "="], token.value)) <relation value:token.value>
            else if (contains(["+", "-", "*", "/"], token.value)) <operator value:token.value>
            else <punctuation value:token.value>;
        {issue:null, index:index + if (joined) 2 else 1, value:value}
    }
}

fn math_script_arg(tokens, index) any^ {
    let at = math_nonspace(tokens, index)^;
    if (at >= len(tokens)) unsupported(at, "math script argument")
    else if (opening(tokens[at])) {
        let group = read_group(tokens, at)^;
        if (group.issue != null) group
        else {
            let contents = math_list(group.tokens)^;
            if (contents.issue != null) contents
            else {issue: null, index: group.end, value: <group *math_content(contents.items)^>}
        }
    }
    else if (math_letter(tokens[at]) or math_digit(tokens[at]))
        {issue: null, index: at + 1, value: tokens[at].value}
    // One unbraced argument is one code point, even when its UTF-8 encoding spans bytes.
    else if (math_unicode(tokens[at])^)
        if (math_positioned(tokens[at])) {issue:null, index:at + 1, value:<unicode_text value:tokens[at].value>}
        else unsupported(at, "math unicode source mapping")
    else if (tokens[at].kind == "control") math_command(tokens, at)
    else if (tokens[at].kind == "char" and len(tokens[at].value) == 1 and ord(tokens[at].value) < 128)
        math_punctuation(tokens, at)
    else unsupported(at, "math script argument")
}

// Consecutive primes form one superscript group before a following ^ argument.
fn math_primes(tokens, index, items = []) any^ {
    if (index >= len(tokens) or tokens[index].kind != "char" or tokens[index].value != "'" or
        (len(items) > 0 and not math_adjacent(tokens[index - 1], tokens[index])))
        {index:index, items:items}
    else math_primes(tokens, index + 1, [*items, <symbol_command name:"prime">])
}

fn math_scripts(tokens, index, base, sub = null, sup = null, modifier = null) any^ {
    let at = math_nonspace(tokens, index)^;
    let token = if (at < len(tokens)) tokens[at] else null;
    if (token != null and token.kind == "control" and contains(["limits", "nolimits"], token.value))
        math_scripts(tokens, at + 1, base, sub, sup, token.value)
    else if (token != null and token.kind == "char" and token.value == "'") {
        let primes = math_primes(tokens, at)^;
        let upper = <group *(if (sup == null) primes.items else [sup, *primes.items])>;
        math_scripts(tokens, primes.index, base, sub, upper, modifier)
    }
    else if (token != null and token.kind == "char" and (token.cat == 7 or token.cat == 8)) {
        let superscript = token.cat == 7;
        let arg = math_script_arg(tokens, at + 1)^;
        if (arg.issue != null) arg
        else {
            // native accumulates superscripts and replaces an earlier subscript.
            let upper = if (superscript and sup != null) <group *[sup, arg.value]> else arg.value;
            math_scripts(tokens, arg.index, base, if (superscript) sub else arg.value,
                if (superscript) upper else sup, modifier)
        }
    }
    else {
        let extra = if (modifier == null) {} else {modifier:modifier};
        let value = if (sub != null and sup != null) <subsup base: base, sub: sub, sup: sup, *:extra>
            else if (sub != null) <subsup base: base, sub: sub, *:extra>
            else if (sup != null) <subsup base: base, sup: sup, *:extra> else base;
        {issue: null, index: at, value: value}
    }
}

fn math_atoms(tokens, index = 0, items = []) any^ {
    if (index >= len(tokens)) {items: items, issue: null}
    else {
        let token = tokens[index];
        if (token.kind == "char" and token.cat == 10) math_atoms(tokens, index + 1, items)
        // a braced group is an atom and can itself carry scripts.
        else if (opening(token)) {
            let group = math_script_arg(tokens, index)^;
            if (group.issue != null) group
            else {
                let scripted = math_scripts(tokens, group.index, group.value)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        else if (token.kind == "char" and contains(["#", "!"], token.value)) {
            let raw = math_raw_run(tokens, index)^;
            if (raw.issue != null) raw
            else {
                let scripted = math_scripts(tokens, raw.index, <raw_math_text value: raw.text>)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        // native math treats a decimal prefix and an ellipsis differently from a period.
        else if (token.kind == "char" and token.value == ".") {
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let third = if (index + 2 < len(tokens)) tokens[index + 2] else null;
            let joined = next != null and math_adjacent(token, next);
            let dots = joined and next.kind == "char" and next.value == "." and third != null and
                third.kind == "char" and third.value == "." and math_adjacent(next, third);
            let decimal_prefix = joined and math_digit(next);
            let numeric = if (decimal_prefix) math_number(tokens, index)^ else null;
            let value = if (decimal_prefix) numeric.text else if (dots) <raw_math_text value: "..."> else <punctuation value: ".">;
            let after = if (decimal_prefix) numeric.index else index + if (dots) 3 else 1;
            let scripted = math_scripts(tokens, after, value)^;
            if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
        }
        else if (math_unicode(token)^) {
            let run = math_text_run(tokens, index, true)^;
            if (run.issue != null) run
            else {
                let scripted = math_scripts(tokens, run.index, <unicode_text value: run.text>)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        else if (math_letter(token) or math_digit(token)) {
            let numeric = if (math_digit(token)) math_number(tokens, index)^ else {index: index + 1, text: token.value};
            let scripted = math_scripts(tokens, numeric.index, numeric.text)^;
            if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
        }
        else if (token.kind == "control") {
            let atom = math_command(tokens, index)^;
            if (atom.issue != null) atom
            else {
                let scripted = math_scripts(tokens, atom.index, atom.value)^;
                if (scripted.issue != null) scripted else math_atoms(tokens, scripted.index, [*items, scripted.value])
            }
        }
        // Remaining ASCII characters have the native punctuation shape.
        else if (token.kind == "char" and len(token.value) == 1 and ord(token.value) < 128) {
            let atom = math_punctuation(tokens,index)^;
            if (atom.issue != null) atom else {
                let scripted = math_scripts(tokens,atom.index,atom.value)^;
                if (scripted.issue != null) scripted else math_atoms(tokens,scripted.index,[*items,scripted.value])
            }
        }
        else unsupported(index, "math atom")
    }
}

// TeX's infix fraction consumes the enclosing math list, not only the next atom.
fn math_infix_index(tokens, index = 0, depth = 0) any^ {
    if (index >= len(tokens)) null
    else {
        let token = tokens[index];
        if (depth == 0 and token.kind == "control" and
                contains(["over", "atop", "above", "choose", "brace", "brack"], token.value)) index
        else math_infix_index(tokens,index + 1,depth + if (opening(token)) 1 else if (closing(token)) -1 else 0)
    }
}

// The lexer normalizes source whitespace to one space token; retain the
// original line endings when validating reconstructed math bytes.
fn math_reconstructed_literal(token) any^ {
    if (token.cat != 10) reconstructed_literal(token)
    else {
        let raw = utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^;
        len(raw) > 0 and all([for (c in split(raw, "")^) contains([" ","\t","\r","\n",chr(11),chr(12)],c)])
    }
}
fn math_list(tokens) any^ {
    let at = math_infix_index(tokens)^;
    if (at == null) math_atoms(tokens)
    else {
        let name = tokens[at].value;
        let thickness = if (name == "above") math_dimension_argument(tokens,at + 1)^
            else {issue:null, index:at + 1};
        let numer = if (thickness.issue != null) thickness else math_atoms(slice(tokens,0,at))^;
        let denom = if (numer.issue != null) numer else math_atoms(slice(tokens,thickness.index,len(tokens)))^;
        if (denom.issue != null) denom
        else {issue:null, items:[<infix_frac cmd:"\\" ++ name,
            numer:<group *math_content(numer.items)^>,
            *:if (name == "above") {thickness:thickness.value} else {},
            denom:<group *math_content(denom.items)^>>]}
    }
}

fn math_element(tokens, sources, index) any^ {
    let named = control(tokens[index], "(") or control(tokens[index], "[");
    let display = if (named) control(tokens[index], "[") else index + 1 < len(tokens) and dollar(tokens[index + 1]);
    let first = if (display and not named) index + 2 else index + 1;
    let closing = if (not named) null else if (display) "]" else ")";
    let close = math_close(tokens, first, display, closing)^;
    if (close.issue != null) close
    else {
        let start = tokens[first - 1];
        let finish = tokens[close.index];
        let body = slice(tokens, first, close.index);
        let direct = start.direct == true and finish.direct == true and start.file == finish.file and
            all([for (token in body) token.direct == true and token.file == start.file]);
        let mapped = start.output_text != null and (not direct or start.read_offset != null or
            any([for (token in body) math_source_normalized(token,sources)^]) or
            any([for (i in (index + 1 to close.index)^) tokens[i].seq != null and tokens[i - 1].seq != null and
                tokens[i].file == tokens[i - 1].file and tokens[i].seq != tokens[i - 1].seq + 1]));
        // mapped math must agree with native lexical categories as well as bytes.
        let reconstructed = mapped and all([for (i in (index to close.end - 1)^)
            tokens[i].output_start != null and tokens[i].output_end != null and reconstructed_token(tokens[i])^ and
            (tokens[i].kind != "char" or (math_reconstructed_literal(tokens[i])^ and
                ((tokens[i].value == "^") == (tokens[i].cat == 7)) and
                ((tokens[i].value == "_") == (tokens[i].cat == 8)))) and
            (i == index or (tokens[i - 1].output_end != null and tokens[i].output_start >= tokens[i - 1].output_end and
                (trim(output_between(tokens[i - 1],tokens[i])^) ^ { null }) == ""))]);
        if ((not direct and not mapped) or (mapped and not reconstructed)) unsupported(index, "math source mapping")
        else {
            let source = if (mapped) output_between(start,finish)^ else
                source_span({direct: true, file: start.file, start: start.end, end: finish.start}, sources)^;
            let offset = if (mapped and source != null) rebuild.map_offset(start.output_segments,start.output_end)^ else start.end;
            // Text arguments need serialized positions even when the enclosing
            // formula otherwise uses direct source spans.
            let math_output = rebuild.reconstruct({output:body, sources:sources, main_file:start.file}, true)^;
            let math_tokens = if (math_output.issue == null) mark_output_tokens(body, math_output, false)^ else body;
            let atoms = math_list(math_tokens)^;
            if (source == null) unsupported(index, "math source mapping")
            else if (atoms.issue != null) atoms
            else {
                let ast = <math *math_content(atoms.items)^>;
                let node = if (display) <display_math source_offset: offset, source: source, ast: ast>
                    else <inline_math source_offset: offset, source: source, ast: ast>;
                {issue: null, index: close.end, node: node}
            }
        }
    }
}

fn span_tokens(input, offset, limit, tokens = []) any^ {
    let read = tex_reader.next(input)^;
    if (read == null or read.token.start >= limit) tokens
    else span_tokens(read.reader, offset, limit, [*tokens,
        {*:read.token, start: read.token.start + offset, end: read.token.end + offset}])
}

fn definition_node(name, attrs, children) => <[name] *:attrs, *children>

// Inline definitions leave a separated argument to their enclosing group parser.
fn inline_definition_partial(cmd, starred, groups, children, end_offset, trailing_text = "") any^ {
    let attrs = {source_offset:cmd.start, *:if (starred) {starred:true} else {}, argument_groups:groups};
    {issue:null, end_offset:end_offset, nodes:[definition_node(cmd.value,attrs,children),
        *(if (trailing_text == "") [] else [trailing_text])]}
}

fn definition_target(tokens, sources, index) any^ {
    if (index >= len(tokens)) unsupported(index, "definition target")
    else if (opening(tokens[index])) {
        let group = read_group(tokens, index)^;
        if (group.issue != null) group
        else {
            let raw = group_raw(group, sources)^;
            if (raw == null) unsupported(index, "definition target source")
            else {issue: null, end: group.end, value: raw,
                descriptor: {kind: "required", raw: raw, start: group.start_offset, end: group.end_offset}}
        }
    }
    else if (tokens[index].kind == "control") {
        let target = tokens[index];
        let raw = source_span(target, sources)^;
        if (raw == null) unsupported(index, "definition target source")
        else {issue: null, end: index + 1, value: raw,
            descriptor: {kind: "required", raw: raw, start: target.start, end: target.end}}
    }
    else unsupported(index, "definition target")
}

fn definition_option(tokens, sources, index) any^ {
    let present = index < len(tokens) and tokens[index].kind == "char" and tokens[index].value == "[";
    let opt = if (present) read_generic_delimited(tokens, index, "[", "]", "latex-digest-unclosed-optional")^
        else {issue: null, present: false, end: index};
    if (opt.issue != null or not present) opt
    else {
        let raw = group_raw(opt, sources)^;
        // Inline parsing reconstructs skipped comment gaps while retaining raw option bytes.
        let parsed = inline_group(opt, sources)^;
        if (raw == null) unsupported(index, "definition option source")
        else if (parsed.issue != null) parsed
        else if (parsed.index != len(parsed.tokens)) unsupported(index, "definition option")
        else {*:opt, present: true, value: <brack_group *parsed.items>, descriptor: {kind: "optional", raw: raw,
            start: opt.start_offset, end: opt.end_offset}}
    }
}

fn definition_space_end(tokens, index) any^ => if (index < len(tokens) and
    ((tokens[index].kind == "char" and tokens[index].cat == 10) or control(tokens[index],"par")))
    definition_space_end(tokens,index + 1) else index

// Native skips a comment and following whitespace when another argument follows.
fn definition_comment_join(tokens, sources, index, previous_end, file) any^ {
    let at = definition_space_end(tokens,index)^;
    let next = if (at < len(tokens)) tokens[at] else null;
    let eligible = next != null and next.direct == true and next.file == file and next.start >= previous_end and
        (opening(next) or (next.kind == "char" and next.value == "["));
    let gap = if (eligible) source_span({direct:true,file:file,start:previous_end,end:next.start},sources)^ else null;
    if (comment_gap(gap)^) at else index
}

fn definition_prefixes(tokens, sources, index = 0, items = []) any^ {
    if (index >= len(tokens) or tokens[index].kind != "control" or
        not contains(["global", "long", "protected"], tokens[index].value)) {index: index, items: items}
    else {
        let token = tokens[index];
        let node = if (token.value == "global") <global source_offset: token.start, argument_groups: []>
            else if (token.value == "long") <long source_offset: token.start, argument_groups: []>
            else <protected source_offset: token.start, argument_groups: []>;
        let gap = control_gap(tokens, sources, index)^;
        definition_prefixes(tokens, sources, index + 1,
            [*items, node, *(if (gap.text == "") [] else [gap.text])])
    }
}

fn definition_body_start(tokens, index) any^ {
    if (index >= len(tokens)) unsupported(index, "definition body")
    else if (opening(tokens[index])) {issue: null, index: index}
    else if (tokens[index].kind != "char" or closing(tokens[index])) unsupported(index, "definition parameter text")
    else definition_body_start(tokens, index + 1)
}

fn definition_head(target, first, second) => {
    groups: [target.descriptor, *(if (first.present) [first.descriptor] else []),
        *(if (second.present) [second.descriptor] else [])],
    children: [target.value, *(if (first.present) [first.value] else []),
        *(if (second.present) [second.value] else [])]}

// paragraph breaks inside groups belong to the nested inline parse.
fn definition_outer_paragraph(tokens, index) any^ {
    if (index >= len(tokens)) -1
    else if (control(tokens[index],"par") or closing(tokens[index])) index
    else if (opening(tokens[index])) {
        let group = group_end(tokens,index + 1,1)^;
        if (group.issue != null) index else definition_outer_paragraph(tokens,group.end)^
    }
    else definition_outer_paragraph(tokens,index + 1)
}

// Keep preceding whitespace with the break so paragraph reconstruction sees both line endings.
fn definition_paragraph_start(tokens, minimum, boundary) any^ =>
    if (boundary > minimum and tokens[boundary - 1].kind == "char" and tokens[boundary - 1].cat == 10)
        definition_paragraph_start(tokens,minimum,boundary - 1) else boundary

// source whitespace ends native definition arguments even when TeX scans further.
fn definition_partial(tokens, sources, cmd, starred, groups, children, index, text = "") any^ {
    let boundary = definition_outer_paragraph(tokens,index)^;
    let start = if (boundary < 0) -1 else definition_paragraph_start(tokens,index,boundary)^;
    let prefix = if (start < 0) tokens else slice(tokens,0,start);
    let tail = if (boundary >= 0 and not control(tokens[boundary],"par"))
        unsupported(boundary, "definition paragraph source mapping") else inline(prefix, sources, index, text, [], false)^;
    if (tail.issue != null) tail
    else if (tail.index != len(tail.tokens)) unsupported(index, "definition source remainder")
    else {
        let attrs = {source_offset: cmd.start, *:if (starred) {starred: true} else {}, argument_groups: groups};
        {issue: null, nodes: [definition_node(cmd.value, attrs, children), *tail.items],
            *:if (start < 0) {} else {rest_tokens:slice(tokens,start,len(tokens))}}
    }
}

// The document parser scans command names from source bytes, independent of TeX categories.
fn definition_native_tokens(tokens, sources, index = 0, out = []) any^ {
    if (index >= len(tokens)) {issue:null,tokens:out}
    else {
        let token=tokens[index];
        let chars=if(token.kind == "control") split(token.value, "")^ else [];
        let word=native_word_end(chars)^;
        let cut=if(word == 0 and len(chars) > 0) 1 else word;
        if (token.kind == "control" and source_span(token,sources)^ == "\\" ++ token.value and
                len(chars) > 1 and cut < len(chars)) {
            let suffix=slice(chars,cut,len(chars)); let end=token.start + 1 + cut;
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let raw_gap = if (next != null and next.direct == true and next.file == token.file and next.start > token.end)
                source_span({direct:true,file:token.file,start:token.end,end:next.start},sources)^ else "";
            let gap = control_gap(tokens,sources,index)^;
            let normalized_gap = if (raw_gap != null and raw_gap != "") source_gap_bytes(binary(raw_gap)^)^ else null;
            let restored = if (gap.token != null) gap.token else if (normalized_gap != null and normalized_gap.valid)
                {kind:"char",value:" ",cat:10,start:token.end,end:next.start,file:token.file,
                    direct:true,seq:token.seq,normalize_source_gap:true} else null;
            if (not all([for(c in suffix) native_word_suffix_char(c)^]) or
                    token.end != token.start + 1 + len(chars) or (raw_gap != "" and restored == null))
                unsupported(index,"definition command source mapping")
            else {
                let head={*:token,value:join(slice(chars,0,cut), ""),end:end};
                let rest=[for(i,c in suffix) {*:token,kind:"char",value:c,cat:12,start:end + i,end:end + i + 1}];
                // TeX consumed the gap after the original control word, but native sees it after the suffix.
                definition_native_tokens([*slice(tokens,0,index),head,*rest,*(if (restored == null) [] else [restored]),
                    *slice(tokens,index + 1,len(tokens))],sources,index,out)
            }
        }
        else if (token.kind == "control" and source_span(token,sources)^ == "\\" ++ token.value and
                len(chars) > 0 and word == len(chars)) {
            let end=native_join_end(tokens,sources,index + 1,token.end,token.file)^;
            let name=token.value ++ join([for(t in slice(tokens,index + 1,end)) t.value], "")^;
            if (len(name) >= 95) unsupported(index,"definition command source mapping")
            else {
                let joined=if(end == index + 1) token else {*:token,value:name,end:tokens[end - 1].end,seq:tokens[end - 1].seq};
                definition_native_tokens(tokens,sources,end,[*out,joined])
            }
        }
        else definition_native_tokens(tokens,sources,index + 1,[*out,token])
    }
}

fn definition_span(span, sources, allow_tail = false, mapped_end = null) any^ {
    {
        // echoed definitions are parsed from raw bytes by the native digester.
        let input = {*:tex_reader.reader(span.value, span.file), catcodes: span.catcodes, preserve_source:true};
        let read_tokens = span_tokens(input, span.start, len(binary(span.value)^))^;
        let marked = [for (token in read_tokens) {*:token, definition_catcodes: span.catcodes}];
        let normalized = definition_native_tokens(marked,sources)^;
        let tokens = if(normalized.issue == null) normalized.tokens else marked;
        let prefix = definition_prefixes(tokens, sources)^;
        let base = prefix.index;
        let cmd = if (base >= len(tokens)) null else tokens[base];
        let supported = cmd != null and cmd.kind == "control" and tex_reader.definition_command(cmd.value);
        if (normalized.issue != null) normalized
        else if (cmd != null and cmd.kind == "control" and contains(["DeclareRobustCommand", "NewDocumentCommand",
                "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand", "NewDocumentEnvironment",
                "RenewDocumentEnvironment", "ProvideDocumentEnvironment", "DeclareDocumentEnvironment"], cmd.value)) {
            let parsed = inline(tokens, sources, 0, "", [], false)^;
            if (parsed.issue != null) parsed
            else if (parsed.index != len(parsed.tokens)) unsupported(parsed.index, "document command definition")
            else {issue: null, nodes: parsed.items}
        }
        else if (not supported) unsupported(0, "definition command")
        else {
            let primitive = contains(["def", "gdef", "edef", "xdef"], cmd.value);
            let environment = cmd.value == "newenvironment" or cmd.value == "renewenvironment";
            let starred = not primitive and base + 1 < len(tokens) and tokens[base + 1].kind == "char" and tokens[base + 1].value == "*";
            let target_at = base + if (starred) 2 else 1;
            let anchor = tokens[target_at - 1];
            let next = if (target_at < len(tokens)) tokens[target_at] else null;
            let command_gap = if (environment and next != null and next.start > anchor.end)
                source_span({direct: true, file: span.file, start: anchor.end, end: next.start}, sources)^ else "";
            let target = definition_target(tokens, sources, target_at)^;
            let after_target = if (target.issue != null or primitive) target.end else
                definition_comment_join(tokens,sources,target.end,target.descriptor.end,span.file)^;
            let first = if (target.issue != null) target else if (primitive) {issue: null, present: false, end: target.end}
                else definition_option(tokens, sources, after_target)^;
            let after_first = if (first.issue != null or primitive) first.end else
                definition_comment_join(tokens,sources,first.end,if(first.present)first.descriptor.end else target.descriptor.end,span.file)^;
            let second = if (first.issue != null) first else if (primitive) {issue: null, present: false, end: first.end}
                else definition_option(tokens, sources, after_first)^;
            let after_second = if (second.issue != null or primitive) second.end else
                definition_comment_join(tokens,sources,second.end,if(second.present)second.descriptor.end
                    else if(first.present)first.descriptor.end else target.descriptor.end,span.file)^;
            let body_at = if (second.issue != null) second else if (primitive) definition_body_start(tokens, second.end)^
                else {issue: null, index: after_second};
            let body = if (body_at.issue != null) body_at else read_group(tokens, body_at.index)^;
            if (environment and ((command_gap != null and command_gap != "" and (trim(command_gap) ^ { null }) == "") or
                    (next != null and next.kind == "char" and next.cat == 10))) {
                let partial = if (allow_tail) inline_definition_partial(cmd,starred,[],[],cmd.end,command_gap or "") else
                    definition_partial(tokens, sources, cmd, starred, [], [], target_at, command_gap)^;
                if (partial.issue != null) partial else {*:partial, nodes: [*prefix.items, *partial.nodes]}
            }
            // native digestion leaves an active definition target as following text.
            else if (primitive and next != null and next.kind == "active") {
                if (allow_tail) {issue:null, end_offset:cmd.end,
                    nodes:[*prefix.items, definition_node(cmd.value, {source_offset:cmd.start, argument_groups:[]}, [])]}
                else {
                    let partial = definition_partial(tokens, sources, cmd, starred, [], [], target_at)^;
                    if (partial.issue != null) partial else {*:partial, nodes: [*prefix.items, *partial.nodes]}
                }
            }
            else if (target.issue != null) target
            else if (first.issue != null) first
            else if (second.issue != null) second
            else if (body_at.issue != null) body_at
            else if (not primitive and body_at.index < len(tokens) and
                    tokens[body_at.index].kind == "char" and tokens[body_at.index].cat == 10) {
                let head = definition_head(target, first, second);
                let partial = if (allow_tail) inline_definition_partial(cmd,starred,head.groups,head.children,
                    if (second.present) second.descriptor.end else if (first.present) first.descriptor.end else target.descriptor.end) else
                    definition_partial(tokens, sources, cmd, starred, head.groups, head.children, body_at.index)^;
                if (partial.issue != null) partial else {*:partial, nodes: [*prefix.items, *partial.nodes]}
            }
            else if (body.issue != null) body
            else {
                let ending_at = if (environment) definition_comment_join(tokens,sources,body.end,body.end_offset,span.file)^ else body.end;
                let ending = if (environment) read_group(tokens, ending_at)^ else {issue: null, end: body.end};
                if (environment and ending_at == body.end and body.end < len(tokens) and tokens[body.end].kind == "char" and tokens[body.end].cat == 10) {
                    let raw = group_raw(body, sources)^;
                    let head = definition_head(target, first, second);
                    let groups = [*head.groups, {kind: "required", raw: raw, start: body.start_offset, end: body.end_offset}];
                    let children = [*head.children, <curly_group raw>];
                    let partial = if (raw == null) unsupported(body.end, "definition body source")
                        else if (allow_tail) inline_definition_partial(cmd,starred,groups,children,body.end_offset) else
                        definition_partial(tokens, sources, cmd, starred, groups, children, body.end)^;
                    if (partial.issue != null) partial else {*:partial, nodes: [*prefix.items, *partial.nodes]}
                }
                else if (ending.issue != null) ending
                else if (ending.end != len(tokens) and not allow_tail) unsupported(ending.end, "definition trailing tokens")
                else {
                    let raw = group_raw(body, sources)^;
                    let delimited = primitive and body_at.index > second.end;
                    let parameters = if (delimited) source_span({direct: true, file: span.file,
                        start: target.descriptor.end, end: tokens[body_at.index].start}, sources)^ else "";
                    let end_raw = if (environment) group_raw(ending, sources)^ else "";
                    let parsed = if (environment) {issue: null, index: len(body.tokens), items: [raw]}
                        else inline_group(body, sources)^;
                    if (raw == null or end_raw == null or parameters == null) unsupported(body.end, "definition body source")
                    else if (parsed.issue != null) parsed
                    else if (parsed.index != len(if (parsed.tokens == null) body.tokens else parsed.tokens)) unsupported(body.end, "definition body")
                    else {
                        let group = <curly_group *parsed.items>;
                        let end_group = <curly_group end_raw>;
                        let head = definition_head(target, first, second);
                        let body_end = if (not environment and mapped_end != null and body.end == len(tokens)) mapped_end else body.end_offset;
                        let ending_end = if (mapped_end != null and ending.end == len(tokens)) mapped_end else ending.end_offset;
                        let groups = [*head.groups,
                            *(if (delimited) [] else [{kind: "required", raw: raw, start: body.start_offset, end: body_end}]),
                            *(if (environment) [{kind: "required", raw: end_raw,
                                start: ending.start_offset, end: ending_end}] else [])];
                        let children = [*head.children, *(if (delimited) [] else [group]),
                            *(if (environment) [end_group] else [])];
                        let attrs = {source_offset: cmd.start, *:if (starred) {starred: true} else {}, argument_groups: groups};
                        {issue: null, end_offset: tokens[ending.end - 1].end, delimited: delimited,
                            nodes: [*prefix.items, definition_node(cmd.value, attrs, children),
                            *(if (delimited) [parameters, group] else [])]}
                    }
                }
            }
        }
    }
}

// generic delimited groups can contain nested delimiters, groups and commands.
fn generic_delimited_end(tokens, index, opener, closer, issue, depth = 1) any^ {
    // callers propagate scan failures through the positional parser result.
    if (index >= len(tokens)) {issue: issue, index: index, end: index}
    else {
        let token = tokens[index];
        let next = if (token.kind == "char" and token.value == opener) depth + 1
            else if (token.kind == "char" and token.value == closer) depth - 1 else depth;
        if (next == 0) {issue: null, end: index + 1}
        else generic_delimited_end(tokens, index + 1, opener, closer, issue, next)
    }
}

fn read_generic_delimited(tokens, index, opener, closer, issue) any^ {
    let end = generic_delimited_end(tokens, index + 1, opener, closer, issue)^;
    if (end.issue != null) end
    else {
        let inner = slice(tokens, index + 1, end.end - 1);
        let first = tokens[index]; let final_token = tokens[end.end - 1];
        let marked = opener == "(" and first.output_start != null and final_token.output_end != null and
            all([for (token in inner) token.output_start != null and token.output_end != null]);
        {issue: null, end: end.end, tokens: inner,
            start_offset: first.start, end_offset: final_token.end,
            file: first.file, direct: first.direct == true and final_token.direct == true and first.file == final_token.file,
            *:if (marked) {output_text:first.output_text, output_segments:first.output_segments,
                output_start:first.output_start, output_end:final_token.output_end} else {}}
    }
}

// direct tokens from an included file still need mapping into the main source.
fn expanded_source_group(group, main_file = null) => group.issue == null and
    (group.direct != true or (main_file != null and group.file != main_file) or
        any([for (token in group.tokens) token.direct != true or token.file != group.file]))

fn expanded_paren_after_command(tokens,index,main_file) any^ {
    if (index >= len(tokens)) false
    else {
        let token = tokens[index];
        if (token.kind == "char" and token.value == "*") expanded_paren_after_command(tokens,index + 1,main_file)
        else if (opening(token)) {
            let group = group_end(tokens,index + 1,1)^;
            group.issue == null and expanded_paren_after_command(tokens,group.end,main_file)^
        }
        else if (token.kind == "char" and token.value == "[") {
            let group = read_generic_delimited(tokens,index,"[","]","unclosed")^;
            group.issue == null and expanded_paren_after_command(tokens,group.end,main_file)^
        }
        else if (token.kind == "char" and token.value == "(") {
            let group = read_generic_delimited(tokens,index,"(",")","unclosed")^;
            group.issue == null and (expanded_source_group(group,main_file) or expanded_paren_after_command(tokens,group.end,main_file)^)
        }
        else false
    }
}

fn paren_command_scan(tokens, main_file, index = 0) any^ {
    if (index >= len(tokens)) false
    else if (tokens[index].kind == "control" and expanded_paren_after_command(tokens,index + 1,main_file)^) true
    else paren_command_scan(tokens,main_file,index + 1)
}

fn list_probe_space(tokens, index) any^ =>
    if (index < len(tokens) and ((tokens[index].kind == "char" and (tokens[index].cat == 10 or
            contains([chr(11),chr(12)],tokens[index].value))) or control(tokens[index],"par")))
        list_probe_space(tokens,index + 1) else index

fn list_environment(name) => contains(["itemize", "enumerate", "description"],name)
fn declaration_command(token) any^ => token.kind == "control" and
    contains(["documentclass", "usepackage", "title", "author", "date"], token.value)

// expanded command arguments require byte ranges from reconstructed output.
fn expanded_argument_sources(tokens, index, file, sources) any^ {
    if (index >= len(tokens)) false
    else {
        let token = tokens[index];
        if (token.kind == "char" and token.value == "*") expanded_argument_sources(tokens,index + 1,file,sources)
        else if (opening(token) or (token.kind == "char" and token.value == "[")) {
            let group = if (opening(token)) read_group(tokens,index)^
                else read_generic_delimited(tokens,index,"[","]","unclosed")^;
            // a macro may disappear entirely, leaving no generated interior token.
            let consumed = group.issue == null and any([for (i in (index + 1 to group.end - 1)^)
                tokens[i - 1].seq != null and tokens[i].seq != null and tokens[i].file == tokens[i - 1].file and
                tokens[i].seq != tokens[i - 1].seq + 1]);
            let normalized = group.issue == null and any([for (t in (group.tokens)^) math_source_normalized(t,sources)^]);
            group.issue == null and (expanded_source_group(group,file) or consumed or normalized or expanded_argument_sources(tokens,group.end,file,sources)^)
        }
        else false
    }
}

fn list_option_needs_mapping(tokens, main_file, index) any^ {
    let name = read_group(tokens,index + 1)^;
    let at = if (name.issue == null) list_probe_space(tokens,name.end)^ else len(tokens);
    let candidate = name.issue == null and plain_group(name) and list_environment(group_text(name)) and at < len(tokens) and
        tokens[at].kind == "char" and tokens[at].value == "[";
    let option = if (candidate) read_generic_delimited(tokens,at,"[","]","unclosed")^ else null;
    candidate and expanded_source_group(option,main_file)
}

// a direct token may still have been normalized by the TeX reader.
fn math_source_normalized(token, sources) any^ {
    if (token.direct != true or not contains(["char", "control", "active"], token.kind)) false
    else {
        let raw = source_span(token,sources)^;
        let emitted = rebuild.source_fragment(token,sources)^.body;
        raw == null or emitted == null or raw != emitted
    }
}

// expansion may insert math tokens or consume a macro without leaving tokens.
fn expanded_math(tokens, index, main_file, sources) any^ {
    let named = control(tokens[index], "(") or control(tokens[index], "[");
    let display = if (named) control(tokens[index], "[") else index + 1 < len(tokens) and dollar(tokens[index + 1]);
    let first = index + if (display and not named) 2 else 1;
    let closing = if (not named) null else if (display) "]" else ")";
    let end = math_close(tokens, first, display, closing)^;
    if (end.issue != null) {needed:false, end:index + 1}
    else {end:end.end, needed:any([for (i in (index to end.end - 1)^)
        tokens[i].direct != true or tokens[i].file != main_file or math_source_normalized(tokens[i],sources)^ or
        (i > index and tokens[i].seq != null and tokens[i - 1].seq != null and
         tokens[i].file == tokens[i - 1].file and tokens[i].seq != tokens[i - 1].seq + 1)])}
}

// consumed or foreign source requires adjacency in reconstructed output.
fn inline_star_needs_mapping(token, next) => next != null and
    (token.direct != true or next.direct != true or token.file != next.file or
        (token.seq != null and next.seq != null and next.seq != token.seq + 1))

// the reconstructed gap reveals macros that vanished after a star.
fn inline_star_group_needs_mapping(tokens,sources,index) any^ {
    let first = index + 2;
    let group = list_space_index(tokens,sources,first)^;
    group > first and group < len(tokens) and opening(tokens[group])
}

fn definition_group_index(tokens, index) any^ {
    if (tokens[index].kind != "source-span") -1
    else {
        let at = nonspace_index(tokens,index + 1)^;
        let next = if (at < len(tokens)) tokens[at] else null;
        if (next != null and (opening(next) or (next.kind == "char" and contains(["[", "("], next.value)))) at else -1
    }
}

// reconstruction is needed for expanded arguments and constructor-mode commands.
fn needs_output_mapping(tokens, sources, main_file, constructor_mode, index = 0, saw_paren = false,
        saw_generated = false, checked_paren = false) any^ {
    if (index >= len(tokens)) false
    else {
        let token = tokens[index];
        let paren = saw_paren or (token.kind == "char" and token.value == "(");
        let generated = saw_generated or (token.kind == "char" and (token.direct != true or token.file != main_file));
        if ((contains(["setlength", "addtolength"], token.value) and token.kind == "control" and
                (token.direct != true or token.file != main_file)) or
                token.raw_verb == true or token.raw_inline_kind != null or token.raw_group_kind != null or
                token.raw_environment != null or escaped_text_name(token) != null) true
        else if (definition_group_index(tokens,index)^ >= 0) true
        else if (control(token,"par") and token.direct != true) true
        else if (dollar(token) or control(token,"(") or control(token,"[")) {
            let math = expanded_math(tokens,index,main_file,sources)^;
            if (math.needed) true
            else needs_output_mapping(tokens,sources,main_file,constructor_mode,math.end,paren,generated,checked_paren)
        }
        else if (token.kind == "control" and ((declaration_command(token)^ and token.direct != true and token.emit_origin != null) or
                expanded_argument_sources(tokens,index + 1,token.file,sources)^)) true
        // generated inline stars require their reconstructed byte adjacency.
        else if (token.kind == "control" and inline_format_command(token.value) and index + 1 < len(tokens) and
                tokens[index + 1].kind == "char" and tokens[index + 1].value == "*" and
                (inline_star_needs_mapping(token,tokens[index + 1]) or inline_star_group_needs_mapping(tokens,sources,index)^)) true
        else if (constructor_mode and token.kind == "control" and
                (((token.direct != true or token.file != main_file) and token.read_offset != null and
                    not inline_format_command(token.value)) or native_name_needs_reparse(token.value)^ or
                    (token.value == "@" and index + 1 < len(tokens) and native_name_letter(tokens[index + 1])^))) true
        else if (paren and generated and not checked_paren and paren_command_scan(tokens,main_file)^) true
        else if (control(token,"begin") and list_option_needs_mapping(tokens,main_file,index)^) true
        else needs_output_mapping(tokens,sources,main_file,constructor_mode,index + 1,paren,generated,checked_paren or (paren and generated))
    }
}

// a raw echo retains byte positions when every token belongs to that same span.
fn echo_positions_known(tokens, span) any^ {
    let echo = tokens[span.index].echo_span;
    if (echo == null or echo.file == null or echo.start == null or echo.end == null or
            tokens[span.index].reconstruct_text != null or tokens[span.index].echo_count != span.count or
            span.end - span.start != echo.end - echo.start) false
    else all([for (i in span.index to span.index + span.count - 1)
        tokens[i].direct == true and tokens[i].file == echo.file and tokens[i].start != null and tokens[i].end != null and
        tokens[i].start >= echo.start and
        tokens[i].end >= tokens[i].start and tokens[i].end <= echo.end and
        (i == span.index or tokens[i - 1].end <= tokens[i].start)])
}

fn marked_output(token,built,constructor_mode,start=null,end=null) => {*:token,
    *:if(start==null){} else {output_start:start}, *:if(end==null){} else {output_end:end},
    output_text:built.text,output_segments:built.segments,output_constructor_mode:constructor_mode}
fn emitted_spelling(token) => if (token.kind == "control") "\\" ++ token.value
    else if (token.kind == "char") token.value else null

// Align tokens with serialized bytes, allowing a printed control-word separator.
fn synthetic_positions(tokens,span,bytes,index,at,positions=[]) any^ {
    if (index >= span.index + span.count) if (at == len(bytes)) positions else null
    else {
        let token=tokens[index]; let spelling=emitted_spelling(token);
        let part=if (spelling == null) null else binary(spelling)^;
        let previous=if (index > span.index) tokens[index - 1] else null;
        let direct=part != null and at + len(part) <= len(bytes) and slice(bytes,at,at + len(part)) == part;
        let separated=not direct and previous != null and previous.kind == "control" and at < len(bytes) and bytes[at] == 32;
        let start=at + if (separated) 1 else 0; let end=start + if (part == null) 0 else len(part);
        if (part == null or end > len(bytes) or slice(bytes,start,end) != part) null
        else synthetic_positions(tokens,span,bytes,index + 1,end,[*positions,{start:start,end:end}])
    }
}
fn mark_output_span(tokens,built,constructor_mode,span) any^ {
    let first=tokens[span.index]; let bytes=if (first.reconstruct_text == null) null else binary(first.reconstruct_text)^;
    let positions=if (bytes == null or first.reconstruct_count != span.count or len(bytes) != span.end - span.start)
        null else synthetic_positions(tokens,span,bytes,span.index,0)^;
    if (span.count == 1) [marked_output(first,built,constructor_mode,span.start,span.end)]
    else if (echo_positions_known(tokens,span)^) [for (i in span.index to span.index + span.count - 1)
        marked_output(tokens[i],built,constructor_mode,span.start + tokens[i].start - first.echo_span.start,
            span.start + tokens[i].end - first.echo_span.start)]
    else if (positions != null) [for (i in span.index to span.index + span.count - 1)
        {*:marked_output(tokens[i],built,constructor_mode,span.start + positions[i - span.index].start,
            span.start + positions[i - span.index].end),output_synthetic_verified:true}]
    else [marked_output(first,built,constructor_mode,span.start),
        *slice(tokens,span.index + 1,span.index + span.count - 1),
        marked_output(tokens[span.index + span.count - 1],built,constructor_mode,null,span.end)]
}
fn mark_output_tokens(tokens,built,constructor_mode) any^ => [for (span in built.token_spans)
    *mark_output_span(tokens,built,constructor_mode,span)^]

// parenthesized command groups are recognized only before another group or for coordinates.
fn coord_part(text) any^ {
    let chars = split(text, "")^;
    any([for (c in chars) index_of("0123456789", c) != null]) and
        not any([for (c in chars) index_of("0123456789.+- \t\r\n" ++ chr(11) ++ chr(12), c) == null])
}
fn coordinate(group, sources) any^ {
    let raw = group_raw(group, sources)^;
    let pieces = if (raw == null) [] else split(raw, ",")^;
    len(pieces) == 2 and coord_part(pieces[0])^ and coord_part(pieces[1])^
}
fn nonspace_index(tokens, index) any^ => if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
    nonspace_index(tokens, index + 1) else index
fn paren_argument(tokens, sources, index) any^ {
    let group = read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^;
    if (group.issue != null) false
    else {
        let after = nonspace_index(tokens, group.end)^;
        let next = if (after < len(tokens)) tokens[after] else null;
        let ending = tokens[group.end - 1];
        let gap = if (group.output_text != null and next != null and next.output_start != null and
                next.output_start >= group.output_end)
            utf8.decode(slice(binary(group.output_text)^,group.output_end,next.output_start))^
            else if (next != null and ending.direct == true and next.direct == true and
                ending.file == next.file and next.start >= ending.end)
            source_span({direct: true, file: ending.file, start: ending.end, end: next.start}, sources)^ else null;
        let followed = gap != null and (trim(gap) ^ { null }) == "" and
            (opening(next) or (next != null and next.kind == "char" and next.value == "["));
        followed or coordinate(group, sources)^
    }
}

// a comment can join otherwise separated arguments.
fn comment_gap(text) any^ {
    if (text == null or text == "") false
    else {
        let gap = source_gap_bytes(binary(text)^)^;
        gap.valid and gap.seen
    }
}

// Consecutive text arguments retain their separate boundaries in an array.
fn argument_run(items, start, end) any^ =>
    if (end < len(items) and items[end] is string) argument_run(items, start, end + 1)
    else end

fn pack_arguments(items, index = 0, result = []) any^ {
    if (index >= len(items)) result
    else if (items[index] is string) {
        let end = argument_run(items, index, index + 1)^;
        let item = if (end == index + 1) items[index] else slice(items, index, end);
        pack_arguments(items, end, [*result, item])
    }
    else pack_arguments(items, index + 1, [*result, items[index]])
}

// a synthetic next token owns the offset at the reconstructed segment boundary.
fn argument_end_offset(tokens, arg) any^ {
    let next = if (arg.end < len(tokens)) tokens[arg.end] else null;
    let closing_token = tokens[arg.end - 1];
    if (next != null and next.read_offset != null) next.read_offset
    else if (next != null and next.direct == true and closing_token.direct == true and
            next.file == closing_token.file and next.seq != null and closing_token.seq != null and next.seq != closing_token.seq + 1)
        next.start
    else arg.end_offset
}

fn reconstructed_literal(token) any^ {
    if (token.kind != "char" or token.output_text == null or token.output_start == null or token.output_end == null)
        false
    else utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ == token.value
}

fn reconstructed_token(token) any^ {
    if (token.output_text == null or token.output_start == null or token.output_end == null) false
    else if (token.kind == "char") {
        let raw = utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^;
        let escaped = contains(["_", "#", "%", "$", "{", "}"],token.value) and raw == "\\" ++ token.value and
            not opening(token) and not closing(token) and token.cat != 3;
        token.cat == 10 or escaped or (reconstructed_literal(token)^ and
            token.value != "\\" and token.value != "%" and
            (token.value != "{" or token.cat == 1) and (token.value != "}" or token.cat == 2) and
            (token.value != "$" or token.cat == 3))
    }
    else if (token.kind == "control") {
        let raw = utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^;
        // paragraph tokens can carry reconstructed line endings instead of a control word.
        raw == "\\" ++ token.value or (token.value == "par" and
            (contains(raw,"\r") or contains(raw,"\n")) and
            all([for (c in split(raw, "")^) contains([" ","\t","\r","\n"],c)]))
    }
    else if (token.kind == "active") utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^ == token.value
    else false
}

// native control words stop at the first byte outside letters and @.
fn native_name_letter(token) any^ => token != null and token.kind == "char" and len(token.value) == 1 and
    native_word_char(token.value)^
// Join only consecutive letters whose source or reconstructed bytes are verified.
fn native_join_end(tokens, sources, index, previous_end, file = null) any^ {
    let token=if (index < len(tokens)) tokens[index] else null;
    let direct=file != null;
    let letter=if (direct and token != null and token.kind == "active")
        len(token.value) == 1 and native_word_char(token.value)^ else native_name_letter(token)^;
    let valid=token != null and letter and
        (if (direct) token.direct == true and token.file == file and token.start == previous_end and
            source_span(token,sources)^ == token.value
         else token.output_start == previous_end and reconstructed_literal(token)^);
    if (not valid) index else native_join_end(tokens,sources,index + 1,
        if (direct) token.end else token.output_end,file)
}
fn native_word_end(chars, index = 0) any^ {
    if (index >= len(chars)) index
    else if (native_word_char(chars[index])^)
        native_word_end(chars, index + 1)
    else index
}
fn native_name_needs_reparse(value) any^ {
    let chars = split(value, "")^;
    any([for (b in binary(value)^) b >= 128]) or
        (len(chars) > 1 and (len(chars) >= 95 or native_word_end(chars)^ != len(chars)))
}
fn native_split_word(token) any^ {
    if (token.kind != "control" or token.output_constructor_mode != true or
            token.output_start == null or token.output_end == null or not reconstructed_token(token)^) null
    else {
        let chars = split(token.value, "")^;
        let end = native_word_end(chars)^;
        let suffix = slice(chars, end, len(chars));
        if (end == 0 or end >= len(chars) or end >= 95 or
                not all([for (c in suffix) native_word_suffix_char(c)^])) null
        else {
            let start = token.output_start;
            let word_end = start + 1 + end;
            let origin = if (token.read_offset != null) token.read_offset
                else rebuild.map_offset(token.output_segments,start)^;
            [{*:token, value:join(slice(chars,0,end), ""), direct:false,
                read_offset:origin, read_file:if(token.read_file != null) token.read_file else token.file, output_end:word_end},
             *[for (i,c in suffix) {*:token, kind:"char", value:c, cat:12, direct:false,
                output_start:word_end + i, output_end:word_end + i + 1}]]
        }
    }
}

// the native scanner reads one escaped punctuation byte, then ordinary text.
fn native_escaped_word(token) any^ {
    if (token.kind != "control" or token.output_constructor_mode != true) false
    else {
        let chars = split(token.value, "")^;
        len(chars) > 1 and contains(["$", "%", "#", "&", "_", "{", "}"], chars[0]) and
            all([for (c in slice(chars, 1, len(chars)))
                native_suffix_char(c)^]) and
            reconstructed_token(token)^
    }
}

fn output_between(left, right) any^ {
    if (left.output_text == null or left.output_end == null or right == null or right.output_start == null or
            right.output_start < left.output_end) null
    else utf8.decode(slice(binary(left.output_text)^,left.output_end,right.output_start))
}

fn output_gap_text(gap) any^ {
    if (gap == null) null
    else {
        let parsed = source_gap_bytes(binary(gap)^)^;
        if (parsed.valid) parsed.text else null
    }
}

// A constructor retains a directly read character-definition control spelling.
fn chardef_source(token, sources) any^ {
    let origin = token.chardef_origin;
    let raw = if (origin != null and origin.direct == true and origin.file == token.read_file and
            origin.start == token.read_offset) source_span(origin,sources)^ else null;
    if (raw == null or raw != "\\" ++ origin.value) null else raw
}

// Restore verified control spellings in raw argument descriptors.
fn chardef_argument_raw(tokens, sources, bytes, at, end, index = 0, result = "") any^ {
    if (index >= len(tokens)) result ++ utf8.decode(slice(bytes,at,end))^
    else {
        let token = tokens[index];
        if (token.chardef_emission != true) chardef_argument_raw(tokens,sources,bytes,at,end,index + 1,result)
        else {
            let raw = chardef_source(token,sources)^;
            let start = token.output_start; let stop = token.output_end;
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let next_source = if (next != null and next.chardef_emission == true) next.chardef_origin else next;
            let origin = token.chardef_origin;
            // A paragraph token can include a preceding line ending; verify its complete source gap.
            let paragraph = next != null and control(next,"par");
            let after = if (paragraph and index + 2 < len(tokens)) tokens[index + 2] else null;
            let after_source = if (after != null and after.chardef_emission == true) after.chardef_origin else after;
            let paragraph_raw = if (raw != null and after_source != null and after_source.direct == true and
                    after_source.file == origin.file and after_source.start >= origin.end)
                source_span({direct:true,file:origin.file,start:origin.end,end:after_source.start},sources)^ else null;
            let paragraph_safe = not paragraph or (paragraph_raw != null and after.output_start != null and
                stop != null and after.output_start >= stop and after.output_start <= end and
                utf8.decode(slice(bytes,stop,after.output_start))^ == paragraph_raw);
            let has_gap = raw != null and next_source != null and next_source.direct == true and
                not paragraph and next_source.file == origin.file and
                next_source.start > origin.end and next.output_start == stop;
            let gap = if (has_gap) source_span({direct:true,file:origin.file,start:origin.end,end:next_source.start},sources)^ else "";
            let checked = if (gap != null) source_gap_bytes(binary(gap)^)^ else null;
            if (raw == null or start == null or stop == null or start < at or stop > end or stop < start or
                    gap == null or checked == null or not checked.valid or not paragraph_safe or
                    utf8.decode(slice(bytes,start,stop))^ != token.value) null
            else chardef_argument_raw(tokens,sources,bytes,stop,end,index + 1,
                result ++ utf8.decode(slice(bytes,at,start))^ ++ raw ++ gap)
        }
    }
}

fn generic_arguments(tokens, sources, index, previous_end, source_file, groups = [], items = [], allow_paren = true, stop_on_gap = false, literal_end = false, previous_output_end = null, map_source = false, required_mode = "") any^ {
    if (index >= len(tokens)) {issue: null, index: index, groups: groups, items: items}
    else {
        let token = tokens[index];
        let at = if (map_source and token.kind == "char" and token.cat == 10) nonspace_index(tokens,index)^ else index;
        let after = if (at < len(tokens)) tokens[at] else null;
        let comment_join = at > index and after != null and previous_output_end != null and
            after.output_start != null and after.output_text != null and after.output_start >= previous_output_end and
            (opening(after) or (after.kind == "char" and (after.value == "[" or
                (allow_paren and after.value == "(" and paren_argument(tokens,sources,at)^)))) and
            comment_gap(utf8.decode(slice(binary(after.output_text)^,previous_output_end,after.output_start))^)^;
        let required = opening(token);
        let optional = token.kind == "char" and token.value == "[";
        let paren_candidate = allow_paren and token.kind == "char" and token.value == "(";
        let probe = if (paren_candidate) read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^ else null;
        let uncertain = paren_candidate and (token.direct != true or token.file != source_file or expanded_source_group(probe));
        // reconstructed spans qualify expanded coordinate text and its byte boundaries.
        let mapped = uncertain and probe.issue == null and probe.output_text != null and
            previous_output_end != null and token.output_start == previous_output_end and
            reconstructed_literal(token)^ and reconstructed_literal(tokens[probe.end - 1])^ and
            all([for (t in probe.tokens) t.kind == "char" and (t.cat == 10 or reconstructed_literal(t)^)]);
        // An unmatched parenthesis in the complete output is ordinary native text.
        let unmatched = uncertain and probe.issue != null and reconstructed_literal(token)^ and
            token.output_text != null and token.output_start != null and
            not tex_reader.raw_group_end(token.output_text,
                len(utf8.decode(slice(binary(token.output_text)^,0,token.output_start))^) + 1, "(", ")").closed;
        let paren = paren_candidate and (not uncertain or mapped) and paren_argument(tokens, sources, index)^;
        if (comment_join) generic_arguments(tokens,sources,at,previous_end,source_file,groups,items,allow_paren,
            stop_on_gap,literal_end,previous_output_end,map_source,required_mode)
        else if (uncertain and not mapped and not unmatched) unsupported(index, "parenthesized command source mapping")
        else if (not required and not optional and not paren)
            {issue: null, index: index, groups: groups, items: items}
        else if (map_source and (previous_output_end == null or token.output_start == null or token.output_start < previous_output_end))
            unsupported(index, "command argument source gap")
        else if (map_source and token.output_start > previous_output_end and not comment_gap(
                utf8.decode(slice(binary(token.output_text)^,previous_output_end,token.output_start))^)^)
            if (stop_on_gap) {issue: null, index: index, groups: groups, items: items}
            else unsupported(index, "command argument source gap")
        else if (not map_source and not mapped and (token.direct != true or token.file != source_file or token.start < previous_end))
            unsupported(index, "command argument source gap")
        else if (not map_source and not mapped and token.start > previous_end and not comment_gap(source_span({direct: true, file: source_file,
                start: previous_end, end: token.start}, sources)^)^)
            if (stop_on_gap) {issue: null, index: index, groups: groups, items: items}
            else unsupported(index, "command argument source gap")
        else {
            let arg = if (required) read_group(tokens, index)^ else if (optional) read_generic_delimited(tokens, index, "[", "]", "latex-digest-unclosed-optional")^ else read_generic_delimited(tokens, index, "(", ")", "latex-digest-unclosed-paren")^;
            if (arg.issue != null) arg
            else {
                let final_token = tokens[arg.end - 1];
                let output_raw = if (map_source and token.output_start != null and final_token.output_end != null and
                        token.output_text != null and final_token.output_end > token.output_start)
                    utf8.decode(slice(binary(token.output_text)^,token.output_start + 1,final_token.output_end - 1))^ else null;
                let has_chardef = any([for (t in (arg.tokens)^) t.chardef_emission == true]);
                let raw = if (has_chardef and token.output_text != null and token.output_start != null and final_token.output_end != null)
                    chardef_argument_raw([*(arg.tokens)^,final_token],sources,binary(token.output_text)^,token.output_start + 1,final_token.output_end - 1)^
                    else if (has_chardef) null else if (map_source) output_raw else group_raw(arg, sources)^;
                let parsed = if (required and required_mode == "raw")
                    {issue:null, index:len(arg.tokens), tokens:arg.tokens, items:[]} else inline_group({*:arg, *:if(map_source) {output_open:token,output_close:final_token} else {}}, sources)^;
                if (has_chardef and raw == null) unsupported(index, "character definition argument source mapping")
                else if (raw == null) unsupported(index, "command argument source")
                else if (map_source and (not reconstructed_literal(token)^ or not reconstructed_literal(final_token)^ or
                        not all([for (t in (arg.tokens)^) reconstructed_token(t)^])))
                    unsupported(index, "command argument source mapping")
                else if (parsed.issue != null) parsed
                else if (parsed.index != len(parsed.tokens)) unsupported(index, "command argument")
                else {
                    let mapped_start = if (map_source) rebuild.map_offset(token.output_segments,token.output_start)^
                        else if (mapped) rebuild.map_offset(arg.output_segments,arg.output_start)^ else arg.start_offset;
                    let mapped_end = if (map_source) rebuild.map_offset(token.output_segments,final_token.output_end)^
                        else if (mapped) rebuild.map_offset(arg.output_segments,arg.output_end)^
                        else if (literal_end) arg.end_offset else argument_end_offset(tokens, arg)^;
                    let descriptor = {kind: if (required) "required" else if (optional) "optional" else "parenthesized", raw: raw,
                        start: mapped_start, end: mapped_end};
                    let children = if (optional) [<brack_group *parsed.items>]
                        else if (paren) [<paren_group *parsed.items>]
                        else if (required_mode == "raw") [<curly_group raw>]
                        else if (required_mode == "group") [<curly_group *parsed.items>]
                        else if (len(parsed.items) == 0) [<curly_group>] else parsed.items;
                    generic_arguments(tokens, sources, arg.end, if(mapped) mapped_end else arg.end_offset, source_file,
                        [*groups, descriptor], [*items, *children], allow_paren, stop_on_gap, literal_end,
                        tokens[arg.end - 1].output_end, map_source, required_mode)
                }
            }
        }
    }
}

fn accent_next(tokens, index) any^ =>
    if (index < len(tokens) and tokens[index].kind == "char" and tokens[index].cat == 10)
        accent_next(tokens, index + 1) else index

fn accent_gap_safe(tokens, sources, index) any^ {
    let token = tokens[index];
    let next_index = accent_next(tokens, index + 1)^;
    if (token.direct != true) true
    else if (next_index >= len(tokens)) false
    else {
        let next = tokens[next_index];
        if (next.direct != true or next.file != token.file or next.start < token.end) false
        else {
            let gap = source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)^;
            gap != null and (gap == "" or (trim(gap) ^ { null }) == "")
        }
    }
}

fn accent_name(name) => contains(["'", "`", "^", "\"", "~", "=", ".", "u", "v", "H", "c", "d", "b", "t", "k"], name)
fn accent_node(tokens, sources, index) any^ {
    let token = tokens[index];
    let offset = if (token.emit_origin != null) token.emit_origin
        else if (token.read_offset != null) token.read_offset
        else if (token.direct == true) token.start else null;
    let arg = accent_argument(tokens, sources, index + 1)^;
    if (offset == null) unsupported(index, "accent source mapping")
    else if (not accent_gap_safe(tokens, sources, index)^) unsupported(index, "accent source gap")
    else if (arg.issue != null) arg
    else {index: arg.index, node: <accent command: token.value, source_offset: offset, *arg.items>, issue: null}
}

fn accent_argument(tokens, sources, index) any^ {
    if (index >= len(tokens)) unsupported(index, "accent argument")
    else if (tokens[index].kind == "char" and tokens[index].cat == 10)
        accent_argument(tokens, sources, index + 1)
    else if (opening(tokens[index])) {
        let group = read_group(tokens, index)^;
        if (group.issue != null) group
        else if (any([for (t in group.tokens) t.emit_origin != null and t.kind == "char" and t.cat == 12 and
            contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)]))
            unsupported(index, "accent emitted syntax")
        else {
            let children = inline_group(group, sources)^;
            if (children.issue != null) children
            else if (children.index != len(children.tokens)) unsupported(index, "accent group")
            else {index: group.end, items: [<curly_group *children.items>], issue: null}
        }
    }
    else if (tokens[index].kind == "char" and not closing(tokens[index]))
        {index: index + 1, items: [tokens[index].value], issue: null}
    else if (tokens[index].kind == "control" and accent_name(tokens[index].value)) {
        let nested = accent_node(tokens, sources, index)^;
        if (nested.issue != null) nested else {index: nested.index, items: [nested.node], issue: null}
    }
    else unsupported(index, "accent argument")
}

fn rule_span_needs_reparse(token, sources) any^ {
    let span = token.echo_span;
    let raw = source_span({direct: true, file: span.file, start: span.start, end: span.end}, sources)^;
    raw == null or index_of(raw, "%") != null
}

// generated rules have two fully synthetic arguments sharing one source origin.
fn generated_rule_arg(group, origin) any^ {
    let tokens = group.tokens;
    let same_origin = all([for (token in tokens) token.emit_origin == origin]);
    if (not same_origin) {issue: "latex-digest-unsupported", unsupported: "rule argument source"}
    else if (len(tokens) == 1 and control(tokens[0], "linewidth"))
        {issue: null, raw: "\\linewidth", items: [<linewidth source_offset: origin, argument_groups: []>]}
    else if (all([for (token in tokens) token.kind == "char" and token.cat == 12])) {
        let text = join([for (token in tokens) token.value], "");
        {issue: null, raw: text, items: if (text == "") [<curly_group>] else [text]}
    }
    else {issue: "latex-digest-unsupported", unsupported: "rule argument"}
}
fn generated_rule(tokens, index) any^ {
    let origin = tokens[index].emit_origin;
    let first = read_group(tokens, index + 1)^;
    if (first.issue != null) first
    else {
        let second = read_group(tokens, first.end)^;
        if (second.issue != null) second
        else {
            let width = generated_rule_arg(first, origin)^;
            let height = generated_rule_arg(second, origin)^;
            if (width.issue != null) width
            else if (height.issue != null) height
            else {
                let groups = [{kind: "required", raw: width.raw, start: origin, end: origin},
                    {kind: "required", raw: height.raw, start: origin, end: origin}];
                let content = pack_arguments([*width.items, *height.items])^;
                {issue: null, index: second.end, node: <rule source_offset: origin, argument_groups: groups, *content>}
            }
        }
    }
}

fn box_span_needs_reparse(tokens, sources, index, group) any^ {
    let opening = tokens[index];
    let closing = tokens[group.end - 1];
    let raw = if (opening.emit_file == closing.emit_file and closing.box_close == true)
        source_span({direct: true, file: opening.emit_file, start: opening.emit_origin, end: closing.emit_origin}, sources)^
        else null;
    raw == null or index_of(raw, "%") != null or any([for (t in group.tokens)
        (t.emit_origin != null or t.direct == true) and t.kind == "char" and t.cat == 12 and
        contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)])
}
fn generated_mbox(tokens, sources, index) any^ {
    let origin = tokens[index].emit_origin;
    let group = read_group(tokens, index + 1)^;
    if (group.issue != null) group
    else {
        let opening = tokens[index + 1];
        let closing = tokens[group.end - 1];
        let next = if (group.end < len(tokens)) tokens[group.end] else null;
        let safe = opening.box_open == true and closing.box_close == true and
            opening.emit_origin == origin and opening.emit_file == closing.emit_file and
            next != null and next.direct == true and next.file == opening.emit_file and
            next.start == closing.emit_origin + 1 and
            all([for (t in group.tokens) t.kind == "char" and t.direct == true and t.file == opening.emit_file and
                contains([10, 11, 12], t.cat) and
                not contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"], t.value)]);
        let raw = if (safe) source_span({direct: true, file: opening.emit_file,
            start: origin + 1, end: closing.emit_origin}, sources)^ else null;
        let text = join([for (t in group.tokens) t.value], "");
        if (not safe or raw == null or raw != text) unsupported(index, "box source mapping")
        else {
            let groups = [{kind: "required", raw: raw, start: origin, end: next.start}];
            let children = if (text == "") [<curly_group>] else [text];
            {issue: null, index: group.end, node: <mbox source_offset: origin, argument_groups: groups, *children>}
        }
    }
}

// nested echoed definitions retain their original reader category codes.
fn inline_definition_end(tokens, index, offset) any^ =>
    if (index >= len(tokens) or tokens[index].start >= offset) index
    else inline_definition_end(tokens, index + 1, offset)

// definition parsing uses source-local offsets; translate every constructed node.
fn definition_mapped_offset(offset, token) any^ =>
    rebuild.map_offset(token.output_segments,token.output_start + offset - token.start)

fn map_definition_value(value, token) any^ {
    if (value is element) {
        let attrs = map([for (key, item in map(value))
            (key, if (string(key) == "source_offset") definition_mapped_offset(item,token)^
                else if (string(key) == "argument_groups") [for (group in item)
                    {*:group, start:definition_mapped_offset(group.start,token)^,
                        end:definition_mapped_offset(group.end,token)^}]
                else map_definition_value(item,token)^)]);
        <[name(value)] *:attrs, *[for (child in content(value)^) map_definition_value(child,token)^]>
    }
    else if (value is array) [for (item in value) map_definition_value(item,token)^]
    else if (value is map) map([for (key,item in value) (key,map_definition_value(item,token)^)])
    else value
}

fn same_definition_tokens(left, right, native = false) any^ =>
    len(left) == len(right) and all([for (i in 0 to len(left) - 1)
        left[i].kind == right[i].kind and left[i].value == right[i].value and
        left[i].start == right[i].start and left[i].end == right[i].end and
        (not native or left[i].kind != "control" or len(binary(left[i].value)^) == len(left[i].value)) and
        (left[i].cat == right[i].cat or (native and contains([11,12], left[i].cat) and contains([11,12], right[i].cat)))])

// native LaTeX command scanning uses fixed category codes for source names.
fn native_definition_reader(raw, file) => {*:tex_reader.reader(raw, file), catcodes:
    {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '\t': 10, '~': 13, '@': 11}}

// Native definitions retain all adjacent argument groups on the same node.
fn merge_definition_nodes(nodes, args) any^ {
    if (len(args.groups) == 0) nodes
    else {
        let node=nodes[len(nodes) - 1];
        let attrs={*:map(node),argument_groups:[*node.argument_groups,*args.groups]};
        let children=[*content(node)^,*args.items];
        let updated=<[name(node)] *:attrs,*children>;
        [*slice(nodes,0,len(nodes) - 1),updated]
    }
}

fn inline_definition(tokens, sources, index) any^ {
    let token = tokens[index];
    let rest = slice(tokens, index, len(tokens));
    let safe = token.definition_catcodes != null and
        all([for (item in rest) item.direct == true and item.file == token.file]);
    let last_token = if (len(rest) > 0) rest[len(rest) - 1] else null;
    let raw = if (safe and last_token != null) source_span({direct: true, file: token.file,
        start: token.start, end: last_token.end}, sources)^ else null;
    if (raw == null) unsupported(index, "inline definition source mapping")
    else {
        let foreign = token.read_offset != null;
        let emitted_raw = if (foreign and token.output_start != null and last_token.output_end != null and
                token.output_text != null and token.output_segments != null)
            utf8.decode(slice(binary(token.output_text)^,token.output_start,last_token.output_end))^ else null;
        let span = {value: raw, start: token.start, file: token.file, catcodes: token.definition_catcodes};
        let reader = {*:tex_reader.reader(raw, token.file), catcodes: token.definition_catcodes};
        let reread = span_tokens(reader, token.start, len(binary(raw)^))^;
        // The enclosing definition already uses native document-name boundaries.
        let normalized = definition_native_tokens(reread,sources)^;
        let normalized_rest = definition_native_tokens(rest,sources)^;
        let same = normalized.issue == null and normalized_rest.issue == null and
            same_definition_tokens(normalized_rest.tokens,normalized.tokens)^;
        // native LaTeX scans source command names independently of TeX catcodes.
        let native_reader = native_definition_reader(raw, token.file);
        let native_tokens = span_tokens(native_reader, token.start, len(binary(raw)^))^;
        let native_same = normalized_rest.issue == null and same_definition_tokens(normalized_rest.tokens,native_tokens,true)^;
        let parsed = if (same and native_same and (not foreign or emitted_raw == raw)) definition_span(span, sources, true)^ else
            unsupported(index, "inline definition token mapping");
        if (parsed.issue != null) parsed
        else if (parsed.end_offset == null) unsupported(index, "inline definition source mapping")
        else {
            let at=inline_definition_end(tokens,index,parsed.end_offset)^;
            let next=if(at < len(tokens)) tokens[at] else null;
            let group=next != null and (opening(next) or (next.kind == "char" and contains(["[", "("],next.value)));
            let gap=if(group and next.direct == true and next.file == token.file and next.start >= parsed.end_offset)
                source_span({direct:true,file:token.file,start:parsed.end_offset,end:next.start},sources)^ else null;
            let joinable=group and gap != null and (gap == "" or comment_gap(gap)^) and parsed.delimited != true;
            let node=if(len(parsed.nodes) > 0) parsed.nodes[len(parsed.nodes)-1] else null;
            let args=if(joinable and node is element and tex_reader.definition_command(string(name(node))))
                generic_arguments(tokens,sources,at,parsed.end_offset,token.file,[],[],true,true,false,null,false,
                    if(contains(["newenvironment","renewenvironment"],string(name(node)))) "raw" else "group")^
                else {issue:null,index:at,groups:[],items:[]};
            if(args.issue != null) args else {
                let merged=merge_definition_nodes(parsed.nodes,args)^;
                {*:parsed,index:args.index,nodes:if(foreign) [for(n in merged) map_definition_value(n,token)^] else merged}
            }
        }
    }
}

// A definition echo can end inside a native balanced argument; only replay
// adjacent direct source whose tokens remain identical under both scanners.
fn definition_contiguous_end(tokens, index, file, offset, seq) any^ {
    if (index >= len(tokens)) index
    else {
        let token = tokens[index];
        if (token.direct != true or token.file != file or token.start != offset or token.seq != seq + 1 or
                token.echo_span != null or token.reconstruct_text != null) index
        else definition_contiguous_end(tokens,index + 1,file,token.end,token.seq)
    }
}

fn definition_continuation(tokens, sources, index) any^ {
    let span = tokens[index];
    let end = definition_contiguous_end(tokens,index + 1,span.file,span.end,span.last_seq)^;
    if (end == index + 1 or span.read_offset != null) unsupported(index, "definition continuation source mapping")
    else {
        let final_token = tokens[end - 1];
        let raw = source_span({direct:true,file:span.file,start:span.start,end:final_token.end},sources)^;
        let tail = source_span({direct:true,file:span.file,start:span.end,end:final_token.end},sources)^;
        if (raw == null or tail == null) unsupported(index, "definition continuation source mapping")
        else {
            let reader = {*:tex_reader.reader(tail,span.file),catcodes:span.catcodes};
            let reread = span_tokens(reader,span.end,len(binary(tail)^))^;
            let native = native_definition_reader(tail,span.file);
            let native_tokens = span_tokens(native,span.end,len(binary(tail)^))^;
            let original = slice(tokens,index + 1,end);
            if (not same_definition_tokens(original,reread)^ or not same_definition_tokens(original,native_tokens,true)^)
                unsupported(index, "definition continuation token mapping")
            else {
                let parsed = definition_span({value:raw,start:span.start,file:span.file,catcodes:span.catcodes},sources,true)^;
                if (parsed.issue != null) parsed
                else if (parsed.end_offset == null or parsed.end_offset <= span.end)
                    unsupported(index, "definition continuation source mapping")
                else {
                    let at = inline_definition_end(tokens,index + 1,parsed.end_offset)^;
                    // never consume a token whose source extends past the parsed definition.
                    if (at <= index + 1 or tokens[at - 1].end != parsed.end_offset)
                        unsupported(index, "definition continuation token boundary")
                    else {
                        let node = if (len(parsed.nodes) == 0) null else parsed.nodes[len(parsed.nodes) - 1];
                        let args = if (parsed.delimited != true and node is element and
                                tex_reader.definition_command(string(name(node))))
                            generic_arguments(tokens,sources,at,parsed.end_offset,span.file,[],[],true,true,false,null,false,
                                if (contains(["newenvironment","renewenvironment"],string(name(node)))) "raw" else "group")^
                            else {issue:null,index:at,groups:[],items:[]};
                        // Native continues collecting adjacent groups after the echoed definition.
                        if (args.issue != null) args else {*:parsed,index:args.index,nodes:merge_definition_nodes(parsed.nodes,args)^}
                    }
                }
            }
        }
    }
}

// raw environment headers use the same source delimiter scan as inline capture.
fn raw_header_space(source, index) any^ {
    var at = index;
    while (at < len(source) and contains([" ","\t","\n","\r",chr(11),chr(12)],
            tex_reader.text_span(source,at,at + 1)^)) { at = at + 1 }
    at
}

// a closing marker in a TikZ comment is not an environment delimiter to the native parser.
fn raw_comment_open(source, index, limit_value) any^ {
    var at = index, comment = false;
    while (at < limit_value) {
        let c = tex_reader.text_span(source,at,at + 1)^;
        if (c == "\n" or c == "\r") { comment = false; at = at + 1 }
        else if (comment) { at = at + 1 }
        else if (c == "\\") { at = at + 2 }
        else { comment = c == "%"; at = at + 1 }
    }
    comment
}

fn raw_environment_node(token, index) any^ {
    let name = token.raw_environment;
    let raw = if (token.output_text == null or token.output_start == null or token.output_end == null or
        token.output_segments == null) null else utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))^;
    let header = "\\begin{" ++ name ++ "}";
    let close = "\\end{" ++ name ++ "}";
    let supported = raw != null and token.value == "begin" and starts_with(raw,header) and ends_with(raw,close);
    if (not supported) unsupported(index,"raw environment source mapping")
    else {
        let close_at = len(raw) - len(close);
        let escaped = rebuild.slash_count(raw,close_at)^ % 2 != 0;
        let unsafe_tikz = name == "tikzpicture" and (contains(slice(raw,len(header),close_at),"\\begin{tikzpicture}") or
            raw_comment_open(raw,len(header),close_at)^);
        let at = len(header);
        let spaced = if (name == "tikzpicture" or name == "lstlisting" or name == "filecontents" or name == "filecontents*")
            raw_header_space(raw,at)^ else at;
        let opener = if (name == "filecontents" or name == "filecontents*") "{" else "[";
        let closer = if (opener == "{") "}" else "]";
        let header_group = name == "tikzpicture" or name == "lstlisting" or
            name == "filecontents" or name == "filecontents*";
        let has_group = header_group and tex_reader.text_span(raw,spaced,spaced + 1)^ == opener;
        let group = if (has_group) tex_reader.raw_group_end(raw,spaced + 1,opener,closer)^ else null;
        let required = name == "filecontents" or name == "filecontents*";
        let body_at = if (has_group and group.closed) group.end else if (name == "tikzpicture") spaced else at;
        if (escaped or unsafe_tikz or (required and not has_group) or (has_group and not group.closed) or body_at > close_at)
            unsupported(index,"raw environment source mapping")
        else {
            let body = slice(raw,body_at,close_at)^;
            let option = if (has_group) slice(raw,spaced + 1,group.end - 1)^ else null;
            let body_offset = rebuild.map_offset(token.output_segments,token.output_start + len(binary(slice(raw,0,body_at)^)^))^;
            let offset = rebuild.map_offset(token.output_segments,token.output_start)^;
            let attrs = {source_offset:offset, source:body, body_offset:body_offset,
                *:if (name == "tikzpicture") {raw_source:raw} else {},
                *:if (has_group and name == "tikzpicture") {options:option} else {},
                *:if (has_group and name == "lstlisting") {options_raw:option} else {},
                *:if (has_group and required) {filename:option} else {}};
            let children = if (name == "tikzpicture" or body == "") [] else [body];
            {issue:null,index:index + 1,node:<[name] *:attrs, *children>}
        }
    }
}

// Both direct and generated emissions use the same argument boundaries.
fn setlength_argument_end(tokens, index) any^ {
    let target_at = nonspace_index(tokens,index + 1)^;
    let target = if (target_at >= len(tokens)) null else tokens[target_at];
    let group = if (opening(target)) read_group(tokens,target_at)^ else null;
    let after = if (group != null and group.issue == null) group.end
        else if (target != null and target.kind == "control") target_at + 1 else target_at;
    let value_at = nonspace_index(tokens,after)^;
    let value = if (value_at < len(tokens) and opening(tokens[value_at])) read_group(tokens,value_at)^ else null;
    if (value != null and value.issue == null) value.end else index
}

// Source tokens in a constructor can be emitted separately instead of as an echo.
fn setlength_direct_span(tokens, sources, index) any^ {
    let first = tokens[index];
    let end = setlength_argument_end(tokens,index)^;
    let valid = end > index and all([for (i in (index to end - 1)^)
        tokens[i].direct == true and tokens[i].file == first.file and tokens[i].start != null and
        tokens[i].end != null and tokens[i].echo_span == null and tokens[i].reconstruct_text == null and
        (i == index or (tokens[i].seq != null and tokens[i - 1].seq != null and
            tokens[i].seq == tokens[i - 1].seq + 1 and tokens[i].start >= tokens[i - 1].end)) and
        rebuild.source_fragment(tokens[i],sources)^.body == source_span(tokens[i],sources)^]);
    if (not valid) null else {end:end,raw:source_span({direct:true,file:first.file,start:first.start,end:tokens[end - 1].end},sources)^}
}

// Generated commands need the serialized bytes and native emission-origin mapping.
fn setlength_mapped_span(tokens, index) any^ {
    let first = tokens[index];
    let count = first.reconstruct_count;
    let coalesced = first.reconstruct_text != null and count != null and count > 1 and index + count <= len(tokens);
    let end = if (coalesced) index + count else setlength_argument_end(tokens,index)^;
    let mapped = end > index and first.output_segments != null and first.output_start != null and
        tokens[end - 1].output_end != null;
    let valid = mapped and (coalesced or all([for (i in (index to end - 1)^)
        tokens[i].output_start != null and tokens[i].output_end != null and reconstructed_token(tokens[i])^ and
        (i == index or tokens[i].output_start >= tokens[i - 1].output_end)]));
    if (not valid) null else {
        let raw = utf8.decode(slice(binary(first.output_text)^,first.output_start,tokens[end - 1].output_end)) ^ {null};
        if (coalesced and raw != first.reconstruct_text) null else {end:end,raw:raw}
    }
}

// The native setlength node reads the emitted source, including raw group contents.
fn setlength_node(tokens, sources, index) any^ {
    let token = tokens[index];
    let span = token.echo_span;
    let count = token.echo_count;
    let echo_end = if (count == null) index else index + count;
    let valid = span != null and span.valid == true and token.direct == true and span.file == token.file and
        span.start == token.start and count != null and count > 1 and echo_end <= len(tokens) and
        tokens[echo_end - 1].end == span.end and all([for (i in (index to echo_end - 1)^)
            tokens[i].direct == true and tokens[i].file == span.file and tokens[i].start != null and
            tokens[i].end != null and tokens[i].start >= span.start and tokens[i].end <= span.end and
            (i == index or tokens[i - 1].end <= tokens[i].start)]);
    let direct = if (valid) {end:echo_end,raw:source_span({direct:true,file:span.file,start:span.start,end:span.end},sources)^}
        else setlength_direct_span(tokens,sources,index)^;
    let captured = if (direct != null) direct else setlength_mapped_span(tokens,index)^;
    let end = if (captured == null) index else captured.end;
    let raw = if (captured == null) null else captured.raw;
    if (raw == null or not starts_with(raw,"\\setlength")) unsupported(index,"setlength source mapping")
    else {
        let target_at = raw_header_space(raw,len("\\setlength"))^;
        let target_char = tex_reader.text_span(raw,target_at,target_at + 1)^;
        let group = if (target_char == "{") tex_reader.raw_group_end(raw,target_at + 1,"{","}")^ else null;
        let tail = if (target_char == "\\") split(slice(raw,target_at + 1,len(raw))^,"")^ else [];
        let word = native_word_end(tail)^;
        let name_end = if (target_char == "\\" and len(tail) > 0) target_at + 1 + if (word > 0) word else 1 else target_at;
        let target_end = if (group != null and group.closed) group.end else name_end;
        let target_raw = if (group != null and group.closed) slice(raw,target_at + 1,group.end - 1)^
            else if (name_end > target_at) slice(raw,target_at + 1,name_end)^ else null;
        let name = if (group != null and target_raw != null and starts_with(target_raw,"\\")) slice(target_raw,1,len(target_raw))^ else target_raw;
        let value_at = raw_header_space(raw,target_end)^;
        let value_group = if (tex_reader.text_span(raw,value_at,value_at + 1)^ == "{")
            tex_reader.raw_group_end(raw,value_at + 1,"{","}")^ else null;
        // Echoes with different native and TeX token boundaries need a separate mapping.
        let ascii_name = target_char != "\\" or (target_raw != null and
            all([for (b in binary(target_raw)^) b < 128]) and len(target_raw) < 96);
        if (name == null or not ascii_name or value_group == null or not value_group.closed or value_group.end != len(raw))
            unsupported(index,"setlength source mapping")
        else {
            let value = slice(raw,value_at + 1,value_group.end - 1)^;
            let offset = if (token.output_start == null) token.start else rebuild.map_offset(token.output_segments,token.output_start)^;
            {issue:null,index:end,node:<setlength source_offset:offset, length_name:name, length_value:value>}
        }
    }
}

// Group parsing excludes delimiters; restore source whitespace skipped at its edges.
fn inline_group(group, sources) any^ {
    // Adjacent source controls must be visible together when restoring their gaps.
    let prepared = [for (token in group.tokens)
        if (token.chardef_emission == true and chardef_source(token,sources)^ != null)
            {*:token, *:token.chardef_origin, chardef_emission:false, read_offset:null, read_file:null} else token];
    let group_safe = inline_group_verbatim_safe(prepared)^;
    let tokens = if (group_safe) [for (token in prepared)
        if (token.raw_verb == true) {*:token, group_scan_safe:true} else token] else prepared;
    let first = if (len(tokens) == 0) null else tokens[0];
    let final_token = if (len(tokens) == 0) null else tokens[len(tokens) - 1];
    let original_final = if (len(group.tokens) == 0) null else group.tokens[len(group.tokens) - 1];
    let safe = group.direct == true and group.file != null and group.start_offset != null and group.end_offset != null and
        (first == null or ((first.direct == true or first.kind == "source-span") and first.file == group.file and first.start != null)) and
        (final_token == null or ((final_token.direct == true or final_token.kind == "source-span") and
            final_token.file == group.file and final_token.end != null));
    let begin = if (safe) group.start_offset + 1 else 0;
    let end = if (safe) group.end_offset - 1 else 0;
    let first_at = if (not safe or first == null) end else first.start;
    let last_at = if (not safe or final_token == null) end else final_token.end;
    let bounded = safe and begin <= first_at and first_at <= last_at and last_at <= end;
    let mapped = group.output_open != null and group.output_close != null;
    let leading = if (mapped) output_between(group.output_open,if(first == null) group.output_close else first)^
        else if (bounded) source_span({direct:true,file:group.file,start:begin,end:first_at},sources)^ else null;
    let output_trailing = if (mapped and final_token != null) output_between(final_token,group.output_close)^
        else if (mapped) "" else if (bounded) source_span({direct:true,file:group.file,start:last_at,end:end},sources)^ else null;
    let close = group.output_close;
    let source_tail = if (mapped and output_trailing == "" and original_final != null and
            original_final.chardef_emission == true and chardef_source(original_final,sources)^ != null and
            close != null and close.direct == true and final_token.file == close.file and final_token.end <= close.start)
        source_span({direct:true,file:close.file,start:final_token.end,end:close.start},sources)^ else null;
    let trailing = if (source_tail == null) output_trailing else source_tail;
    let leading_check = if (leading == null) null else source_gap_bytes(binary(leading)^)^;
    let trailing_check = if (trailing == null) null else source_gap_bytes(binary(trailing)^)^;
    let prefix = if (leading_check != null and leading_check.valid) paragraph_events(leading)^ else [];
    let suffix = if (not mapped and trailing_check != null and trailing_check.valid) paragraph_events(trailing)^ else [];
    let result=if (mapped and (leading_check == null or trailing_check == null or
            not leading_check.valid or not trailing_check.valid))
        unsupported(0,"command group edge source mapping")
        else inline([*prefix,*tokens,*suffix],sources,0,"",[],false)^;
    if (mapped and trailing_check != null and trailing_check.valid and result.issue == null) {
        let count=len(result.items); let tail=if(count > 0) result.items[count - 1] else null;
        let items=if(tail is string and trailing_check.text != "")
            [*slice(result.items,0,count - 1),tail ++ trailing_check.text]
            else append_text(result.items,trailing_check.text);
        {*:result,items:items}
    } else result
}

fn inline(tokens, sources, index = 0, text = "", items = [], stop_blocks = true, table_mode = false) Parsed^ {
    if (index >= len(tokens)) {index: index, items: append_text(items, text), tokens: tokens, issue: null}
    else {
        let token = tokens[index];
        let pending_comment = if (not paragraph_space(token,sources)^) comment_between_text(tokens,sources,index)^ else null;
        if (pending_comment != null) {
            let updated = [*slice(tokens,0,index),{*:token,paragraph_gap_seen:true},*slice(tokens,index + 1,len(tokens))];
            inline(updated,sources,index,text ++ pending_comment,items,stop_blocks,table_mode)
        }
        else if (paragraph_space(token,sources)^) {
            let run = paragraph_run(tokens,sources,index)^;
            if (run.issue != null) run
            else if (run.fast) inline(tokens,sources,run.index,text ++ join([for (event in run.events) event.value],""),
                items,stop_blocks,table_mode)
            else {
                let normalized = normalize_paragraph(tokens,index,run)^;
                inline(normalized,sources,index,text,items,stop_blocks,table_mode)
            }
        }
        else if (token.kind == "paragraph-comment")
            inline(tokens,sources,index + 1,text,items,stop_blocks,table_mode)
        else if (stop_blocks and (control(token, "par") or block(token)))
            {index: index, items: append_text(items, text), tokens: tokens, issue: null}
        else if (control(token, "par"))
            inline(tokens, sources, index + 1, "", [*append_text(items, text), 'parbreak'], stop_blocks, table_mode)
        else if (token.raw_environment != null) {
            let raw = raw_environment_node(token,index)^;
            if (raw.issue != null) raw
            else inline(tokens,sources,raw.index,"",[*append_text(items,text),raw.node],stop_blocks,table_mode)
        }
        else if (not stop_blocks and control(token, "begin")) {
            let gap = environment_command_gap(tokens, sources, index)^;
            if (gap == null or (gap != "" and (trim(gap) ^ { null }) != ""))
                unsupported(index, "environment command source gap")
            else if (gap != "") {
                // native parsing leaves a separated name group in the inline stream.
                let rest = restore_control_gap(tokens, sources, index)^;
                inline(rest, sources, index + 1, text, items, stop_blocks, table_mode)
            }
            else {
                let env = parse_environment(tokens, sources, index, "inline")^;
                if (env.issue != null) env
                else inline(env.tokens, sources, env.index, "", [*append_text(items, text), env.node], stop_blocks, table_mode)
            }
        }
        // native inline parsing consumes the end command but leaves its argument.
        else if (control(token, "end")) {
            let gap = control_gap(tokens, sources, index)^;
            inline(tokens, sources, index + 1, text ++ gap.text, items, stop_blocks, table_mode)
        }
        else if (token.kind == "source-span") {
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let contiguous = next != null and next.file == token.file and next.seq == token.last_seq + 1;
            // reconstructed segment boundaries map to the following emitted token.
            let mapped_end = if (next == null or contiguous) null
                else if (next.output_start != null and next.output_segments != null)
                    rebuild.map_offset(next.output_segments,next.output_start)^
                else if (next.read_offset != null) next.read_offset
                else if (next.file == token.file and (next.direct == true or next.kind == "source-span")) next.start
                else null;
            let group_index = definition_group_index(tokens,index)^;
            let following = if (group_index < 0) null else tokens[group_index];
            let can_map = following != null and token.output_end != null and following.output_start != null and
                following.output_text != null and following.output_start >= token.output_end;
            let gap = if (can_map) utf8.decode(slice(binary(following.output_text)^,token.output_end,following.output_start))^ else null;
            let joined_group = gap != null and (gap == "" or comment_gap(gap)^);
            let definition = definition_span(token, sources, false, mapped_end)^;
            if (definition.issue != null) {
                let continued = definition_continuation(tokens,sources,index)^;
                // definition helpers can fail before assigning a token index.
                if (continued.issue != null) {*:definition,index:index}
                else inline(tokens,sources,continued.index,"",[*append_text(items,text),*continued.nodes],stop_blocks,table_mode)
            }
            // The echoed span can contain a later paragraph that belongs to the outer block parser.
            else if (definition.rest_tokens != null) {
                let rest = [for (part in definition.rest_tokens) {*:part,
                    *:if (token.seq == null or part.seq == null) {} else {seq:token.seq + part.seq - 1}}];
                inline([*slice(tokens,0,index),*rest,*slice(tokens,index + 1,len(tokens))],
                    sources,index,"",[*append_text(items,text),*definition.nodes],stop_blocks,table_mode)
            }
            else if (group_index >= 0 and not can_map) unsupported(index, "definition following group source mapping")
            // delimited primitive definitions end at their parameter text in the native parser.
            else if (joined_group and definition.delimited == true)
                inline(tokens,sources,index + 1,"",[*append_text(items,text),*definition.nodes],stop_blocks,table_mode)
            else if (joined_group) {
                let node = if (len(definition.nodes) == 0) null else definition.nodes[len(definition.nodes) - 1];
                let valid = definition.end_offset != null and node is element and tex_reader.definition_command(string(name(node)));
                let args = if (not valid or token.output_end == null or token.output_segments == null)
                    unsupported(index, "definition following group source mapping")
                    else generic_arguments(tokens,sources,group_index,token.end,token.file,[],[],true,true,false,
                        token.output_end,true,if (contains(["newenvironment", "renewenvironment"], string(name(node)))) "raw" else "group")^;
                if (args.issue != null) args
                else if (len(args.groups) == 0)
                    // native skips a comment gap even when a following parenthesis is not an argument.
                    inline(tokens,sources,if (comment_gap(gap)^) group_index else index + 1,"",
                        [*append_text(items,text),*definition.nodes],stop_blocks,table_mode)
                else {
                    let merged = merge_definition_nodes(definition.nodes,args)^;
                    inline(tokens,sources,args.index,"",[*append_text(items,text),*merged],stop_blocks,table_mode)
                }
            }
            else inline(tokens, sources, index + 1, "", [*append_text(items, text), *definition.nodes], stop_blocks, table_mode)
        }
        else if (dollar(token) or control(token, "(") or control(token, "[")) {
            let math = math_element(tokens, sources, index)^;
            if (math.issue != null) math
            else inline(tokens, sources, math.index, "", [*append_text(items, text), math.node], stop_blocks, table_mode)
        }
        else if (opening(token)) {
            let group = read_group(tokens, index)^;
            if (group.issue != null) group
            else if (token.box_open == true and box_span_needs_reparse(tokens, sources, index, group)^)
                unsupported(index, "box source gap")
            else {
                let children = inline_group(group, sources)^;
                if (children.issue != null) children
                else if (children.index != len(children.tokens)) unsupported(index, "group")
                else inline(tokens, sources, group.end, "", [*append_text(items, text),
                    <curly_group *children.items>], stop_blocks, table_mode)
            }
        }
        else if (token.kind == "active" and contains(["\\", "{", "}", "$", "&", "#", "^", "_", "%"], token.value))
            unsupported(index, "active syntax character")
        else if (token.kind == "active")
            inline(tokens, sources, index + 1, text ++ token.value, items, stop_blocks, table_mode)
        // native tabular bodies emit cell separators at this nesting level only.
        else if (table_mode and token.kind == "char" and token.value == "&")
            inline(tokens, sources, index + 1, "", [*append_text(items, text), 'alignment_tab'], stop_blocks, table_mode)
        // Native constructor arguments retain directly read character-definition names.
        else if (not stop_blocks and token.chardef_emission == true) {
            let origin = token.chardef_origin;
            let raw = chardef_source(token,sources)^;
            if (raw == null) unsupported(index, "character definition argument source mapping")
            else inline([*slice(tokens,0,index),origin,*slice(tokens,index + 1,len(tokens))],sources,
                index,text,items,stop_blocks,table_mode)
        }
        else if (escaped_text_command(token)^) {
            let node = escaped_text_node(token)^;
            inline(tokens,sources,index + 1,"",[*append_text(items,text),node],stop_blocks,table_mode)
        }
        else if (token.kind == "char" and not opening(token) and not closing(token) and token.cat != 3) {
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let gap = if (token.cat != 10 and token.output_constructor_mode == true and next != null and
                    token.output_end != null and next.output_start != null) output_between(token,next)^ else "";
            // reconstruct source spaces discarded by the TeX reader in constructor arguments.
            let after = if (gap == " ") " " else "";
            inline(tokens, sources, index + 1, text ++ (if (token.cat == 10)
                whitespace(token, next, sources)^ else token.value ++ after), items, stop_blocks, table_mode)
        }
        else if (control(token, "item")) {
            let opt = read_optional(tokens, index + 1)^;
            let offset = if (token.read_offset != null) token.read_offset else token.start;
            if (opt.issue != null) opt
            else if (opt.present) {
                let label = <brack_group group_text(opt)>;
                inline(tokens, sources, opt.end, "", [*append_text(items, text),
                    <item source_offset: offset, label>], stop_blocks, table_mode)
            }
            else {
                let gap = control_gap(tokens, sources, index)^;
                inline(tokens, sources, index + 1, gap.text,
                    [*append_text(items, text), <item source_offset: offset>], stop_blocks, table_mode)
            }
        }
        else if (token.kind == "control" and token.direct != true and token.read_offset == null and
            inline_format_command(token.value))
            unsupported(index, "inline command source mapping")
        else if (token.kind == "control" and inline_format_command(token.value)) {
            // native parsing keeps a formatting node even without a following group.
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let star = next != null and next.kind == "char" and next.value == "*";
            let generated = star and (inline_star_needs_mapping(token,next) or inline_star_group_needs_mapping(tokens,sources,index)^);
            let mapped = star and (if (generated) reconstructed_token(token)^ and reconstructed_literal(next)^
                else token.direct == true and next.direct == true and token.file == next.file);
            let gap_before_star = if (generated and star) output_between(token,next)^ else "";
            let starred = mapped and (if (generated) gap_before_star == "" else token.end == next.start);
            let after_star = index + if (starred) 2 else 1;
            let group_at = if (starred) list_space_index(tokens,sources,after_star)^ else after_star;
            let between = if (group_at > after_star and group_at < len(tokens) and opening(tokens[group_at]))
                if (tokens[after_star - 1].output_end != null) output_between(tokens[after_star - 1],tokens[group_at])^
                else if (tokens[after_star - 1].direct == true and tokens[group_at].direct == true and
                        tokens[after_star - 1].file == tokens[group_at].file)
                    source_span({direct:true,file:tokens[group_at].file,start:tokens[after_star - 1].end,end:tokens[group_at].start},sources)^
                else null
                else null;
            // native skips only literal whitespace between a star and a group.
            let at = if (between != null and all([for (b in binary(between)^) ascii_space(b)])) group_at else after_star;
            let gap = format_control_gap(tokens, sources, at - 1)^;
            if (star and (not mapped or gap_before_star == null)) unsupported(index, "inline command star source mapping")
            else if (starred and group_at > after_star and group_at < len(tokens) and opening(tokens[group_at]) and between == null)
                unsupported(index, "inline command argument source mapping")
            else if (at >= len(tokens) or not opening(tokens[at]) or (gap.valid and gap.seen)) {
                let node = inline_element(token.value, if (token.read_offset != null)
                    token.read_offset else token.start, []);
                let text_gap = if (gap.valid) gap.text else spacing_word_gap(tokens, sources, at - 1)^;
                inline(tokens, sources, at, text_gap,
                    [*append_text(items, text), node], stop_blocks, table_mode)
            }
            else {
                let arg = read_group(tokens, at)^;
                if (arg.issue != null) arg
                else {
                    let children = inline_group(arg, sources)^;
                    if (children.issue != null) children
                    else if (children.index != len(children.tokens)) unsupported(index, "inline group")
                    else {
                        let node = inline_element(token.value, if (token.read_offset != null)
                            token.read_offset else token.start, children.items);
                        inline(tokens, sources, arg.end, "", [*append_text(items, text), node], stop_blocks, table_mode)
                    }
                }
            }
        }
        else if (token.kind == "control" and token.value == " ") {
            let gap = source_control_gap(tokens, sources, index)^;
            if (gap == null) unsupported(index, "control-space source gap")
            else inline(tokens, sources, index + 1, text ++ " " ++ gap, items, stop_blocks, table_mode)
        }
        else if (token.kind == "control" and contains(["$", "%", "#", "&", "_", "{", "}"], token.value))
            inline(tokens, sources, index + 1, text ++ token.value, items, stop_blocks, table_mode)
        else if (token.kind == "control" and contains(["\\", "quad", "qquad", ",", ";", ":", "!"], token.value)) {
            let gap = if (contains(["quad", "qquad"], token.value))
                spacing_word_gap(tokens, sources, index)^ else control_gap(tokens, sources, index).text;
            let value = if (token.value == "\\") 'row_sep' else if (token.value == "quad") 'quad'
                else if (token.value == "qquad") 'qquad' else if (token.value == ",") ','
                else if (token.value == ";") ';' else if (token.value == ":") ':' else '!';
            if (gap == null) unsupported(index, "spacing command source gap")
            else inline(tokens, sources, index + 1, gap, [*append_text(items, text), value], stop_blocks, table_mode)
        }
        else if (token.tabbing_command == true) {
            let node = <[token.value] source_offset:environment_offset(token)^, argument_groups:[]>;
            inline(tokens, sources, index + 1, "", [*append_text(items, text), node], stop_blocks, table_mode)
        }
        else if (token.kind == "control" and accent_name(token.value)) {
            let accent = accent_node(tokens, sources, index)^;
            if (accent.issue != null) accent
            else inline(tokens, sources, accent.index, "", [*append_text(items, text), accent.node], stop_blocks, table_mode)
        }
        else if (token.kind == "control" and contains(["hrule", "vrule"], token.value) and token.echo_span != null and
            rule_span_needs_reparse(token, sources)^)
            unsupported(index, "rule comment source gap")
        // a replayed synthetic rule's last argument follows the later source cursor.
        else if (control(token, "rule") and token.emit_origin != null and token.box_replayed == true)
            unsupported(index, "box rule source mapping")
        else if (control(token, "rule") and token.emit_origin != null) {
            let rule = generated_rule(tokens, index)^;
            if (rule.issue != null) rule
            else inline(tokens, sources, rule.index, "", [*append_text(items, text), rule.node], stop_blocks, table_mode)
        }
        else if (control(token, "mbox") and token.emit_origin != null) {
            let box = generated_mbox(tokens, sources, index)^;
            if (box.issue != null) box
            else inline(tokens, sources, box.index, "", [*append_text(items, text), box.node], stop_blocks, table_mode)
        }
        else if (token.kind == "control" and token.definition_catcodes != null and
                tex_reader.definition_command(token.value)) {
            let definition = inline_definition(tokens, sources, index)^;
            if (definition.issue != null) definition
            else inline(tokens, sources, definition.index, "", [*append_text(items, text), *definition.nodes], stop_blocks, table_mode)
        }
        else if (token.raw_verb == true) {
            let verb = verb_element(token,index,not stop_blocks and token.group_scan_safe != true)^;
            if (verb.issue != null) verb
            else inline(tokens,sources,index + 1,"",[*append_text(items,text),verb.node],stop_blocks,table_mode)
        }
        else if (token.raw_inline_kind != null) {
            let raw = raw_inline_element(token,index,not stop_blocks)^;
            if (raw.issue != null) raw
            else inline(tokens,sources,index + 1,"",[*append_text(items,text),raw.node],stop_blocks,table_mode)
        }
        else if (token.raw_group_kind != null) {
            let raw = raw_group_element(token,index,not stop_blocks)^;
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            // reconstructed gaps include separators and comment-following whitespace.
            let gap = if (next == null or paragraph_space(next,sources)^) ""
                else output_gap_text(output_between(token,next)^)^;
            if (raw.issue != null) raw
            else if (gap == null) unsupported(index,"raw group source gap")
            else inline(tokens,sources,index + 1,raw.tail ++ gap,[*append_text(items,text),raw.node],stop_blocks,table_mode)
        }
        // the native scanner can split UTF-8 inside a control symbol (LI-064).
        else if (token.kind == "control" and token.output_constructor_mode == true and token.output_start != null and
                any([for (b in binary(token.value)^) b >= 128]))
            unsupported(index, "native command unicode source mapping")
        // TeX can emit a control symbol followed by letters as one native word.
        else if (control(token,"@") and token.output_constructor_mode == true and token.output_start != null and
                token.output_end != null and index + 1 < len(tokens) and native_name_letter(tokens[index + 1])^ and
                tokens[index + 1].output_start == token.output_end) {
            let end = native_join_end(tokens,sources,index + 1,token.output_end)^;
            let word = "@" ++ join([for (t in slice(tokens,index + 1,end)) t.value], "");
            if (len(word) >= 95) unsupported(index, "native command lexical source mapping")
            else {
                let joined = {*:token, value:word, direct:false, read_offset:rebuild.map_offset(token.output_segments,token.output_start)^,
                    read_file:if(token.read_file != null) token.read_file else token.file, output_end:tokens[end - 1].output_end};
                inline([*slice(tokens,0,index), joined, *slice(tokens,end,len(tokens))], sources, index, text, items, stop_blocks, table_mode)
            }
        }
        else if (native_split_word(token)^ != null or native_escaped_word(token)^) {
            let split_word = native_split_word(token)^;
            let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let gap = if (next == null) "" else output_between(token,next)^;
            let checked = if (gap == null) null else source_gap_bytes(binary(gap)^)^;
            // The serializer preserves comments and whitespace after the original token.
            if (checked == null or not checked.valid or (split_word == null and gap != "" and gap != " "))
                unsupported(index, "native command source gap")
            else if (split_word == null)
                inline(tokens, sources, index + 1, text ++ token.value ++ gap, items, stop_blocks, table_mode)
            else {
                // Keep comments with a following paragraph token in one source run.
                let joined_paragraph = contains(gap,"%") and paragraph_space(next,sources)^;
                let gap_events = if (joined_paragraph) [{kind:"char",value:" ",cat:10,
                    normalize_output_gap:true,output_text:token.output_text,output_start:token.output_end,
                    output_end:next.output_start}] else paragraph_events(gap)^;
                let replaced = [*slice(tokens,0,index), *split_word, *gap_events, *slice(tokens,index + 1,len(tokens))];
                inline(replaced, sources, index, text, items, stop_blocks, table_mode)
            }
        }
        // do not treat a longer TeX control token as one native command.
        else if (token.kind == "control" and token.output_constructor_mode == true and token.output_start != null and
                len(token.value) > 1 and (len(token.value) >= 95 or native_word_end(split(token.value, "")^) != len(token.value)))
            unsupported(index, "native command lexical source mapping")
        else if (control(token,"setlength")) {
            let parsed = setlength_node(tokens,sources,index)^;
            if (parsed.issue != null) parsed
            else inline(tokens,sources,parsed.index,"",[*append_text(items,text),parsed.node],stop_blocks,table_mode)
        }
        else if (token.kind == "control" and special_command(token.value))
            unsupported(index, token.value)
        // ordinary expansion may discard argument tokens needed by the native parser.
        else if (token.kind == "control" and (token.direct != true or token.read_offset != null) and
                (token.output_constructor_mode != true or not reconstructed_token(token)^ or
                    (token.read_offset == null and (token.emit_origin == null or token.output_synthetic_verified != true)) or
                    (token.read_offset != null and (token.read_file == null or
                        (token.direct == true and token.file == token.read_file)))))
            unsupported(index, "command source mapping")
        else if (token.kind == "control") {
            // native commands consume a star only when it immediately follows the control word.
            // Synthetic commands use their verified reconstructed spelling and emission origin.
            let mapped_command = token.read_offset != null or (token.output_synthetic_verified == true and token.direct != true and token.emit_origin != null and
                reconstructed_token(token)^) or (token.output_start != null and
                expanded_argument_sources(tokens,index + 1,token.file,sources)^);
            let offset = if (mapped_command) rebuild.map_offset(token.output_segments,token.output_start)^ else token.start;
            let next_token = if (index + 1 < len(tokens)) tokens[index + 1] else null;
            let star = next_token != null and next_token.kind == "char" and next_token.value == "*";
            let mapped = star and (if (mapped_command) reconstructed_literal(next_token)^
                else next_token.direct == true and token.file == next_token.file);
            let star_gap = if (mapped_command and star) output_between(token,next_token)^ else "";
            let starred = mapped and (if (mapped_command) star_gap == "" else token.end == next_token.start);
            let tag = if (token.value == "newtheorem" and starred) "newtheorem*" else token.value;
            let at = index + if (starred) 2 else 1;
            let next = if (at < len(tokens)) tokens[at] else null;
            let anchor = tokens[at - 1];
            let gap = if (mapped_command and next != null) output_between(anchor,next)^
                else if (next != null and anchor.direct == true and next.direct == true and
                    anchor.file == next.file and next.start > anchor.end)
                source_span({direct: true, file: anchor.file, start: anchor.end, end: next.start}, sources)^ else "";
            let spaced_group = next != null and (opening(next) or
                (next.kind == "char" and (next.value == "[" or next.value == "("))) and gap != "";
            let joined = if (spaced_group) comment_gap(gap)^ else false;
            if (mapped_command and next != null and gap == null) unsupported(index, "command source mapping")
            else if (star and (not mapped or star_gap == null)) unsupported(index, "command star source mapping")
            else if (spaced_group and not joined and (gap == null or (trim(gap) ^ { null }) != ""))
                unsupported(index, "command argument source gap")
            else if (spaced_group and not joined) {
                let attrs = {source_offset: offset, *:if (starred) {starred: true} else {}, argument_groups: []};
                let node = <[tag] *:attrs>;
                inline(tokens, sources, at, gap, [*append_text(items, text), node], stop_blocks, table_mode)
            }
            else if (at < len(tokens) and (opening(tokens[at]) or
                    (tokens[at].kind == "char" and (tokens[at].value == "[" or tokens[at].value == "(")))) {
                let args = generic_arguments(tokens, sources, at, anchor.end, token.file, [], [], true, false, false,
                    anchor.output_end, mapped_command, if (code_fragment_command(token.value)) "raw" else "")^;
                if (args.issue != null) args
                else {
                    let following = if (args.index < len(tokens)) tokens[args.index] else null;
                    let tail_gap = if (mapped_command and following != null)
                        output_gap_text(output_between(tokens[args.index - 1],following)^)^ else "";
                    let attrs = {source_offset: offset, *:if (starred) {starred: true} else {}, argument_groups: args.groups};
                    let content = pack_arguments(args.items)^;
                    let node = <[tag] *:attrs, *content>;
                    if (tail_gap == null) unsupported(index, "command argument source mapping")
                    else inline(tokens, sources, args.index, tail_gap, [*append_text(items, text), node], stop_blocks, table_mode)
                }
            }
            else {
                let gap = if (mapped_command) {text: output_gap_text(gap)^} else if (starred) {text: ""} else control_gap(tokens, sources, index)^;
                let attrs = {source_offset: offset, *:if (starred) {starred: true} else {}, argument_groups: []};
                let node = <[tag] *:attrs>;
                if (gap.text == null) unsupported(index, "command source gap")
                else inline(tokens, sources, at, gap.text, [*append_text(items, text), node], stop_blocks, table_mode)
            }
        }
        else unsupported(index, "token")
    }
}

fn declaration(name, offset, groups, children, starred = false) =>
    <[name] source_offset: offset, *:if (starred) {starred: true} else {}, argument_groups: groups, *children>

fn environment_node(name, offset, children, attrs = {}) => <[name] source_offset: offset, *:attrs, *children>

// foreign environment offsets follow reconstructed segment boundaries.
fn environment_offset(token) any^ => if ((token.read_offset != null or token.direct != true) and
        token.output_start != null and token.output_segments != null)
        rebuild.map_offset(token.output_segments,token.output_start)
    else if (token.read_offset != null) token.read_offset else token.start

// Native digestion assigns special headers, math or raw bodies to these environments.
fn special_environment(name) any^ => tex_reader.raw_environment_name(name)^ or common_math_environment(name) or contains([
    "eqnarray", "eqnarray*", "aligned*", "multline", "multline*", "split*",
    "IEEEeqnarray*", "array*", "smallmatrix*"], name)

fn heading(name, attrs) => <[name] *:attrs>

// the LaTeX source parser requires the environment name brace to touch the command.
fn environment_command_gap(tokens, sources, index) any^ {
    let token = tokens[index];
    let next = if (index + 1 < len(tokens)) tokens[index + 1] else null;
    if (next == null or token.direct != true or next.direct != true or token.file != next.file or
        next.start <= token.end) ""
    else source_span({direct: true, file: token.file, start: token.end, end: next.start}, sources)
}

// Generated environment names are admitted only when their emitted bytes match the token sequence.
fn reconstructed_environment(tokens, index, arg) any^ {
    let first = tokens[index];
    let final_token = tokens[arg.end - 1];
    if (first.output_text == null or first.output_start == null or final_token.output_end == null or
        final_token.output_text != first.output_text or final_token.output_end < first.output_start) false
    else utf8.decode(slice(binary(first.output_text)^, first.output_start, final_token.output_end))^ ==
        "\\" ++ first.value ++ "{" ++ group_text(arg) ++ "}"
}

// Subfigure headers retain their raw argument groups rather than becoming body text.
fn environment_header_group(tokens, sources, index, previous_end, file, optional) any^ {
    let at = nonspace_index(tokens, index)^;
    let token = if (at < len(tokens)) tokens[at] else null;
    let present = if (optional) token != null and token.kind == "char" and token.value == "["
        else opening(token);
    if (not present) {issue:null, index:index, end:previous_end, groups:[], tokens:null}
    else {
        let gap = if (token.direct == true and token.file == file and token.start >= previous_end)
            source_span({direct:true, file:file, start:previous_end, end:token.start}, sources)^ else null;
        let group = if (optional) read_generic_delimited(tokens, at, "[", "]", "latex-digest-unclosed-optional")^
            else read_group(tokens, at)^;
        if (group.issue != null) group
        else {
            let raw = group_raw(group, sources)^;
            if (gap == null or (trim(gap) ^ { null }) != "" or raw == null or
                any([for (part in group.tokens) part.direct != true or part.file != file]))
                unsupported(at, "environment header source mapping")
            else {issue:null, index:group.end, end:group.end_offset, tokens:group.tokens,
                groups:[{kind:if (optional) "optional" else "required", raw:raw,
                    start:environment_offset(token)^, end:argument_end_offset(tokens, group)^}]}
        }
    }
}

fn multicols_header(tokens, sources, arg) any^ {
    let count = environment_header_group(tokens, sources, arg.end, arg.end_offset, arg.file, false)^;
    if (count.issue != null) count
    else {
        let preface = environment_header_group(tokens, sources, count.index, count.end, arg.file, true)^;
        if (preface.issue != null) preface
        else {
            let skip = environment_header_group(tokens, sources, preface.index, preface.end, arg.file, true)^;
            if (skip.issue != null) skip
            else {
                let parsed = if (preface.tokens != null) blocks(preface.tokens, sources)^ else null;
                if (parsed != null and parsed.issue != null) parsed
                else {issue:null, index:skip.index,
                    attrs:{argument_groups:[*count.groups, *preface.groups, *skip.groups],
                        *:if (len(count.groups) > 0) {columns:count.groups[0].raw} else {}},
                    nodes:if (parsed != null) [<multicols_preface *parsed.nodes>] else []}
            }
        }
    }
}

fn subfigure_header(tokens, sources, arg) any^ {
    let option = environment_header_group(tokens, sources, arg.end, arg.end_offset, arg.file, true)^;
    if (option.issue != null) option
    else {
        let width = environment_header_group(tokens, sources, option.index, option.end, arg.file, false)^;
        if (width.issue != null) width
        else {issue:null, index:width.index, attrs:{argument_groups:[*option.groups, *width.groups]}}
    }
}

fn table_columns_environment(name) bool => contains(["tabular", "array", "longtable", "tabularx", "xltabular"], name)

// Table width and column groups are consumed only when adjacent to the preceding group.
fn table_header_group(tokens, sources, index, previous_end, file) any^ {
    let next = if (index < len(tokens)) tokens[index] else null;
    if (not opening(next)) {issue:null, index:index, end:previous_end, raw:null}
    else if (next.direct != true or next.file != file) unsupported(index, "table header source mapping")
    else if (next.start != previous_end) {issue:null, index:index, end:previous_end, raw:null}
    else {
        let group = read_group(tokens, index)^;
        if (group.issue != null) group
        else {
            let raw = group_raw(group, sources)^;
            if (raw == null or any([for (part in group.tokens) part.direct != true or part.file != file]))
                unsupported(index, "table columns source mapping")
            else {issue:null, index:group.end, end:group.end_offset, raw:raw}
        }
    }
}

// table headers belong to the environment only when they touch its name.
fn table_header(name, tokens, sources, arg) any^ {
    let at = arg.end;
    let next = if (at < len(tokens)) tokens[at] else null;
    let adjacent = next != null and next.direct == true and arg.direct == true and
        next.file == arg.file and next.start == arg.end_offset;
    let header_syntax = next != null and name == "table" and next.kind == "char" and next.value == "[";
    if (header_syntax and (next.direct != true or next.file != arg.file))
        unsupported(at, "table header source mapping")
    else if (name == "table" and adjacent and next.kind == "char" and next.value == "[") {
        let option = read_optional(tokens, at)^;
        if (option.issue != null) option
        else {
            let raw = group_raw(option, sources)^;
            let content = inline(option.tokens, sources, 0, "", [], false)^;
            if (raw == null or contains(raw, "%") or
                any([for (token in option.tokens) token.direct != true or token.file != option.file]))
                unsupported(at, "table placement source mapping")
            else if (content.issue != null) content
            else if (content.index != len(content.tokens)) unsupported(at, "table placement")
            else {issue: null, index: option.end, attrs: {placement: <brack_group *content.items>}}
        }
    }
    else if (table_columns_environment(name)) {
        let width = if (contains(["tabularx", "xltabular"], name))
            table_header_group(tokens, sources, at, arg.end_offset, arg.file)^ else
            {issue:null, index:at, end:arg.end_offset, raw:null};
        if (width.issue != null) width
        else {
            let columns = table_header_group(tokens, sources, width.index, width.end, arg.file)^;
            if (columns.issue != null) columns
            else {issue:null, index:columns.index, attrs:{*:if (width.raw != null) {width:width.raw} else {},
                *:if (columns.raw != null) {columns:columns.raw} else {}}}
        }
    }
    else { {issue: null, index: at, attrs: map([])} }
}

// the native scanner sees emitted bytes, not the original token's category code.
fn list_emission(token, sources) any^ {
    if (token.output_start != null and token.output_end != null and token.output_text != null)
        utf8.decode(slice(binary(token.output_text)^,token.output_start,token.output_end))
    else if (token.echo_span != null or token.reconstruct_text != null or token.kind == "source-span") null
    else rebuild.source_fragment(token,sources).body
}

fn list_initial(token, sources) any^ {
    if (token.reconstruct_text != null) token.reconstruct_text
    else if (token.echo_span != null) source_span({direct:true,file:token.echo_span.file,
        start:token.echo_span.start,end:token.echo_span.end},sources)
    else if (token.kind == "source-span") token.value
    else null
}

fn list_first_nonspace(bytes, index = 0) any^ =>
    if (index < len(bytes) and ascii_space(bytes[index])) list_first_nonspace(bytes,index + 1) else index

fn list_space_index(tokens, sources, index) any^ {
    if (index >= len(tokens)) index
    else {
        let spelling = list_emission(tokens[index],sources)^;
        let raw = if (spelling == null) list_initial(tokens[index],sources)^ else spelling;
        if (raw != null and list_first_nonspace(binary(raw)^)^ == len(binary(raw)^))
            list_space_index(tokens,sources,index + 1) else index
    }
}

// comments and escapes can span token boundaries in the emitted source.
fn scan_group_bytes(bytes, index, depth, comment, escaped, open_byte = 91, close_byte = 93) any^ {
    if (index >= len(bytes)) {closed:false,offset:index,depth:depth,comment:comment,escaped:escaped}
    else {
        let byte = bytes[index];
        if (comment) scan_group_bytes(bytes,index + 1,depth,byte != 10 and byte != 13,false,open_byte,close_byte)
        else if (escaped) scan_group_bytes(bytes,index + 1,depth,false,false,open_byte,close_byte)
        else if (byte == 92) scan_group_bytes(bytes,index + 1,depth,false,true,open_byte,close_byte)
        else if (byte == 37) scan_group_bytes(bytes,index + 1,depth,true,false,open_byte,close_byte)
        else if (byte == open_byte) scan_group_bytes(bytes,index + 1,depth + 1,false,false,open_byte,close_byte)
        else if (byte == close_byte and depth == 1) {closed:true,offset:index + 1,depth:0,comment:false,escaped:false}
        else scan_group_bytes(bytes,index + 1,depth - if (byte == close_byte) 1 else 0,false,false,open_byte,close_byte)
    }
}

fn list_option_end(tokens, sources, index, depth = 1, comment = false, escaped = false) any^ {
    if (index >= len(tokens)) {issue: "latex-digest-unclosed-optional", end: index}
    else {
        let previous = tokens[index - 1];
        let current = tokens[index];
        let gap = if (previous.output_end != null and current.output_start != null) output_between(previous,current)^
            else if (previous.direct == true and current.direct == true and previous.file == current.file and
                    previous.seq != null and current.seq == previous.seq + 1 and current.start >= previous.end)
                source_span({direct:true,file:previous.file,start:previous.end,end:current.start},sources)^ else null;
        let spelling = list_emission(current,sources)^;
        if (gap == null or spelling == null) unsupported(index,"environment option source mapping")
        else {
            let gap_scan = scan_group_bytes(binary(gap)^,0,depth,comment,escaped)^;
            let bytes = binary(spelling)^;
            let scan = if (gap_scan.closed) gap_scan else
                scan_group_bytes(bytes,0,gap_scan.depth,gap_scan.comment,gap_scan.escaped)^;
            if (gap_scan.closed) unsupported(index,"environment option token boundary")
            else if (scan.closed and scan.offset != len(bytes)) {
                // Split a verified control word so the bracket closes the option at its actual byte.
                let split = native_split_word(current)^;
                if (split == null) unsupported(index,"environment option token boundary")
                else list_option_end([*slice(tokens,0,index),*split,*slice(tokens,index + 1,len(tokens))],
                    sources,index,depth,comment,escaped)
            }
            else if (scan.closed) {issue:null,end:index + 1,tokens:tokens}
            else list_option_end(tokens,sources,index + 1,scan.depth,scan.comment,scan.escaped)
        }
    }
}

fn list_direct_option(tokens, sources, index, end, file) any^ {
    if (index >= end) true
    else {
        let token = tokens[index];
        let raw = source_span(token,sources)^;
        let spelling = list_emission(token,sources)^;
        token.direct == true and token.file == file and token.seq != null and raw != null and raw == spelling and
            (index + 1 == end or (tokens[index + 1].seq != null and tokens[index + 1].seq == token.seq + 1)) and
            list_direct_option(tokens,sources,index + 1,end,file)^
    }
}

// list options retain their raw spelling and consume only preceding whitespace.
fn list_header(tokens, sources, arg) any^ {
    let at = list_space_index(tokens,sources,arg.end)^;
    let next = if (at < len(tokens)) tokens[at] else null;
    let spelling = if (next == null) "" else list_emission(next,sources)^;
    let initial = if (spelling == null) list_initial(next,sources)^ else spelling;
    let initial_bytes = if (initial == null) null else binary(initial)^;
    let first = if (initial_bytes == null) 0 else list_first_nonspace(initial_bytes)^;
    if (initial == null or (spelling != "[" and first < len(initial_bytes) and initial_bytes[first] == 91))
        unsupported(at,"environment option source mapping")
    else if (spelling != "[")
        {issue: null, index: arg.end, attrs: {}}
    else {
        let ending = tokens[arg.end - 1];
        let gap = if (ending.output_end != null and next.output_start != null) output_between(ending,next)^
            else if (ending.direct == true and next.direct == true and ending.file == next.file and next.start >= ending.end)
                source_span({direct:true,file:ending.file,start:ending.end,end:next.start},sources)^ else null;
        if (gap == null) unsupported(at, "environment option source gap")
        else if (list_first_nonspace(binary(gap)^)^ != len(binary(gap)^)) {issue: null, index: arg.end, attrs: {}}
        else {
            let option = list_option_end(tokens,sources,at + 1)^;
            if (option.issue != null) option
            else {
                let final_token = option.tokens[option.end - 1];
                let mapped = next.output_start != null and next.output_end != null and final_token.output_start != null and
                    final_token.output_end != null;
                let raw = if (mapped) utf8.decode(slice(binary(next.output_text)^,next.output_start + 1,final_token.output_end - 1))^
                    else source_span({direct:next.direct,file:next.file,start:next.start + 1,end:final_token.end - 1},sources)^;
                if (raw == null or (not mapped and not list_direct_option(option.tokens,sources,at,option.end,next.file)^))
                    unsupported(at, "environment option source mapping")
                else {issue: null, index: option.end, tokens:option.tokens, attrs: {options_raw: raw}}
            }
        }
    }
}

// math environment delimiters can nest without closing the outer environment.
fn math_environment_end(tokens, index, name, depth = 0, strict = true) any^ {
    if (index >= len(tokens)) unsupported(index, "unclosed math environment")
    else if (control(tokens[index], "begin") or control(tokens[index], "end")) {
        let group = read_group(tokens, index + 1)^;
        if (group.issue != null or not plain_group(group)) unsupported(index, "math environment delimiter")
        else if (strict and (tokens[index].direct != true or group.direct != true or tokens[index].file != group.file or
            tokens[index].end != group.start_offset))
            unsupported(index, "math environment delimiter source mapping")
        else if (control(tokens[index], "begin")) math_environment_end(tokens, group.end, name, depth + 1, strict)
        else if (depth > 0) math_environment_end(tokens, group.end, name, depth - 1, strict)
        else if (group_text(group) == name) {issue: null, index: index, end: group.end}
        else unsupported(index, "math environment end")
    }
    else math_environment_end(tokens, index + 1, name, depth, strict)
}

// Only commands at the top level of tabbing use its tab-stop meanings.
fn tabbing_commands(tokens, index, end, groups = 0, environments = 0, found = []) any^ {
    if (index >= end) found
    else {
        let token = tokens[index];
        if (control(token,"begin") or control(token,"end")) {
            let name = read_group(tokens, index + 1)^;
            if (name.issue != null) found
            else tabbing_commands(tokens, name.end, end, groups,
                environments + if (control(token,"begin")) 1 else -1, found)
        }
        else {
            let marked = groups == 0 and environments == 0 and token.kind == "control" and
                contains(["=", "'", "`"], token.value);
            tabbing_commands(tokens, index + 1, end,
                groups + if (opening(token)) 1 else if (closing(token)) -1 else 0, environments,
                if (marked) [*found, index] else found)
        }
    }
}

fn tabbing_stream(tokens, index) any^ {
    let ending = math_environment_end(tokens, index, "tabbing", 0, false)^;
    if (ending.issue != null) ending
    else {
        let marked = tabbing_commands(tokens, index, ending.index)^;
        {issue:null, tokens:[for (i, token in tokens)
            if (contains(marked, i)) {*:token, tabbing_command:true} else token]}
    }
}
fn ascii_space(value) => contains([9, 10, 11, 12, 13, 32], value)
fn math_trim_begin(bytes, at = 0) any^ =>
    if (at < len(bytes) and ascii_space(bytes[at])) math_trim_begin(bytes, at + 1) else at
fn math_trim_end(bytes, begin, at) any^ =>
    if (at > begin and ascii_space(bytes[at - 1])) math_trim_end(bytes, begin, at - 1) else at

fn parse_math_environment(tokens, sources, index, arg) any^ {
    let token = tokens[index];
    let name = group_text(arg);
    let finish = math_environment_end(tokens, arg.end, name)^;
    if (finish.issue != null) finish
    else {
        let end_token = tokens[finish.index];
        let body = slice(tokens, arg.end, finish.index);
        let mapped = end_token.direct == true and end_token.file == token.file and
            any([for (part in body) part.direct != true or part.file != token.file]) == false;
        let raw = if (mapped) source_span({direct: true, file: token.file, start: arg.end_offset, end: end_token.start}, sources)^ else null;
        let reconstructed = if (raw == null or contains(raw, "%")) null else
            rebuild.reconstruct({output: body, sources: sources, main_file: token.file}, true)^;
        if (reconstructed == null or reconstructed.issue != null) unsupported(index, "math environment source mapping")
        else {
            let bytes = binary(reconstructed.text)^;
            let begin = math_trim_begin(bytes)^;
            let end = math_trim_end(bytes, begin, len(bytes))^;
            let source = utf8.decode(slice(bytes, begin, end))^;
            let math_tokens = mark_output_tokens(body, reconstructed, false)^;
            let atoms = math_matrix_items(math_tokens)^;
            if (atoms.issue != null) atoms
            else {issue: null, index: finish.end, tokens: tokens,
                node: environment_node(name, environment_offset(token)^, [], {source: source, ast: <math *math_content(atoms.items)^>})}
        }
    }
}

// an environment has the same structure in a paragraph or a nested argument.
fn parse_environment(tokens, sources, index, environment) any^ {
    let token = tokens[index];
    let arg = read_group(tokens, index + 1)^;
    if (arg.issue != null) arg
    else if (not plain_group(arg) or
        ((token.direct != true or arg.direct != true) and not reconstructed_environment(tokens, index, arg)^) or
        (group_text(arg) == "document" and environment != null)) unsupported(index, "environment")
    else if (contains(["equation", "equation*", "align", "align*", "alignat", "alignat*",
        "gather", "gather*", "multline", "multline*", "eqnarray", "eqnarray*", "IEEEeqnarray", "IEEEeqnarray*"], group_text(arg)))
        parse_math_environment(tokens, sources, index, arg)
    else if (special_environment(group_text(arg))^) unsupported(index, "environment")
    else {
        let name = group_text(arg);
        let header = if (contains(["subfigure", "subtable"], name)) subfigure_header(tokens, sources, arg)^
            else if (contains(["multicols", "multicols*"], name)) multicols_header(tokens, sources, arg)^
            else if (list_environment(name) or name == "tcolorbox") list_header(tokens, sources, arg)^
            else table_header(name, tokens, sources, arg)^;
        if (header.issue != null) header
        else {
            let base_tokens = if (header.tokens == null) tokens else header.tokens;
            let prepared = if (name == "tabbing") tabbing_stream(base_tokens, header.index)^
                else {issue:null, tokens:base_tokens};
            let body = if (prepared.issue == null) blocks(prepared.tokens, sources, header.index, [], name)^ else prepared;
            if (body.issue != null) body
            // nested blocks can insert gap tokens; return the stream that owns their cursor.
            else {
                let prefix = if (header.nodes == null) [] else header.nodes;
                {issue: null, index: body.index, tokens: body.tokens, node: environment_node(name,
                    environment_offset(token)^, [*prefix, *body.nodes], header.attrs)}
            }
        }
    }
}

fn blocks(tokens, sources, index = 0, nodes = [], environment = null) Parsed^ {
    if (index >= len(tokens)) if (environment == null) {index: index, nodes: nodes, tokens: tokens, issue: null}
        else unsupported(index, "unclosed environment")
    else {
        let token = tokens[index];
        let env_gap = if (control(token, "begin") or control(token, "end"))
            environment_command_gap(tokens, sources, index)^ else "";
        if (token.raw_environment != null) {
            let raw = raw_environment_node(token,index)^;
            if (raw.issue != null) raw
            else blocks(tokens,sources,raw.index,[*nodes,raw.node],environment)
        }
        else if (paragraph_space(token,sources)^) {
            let run = paragraph_run(tokens,sources,index)^;
            if (run.issue != null) run
            else if (run.fast) {
                let para = inline(tokens,sources,index,"",[],true,table_columns_environment(environment))^;
                if (para.issue != null) para
                else blocks(para.tokens,sources,para.index,[*nodes,<paragraph *para.items>],environment)
            }
            else {
                let normalized = normalize_paragraph(tokens,index,run)^;
                blocks(normalized,sources,index,nodes,environment)
            }
        }
        else if (control(token, "par")) {
            let after = if (token.paragraph_normalized == true) tokens else explicit_par_gap(tokens,sources,index)^;
            let next = if (token.direct != true and token.output_end != null) generated_par_next(after,index + 1)^ else index + 1;
            blocks(after, sources, next, nodes, environment)
        }
        else if (env_gap == null) unsupported(index, "environment command source gap")
        else if (env_gap != "") {
            if ((trim(env_gap) ^ { null }) != "") unsupported(index, "environment command source gap")
            else {
                let rest = restore_control_gap(tokens, sources, index)^;
                let para = inline(rest, sources, index + 1, "", [], true, table_columns_environment(environment))^;
                if (para.issue != null) para
                else blocks(para.tokens, sources, para.index, [*nodes, <paragraph *para.items>], environment)
            }
        }
        else if (control(token, "end")) {
            let arg = read_group(tokens, index + 1)^;
            if (arg.issue != null) arg
            else if (environment != null and plain_group(arg) and group_text(arg) == environment and
                ((token.direct == true and arg.direct == true) or reconstructed_environment(tokens, index, arg)^))
                {index: arg.end, nodes: nodes, tokens: tokens, issue: null}
            else unsupported(index, "environment end")
        }
        else if (control(token, "begin")) {
            let env = parse_environment(tokens, sources, index, environment)^;
            if (env.issue != null) env
            else blocks(env.tokens, sources, env.index, [*nodes, env.node], environment)
        }
        else if (token.kind == "control" and
            contains(["part", "chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"], token.value)) {
            let starred = index + 1 < len(tokens) and tokens[index + 1].kind == "char" and tokens[index + 1].value == "*";
            let opt = read_optional(tokens, index + if (starred) 2 else 1)^;
            let arg = if (opt.issue != null) opt else read_group(tokens, opt.end)^;
            if (opt.issue != null) opt
            else if (arg.issue != null) arg
            else {
                let children = inline_group(arg, sources)^;
                if (children.issue != null) children
                else if (children.index != len(children.tokens)) unsupported(index, "heading title")
                else {
                    let title = <curly_group *children.items>;
                    let toc = if (opt.present) <brack_group group_text(opt)> else null;
                    let attrs = {source_offset: token.start, *:if (starred) {starred: true} else {},
                        *:if (toc != null) {toc: toc} else {}, title: title};
                    blocks(tokens, sources, arg.end, [*nodes, heading(token.value, attrs)], environment)
                }
            }
        }
        else if (token.kind == "control" and (token.value == "maketitle" or token.value == "tableofcontents")) {
            if (index + 1 < len(tokens) and (opening(tokens[index + 1]) or
                (tokens[index + 1].kind == "char" and tokens[index + 1].value == "[")))
                unsupported(index, "declaration arguments")
            else {
                let rest = restore_control_gap(tokens, sources, index)^;
                blocks(rest, sources, index + 1, [*nodes, declaration(token.value, token.start, [], [])], environment)
            }
        }
        else if (declaration_command(token)^) {
            // declarations follow the same adjacent-group rules as generic commands.
            let end = if (token.echo_span != null) index + token.echo_count else index + 1;
            let restored = restore_declaration_gaps(tokens, sources, index, end, slice(tokens, 0, index))^;
            if (restored.issue != null) restored
            else {
                let next = if (index + 1 < len(restored.tokens)) restored.tokens[index + 1] else null;
                let star = next != null and next.kind == "char" and next.value == "*";
                let needs_mapped = token.direct != true or expanded_argument_sources(restored.tokens,index + 1,token.file,sources)^;
                let generated = needs_mapped and token.output_start != null and token.output_end != null and
                    token.output_segments != null and reconstructed_token(token)^;
                let mapped = star and (if (generated) reconstructed_literal(next)^ else
                    token.direct == true and next.direct == true and token.file == next.file);
                let starred = mapped and (if (generated) token.output_end == next.output_start else token.end == next.start);
                let at = index + if (starred) 2 else 1;
                let after = if (starred) next.end else token.end;
                let anchor = if (starred) next else token;
                let args = if (needs_mapped and not generated) unsupported(index, "declaration source mapping")
                    else if (star and not mapped) unsupported(index, "declaration star source mapping")
                    else generic_arguments(restored.tokens, sources, at, after, token.file, [], [], true, true, true,
                        anchor.output_end, generated)^;
                if (args.issue != null) args
                else {
                    let offset = if (generated) rebuild.map_offset(token.output_segments,token.output_start)^ else token.start;
                    let node = declaration(token.value, offset, args.groups, pack_arguments(args.items)^, starred);
                    let rest = restore_control_gap(restored.tokens, sources, args.index - 1)^;
                    blocks(rest, sources, args.index, [*nodes, node], environment)
                }
            }
        }
        else {
            let para = inline(tokens, sources, index, "", [], true, table_columns_environment(environment))^;
            if (para.issue != null) para
            // A split source span can emit nodes while leaving its replacement at the same index.
            else if (para.index == index and len(para.items) == 0) unsupported(index, "empty paragraph")
            else blocks(para.tokens, sources, para.index, [*nodes, <paragraph *para.items>], environment)
        }
    }
}

// Syntax inside a comment is discarded; live syntax needs the full trailing parser.
fn plain_trailing_text(text) any^ => not any([for (event in paragraph_events(text)^)
    if (event.kind != "char") false else any([for (char in split(event.value, "")^) contains(["\\", "{", "}", "$"], char)])])
fn trailing_event_nodes(events, index = 0, text = "", started = false, nodes = []) any^ {
    if (index >= len(events)) [*nodes, *(if (started) [<paragraph text>] else [])]
    else {
        let event = events[index];
        if (control(event,"par")) trailing_event_nodes(events,index + 1,"",false,
            [*nodes, *(if (started) [<paragraph text>] else [])])
        else if (event.kind == "paragraph-comment") trailing_event_nodes(events,index + 1,text,true,nodes)
        else trailing_event_nodes(events,index + 1,text ++ event.value,true,nodes)
    }
}
fn trailing_nodes(text) any^ => trailing_event_nodes(paragraph_events(text)^)

// Reconstruct emitted positions before parsing standalone math: macro output
// can join tokens from different source spans into one native math atom.
pub fn math(expanded: map) Digested^ {
    let built = rebuild.reconstruct(expanded, true)^;
    if (built.issue != null) {ast:null, issue:built.issue}
    else {
        let tokens = mark_output_tokens(expanded.output, built, expanded.constructor_argument_mode)^;
        let atoms = math_list(tokens)^;
        if (atoms.issue != null) {ast:null, issue:atoms.issue, unsupported:atoms.unsupported}
        else {
            let content = math_content(atoms.items)^;
            let diagnostic = if (len(expanded.diagnostic_records) > 0) expanded.diagnostic_records[0].message else null;
            {ast:if (diagnostic == null) <math *content> else <math error:diagnostic, *content>, issue:null}
        }
    }
}

pub fn digest(expanded: map) Digested^ {
    if (expanded.issue != null) {ast: null, issue: expanded.issue, unsupported: expanded.unsupported}
    // non-text source after the document needs full source-aware digestion.
    else if (expanded.trailing_source != null and not plain_trailing_text(expanded.trailing_source.text)^)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "post-document source mapping"}
    // macro-expanded assignments can join following groups to echoed definitions.
    else if (expanded.prefixed_macro_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "prefixed macro source mapping"}
    // consumed paragraph data can change native argument and math source mapping.
    else if (expanded.paragraph_data_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "paragraph assignment source mapping"}
    else if (expanded.paragraph_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "paragraph quantity definition source mapping"}
    else if (expanded.font_name_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font name source mapping"}
    // Legacy argument expansion consumes font commands that native constructors retain.
    else if (expanded.font_token_seen == true and not expanded.constructor_argument_mode)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font token source mapping"}
    else if (expanded.mathchardef_emission_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "math character source mapping"}
    // Expanded tail results still need the native emission-origin mapping.
    else if (expanded.expl3_tl_tail_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 token tail source mapping"}
    else if (expanded.expl3_exp_args_once_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 one-step argument source mapping"}
    else if (expanded.expl3_exp_args_f_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 f argument source mapping"}
    else if (expanded.expl3_exp_args_value_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 value argument source mapping"}
    else if (expanded.expl3_int_math_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 integer source mapping"}
    else if (any([for (token in expanded.output) token.generated_text_mapping == true]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "generated text source mapping"}
    else if (expanded.inspection_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "inspection source mapping"}
    else if (expanded.numeric_math_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "numeric math source mapping"}
    else if (expanded.shipout_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "shipout source mapping"}
    else if (expanded.box_movement_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "box movement source mapping"}
    else if (expanded.pdf_random_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "random or timer source mapping"}
    else if (expanded.raw_inline_other_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "raw inline source mapping"}
    else if (expanded.input_stream_state_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "input stream command source mapping"}
    else if (expanded.input_eof_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "input EOF conditional source mapping"}
    else if (expanded.expl3_dynamic_read_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 dynamic read source mapping"}
    else if (expanded.expl3_dynamic_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 dynamic assignment source mapping"}
    else if (expanded.expl3_cs_eq_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 control-sequence equality source mapping"}
    else if (expanded.expl3_seq_in_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 sequence membership source mapping"}
    else if (expanded.expl3_seq_body_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 sequence macro source mapping"}
    // Constructor arguments retain the native assignment tokens needed for mapping.
    else if (expanded.expl3_tl_put_seen == true and not expanded.constructor_argument_mode)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "expl3 token-list assignment source mapping"}
    else if (expanded.page_state_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "page state assignment source mapping"}
    else if (expanded.page_state_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "page state definition source mapping"}
    else if (expanded.font_alias_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font alias assignment source mapping"}
    else if (expanded.font_family_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font family assignment source mapping"}
    else if (expanded.font_character_assignment_seen == true)
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font character assignment source mapping"}
    else if (expanded.font_character_quantity_seen == true and any([for (token in expanded.output) token.kind == "source-span"]))
        {ast: null, issue: "latex-digest-unsupported", unsupported: "font character definition source mapping"}
    else {
        let mapped = if (needs_output_mapping(expanded.output,expanded.sources,expanded.main_file,expanded.constructor_argument_mode)^) rebuild.reconstruct(expanded,true)^ else null;
        let tokens = if (mapped == null or mapped.issue != null) expanded.output else mark_output_tokens(expanded.output,mapped,expanded.constructor_argument_mode)^;
        let parsed = blocks(tokens, expanded.sources)^;
        if (parsed.issue != null) {ast: null, issue: parsed.issue, unsupported: parsed.unsupported}
        else {
            let loaded = if (expanded.packages == null) [] else
                [for (package in expanded.packages where package.kind == "local") package.name];
            let count = if (expanded.diagnostics == null) 0 else len(expanded.diagnostics);
            let records = if (expanded.diagnostic_records == null) [] else expanded.diagnostic_records;
            let complete_report = len(records) == count;
            let tail = if (expanded.trailing_source == null) [] else trailing_nodes(expanded.trailing_source.text)^;
            let nodes = [*parsed.nodes, *tail];
            let root = if (complete_report and (count > 0 or len(loaded) > 0))
                <latex_document tex_diagnostics: records, tex_packages: loaded, *nodes>
                else <latex_document *nodes>;
            {ast: root, issue: null, complete: false}
        }
    }
}
