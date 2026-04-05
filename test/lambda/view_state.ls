// Phase 2: view template state management tests

// Test 1: View with state reads default value in body
view int state count: 0 {
  "count:" ++ string(count)
}
apply(42)
0

// Test 2: State initialized to non-zero default
view string state label: "default" {
  ~ ++ ":" ++ label
}
apply("hello")
0

// Test 3: State initialized to boolean default
view bool state active: true {
  string(active)
}
apply(true)
0

// Test 4: Multiple state variables in one view
view float state x: 1, y: 2 {
  string(x) ++ "," ++ string(y)
}
apply(3.14)
0

// Test 5: State persistence — same model item returns same state
let result1 = apply(42)
let result2 = apply(42)
result1 == result2
0

// Test 6: View with state and event handler (handler compiles but not invoked)
view any state flag: false {
  string(flag)
}
on click() {
  flag = !flag
}
apply(null)
0

// Test 7: View with state & multiple handlers (element pattern)
view <msg> state text: "init", cursor: 0 {
  text
}
on input(e) {
  text = "changed"
}
on focus() {
  cursor = 1
}
apply(<msg "hi">)

