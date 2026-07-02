// Test MIR JIT parameter type inference for procedural functions — division case.
// Regression: an untyped pn parameter used only in true division (`/`) with a
// non-literal operand was speculatively inferred as INT, so a float argument was
// truncated at the call site (e.g. a timestep dt = 0.5 → 0), making `dt / x` == 0.
// Lambda's `/` is always true division (4/2 → 2.0), so such a param is float-natured
// and must stay float/ANY. This is the bug behind the nbody benchmark's wrong energy.

// Test 1: float param is the dividend, divisor is another (non-literal) param.
// Without the fix dt → INT, 0.5 truncates to 0, and the result is 0.
pn scale(dt, denom) {
    return dt / denom
}

// Test 2: small timestep (the exact nbody value class), divisor non-literal.
pn scale2(dt, denom) {
    return dt / denom
}

// Test 3: the nbody mag pattern — divisor is a computed expression from array
// reads (dt / (d2 * dist)). This is the precise shape that failed in advance().
pn compute_mag(arr, dt) {
    var dx = arr[0] - arr[1]
    var d2 = dx * dx
    var dist = math.sqrt(d2)
    return dt / (d2 * dist)
}

// Test 4: float param used in BOTH division and multiplication/addition.
// The `/` use must mark it float so the later `* v` and `+ dt` stay float too.
pn mixed(dt, denom, v) {
    var mag = dt / denom
    return mag * v + dt
}

// Test 5 (regression): param used with an INT literal in arithmetic must still
// infer INT (fast path unchanged) — n + 1 returns 11 for 10.
pn add_one(n) {
    return n + 1
}

// Test 6 (regression): param used in `/` against an INT literal. Positive INT
// evidence still applies; true division yields a float result (7 / 2 → 3.5).
pn half(n) {
    return n / 2
}

pn main() {
    // 0.5 / 64.0 = 0.0078125  (was 0 before the fix)
    print("T1:" ++ (scale(0.5, 64.0)) ++ "\n")
    // 0.01 / 2.0 = 0.005
    print("T2:" ++ (scale2(0.01, 2.0)) ++ "\n")
    // arr=[1.0,5.0], dt=0.5: dx=-4, d2=16, dist=4, mag = 0.5/64 = 0.0078125
    var arr = [1.0, 5.0]
    print("T3:" ++ (compute_mag(arr, 0.5)) ++ "\n")
    // mag = 0.5/64 = 0.0078125; 0.0078125*100 + 0.5 = 1.28125
    print("T4:" ++ (mixed(0.5, 64.0, 100.0)) ++ "\n")
    // regression: int arithmetic still works
    print("T5:" ++ (add_one(10)) ++ "\n")
    // regression: int param, true division → float result
    print("T6:" ++ (half(7)) ++ "\n")
}
