// test_latex_analyze.ls — Test document structure analysis
// Coverage: analyze.ls — analyze (walk AST to extract doc info)

import analyze: lambda.package.latex.analyze

"===== LATEX ANALYZE TESTS ====="

// ---- minimal document ----
let ast1 = <document
    <documentclass "article">
    <title <curly_group "My Title">>
    <author <curly_group "John Doe">>
    <begin "document">
    <section <curly_group "Intro">>
    <text "Hello world.">
    <section <curly_group "Methods">>
    <subsection <curly_group "Setup">>
    <text "Details here.">
    <end "document">>

let info = analyze.analyze(ast1)

"1. docclass:"; info.docclass
"2. title:"; info.title
"3. author:"; info.author
"4. headings count:"; len(info.headings)
"5. first heading text:"; info.headings[0].text
"6. first heading level:"; info.headings[0].level
"7. second heading text:"; info.headings[1].text
"8. third heading text:"; info.headings[2].text
"9. third heading level:"; info.headings[2].level

// ---- document with footnotes ----
let ast2 = <document
    <documentclass "article">
    <begin "document">
    <section <curly_group "Notes">>
    <text "See">
    <footnote <curly_group "First note">>
    <text "and">
    <footnote <curly_group "Second note">>
    <end "document">>

let info2 = analyze.analyze(ast2)
"10. footnote count:"; len(info2.footnotes)

// ---- document with labels ----
let ast3 = <document
    <documentclass "article">
    <begin "document">
    <section <curly_group "Labeled">>
    <label <curly_group "sec:intro">>
    <end "document">>

let info3 = analyze.analyze(ast3)
"11. labels count:"; len(info3.labels)

// ---- empty document ----
let ast4 = <document <documentclass "article"> <begin "document"> <end "document">>
let info4 = analyze.analyze(ast4)
"12. empty headings:"; len(info4.headings)
"13. empty title:"; info4.title == null
"14. empty docclass:"; info4.docclass

// ---- secnumdepth present ----
"15. secnumdepth:"; info.secnumdepth

"===== ALL LATEX ANALYZE TESTS DONE ====="
