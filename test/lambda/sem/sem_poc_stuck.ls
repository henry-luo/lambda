// Semantics POC — the NEGATIVE suite (M3). Interpreter: sem_poc_kernel.ls.
// Design: vibe/Lambda_Semantics_DSL_POC.md.
//
// This file is a spec artifact rather than a differential one. The other
// fixtures ask "does the model agree with the runtime?"; this one asks the
// prior question — **what does the model consider an error at all?** — and
// pins the answer, because a model that quietly invents a value for an
// ill-formed term would agree with nothing and detect nothing.
//
// The partition it records has three regions, and the middle one is the
// interesting one:
//
//   STUCK    no rule applies. `run` reports the symbol 'stuck'.
//   TOTAL    a rule applies and answers null. Reads are deliberately total in
//            Lambda (S7.1.1v2, C15), so the model inherits that rather than
//            inventing an error the runtime does not have.
//   ADOPTED  the model has no opinion and takes the host's, e.g. truthiness.
//
// Everything here is `== 'stuck'` or `== null` or `== <value>`; goldens are
// `true` throughout.

import .sem_poc_kernel

// ===== STUCK: no rule applies =====

// a free variable has no binding to read
run(<vr n:'nope'>) == 'stuck';

// a tag outside the modelled grammar. Nothing falls through to a default value
run(<nosuchnode <lit v:1>>) == 'stuck';
run(<qqq>) == 'stuck';

// stuckness propagates out of any nested position, through every arm shape
run(<add <lit v:1> <vr n:'q'>>) == 'stuck';
run(<add <vr n:'q'> <lit v:1>>) == 'stuck';
run(<iff <vr n:'q'> <lit v:1> <lit v:2>>) == 'stuck';
run(<arr <lit v:1> <vr n:'q'>>) == 'stuck';
run(<forr n:'x', <arr <lit v:1>> <vr n:'q'>>) == 'stuck';
run(<lett n:'x', <vr n:'q'> <lit v:1>>) == 'stuck';

// a MALFORMED term — an operator missing an operand — is stuck rather than
// treating the absent child as null and computing with it
run(<add <lit v:1>>) == 'stuck';
run(<app <lam p:'x', <vr n:'x'>>>) == 'stuck';

// application is defined only on closures. A scalar, a string, an array and a
// non-closure element are all stuck, none of them read as a callable
run(<app <lit v:1> <lit v:2>>) == 'stuck';
run(<app <lit v:"s"> <lit v:2>>) == 'stuck';
run(<app <arr <lit v:1>> <lit v:2>>) == 'stuck';
run(<app <mp <kv k:'a', <lit v:1>>> <lit v:2>>) == 'stuck';

// the two application forms do not substitute for one another in either
// direction: arity is part of a closure's identity, not a coincidence
run(<appn <lam p:'x', <vr n:'x'>> <lit v:1>>) == 'stuck';
run(<app <lamn ps: ['x'], <vr n:'x'>> <lit v:1>>) == 'stuck';

// arity mismatch, both directions — no currying, no null-padding, no dropping
run(<appn <lamn ps: ['x', 'y'], <vr n:'x'>> <lit v:1>>) == 'stuck';
run(<appn <lamn ps: ['x'], <vr n:'x'>> <lit v:1> <lit v:2>>) == 'stuck';
run(<appn <lamn ps: [], <lit v:1>> <lit v:9>>) == 'stuck';

// divergence is stuckness too, which is what makes the judgment total: fuel
// bounds recursion depth, so a non-terminating term reports instead of
// destroying the run (finding F5)
run(<app <lam p:'x', <app <vr n:'x'> <vr n:'x'>>>
         <lam p:'x', <app <vr n:'x'> <vr n:'x'>>>>) == 'stuck';
run(<lett n:'loop', <fun f:'loop', p:'n', <app <vr n:'loop'> <vr n:'n'>>>
     <app <vr n:'loop'> <lit v:1>>>) == 'stuck';

// a binder does not escape the construct that introduced it
run(<lett n:'y', <forr n:'x', <arr <lit v:1>> <vr n:'x'>> <vr n:'x'>>) == 'stuck';
run(<lett n:'y', <app <lam p:'p', <vr n:'p'>> <lit v:1>> <vr n:'p'>>) == 'stuck';

// ===== TOTAL: a rule applies and answers null =====
// These are NOT errors. Lambda's reads are total, so the model reports null
// exactly where the runtime does — and the native side of each line proves it.

run(<idx <arr <lit v:10>> <lit v:9>>) == [10][9];        // out of bounds
run(<idx <arr <lit v:10>> <lit v:-1>>) == [10][-1];      // negative index
run(<fld k:'zzz', <mp <kv k:'a', <lit v:1>>>>) == {a: 1}.zzz;   // missing key

// reading a field of a non-map, or indexing with a non-integer, are also total
run(<fld k:'x', <lit v:5>>) == null;
run(<idx <arr <lit v:1>> <lit v:"k">>) == null;

// a `lit` with no value attribute is well-formed and means null — the model
// does not require the attribute to be present
run(<lit>) == null;

// ===== ADOPTED: the model defers to the host rather than ruling =====

// a non-boolean guard is NOT an error: `iff` inherits Lambda's truthiness, so
// an int or a string guard selects a branch exactly as the runtime does
run(<iff <lit v:5> <lit v:1> <lit v:2>>) == (if (5) 1 else 2);
run(<iff <lit v:"s"> <lit v:1> <lit v:2>>) == (if ("s") 1 else 2);
run(<iff <lit v:0> <lit v:1> <lit v:2>>) == (if (0) 1 else 2);
run(<iff <lit v:null> <lit v:1> <lit v:2>>) == (if (null) 1 else 2);

// ===== a KNOWN model limitation, pinned rather than hidden =====
// Lambda has two error channels: a returned error VALUE that flows as ordinary
// data, and a raised error that must be handled. Natively `1 + "str"` (a real
// line in test/lambda/expr.ls) yields an error *value*. The model cannot
// reproduce that: every arm propagates with `^`, so the same term is raised and
// reported 'stuck'. Both sides agree the term has no ordinary value; they
// disagree on which channel carries that fact. P8 puts errors-as-modelled-values
// out of POC scope, so this is compared on is-there-a-value rather than on
// equality — a divergence recorded as a fact instead of papered over.
(run(<add <lit v:1> <lit v:"str">>) == 'stuck') and ((1 + "str") is error);

// ===== the partition is exhaustive for the modelled grammar =====
// A term is stuck or it has a value; there is no third outcome, and evaluation
// continues normally after any of the above.
run(<add <lit v:1> <lit v:2>>) == 3
