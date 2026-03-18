// test_latex_to_html.ls — Test HTML serialization
// Coverage: to_html.ls — to_html (serialize element tree to HTML string)

import to_html: lambda.package.latex.to_html

"===== LATEX TO_HTML TESTS ====="

// ---- simple element ----
"1. span:"; to_html.to_html(<span "hello">)

// ---- element with class ----
"2. class:"; to_html.to_html(<div class: "foo" "text">)

// ---- void element ----
"3. br:"; to_html.to_html(<br>)
"4. hr:"; to_html.to_html(<hr>)

// ---- nested elements ----
"5. nested:"; to_html.to_html(<div <p "inner">>)

// ---- element with style ----
"6. style:"; to_html.to_html(<span style: "color:red" "styled">)

// ---- plain string ----
"7. string:"; to_html.to_html("plain text")

// ---- HTML escaping ----
"8. escape lt:"; contains(to_html.to_html("<script>"), "&lt;")
"9. escape amp:"; contains(to_html.to_html("a & b"), "&amp;")

// ---- empty element ----
"10. empty div:"; to_html.to_html(<div>)

// ---- multiple children ----
"11. multi:"; to_html.to_html(<p "a" <em "b"> "c">)

// ---- element with id ----
"12. id attr:"; contains(to_html.to_html(<h1 id: "sec1" "Title">), "id=")

"===== ALL LATEX TO_HTML TESTS DONE ====="
