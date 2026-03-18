// test_latex_css.ls — Test LaTeX CSS stylesheet
// Coverage: css.ls — get_stylesheet, STYLESHEET constant

import css: lambda.package.latex.css

"===== LATEX CSS TESTS ====="

// ---- get_stylesheet returns a string ----
let sheet = css.get_stylesheet()
"1. is string:"; sheet is string
"2. not empty:"; (len(sheet) > 0)

// ---- contains expected CSS rules ----
"3. has body rule:"; contains(sheet, "latex-body")
"4. has h1 rule:"; contains(sheet, "latex-h1")
"5. has h2 rule:"; contains(sheet, "latex-h2")
"6. has itemize:"; contains(sheet, "latex-itemize")
"7. has enumerate:"; contains(sheet, "latex-enumerate")
"8. has quote:"; contains(sheet, "latex-quote")
"9. has verbatim:"; contains(sheet, "latex-verbatim")
"10. has table:"; contains(sheet, "latex-table")
"11. has figure:"; contains(sheet, "latex-figure")
"12. has footnote:"; contains(sheet, "latex-footnote")
"13. has bold:"; contains(sheet, "latex-textbf")
"14. has italic:"; contains(sheet, "latex-textit")
"15. has monospace:"; contains(sheet, "latex-texttt")

// ---- STYLESHEET constant is same as function ----
"16. const equals fn:"; css.STYLESHEET == css.get_stylesheet()

"===== ALL LATEX CSS TESTS DONE ====="
