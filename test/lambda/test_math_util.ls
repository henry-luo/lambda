// test_math_util.ls — Test math package utility functions
// Coverage: util.ls — fmt_num, fmt_em, fmt_pct, clamp, str_repeat, str_join, starts_with, attr_or, text_of

import util: .lambda.package.math.util

"===== MATH UTIL TESTS ====="

// ---- fmt_num ----
"1. fmt_num 3.14159 2:"; util.fmt_num(3.14159, 2)
"2. fmt_num 0.5 1:"; util.fmt_num(0.5, 1)
"3. fmt_num 1.0 0:"; util.fmt_num(1.0, 0)
"4. fmt_num 0.16667 5:"; util.fmt_num(0.16667, 5)

// ---- fmt_em ----
"5. fmt_em 0.5:"; util.fmt_em(0.5)
"6. fmt_em 1.0:"; util.fmt_em(1.0)
"7. fmt_em 0.16667:"; util.fmt_em(0.16667)

// ---- fmt_pct ----
"8. fmt_pct 0.7:"; util.fmt_pct(0.7)
"9. fmt_pct 1.0:"; util.fmt_pct(1.0)
"10. fmt_pct 0.5:"; util.fmt_pct(0.5)

// ---- clamp ----
"11. clamp 5 0 10:"; util.clamp(5, 0, 10)
"12. clamp -1 0 10:"; util.clamp(-1, 0, 10)
"13. clamp 15 0 10:"; util.clamp(15, 0, 10)
"14. clamp 0 0 0:"; util.clamp(0, 0, 0)

// ---- str_repeat ----
"15. repeat a 3:"; util.str_repeat("a", 3)
"16. repeat xy 2:"; util.str_repeat("xy", 2)
"17. repeat a 0:"; util.str_repeat("a", 0)
"18. repeat a 1:"; util.str_repeat("a", 1)

// ---- str_join ----
"19. join abc comma:"; util.str_join(["a", "b", "c"], ",")
"20. join one:"; util.str_join(["one"], ",")
"21. join empty:"; util.str_join([], ",")
"22. join space:"; util.str_join(["x", "y", "z"], " ")

// ---- starts_with ----
"23. starts hello he:"; util.starts_with("hello", "he")
"24. starts hello world:"; util.starts_with("hello", "world")
"25. starts empty:"; util.starts_with("hello", "")
"26. starts short:"; util.starts_with("hi", "hello")

// ---- attr_or ----
let el1 = <span class: "test", style: "color:red">
"27. attr_or has:"; util.attr_or(el1, "class", "default")
"28. attr_or miss:"; util.attr_or(el1, "id", "default")

// ---- text_of ----
"29. text_of string:"; util.text_of("hello")
"30. text_of element:"; util.text_of(<span; "content">)

// ---- constants ----
"31. PT_PER_EM:"; util.PT_PER_EM
"32. SCRIPT_SPACE:"; util.SCRIPT_SPACE

"===== ALL UTIL TESTS DONE ====="
