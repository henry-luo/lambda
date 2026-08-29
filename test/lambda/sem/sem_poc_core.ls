// Semantics POC — positive differential checks (M0 kernel, M1 breadth,
// M2 collections). The interpreter itself lives in sem_poc_kernel.ls; this file
// is only the checks. Design: vibe/Lambda_Semantics_DSL_POC.md.
//
// Every check is DIFFERENTIAL (P6): it evaluates a term through the model and
// compares against the runtime evaluating the same program natively. The two
// sides share a process but not a mechanism — the native side runs
// parser -> AST -> MIR JIT -> tagged values, the model side walks elements and
// applies one arm per node kind — so a codegen bug and a wrong arm have no way
// to coincide. Goldens here are `true`, i.e. a specification rather than
// captured output.

import .sem_poc_kernel

// ===== M0: core differential checks — model result == native result =====

// arithmetic, nested, with precedence carried by term structure not parsing
run(<add <lit v:3> <mul <lit v:4> <lit v:5>>>) == (3 + 4 * 5);

// let-binding and variable reference
run(<lett n:'x', <lit v:10> <add <vr n:'x'> <lit v:1>>>) == (let x = 10, x + 1);

// abstraction and application against a native anonymous function
run(<app <lam p:'y', <add <vr n:'y'> <lit v:2>>> <lit v:40>>)
    == (((y) => y + 2)(40));

// closure capture: the lambda closes over `a` from its defining scope
run(<lett n:'a', <lit v:7>
     <lett n:'f', <lam p:'z', <add <vr n:'z'> <vr n:'a'>>>
      <app <vr n:'f'> <lit v:5>>>>)
    == (let a = 7, let f = (z) => z + a, f(5));

// LEXICAL vs DYNAMIC scoping: the closure must read the `a` visible where it
// was DEFINED (1), not the one visible where it is CALLED (100). Mutation
// testing showed every other closure check here still passed when the model was
// switched to capture the caller's environment — the captured name happened to
// be in scope at the call site too — so this is the one that discriminates.
// (the native side uses a factory rather than a shadowing let-chain: rebinding
// `a` in one scope is a duplicate-definition error, so only the TERM can spell
// the shadowing that makes this discriminating)
fn mk_lex() { let a = 1  fn inner(z) => z + a  inner }
run(<lett n:'f', <lett n:'a', <lit v:1> <lam p:'z', <add <vr n:'z'> <vr n:'a'>>>>
     <lett n:'a', <lit v:100> <app <vr n:'f'> <lit v:0>>>>)
    == mk_lex()(0);

// shadowing: the inner binding wins, and the outer one survives it
run(<lett n:'x', <lit v:1>
     <add <lett n:'x', <lit v:100> <vr n:'x'>>
          <vr n:'x'>>>)
    == (let x = 1, (let x = 100, x) + x);

// conditional, both arms, guard evaluated through the model
run(<iff <lt <lit v:1> <lit v:2>> <lit v:10> <lit v:20>>) == (if (1 < 2) 10 else 20);
run(<iff <lt <lit v:5> <lit v:2>> <lit v:10> <lit v:20>>) == (if (5 < 2) 10 else 20);

// recursion: fact(6) through the model vs the native recursive fn
fn fact(n: int) int => if (n <= 1) 1 else n * fact(n - 1);
run(<lett n:'fact',
     <fun f:'fact', p:'n',
      <iff <le <vr n:'n'> <lit v:1>>
           <lit v:1>
           <mul <vr n:'n'> <app <vr n:'fact'> <sub <vr n:'n'> <lit v:1>>>>>>
     <app <vr n:'fact'> <lit v:6>>>)
    == fact(6);

// ===== M0: stuckness is total — unmodelled terms never yield a value =====
run(<vr n:'q'>) == 'stuck';                       // unbound variable
run(<nosuchnode <lit v:1>>) == 'stuck';           // no rule for this tag
run(<add <lit v:1> <vr n:'q'>>) == 'stuck';       // propagates from nested position

