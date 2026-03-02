// test_latex_font_decl.ls — Test font/alignment declarations and appendix numbering
// Tests Phase 2D: font declarations, alignment declarations
// Tests Phase 2E: appendix numbering, secnumdepth

import font_decl: lambda.package.latex.elements.font_decl
import dc_article: lambda.package.latex.docclass.article

"===== FONT DECLARATION TESTS ====="

// ---- is_font_decl ----
"1. itshape is font_decl:"; font_decl.is_font_decl("itshape")
"2. bfseries is font_decl:"; font_decl.is_font_decl("bfseries")
"3. ttfamily is font_decl:"; font_decl.is_font_decl("ttfamily")
"4. centering is not font_decl:"; font_decl.is_font_decl("centering")
"5. unknown is not font_decl:"; font_decl.is_font_decl("foobar")

// ---- is_align_decl ----
"6. centering is align_decl:"; font_decl.is_align_decl("centering")
"7. raggedright is align_decl:"; font_decl.is_align_decl("raggedright")
"8. raggedleft is align_decl:"; font_decl.is_align_decl("raggedleft")
"9. itshape is not align_decl:"; font_decl.is_align_decl("itshape")

// ---- font_decl_style ----
"10. itshape style:"; font_decl.font_decl_style("itshape")
"11. bfseries style:"; font_decl.font_decl_style("bfseries")
"12. ttfamily style:"; font_decl.font_decl_style("ttfamily")
"13. rmfamily style:"; font_decl.font_decl_style("rmfamily")
"14. sffamily style:"; font_decl.font_decl_style("sffamily")
"15. scshape style:"; font_decl.font_decl_style("scshape")
"16. slshape style:"; font_decl.font_decl_style("slshape")
"17. upshape style:"; font_decl.font_decl_style("upshape")
"18. mdseries style:"; font_decl.font_decl_style("mdseries")

// ---- align_decl_style ----
"19. centering style:"; font_decl.align_decl_style("centering")
"20. raggedright style:"; font_decl.align_decl_style("raggedright")
"21. raggedleft style:"; font_decl.align_decl_style("raggedleft")

// ---- wrap_font_decl ----
"22. wrap itshape tag:"; name(font_decl.wrap_font_decl("itshape", ["hello"]))
"23. wrap itshape class:"; font_decl.wrap_font_decl("itshape", ["hello"]).class
"24. wrap itshape style:"; font_decl.wrap_font_decl("itshape", ["hello"]).style
"25. wrap bfseries class:"; font_decl.wrap_font_decl("bfseries", ["bold"]).class
"26. wrap bfseries style:"; font_decl.wrap_font_decl("bfseries", ["bold"]).style
"27. wrap ttfamily class:"; font_decl.wrap_font_decl("ttfamily", ["mono"]).class

// ---- wrap_align_decl ----
"28. wrap centering tag:"; name(font_decl.wrap_align_decl("centering", ["centered"]))
"29. wrap centering class:"; font_decl.wrap_align_decl("centering", ["centered"]).class
"30. wrap centering style:"; font_decl.wrap_align_decl("centering", ["centered"]).style
"31. wrap raggedleft class:"; font_decl.wrap_align_decl("raggedleft", ["right"]).class
"32. wrap raggedleft style:"; font_decl.wrap_align_decl("raggedleft", ["right"]).style

"===== APPENDIX NUMBERING TESTS ====="

// ---- article: normal numbering ----
let counters1 = {section: 3, subsection: 2, subsubsection: 1}
"33. normal section:"; dc_article.format_section_number(counters1, "section", false)
"34. normal subsection:"; dc_article.format_section_number(counters1, "subsection", false)
"35. normal subsubsection:"; dc_article.format_section_number(counters1, "subsubsection", false)

// ---- article: appendix numbering ----
let counters2 = {section: 1, subsection: 2, subsubsection: 0}
"36. appendix section A:"; dc_article.format_section_number(counters2, "section", true)
"37. appendix subsection A.2:"; dc_article.format_section_number(counters2, "subsection", true)

let counters3 = {section: 2, subsection: 1, subsubsection: 3}
"38. appendix section B:"; dc_article.format_section_number(counters3, "section", true)
"39. appendix subsubsection B.1.3:"; dc_article.format_section_number(counters3, "subsubsection", true)

// ---- edge: section 26 = Z ----
let counters4 = {section: 26, subsection: 0, subsubsection: 0}
"40. appendix section Z:"; dc_article.format_section_number(counters4, "section", true)

"===== ALL FONT DECL TESTS DONE ====="
