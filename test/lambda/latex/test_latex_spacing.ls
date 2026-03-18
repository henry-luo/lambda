// test_latex_spacing.ls — Test spacing, line breaks, page breaks, rules
// Coverage: elements/spacing.ls — render_linebreak, render_hspace, render_vspace,
//   render_hrule, render_pagebreak, render_hfill, render_nbsp,
//   render_bigskip_el, render_medskip_el, render_smallskip_el,
//   render_quad_el, render_qquad_el, render_enspace_el, render_thinspace_el,
//   render_negthinspace_el, render_noindent_el, render_hspace_el, render_vspace_el

import sp: lambda.package.latex.elements.spacing

"===== LATEX SPACING TESTS ====="

// ---- linebreak / newline ----
let ctx = {}
let lb = sp.render_linebreak(ctx)
"1. linebreak tag:"; name(lb.result)
"2. linebreak ctx:"; lb.ctx == ctx

let nl = sp.render_newline(ctx)
"3. newline tag:"; name(nl.result)

// ---- hspace ----
let hsp_el = <hspace <curly_group "2em">>
let hsp = sp.render_hspace(hsp_el, ctx)
"4. hspace tag:"; name(hsp.result)
"5. hspace class:"; hsp.result.class
"6. hspace has margin:"; contains(hsp.result.style, "margin-left:")

// ---- vspace ----
let vsp_el = <vspace <curly_group "1cm">>
let vsp = sp.render_vspace(vsp_el, ctx)
"7. vspace tag:"; name(vsp.result)
"8. vspace class:"; vsp.result.class
"9. vspace has margin:"; contains(vsp.result.style, "margin-top:")

// ---- hrule ----
let hr = sp.render_hrule(ctx)
"10. hrule tag:"; name(hr.result)

// ---- pagebreak ----
let pb = sp.render_pagebreak(ctx)
"11. pagebreak tag:"; name(pb.result)
"12. pagebreak class:"; pb.result.class

// ---- hfill ----
let hf = sp.render_hfill(ctx)
"13. hfill tag:"; name(hf.result)
"14. hfill class:"; hf.result.class
"15. hfill has flex:"; contains(hf.result.style, "flex:")

// ---- nbsp ----
let nbsp = sp.render_nbsp(ctx)
"16. nbsp is string:"; nbsp.result is string

// ---- skip elements ----
"17. bigskip tag:"; name(sp.render_bigskip_el())
"18. bigskip class:"; sp.render_bigskip_el().class
"19. medskip tag:"; name(sp.render_medskip_el())
"20. medskip class:"; sp.render_medskip_el().class
"21. smallskip tag:"; name(sp.render_smallskip_el())
"22. smallskip class:"; sp.render_smallskip_el().class

// ---- break elements ----
"23. bigbreak tag:"; name(sp.render_bigbreak_el())
"24. medbreak tag:"; name(sp.render_medbreak_el())
"25. smallbreak tag:"; name(sp.render_smallbreak_el())

// ---- quad / qquad ----
"26. quad tag:"; name(sp.render_quad_el())
"27. quad class:"; sp.render_quad_el().class
"28. qquad tag:"; name(sp.render_qquad_el())
"29. qquad class:"; sp.render_qquad_el().class

// ---- enspace / thinspace ----
"30. enspace is string:"; sp.render_enspace_el() is string
"31. thinspace is string:"; sp.render_thinspace_el() is string

// ---- negthinspace ----
"32. negthinspace tag:"; name(sp.render_negthinspace_el())

// ---- noindent ----
"33. noindent tag:"; name(sp.render_noindent_el())

// ---- italcorr (should return null) ----
"34. italcorr null:"; sp.render_italcorr_el() == null

// ---- hspace_el ----
let hsp_el2 = <hspace <curly_group "3pt">>
let hsp2 = sp.render_hspace_el(hsp_el2)
"35. hspace_el tag:"; name(hsp2)
"36. hspace_el has margin:"; contains(hsp2.style, "margin-left:")

// ---- vspace_el ----
let vsp_el2 = <vspace <curly_group "5mm">>
let vsp2 = sp.render_vspace_el(vsp_el2)
"37. vspace_el tag:"; name(vsp2)
"38. vspace_el has margin:"; contains(vsp2.style, "margin-top:")

"===== ALL LATEX SPACING TESTS DONE ====="
