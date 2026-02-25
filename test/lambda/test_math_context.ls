// test_math_context.ls — Test math rendering context
// Coverage: context.ls — display_context, text_context, derive, numer_context, denom_context, sup_context, sub_context, is_display, is_script, context_scale, font_size_css

import ctx: .lambda.package.math.context

"===== MATH CONTEXT TESTS ====="

// ---- display_context ----
let dc = ctx.display_context()
"1. display style:"; dc.style
"2. display size:"; dc.size
"3. display font:"; dc.font
"4. display cramped:"; dc.cramped
"5. display phantom:"; dc.phantom

// ---- text_context ----
let tc = ctx.text_context()
"6. text style:"; tc.style
"7. text size:"; tc.size
"8. text cramped:"; tc.cramped

// ---- is_display ----
"9. dc is_display:"; ctx.is_display(dc)
"10. tc is_display:"; ctx.is_display(tc)

// ---- derive ----
let d1 = ctx.derive(dc, {color: "#ff0000"})
"11. derive color:"; d1.color
"12. derive keeps style:"; d1.style
"13. derive keeps font:"; d1.font

let d2 = ctx.derive(dc, {font: "mathbf", cramped: true})
"14. derive font:"; d2.font
"15. derive cramped:"; d2.cramped
"16. derive keeps color:"; d2.color

// ---- numer_context ----
let nc = ctx.numer_context(dc)
"17. numer of display:"; nc.style
"18. numer cramped:"; nc.cramped

let nc2 = ctx.numer_context(tc)
"19. numer of text:"; nc2.style

let nc3 = ctx.numer_context(ctx.derive(dc, {style: "script"}))
"20. numer of script:"; nc3.style

// ---- denom_context ----
let dnc = ctx.denom_context(dc)
"21. denom of display:"; dnc.style
"22. denom cramped:"; dnc.cramped

let dnc2 = ctx.denom_context(tc)
"23. denom of text:"; dnc2.style
"24. denom of text cramped:"; dnc2.cramped

// ---- sup_context ----
let sc = ctx.sup_context(dc)
"25. sup of display:"; sc.style

let sc2 = ctx.sup_context(tc)
"26. sup of text:"; sc2.style

let sc3 = ctx.sup_context(ctx.derive(dc, {style: "script"}))
"27. sup of script:"; sc3.style

// ---- sub_context ----
let subc = ctx.sub_context(dc)
"28. sub of display:"; subc.style
"29. sub cramped:"; subc.cramped

let subc2 = ctx.sub_context(tc)
"30. sub of text:"; subc2.style

// ---- is_script ----
"31. display is_script:"; ctx.is_script(dc)
"32. text is_script:"; ctx.is_script(tc)
"33. script is_script:"; ctx.is_script(ctx.derive(dc, {style: "script"}))
"34. scriptscript is_script:"; ctx.is_script(ctx.derive(dc, {style: "scriptscript"}))

// ---- context_scale ----
"35. display scale:"; ctx.context_scale(dc)
"36. text scale:"; ctx.context_scale(tc)
"37. script scale:"; ctx.context_scale(ctx.derive(dc, {style: "script"}))
"38. scriptscript scale:"; ctx.context_scale(ctx.derive(dc, {style: "scriptscript"}))

// ---- font_size_css ----
"39. display font_css:"; ctx.font_size_css(dc)
"40. script font_css:"; ctx.font_size_css(ctx.derive(dc, {style: "script"}))
"41. scriptscript font_css:"; ctx.font_size_css(ctx.derive(dc, {style: "scriptscript"}))

"===== ALL CONTEXT TESTS DONE ====="
