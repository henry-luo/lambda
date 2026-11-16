import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
import expansion: lambda.doc.latex.tex_engine.expansion
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let source=join([for(i in 1 to 10000) "\\relax"], " ");
let r=expansion.expand(stream.from_reader(reader.reader(source)),1000000)^;
let native=parse(source,{type:"tex"})^;
{finished:r.finished,issue:r.issue,output:len(r.output),text:reconstruct.reconstruct(r)^.text==native.text};
let spaces=join([for(i in 1 to 80000) " "], "");
let long=reader.load_line(reader.reader("A" ++ spaces ++ "\r\nZ"))^;
let after=reader.load_line(long)^;
{first:long.line,first_length:len(long.line),first_next:long.next_line,first_byte:long.next_byte,
 second:after.line,second_start:after.line_start,second_byte:after.line_byte};
let unicode=reader.load_line(reader.reader("é字🙂 A   \r\nZ\t \rQ\n"))^;
let unicode2=reader.load_line(unicode)^;
let unicode3=reader.load_line(unicode2)^;
{lines:[unicode.line,unicode2.line,unicode3.line],starts:[unicode.line_start,unicode2.line_start,unicode3.line_start],
 bytes:[unicode.line_byte,unicode2.line_byte,unicode3.line_byte],next_bytes:[unicode.next_byte,unicode2.next_byte,unicode3.next_byte]}
