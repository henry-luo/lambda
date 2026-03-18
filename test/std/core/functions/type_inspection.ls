// Test: Type Inspection
// Layer: 1 | Category: function | Covers: type(), name(), len()

// ===== type() =====
type(42)
type(3.14)
type("hello")
type(true)
type(null)
type([1, 2])
type({a: 1})
type(<div>)
type([1, 2])
type(1 to 5)
type('hello')

// ===== name() =====
name(<div>)
name(<p; "hello">)
name(<span class: "test">)

// ===== len() =====
len("hello")
len([1, 2, 3])
len([1, 2, 3])
len({a: 1, b: 2})
len("")
len([])
len({})

// ===== Method-style =====
42.type()
"hello".type()
"hello".len()
[1, 2, 3].len()
