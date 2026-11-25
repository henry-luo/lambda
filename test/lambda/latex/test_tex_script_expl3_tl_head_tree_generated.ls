import compare: ._tex_script_tl_head_compare
let bodies=["\\textbf{\\tl_head:n{abc}}","\\section{\\tl_head:n{{ab}c}}X",
 "\\let\\h=\\tl_head:n\\h{abc}","\\def\\x{XY}\\tl_head:n{\\x Z}",
 "\\edef\\y{\\tl_head:n{abc}}\\y","$x+\\tl_head:n{éβ}$"];
let checked=[for(body in bodies,constructor in [false,true]) compare.check(body,constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text or not x.diagnostics or x.ast!=true) x]}
