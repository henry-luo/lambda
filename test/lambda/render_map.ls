// Phase 3: render map — observer-based reconciliation tests

// Test 1: apply() records mapping; same source returns same result
view int state count: 0 {
  "v:" ++ string(count)
}
let r1 = apply(10)
r1
0

// Test 2: Second apply() with same source item returns cached mapping result
let r2 = apply(10)
r1 == r2
0

// Test 3: Different source item gets separate mapping
view string state label: "x" {
  it ++ ":" ++ label
}
apply("a")
0

// Test 4: apply() with different source items produces independent results
apply("b")
0

// Test 5: View with state mutation compiles (handler marks entry dirty)
view any state flag: false {
  string(flag)
}
on click() {
  flag = !flag
}
apply(null)
0

// Test 6: View with element pattern gets render map entry
view <msg> state text: "init" {
  text
}
apply(<msg "hi">)
0

// Test 7: Multiple state vars — state set triggers dirty marking
view float state x: 1, y: 2 {
  string(x) ++ "," ++ string(y)
}
apply(3.14)
