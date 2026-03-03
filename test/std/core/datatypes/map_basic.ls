// Test: Map Basic Operations
// Layer: 3 | Category: datatype | Covers: map literal, access, length

{}
{x: 42}
{a: 1, b: 2, c: 3}
type({a: 1})
{a: 1} is map
let m = {name: "Alice", age: 30}
m.name
m.age
let m2 = {a: 1}
m2.b
m2.x.y.z
len({})
len({a: 1, b: 2, c: 3})
{outer: {inner: 42}}
