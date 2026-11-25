// D10.2.2v2: repeated math parses retain fresh definitions and resource values.
import facade: lambda.doc.latex.tex_engine.facade
let options = {type:"math", flavor:"latex"};
let first = parse("\\def\\sample{x}\\sample", options)^;
let plain = parse("x", options)^;
let later = parse("\\sample", options)^;
let independent = facade.math("\\sample")^;
let local_x = {type:"math", resources:{local:{'part.tex':{data:"x"}},bundled:{}}};
let local_y = {type:"math", resources:{local:{'part.tex':{data:"y"}},bundled:{}}};
let x = parse("\\input{part}", local_x)^;
let y = parse("\\input{part}", local_y)^;
[
    first == plain,
    independent.issue == null and later == independent.root,
    x == plain,
    y == parse("y", options)^,
    parse("\\input{part}", local_x)^ == x
]
