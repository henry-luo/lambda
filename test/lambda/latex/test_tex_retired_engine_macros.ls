// D10.2.2v2: retain the native engine's macro and expansion-budget cases.
import facade: lambda.doc.latex.tex_engine.facade
import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion

let catcodes = "\\catcode`\\{=1 \\catcode`\\}=2 \\catcode`\\#=6 ";
let examples = [
    {source:"\\def\\a{\\def\\a{B}\\immediate\\write16{C}}\\a\\immediate\\write16{\\a}", messages:["C", "B"]},
    {source:"\\def\\pair#1,#2;{[#2:#1:#2]}\\immediate\\write16{\\pair X,Y;}", messages:["[Y:X:Y]"]},
    {source:"\\def\\duplicate#1{#1#1}\\immediate\\write16{\\duplicate{ab}}", messages:["abab"]},
    {source:"\\def\\identity#1{#1}\\immediate\\write16{L\\identity{}R}", messages:["LR"]},
    {source:"\\def\\old{X}\\edef\\copy{\\old}\\def\\old{Y}\\immediate\\write16{\\copy\\old}", messages:["XY"]}
];
fn check(example) any^ {
    let result=facade.tex(catcodes ++ example.source, {ini:true})^;
    result.issue == null and result.root.messages == example.messages and len(result.root.diagnostics) == 0
}
let limited=expansion.expand(stream.from_reader(reader.reader("\\catcode`\\{=1 \\catcode`\\}=2 \\def\\a{\\a}\\a")),3,{ini:true})^;
[*[for (example in examples) check(example)^], limited.issue == "tex-budget"]
