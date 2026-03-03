// Test: Raise in Pure Function
// Layer: 2 | Category: negative | Covers: use raise in fn with plain T return

// Function with plain return type using raise - should be compile error
fn bad_function() int {
    raise error("not allowed")
}

bad_function()
