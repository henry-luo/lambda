// test_math_atom_spacing.ls — Test math spacing commands
// Coverage: atoms/spacing.ls — render (quad, qquad, thinspace, etc.)

import sp: lambda.package.math.atoms.spacing

"===== MATH ATOM SPACING TESTS ====="

// ---- \quad ----
let quad = sp.render({cmd: "\\quad"}, {}, null)
"1. quad type:"; quad.type
"2. quad width:"; quad.width

// ---- \qquad ----
let qquad = sp.render({cmd: "\\qquad"}, {}, null)
"3. qquad width:"; qquad.width

// ---- \enspace ----
let ensp = sp.render({cmd: "\\enspace"}, {}, null)
"4. enspace width:"; ensp.width

// ---- \, (thinspace) ----
let thin = sp.render({cmd: "\\,"}, {}, null)
"5. thin width:"; thin.width
"6. thin type:"; thin.type

// ---- \: (medspace) ----
let med = sp.render({cmd: "\\:"}, {}, null)
"7. medspace width:"; med.width

// ---- \; (thickspace) ----
let thick = sp.render({cmd: "\\;"}, {}, null)
"8. thickspace width:"; thick.width

// ---- \! (negative thin) ----
let neg = sp.render({cmd: "\\!"}, {}, null)
"9. negthin width:"; neg.width

// ---- named equivalents ----
let thin2 = sp.render({cmd: "\\thinspace"}, {}, null)
"10. thinspace eq:"; thin2.width == thin.width

let med2 = sp.render({cmd: "\\medspace"}, {}, null)
"11. medspace eq:"; med2.width == med.width

let thick2 = sp.render({cmd: "\\thickspace"}, {}, null)
"12. thickspace eq:"; thick2.width == thick.width

let neg2 = sp.render({cmd: "\\negthinspace"}, {}, null)
"13. negthin eq:"; neg2.width == neg.width

// ---- custom dimension (hspace with em) ----
let cust = sp.render({cmd: "\\hspace", value: "1.5", unit: "em"}, {}, null)
"14. custom width:"; cust.width
"15. custom type:"; cust.type

// ---- dimension in pt ----
let pt = sp.render({cmd: "\\kern", value: "10", unit: "pt"}, {}, null)
"16. pt width:"; pt.width

// ---- negative sign ----
let neg_d = sp.render({cmd: "\\kern", value: "1", unit: "em", sign: "-"}, {}, null)
"17. negative:"; neg_d.width

// ---- zero / null ----
let zero = sp.render({cmd: "\\hspace"}, {}, null)
"18. null value:"; zero.width

// ---- all skips have element ----
"19. quad has el:"; quad.element is element
"20. thin has el:"; thin.element is element
"21. cust has el:"; cust.element is element

"===== ALL MATH ATOM SPACING TESTS DONE ====="
