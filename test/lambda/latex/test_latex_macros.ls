// test_latex_macros.ls — Test macro definition parsing and substitution
// Coverage: macros.ls — get_defs, find_macro, substitute_body

import macros: lambda.package.latex.macros

"===== LATEX MACROS TESTS ====="

// ---- get_defs: simple \newcommand ----
let nc1 = <newcommand <curly_group "\\hello"> <curly_group "world">>
let defs1 = macros.get_defs(<document ;nc1>)
"1. def count:"; len(defs1)
"2. def name:"; defs1[0].name
"3. def params 0:"; defs1[0].params

// ---- get_defs: \newcommand with params ----
let nc2 = <newcommand <curly_group "\\greet"> <brack_group "1"> <curly_group "Hello #1">>
let defs2 = macros.get_defs(<document ;nc2>)
"4. params 1:"; defs2[0].params
"5. body text:"; defs2[0].body is element

// ---- get_defs: \newcommand with optional arg ----
let nc3 = <newcommand <curly_group "\\opt"> <brack_group "1"> <brack_group "default"> <curly_group "Hi #1">>
let defs3 = macros.get_defs(<document ;nc3>)
"6. opt params:"; defs3[0].params
"7. default_arg:"; defs3[0].default_arg

// ---- get_defs: multiple macros ----
let multi = <document
    <newcommand <curly_group "\\a"> <curly_group "alpha">>
    <newcommand <curly_group "\\b"> <curly_group "beta">>>
let defs4 = macros.get_defs(multi)
"8. multi count:"; len(defs4)
"9. multi first:"; defs4[0].name
"10. multi second:"; defs4[1].name

// ---- find_macro ----
"11. find existing:"; macros.find_macro(defs4, "\\a") != null
"12. find name:"; macros.find_macro(defs4, "\\a").name
"13. find missing:"; macros.find_macro(defs4, "\\z") == null

// ---- get_defs: no macros ----
let empty = <document "just text">
let defs_empty = macros.get_defs(empty)
"14. empty defs:"; len(defs_empty)

// ---- substitute_body ----
let body = <curly_group "Hello " <param_ref "1"> " and " <param_ref "2">>
let inv = <greet <curly_group "World"> <curly_group "Lambda">>
let sub = macros.substitute_body(body, inv)
"15. sub is array:"; sub is array
"16. sub length:"; len(sub)

"===== ALL LATEX MACROS TESTS DONE ====="
