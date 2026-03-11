// Test: Cross-Type Object
// Layer: 3 | Category: boundary | Covers: object is nominal, object vs map, object in collection

// ===== Nominal typing - different types not equal =====
type TypeA { value: int }
type TypeB { value: int }
let a = <TypeA value: 42>
let b = <TypeB value: 42>
a is TypeA
a is TypeB
b is TypeB
b is TypeA

// ===== Object vs map =====
let obj = <TypeA value: 42>
let m = {value: 42}
obj is TypeA
obj is map
m is map
m is TypeA

// ===== Object in collection =====
type Item { id: int, name: string }
let items = [
    <Item id: 1, name: "one">,
    <Item id: 2, name: "two">,
    <Item id: 3, name: "three">
]
len(items)
items[0] is Item
items[0].name

// ===== Object in map value =====
let registry = {
    first: <Item id: 1, name: "one">,
    second: <Item id: 2, name: "two">
}
registry.first.name
registry.second is Item

// ===== Inheritance is check =====
type Base { x: int }
type Derived : Base { y: int }
let d = <Derived x: 1, y: 2>
d is Derived
d is Base
d is map

// ===== Object member access like map =====
let obj2 = <TypeA value: 99>
obj2.value

// ===== Object type() =====
type(a)
type(m)

// ===== Object equality =====
let x = <TypeA value: 10>
let y = <TypeA value: 10>
let z = <TypeA value: 20>
x == y
x == z

// ===== Object in filter =====
let filtered = items | filter((item) => item.id > 1)
len(filtered)
filtered[0].name

// ===== Object in map transform =====
items | map((item) => item.name)
