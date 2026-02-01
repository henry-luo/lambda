// Test: nested scope shadowing is allowed
// Same name can be reused in nested scopes (functions, blocks)
// This should NOT be a duplicate definition error

"=== Variable Shadowing in Functions ==="

let x = 1
let y = 2

fn shadow_x() int {
    let x = 10  // shadows outer x
    x
}

fn shadow_both() int {
    let x = 100
    let y = 200
    x + y
}

fn use_outer_then_shadow(n: int) int {
    // first use outer x
    let result = x + n
    // then shadow it
    let x = 50
    result + x
}

x                       // 1 - outer unchanged
y                       // 2 - outer unchanged
shadow_x()              // 10
shadow_both()           // 300
use_outer_then_shadow(5) // (1+5) + 50 = 56

"=== Variable Shadowing in Block Expressions ==="

let a = 5
let block1 = (let a = 10, a * 2)        // 20
let block2 = (let a = 100, let b = a + 1, b)  // 101
a                       // 5 - outer unchanged
block1
block2

"=== Nested Function Shadowing ==="

fn outer_fn() int {
    let z = 1
    fn inner_fn() int {
        let z = 10  // shadows outer_fn's z
        z
    }
    z + inner_fn()  // 1 + 10 = 11
}
outer_fn()

"=== For Loop Variable Shadowing ==="

let i = 999
for i in 1 to 3 { i * 10 }  // loop var shadows outer i
i  // 999 - outer unchanged after loop

"=== Multiple Levels of Shadowing ==="

let level = 0

fn level1() int {
    let level = 1
    fn level2() int {
        let level = 2
        level
    }
    level + level2()  // 1 + 2 = 3
}

level       // 0
level1()    // 3

"All nested shadowing tests passed!"
