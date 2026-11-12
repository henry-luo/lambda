import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
fn check(source, prefix = "") any^ {
    let r = {*:reader.reader(source), endline: -1, catcodes: {'{': 1, '}': 2, '#': 6}};
    let x = expansion.expand(stream.from_reader(r), 1000)^;
    let native = parse(source, {type: "tex"})^;
    {matches: x.issue == null and slice(native.text, 0, len(prefix)) == prefix and join([for (t in x.output) t.value], "") == slice(native.text, len(prefix), len(native.text)), issue: x.issue}
}
[for (entry in [
    ["\\nullfont \\dimen0=1em \\dimen1=1ex \\the\\dimen0,\\the\\dimen1", ""],
    ["\\font\\a=cmr10 at 10pt \\a \\dimen0=1em \\dimen1=.5ex \\the\\dimen0,\\the\\dimen1", ""],
    ["\\font\\a=cmr10 at 10pt \\a \\fontdimen6\\a=-4pt \\dimen0=1.5em \\the\\dimen0", ""],
    ["\\font\\a=cmr10 at 10pt \\a \\fontdimen5\\a=3pt \\dimen0=-.5ex \\the\\dimen0", ""],
    ["\\pdfpxdimen=2pt \\dimen0=1.5px \\the\\dimen0", ""],
    ["\\pdfpxdimen=-3pt \\dimen0=-.5px \\the\\dimen0", ""],
    ["\\font\\a=cmr10 at 10pt \\a \\dimen0=2EM \\dimen1=2Ex \\the\\dimen0,\\the\\dimen1", ""],
    ["\\font\\a=cmr10 at 10pt \\a \\def\\unit{em}\\dimen0=.25\\unit \\the\\dimen0", "\\def\\unit{em}"],
    ["\\mag=2000 \\pdfpxdimen=2pt \\dimen0=1px \\dimen1=1truept \\the\\dimen0,\\the\\dimen1", ""],
    ["\\muskip0=1mu plus2mu minus3mu \\the\\muskip0", ""]]) check(entry[0], entry[1])^]
