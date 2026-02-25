// test_latex_boxes.ls — Test box commands rendering
// Tests Phase 2B: \fbox, \mbox, \makebox, \framebox, \parbox, \raisebox, \llap, etc.

import boxes: .lambda.package.latex.elements.boxes

"===== BOX COMMAND TESTS ====="

// ---- simple boxes ----
"1. mbox tag:"; name(boxes.render_mbox(["hello"]))
"2. mbox class:"; boxes.render_mbox(["hello"]).class
"3. fbox tag:"; name(boxes.render_fbox(["framed"]))
"4. fbox class:"; boxes.render_fbox(["framed"]).class
"5. frame tag:"; name(boxes.render_frame(["framed"]))
"6. frame class:"; boxes.render_frame(["framed"]).class

// ---- makebox with args ----
// simulate: <makebox <brack_group "3cm"> <brack_group "c"> "centered">
let makebox_el = <makebox <brack_group "3cm"> <brack_group "c"> "centered">
"7. makebox tag:"; name(boxes.render_makebox(makebox_el, ["centered"]))
"8. makebox class:"; boxes.render_makebox(makebox_el, ["centered"]).class
"9. makebox style has width:"; contains(boxes.render_makebox(makebox_el, ["centered"]).style, "width:3cm")
"10. makebox style has center:"; contains(boxes.render_makebox(makebox_el, ["centered"]).style, "text-align:center")

// ---- framebox ----
let framebox_el = <framebox <brack_group "5cm"> <brack_group "l"> "left text">
"11. framebox class:"; boxes.render_framebox(framebox_el, ["left text"]).class
"12. framebox has border:"; contains(boxes.render_framebox(framebox_el, ["left text"]).style, "border:")

// ---- parbox ----
let parbox_el = <parbox "5cm" "paragraph text">
"13. parbox tag:"; name(boxes.render_parbox(parbox_el, ["5cm", "paragraph text"]))
"14. parbox class:"; boxes.render_parbox(parbox_el, ["5cm", "paragraph text"]).class
"15. parbox has width:"; contains(boxes.render_parbox(parbox_el, ["5cm", "paragraph text"]).style, "width:5cm")

// ---- raisebox ----
let raisebox_el = <raisebox "1em" "raised text">
"16. raisebox tag:"; name(boxes.render_raisebox(raisebox_el, ["1em", "raised text"]))
"17. raisebox class:"; boxes.render_raisebox(raisebox_el, ["1em", "raised text"]).class
"18. raisebox has bottom:"; contains(boxes.render_raisebox(raisebox_el, ["1em", "raised text"]).style, "bottom:1em")

// ---- low-level boxes ----
"19. llap tag:"; name(boxes.render_llap(["lapped"]))
"20. llap class:"; boxes.render_llap(["lapped"]).class
"21. llap has width:0:"; contains(boxes.render_llap(["lapped"]).style, "width:0")

"22. rlap class:"; boxes.render_rlap(["rapped"]).class
"23. rlap has width:0:"; contains(boxes.render_rlap(["rapped"]).style, "width:0")

"24. smash class:"; boxes.render_smash(["smashed"]).class
"25. smash has height:0:"; contains(boxes.render_smash(["smashed"]).style, "height:0")

"26. phantom class:"; boxes.render_phantom(["ghost"]).class
"27. phantom has hidden:"; contains(boxes.render_phantom(["ghost"]).style, "visibility:hidden")

"28. hphantom class:"; boxes.render_hphantom(["hghost"]).class
"29. hphantom has hidden:"; contains(boxes.render_hphantom(["hghost"]).style, "visibility:hidden")

"30. vphantom class:"; boxes.render_vphantom(["vghost"]).class
"31. vphantom has hidden:"; contains(boxes.render_vphantom(["vghost"]).style, "visibility:hidden")

"===== ALL BOX TESTS DONE ====="