// ===== M1: remaining scalar operators =====

// division is true division, so an int pair yields a float
run(<div <lit v:7> <lit v:2>>) == (7 / 2);
run(<mod <lit v:7> <lit v:2>>) == (7 % 2);

// `/` by zero is inf, not an error, at compile time and run time alike — the
// model inherits it from the host (P5) with no special case.
run(<div <lit v:10> <lit v:0>>) == (10 / 0);

// `%` by zero is nan, and nan is not equal to itself, so this compares
// nan-ness rather than values. The native side must build its zero at RUN time:
// a literal `10 % 0` is rejected at compile time (E312, `%` and `div` only),
// even though the identical operation is legal and answers nan once the zero
// arrives at run time. The model, which never sees a literal, always takes the
// run-time meaning — so this check pins the asymmetry rather than hiding it.
fn zero(n: int) int => n - n;
let model_mod0 = run(<mod <lit v:10> <lit v:0>>);
(model_mod0 != model_mod0) == (10 % zero(3) != 10 % zero(3));

// the full comparison set
run(<gt <lit v:5> <lit v:2>>) == (5 > 2);
run(<ge <lit v:2> <lit v:2>>) == (2 >= 2);
run(<eq <lit v:3> <lit v:3>>) == (3 == 3);
run(<ne <lit v:3> <lit v:4>>) == (3 != 4);

// strings are ordinary model values; concatenation and equality both work
run(<cat <lit v:"ab"> <lit v:"cd">>) == ("ab" ++ "cd");
run(<eq <cat <lit v:"ab"> <lit v:"cd">> <lit v:"abcd">>) == ("ab" ++ "cd" == "abcd");

// ===== M1: logical operators, and short-circuit as an observable property =====
run(<andd <lit v:true> <lit v:false>>) == (true and false);
run(<orr <lit v:false> <lit v:true>>) == (false or true);
run(<nott <lit v:true>>) == (not true);

// short-circuit is proven, not asserted: the right operand is an unbound
// variable, which is stuck if evaluated. Getting a value back means the model
// never recursed into it — matching native `and`/`or`, which also skip it.
run(<andd <lit v:false> <vr n:'q'>>) == false;
run(<orr <lit v:true> <vr n:'q'>>) == true;
// and the converse: when the left operand does NOT decide, the stuck right
// operand is reached, so short-circuit is not just swallowing errors
run(<andd <lit v:true> <vr n:'q'>>) == 'stuck';
run(<orr <lit v:false> <vr n:'q'>>) == 'stuck';

// ===== M1: n-ary functions =====

// two parameters, applied against the equivalent native lambda
run(<appn <lamn ps: ['x', 'y'], <add <vr n:'x'> <vr n:'y'>>>
          <lit v:3> <lit v:4>>)
    == (((x, y) => x + y)(3, 4));

// three parameters, and the binding order matters: `sub` is not commutative
run(<appn <lamn ps: ['a', 'b', 'c'], <sub <mul <vr n:'a'> <vr n:'b'>> <vr n:'c'>>>
          <lit v:10> <lit v:3> <lit v:4>>)
    == (((a, b, c) => a * b - c)(10, 3, 4));

// an n-ary closure still captures its defining environment
run(<lett n:'k', <lit v:100>
     <appn <lamn ps: ['x', 'y'], <add <add <vr n:'x'> <vr n:'y'>> <vr n:'k'>>>
           <lit v:1> <lit v:2>>>)
    == (let k = 100, ((x, y) => x + y + k)(1, 2));

// zero parameters is a degenerate but well-formed case
run(<appn <lamn ps: [], <lit v:42>>>) == 42;

// ===== M1: application stuckness =====
run(<appn <lamn ps: ['x', 'y'], <vr n:'x'>> <lit v:1>>) == 'stuck';          // too few
run(<appn <lamn ps: ['x'], <vr n:'x'>> <lit v:1> <lit v:2>>) == 'stuck';     // too many
run(<app <lit v:1> <lit v:2>>) == 'stuck';                                    // not a function
run(<appn <lam p:'x', <vr n:'x'>> <lit v:1>>) == 'stuck';   // unary closure, n-ary form

