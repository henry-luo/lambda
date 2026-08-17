// Test: Unhandled Error
// Layer: 2 | Category: negative | Covers: call error-returning fn without postfix ^, handler, or or

fn risky() int^ {
    raise error("something went wrong")
}

// Call without handling - should produce compile error E228
risky()
