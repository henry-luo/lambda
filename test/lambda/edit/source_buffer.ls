// lambda.edit source buffer (vibe/radiant/Radiant_Design_Source_Editor.md §5,
// CED12): exact text round trips, chunked line access, deltas and their
// inverses, and chunk rebuilds across boundaries.
import buf: lambda.edit.source_buffer

fn round_trips(text) => buf.to_text(buf.from_text(text)) == text

"round trip:";
[round_trips(""), round_trips("a"), round_trips("a\n"), round_trips("a\nb"), round_trips("a\r\nb\r\n"), round_trips("a\r\nb\nc"),
 round_trips("\n\n"), round_trips("xé中\n")]

"line endings:";
let crlf = buf.from_text("one\r\ntwo\r\n");
[crlf.eol == "\r\n", crlf.final_newline, buf.lines(crlf, 0, 5)]

// 1000 numbered lines span four chunks of 250
fn numbered(n) => join([for (i in 0 to n - 1) "line " ++ string(i)], "\n") ++ "\n"
let big = buf.from_text(numbered(1000))

"chunks:";
[big.count, len(big.chunks), big.starts, buf.line(big, 0), buf.line(big, 249),
 buf.line(big, 250), buf.line(big, 999), buf.chunk_of(big, 750)]

"text_between:";
[buf.text_between(big, buf.loc(1, 5), buf.loc(1, 6)),
 buf.text_between(big, buf.loc(2, 2), buf.loc(0, 4)),
 buf.text_between(big, buf.loc(249, 5), buf.loc(251, 4))]

// insert one character, then undo through the inverse
"insert:";
let ins = buf.apply_delta(big, buf.delta(buf.loc(10, 4), buf.loc(10, 4), ["!"]));
[buf.line(ins.buf, 10), ins.buf.version, ins.inverse, ins.chunk, ins.removed, ins.added]
let back = buf.apply_delta(ins.buf, ins.inverse);
[buf.to_text(back.buf) == numbered(1000), back.buf.starts == big.starts]

// a newline inside a line splits it and shifts every later chunk
"split:";
let split_line = buf.apply_delta(big, buf.delta(buf.loc(260, 2), buf.loc(260, 2), ["", ""]));
[split_line.buf.count, buf.line(split_line.buf, 260), buf.line(split_line.buf, 261),
 buf.line(split_line.buf, 1000), split_line.buf.starts]

// a deletion across a chunk boundary joins two lines
"join across chunks:";
let joined = buf.apply_delta(big, buf.delta(buf.loc(249, 4), buf.loc(250, 4), [""]));
[joined.buf.count, buf.line(joined.buf, 249), buf.line(joined.buf, 250), joined.buf.starts,
 joined.inverse]
let unjoined = buf.apply_delta(joined.buf, joined.inverse);
[buf.to_text(unjoined.buf) == numbered(1000)]

// multi-line paste in the middle, then undo
"paste:";
let pasted = buf.apply_delta(big, buf.delta(buf.loc(500, 0), buf.loc(500, 0), ["a", "b", "c"]));
[pasted.buf.count, buf.lines(pasted.buf, 500, 4), pasted.inverse];
[buf.to_text(buf.apply_delta(pasted.buf, pasted.inverse).buf) == numbered(1000)]

// deleting almost everything leaves one line
"delete all:";
let gone = buf.apply_delta(big, buf.delta(buf.loc(0, 0), buf.doc_end(big), [""]));
[gone.buf.count, len(gone.buf.chunks), buf.to_text(gone.buf), gone.buf.starts];
[buf.to_text(buf.apply_delta(gone.buf, gone.inverse).buf) == numbered(1000)]

// a small region merges with the next chunk
"merge:";
let small = buf.apply_delta(big, buf.delta(buf.loc(0, 0), buf.loc(240, 0), [""]));
[small.buf.count, small.buf.starts, small.removed, small.added]

"clamp:";
[buf.clamp(big, buf.loc(-3, 9)), buf.clamp(big, buf.loc(5, 99)), buf.clamp(big, buf.loc(5000, 1))]
