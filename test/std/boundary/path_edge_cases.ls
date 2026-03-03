// Test: Path Edge Cases
// Layer: 3 | Category: boundary | Covers: special chars, long paths, various schemes

// ===== Simple relative path =====
let p1 = /hello/world
type(p1)

// ===== Root path =====
let p2 = /
type(p2)

// ===== HTTP scheme =====
let p3 = /http://example.com/path
p3

// ===== HTTPS scheme =====
let p4 = /https://example.com/api/v1
p4

// ===== Path with multiple segments =====
let p5 = /a/b/c/d/e/f/g
p5

// ===== Path with wildcards =====
let p6 = /data/*/items
p6

// ===== Path with globstar =====
let p7 = /root/**/leaf
p7

// ===== Quoted segment =====
let p8 = /"hello world"/items
p8

// ===== Dynamic segment =====
let name = "users"
let p9 = /(name)/list
p9

// ===== Path equality =====
/a/b == /a/b
/a/b == /a/c

// ===== Path in collection =====
let paths = [/a, /b, /c]
len(paths)

// ===== Path type check =====
/test is path
