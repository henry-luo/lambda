// Test: sized numeric types in collections and data structures

// ===== Arrays with sized types =====
"=== arrays ==="
let a1 = [1i8, 2i8, 3i8]
a1
len(a1)
a1[0]
a1[1] + a1[2]

let a2 = [100u16, 200u16, 300u16]
a2
len(a2)

let a3 = [1.5f32, 2.5f32, 3.5f32]
a3
len(a3)

// mixed sized types in array
let a4 = [1i8, 200u16, 3.14f32]
a4
len(a4)

// u64 in array
let a5 = [100u64, 200u64, 300u64]
a5
len(a5)

// ===== Array operations with sized types =====
"=== array ops ==="
let arr = [10i8, 20i8, 30i8, 40i8, 50i8]
len(arr)

// map over sized array: promotes to standard int
arr | ~ * 2

// filter sized array
arr that ~ == 30

// concatenation
[1i8, 2i8] ++ [3i8, 4i8]

// ===== Maps with sized values (stored as promoted values) =====
"=== map with sized ==="
let m = {x: int(10i8), y: int(20i8)}
m.x
m.y
m.x + m.y

// ===== Sized types as element content =====
"=== element content ==="
let e1 = <data; 42i8>
e1[0]

let e2 = <values; [1i8, 2i8, 3i8]>
len(e2[0])
e2[0][0]
e2[0][1]
e2[0][2]

// ===== Nested arrays =====
"=== nested arrays ==="
let matrix = [[1i8, 2i8], [3i8, 4i8]]
len(matrix)
matrix[0]
matrix[1]
matrix[0][0] + matrix[1][1]

// ===== Array with mixed standard and sized =====
"=== mixed array ==="
let mixed = [1i8, 2, 3.14, 100u64]
mixed
len(mixed)
type(mixed[0])
1
type(mixed[3])
