// test_math_box.ls — Test math box model
// Coverage: box.ls — text_box, skip_box, null_delim, make_box, hbox, with_class, with_color, with_scale

import box: lambda.package.math.box

"===== MATH BOX TESTS ====="

// ---- text_box ----
let tb = box.text_box("x", "ML__mathit", "mord")
"1. text_box type:"; tb.type
"2. text_box height:"; tb.height
"3. text_box depth:"; tb.depth
"4. text_box italic:"; tb.italic
"5. text_box el tag:"; name(tb.element)
"6. text_box el class:"; tb.element.class
"7. text_box el text:"; tb.element[0]

// ---- text_box multi-char ----
let tb2 = box.text_box("abc", "ML__cmr", "mord")
"8. text_box 3ch width:"; tb2.width
"9. text_box 3ch class:"; tb2.element.class

// ---- text_box no class ----
let tb3 = box.text_box("y", null, "mord")
"10. text_box null class tag:"; name(tb3.element)

// ---- skip_box ----
let sb = box.skip_box(0.5)
"11. skip type:"; sb.type
"12. skip width:"; sb.width
"13. skip height:"; sb.height
"14. skip has style:"; contains(sb.element.style, "width:")

// ---- null_delim ----
let nd = box.null_delim()
"15. null_delim type:"; nd.type
"16. null_delim width:"; nd.width
"17. null_delim class:"; nd.element.class

// ---- make_box ----
let mb = box.make_box(<span; "test">, 0.8, 0.2, 1.0, "mord")
"18. make type:"; mb.type
"19. make height:"; mb.height
"20. make depth:"; mb.depth
"21. make width:"; mb.width

// ---- hbox ----
let h = box.hbox([tb, sb])
"22. hbox tag:"; name(h.element)
"23. hbox has width:"; h.width != 0.0
"24. hbox height:"; h.height

// ---- with_class ----
let wc = box.with_class(tb, "extra")
"25. with_class tag:"; name(wc.element)
"26. with_class has class:"; wc.element.class

// ---- with_color ----
let wcol = box.with_color(tb, "#ff0000")
"27. with_color tag:"; name(wcol.element)
"28. with_color style:"; contains(wcol.element.style, "color:")

// ---- with_scale ----
let ws = box.with_scale(tb, 0.7)
"29. with_scale tag:"; name(ws.element)
"30. with_scale has font-size:"; contains(ws.element.style, "font-size:")

// ---- with_scale 1.0 (no wrapping) ----
let ws1 = box.with_scale(tb, 1.0)
"31. with_scale 1.0 same:"; name(ws1.element)

"===== ALL BOX TESTS DONE ====="
