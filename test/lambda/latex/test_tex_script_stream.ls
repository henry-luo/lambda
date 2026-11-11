import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream

fn compact(tokens) => [for (t in tokens) [t.kind, t.value, t.cat]]
fn source(text) => stream.from_reader({*:reader.reader(text), catcodes: {'{': 1, '}': 2}})
let first = stream.argument(source("  {a{bc}d}Z"));
"group argument:"; compact(first.tokens)
"following token:"; [stream.next(first.stream).token.value]
let second = stream.argument(source("  x y"));
"single argument:"; compact(second.tokens)
let extra = stream.argument(source("}"));
"extra brace:"; [extra.error, stream.next(extra.stream).token.value]
let missing = stream.argument(source("{abc"));
"runaway group:"; [missing.error]
let paragraph = stream.argument(source("\n"));
"short argument paragraph:"; [paragraph.error, stream.next(paragraph.stream).token.value]
let long = stream.argument(source("\n"), true);
"long argument paragraph:"; compact(long.tokens)
let original = source("Z");
let injected = stream.push(original, [{kind: "char", value: "A", cat: 11}]);
let i = stream.next(injected);
"injected input:"; [i.token.value, stream.next(i.stream).token.value]
let direct_input = stream.from_reader({*:reader.reader("\\x AB", 7), endline: -1});
let command = stream.next(direct_input);
let traced = stream.begin_trace(command.stream, command.token, 7)^;
let a_read = stream.next(traced);
let b_read = stream.next(a_read.stream);
let span = stream.end_trace(b_read.stream)^;
"direct source trace:"; [span.valid, span.file, span.start, span.end]
let backed = stream.end_trace(stream.push(b_read.stream, [b_read.token]))^;
"lookahead source trace:"; [backed.valid, backed.start, backed.end, stream.next(backed.stream).token.value]
let foreign = stream.next(stream.push(traced, [{kind: "char", value: "X", cat: 11}]));
"foreign source trace:"; [stream.end_trace(foreign.stream)^.valid]
let old_input = stream.from_reader({*:reader.reader("AB", 7), endline: -1});
let old_first = stream.next(old_input);
let old_second = stream.next(old_first.stream);
let old_trace = stream.begin_trace(old_second.stream, old_second.token, 7)^;
let old_read = stream.next(stream.push(old_trace, [old_first.token]));
"older token source trace:"; [stream.end_trace(old_read.stream)^.valid]
let nested = stream.push_reader(traced, reader.reader("X", 8));
"different active file trace:"; [stream.end_trace(nested)^.valid]
fn read_many(input, count, tokens = []) any^ {
    if (count == 0) {input: input, tokens: tokens}
    else {
        let read = stream.next(input)^;
        read_many(read.stream, count - 1, [*tokens, read.token])
    }
}
let many_input = stream.from_reader({*:reader.reader("\\x " ++ join([for (i in 1 to 70) "a"], ""), 7), endline: -1});
let many_first = stream.next(many_input);
let many_trace = stream.begin_trace(many_first.stream, many_first.token, 7)^;
let many_reads = read_many(many_trace, 70)^;
"long source trace:"; [stream.end_trace(many_reads.input)^.valid]
let many_pending = stream.push(many_reads.input, many_reads.tokens);
"expired lookahead trace:"; [stream.end_trace(many_pending)^.valid]
"source trace sequence boundaries:"; [span.first_seq, span.last_seq, backed.first_seq, backed.last_seq]
