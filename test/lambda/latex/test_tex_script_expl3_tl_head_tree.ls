import compare: ._tex_script_tl_head_compare
let bodies=["\\tl_head:n{abc}","A\\tl_head:n{{ab}c}B","\\tl_head:n{}X",
 "\\tl_head:n{~abc}","\\tl_head:n{éclair}","\\tl_head:n{{αβ}c}"];
let checked=[for(body in bodies,constructor in [false,true]) compare.check(body,constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text or not x.diagnostics or x.ast!=true) x]}
