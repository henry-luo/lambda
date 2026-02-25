// test_math_spacing.ls — Test math inter-atom spacing table
// Coverage: spacing_table.ls — atom_type_index, get_spacing, spacing_class

import sp: .lambda.package.math.spacing_table

"===== MATH SPACING TESTS ====="

// ---- atom_type_index ----
"1. idx mord:"; sp.atom_type_index("mord")
"2. idx mop:"; sp.atom_type_index("mop")
"3. idx mbin:"; sp.atom_type_index("mbin")
"4. idx mrel:"; sp.atom_type_index("mrel")
"5. idx mopen:"; sp.atom_type_index("mopen")
"6. idx mclose:"; sp.atom_type_index("mclose")
"7. idx mpunct:"; sp.atom_type_index("mpunct")
"8. idx minner:"; sp.atom_type_index("minner")
"9. idx unknown:"; sp.atom_type_index("xxx")

// ---- get_spacing: display style ----
"10. ord-ord display:"; sp.get_spacing("mord", "mord", "display")
"11. ord-op display:"; sp.get_spacing("mord", "mop", "display")
"12. ord-bin display:"; sp.get_spacing("mord", "mbin", "display")
"13. ord-rel display:"; sp.get_spacing("mord", "mrel", "display")
"14. ord-open display:"; sp.get_spacing("mord", "mopen", "display")
"15. bin-ord display:"; sp.get_spacing("mbin", "mord", "display")
"16. rel-ord display:"; sp.get_spacing("mrel", "mord", "display")
"17. op-op display:"; sp.get_spacing("mop", "mop", "display")
"18. close-bin display:"; sp.get_spacing("mclose", "mbin", "display")

// ---- get_spacing: script style (conditional spacing suppressed) ----
"19. ord-bin script:"; sp.get_spacing("mord", "mbin", "script")
"20. ord-rel script:"; sp.get_spacing("mord", "mrel", "script")
"21. bin-ord script:"; sp.get_spacing("mbin", "mord", "script")
"22. op-op script:"; sp.get_spacing("mop", "mop", "script")

// ---- get_spacing: text style (same as display for conditional) ----
"23. ord-bin text:"; sp.get_spacing("mord", "mbin", "text")
"24. ord-rel text:"; sp.get_spacing("mord", "mrel", "text")

// ---- spacing_class ----
"25. class 0:"; sp.spacing_class(0.0)
"26. class thin:"; sp.spacing_class(0.16667)
"27. class medium:"; sp.spacing_class(0.22222)
"28. class thick:"; sp.spacing_class(0.27778)
"29. class negative:"; sp.spacing_class(-0.16667)

"===== ALL SPACING TESTS DONE ====="
