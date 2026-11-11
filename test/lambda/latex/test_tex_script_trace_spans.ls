import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
fn report(source) any^ {
 let r={*:reader.reader(source,7),endline:-1,catcodes:{'{':1,'}':2,'#':6}};
 let x=expansion.expand(stream.from_reader(r),100,{echo_definitions:true})^;
 {issue:x.issue,spans:[for(t in x.output where t.kind == "source-span") {value:t.value,start:t.start,end:t.end,first:t.seq,last:t.last_seq}],next:[for(t in x.output where t.kind != "source-span") [t.value,t.seq,t.start]]}
}
report("\\def\\x{A}% comment\nB")
report("\\def\\m{(1,2)}\\m C")
