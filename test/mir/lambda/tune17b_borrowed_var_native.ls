// Tune17b: a borrowed `var` container parameter must not cost the function its
// native body, nor push its call sites onto the boxed `_b` adapter.
// Regression guard: map/object/element are never native param types, so such a
// parameter is a boxed Item in every generated entry. Disqualifying the whole
// function only stripped the sibling scalar params of their raw lanes, and the
// forced `_b` route re-boxed every scalar argument at the call site.

type Tune17bRec = {n: int}

pn tune17b_step(var s: Tune17bRec, var v: int[], k: int) any {
    s.n = s.n + k
    v[0] = v[0] + k
}

pn main() {
    var s: Tune17bRec = {n: 0}
    var v: int[] = [1, 2]
    var i: int = 0
    while (i < 3) {
        tune17b_step(s, v, 2)
        i = i + 1
    }
    print(s.n)
    print("\n")
}
