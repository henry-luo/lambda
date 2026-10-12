// P5: lambda.grammar.xpath parses XPath 3.1 into a Lambda element tree
// (Tree-sitter style: one left-recursive Expr with left/op/right labels,
// operators as symbols, punctuation hidden). One line per expression.
import lambda.grammar.xpath

let cases = [
  "/", "/a/b", "//book[@year > 2000]/title", "a//b", "../x", "@id", "child::a/descendant-or-self::node()",
  "text()", "*", "a:*", "*:b", "ns:name", "Q{http://x}local",
  "$x", "$x[1]", "count(//a)", "fn:concat('a', \"b\")", "f(?, 2)", "f#2",
  "1.5", ".5", "1e3", "'it''s'", "\"q\"\"q\"",
  "1 to 5", "a = b", "a eq b", "a is b", "a << b", "1 < 2 and 2 < 3", "a or b and c",
  "-1 * 2", "- - a", "a || b || c", "a | b intersect c", "a ! b ! c",
  "x instance of xs:integer+", "x treat as item()*", "x cast as xs:string?", "x castable as xs:int",
  "$s => upper-case() => string-length()",
  "for $i in 1 to 3, $j in 4 return $i * $j", "let $x := 1 return $x + 1",
  "some $x in (1, 2) satisfies $x > 1", "if (a) then b else c",
  "function($a as xs:int) as xs:int { $a + 1 }", "map { 'a': 1, 'b': 2 }", "[1, 2, 3]", "array { 1, 2 }",
  "$m?key", "$a?1", "$a?*", "?name", "(1, 2, 3)", "()", ".", "div div div", "a (: comment :) + b",
  "element(foo)", "attribute(*, xs:string)", "document-node(element(x))", "processing-instruction(pi)"
]
for (c in cases) c ++ "  =>  " ++ format(parse(c, xpath) ^ { "ERROR " ++ ^.message }, "mark") ++ "\n"

// the spec's restrictions: comparisons and ranges do not chain, adjacent
// operands need an operator, a for needs its return
let errors = ["1 < 2 < 3", "1 to 2 to 3", "a b", "for $x in 1", "f(", "$"]
for (e in errors) e ++ "  =>  " ++ (parse(e, xpath) ^ { ^.message }) ++ "\n"

// a rule is a type (GM5.4: bind it, then test membership); spans include
// hidden tokens such as `$`
let step = xpath.AxisStep
;["child::a" is step, "1 + 2" is step]
format(parse("$x + 1", xpath, {spans: true})^, "mark")
