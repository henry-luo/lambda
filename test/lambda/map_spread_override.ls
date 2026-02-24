// Test map spread with field override
// Explicit key:value should override same-named field from spread

// Basic override: ++ in map spread
let info = {name: "test", items: [1, 2, 3]}
let new_info = {info, items: info.items ++ [4]}
new_info.items
[new_info.name]

// Override with simple value
let m1 = {name: "a", count: 10}
let m2 = {m1, name: "b"}
[m2.name]
m2.count

// Non-overlapping field (fallback to spread)
let m3 = {m1, extra: "new"}
[m3.name]
m3.count
[m3.extra]

// Override via function return
fn add_item(m, item) {
    {m, items: m.items ++ [item]}
}
let r1 = add_item(info, 4)
r1.items

// Chained updates
let r2 = add_item(r1, 5)
r2.items

// Multiple overrides in single spread
let m4 = {name: "x", a: 1, b: 2}
let m5 = {m4, a: 10, b: 20}
[m5.name]
m5.a
m5.b

// Override with computed expression
let m6 = {m1, count: m1.count + 5}
m6.count

// Order matters: spread AFTER explicit → spread wins (last-writer-wins)
let m7 = {name: "first", info}
[m7.name]
m7.items
