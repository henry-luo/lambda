// Test: I/O Pure Functions
// Layer: 2 | Category: function | Covers: input(), format(), exists()

// ===== format() with options =====
format(42)
format(3.14)
format([1, 2, 3])
format({a: 1, b: 2})

// ===== exists() =====
exists(/etc)
exists(/this_path_does_not_exist)

// ===== Type string representation =====
string(42)
string(3.14)
string(true)
string(null)
string([1, 2, 3])
string({a: 1})
string(<div>)

// ===== len() on various types =====
len("hello")
len([1, 2, 3])
len({a: 1, b: 2})
