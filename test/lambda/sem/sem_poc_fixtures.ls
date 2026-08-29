// Semantics POC — REAL FIXTURE SLICE (M3, Phase 2 of §6.2).
// Interpreter: sem_poc_kernel.ls. Design: vibe/Lambda_Semantics_DSL_POC.md.
//
// The other fixtures check terms this POC invented, which risks a model that is
// only ever asked what it happens to be good at. This file translates programs
// taken from the EXISTING suite — test/lambda/closure.ls and test/lambda/expr.ls
// — into terms, and checks the model against the same program run natively.
// Each check names its source, and the expectations are the ones those fixtures
// already assert (closure.ls carries `// expect:` comments and a golden of
// `[15, 37, 25, 125, 33, 10, 22, ...]`).
//
// Hand translation is the honest cost of Phase 2: the model has no ingestion
// path yet, so a human maps surface syntax to terms. Phase 3 (§6.3) is what
// would mechanise it, and every line here becomes a regression test for that.

import .sem_poc_kernel

// A parse constraint shapes how the terms below are written: an identifier in
// element-content position followed by `<` is read as a LESS-THAN, so
// `<app make_adder LIT>` (LIT an element literal) does not parse, while
// `<app make_adder a10>` and `<app <lam ...> LIT>` both do (finding F10).
// Argument terms are bound to names first — a workaround, not a modelling choice.
let a2 = <lit v:2>
let a3 = <lit v:3>
let a4 = <lit v:4>
let a5 = <lit v:5>
let a7 = <lit v:7>
let a10 = <lit v:10>
let a20 = <lit v:20>
let a100 = <lit v:100>
let a123 = <lit v:123>

// ===== test/lambda/closure.ls =====

// Test 1 — `fn make_adder(n) { fn inner(x) => x + n  inner }` then
// `make_adder(10)(5)`. A function returning a function: the term nests one
// abstraction inside another, and the inner one captures `n`. expect: 15
let make_adder = <lam p:'n', <lam p:'x', <add <vr n:'x'> <vr n:'n'>>>>
run(<app <app make_adder a10> a5>) == 15;

// Test 2 — make_affine(3, 7) then affine(10) = x * a + b. expect: 37
let make_affine = <lam p:'a', <lam p:'b', <lam p:'x',
    <add <mul <vr n:'x'> <vr n:'a'>> <vr n:'b'>>>>>
run(<app <app <app make_affine a3> a7> a10>) == 37;

// Test 3 — chained call on the returned closure: make_adder(20)(5). expect: 25
run(<app <app make_adder a20> a5>) == 25;

// Test 4 — two closures from the same factory keep INDEPENDENT environments:
// add5(10) + add100(10). This is the check that a captured environment is
// copied per closure rather than shared. expect: 125
let t4_body = <lett n:'add5', <app <vr n:'mk'> a5>
                <lett n:'add100', <app <vr n:'mk'> a100>
                 <add <app <vr n:'add5'> a10>
                      <app <vr n:'add100'> a10>>>>
run(<lett n:'mk', make_adder t4_body>) == 125;

// Test 5 — three-deep nesting, inner captures two enclosing scopes:
// make_nested(10)(20)(3) = 3 + 10 + 20. expect: 33
let make_nested = <lam p:'a', <lam p:'b', <lam p:'x',
    <add <add <vr n:'x'> <vr n:'a'>> <vr n:'b'>>>>>
run(<app <app <app make_nested a10> a20> a3>) == 33;

// Test 6 — outer(2)(3)(4) = x * y + z. expect: 10
let outer3 = <lam p:'x', <lam p:'y', <lam p:'z',
    <add <mul <vr n:'x'> <vr n:'y'>> <vr n:'z'>>>>>
run(<app <app <app outer3 a2> a3> a4>) == 10;

// Test 7 — make_scaled_adder(scale, offset) then adder(x) = x + scale * offset,
// as `make_scaled_adder(3, 4)(10)`. expect: 22
let make_scaled = <lam p:'scale', <lam p:'offset', <lam p:'x',
    <add <vr n:'x'> <mul <vr n:'scale'> <vr n:'offset'>>>>>>
