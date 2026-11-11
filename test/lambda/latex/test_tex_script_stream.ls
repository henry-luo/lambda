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
