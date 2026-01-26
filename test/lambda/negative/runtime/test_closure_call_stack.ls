// Test runtime error with closure call stack

// Create closures that call each other
let outer_val = 100

let level5 = fn(x) { 
    // Try to call x as a function - will fail if x is not a function
    x(1, 2)
}

let level4 = fn(x) { 
    level5(x) 
}

let level3 = fn(x) { 
    level4(x) 
}

let level2 = fn(x) { 
    level3(x) 
}

let level1 = fn(x) { 
    level2(x) 
}

// Call chain with a non-function (integer 42)
level1(42)
