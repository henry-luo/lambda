// Semantics POC — SHARED KERNEL: a big-step definitional interpreter for
// Lambda's functional core, written in plain Lambda with no new syntax or
// runtime. Design: vibe/Lambda_Semantics_DSL_POC.md (P1-P10).
//
// This file declares only `pub fn`s and produces no output, so it has no `.txt`
// golden and the test discovery skips it (it only collects `.ls` files that
// have a matching `.txt`). Its consumers are the fixtures beside it:
//   sem_poc_core.ls      positive differential checks (M0-M2)
//   sem_poc_stuck.ls     the negative suite: what the model calls an error (M3)
//   sem_poc_fixtures.ls  real test/lambda programs, hand-translated (M3)
//
// Encoding (P3): terms are elements — tag = node kind, attributes = atomic
// metadata (`v` literal, `n` name, `p` parameter, `ps` parameter list, `k` key,
// `f` self-name), children = sub-terms in evaluation order. Environments,
// closures and modelled maps are elements too. Tags are deliberately not Lambda
// keywords (`lett`, `iff`, `vr`) so a term can never collide with surface syntax.
//
// Child access is always `t[i]` (children-only) and `len(t)` is read only on
// attribute-free terms, so this model is unaffected by whether element `len`
// counts attributes as well as children (S8.3.1).
// ===== environment: element assoc-list, newest binding first =====
// values ride in the `v` attribute (attributes hold any value without the
// splicing hazards content position has); first hit wins, so shadowing is
// correct by construction rather than by a rule.
pub fn lookup(env, n) => {
    let hits = for (b in env where b.n == n) b.v
    if (len(hits) > 0) hits[0] else raise error("unbound variable")
}

pub fn bind(env, n, v) => {
    let rest = for (b in env) b;
    <env <b n: n, v: v> rest>
}

// bind a parameter list to already-evaluated arguments, left to right. Takes no
// terms and never calls `ev`, so it needs no mutual recursion with the judgment.
pub fn bind_params(env, ps, args, i) =>
    if (i >= len(ps)) env
    else bind_params(bind(env, ps[i], args[i]), ps, args, i + 1)

// application is only defined on closure values; an int or a bare term is stuck
// rather than silently reading null out of a non-element. The `and` here is the
// short-circuit that keeps `name(f)` from ever seeing a non-element.
pub fn closure_of(f, kinds) any^ =>
    if (type(f) == element and contains(kinds, name(f))) f
    else raise error("apply non-function")

