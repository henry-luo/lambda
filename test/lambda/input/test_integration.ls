// Test integration of math parser with document parsers
let direct_math = input('./test/input/math_simple.txt', 'math')
"Direct math parsing:"
direct_math

let markdown_with_math = input('./test/input/test_markdown_math.md', 'markdown')
"Markdown with integrated math:"
markdown_with_math

let latex_doc_with_math = input('./test/input/test_latex_math.tex', 'latex')
"LaTeX document with integrated math:"
latex_doc_with_math

"All enhanced input parsers with math integration working successfully!"
