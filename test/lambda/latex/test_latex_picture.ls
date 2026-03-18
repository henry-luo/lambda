// test_latex_picture.ls — Test picture environment rendering
// Tests Phase 2G: \begin{picture}(w,h)...\end{picture} → SVG

import picture: lambda.package.latex.elements.picture

"===== PICTURE ENVIRONMENT TESTS ====="

// ---- parse_coord ----
"1. parse_coord basic:"
picture.parse_coord("(100,50)")

"2. parse_coord null:"
picture.parse_coord(null)

"3. parse_coord no parens:"
picture.parse_coord("hello")

// ---- circle ----
let pic_circle = <picture; <paragraph; "(100,50)"
    <put> "(50,25)" <curly_group; <circle; "30">>
>>
let svg_circle = format(picture.render_picture(pic_circle), 'xml')
"4. circle has viewBox:"
contains(svg_circle, "viewBox=\"0 0 1000 500\"")
"5. circle has cx=500:"
contains(svg_circle, "cx=\"500\"")
"6. circle has cy=250:"
contains(svg_circle, "cy=\"250\"")
"7. circle has r=150:"
contains(svg_circle, "r=\"150\"")

// ---- line ----
let pic_line = <picture; <paragraph; "(100,50)"
    <put> "(10,10)" <curly_group; <line> "(1,0)" <curly_group; "60">>
>>
let svg_line = format(picture.render_picture(pic_line), 'xml')
"8. line has x1=100:"
contains(svg_line, "x1=\"100\"")
"9. line has x2=700:"
contains(svg_line, "x2=\"700\"")
"10. line y values equal (horizontal):"
contains(svg_line, "y1=\"400\"") and contains(svg_line, "y2=\"400\"")

// ---- vector (arrow) ----
let pic_vec = <picture; <paragraph; "(100,50)"
    <put> "(10,10)" <curly_group; <vector> "(1,1)" <curly_group; "30">>
>>
let svg_vec = format(picture.render_picture(pic_vec), 'xml')
"11. vector has arrowhead marker-end:"
contains(svg_vec, "marker-end:url(#arrowhead)")
"12. vector has arrowhead def:"
contains(svg_vec, "<marker id=\"arrowhead\"")

// ---- oval ----
let pic_oval = <picture; <paragraph; "(100,50)"
    <put> "(50,25)" <curly_group; <oval> "(40,20)">
>>
let svg_oval = format(picture.render_picture(pic_oval), 'xml')
"13. oval is ellipse:"
contains(svg_oval, "<ellipse")
"14. oval rx=200:"
contains(svg_oval, "rx=\"200\"")
"15. oval ry=100:"
contains(svg_oval, "ry=\"100\"")

// ---- text ----
let pic_text = <picture; <paragraph; "(100,50)"
    <put> "(20,30)" <curly_group; "Hello">
>>
let svg_text = format(picture.render_picture(pic_text), 'xml')
"16. text has content:"
contains(svg_text, ">Hello</text>")
"17. text x=200:"
contains(svg_text, "x=\"200\"")

// ---- qbezier ----
let pic_qb = <picture; <paragraph; "(100,50)"
    <qbezier> "(0,0)(50,50)(100,0)">
>
let svg_qb = format(picture.render_picture(pic_qb), 'xml')
"18. qbezier is path:"
contains(svg_qb, "<path")
"19. qbezier has Q command:"
contains(svg_qb, " Q ")

// ---- multiput ----
let pic_mp = <picture; <paragraph; "(100,50)"
    <multiput> "(10,10)(20,0)" <curly_group; "3"> <curly_group; <circle; "5">>
>>
let svg_mp = format(picture.render_picture(pic_mp), 'xml')
"20. multiput has 3 circles:"
len(split(svg_mp, "<circle")) - 1

// ---- empty picture ----
let pic_empty = <picture; <paragraph>>
let svg_empty = picture.render_picture(pic_empty)
"21. empty picture tag:"
name(svg_empty)
