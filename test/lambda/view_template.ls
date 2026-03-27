// Phase 1: view/edit template and apply() tests

// Test 1: Basic view with int pattern
view int {
  it * 2
}
apply(42)

// Test 2: Float view
view float {
  it + 0.5
}
apply(3.14)

// Test 3: String view
view string {
  "hi:" ++ it
}
apply("world")
0  // separator

// Test 4: Bool view
view bool {
  string(it) ++ "!"
}
apply(true)
0  // separator
apply(false)
0  // separator

// Test 5: Catch-all view (any)
view any {
  "any"
}
apply(null)
0  // separator

// Test 6: Specificity - int view wins over catch-all
apply(7)

// Test 7: Pipe syntax with apply
99 | apply()

// Test 8: User function doesn't conflict with sys apply
fn my_apply(f, x) => f(x)
my_apply((x) => x * 10, 5)
