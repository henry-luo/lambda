import tex: lambda.doc.latex.tex_engine.reader

fn collect(r, tokens = []) any^ {
    let step = tex.next(r);
    if (step.token == null) tokens else collect(step.reader, [*tokens, step.token])
}
fn compact(tokens) => [for (t in tokens) [t.kind, t.value, t.cat]]

let a = compact(collect(tex.reader("\\alpha  x% comment\n\n\\% y")))
"control words, comments, empty lines:"; a
let r = tex.reader("a!b");
let first = tex.next(r);
let changed = {*:first.reader, catcodes: {'!': 11}};
"dynamic category codes:"; [first.token.cat, tex.next(changed).token.cat]
let supers = {*:tex.reader("^^41^^42"), catcodes: {'^': 7}};
"superscript notation:"; compact(collect(supers))
"control symbols and spaces:"; compact(collect(tex.reader("\\!  A\\  B")))
"CRLF and trimmed spaces:"; compact(collect(tex.reader("a  \r\nb\r\n\r\nc")))
"disabled endline:"; compact(collect({*:tex.reader("a\nb"), endline: -1}))
"active character:"; compact(collect({*:tex.reader("x~y"), catcodes: {'~': 13}}))
"unicode defaults to other:"; compact(collect(tex.reader("é")))
"unicode positions:"; [for (t in ((collect(tex.reader("éA"))) ^ { [] })) [t.start, t.end]]
let word_before = tex.next(tex.reader("\\a!b"));
let rest_after = {*:word_before.reader, catcodes: {'!': 11}};
"changed code after a token:"; [word_before.token.value, tex.next(rest_after).token.cat]
