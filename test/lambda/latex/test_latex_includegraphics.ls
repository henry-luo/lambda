// test_latex_includegraphics.ls — Test \includegraphics option parsing
// Tests Phase 2I: width, height, scale, angle, keepaspectratio, trim+clip

import util: lambda.package.latex.util

"===== INCLUDEGRAPHICS OPTION TESTS ====="

// ---- parse_kv_options: basic key=value parsing ----
let opts1 = util.parse_kv_options("width=5cm,height=3cm")
"1. width:"; opts1.width
"2. height:"; opts1.height

let opts2 = util.parse_kv_options("width=10em, height=8em, scale=0.5")
"3. width with spaces:"; opts2.width
"4. scale:"; opts2.scale

// ---- parse_kv_options: flags without value ----
let opts3 = util.parse_kv_options("width=5cm,keepaspectratio")
"5. keepaspectratio flag:"; opts3.keepaspectratio
"6. width with flag:"; opts3.width

// ---- parse_kv_options: angle ----
let opts4 = util.parse_kv_options("angle=90,scale=0.75")
"7. angle:"; opts4.angle
"8. scale with angle:"; opts4.scale

// ---- parse_kv_options: null/empty ----
let opts5 = util.parse_kv_options(null)
"9. null opts is map:"; opts5 is map

let opts6 = util.parse_kv_options("")
"10. empty opts is map:"; opts6 is map

// ---- parse_kv_options: trim + clip ----
let opts7 = util.parse_kv_options("trim=0 0 5cm 0,clip")
"11. trim value:"; opts7.trim
"12. clip flag:"; opts7.clip

// ---- text_of_skip_brack: extracts text skipping brack_group ----
let el1 = <includegraphics <brack_group "width=5cm"> "image.png">
"13. text_of_skip_brack:"; util.text_of_skip_brack(el1)

let el2 = <includegraphics "photo.jpg">
"14. no brack:"; util.text_of_skip_brack(el2)

let el3 = <includegraphics <brack_group "width=5cm,height=3cm"> "path/to/img.png">
"15. with path:"; util.text_of_skip_brack(el3)

// ---- find_child: finds brack_group ----
"16. find brack:"; util.find_child(el1, 'brack_group') is element
"17. no brack found:"; util.find_child(el2, 'brack_group')

// ---- parse_kv_options: single key ----
let opts8 = util.parse_kv_options("width=100%")
"18. percent width:"; opts8.width

// ---- parse_kv_options: complex values ----
let opts9 = util.parse_kv_options("width=0.5\\textwidth")
"19. textwidth:"; opts9.width

// ---- parse_kv_options: extra commas ----
let opts10 = util.parse_kv_options(",width=5cm,,height=3cm,")
"20. extra commas width:"; opts10.width
"21. extra commas height:"; opts10.height

"===== ALL TESTS PASSED ====="
