// test_latex_symbols.ls — Test extended symbol and spacing command rendering
// Tests Phase 2A (symbols) and Phase 2C (spacing) of LaTeX package

import sym: .lambda.package.latex.symbols
import spacing: .lambda.package.latex.elements.spacing

"===== EXTENDED SYMBOL RESOLUTION TESTS ====="

// ---- textgreek lowercase ----
"1. textalpha:"; sym.resolve_extended("textalpha")
"2. textbeta:"; sym.resolve_extended("textbeta")
"3. textgamma:"; sym.resolve_extended("textgamma")
"4. textdelta:"; sym.resolve_extended("textdelta")
"5. textomega:"; sym.resolve_extended("textomega")
"6. textpi:"; sym.resolve_extended("textpi")
"7. textsigma:"; sym.resolve_extended("textsigma")
"8. textlambda:"; sym.resolve_extended("textlambda")

// ---- textgreek uppercase ----
"9. textAlpha:"; sym.resolve_extended("textAlpha")
"10. textGamma:"; sym.resolve_extended("textGamma")
"11. textDelta:"; sym.resolve_extended("textDelta")
"12. textOmega:"; sym.resolve_extended("textOmega")
"13. textSigma:"; sym.resolve_extended("textSigma")
"14. textPi:"; sym.resolve_extended("textPi")

// ---- textcomp: currency ----
"15. texteuro:"; sym.resolve_extended("texteuro")
"16. textcent:"; sym.resolve_extended("textcent")
"17. textyen:"; sym.resolve_extended("textyen")
"18. textsterling:"; sym.resolve_extended("textsterling")
"19. textwon:"; sym.resolve_extended("textwon")

// ---- textcomp: math/science ----
"20. textdegree:"; sym.resolve_extended("textdegree")
"21. textcelsius:"; sym.resolve_extended("textcelsius")
"22. texttimes:"; sym.resolve_extended("texttimes")
"23. textdiv:"; sym.resolve_extended("textdiv")
"24. textpm:"; sym.resolve_extended("textpm")
"25. textsurd:"; sym.resolve_extended("textsurd")
"26. textonehalf:"; sym.resolve_extended("textonehalf")
"27. textthreequarters:"; sym.resolve_extended("textthreequarters")
"28. textonesuperior:"; sym.resolve_extended("textonesuperior")
"29. texttwosuperior:"; sym.resolve_extended("texttwosuperior")
"30. textthreesuperior:"; sym.resolve_extended("textthreesuperior")

// ---- textcomp: arrows ----
"31. textleftarrow:"; sym.resolve_extended("textleftarrow")
"32. textrightarrow:"; sym.resolve_extended("textrightarrow")
"33. textuparrow:"; sym.resolve_extended("textuparrow")
"34. textdownarrow:"; sym.resolve_extended("textdownarrow")

// ---- gensymb / misc ----
"35. checkmark:"; sym.resolve_extended("checkmark")
"36. textregistered:"; sym.resolve_extended("textregistered")
"37. texttrademark:"; sym.resolve_extended("texttrademark")
"38. textmusicalnote:"; sym.resolve_extended("textmusicalnote")
"39. textnumero:"; sym.resolve_extended("textnumero")
"40. textestimated:"; sym.resolve_extended("textestimated")

// ---- non-ASCII character commands ----
"41. IJ:"; sym.resolve_extended("IJ")
"42. ij:"; sym.resolve_extended("ij")
"43. TH:"; sym.resolve_extended("TH")
"44. th:"; sym.resolve_extended("th")
"45. DH:"; sym.resolve_extended("DH")
"46. dh:"; sym.resolve_extended("dh")
"47. NG:"; sym.resolve_extended("NG")
"48. ng:"; sym.resolve_extended("ng")

// ---- old-style numerals ----
"49. textzerooldstyle:"; sym.resolve_extended("textzerooldstyle")
"50. textfiveoldstyle:"; sym.resolve_extended("textfiveoldstyle")

// ---- null for unknown ----
"51. unknown returns null:"; sym.resolve_extended("notacommand") == null

"===== SPACING ELEMENT TESTS ====="

// vertical skips
"52. bigskip:"; name(spacing.render_bigskip_el())
"53. medskip:"; name(spacing.render_medskip_el())
"54. smallskip:"; name(spacing.render_smallskip_el())

// horizontal spacing
"55. quad:"; name(spacing.render_quad_el())
"56. qquad:"; name(spacing.render_qquad_el())
"57. thinspace:"; spacing.render_thinspace_el()
"58. enspace:"; spacing.render_enspace_el()

// noindent
"59. noindent:"; name(spacing.render_noindent_el())

"===== ALL TESTS DONE ====="