// ===== the judgment =====
// `ev` is what the parent proposal's §5.4 says a moded judgment compiles to: a
// recursive fn returning `T ^ StuckErr`. The `match` is the rule set, one arm
// per node kind; `^` on each recursive call is stuckness propagation. Nothing
// unmodelled can silently produce a value — the default arm raises.
// Evaluation order is inherited from the meta-language: `ev(t[0]) + ev(t[1])`
// IS left-to-right strictness, which is why no `strict` annotation is needed.
//
// `fuel` bounds recursion DEPTH and makes the judgment TOTAL: without it a term
// like omega (below) diverges, and because a script's top level prints only
// after the whole file evaluates (S16.7), one diverging term erased every other
// result and reported a bare stack overflow instead (finding F5). Exhausting
// fuel is ordinary stuckness, so divergence now localizes to its own check.
// There is no tail-call escape: every arm decrements, tail positions included.
pub fn ev(t, env, fuel) any^ => {
    if (fuel <= 0) raise error("out of fuel")
    match name(t) {
    case 'lit': t.v
    case 'vr':  lookup(env, t.n)^
    // arithmetic — model scalars ARE host scalars (P5), so `/` is Lambda's
    // true division (7/2 = 3.5, x/0 = inf) rather than a separate numeric tower
    case 'add': ev(t[0], env, fuel - 1)^ + ev(t[1], env, fuel - 1)^
    case 'sub': ev(t[0], env, fuel - 1)^ - ev(t[1], env, fuel - 1)^
    case 'mul': ev(t[0], env, fuel - 1)^ * ev(t[1], env, fuel - 1)^
    case 'div': ev(t[0], env, fuel - 1)^ / ev(t[1], env, fuel - 1)^
    case 'mod': ev(t[0], env, fuel - 1)^ % ev(t[1], env, fuel - 1)^
    case 'cat': ev(t[0], env, fuel - 1)^ ++ ev(t[1], env, fuel - 1)^
    case 'pow': ev(t[0], env, fuel - 1)^ ** ev(t[1], env, fuel - 1)^
    case 'neg': - ev(t[0], env, fuel - 1)^
    // comparison
    case 'lt':  ev(t[0], env, fuel - 1)^ <  ev(t[1], env, fuel - 1)^
    case 'le':  ev(t[0], env, fuel - 1)^ <= ev(t[1], env, fuel - 1)^
    case 'gt':  ev(t[0], env, fuel - 1)^ >  ev(t[1], env, fuel - 1)^
    case 'ge':  ev(t[0], env, fuel - 1)^ >= ev(t[1], env, fuel - 1)^
    case 'eq':  ev(t[0], env, fuel - 1)^ == ev(t[1], env, fuel - 1)^
    case 'ne':  ev(t[0], env, fuel - 1)^ != ev(t[1], env, fuel - 1)^
    // logical — short-circuit is expressed by NOT recursing into the second
    // child, so an ill-formed or stuck right operand is never reached. The
    // checks below prove that by putting an unbound variable there.
    case 'andd': if (ev(t[0], env, fuel - 1)^) ev(t[1], env, fuel - 1)^ else false
    case 'orr':  if (ev(t[0], env, fuel - 1)^) true else ev(t[1], env, fuel - 1)^
    case 'nott': not ev(t[0], env, fuel - 1)^
    case 'iff': if (ev(t[0], env, fuel - 1)^) ev(t[1], env, fuel - 1)^
                else ev(t[2], env, fuel - 1)^
    // a closure captures its defining environment as data — this is what makes
    // the model environment-based (P2) and so needs no capture-avoiding subst
    case 'lam': {
        let body = t[0];
        <clo p: t.p, body env>
    }
    // named function: same shape plus a self-name, consumed by `app` below
    case 'fun': {
        let body = t[0];
        <rclo f: t.f, p: t.p, body env>
    }
    // n-ary abstraction: parameters ride in the `ps` attribute as an array
    case 'lamn': {
        let body = t[0];
        <clon ps: t.ps, body env>
    }
    case 'app': {
        let f = closure_of(ev(t[0], env, fuel - 1)^, ['clo', 'rclo'])^
        let a = ev(t[1], env, fuel - 1)^
        let body = f[0]
        let cenv = f[1]
        let env1 = bind(cenv, f.p, a)
        // recursion without mutation or a cyclic environment: a named closure
        // re-binds ITSELF before its body runs, so the body's own name resolves.
        // A plain `clo` carries no `f` attribute, so `f.f` reads null (falsy)
        // and both closure kinds share this one arm.
        let env2 = if (f.f) bind(env1, f.f, f) else env1
        ev(body, env2, fuel - 1)^
    }
    case 'appn': {
        let f = closure_of(ev(t[0], env, fuel - 1)^, ['clon'])^
        // `appn` carries no attributes, so len(t) is its child count: the
        // callee followed by the arguments.
        let args = for (i in 1 to len(t) - 1) ev(t[i], env, fuel - 1)^
        let ps = f.ps
        // arity is part of the semantics, not a host detail: a mismatch has no
        // rule, so it is stuck rather than binding null or dropping arguments
        if (len(ps) != len(args)) raise error("arity mismatch")
        let body = f[0]
        let cenv = f[1]
        ev(body, bind_params(cenv, ps, args, 0), fuel - 1)^
    }
    case 'lett': ev(t[1], bind(env, t.n, ev(t[0], env, fuel - 1)^), fuel - 1)^

    // ===== M2: collections =====
    // array literal — attribute-free, so len(t) is the element count. The
    // result is a REAL host array, so checks compare it with `==` directly.
    // `[*items]` is load-bearing, not decoration: a bare comprehension result
    // stays SPREADABLE, and that flag survives being returned and re-bound, so
    // a nested array would splice into the enclosing comprehension (flattening
    // it) and an empty one would vanish where a block expects a value. Every
    // array the model creates is de-spread here so downstream rules see a plain
    // value (finding F8).
    case 'arr': {
        let items = for (i in 0 to len(t) - 1) ev(t[i], env, fuel - 1)^;
        [*items]
    }
    // indexing inherits the host's total read: out of bounds and negative
    // indices are null rather than errors (S7.1.1v2, C15), not a rule of ours
    case 'idx': {
        let a = ev(t[0], env, fuel - 1)^
        let i = ev(t[1], env, fuel - 1)^;
        a[i]
    }
    case 'slice': {
        let a = ev(t[0], env, fuel - 1)^
        let i = ev(t[1], env, fuel - 1)^
        let j = ev(t[2], env, fuel - 1)^;
        a[i to j]
    }
    // comprehension: `<forr n:'x', src body>` binds each element in turn. The
    // binder is an attribute, so children stay [source, body].
    case 'forr': {
        let src = ev(t[0], env, fuel - 1)^
        let items = for (x in src) ev(t[1], bind(env, t.n, x), fuel - 1)^;
        [*items]     // de-spread for the same reason as `arr` above (F8)
    }
    // a modelled map is an assoc-list element, NOT a host map: map literals
    // reject a computed key, so the model cannot build `{k: v}` from an
    // evaluated key at all. Field reads are therefore what the checks compare.
    case 'mp': {
        let ents = for (i in 0 to len(t) - 1) {
            let kv = t[i]
            let v = ev(kv[0], env, fuel - 1)^;
            <ent k: kv.k, v: v>
        };
        <mapv ents>
    }
    case 'fld': {
        let m = ev(t[0], env, fuel - 1)^
        let hits = for (e in m where e.k == t.k) e.v
        // a missing key reads null, matching the host's `m.absent`
        if (len(hits) > 0) hits[0] else null
    }
    // the trusted-primitive boundary (parent §5.6): `len` is not derived from
    // rules, it is delegated to the host and declared as such here
    case 'lenn': len(ev(t[0], env, fuel - 1)^)

    default: raise error("stuck: no rule for term")
    }
}


// one entry point for every check: fixed initial fuel, empty environment, and
// stuckness surfaced as the symbol 'stuck' rather than a raised error. FUEL is
// a literal here so the module exports only functions.
pub fn run(t) => ev(t, <env>, 400) ^ { 'stuck' }
