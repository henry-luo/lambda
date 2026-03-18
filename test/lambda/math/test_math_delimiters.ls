// test_math_delimiters.ls — Test delimiter rendering
// Coverage: atoms/delimiters.ls — render_stretchy, render_at_scale

import delim: lambda.package.math.atoms.delimiters
import box: lambda.package.math.box

"===== MATH DELIMITER TESTS ====="

// ---- null/empty delimiters return null_delim ----
let nd1 = delim.render_stretchy(null, 1.0, "mopen")
"1. null delim type:"; nd1.type
"2. null delim width:"; nd1.width

let nd2 = delim.render_stretchy(".", 1.0, "mopen")
"3. dot delim type:"; nd2.type

let nd3 = delim.render_stretchy("", 2.0, "mclose")
"4. empty delim type:"; nd3.type

// ---- small content → size 1 ----
let s1 = delim.render_stretchy("(", 0.8, "mopen")
"5. small paren type:"; s1.type
"6. small paren has el:"; s1.element is element
"7. small paren height >0:"; (s1.height > 0)

// ---- medium content → size 2 ----
let s2 = delim.render_stretchy("[", 1.5, "mopen")
"8. medium bracket type:"; s2.type
"9. medium bracket height:"; (s2.height > 0)

// ---- large content → size 3-4 ----
let s3 = delim.render_stretchy("{", 2.2, "mopen")
"10. large brace type:"; s3.type

// ---- command delimiters ----
let s4 = delim.render_stretchy("\\langle", 1.0, "mopen")
"11. langle has el:"; s4.element is element
"12. langle type:"; s4.type

let s5 = delim.render_stretchy("\\rangle", 1.0, "mclose")
"13. rangle type:"; s5.type

// ---- brace commands ----
let s6 = delim.render_stretchy("\\{", 1.0, "mopen")
"14. lbrace has el:"; s6.element is element

let s7 = delim.render_stretchy("\\}", 1.0, "mclose")
"15. rbrace has el:"; s7.element is element

// ---- render_at_scale ----
let sc1 = delim.render_at_scale("(", 1.2, "mopen")
"16. scale 1.2 type:"; sc1.type
"17. scale 1.2 height:"; (sc1.height > 0)

let sc2 = delim.render_at_scale(")", 1.8, "mclose")
"18. scale 1.8 type:"; sc2.type

let sc3 = delim.render_at_scale(".", 2.4, "mopen")
"19. scale dot null type:"; sc3.type

// ---- vert/Vert delimiters ----
let v1 = delim.render_stretchy("|", 1.0, "mopen")
"20. vert has el:"; v1.element is element

let v2 = delim.render_stretchy("\\Vert", 1.0, "mclose")
"21. Vert has el:"; v2.element is element

// ---- floor/ceil ----
let fl = delim.render_stretchy("\\lfloor", 1.0, "mopen")
"22. floor has el:"; fl.element is element

let cl = delim.render_stretchy("\\rceil", 1.0, "mclose")
"23. ceil has el:"; cl.element is element

// ---- very large → SVG path ----
let big = delim.render_stretchy("(", 4.0, "mopen")
"24. big paren type:"; big.type
"25. big paren height:"; (big.height > 0)

"===== ALL MATH DELIMITER TESTS DONE ====="
