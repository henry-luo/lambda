#!/bin/bash
# S16 Surface Syntax conformance for the Tree-sitter reference grammar.
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
  out=$(cd "$G" && npx tree-sitter parse "$WORK/case.ls" 2>&1)
  if echo "$out" | grep -qE 'ERROR|MISSING|Unexpected'; then got=R; else got=A; fi
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
echo
echo "pass=$pass fail=$fail"
