// Use the same LaTeX source and D10.2.2 transform as the full-document test.
// The wheel replay stops before the remaining sections enter the viewport.
import latex: lambda.doc.latex.latex

let path = "./test/input/math_intensive_test.tex"
let source = input(path, "text")^
let sections = split(source, "\\section{Relations, Logic, and Negation}")^
if (len(sections) != 2) raise error("Could not locate the end of the math scroll excerpt")
else latex.render_document(latex.parse_source(sections[0] ++ "\n\\end{document}\n", path), {source_path:path})^
