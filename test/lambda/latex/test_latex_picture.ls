// test_latex_picture.ls — Test picture environment rendering
// Tests Phase 2G: \begin{picture}(w,h)...\end{picture} → SVG, parsed by the
// direct parser, which keeps each command's arguments on the command itself

import picture: lambda.doc.latex.elements.picture
import util: lambda.doc.latex.util

fn pic(body) {
    let ast = parse("\\begin{picture}" ++ body ++ "\\end{picture}", "latex") ^ { null }
    util.find_descendant(ast, "picture")
}

"===== PICTURE ENVIRONMENT TESTS ====="

// ---- parse_coord ----
"1. parse_coord basic:"
picture.parse_coord("(100,50)")

"2. parse_coord null:"
picture.parse_coord(null)

"3. parse_coord no parens:"
picture.parse_coord("hello")

// ---- circle ----
let pic_circle = pic("(100,50)\\put(50,25){\\circle{30}}")
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
let pic_line = pic("(100,50)\\put(10,10){\\line(1,0){60}}")
let svg_line = format(picture.render_picture(pic_line), 'xml')
"8. line has x1=100:"
contains(svg_line, "x1=\"100\"")
"9. line has x2=700:"
contains(svg_line, "x2=\"700\"")
"10. line y values equal (horizontal):"
contains(svg_line, "y1=\"400\"") and contains(svg_line, "y2=\"400\"")

// ---- vector (arrow) ----
let pic_vec = pic("(100,50)\\put(10,10){\\vector(1,1){30}}")
let svg_vec = format(picture.render_picture(pic_vec), 'xml')
"11. vector has arrowhead polygon:"
contains(svg_vec, "<polygon")
"12. vector has line element:"
contains(svg_vec, "<line")

// ---- oval ----
let pic_oval = pic("(100,50)\\put(50,25){\\oval(40,20)}")
let svg_oval = format(picture.render_picture(pic_oval), 'xml')
"13. oval is ellipse:"
contains(svg_oval, "<ellipse")
"14. oval rx=200:"
contains(svg_oval, "rx=\"200\"")
"15. oval ry=100:"
contains(svg_oval, "ry=\"100\"")

// ---- text ----
let pic_text = pic("(100,50)\\put(20,30){Hello}")
let svg_text = format(picture.render_picture(pic_text), 'xml')
"16. text has content:"
contains(svg_text, ">Hello</text>")
"17. text x=200:"
contains(svg_text, "x=\"200\"")

// ---- qbezier ----
let pic_qb = pic("(100,50)\\qbezier(0,0)(50,50)(100,0)")
let svg_qb = format(picture.render_picture(pic_qb), 'xml')
"18. qbezier is path:"
contains(svg_qb, "<path")
"19. qbezier has Q command:"
contains(svg_qb, " Q ")

// ---- multiput ----
let pic_mp = pic("(100,50)\\multiput(10,10)(20,0){3}{\\circle{5}}")
let svg_mp = format(picture.render_picture(pic_mp), 'xml')
"20. multiput has 3 circles:"
len(split(svg_mp, "<circle")) - 1

// ---- empty picture ----
let pic_empty = pic("")
let svg_empty = picture.render_picture(pic_empty)
"21. empty picture tag:"
name(svg_empty)
