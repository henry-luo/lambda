import reader: lambda.doc.latex.tex_engine.reader
import stream: lambda.doc.latex.tex_engine.stream
let source={*:reader.reader("A B", 7),endline:-1};
let start=stream.from_reader(source);
let a=stream.next(start)^;
let space=stream.next(a.stream)^;
let b=stream.next(space.stream)^;
let replay=stream.next(stream.push(b.stream,[a.token]))^;
let foreign={*:reader.reader("Z",8),endline:-1};
let f=stream.next(stream.push_reader(replay.stream,foreign))^;
[{event:"initial",offset:start.last_main_offset},
 {event:"main a",offset:a.stream.last_main_offset},
 {event:"main space",offset:space.stream.last_main_offset},
 {event:"main b",offset:b.stream.last_main_offset},
 {event:"pushed back a",offset:replay.stream.last_main_offset},
 {event:"foreign z",offset:f.stream.last_main_offset}]
