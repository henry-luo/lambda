// D10.2.2v2: the enclosing source group must balance across verbatim spans.
let balanced="\\begin{document}\\fbox{\\verb|\\mbox{|x\\verb|}|}\\end{document}"
let open_src="\\begin{document}\\fbox{\\verb|{|x}\\end{document}"
let close_src="\\begin{document}\\fbox{\\verb|}|x}\\end{document}";
[parse(balanced,"latex") ^ { ^.message },parse(open_src,"latex") ^ { ^.message },parse(close_src,"latex") ^ { ^.message }]
