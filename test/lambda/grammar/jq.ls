// M4: lambda.grammar.jq parses jq 1.7.1 into a Lambda element tree, ported
// from jq's bison grammar: bison's precedence declarations are the levels of
// one left-recursive Exp; Term is the postfix chain. One line per program.
import lambda.grammar.jq

let cases = [
  ".", "..", ".a.b", ".a .b", ".\"x y\"", ".[0]", ".a.[0]", ".[2:4]", ".[:-1]", ".[]?", ".a?.b?", "$x[0]",
  "1.5e3", ".5", "\"\"", "\"a\\\"b \\u00e9\"", "\"x \\(.a + 1) y\"", "@base64 \"a\\(.)\"", "@csv", "$__loc__", "$__loc__x",
  "1 + 2 * 3", "-1 + 2", "-1 * 2", "-.a?", "1, 2 | . * 10", ".a // .b // .c", ".a |= . + 1", "a and b or c",
  ".[] | select(.age > 30) | {name, age}", "{a: .x | .y, \"b\": 1, (.k): 2, $v, if: 3, and}", "[.[] | . * 2]", "[]",
  "def f: 1; f | g", "def f(g; $x): g + $x; f(.a; 1)", "def f: 1; def g: 2;",
  "reduce .[] as $x (0; . + $x)", "foreach .[] as [$a, $b] (0; . + $a; [., $b])",
  "if . == 0 then \"zero\" elif . == 1 then \"one\" else \"many\" end", "if . then 1 end",
  "try error(\"x\") catch .", "try .a | .b", "try (1/0) catch . | length", ".a?",
  ". as [$a, {b: $c}] | $a", ". as {a: $x} ?// [$x] | $x", "label $out | .[] | if . > 1 then break $out else . end",
  "import \"m\" as m; m::f", "include \"lib\"; f", "module {name: \"x\"}; def f: 1;",
  "# comment\n.a # trailing\n| .b", ""
]
for (c in cases) c ++ "  =>  " ++ format(parse(c, jq) ^ { "ERROR " ++ ^.message }, "mark") ++ "\n"

// bison's %nonassoc refuses chained comparisons and assignments; an object
// value is jq's ExpD (no arithmetic without parentheses); reduce is an Exp in
// jq 1.7.1, so it takes no suffix
let errors = ["1 == 2 == 3", ".a = 1 = 2", "{a: 1 + 2}", ". as $x", "reduce .[] as $x (0; .)[0]", ". a", "{@base64}"]
for (e in errors) e ++ "  =>  " ++ (parse(e, jq) ^ { ^.message }) ++ "\n"
