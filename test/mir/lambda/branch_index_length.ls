// S7.1.1v3/D2.5.3: a dominating nonnegative and rank-one length test
// proves the indexed read, but neither test alone proves it.
fn guarded(a: float[], i: int) float? =>
    if (i >= 0 and i < len(a)) a[i] else null

fn reversed(a: float[], i: int) float? =>
    if (i >= 0 and len(a) > i) a[i] else null

fn no_lower(a: float[], i: int) float? =>
    if (i < len(a)) a[i] else null

fn different(a: float[], b: float[], i: int) float? =>
    if (i >= 0 and i < len(b)) a[i] else null

fn leaked(a: float[], i: int) float? =>
    if (i >= 0 and i < len(a)) null else a[i]

pn rebound(a: float[], i: int) float? {
    if (i >= 0 and i < len(a)) {
        a = [5.0]
        return a[i]
    }
    return null
}

pn main() {
    let a: float[] = [2.5, 3.5]
    let b: float[] = [9.0]
    print(guarded(a, 1)); print(" ")
    print(guarded(a, -1)); print(" ")
    print(guarded(a, 2)); print(" ")
    print(reversed(a, 0)); print(" ")
    print(no_lower(a, -1)); print(" ")
    print(different(a, b, 1)); print(" ")
    print(leaked(a, 2)); print(" ")
    print(rebound(a, 1)); print("\n")
}
