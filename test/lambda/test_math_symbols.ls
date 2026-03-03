// test_math_symbols.ls — Test math package symbol lookup and classification
// Coverage: symbols.ls — lookup_symbol, classify_symbol, get_operator_name, get_accent, get_delim_size

import sym: lambda.package.math.symbols

"===== MATH SYMBOL TESTS ====="

// ---- Greek lowercase ----
"1. alpha:"; sym.lookup_symbol("alpha")
"2. beta:"; sym.lookup_symbol("beta")
"3. gamma:"; sym.lookup_symbol("gamma")
"4. omega:"; sym.lookup_symbol("omega")
"5. theta:"; sym.lookup_symbol("theta")

// ---- Greek uppercase ----
"6. Gamma:"; sym.lookup_symbol("Gamma")
"7. Delta:"; sym.lookup_symbol("Delta")
"8. Sigma:"; sym.lookup_symbol("Sigma")
"9. Omega:"; sym.lookup_symbol("Omega")

// ---- with backslash prefix ----
"10. \\alpha:"; sym.lookup_symbol("\\alpha")
"11. \\infty:"; sym.lookup_symbol("\\infty")

// ---- binary operators ----
"12. times:"; sym.lookup_symbol("times")
"13. cdot:"; sym.lookup_symbol("cdot")
"14. pm:"; sym.lookup_symbol("pm")
"15. cup:"; sym.lookup_symbol("cup")
"16. cap:"; sym.lookup_symbol("cap")

// ---- relations ----
"17. leq:"; sym.lookup_symbol("leq")
"18. geq:"; sym.lookup_symbol("geq")
"19. neq:"; sym.lookup_symbol("neq")
"20. equiv:"; sym.lookup_symbol("equiv")
"21. subset:"; sym.lookup_symbol("subset")
"22. in:"; sym.lookup_symbol("in")

// ---- arrows ----
"23. rightarrow:"; sym.lookup_symbol("rightarrow")
"24. Rightarrow:"; sym.lookup_symbol("Rightarrow")
"25. mapsto:"; sym.lookup_symbol("mapsto")
"26. iff:"; sym.lookup_symbol("iff")

// ---- misc symbols ----
"27. infty:"; sym.lookup_symbol("infty")
"28. nabla:"; sym.lookup_symbol("nabla")
"29. partial:"; sym.lookup_symbol("partial")
"30. forall:"; sym.lookup_symbol("forall")
"31. exists:"; sym.lookup_symbol("exists")
"32. emptyset:"; sym.lookup_symbol("emptyset")

// ---- big operators ----
"33. sum:"; sym.lookup_symbol("sum")
"34. prod:"; sym.lookup_symbol("prod")
"35. int:"; sym.lookup_symbol("int")
"36. bigcup:"; sym.lookup_symbol("bigcup")

// ---- unknown ----
"37. unknown:"; sym.lookup_symbol("zzzzz")

// ---- classify_symbol ----
"38. classify times:"; sym.classify_symbol("times")
"39. classify leq:"; sym.classify_symbol("leq")
"40. classify rightarrow:"; sym.classify_symbol("rightarrow")
"41. classify sum:"; sym.classify_symbol("sum")
"42. classify alpha:"; sym.classify_symbol("alpha")
"43. classify sin:"; sym.classify_symbol("sin")
"44. classify unknown:"; sym.classify_symbol("zzz")

// ---- classify with backslash ----
"45. classify \\pm:"; sym.classify_symbol("\\pm")
"46. classify \\equiv:"; sym.classify_symbol("\\equiv")

// ---- get_operator_name ----
"47. op sin:"; sym.get_operator_name("sin")
"48. op cos:"; sym.get_operator_name("cos")
"49. op lim:"; sym.get_operator_name("lim")
"50. op limsup:"; sym.get_operator_name("limsup")
"51. op det:"; sym.get_operator_name("det")
"52. op unknown:"; sym.get_operator_name("zzz")

// ---- get_accent ----
"53. accent hat:"; sym.get_accent("hat")
"54. accent tilde:"; sym.get_accent("tilde")
"55. accent vec:"; sym.get_accent("vec")
"56. accent bar:"; sym.get_accent("bar")
"57. accent unknown:"; sym.get_accent("zzz")

// ---- get_delim_size ----
"58. delim big:"; sym.get_delim_size("big")
"59. delim Big:"; sym.get_delim_size("Big")
"60. delim bigg:"; sym.get_delim_size("bigg")
"61. delim Bigg:"; sym.get_delim_size("Bigg")

"===== ALL SYMBOL TESTS DONE ====="
