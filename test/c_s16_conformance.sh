#!/bin/bash
# S16 Surface Syntax conformance for the PRODUCTION C recursive-descent parser.
# Mirrors test/ts_s16_conformance.sh case for case: the two front ends must
# accept and reject exactly the same language (vibe/Lambda_Design_Syntax.md 4.4).
# Accept/reject pairs for S16.1-S16.6 and the vibe/Lambda_Design_Syntax.md
# section 7 audit rulings. Run: ./test/ts_s16_conformance.sh
# S16 + §7 conformance harness. Each case: expected(A=accept,R=reject) :: source
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
G="$ROOT/lambda/tree-sitter-lambda"
WORK="$ROOT/temp/tsx"
mkdir -p "$WORK"
pass=0; fail=0
run() {
  local exp="$1" name="$2" src="$3"
  printf '%b' "$src" > "$WORK/case.ls"
  out=$("$ROOT/lambda.exe" "$WORK/case.ls" --no-log 2>&1)
  # Match the PARSE error code only: a script can parse fine and still fail at
  # run time (an unresolved import, say), which must not read as a rejection.
  if echo "$out" | grep -qE 'error\[E100\]'; then got=R; else got=A; fi
  if [ "$got" = "$exp" ]; then pass=$((pass+1)); printf '  ok   %-42s [%s]\n' "$name" "$exp";
  else fail=$((fail+1)); printf 'FAIL   %-42s exp=%s got=%s\n' "$name" "$exp" "$got"; fi
}
echo "--- S16.1/16.2 separation & line-start ---"
run A "juxtapose let/let"            'let x = 1 let y = 2\nx + y\n'
run A "trailing op continues"        '1 +\n2\n'
run A "pipe at line start"           'let d = [1]\nd\n|> len(~)\n'
run A "gt at line start"             'let a = 1\na\n> 0\n'
run A "and at line start"            'let a = 1\na\nand 1\n'
run A "fluent .ident( chain"         'let d = [1]\nd\n.len()\n'
run A "explicit ; separator"         '{ 1; 2 }\n'
run A "return greedy over newline"   'pn main() {\nreturn\n42\n}\n'
run R "line-start ( is dual-role"    'let a = 1\na\n(1 + 2)\n'
run R "line-start + is dual-role"    'let a = 1\na\n+ 2\n'
run R "line-start [ is dual-role"    'let a = [1]\na\n[0]\n'
run A "fluent chain, any member (7.15)"  'let m = {a: 1}\nm\n.a\n'
run R "line-start .5 stays dual-role"    'let a = 1\na\n.5\n'
run A "relative path is backslash-dot"   'let p = \\.a.b\np\n'
run R "line-start < is dual-role"    'let x = 1\nx\n<b "hi">\n'
run R "trailing ; banned"            '{ 1; 2; }\n'
run R "empty stmt slot banned"       '{ 1; ; 2 }\n'
run R "trailing , banned (array)"    '[1, 2,]\n'
run R "empty , slot banned (call)"   'f(1, , 2)\n'
echo "--- 7.14 closed-tail juxtaposition ---"
run A "closed: fn body then [ line"      'fn a(x) { 1 }\n[1, 2]\n'
run A "closed: fn body then ( line"      'fn a(x) { 1 }\n(1 + 2)\n'
run A "closed: bare if then [ line"      'if 1 { 2 }\n[3]\n'
run A "closed: while then [ line"        'pn main() { while (1) { 2 }\n[3] }\n'
run A "closed: match then [ line"        'match 1 { case int: 1 }\n[2]\n'
run R "open: let tail then [ line"       'let x = [5]\n[0]\n'
run R "open: type alias then [ line"     'type T = int\n[3]\n'
run R "open: if-else expr then [ line"   'if (1) 2 else 3\n[4]\n'
run R "open: map primary then [ line"    '{a: 1}\n["a"]\n'
echo "--- S16.4 / 5.9v3 braces ---"
run A "map in expr position"         'let m = {a: 1}\n'
run A "block expression"             'let v = { let y = 1 y + 1 }\n'
run A "arrow block body"             'let f = (x) => { let y = x + 1 y }\n'
run A "arrow map body"               'let f = (x) => {a: x}\n'
run A "empty braces"                 'let m = {}\n'
run A "paren-if map body"            'let r = if (1) {a: 1} else {b: 2}\n'
run A "bare-if map body (no flip)"   'if 1 {a: 1}\n'
echo "--- S16.6 control forms ---"
run A "if paren + expr body"         'let r = if (1) 2 else 3\n'
run A "if bare + braced body"        'if 1 { 2 }\n'
run A "if without else"              'if 1 { 2 }\n'
run A "while paren"                  'pn main() { while (1) { 2 } }\n'
run A "while bare"                   'pn main() { while 1 { 2 } }\n'
run A "for paren"                    'for (x in [1]) x\n'
run A "for bare braced"              'for x in [1] { x }\n'
run A "match single form"            'let r = match 1 { case int: 1 default: 2 }\n'
run A "bare-cond ( commits to paren form"  'if (1+2)*2 { 3 }\n'
echo "--- §7 audit rulings ---"
run R "unary ! removed"              'let a = !true\n'
run A "not is the negation"          'let a = not true\n'
run A "not binds loose"              'let a = not 1 == 2\n'
run A "numeric separators"           'let a = 1_000_000\n'
run A "hex separators"               'let a = 0xFF_FF\n'
run A "sized float int spelling"     'let a = 1f32\n'
run A "unary + kept"                 'let a = +5\n'
run A "pub let modifier"             'pub let x = 1\n'
run R "bare pub x = 1 removed"       'pub x = 1\n'
run A "spread is *"                  'let a = [1]\nlet b = [*a, 3]\n'
echo "--- §7.11 element / object separators ---"
run R "obj-type ';' divider retired"     'type E { a: int; fn r() { 1 } }\n'
run A "paren island lifts element scope" '<div a: (1 > 0)>\n'
run A "attrs, boundary comma, content"   '<div class: "x", id: "y", "text">\n'
run A "tag to content juxtaposition"     '<div "str">\n'
run A "boundary comma optional"          '<div a: 1, b: 2, "text">\n'
run A "no-attr element takes no comma"   '<svg \\.rect>\n'
run R "leading comma with no attrs"      '<svg, \\.rect>\n'
run R "attrs+content need the comma"     '<div a: 1 "text">\n'
run A "greedy attr value without comma"  '<div a: 1>\n'
run A "qualified tag maximal munch"      '<svg .rect>\n'
run R "attr comma required"              '<div a: 1 b: 2>\n'
run A "object type one comma list"       'type E { a: int, that ~.a > 0, fn r() { 1 } }\n'
# Precedence is invisible to an accept/reject suite: `not 1 == 2` parses either
# way and only its VALUE reveals the tier. §7.2 shipped un-implemented in this
# parser for exactly that reason, so the loose-`not` rulings assert results.
runval() {
  local name="$1" want="$2" src="$3"
  printf '%b' "$src" > "$WORK/case.ls"
  got=$("$ROOT/lambda.exe" "$WORK/case.ls" --no-log 2>&1 | tail -1)
  if [ "$got" = "$want" ]; then pass=$((pass+1)); printf '  ok   %-42s [=%s]\n' "$name" "$want";
  else fail=$((fail+1)); printf 'FAIL   %-42s want=%s got=%s\n' "$name" "$want" "$got"; fi
}
echo "--- §7.2 not binds loose (value checks) ---"
runval "not 1 == 2  is not (1 == 2)"     'true'  'not 1 == 2\n'
runval "not 1 in [1,2] is not (1 in ..)" 'false' 'not 1 in [1,2]\n'
runval "not a and 1 is (not a) and 1"    'false' 'let a = 1\nnot a and 1\n'
echo "--- corner: line-start continuation & guards ---"
run A "unfinished call bracket continues"  'fn f(x) { x }\nlet v = f(\n1)\n'
run A "unfinished index bracket continues" 'let m = {a: 1}\nlet v = m[\n"a"]\n'
run A "explicit ; then dual-role line"     'let m = {a: 1};\n["a"]\n'
run A "trailing operator continues"        'let a = 1 +\n2\n'
run A "continuation word opens line"       'let a = 1\nlet b = a\nand 1\n'
run A "pipe opens line"                    'let d = [1,2]\nlet n = d\n|> len(~)\n'
run R "line-start * is dual-role"          'let a = [1]\na\n*a\n'
run R "line-start ^ is dual-role"          'fn f() { 1 }\nlet r = f()\n^ { 0 }\n'
run R "line-start / is dual-role"          'let a = 4\na\n/ 2\n'
run A "comment between statements"         'let a = 1\n// note\nlet b = 2\n'
run A "block comment inside expression"    'let a = 1 /* c */ + 2\n'
run R "comment cannot rescue line-start +"  'let a = 1\n/* c */ + 2\n'
run R "line comment cannot rescue either"   'let a = 1\n// c\n+ 2\n'
run A "comment carry does not go stale"     'let a = 1\n/* c */ and 1 + 2\n'
run A "comment inside array literal"        'let a = [1, /* x */ 2]\n'
run A "leading file comment"                '// header\nlet a = 1\na\n'
echo "--- corner: 7.15 dot handling ---"
run A "fluent chain bare member"           'let m = {a: 1}\nlet v = m\n.a\n'
run A "fluent chain call"                  'let d = [1]\nlet n = d\n.len()\n'
# LR02-11: the member name may be a type keyword. `token_is_key` is the shared
# admitted set, so these must track `parse_path_segment`, not LAMBDA_TOK_IDENTIFIER.
run A "fluent chain, type-keyword member"  'let n = 42\nlet s = n\n.string()\n'
run A "fluent chain, .int() member"        'let s = "12"\nlet n = s\n.int()\n'
run A "fluent chain, .map( member"         'let d = [1]\nlet r = d\n.map((x) => x)\n'
run R "line-start .digit stays dual-role"  'let a = 1\na\n.5\n'
run A "relative path statement"            'let p = \\.a.b\np\n'
run A "rooted path statement"              'let p = /.a.b\np\n'
run R "bare-dot relative path retired"     'let p = .a.b\np\n'
echo "--- corner: 5.9v3 braces ---"
run A "empty braces as value"              'let m = {}\nm\n'
run A "handler brace same line"            'fn f() { 1 }\nlet r = f() ^ { 0 }\nr\n'
run R "handler brace on next line"         'fn f() { 1 }\nlet r = f() ^\n{ 0 }\nr\n'
run A "bare propagate then ; then block"   'fn f() { 1 }\nlet r = f()^;\n{ 0 }\n'
run A "empty braces in fn control body"    'let r = if (1) {} else {}\nr\n'
run R "bare {} statement in pn is dead"    'pn main() { {} }\n'
run A "empty map value in pn is fine"      'pn main() { let m = {} m }\n'
run A "block expression value"             'let v = { let y = 1 y + 1 }\nv\n'
run A "nested block expressions"           'let v = { let y = { 1 } y }\nv\n'
run A "arrow empty body"                   'let f = (x) => {}\nf(1)\n'
run A "if paren block body"                'let r = if (1) { 2 } else { 3 }\nr\n'
echo "--- corner: control forms ---"
run A "nested dangling else"               'let r = if (1) if (0) 2 else 3 else 4\nr\n'
run A "for bare with clauses"              'for x in [1,2] where x > 0 { x }\n'
run A "for paren with clauses"             'let r = for (x in [1,2] where x > 0) x\nr\n'
run R "bare while cond opening with ("     'pn main() { while (1)*2 { 3 } }\n'
echo "--- corner: 7.14 closed vs open tail ---"
run R "open: arrow body then ( line"       'fn f() => 1\n(2)\n'
run A "closed: view decl then [ line"      'view P: int { 1 }\n[0]\n'
run A "closed: object type then [ line"    'type E { a: int }\n[0]\n'
run A "closed: import then [ line"         'import math\n[1, 2]\n'
run A "closed: import then ( line"         'import math\n(1)\n'
run A "import list still uses ,"           'import math, sys\n1\n'
run A "dotted module continues a line"     'import math\n.sub\n1\n'
echo "--- corner: 7.4 / 7.16 numerics ---"
run A "separators in float"                'let a = 1_000.5_5\na\n'
run A "separators in exponent"             'let a = 1e1_0\na\n'
run A "hex with separators"                'let a = 0xFF_FF\na\n'
run R "number runs into identifier"        'let a = 123abc\n'
run R "binary-literal habit rejected"      'let a = 0b1010\n'
run R "trailing underscore"                'let a = 1_\n'
echo "--- corner: element scope (S16.5.1) ---"
run A "element then following statement"   '<p "x">\n1\n'
run A "let-bound element then statement"   'let e = <p "x">\n1\n'
run A "nested element children"            '<div <b "x"> <i "y">>\n'
run A "element with no content"            '<p>\n1\n'

echo
echo "pass=$pass fail=$fail"
