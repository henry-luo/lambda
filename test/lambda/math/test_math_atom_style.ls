// test_math_atom_style.ls — Test math style/font command rendering
// Coverage: atoms/style.ls — render (mathbf, mathrm, displaystyle, etc.)

import style: lambda.package.math.atoms.style
import box: lambda.package.math.box
import mctx: lambda.package.math.context

"===== MATH ATOM STYLE TESTS ====="

// mock render function: returns a text_box of the node's text
fn mock_render(node, context) {
    let text = if (node is string) node
        else if (node is element and len(node) > 0) string(node[0])
        else "?"
    box.text_box(text, context.font, "mord")
}

let base_ctx = mctx.display_context()

// ---- \mathbf ----
let bf_node = {cmd: "\\mathbf", arg: <group "x">}
let bf = style.render(bf_node, base_ctx, mock_render)
"1. mathbf type:"; bf.type
"2. mathbf has el:"; bf.element is element

// ---- \mathrm ----
let rm_node = {cmd: "\\mathrm", arg: <group "sin">}
let rm = style.render(rm_node, base_ctx, mock_render)
"3. mathrm type:"; rm.type

// ---- \mathit ----
let it_node = {cmd: "\\mathit", arg: <group "x">}
let it = style.render(it_node, base_ctx, mock_render)
"4. mathit type:"; it.type

// ---- \mathbb ----
let bb_node = {cmd: "\\mathbb", arg: <group "R">}
let bb = style.render(bb_node, base_ctx, mock_render)
"5. mathbb type:"; bb.type

// ---- \mathcal ----
let cal_node = {cmd: "\\mathcal", arg: <group "L">}
let cal = style.render(cal_node, base_ctx, mock_render)
"6. mathcal type:"; cal.type

// ---- \mathfrak ----
let frak_node = {cmd: "\\mathfrak", arg: <group "g">}
let frak = style.render(frak_node, base_ctx, mock_render)
"7. mathfrak type:"; frak.type

// ---- \mathtt ----
let tt_node = {cmd: "\\mathtt", arg: <group "x">}
let tt = style.render(tt_node, base_ctx, mock_render)
"8. mathtt type:"; tt.type

// ---- \mathscr ----
let scr_node = {cmd: "\\mathscr", arg: <group "F">}
let scr = style.render(scr_node, base_ctx, mock_render)
"9. mathscr type:"; scr.type

// ---- \mathsf ----
let sf_node = {cmd: "\\mathsf", arg: <group "A">}
let sf = style.render(sf_node, base_ctx, mock_render)
"10. mathsf type:"; sf.type

// ---- \displaystyle ----
let ds_node = {cmd: "\\displaystyle", arg: <group "x">}
let ds = style.render(ds_node, base_ctx, mock_render)
"11. displaystyle type:"; ds.type

// ---- \textstyle ----
let ts_node = {cmd: "\\textstyle", arg: <group "x">}
let ts = style.render(ts_node, base_ctx, mock_render)
"12. textstyle type:"; ts.type

// ---- \scriptstyle ----
let ss_node = {cmd: "\\scriptstyle", arg: <group "x">}
let ss = style.render(ss_node, base_ctx, mock_render)
"13. scriptstyle type:"; ss.type

// ---- no children / empty node ----
let empty_node = {cmd: "\\mathbf"}
let emp = style.render(empty_node, base_ctx, mock_render)
"14. empty node has el:"; emp.element is element

"===== ALL MATH ATOM STYLE TESTS DONE ====="
