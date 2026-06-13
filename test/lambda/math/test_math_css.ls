// test_math_css.ls — Test math CSS class constants and helpers
// Coverage: css.ls — class constants, classes(), font_class(), get_stylesheet(), wrap_standalone()

import css: lambda.package.math.css

"===== MATH CSS TESTS ====="

// ---- class constants ----
"1. LATEX:"; css.LATEX
"2. BASE:"; css.BASE
"3. STRUT:"; css.STRUT
"4. MATHIT:"; css.MATHIT
"5. CMR:"; css.CMR
"6. MATHBF:"; css.MATHBF
"7. MFRAC:"; css.MFRAC
"8. SQRT:"; css.SQRT
"9. MSUBSUP:"; css.MSUBSUP
"10. NULLDELIMITER:"; css.NULLDELIMITER
"11. ERROR:"; css.ERROR
"12. THIN:"; css.THIN
"13. MEDIUM:"; css.MEDIUM
"14. THICK:"; css.THICK
"15. QUAD:"; css.QUAD
"16. QQUAD:"; css.QQUAD

// ---- classes() helper ----
"17. classes 2:"; css.classes(["lm_mathit", "lm_bold"])
"18. classes with null:"; css.classes(["lm_cmr", null, "lm_it"])
"19. classes single:"; css.classes(["lm_text"])
"20. classes empty:"; css.classes([])

// ---- font_class() ----
"21. font mathit:"; css.font_class("mathit")
"22. font cmr:"; css.font_class("cmr")
"23. font mathbf:"; css.font_class("mathbf")
"24. font bb:"; css.font_class("bb")
"25. font cal:"; css.font_class("cal")
"26. font frak:"; css.font_class("frak")
"27. font tt:"; css.font_class("tt")
"28. font text:"; css.font_class("text")
"29. font unknown:"; css.font_class("unknown")

// ---- get_stylesheet() returns a string with lm_latex ----
let ss = css.get_stylesheet()
"30. stylesheet is string:"; ss is string
"31. stylesheet has lm_latex:"; contains(ss, ".lm_latex")
"32. stylesheet has lm_mathit:"; contains(ss, ".lm_mathit")
"33. stylesheet has lm_mfrac:"; contains(ss, ".lm_mfrac")
"34. stylesheet default uses local main font:"; contains(ss, "Computer Modern Serif")
"35. stylesheet default uses local typewriter font:"; contains(ss, "Computer Modern Typewriter")
"36. stylesheet default keeps available size font:"; contains(ss, "font-family:KaTeX_Size1")

let katex_ss = css.get_stylesheet({font_option: "katex"})
"37. stylesheet katex uses KaTeX math:"; contains(katex_ss, ".lm_mathit{font-family:KaTeX_Math")
"38. stylesheet katex uses KaTeX typewriter:"; contains(katex_ss, ".lm_tt{font-family:KaTeX_Typewriter")
"39. stylesheet katex omits local main font:"; not contains(katex_ss, "Computer Modern Serif")

// ---- wrap_standalone() ----
let wrapped = css.wrap_standalone(<span; "x">)
"40. wrap tag:"; name(wrapped)
"41. wrap has style child:"; name(wrapped[0])
"42. wrap has content:"; name(wrapped[1])

"===== ALL CSS TESTS DONE ====="
