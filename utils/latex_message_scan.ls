// Source scanning for the generated expl3 message catalog.
import reader: lambda.doc.latex.tex_engine.reader
import expl3: lambda.doc.latex.tex_engine.expl3
import stream: lambda.doc.latex.tex_engine.stream

let job=input("temp/latex-message-catalog/scan.json","json")^;
let codes=expl3.syntax_codes({'{':1,'}':2,'#':6,[chr(0)]:13});

fn find_command(current, offset, expected) any^ {
 let step=reader.next(current);
 if(step==null or step.token.start>offset) false
 else if(step.token.start==offset) step.token.kind=="control" and step.token.value==expected
 else find_command(step.reader,offset,expected)
}
fn read_list(current, output = "") any^ {
 let step=reader.next(current);
 if(step==null) output
 else if(step.token.kind!="char") raise error("non-character catalog module name")
 else read_list(step.reader,output ++ step.token.value)
}
fn source_reader(source) map => {*:reader.reader(source,-1),endline:-1,catcodes:codes}
fn arguments(current, count) any^ {
 if(count==0) {
  let active=stream.active_reader(current);
  if(active==null) raise error("missing catalog macro argument") else active.line_start + active.position
 }
 else {
  let arg=stream.argument(current,true)^;
  if(arg.error!=null) raise error("invalid catalog macro argument") else arguments(arg.stream,count-1)
 }
}
fn controls_only(current) any^ {
 let step=reader.next(current);
 if(step==null) true
 else if(step.token.kind!="control") false
 else controls_only(step.reader)
}
fn group_depth(current, depth = 0) any^ {
 let step=reader.next(current);
 if(step==null) depth
 else if(step.token.kind=="char" and step.token.cat==1) group_depth(step.reader,depth+1)
 else if(step.token.kind=="char" and step.token.cat==2) group_depth(step.reader,depth-1)
 else group_depth(step.reader,depth)
}
fn opening(source, offset) any^ {
 let tail=slice(source,offset,len(source))^;
 let step=reader.next(source_reader(tail));
 if(step==null or step.token.kind!="char" or step.token.cat!=1) null
 else offset + step.reader.line_start + step.reader.position - 1
}

let result=if(job.mode=="commands")
 [for(item in job.items) find_command(source_reader(item.line),item.offset,item.name)^]
else if(job.mode=="groups") {
 let source=input("lmd/package/doc/latex/tex/expl3-code.tex","text")^;
 [for(start in job.items) reader.raw_group_end(source,start+1,"{","}")]
}
else if(job.mode=="openings") {
 let source=input("lmd/package/doc/latex/tex/expl3-code.tex","text")^;
 [for(offset in job.items) opening(source,offset)^]
}
else if(job.mode=="lists")
 [for(source in job.items) read_list(source_reader(source))^]
else if(job.mode=="arguments")
 [for(item in job.items) arguments(stream.from_reader(source_reader(item.text)),item.count)^]
else if(job.mode=="prefixes")
 [for(source in job.items) controls_only(source_reader(source))^]
else if(job.mode=="depths")
 [for(source in job.items) group_depth(source_reader(source))^]
else raise error("unknown catalog source scan");
format(result,'json')^
