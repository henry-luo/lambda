// Test runtime error with deep call stack (10 levels)

fn level10(x) {
    // Try to call x as a function - will fail if x is not a function
    x(1, 2)
}

fn level9(x) { level10(x) }
fn level8(x) { level9(x) }
fn level7(x) { level8(x) }
fn level6(x) { level7(x) }
fn level5(x) { level6(x) }
fn level4(x) { level5(x) }
fn level3(x) { level4(x) }
fn level2(x) { level3(x) }
fn level1(x) { level2(x) }

// Call chain: level1 -> level2 -> ... -> level10 -> x(1,2) where x=42
level1(42)