// ===== M2: arrays, indexing, slicing =====

// an array term builds a real host array, so it compares directly
run(<arr <lit v:10> <lit v:20> <lit v:30>>) == [10, 20, 30];
run(<arr>) == [];
// elements are arbitrary terms, evaluated left to right
run(<arr <add <lit v:1> <lit v:1>> <mul <lit v:3> <lit v:3>>>) == [2, 9];
// arrays nest
run(<arr <arr <lit v:1>> <arr <lit v:2> <lit v:3>>>) == [[1], [2, 3]];

// indexing, and the host's total read: out of bounds and negative are null
run(<idx <arr <lit v:10> <lit v:20> <lit v:30>> <lit v:1>>) == [10, 20, 30][1];
run(<idx <arr <lit v:10>> <lit v:9>>) == [10][9];
run(<idx <arr <lit v:10>> <lit v:-1>>) == [10][-1];

// slicing with an inclusive range bound
run(<slice <arr <lit v:10> <lit v:20> <lit v:30>> <lit v:0> <lit v:1>>)
    == [10, 20, 30][0 to 1];

// `len` is delegated to the host as a declared builtin, not derived
run(<lenn <arr <lit v:1> <lit v:2> <lit v:3>>>) == len([1, 2, 3]);

// ===== M2: comprehension =====

// map over a sequence: the binder is visible in the body, and only there
run(<forr n:'x', <arr <lit v:1> <lit v:2> <lit v:3>> <mul <vr n:'x'> <lit v:2>>>)
    == (for (x in [1, 2, 3]) x * 2);
// the body closes over the surrounding environment as well as the binder
run(<lett n:'k', <lit v:10>
     <forr n:'x', <arr <lit v:1> <lit v:2>> <add <vr n:'x'> <vr n:'k'>>>>)
    == (let k = 10, for (x in [1, 2]) x + k);
// an empty source yields an empty result, and the binder does not escape
run(<forr n:'x', <arr> <vr n:'x'>>) == [];
run(<lett n:'y', <forr n:'x', <arr <lit v:1>> <vr n:'x'>> <vr n:'x'>>) == 'stuck';

// ===== M2: maps — modelled as assoc-lists, compared by field read =====
// The model cannot build a host map: `{(k): v}` is a parse error, so there is
// no way to construct one from an evaluated key. Field READS are the comparable
// surface, and they are what the semantics is actually about (S8.4).
run(<fld k:'a', <mp <kv k:'a', <lit v:1>> <kv k:'b', <lit v:2>>>>)
    == {a: 1, b: 2}.a;
run(<fld k:'b', <mp <kv k:'a', <lit v:1>> <kv k:'b', <lit v:2>>>>)
    == {a: 1, b: 2}.b;
// a missing key reads null on both sides rather than raising
run(<fld k:'zzz', <mp <kv k:'a', <lit v:1>>>>) == {a: 1}.zzz;
// values are arbitrary terms, evaluated when the map is built
run(<fld k:'s', <mp <kv k:'s', <add <lit v:2> <lit v:3>>>>>) == 5;

// ===== M2: totality — divergence is stuckness, not a crash (F5) =====
// omega, the classic non-terminating term: (\x. x x) (\x. x x). Without fuel
// this overflowed the stack and erased every other result in the file; with it
// the term is simply stuck, and the checks around it still report.
run(<app <lam p:'x', <app <vr n:'x'> <vr n:'x'>>>
         <lam p:'x', <app <vr n:'x'> <vr n:'x'>>>>) == 'stuck';
// a self-applying recursive function with no base case is likewise stuck
run(<lett n:'loop', <fun f:'loop', p:'n', <app <vr n:'loop'> <vr n:'n'>>>
     <app <vr n:'loop'> <lit v:1>>>) == 'stuck';
// and evaluation continues normally afterwards — divergence is contained
run(<add <lit v:1> <lit v:2>>) == 3
