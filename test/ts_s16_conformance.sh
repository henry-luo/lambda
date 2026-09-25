#!/bin/bash
# S16 Surface Syntax conformance for the Tree-sitter reference grammar.
# Accept/reject pairs for S16.1-S16.6 and the vibe/Lambda_Design_Syntax.md
# section 7 audit rulings. Run: ./test/ts_s16_conformance.sh
# S16 + §7 conformance harness. Each case: expected(A=accept,R=reject) :: source
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
G="$ROOT/lambda/tree-sitter-lambda"
WORK="$ROOT/temp/tsx"
# The project-pinned CLI, as the Makefile runs it; `npx` may query the registry.
TS_CLI="$ROOT/node_modules/.bin/tree-sitter"
# The CLI caches compiled grammars by language name, so a cache shared with
# another checkout could serve that checkout's parser.
export TREE_SITTER_LIBDIR="$ROOT/temp/tree-sitter-lib"
mkdir -p "$WORK"
pass=0; fail=0
no_tree_shown=0
run() {
  local exp="$1" name="$2" src="$3"
  printf '%b' "$src" > "$WORK/case.ls"
  out=$(cd "$G" && "$TS_CLI" parse "$WORK/case.ls" 2>&1)
  # A verdict needs a parse tree. Without one the CLI never parsed the case --
  # the grammar failed to compile (a stale src/parser.c, or a CLI older than
  # the grammar's ABI) or the CLI itself failed -- and the error grep below
  # would read that as an accept. Such a case fails; the cause prints once.
  if ! printf '%s\n' "$out" | grep -qE '^\([A-Za-z_]+ \[[0-9]+, [0-9]+\] - \['; then
    fail=$((fail+1)); printf 'FAIL   %-42s exp=%s got=no parse tree\n' "$name" "$exp"
    if [ "$no_tree_shown" = 0 ]; then
      no_tree_shown=1
      printf '%s\n' "$out" | grep -v '^[[:space:]]*$' | head -6 | sed 's/^/       | /'
    fi
    return
  fi
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
# §7.17 is a KNOWN LIMITATION of the reference grammar: a comment between an
# expression and a line-start dual-role token defeats the guard here (the C
# parser, which is production, rejects correctly). Two attempts at scanner-owned
# comments failed — see the design doc — so no assertion is made for it.
run A "comment inside array literal"        'let a = [1, /* x */ 2]\n'
run A "leading file comment"                '// header\nlet a = 1\na\n'
# §7.17: these two are asserted in the C harness only — the reference grammar
# cannot see a line break tree-sitter already consumed as an extra.
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
echo "--- corner: S16.6.6/7 statement bodies need braces ---"
run R "if body bare return"          'pn main() {\nlet k = 1\nif (k == 2) return -1\n1\n}\n'
run R "else body bare return"        'pn main() {\nlet k = 1\nif (k == 2) { 0 } else return -1\n1\n}\n'
run R "if body bare break"           'pn main() {\nvar i = 0\nwhile (i < 3) { i = i + 1; if (i == 2) break }\ni\n}\n'
run R "for body bare return"         'pn main() {\nfor (x in [1,2]) return x\n0\n}\n'
run R "case colon bare return"       'pn main() {\nmatch 1 { case int: return 99 default: 0 }\n}\n'
run R "arrow body bare return"       'let f = (x) => return x\nf(1)\n'
run A "identifier with keyword prefix" 'let returnValue = 1\nlet r = if (returnValue == 1) returnValue else 0\nr\n'
run A "arrow body with binop tail"   'let f = (x, y) => x > y\nf(3, 1)\n'
run R "pn arrow expr body"           'pn p() => 1\npn main() { p() }\n'
run R "pn arrow braced body"         'pn p() => { 1 }\npn main() { p() }\n'
run A "if braced return"             'pn main() {\nlet k = 1\nif (k == 1) { return 7 }\n0\n}\n'
run A "case colon braced block"      'pn main() {\nmatch 1 { case int: { return 99 } default: 0 }\n}\n'
run A "if body raise is expression"  'pn main() {\nlet k = 1\nlet r = if (k == 2) raise error("x") else 5\nr\n}\n'
run A "pn braced body"               'pn p() { 1 }\npn main() { p() }\n'
run R "line-start .digit stays dual-role"  'let a = 1\na\n.5\n'
run A "relative path statement"            'let p = \\.a.b\np\n'
run A "rooted path statement"              'let p = /.a.b\np\n'
run R "bare-dot relative path retired"     'let p = .a.b\np\n'
echo "--- corner: S2.4 path roots and steps ---"
run A "bare relative root"                 'let p = \;\n[p]\n'
run A "bare rooted root"                   'let p = /;\n[p]\n'
run R "trailing dot after relative root"   'let p = \\.;\n[p]\n'
run R "empty rooted step"                  'let p = /.;\n[p]\n'
run R "empty step after relative root"     'let p = \\..a;\n[p]\n'
run A "relative leading integer key"       'let p = \\.1.x;\n[p]\n'
run A "rooted leading integer key"         'let p = /.1.2;\n[p]\n'
run A "rooted index step"                  'let p = /[1];\n[p]\n'
run A "relative index step"                'let p = \\[1].x;\n[p]\n'
run A "key step after an index step"       'let p = \\[1].name;\n[p]\n'
run R "dot before a rooted index step"     'let p = /.[1];\n[p]\n'
run R "dot before a relative index step"   'let p = \\.[1].x;\n[p]\n'
run A "integer key after a root step"      'let p = file./.1;\n[p]\n'
run A "integer keys in a member chain"     'let d = [[1, 2], [3, 4]];\n[d.1.0]\n'
run A "return a relative path"             'pn main() {\nreturn \\.a\n}\n'
run A "trailing dot continues a path"      'let p = \\.\nb;\n[p]\n'
run A "line-start .~~ continues a path"    'let p = \\.a\n.~~;\n[p]\n'
run A "line-start ./ continues a path"     'let p = \\.a\n./;\n[p]\n'
run A "line-start .** continues a path"    'let p = \\.a\n.**;\n[p]\n'
run R "line-start .digit after a path"     'let p = \\.a\n.1;\n[p]\n'
run R "line-start [ after a path"          'let p = \\.a\n[1];\n[p]\n'
run A "relative path after an import"      'import math\n\\.a\n'
run R "retired /a spelling"               'let p = /b;\n[p]\n'
run R "root pressed against a number"      'let p = /1;\n[p]\n'
run R "query pressed against a root"       'let p = /.?int;\n[p]\n'
run R "slash as an import separator"       'import .a/b\n'
run R "backslash as an import separator"   'import .a\\b\n'
run R "backslash relative import"          'import \\a\n'
run A "rooted path line after an import"   'import math\n/.a\n'
echo "--- corner: 5.9v3 braces ---"
run A "empty braces as value"              'let m = {}\nm\n'
run A "handler brace same line"            'fn f() { 1 }\nlet r = f() ^ { 0 }\nr\n'
run R "handler brace on next line"         'fn f() { 1 }\nlet r = f() ^\n{ 0 }\nr\n'
run A "bare propagate then ; then block"   'fn f() { 1 }\nlet r = f()^;\n{ 0 }\n'
run A "empty braces in fn control body"    'let r = if (1) {} else {}\nr\n'
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

echo "--- corner: references and Tier 3 (PTH30-PTH80) ---"
run A "force step"                         'let p = /.a\np#\n'
run A "force fragment sugar"               'let p = /.a\np#name\n'
run A "force then member"                  'let p = /.a\np#.name\n'
run A "force at line start continues"      'let p = /.a\np\n#name\n'
run A "reference equality"                 'let a = 1\nlet b = (a === a)\nb\n'
run A "=== at line start continues"        'let a = 1\nlet b = a\n=== a\nb\n'
run A "address-of"                         'let a = [1]\nlet r = &a\nr\n'
run A "address-of binds looser"            'let a = {b: 1}\nlet r = &a.b\nr\n'
run R "line-start & is dual-role"          'let a = [1]\nlet b = a\n&b\n'
run A "infix & still continues a line"     'let t = int\n| string\n1\n'
run A "put upsert"                         'let d = temp("t")\nput d.a = 1\n'
run A "put before / after"                 'let d = temp("t")\nput 1 before d.a, 2 after d.b\n'
run A "put into"                           'let d = temp("t")\nput 1 into d.a\n'
run A "del one location"                   'let d = temp("t")\ndel d.a\n'
run A "comma-joined edits"                 'let d = temp("t")\nput d.a = 1, d.b = 2\n'
run A "commit and rollback"                'let d = temp("t")\ncommit\nrollback\n'
run R "commit( is not a call"              'commit()\n'
run A "edit_commit() is the sys call"      'edit_commit()\n'
run A "open block with alias"              'let d = temp("t")\nopen v = d { put v.a = 1; commit }\n'
run A "open block with no alias"           'let d = temp("t")\nopen d { commit }\n'
run A "open closes: [ on next line"        'let d = temp("t")\nopen d { commit }\n[0]\n'
run A "open is still a data name"          'let m = {open: true}\nm.open\n'
run A "del is still a data name"           '<del "x">\n'

echo "--- S11.1.1v3 / S11.1.6v2 type suffix chains ---"
run A "counted ranks chain"                'type G = int[2][3]\n'
run A "open rank over a counted rank"      'type G = int[][3]\n'
run A "three ranks"                        'type G = int[2][1][1]\n'
run A "nullable array"                     'type G = int[]?\n'
run A "nullable array of nullables"        'type G = int?[]?\n'
run A "array of nullable arrays"           'type G = int[]?[]\n'
run A "rank chain in an annotation"        'let g: int[2][3] = [[1, 2], [3, 4], [5, 6]]\n'
run R "two ? never meet"                   'type G = int??\n'
run R "no ? after a nullable array"        'type G = int[]??\n'
run R "no run count after an array"        'type G = int[]+\n'
# Type_Pattern §1.3 no-chaining rule (USER 2026-09-24): only `?` or an array
# suffix may precede an array suffix; an array of runs is grouped.
run R "no array after a run"               'type G = int+[]\n'
run R "no array after a star run"          'type G = int*[]\n'
run R "no array after a counted run"       'type G = int{2}[]\n'
run A "a grouped run takes an array"       'type G = (int+)[]\n'
# S11.1.6v2 conformance note: a count binds tight and holds an integer, as in
# C's parser_at_counted_run; any other brace after a type is a new statement.
run A "spaced map after a type alias"      'type T = int {a: 1}\n1\n'
run A "tight map after a type alias"       'type T = int{a: 1}\n1\n'
# S16.2.3: `+` and `*` are dual-role in type space too.
run R "line-start + ends a type"           'type T = int\n+ 1\n'
run R "line-start * ends a type"           'type T = int\n* 1\n'

echo "--- S11.1.6v2 / S11.1.5 the right side of is is a type ---"
run A "is takes a nullable type"           'let a = 1 is int?\n'
run A "is takes a nullable type in parens" 'let a = (1 is int?)\na\n'
run A "is takes an array type"             'let a = [1] is int[]\n'
run A "is takes a rank chain"              'let a = [[1]] is int[][]\n'
run A "is takes a nullable array"          'let a = null is int[]?\n'
run A "is takes an array of nullables"     'let a = [1] is int?[]\n'
run A "is takes a counted run"             'let a = [1, 2] is int{2}\n'
run A "is takes a negated type"            'let a = 1 is !int\n'
run A "is takes the type keyword"          'let a = 1 is type\n'
run A "is takes a bare fn colour"          'let f = (x) => x\nlet a = f is fn\n'
run A "match arm takes a bare fn colour"   'let f = (x) => x\nmatch f { case fn: 1 default: 0 }\n'
run A "a bare pn colour is a type"         'type P = pn\n1\n'
run R "is rejects a negative value"        'let a = 1 is -1\n'
run R "is rejects a call"                  'let x = 3\nlet a = x is type(x)\n'
run A "spaced brace after an is type: body" 'let x = 3\nif x is int { 1 }\n'

echo "--- S11.1.1v3 / S11.1.6v2 return types ---"
run A "pn return type then body"           'pn f(k: int) int { return k }\npn main() { f(1) }\n'
run A "fn return type then body"           'fn f(k: int) int { k }\nf(1)\n'
run A "tight brace after a return: body"   'fn f() int{1}\nf()\n'
run A "fn type return takes a count"       'fn h(f: fn (x: int) int{2}) { 1 }\nh\n'
run A "return type rank chain"             'fn f() int[][] { [[1]] }\nf()\n'
run A "return type array of nullables"     'fn g() int?[] { [1] }\ng()\n'
run A "return type nullable array"         'fn h() int[]? { null }\nh()\n'
run A "error arm after a chained return"   'fn e() int[]? ^ error { null }\ne()\n'
run R "no run suffix after a return array" 'fn f() int[]+ { [1] }\nf()\n'

echo "--- S2.5.1v2 / S2.5.4 / S2.5.5v2 list literals (LR02-18) ---"
run A "list literal"                       'let a = (1, 2)\na\n'
run A "list literal statement"             '(1, 2)\n'
run A "empty list is null"                 'let a = ()\na\n'
run A "let-group keeps every item"         'let a = (let x = 1, x, 2)\na\n'
run A "let item after an item"             'let a = (1, let x = 2, x)\na\n'
run A "declaration-only group"             'let a = (let x = 1, let y = 2)\na\n'
run A "decomposition in a list"            'let a = (let p, q = [1, 2], p + q)\na\n'
run R "each binding takes its own let"     'let a = (let p = 1, q = 2, p + q)\na\n'
run R "trailing , banned (list)"           'let a = (1, 2,)\na\n'
run R "empty , slot banned (list)"         'let a = (1, , 2)\na\n'
run A "lists in lists"                     'let a = ((1, 2), (3, 4))\na\n'
run A "list spans lines"                   'let a = (\n1,\n2\n)\na\n'
run R "line-start ( list is dual-role"     'let a = 1\n(2, 3)\n'
run A "closed: fn body then list line"     'fn g() { 1 }\n(1, 2)\n'
run A "arrow body is a list"               'let f = (x) => (x, x)\nf(1)\n'

echo "--- arrow heads are parameter lists ---"
run A "untyped head with return type"      'let f = (x) int => x\nf(1)\n'
run A "two-name head with return type"     'let f = (a, b) int => a\nf(1, 2)\n'
run A "empty head with return type"        'let f = () int => 1\nf()\n'
run A "mixed head with return type"        'let f = (x, y: int) int => x\nf(1, 2)\n'
run A "return type opens the next line"    'let f = (x)\nint => x\nf(1)\n'
run R "head of values is no arrow"         'let f = (1, 2) => 3\n'
run R "head of a member is no arrow"       'let f = (a.b) => 1\n'

echo "--- S2.5.4v2 let binds only as a list item ---"
run R "no let in an array literal"         'let a = [let x = 1, x]\na\n'
run R "no let as an argument"              'let f = (x) => x\nlet a = f(let x = 1)\na\n'
run R "no let as a binding value"          'let a = let b = 2\na\n'
run R "no let as an operand"               'let a = 1 + let x = 2\na\n'
run R "no let as an if condition"          'let a = if (let x = 1) 2 else 3\na\n'
run R "let is never a name"                'let a = let\n'
run A "let still names an argument"        'let f = (x) => x\nlet a = f(let: 1)\na\n'
run A "let clause in a for head"           'let a = [for (x in [1, 2], let y = x * 2) y]\na\n'

echo "--- S16.9.7 a rest parameter closes a parameter list ---"
run A "rest-only arrow head"               'let f = (...) => len(varg())\nf(1, 2)\n'
run A "arrow head ends with a rest"        'let f = (a, ...) => a\nf(1, 2)\n'
run A "rest head with a return type"       'let f = (...) int => 1\nf()\n'
run A "a bare ... is still an item"        'let a = (...)\na\n'
run R "rest parameter comes last"          'let f = (..., a) => a\n'
run R "one rest parameter"                 'let f = (..., ...) => 1\n'
run R "rest last in a declaration"         'fn f(..., a) { 1 }\n'
run R "no leading , in parameters"         'fn f(, a) { 1 }\n'

echo "--- S11.1.5v2 / S16.2.3v3 function-type signatures ---"
run R "fn type spells its parameters"      'fn h(f: fn int) { 1 }\n'
run R "no fn T shorthand in a bracket"     'type T = [fn int]\n'
run A "empty signature with a return"      'fn h(f: fn () int) { 1 }\nh\n'
run A "signature without a return type"    'fn h(f: fn ()) { 1 }\nh\n'
run A "parameters without a return type"   'fn h(f: fn (x: int)) { 1 }\nh\n'
run A "a signature returns a signature"    'type F = fn (x: int) fn (y: int) int\n1\n'
run R "return type only on the ) line"     'type F = fn ()\nint\n'
run A "; ends a bare signature"            'let x = 1\ntype F = fn ();\nx\n'
run A "a declaration follows a signature"  'type F = fn ()\nfn g() { 1 }\n'
run A "a brace after a signature: body"    'let f = (x) => x\nif f is fn () { 1 }\n'
run R "no trailing , in a signature"       'type F = fn (x: int,) int\n'
run R "no leading , in a signature"        'type F = fn (, x: int) int\n'
run A "fn type in a union after a type"    'type T = int | fn (x: int) int\n1\n'
run A "a statement keyword ends a signature" 'type F = fn ()\nlet y = 2\ny\n'
run R "a signature takes no suffix"        'type F = fn (x: int)?\n1\n'
run R "a bare fn takes no suffix"          'type F = fn[]\n1\n'
run A "a grouped signature takes a suffix" 'type F = (fn (x: int))?\n1\n'
run A "the return type keeps its suffix"   'type F = fn () int?\n1\n'

echo "--- S16.10 keywords: bindings never, data names yes ---"
run R "no var as a value"                  'let a = var\n'
run R "no pub as a value"                  'let a = pub\n'
run R "no put as a value"                  'let a = put\n'
run R "no while as a value"                'let a = while\n'
run R "no pn as a value"                   'let a = pn\n'
run R "no var as an array item"            'let a = [var, 1]\n'
run R "an arrow body cannot break"         'let f = (x) => break\n'
# C still reads these four as values (Lambda_Issue_Ledger LR02-21), so they
# are not mirrored in c_s16_conformance.sh yet.
run R "no fn as a value"                   'let a = fn\n'
run R "no view as a value"                 'let a = view\n'
run R "no state as a value"                'let a = state\n'
run R "no apply as a value"                'let a = apply\n'
run R "keyword let name"                   'let if = 1\n'
run R "type-word let name"                 'let type = 1\n'
run R "base-type let name"                 'let int = 1\n'
run R "named-value let name"               'let true = 1\n'
run R "Tier-3 let name"                    'let put = 1\n'
run R "reserved state let name"            'let state = 1\n'
run R "keyword fn name"                    'fn if() { 1 }\n'
run R "keyword parameter"                  'fn f(x: int, while) { 1 }\n'
run R "keyword type name"                  'type if = int\n'
run R "keyword loop name"                  'let a = [1]\nlet z = for (if in a) 1\n'
run R "keyword import alias"               'import if: .lib.x\n'
run R "keyword event name"                 'view <div> { 1 }\non commit(e) { 1 }\n'
run A "clause-word binding reads"          'let offset = 5\nlet z = offset + 1\nz\n'
run A "continuation-word binding reads"    'let default = 4\nlet z = default + 1\nz\n'
run A "statement keywords as map keys"     'let m = {if: 1, while: 2, return: 3}\nm\n'
run A "type words as map keys"             'let m = {int: 1, type: 2}\nm\n'
run A "let and Tier-3 words as map keys"   'let m = {let: 1, put: 2, commit: 3}\nm\n'
run A "view, state, last as map keys"      'let m = {view: 1, state: 2, last: 3}\nm\n'
run A "apply, import as map keys"          'let m = {apply: 1, import: 2}\nm\n'
run R "no named-value map key"             'let m = {true: 1}\n'
run R "no not map key"                     'let m = {not: 1}\n'
run A "keyword element tag"                'let e = <if a: 1>\ne\n'
run A "base-type element tag"              'let e = <int a: 1>\ne\n'
run A "keyword attribute names"            'let e = <div let: 1, if: 2, while: 3>\ne\n'
run A "keywords in dotted names"           'let e = <svg.if xml.if: 1>\ne\n'
run A "keyword member steps"               'let m = {a: 1}\nlet z = [m.if, m.type, m.let, m.int, m.last]\nz\n'
run A "keyword fluent member"              'let m = {a: 1}\nlet z = m\n.if\nz\n'
run A "keyword named argument"             'fn f(x) { x }\nlet z = f(type: 1)\nz\n'
run A "keyword object fields"              'type T { if: int, let: int, type?: int }\n1\n'
run A "keyword element-type tag"           'type E = <if a: int>\n1\n'
run A "keyword map-type field"             'type M = {if: int}\n1\n'
run A "keyword method names"               'type T { a: int, fn if() => 1, fn state() => a, pn open() { 1 } }\n1\n'
run A "keyword module segment"             'import .lib.string\n1\n'
run A "base-type loop index type"          'let a = [1]\nlet z = for (i: int, x in a) x\nz\n'

echo "--- LR02-20/26: C parser gaps ---"
# S16.2.2v2: `?` only continues, so a line-start `?` extends a complete type
run A "line-start ? continues a type alias"   'type T = int\n?\n1\n'
run A "line-start ? continues an annotation"  'let x: int\n? = null\nx\n'
run A "line-start ? in a parameter type"      'fn f(a: int\n?) { a }\nf(null)\n'
# S2.5.1v2, S2.5.5v2: no item limit (a 65-argument call parses; the 16-argument
# source limit is a later semantic check, D6.2.2v2)
items65="$(seq -s ', ' 0 64 | sed 's/, *$//')"
attrs70="$(for i in $(seq 0 69); do printf 'a%d: %d, ' "$i" "$i"; done | sed 's/, $//')"
names70="$(for i in $(seq 0 69); do printf 'n%d, ' "$i"; done | sed 's/, $//')"
items70="$(seq -s ', ' 0 69 | sed 's/, *$//')"
run A "65-item list literal"                  "let l = ($items65)\nlen(l)\n"
run A "65 call arguments"                     "fn f(...) => len(varg())\nf($items65)\n"
run A "70 element attributes"                 "let e = <e $attrs70>\ne\n"
run A "70 decomposition names"                "let $names70 = [$items70]\nn0\n"

echo
echo "pass=$pass fail=$fail"
