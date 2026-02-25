// test_latex_color.ls — Test color commands and list enhancements
// Tests Phase 2F: \textcolor, \colorbox, \fcolorbox, \definecolor, \color, color resolution
// Tests Phase 2H: nested list CSS classes, custom item labels

import color: .lambda.package.latex.elements.color

"===== COLOR COMMAND TESTS ====="

// ---- resolve_color: named colors ----
"1. red:"; color.resolve_color("red", null)
"2. blue:"; color.resolve_color("blue", null)
"3. green:"; color.resolve_color("green", null)
"4. black:"; color.resolve_color("black", null)
"5. white:"; color.resolve_color("white", null)
"6. cyan:"; color.resolve_color("cyan", null)
"7. magenta:"; color.resolve_color("magenta", null)
"8. yellow:"; color.resolve_color("yellow", null)
"9. orange:"; color.resolve_color("orange", null)
"10. purple:"; color.resolve_color("purple", null)
"11. darkgray:"; color.resolve_color("darkgray", null)
"12. lightgray:"; color.resolve_color("lightgray", null)

// ---- resolve_color: hex passthrough ----
"13. hex passthrough:"; color.resolve_color("#ff0000", null)

// ---- resolve_color: custom colors ----
let my_colors = {"myred": "#cc0000", "myblue": "rgb(0,0,200)"}
"14. custom myred:"; color.resolve_color("myred", my_colors)
"15. custom myblue:"; color.resolve_color("myblue", my_colors)
"16. custom fallback:"; color.resolve_color("red", my_colors)

// ---- resolve_color: unknown passthrough ----
"17. unknown passthrough:"; color.resolve_color("chartreuse", null)

// ---- parse_definecolor ----
"18. HTML model:"; color.parse_definecolor("mycolor", "HTML", "FF8000").css_color
"19. HTML name:"; color.parse_definecolor("mycolor", "HTML", "FF8000").color_name

// ---- render_textcolor ----
// Use curly_group children to prevent string merging in element literals
let tc_el = <textcolor <curly_group "red"> <curly_group "hello">>
let tc_result = color.render_textcolor(tc_el, ["hello"], null)
"20. textcolor tag:"; name(tc_result)
"21. textcolor class:"; tc_result.class
"22. textcolor has color:"; contains(tc_result.style, "color:")

// ---- render_colorbox ----
let cb_el = <colorbox <curly_group "yellow"> <curly_group "boxed">>
let cb_result = color.render_colorbox(cb_el, ["boxed"], null)
"23. colorbox tag:"; name(cb_result)
"24. colorbox class:"; cb_result.class
"25. colorbox has bg:"; contains(cb_result.style, "background-color:")

// ---- render_fcolorbox ----
let fcb_el = <fcolorbox <curly_group "black"> <curly_group "white"> <curly_group "framed">>
let fcb_result = color.render_fcolorbox(fcb_el, ["framed"], null)
"26. fcolorbox tag:"; name(fcb_result)
"27. fcolorbox class:"; fcb_result.class
"28. fcolorbox has border:"; contains(fcb_result.style, "border:")
"29. fcolorbox has bg:"; contains(fcb_result.style, "background-color:")

// ---- is_color_decl ----
"30. color is decl:"; color.is_color_decl("color")
"31. textcolor is not decl:"; color.is_color_decl("textcolor")

// ---- color_decl_style ----
let cd_el = <color "blue">
"32. color decl style:"; color.color_decl_style(cd_el, null)

// ---- wrap_color_decl ----
let wrap_result = color.wrap_color_decl(cd_el, ["hello"], null)
"33. wrap color tag:"; name(wrap_result)
"34. wrap color class:"; wrap_result.class
"35. wrap color has style:"; contains(wrap_result.style, "color:")

// ---- parse_definecolor models ----
"36. rgb model:"; color.parse_definecolor("c1", "rgb", "1.0,0.5,0.0").css_color
"37. RGB model:"; color.parse_definecolor("c2", "RGB", "255,128,0").css_color

"===== ALL COLOR TESTS DONE ====="
