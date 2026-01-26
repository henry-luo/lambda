// Test: large user function calls sys func that errors

fn heavy_computation(data) {
    // Lots of code to prevent inlining
    let a1 = len(data) + 1
    let a2 = a1 * 2 + a1 * 3
    let a3 = a2 - a1 + a2 - a1
    let a4 = a3 / 2 + a3 / 3
    let a5 = a4 * a4 + a4 * a4
    let a6 = a5 + a1 + a2 + a3
    let a7 = a6 * 2 + a6 * 3
    let a8 = a7 - a6 + a7 - a6
    let a9 = a8 / 2 + a8 / 3
    let a10 = a9 * a9 + a9 * a9
    let b1 = a10 + a1 + a2 + a3
    let b2 = b1 * 2 + b1 * 3
    let b3 = b2 - b1 + b2 - b1
    let b4 = b3 / 2 + b3 / 3
    let b5 = b4 * b4 + b4 * b4
    let b6 = b5 + b1 + b2 + b3
    let b7 = b6 * 2 + b6 * 3
    let b8 = b7 - b6 + b7 - b6
    let b9 = b8 / 2 + b8 / 3
    let b10 = b9 * b9 + b9 * b9
    
    // Now call error() - a sys func
    error("Computation failed at step " ++ string(b10))
}

fn process_data(items) {
    let x1 = len(items) * 10
    let x2 = x1 + 100
    let x3 = x2 * 2
    let x4 = x3 - x1
    let x5 = x4 / 2
    let result = heavy_computation(items)
    result
}

fn main_entry() {
    let data = [1, 2, 3, 4, 5]
    let y1 = len(data) + 10
    let y2 = y1 * 5
    let y3 = y2 - y1
    process_data(data)
}

main_entry()
