import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
fn check(source) any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let x = expansion.expand(stream.from_reader(r), 1000)^;
    let native = parse(source, {type: "tex"})^;
    {matches: x.issue == null and join([for (t in x.output) t.value], "") == native.text, issue: x.issue}
}
[for (source in [
    "\\catcode`p=7 \\dimen0=1pt \\the\\dimen0",
    "\\let\\T=\\the \\catcode`t=8 \\dimen0=1pt \\T\\dimen0",
    "\\catcode`p=4 \\dimen0=1pt \\the\\dimen0",
    "\\catcode`y=7 \\count0=2 \\advance\\count0 by 3 \\the\\count0",
    "\\let\\g=\\skip \\catcode`s=8 \\g0=1pt plus2pt \\the\\g0",
    "\\let\\d=\\dimen \\catcode`m=8 \\d0=1cm \\the\\d0"
]) check(source)^]
