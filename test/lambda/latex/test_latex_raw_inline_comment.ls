// D10.2.2v2: a raw percent may not hide an enclosing environment boundary.
let safe="\\begin{document}Inline \\Verb|a_b%c|.\nNext\\end{document}"
let unsafe="\\begin{document}Inline \\Verb|a%b| x\\end{document}";
[parse(safe,"latex") ^ { ^.message },parse(unsafe,"latex") ^ { ^.message }]
