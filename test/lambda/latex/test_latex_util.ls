// test_latex_util.ls — Test LaTeX utility functions
// Coverage: util.ls — text_of, rich_text_of, slugify, str_repeat, str_join,
//   is_parbreak, is_whitespace, trim_children, find_child, find_descendant,
//   children_array, attr_or, text_of_child, parse_kv_options, lookup

import util: lambda.package.latex.util

"===== LATEX UTIL TESTS ====="

// ---- text_of ----
"1. text_of null:"; util.text_of(null)
"2. text_of str:"; util.text_of("hello")
"3. text_of int:"; util.text_of(42)
"4. text_of float:"; util.text_of(3.14)
"5. text_of element:"; util.text_of(<span "world">)
"6. text_of nested:"; util.text_of(<div <span "ab"> "cd">)

// ---- rich_text_of ----
"7. rich_text str:"; util.rich_text_of("plain")
"8. rich_text LaTeX:"; util.rich_text_of(<LaTeX>)
"9. rich_text TeX:"; util.rich_text_of(<TeX>)
"10. rich_text ldots:"; util.rich_text_of(<ldots>)
"11. rich_text copyright:"; util.rich_text_of(<copyright>)
"12. rich_text nested:"; util.rich_text_of(<span "Hello " <ldots>>)

// ---- slugify ----
"13. slugify simple:"; util.slugify("Hello World")
"14. slugify spaces:"; util.slugify("  Foo Bar  ")
"15. slugify underscore:"; util.slugify("a_b_c")

// ---- str_repeat ----
"16. repeat 0:"; util.str_repeat("ab", 0)
"17. repeat 1:"; util.str_repeat("ab", 1)
"18. repeat 3:"; util.str_repeat("ab", 3)

// ---- str_join ----
"19. join empty:"; util.str_join([], ", ")
"20. join one:"; util.str_join(["a"], ", ")
"21. join three:"; util.str_join(["a", "b", "c"], ", ")

// ---- is_parbreak ----
"22. parbreak sym:"; util.is_parbreak('parbreak')
"23. parbreak str:"; util.is_parbreak("parbreak")
"24. parbreak other:"; util.is_parbreak('foo')

// ---- is_whitespace ----
"25. ws empty:"; util.is_whitespace("")
"26. ws spaces:"; util.is_whitespace("   ")
"27. ws text:"; util.is_whitespace("hello")
"28. ws non-str:"; util.is_whitespace(42)

// ---- trim_children ----
"29. trim empty:"; len(util.trim_children([]))
"30. trim ws:"; len(util.trim_children(["  ", "hello", "  "]))
"31. trim mixed:"; util.trim_children(["  ", "a", " ", "b", "  "])[0]

// ---- find_child ----
let parent = <div <span "a"> <em "b"> <span "c">>
"32. find_child span:"; util.text_of(util.find_child(parent, "span"))
"33. find_child em:"; util.text_of(util.find_child(parent, "em"))
"34. find_child missing:"; util.find_child(parent, "strong") == null

// ---- find_descendant ----
let deep = <div <section <p "deep">>>
"35. find_desc p:"; util.text_of(util.find_descendant(deep, "p"))
"36. find_desc missing:"; util.find_descendant(deep, "h1") == null

// ---- children_array ----
let ca_el = <div "a" "b" "c">
"37. children_array len:"; len(util.children_array(ca_el))
"38. children_array first:"; util.children_array(ca_el)[0]

// ---- attr_or ----
let ao_el = <div class: "foo">
"39. attr_or found:"; util.attr_or(ao_el, "class", "bar")
"40. attr_or default:"; util.attr_or(ao_el, "id", "default")

// ---- text_of_child ----
let toc_el = <div "first" "second" "third">
"41. text_of_child 0:"; util.text_of_child(toc_el, 0)
"42. text_of_child 1:"; util.text_of_child(toc_el, 1)
"43. text_of_child oob:"; util.text_of_child(toc_el, 99)

// ---- parse_kv_options ----
"44. kv empty:"; len(util.parse_kv_options(null))
"45. kv pair:"; util.parse_kv_options("width=3cm").width
"46. kv flag:"; util.parse_kv_options("keepaspectratio").keepaspectratio
"47. kv multi:"; util.parse_kv_options("width=3cm, height=2cm").height

// ---- lookup ----
let entries = [{key: "a", val: 1}, {key: "b", val: 2}, {key: "a", val: 3}]
"48. lookup first:"; util.lookup(entries, "b")
"49. lookup last wins:"; util.lookup(entries, "a")
"50. lookup missing:"; util.lookup(entries, "z") == null
"51. lookup null:"; util.lookup(null, "x") == null

"===== ALL LATEX UTIL TESTS DONE ====="
