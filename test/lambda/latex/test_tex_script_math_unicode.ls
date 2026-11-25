import compare: ._tex_script_math_unicode_compare
let bodies=["é","éα","é α","xéy","é^2","é_2α","𝛼β","aéβ1","é\\alpha","é% c\nα",
 "\\vcenter{é}","\\frac{é}{α}","x^{éβ}"];
let checked=[for(body in bodies,constructor in [false,true])
 compare.check("\\begin{document}$" ++ body ++ "$\\end{document}",constructor)^];
{cases:len(checked),failures:[for(x in checked where x.issue!=null or not x.text or not x.diagnostics or not x.messages or x.ast!=true) x]}
