// M5: lambda.grammar.xquery parses XQuery 3.1 into a Lambda element tree,
// composed over lambda.grammar.xpath (§4.5). Direct constructors use `~`
// (GM13): their text keeps its whitespace, `{ … }` inside skips it. One line
// per query.
import lambda.grammar.xquery

let cases = [
  "1 + 2",
  "for $b in //book where $b/price > 10 order by $b/title descending empty least return $b/title",
  "let $s := (1, 2) let $t as xs:integer := 4 for $i allowing empty at $p in $s count $c group by $k := $i mod 2 return ($k, $c)",
  "for tumbling window $w in (1 to 10) start $s when true() only end $e when $e - $s eq 2 return $w",
  "for sliding window $w in $x start at $i previous $p when 1 end next $n when 2 return $w",
  "for $x in $s stable order by $x ascending collation \"c\" return $x",
  "some $x as xs:integer in (1, 2) satisfies $x gt 1",
  "switch ($x) case 1 case 2 return \"low\" default return \"high\"",
  "typeswitch ($n) case $e as element() return 1 case text() | comment() return 2 default $d return 3",
  "try { error() } catch err:FOER0000 | * { 0 }",
  "<a>x {1} y</a>", "<p/>", "<a>  </a>", "<a b=\"1\" c='x''y' d=\"{1}-{{}}\">t &amp; &#x41; {{</a>",
  "<book isbn=\"{ $b/@isbn }\">\n  <title>{ $b/title/text() }</title>\n  <!-- c --><?pi data?><![CDATA[<raw>]]>\n</book>",
  "$x/<a/>", "1 < 2",
  "element { \"x\" } { attribute id { 1 }, text { \"t\" } }", "document { <r/> }", "namespace p { \"urn:p\" }",
  "processing-instruction pi { \"d\" }", "comment { \"c\" }", "element div { () }",
  "``[Hello `{ $name }`, you are `{ $age }` years]``", "``[plain ] text]``", "``[a]]`` || ``[b]``",
  "(# ext:p some content #) { 1 }", "validate strict { $doc }", "validate type xs:int { 1 }", "ordered { $a }", "unordered { $a }",
  "%public function($a) { $a }",
  "xquery version \"3.1\";\ndeclare namespace f = \"urn:f\";\ndeclare variable $x as xs:integer := 3;\ndeclare %private function f:inc($n as xs:integer) as xs:integer { $n + 1 };\nf:inc($x)",
  "module namespace m = \"urn:m\"; import module namespace x = \"urn:x\" at \"x.xq\"; declare function m:id($x) { $x };",
  "declare boundary-space preserve; declare default element namespace \"urn:e\"; declare copy-namespaces no-preserve, inherit; declare option o:x \"y\"; declare context item as node() external; .",
  "declare decimal-format d decimal-separator = \",\"; declare variable $v external := 1; $v",
  "xquery encoding \"utf-8\"; 1 (: a comment :) + 2"
]
for (c in cases) c ++ "  =>  " ++ format(parse(c, xquery) ^ { "ERROR " ++ ^.message }, "mark") ++ "\n"

// a lone `}` in content, space after `<` or `</`, a setter after a
// declaration, a FLWOR without return
let errors = ["<a>}</a>", "< a/>", "<a></ a>", "declare variable $x := 1; declare namespace f = \"u\"; 1", "for $x in 1"]
for (e in errors) e ++ "  =>  " ++ (parse(e, xquery) ^ { ^.message }) ++ "\n"
