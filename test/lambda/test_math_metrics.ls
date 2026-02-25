// test_math_metrics.ls — Test math font metrics module
// Coverage: metrics.ls — constants, style_index, style_scale, get_metric, at, sigma parameters

import met: .lambda.package.math.metrics

"===== MATH METRICS TESTS ====="

// ---- constants ----
"1. AXIS_HEIGHT:"; met.AXIS_HEIGHT
"2. X_HEIGHT:"; met.X_HEIGHT
"3. PT_PER_EM:"; met.PT_PER_EM
"4. BASELINE_SKIP:"; met.BASELINE_SKIP
"5. DEFAULT_CHAR_HEIGHT:"; met.DEFAULT_CHAR_HEIGHT
"6. DEFAULT_CHAR_DEPTH:"; met.DEFAULT_CHAR_DEPTH
"7. DEFAULT_CHAR_WIDTH:"; met.DEFAULT_CHAR_WIDTH

// ---- style_index ----
"8. idx display:"; met.style_index("display")
"9. idx text:"; met.style_index("text")
"10. idx script:"; met.style_index("script")
"11. idx scriptscript:"; met.style_index("scriptscript")
"12. idx unknown:"; met.style_index("xxx")

// ---- style_scale ----
"13. scale display:"; met.style_scale("display")
"14. scale text:"; met.style_scale("text")
"15. scale script:"; met.style_scale("script")
"16. scale scriptscript:"; met.style_scale("scriptscript")

// ---- get_metric ----
"17. num1 display:"; met.get_metric(met.num1, "display")
"18. num1 script:"; met.get_metric(met.num1, "script")
"19. num1 scriptscript:"; met.get_metric(met.num1, "scriptscript")
"20. denom1 display:"; met.get_metric(met.denom1, "display")
"21. sup1 display:"; met.get_metric(met.sup1, "display")
"22. sub1 display:"; met.get_metric(met.sub1, "display")
"23. rule display:"; met.get_metric(met.defaultRuleThickness, "display")
"24. rule script:"; met.get_metric(met.defaultRuleThickness, "script")

// ---- at ----
"25. at 0:"; met.at(met.quad, 0)
"26. at 1:"; met.at(met.quad, 1)
"27. at 2:"; met.at(met.quad, 2)

// ---- FONT_SCALE ----
"28. font scale 5:"; met.FONT_SCALE[5]
"29. font scale 1:"; met.FONT_SCALE[1]
"30. DEFAULT_FONT_SIZE:"; met.DEFAULT_FONT_SIZE

"===== ALL METRICS TESTS DONE ====="
