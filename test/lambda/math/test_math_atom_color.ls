// test_math_atom_color.ls — Test math color command rendering
// Coverage: atoms/color.ls — render (textcolor, color, colorbox)

import col: lambda.package.math.atoms.color
import box: lambda.package.math.box
import mctx: lambda.package.math.context

"===== MATH ATOM COLOR TESTS ====="

// mock render function
fn mock_render(node, context) {
    let text = if (node is string) node
        else if (node is element and len(node) > 0) string(node[0])
        else "?"
    box.text_box(text, null, "mord")
}

let base_ctx = mctx.display_context()

// ---- \textcolor with named color ----
let tc_node = {cmd: "\\textcolor", color: "red", content: <group "x">}
let tc = col.render(tc_node, base_ctx, mock_render)
"1. textcolor type:"; tc.type
"2. textcolor has el:"; tc.element is element
"3. textcolor has style:"; tc.element.style != null

// ---- \textcolor with hex color ----
let tc_hex = {cmd: "\\textcolor", color: "#0000ff", content: <group "y">}
let tc2 = col.render(tc_hex, base_ctx, mock_render)
"4. hex color has el:"; tc2.element is element

// ---- \color (same as textcolor for rendering) ----
let c_node = {cmd: "\\color", color: "blue", content: <group "z">}
let c = col.render(c_node, base_ctx, mock_render)
"5. color type:"; c.type

// ---- \colorbox ----
let cb_node = {cmd: "\\colorbox", color: "yellow", content: <group "boxed">}
let cb = col.render(cb_node, base_ctx, mock_render)
"6. colorbox type:"; cb.type
"7. colorbox has bg:"; contains(cb.element.style, "background-color:")
"8. colorbox has padding:"; contains(cb.element.style, "padding:")
"9. colorbox extra width:"; (cb.width > 0)

// ---- null content ----
let nc_node = {cmd: "\\textcolor", color: "green"}
let nc = col.render(nc_node, base_ctx, mock_render)
"10. null content type:"; nc.type

// ---- null color defaults to black ----
let nk_node = {cmd: "\\textcolor", content: <group "a">}
let nk = col.render(nk_node, base_ctx, mock_render)
"11. no color has el:"; nk.element is element

"===== ALL MATH ATOM COLOR TESTS DONE ====="
