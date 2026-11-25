// D10.2.2v2: only text emitted to the document needs source mapping.
let consumed = "\\edef\\discard{\\string\\foo}\\begin{document}OK\\end{document}"
let emitted = "\\begin{document}\\string\\foo\\end{document}"
let delayed = "\\edef\\saved{\\string\\foo}\\begin{document}\\saved\\end{document}";
[parse(consumed,"latex") ^ { ^.message }, parse(emitted,"latex") ^ { ^.message },
    parse(delayed,"latex") ^ { ^.message }]
