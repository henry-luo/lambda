// test_latex_pkg.ls — Test script for the LaTeX package
// Run: ./lambda.exe run lambda/package/latex/test_latex_pkg.ls

import latex: .lambda.package.latex.latex

// Simple LaTeX document for testing
let tex_source = "\\documentclass{article}
\\title{Hello World}
\\author{Test Author}
\\begin{document}
\\maketitle
\\section{Introduction}
This is a \\textbf{bold} and \\textit{italic} test.
Here is some \\texttt{inline code}.

A second paragraph with a footnote\\footnote{This is a note.}.

\\subsection{Lists}
\\begin{itemize}
\\item First item
\\item Second item
\\item Third item
\\end{itemize}

\\begin{enumerate}
\\item Alpha
\\item Beta
\\item Gamma
\\end{enumerate}

\\subsection{Math}
Inline math: $x^2 + y^2 = z^2$

Display math:
\\[ E = mc^2 \\]

\\subsection{Quote}
\\begin{quote}
To be or not to be, that is the question.
\\end{quote}

\\section{Conclusion}
This is the end.
\\end{document}"

let ast^err = input(tex_source, {type: "latex", source: true})

pn main() {
    print("=== LaTeX AST tags ===")
    print(name(ast))
    print(len(ast))

    print("=== Rendering ===")
    let html = latex.render(ast, {standalone: false, numbering: true})
    print(format(html, {type: "html"}))
}
