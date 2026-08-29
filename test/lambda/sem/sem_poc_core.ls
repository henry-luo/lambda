// Semantics POC — M0 kernel: a big-step definitional interpreter for Lambda's
// functional core, written in plain Lambda with no new syntax or runtime.
// Design: vibe/Lambda_Semantics_DSL_POC.md (P1-P10); parent proposal:
// vibe/Lambda_Semantics_DSL_Proposal.md.
//
// Encoding (P3): terms are elements — tag = node kind, attributes = atomic
// metadata (`v` literal, `n` name, `p` parameter, `f` self-name), children =
// sub-terms in evaluation order. Environments and closures are elements too.
// Tags are deliberately not Lambda keywords (`lett`, `iff`, `vr`) so a term can
// never collide with surface syntax.
//
// Every check is DIFFERENTIAL (P6): it evaluates a term through the model and
// compares against the runtime evaluating the same program natively. The two
// sides share a process but not a mechanism — the native side runs
// parser -> AST -> MIR JIT -> tagged values, the model side walks elements and
// applies one arm per node kind — so a codegen bug and a wrong arm have no way
// to coincide. Goldens here are `true`, i.e. a specification rather than
// captured output.

// ===== environment: element assoc-list, newest binding first =====
// values ride in the `v` attribute (attributes hold any value without the
// splicing hazards content position has); first hit wins, so shadowing is
// correct by construction rather than by a rule.
fn lookup(env, n) => {
    let hits = for (b in env where b.n == n) b.v
    if (len(hits) > 0) hits[0] else raise error("unbound variable")
}

fn bind(env, n, v) => {
    let rest = for (b in env) b;
    <env <b n: n, v: v> rest>
}

// ===== the judgment =====
// `ev` is what the parent proposal's §5.4 says a moded judgment compiles to: a
// recursive fn returning `T ^ StuckErr`. The `match` is the rule set, one arm
// per node kind; `^` on each recursive call is stuckness propagation. Nothing
// unmodelled can silently produce a value — the default arm raises.
// Evaluation order is inherited from the meta-language: `ev(t[0]) + ev(t[1])`
// IS left-to-right strictness, which is why no `strict` annotation is needed.
fn ev(t, env) any^ => match name(t) {
    case 'lit': t.v
    case 'vr':  lookup(env, t.n)^
    case 'add': ev(t[0], env)^ + ev(t[1], env)^
    case 'sub': ev(t[0], env)^ - ev(t[1], env)^
    case 'mul': ev(t[0], env)^ * ev(t[1], env)^
    case 'lt':  ev(t[0], env)^ < ev(t[1], env)^
    case 'le':  ev(t[0], env)^ <= ev(t[1], env)^
    case 'iff': if (ev(t[0], env)^) ev(t[1], env)^ else ev(t[2], env)^
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
    case 'app': {
        let f = ev(t[0], env)^
        let a = ev(t[1], env)^
        let body = f[0]
        let cenv = f[1]
        let env1 = bind(cenv, f.p, a)
        // recursion without mutation or a cyclic environment: a named closure
        // re-binds ITSELF before its body runs, so the body's own name resolves.
        // A plain `clo` carries no `f` attribute, so `f.f` reads null (falsy)
        // and both closure kinds share this one arm.
        let env2 = if (f.f) bind(env1, f.f, f) else env1
        ev(body, env2)^
    }
    case 'lett': ev(t[1], bind(env, t.n, ev(t[0], env)^))^
    default: raise error("stuck: no rule for term")
}

let env0 = <env>;

// ===== differential checks: model result == native result =====

// arithmetic, nested, with precedence carried by term structure not parsing
(ev(<add <lit v:3> <mul <lit v:4> <lit v:5>>>, env0) ^ { 'stuck' }) == (3 + 4 * 5);

// let-binding and variable reference
(ev(<lett n:'x', <lit v:10> <add <vr n:'x'> <lit v:1>>>, env0) ^ { 'stuck' })
    == (let x = 10, x + 1);

// abstraction and application against a native anonymous function
(ev(<app <lam p:'y', <add <vr n:'y'> <lit v:2>>> <lit v:40>>, env0) ^ { 'stuck' })
    == (((y) => y + 2)(40));

// closure capture: the lambda closes over `a` from its defining scope
(ev(<lett n:'a', <lit v:7>
     <lett n:'f', <lam p:'z', <add <vr n:'z'> <vr n:'a'>>>
      <app <vr n:'f'> <lit v:5>>>>, env0) ^ { 'stuck' })
    == (let a = 7, let f = (z) => z + a, f(5));

// shadowing: the inner binding wins, and the outer one survives it
(ev(<lett n:'x', <lit v:1>
     <add <lett n:'x', <lit v:100> <vr n:'x'>>
          <vr n:'x'>>>, env0) ^ { 'stuck' })
    == (let x = 1, (let x = 100, x) + x);

// conditional, both arms, guard evaluated through the model
(ev(<iff <lt <lit v:1> <lit v:2>> <lit v:10> <lit v:20>>, env0) ^ { 'stuck' })
    == (if (1 < 2) 10 else 20);
(ev(<iff <lt <lit v:5> <lit v:2>> <lit v:10> <lit v:20>>, env0) ^ { 'stuck' })
    == (if (5 < 2) 10 else 20);

// recursion: fact(6) through the model vs the native recursive fn
fn fact(n: int) int => if (n <= 1) 1 else n * fact(n - 1);
(ev(<lett n:'fact',
     <fun f:'fact', p:'n',
      <iff <le <vr n:'n'> <lit v:1>>
           <lit v:1>
           <mul <vr n:'n'> <app <vr n:'fact'> <sub <vr n:'n'> <lit v:1>>>>>>
     <app <vr n:'fact'> <lit v:6>>>, env0) ^ { 'stuck' })
    == fact(6);

// ===== stuckness is total: unmodelled or ill-formed terms never yield a value =====
// an unbound variable has no rule, so it raises rather than reading null
(ev(<vr n:'q'>, env0) ^ { 'stuck' }) == 'stuck';
// a tag with no arm is stuck, not silently skipped
(ev(<nosuchnode <lit v:1>>, env0) ^ { 'stuck' }) == 'stuck';
// stuckness propagates out of a nested position through `^`
(ev(<add <lit v:1> <vr n:'q'>>, env0) ^ { 'stuck' }) == 'stuck'