run(<app <app <app make_scaled a3> a4> a10>) == 22;

// The same programs, cross-checked against the runtime evaluating the native
// source rather than against a literal — so these lines fail if EITHER side
// changes, which is the differential property the constants above lack.
fn make_adder_n(n) { fn inner(x) => x + n  inner }
run(<app <app make_adder a10> a5>) == make_adder_n(10)(5);
run(<app <app make_adder a20> a5>) == make_adder_n(20)(5);
fn outer_n(x) { fn mid(y) { fn inner(z) => x * y + z  inner }  mid }
run(<app <app <app outer3 a2> a3> a4>) == outer_n(2)(3)(4);

// ===== test/lambda/expr.ls =====

// `(let a=12, a+10)` — the file's opening line. expect: 22
run(<lett n:'a', <lit v:12> <add <vr n:'a'> a10>>) == (let a = 12, a + 10);

// From the big list literal: `(let a=0.5, let b=2, [a + 3 * b, ...])`.
// Precedence lives in the term's shape, so this also checks that the surface
// parse and the term agree about what `a + 3 * b` means.
run(<lett n:'a', <lit v:0.5>
     <lett n:'b', <lit v:2>
      <add <vr n:'a'> <mul <lit v:3> <vr n:'b'>>>>>)
    == (let a = 0.5, let b = 2, a + 3 * b);

// the arithmetic and comparison entries of that same list
run(<div <lit v:5> a2>) == (5 / 2);
run(<pow <lit v:3> a3>) == (3 ** 3);
run(<mod <lit v:17> <lit v:9>>) == (17 % 9);
run(<gt <lit v:2.5> <lit v:1>>) == (2.5 > 1);
run(<le <lit v:5> <lit v:5.0>>) == (5 <= 5.0);
run(<gt <sub <lit v:7> <lit v:3.5>> a5>) == ((7 - 3.5) > 5);
run(<cat <lit v:"hello"> <lit v:"world">>) == ("hello" ++ "world");
run(<iff <gt <lit v:100> <lit v:90>> <lit v:'great'> <lit v:'not great'>>)
    == (if (100 > 90) 'great' else 'not great');

// `[d+4, not true, not(1>2), -(-2)]` from the unary-operator section
run(<add <lit v:123> a4>) == (123 + 4);
run(<nott <lit v:true>>) == (not true);
run(<nott <gt <lit v:1> a2>>) == (not (1 > 2));
run(<neg <lit v:-2>>) == -(-2);

// `1/0; -1/0; 0/0;` — the suite already relies on division by zero being a
// VALUE, which is why only `%` and `div` reject a literal zero (finding F6).
run(<div <lit v:1> <lit v:0>>) == (1 / 0);
run(<div <lit v:-1> <lit v:0>>) == (-1 / 0);
// 0/0 is nan, which is not equal to itself, so compare nan-ness
let m00 = run(<div <lit v:0> <lit v:0>>);
(m00 != m00) == (0 / 0 != 0 / 0);

// `for b in [123] { b+1 }` — the file's for-statement section, as a
// comprehension over the same source
run(<forr n:'b', <arr a123> <add <vr n:'b'> <lit v:1>>>)
    == (for (b in [123]) b + 1);
run(<forr n:'a', <arr <lit v:"a"> <lit v:"b"> <lit v:"c">> <lit v:"ha!">>)
    == (for (a in ["a", "b", "c"]) "ha!");

// map field reads, from `let m = {a:123, b:-456, c:0.5, ...}`
let m_term = <mp <kv k:'a', a123> <kv k:'b', <lit v:-456>>
                 <kv k:'c', <lit v:0.5>> <kv k:'d', <lit v:true>>>
run(<fld k:'a', m_term>) == {a:123, b:-456, c:0.5, d:true}.a;
run(<fld k:'b', m_term>) == {a:123, b:-456, c:0.5, d:true}.b;
run(<fld k:'c', m_term>) == {a:123, b:-456, c:0.5, d:true}.c;
run(<fld k:'d', m_term>) == {a:123, b:-456, c:0.5, d:true}.d
