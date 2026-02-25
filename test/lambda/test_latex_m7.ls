// test_latex_m7.ls — Milestone 7: LaTeXML Features
// Tests: \newtheorem, booktabs, \newcommand optional args, \autoref, \nameref

import latex: .lambda.package.latex.latex
import macros: .lambda.package.latex.macros

"===== MILESTONE 7 TESTS ====="

// ---- Macro optional-arg definition parsing ----
// Construct a \newcommand element: \newcommand{\greeting}[1][World]{Hello, #1!}
let nc1 = <newcommand <curly_group "\\greeting"> <brack_group "1"> <brack_group "World"> <curly_group "Hello, #1!">>
let defs1 = macros.get_defs(<document nc1>)
"1. macro def count:"; len(defs1)
"2. macro name:"; defs1[0].name
"3. macro params:"; defs1[0].params
"4. macro default_arg:"; defs1[0].default_arg

// A simpler macro without optional arg
let nc2 = <newcommand <curly_group "\\simple"> <brack_group "1"> <curly_group "Hello #1">>
let defs2 = macros.get_defs(<document nc2>)
"5. simple def count:"; len(defs2)
"6. simple default_arg null:"; defs2[0].default_arg == null

// ---- E2E rendering ----
let html = latex.render_file_to_html("test/input/test_latex_m7.tex")

// ---- newtheorem checks ----
"7. has theorem class:"; contains(html, "latex-theorem")
"8. has definition class:"; contains(html, "latex-definition")
"9. has theorem head:"; contains(html, "latex-theorem-head")
"10. has Theorem 1:"; contains(html, "Theorem 1.")
"11. has Theorem 3 (shared counter):"; contains(html, "Theorem 3.")
"12. has Definition 1:"; contains(html, "Definition 1.")
"13. has Lemma 2 (shared):"; contains(html, "Lemma 2.")
"13b. Remark unnumbered:"; contains(html, "Remark.") and contains(html, "Remark 1") == false

// ---- booktabs checks ----
"14. has toprule:"; contains(html, "latex-toprule")
"15. has midrule:"; contains(html, "latex-midrule")
"16. has bottomrule:"; contains(html, "latex-bottomrule")

// ---- autoref checks ----
"17. has Section autoref:"; contains(html, "Section")
"18. has Theorem autoref:"; contains(html, "Theorem")
"19. has Definition autoref:"; contains(html, "Definition")
"20. has Table autoref:"; contains(html, "Table")

// ---- nameref checks ----
"21. has nameref link:"; contains(html, "Introduction")

// ---- newcommand optional arg expansion ----
"22. default expansion:"; contains(html, "Hello, World!")
"23. override expansion:"; contains(html, "Hello, Alice!")
"24. pair with default:"; contains(html, "(default, second)")
"25. pair with override:"; contains(html, "(first, second)")
