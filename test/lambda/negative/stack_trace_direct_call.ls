// Test: stack trace with ONLY direct Lambda function calls (no closures)
// Functions are made large to prevent MIR inlining (threshold is ~50 instructions)

fn boom() {
    let a = 1 + 2 + 3 + 4 + 5
    let b = a * 2 + a * 3 + a * 4
    let c = b - a + b - a + b - a
    let d = c / 2 + c / 3 + c / 4
    let e = d * d + d * d + d * d
    let f = e + a + b + c + d + e
    let g = f * 2 + f * 3 + f * 4 + f * 5
    let h = g - f + g - f + g - f + g - f
    let i = h / 2 + h / 3 + h / 4 + h / 5
    let j = i + a + b + c + d + e + f + g + h
    error("Test error from boom")
}

fn level1() {
    let a = 10 + 20 + 30 + 40 + 50
    let b = a * 2 + a * 3 + a * 4
    let c = b - a + b - a + b - a
    let d = c / 2 + c / 3 + c / 4
    let e = d * d + d * d + d * d
    let f = e + a + b + c + d + e
    let g = f * 2 + f * 3 + f * 4 + f * 5
    let h = g - f + g - f + g - f + g - f
    let result = boom()
    result
}

fn level2() {
    let a = 100 + 200 + 300 + 400 + 500
    let b = a * 2 + a * 3 + a * 4
    let c = b - a + b - a + b - a
    let d = c / 2 + c / 3 + c / 4
    let e = d * d + d * d + d * d
    let f = e + a + b + c + d + e
    let g = f * 2 + f * 3 + f * 4 + f * 5
    let h = g - f + g - f + g - f + g - f
    let result = level1()
    result
}

fn level3() {
    let a = 1000 + 2000 + 3000 + 4000 + 5000
    let b = a * 2 + a * 3 + a * 4
    let c = b - a + b - a + b - a
    let d = c / 2 + c / 3 + c / 4
    let e = d * d + d * d + d * d
    let f = e + a + b + c + d + e
    let g = f * 2 + f * 3 + f * 4 + f * 5
    let h = g - f + g - f + g - f + g - f
    let result = level2()
    result
}

// Direct call chain: main -> level3 -> level2 -> level1 -> boom -> error
level3()
