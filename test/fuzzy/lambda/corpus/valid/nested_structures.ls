// Complex nested data structures
// Tests deep heterogeneous nesting and composition

// Deep nested maps
let nested_map = {
    level1: {
        level2: {
            level3: {
                level4: {
                    value: 42
                }
            }
        }
    }
}

// Mixed collections
let mixed = [
    {a: [1, 2, 3], b: {x: 10, y: 20}},
    [{c: 5}, [6, 7, 8]],
    [[["deep"]]]
]

// Arrays of maps of arrays
let complex = [
    {data: [1, 2, 3], meta: {tags: ["a", "b"], count: 2}},
    {data: [4, 5, 6], meta: {tags: ["c", "d"], count: 2}}
]

// Nested function structures
let ops = {
    add: (a, b) => a + b,
    mul: (a, b) => a * b,
    compose: (f, g) => (x) => g(f(x))
}

// Use composed structures
let result1 = nested_map.level1.level2.level3.level4.value
let result2 = mixed[0].a[1] + mixed[1][0].c
let result3 = ops.compose(ops.add, ops.mul)(2, 3)

[result1, result2, result3]
