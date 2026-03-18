// test_math_atom_enclose.ls — Test math enclosure commands
// Coverage: atoms/enclose.ls — render_box, render_phantom, render_rule

import enc: lambda.package.math.atoms.enclose
import box: lambda.package.math.box
import mctx: lambda.package.math.context

"===== MATH ATOM ENCLOSE TESTS ====="

// mock render function
fn mock_render(node, context) {
    let text = if (node is string) node
        else if (node is element and (len(node) > 0)) string(node[0])
        else "?"
    box.text_box(text, null, "mord")
}

let base_ctx = mctx.display_context()

// ---- \boxed ----
let bx_node = {cmd: "\\boxed", content: <group "x">}
let bx = enc.render_box(bx_node, base_ctx, mock_render)
"1. boxed type:"; bx.type
"2. boxed has border:"; contains(bx.element.style, "border:")
"3. boxed extra height:"; (bx.height > 0.7)
"4. boxed extra width:"; (bx.width > 0.5)

// ---- \llap ----
let ll_node = {cmd: "\\llap", content: <group "L">}
let ll = enc.render_box(ll_node, base_ctx, mock_render)
"5. llap width:"; ll.width
"6. llap align:"; contains(ll.element.style, "text-align:right")

// ---- \rlap ----
let rl_node = {cmd: "\\rlap", content: <group "R">}
let rl = enc.render_box(rl_node, base_ctx, mock_render)
"7. rlap width:"; rl.width
"8. rlap align:"; contains(rl.element.style, "text-align:left")

// ---- \clap ----
let cl_node = {cmd: "\\clap", content: <group "C">}
let cl = enc.render_box(cl_node, base_ctx, mock_render)
"9. clap width:"; cl.width
"10. clap align:"; contains(cl.element.style, "text-align:center")

// ---- \phantom ----
let ph_node = {cmd: "\\phantom", content: <group "m">}
let ph = enc.render_phantom(ph_node, base_ctx, mock_render)
"11. phantom type:"; ph.type
"12. phantom hidden:"; contains(ph.element.style, "visibility:hidden")
"13. phantom height > 0:"; (ph.height > 0)
"14. phantom width > 0:"; (ph.width > 0)

// ---- \hphantom ----
let hp_node = {cmd: "\\hphantom", content: <group "m">}
let hp = enc.render_phantom(hp_node, base_ctx, mock_render)
"15. hphantom height:"; hp.height
"16. hphantom depth:"; hp.depth
"17. hphantom width > 0:"; (hp.width > 0)

// ---- \vphantom ----
let vp_node = {cmd: "\\vphantom", content: <group "m">}
let vp = enc.render_phantom(vp_node, base_ctx, mock_render)
"18. vphantom width:"; vp.width
"19. vphantom height > 0:"; (vp.height > 0)

// ---- \smash (default: zero h and d) ----
let sm_node = {cmd: "\\smash", content: <group "m">}
let sm = enc.render_phantom(sm_node, base_ctx, mock_render)
"20. smash height:"; sm.height
"21. smash depth:"; sm.depth
"22. smash width > 0:"; (sm.width > 0)

// ---- \smash[b] (zero depth only) ----
let smb_node = {cmd: "\\smash", content: <group "m">, options: "b"}
let smb = enc.render_phantom(smb_node, base_ctx, mock_render)
"23. smash b height > 0:"; (smb.height > 0)
"24. smash b depth:"; smb.depth

// ---- \smash[t] (zero height only) ----
let smt_node = {cmd: "\\smash", content: <group "m">, options: "t"}
let smt = enc.render_phantom(smt_node, base_ctx, mock_render)
"25. smash t height:"; smt.height
"26. smash t depth > 0:"; (smt.depth > 0)

// ---- \rule ----
let ru_node = {cmd: "\\rule", width: "2", height: "0.5"}
let ru = enc.render_rule(ru_node, base_ctx, mock_render)
"27. rule type:"; ru.type
"28. rule has style:"; ru.element.style != null
"29. rule has bg:"; contains(ru.element.style, "background:")
"30. rule width:"; ru.width
"31. rule height:"; ru.height

// ---- \rule with shift ----
let ru2_node = {cmd: "\\rule", width: "1", height: "0.3", shift: "0.2"}
let ru2 = enc.render_rule(ru2_node, base_ctx, mock_render)
"32. shifted rule align:"; contains(ru2.element.style, "vertical-align:")

// ---- default rule dims ----
let ru3_node = {cmd: "\\rule"}
let ru3 = enc.render_rule(ru3_node, base_ctx, mock_render)
"33. default width:"; ru3.width
"34. default height:"; ru3.height

"===== ALL MATH ATOM ENCLOSE TESTS DONE ====="
