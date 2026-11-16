import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let source=join([for(i in 1 to 80000) "\\relax"], "\n");
let r=expansion.expand(stream.from_reader(reader.reader(source)),1000000)^;
let native=parse(source,{type:"tex"})^;
{finished:r.finished,issue:r.issue,expansions:r.expansions,output:len(r.output),text:reconstruct.reconstruct(r)^.text==native.text};
let limited=expansion.expand(stream.from_reader({*:reader.reader("\\message{before}\\def\\x{\\x}\\x"),catcodes:{'{':1,'}':2}}),3)^;
{finished:limited.finished,issue:limited.issue,expansions:limited.expansions,messages:limited.messages}
