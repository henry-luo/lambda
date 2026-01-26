// Map edge cases
// Tests empty maps, single entries, special keys, deep nesting

// Empty map
let empty1 = {}
let empty2 = len({})

// Single entry
let single1 = {a: 1}
let single2 = {x: {y: {z: 42}}}

// Deep nesting
let deep = {
    a: {
        b: {
            c: {
                d: {
                    e: {
                        f: 42
                    }
                }
            }
        }
    }
}

// Special key names (if supported)
let special_keys = {
    x: 1,
    y: 2,
    z: 3
}

// Different value types
let value_types = {
    null_val: null,
    bool_val: true,
    int_val: 42,
    float_val: 3.14,
    str_val: "hello",
    arr_val: [1, 2, 3],
    map_val: {nested: "yes"},
    fn_val: (x) => x + 1
}

// Map operations
let access1 = single1.a
let access2 = deep.a.b.c.d.e.f
let access3 = value_types.arr_val[0]
let access4 = value_types.fn_val(10)

// Nested maps with arrays
let complex = {
    users: [
        {name: "Alice", age: 30, tags: ["admin", "user"]},
        {name: "Bob", age: 25, tags: ["user"]}
    ],
    settings: {
        theme: "dark",
        notifications: {
            email: true,
            push: false
        }
    }
}

let test1 = complex.users[0].name
let test2 = complex.settings.notifications.email
let test3 = len(complex.users[1].tags)

// Map with function values
let ops = {
    add: (a, b) => a + b,
    mul: (a, b) => a * b,
    compose: (f, g) => (x) => g(f(x))
}

let test4 = ops.add(5, 3)
let test5 = ops.mul(4, 7)

[access1, access2, test1, test2, test3, test4, test5]
