// D10.2.2v2: the native Input document cases now exercise the runtime parser.
let sources = [
    "\\begin{document}$\\$\\%\\# $ and $x$\\end{document}",
    "\\begin{document}A\\verb|x_%|B\\end{document}",
    "\\begin{document}A\\verb|\\end{document}|B\\end{document}",
    "\\begin{document}A\\verb*|x_%|B\\end{document}",
    "\\begin{document}$\\downarrow[unclosed$\\end{document}",
    "\\begin{document}\\begin{itemize}\\item outer\\begin{itemize}\\item inner\\end{itemize}\\end{itemize}\\begin{enumerate}\\item outer\\begin{enumerate}\\item inner\\end{enumerate}\\end{enumerate}\\end{document}",
    "\\newenvironment{box}[1]{\\begin{center}\\textbf{#1}}{\\end{center}}\\renewenvironment{box}{\\begin{quote}}{\\end{quote}}\\begin{document}Visible text\\end{document}"
];
fn accepted(source) bool => (parse(source,"latex") is element) ^ { false }
let document="\\documentclass{article}\n\\usepackage[utf8]{inputenc}\n\\title{Lambda Test}\n" ++
    "\\author{Test User}\n\\date{January 2025}\n\\begin{document}\n\\maketitle\n" ++
    "\\section{Introduction}This is a test document with \\textbf{bold} and \\textit{italic} text.\n" ++
    "\\subsection{Features}\\begin{itemize}\\item First item\\item Second item\\item Third item\\end{itemize}\n" ++
    "\\section{Math}Here is an equation: $E = mc^2$\\end{document}";
let formatted = format(parse(document,"latex")^,"latex") ^ { "" };
[*[for (source in sources) accepted(source)], len(formatted) > 0]
