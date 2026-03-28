// Phase 4: edit bridge — MarkEditor integration tests

// Test 1: edit template compiles and can be applied
edit int {
  string(it)
}
let r1 = apply(42, {mode: "edit"})
r1
0

// Test 2: edit template with state compiles
edit string state count: 0 {
  it ++ ":" ++ string(count)
}
let r2 = apply("hello", {mode: "edit"})
r2
0

// Test 3: undo() returns false when no history
undo()
0

// Test 4: redo() returns false when no history
redo()
0

// Test 5: commit() returns version number
commit()
0

// Test 6: commit with description returns version number
commit("initial")
0

// Test 7: edit template with handler compiles
edit any state label: "x" {
  string(label)
}
on click() {
  label = "clicked"
}
apply(null, {mode: "edit"})
